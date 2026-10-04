# SPDX-License-Identifier: GPL-3.0-only
"""EOS reliable ordered packets adapted to the existing bounded netplay stream.

The dedicated owner thread ticks EOS even when the game broker waits on sockets.
No Epic Auth, browser sign-in, overlay, or credential logging is used.
"""
from concurrent.futures import Future
import ctypes
import json
import os
from pathlib import Path
import queue
import sys
import threading
import time

MAX_BUFFER = 8 * 1024 * 1024
PACKET_SIZE = 1024  # Below the SDK's 1170-byte packet maximum.
FIELDS = ('product_id', 'sandbox_id', 'deployment_id', 'client_id', 'client_secret')


def locations():
    root = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
    packaged = root / 'payload/eos'
    local = Path(__file__).resolve().parent / 'eos-local'
    override = os.environ.get('RESORT_EOS_CONFIG')
    config = Path(override).expanduser() if override else (
        packaged / 'client.json' if (packaged / 'client.json').is_file()
        else Path(__file__).resolve().parent.parent / 'work/eos/client.json')
    name = 'riisorted-eos.dll' if sys.platform == 'win32' else 'libriisorted-eos.so'
    library = packaged / name
    if not library.is_file(): library = local / name
    return library, config


def available():
    library, config = locations()
    return library.is_file() and config.is_file()


def configuration():
    library, path = locations()
    if not library.is_file() or not path.is_file():
        raise ValueError('EOS is not configured. Build scripts/build_eos.py and provide the local EOS client configuration.')
    try:
        data = json.loads(path.read_text())
        if any(not isinstance(data.get(key), str) or not data[key] or len(data[key]) > 256 for key in FIELDS):
            raise ValueError()
        for key in FIELDS[:3]:
            if len(data[key]) != 32 or any(c not in '0123456789abcdef' for c in data[key]): raise ValueError()
    except (ValueError, OSError):
        raise ValueError('Invalid EOS client configuration. Check the five credential fields locally.') from None
    return library, data


class EosStream:
    """One owner thread and one broker reader/writer; close is thread-safe."""
    def __init__(self, cache, cancelled, log, force_relay=False):
        self.library, self.config = configuration()
        cache = Path(cache); cache.mkdir(parents=True, exist_ok=True)
        self.cache = str(cache.resolve())
        self.cancelled, self.log, self.force_relay = cancelled, log, force_relay
        self.stopped = threading.Event()
        self.condition = threading.Condition()
        self.commands = queue.Queue(maxsize=32)
        self.buffer = bytearray()
        self.error = None
        self.timeout = 20
        self.initialized = Future()
        self.thread = threading.Thread(target=self._loop, name='Riisorted EOS', daemon=True)
        self.thread.start()
        try:
            self._wait(self.initialized, 30)
        except Exception:
            self.finish()
            raise

    def _wait(self, future, timeout):
        deadline = None if timeout is None else time.monotonic() + timeout
        while not future.done():
            if self.cancelled.is_set() or self.stopped.is_set(): raise ConnectionAbortedError('EOS session cancelled.')
            if self.error: raise ConnectionError(self.error)
            if deadline is not None and time.monotonic() >= deadline: raise TimeoutError('EOS operation timed out.')
            try:
                # Future completion wakes immediately; do not add a fixed
                # polling delay to every gameplay input exchange.
                return future.result(timeout=0.1)
            except TimeoutError:
                if future.done(): raise
        return future.result()

    def call(self, name, *args):
        future = Future()
        self.commands.put_nowait((name, args, future))
        return self._wait(future, 30)

    def wait_for(self, name, timeout=120):
        deadline = None if timeout is None else time.monotonic() + timeout
        while not self.call(name):
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError(f'EOS {name.replace("_", " ")} timed out. Check connectivity and client policy permissions.')
            self.stopped.wait(0.02)

    def _loop(self):
        bridge = None
        pending = None
        timer = None
        try:
            if sys.platform == 'win32':
                # Match the 1 ms SDK pump with Windows timer resolution for the
                # lifetime of this transport, then restore it on every exit.
                winmm = ctypes.WinDLL('winmm')
                winmm.timeBeginPeriod.argtypes = [ctypes.c_uint]
                winmm.timeBeginPeriod.restype = ctypes.c_uint
                winmm.timeEndPeriod.argtypes = [ctypes.c_uint]
                winmm.timeEndPeriod.restype = ctypes.c_uint
                if winmm.timeBeginPeriod(1) == 0:
                    timer = winmm
                self._dll_directory = os.add_dll_directory(str(self.library.parent.resolve()))
            api = ctypes.CDLL(str(self.library))
            ptr, string, number = ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int
            create = api.riis_eos_create
            create.argtypes = [string] * 7 + [number]; create.restype = ptr
            for name in ('tick', 'destroy', 'host'):
                fn = getattr(api, 'riis_eos_' + name); fn.argtypes = [ptr]; fn.restype = None
            api.riis_eos_join.argtypes = [ptr, string, string]; api.riis_eos_join.restype = None
            for name in ('error', 'room_id', 'user_id'):
                fn = getattr(api, 'riis_eos_' + name); fn.argtypes = [ptr]; fn.restype = string
            for name in ('login_ready', 'room_ready', 'select_peer', 'network'):
                fn = getattr(api, 'riis_eos_' + name); fn.argtypes = [ptr]; fn.restype = number
            for name in ('send', 'receive'):
                fn = getattr(api, 'riis_eos_' + name); fn.argtypes = [ptr, ptr, ctypes.c_uint32]; fn.restype = number
            credentials = [self.config[key].encode() for key in FIELDS]
            bridge = create(*credentials, self.cache.encode(), b'Riisorted Player', int(self.force_relay))
            if not bridge: raise RuntimeError('EOS initialization failed. Check SDK dependencies and the local client configuration.')
            self.initialized.set_result(True)
            storage = ctypes.create_string_buffer(2048)
            last_network = 0
            while not self.stopped.is_set() and not self.cancelled.is_set():
                api.riis_eos_tick(bridge)
                failure = api.riis_eos_error(bridge)
                if failure: raise RuntimeError(failure.decode())
                network = api.riis_eos_network(bridge)
                if network and network != last_network:
                    self.log('EOS P2P connected via ' + ('Epic relay.' if network == 2 else 'a direct connection.'))
                    last_network = network
                for _ in range(128 if api.riis_eos_login_ready(bridge) else 0):
                    size = api.riis_eos_receive(bridge, storage, len(storage))
                    if size < 0: raise RuntimeError(api.riis_eos_error(bridge).decode())
                    if not size: break
                    with self.condition:
                        if len(self.buffer) + size > MAX_BUFFER: raise ValueError('EOS receive buffer exceeded its limit.')
                        self.buffer.extend(storage.raw[:size]); self.condition.notify_all()
                if pending is None:
                    try:
                        name, args, future = self.commands.get_nowait()
                    except queue.Empty:
                        pass
                    else:
                        if name == 'sendall': pending = (args[0], 0, future)
                        else:
                            fn = getattr(api, 'riis_eos_' + name)
                            result = fn(bridge, *[arg.encode() if isinstance(arg, str) else arg for arg in args])
                            future.set_result(result.decode() if isinstance(result, bytes) else result)
                if pending:
                    data, offset, future = pending
                    for _ in range(64):
                        packet = data[offset:offset + PACKET_SIZE]
                        result = api.riis_eos_send(bridge, packet, len(packet))
                        if result < 0: raise RuntimeError(api.riis_eos_error(bridge).decode() or 'EOS packet send failed.')
                        if not result: break  # Backpressure; tick and drain receives before retry.
                        offset += len(packet)
                        if offset == len(data): future.set_result(None); pending = None; break
                    if pending: pending = (data, offset, future)
                self.stopped.wait(0.001)
        except Exception as error:
            # SDK result names only; never serialize configuration/credentials.
            self.error = str(error)
            if not self.initialized.done(): self.initialized.set_exception(error)
        finally:
            if pending and not pending[2].done(): pending[2].set_exception(ConnectionError('EOS transport stopped.'))
            while not self.commands.empty():
                _, _, future = self.commands.get_nowait()
                future.set_exception(ConnectionError('EOS transport stopped.'))
            with self.condition: self.condition.notify_all()
            if bridge: api.riis_eos_destroy(bridge)
            if hasattr(self, '_dll_directory'):
                self._dll_directory.close()
            if timer is not None:
                timer.timeEndPeriod(1)
            self.stopped.set()

    def sendall(self, data):
        if not data or len(data) > MAX_BUFFER: raise ValueError('Invalid EOS send size.')
        future = Future()
        self.commands.put_nowait(('sendall', (bytes(data),), future))
        self._wait(future, self.timeout)

    def recv(self, size):
        deadline = time.monotonic() + self.timeout
        with self.condition:
            while not self.buffer:
                if self.error: raise ConnectionError(self.error)
                if self.stopped.is_set() or self.cancelled.is_set(): raise ConnectionAbortedError('EOS session stopped.')
                remaining = deadline - time.monotonic()
                if remaining <= 0: raise TimeoutError('The other player stopped sending EOS data.')
                self.condition.wait(min(remaining, 0.1))
            data = bytes(self.buffer[:size]); del self.buffer[:size]; return data

    def settimeout(self, timeout): self.timeout = timeout
    def shutdown(self, _): self.close()
    def close(self):
        self.stopped.set()
        with self.condition: self.condition.notify_all()
    def finish(self):
        self.close(); self.thread.join(timeout=5)
