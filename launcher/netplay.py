# SPDX-License-Identifier: GPL-3.0-only
"""Experimental two-player direct TLS netplay and the local native input broker.

No game/system artwork is transferred. Session saves never replace personal saves.
"""
from __future__ import annotations
import base64
import errno
import hashlib
import json
import os
from pathlib import Path
import platform
import math
import queue
import secrets
import shutil
import socket
import ssl
import struct
import subprocess
import threading
import time
import tomllib
import zlib

from mii_data import Database, checksum, validate_record
from runtime_identity import simulation_identity, verify_local_release

VERSION = 5  # Session compatibility and invitation schema.
MOTION_VERSION = 1  # Native motion packet format remains unchanged.
MAX_PACKET = 4 * 1024 * 1024
MAX_MOTION = 24 + 128 * 181
FILES = {
    'title/00010004/525a5450/data/Sports2.dat': 1024 * 1024,
    'title/00010004/525a5450/data/banner.bin': 128 * 1024,
    'title/00000001/00000002/data/setting.txt': 256,
    'shared2/menu/FaceLib/RFL_DB.dat': 779968,
}
DB = 'shared2/menu/FaceLib/RFL_DB.dat'
ART = 'shared2/menu/FaceLib/RFL_Res.dat'
GAME_EXECUTABLE = 'Resortcompiled.exe' if platform.system() == 'Windows' else 'Resortcompiled'
ASSETS = (GAME_EXECUTABLE, 'dsp_coef.bin', 'font_western.bin', 'font_japanese.bin',
          'cacert.pem', 'wii_bootstrap')


def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def tree_hash(root):
    files = {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
    return hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()


def exact(sock, size):
    result = bytearray()
    while len(result) < size:
        block = sock.recv(size - len(result))
        if not block:
            raise ConnectionError('The other player disconnected.')
        result.extend(block)
    return bytes(result)


def receive(sock, limit=MAX_PACKET):
    size, = struct.unpack('>I', exact(sock, 4))
    if not 0 < size <= limit:
        raise ValueError('Invalid online message size.')
    result = exact(sock, size)
    if result[:1] == b'E':
        raise ConnectionError(result[1:].decode('utf-8', errors='replace'))
    return result


def send(sock, data):
    if not 0 < len(data) <= MAX_PACKET:
        raise ValueError('Online message exceeds the size limit.')
    sock.sendall(struct.pack('>I', len(data)) + data)


def send_json(sock, data):
    send(sock, b'J' + json.dumps(data, allow_nan=False).encode('utf-8'))


def receive_json(sock):
    data = receive(sock)
    if data[:1] != b'J':
        raise ValueError('Expected online setup data.')
    result = json.loads(data[1:])
    if not isinstance(result, dict):
        raise ValueError('Invalid online setup data.')
    return result


def toml_document(data):
    lines = []
    for section, values in data.items():
        if not isinstance(values, dict) or not section.replace('_', '').isalnum():
            raise ValueError('Unsupported session configuration section.')
        lines.append(f'[{section}]')
        for key, value in values.items():
            if not key.replace('_', '').isalnum() or isinstance(value, dict):
                raise ValueError('Unsupported session configuration field.')
            lines.append(f'{key} = {json.dumps(value, allow_nan=False)}')
        lines.append('')
    result = '\n'.join(lines)
    tomllib.loads(result)
    return result


class OnlineSession:
    def __init__(self, source, folder, log=print, invitation=lambda code: None):
        self.source = Path(source).resolve()
        self.folder = Path(folder).resolve()
        self.log = log
        self.invitation = invitation
        self.cancelled = threading.Event()
        self.sockets = []
        self.child = None
        self.role = None
        self.transport = 'direct'
        self.force_relay = False
        self.environment = os.environ.copy()

    def track(self, sock):
        self.sockets.append(sock)
        sock.settimeout(20)
        return sock

    def cancel(self):
        self.cancelled.set()
        for sock in list(self.sockets):
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
        if self.child and self.child.poll() is None:
            self.child.terminate()

    def prepare(self):
        if platform.system() not in ('Linux', 'Windows') or platform.machine() not in ('x86_64', 'AMD64'):
            raise ValueError('Online play supports Linux and Windows x86-64.')
        if not (self.source / 'portable.txt').is_file():
            raise ValueError('Choose a portable runtime installation.')
        verify_local_release(self.source)
        simulation = simulation_identity(self.source / GAME_EXECUTABLE)
        userdata = self.source / 'UserData'
        self.config = tomllib.loads((userdata / 'Config.toml').read_text(encoding='utf-8-sig'))
        paths = self.config.get('paths', {})
        def resolve(value):
            path = Path(value)
            return (path if path.is_absolute() else userdata / path).resolve()
        self.nand = resolve(paths.get('nand_root') or 'NAND')
        dvd = resolve(paths.get('dvd_root') or '')
        if not (dvd / 'sys/main.dol').is_file():
            raise ValueError('Configure an extracted Wii Sports Resort game before hosting/joining.')
        overlays = [resolve(value) for value in paths.get('overlay_roots', [])]
        if paths.get('retro_rewind_root'):
            raise ValueError('Legacy mod roots are unsupported in online sessions.')
        if any(not path.is_dir() for path in overlays):
            raise ValueError('An overlay folder is missing.')
        self.local_paths = {'dvd_root': str(dvd), 'overlay_roots': [str(p) for p in overlays], 'nand_root': 'NAND'}
        self.folder.mkdir(parents=True, exist_ok=False)
        self.run = self.folder / 'game'
        self.run.mkdir()
        # Execute the installed binary in place. Only mutable state belongs to
        # a session; DLLs, bootstrap content and fonts are shared with the install.
        libraries = sorted({p.name for pattern in ('*.dll', '*.so', '*.so.*')
                            for p in self.source.glob(pattern) if p.is_file()})
        (self.run / 'UserData/NAND').mkdir(parents=True)
        self.log('Checking runtime, artwork and game content…')
        self.compat = {
            'version': VERSION, 'simulation': simulation,
            'game': tree_hash(dvd), 'overlays': [tree_hash(p) for p in overlays],
            'artwork': sha(self.nand / ART),
            'assets': {name: tree_hash(self.source / name) if (self.source / name).is_dir() else sha(self.source / name)
                       for name in ASSETS if name not in (GAME_EXECUTABLE, 'cacert.pem') and (self.source / name).exists()},
            'motion_conventions': [self.environment.get('RESORT_MPLS_SIGNS', '1,1,1'),
                                   self.environment.get('RESORT_MPLS_DIRSIGN', '1')],
        }
        # Binary integrity is local: ELF and PE libraries deliberately differ.
        (self.folder / 'platform-binaries.json').write_text(json.dumps({
            'platform': platform.system(), 'simulation': simulation,
            'executable': sha(self.source / GAME_EXECUTABLE),
            'libraries': {name: sha(self.source / name) for name in libraries},
        }, indent=2), encoding='utf-8')
        selected = getattr(self, 'selected_slots', None)
        self.miis = [record for slot, record in Database(self.nand / DB).records()
                     if selected is None or slot in selected]
        if (userdata / 'keyboard_bindings.dat').exists():
            shutil.copy2(userdata / 'keyboard_bindings.dat', self.run / 'UserData/keyboard_bindings.dat')

    def compatible(self, received):
        if not isinstance(received, dict):
            raise ValueError('Invalid compatibility manifest.')
        if received != self.compat:
            differences = [key for key, value in self.compat.items() if received.get(key) != value]
            raise ValueError('Online compatibility mismatch: ' + ', '.join(differences))

    def baseline(self, guest_miis):
        nand = self.run / 'UserData/NAND'
        for name, limit in FILES.items():
            source = self.nand / name
            if source.is_file():
                if source.stat().st_size > limit:
                    raise ValueError(f'Session file exceeds allowed size: {name}')
                (nand / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, nand / name)
        database = Database(nand / DB)
        identities = {record[24:32]: record for _, record in database.records()}
        added = 0
        if not isinstance(guest_miis, list) or len(guest_miis) > 100:
            raise ValueError('Invalid guest Mii collection.')
        for encoded in guest_miis:
            if not isinstance(encoded, str) or len(encoded) > 104:
                raise ValueError('Invalid guest Mii record size.')
            record = base64.b64decode(encoded, validate=True)
            validate_record(record)
            identity = record[24:32]
            if identity in identities:
                if identities[identity] != record:
                    raise ValueError('A guest Mii has the same identity as a different host Mii. Resolve the conflict before joining.')
                continue
            database.put(record)
            identities[identity] = record
            added += 1
        checksum(database.data)
        (nand / DB).write_bytes(database.data)
        self.log(f'Shared session collection ready: {len(identities)} Miis ({added} added).')
        return {name: base64.b64encode((nand / name).read_bytes()).decode()
                for name in FILES if (nand / name).exists()}

    def install_baseline(self, files, config):
        if not isinstance(files, dict) or not set(files).issubset(FILES) or DB not in files:
            raise ValueError('Invalid host session file list.')
        if not isinstance(config, dict):
            raise ValueError('Invalid host configuration.')
        nand = self.run / 'UserData/NAND'
        for name, encoded in files.items():
            if not isinstance(encoded, str) or len(encoded) > (FILES[name] + 2) // 3 * 4:
                raise ValueError('Host session file exceeds its size limit.')
            data = base64.b64decode(encoded, validate=True)
            if len(data) > FILES[name]:
                raise ValueError('Host session file exceeds its size limit.')
            (nand / name).parent.mkdir(parents=True, exist_ok=True)
            (nand / name).write_bytes(data)
        Database(nand / DB)
        (nand / ART).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.nand / ART, nand / ART)
        # Game-affecting settings are shared. Filesystem paths and personal
        # keyboard bindings stay local, and real remote discovery is disabled.
        common = json.loads(json.dumps(config))
        common['paths'] = self.local_paths
        common.setdefault('controller', {})['wii_remotes'] = False
        common.setdefault('network', {})['enabled'] = False
        common.setdefault('discord', {})['enabled'] = False
        common.setdefault('video', {})['frame_interpolation_fps'] = 0
        (self.run / 'UserData/Config.toml').write_text(toml_document(common), encoding='utf-8')

    def connect(self, role, address, port, code):
        self.role = role
        if self.transport == 'eos':
            return self.connect_eos(role, code)
        if role == 'host':
            if not shutil.which('openssl'):
                raise ValueError('Hosting this development build needs the openssl command.')
            key, cert = self.folder / 'key.pem', self.folder / 'cert.pem'
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                            '-subj', '/CN=Riisorted session', '-keyout', str(key), '-out', str(cert)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           env=self.environment)
            key.chmod(0o600)
            fingerprint = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()
            secret = secrets.token_hex(32)
            invite = base64.urlsafe_b64encode(json.dumps({'v': VERSION, 'port': port,
                            'fingerprint': fingerprint, 'secret': secret}).encode()).decode()
            listener = self.track(socket.socket(socket.AF_INET, socket.SOCK_STREAM))
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((address, port))
            listener.listen(1)
            # A lobby stays open until the host leaves. Poll only so cancellation
            # is responsive even when closing a listener does not wake accept().
            listener.settimeout(1)
            self.invitation(invite)
            self.log(f'Listening on {address}:{port}. Waiting until you leave the session…')
            while not self.cancelled.is_set():
                try:
                    raw, remote = listener.accept()
                    break
                except socket.timeout:
                    continue
            else:
                raise ConnectionAbortedError('Session cancelled.')
            self.log(f'Incoming connection from {remote[0]}. Checking invitation…')
            self.track(raw)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_3
            context.load_cert_chain(cert, key)
            peer = self.track(context.wrap_socket(raw, server_side=True))
            hello = receive_json(peer)
            if not secrets.compare_digest(str(hello.get('secret', '')), secret):
                raise ValueError('Invalid session invitation.')
            self.compatible(hello.get('compat', {}))
            files = self.baseline(hello.get('miis'))
            self.install_baseline(files, self.config)
            common = dict(self.config)
            common.pop('paths', None)
            send_json(peer, {'files': files, 'config': common})
            ready = receive_json(peer)
            if ready != {'ready': True}:
                raise ValueError('The guest did not finish session setup.')
            send_json(peer, {'start': True})
        else:
            if len(code) > 2048:
                raise ValueError('Invitation is too long.')
            invite = json.loads(base64.urlsafe_b64decode(code))
            if invite.get('v') != VERSION or not 1 <= int(invite['port']) <= 65535:
                raise ValueError('Invalid session invitation.')
            host_port = int(invite['port'])
            self.log(f'Connecting to {address}:{host_port}…')
            try:
                raw = self.track(socket.create_connection((address, host_port), timeout=20))
            except OSError as error:
                if error.errno in (errno.EHOSTUNREACH, errno.ENETUNREACH):
                    reason = ('No route to host: the operating system could not reach the host, '
                              'or a firewall rejected the connection. On both PCs, check ZeroTier '
                              'authorization and assigned addresses; on the guest check '
                              f'“ip route get {address}” and ping the host. The host firewall '
                              f'must allow TCP {host_port} from the guest’s ZeroTier address.')
                elif error.errno == errno.ECONNREFUSED:
                    reason = ('Connection refused: check that the host lobby is still open, '
                              'the selected network address and invitation are current, and '
                              'the host firewall permits this port.')
                elif isinstance(error, TimeoutError):
                    reason = ('Connection timed out: check both ZeroTier devices are authorized, '
                              'the host address is correct, and the host firewall allows this port.')
                else:
                    raise
                raise ConnectionError(f'{reason}\nEndpoint: {address}:{host_port} ({error})') from error
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.minimum_version = ssl.TLSVersion.TLSv1_3
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            peer = self.track(context.wrap_socket(raw, server_hostname='Riisorted session'))
            # Trust is pinned to the certificate supplied in the invitation,
            # not to an arbitrary self-signed server certificate.
            fingerprint = hashlib.sha256(peer.getpeercert(binary_form=True)).hexdigest()
            if not secrets.compare_digest(fingerprint, str(invite['fingerprint'])):
                raise ValueError('The host certificate does not match the invitation.')
            send_json(peer, {'secret': invite['secret'], 'compat': self.compat,
                             'miis': [base64.b64encode(record).decode() for record in self.miis]})
            setup = receive_json(peer)
            self.install_baseline(setup.get('files'), setup.get('config'))
            send_json(peer, {'ready': True})
            if receive_json(peer) != {'start': True}:
                raise ValueError('Invalid session start message.')
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.log('Connected. Host save and shared Miis are ready; launching both games.')
        return peer

    def connect_eos(self, role, code):
        from eos_transport import EosStream
        self.log('Starting EOS device login; no Epic account sign-in is needed…')
        peer = EosStream(self.folder.parent / 'EOSCache', self.cancelled, self.log, self.force_relay)
        self.track(peer)
        peer.wait_for('login_ready')
        self.log('EOS device identity is ready.')
        if role == 'host':
            peer.call('host'); peer.wait_for('room_ready')
            secret = secrets.token_hex(32)
            invite = {'v': VERSION, 'transport': 'eos', 'lobby': peer.call('room_id'),
                      'owner': peer.call('user_id'), 'secret': secret,
                      'product': peer.config['product_id'], 'sandbox': peer.config['sandbox_id'],
                      'deployment': peer.config['deployment_id']}
            code = 'riis-eos1:' + base64.urlsafe_b64encode(json.dumps(invite).encode()).decode()
            self.invitation(code)
            self.log('EOS lobby open. Waiting until you leave the session; share the invitation, no IP address required.')
            peer.wait_for('select_peer', timeout=None)
        else:
            try:
                if len(code) > 2048 or not code.startswith('riis-eos1:'): raise ValueError()
                invite = json.loads(base64.b64decode(code[10:], altchars=b'-_', validate=True))
                if invite.get('v') != VERSION or invite.get('transport') != 'eos': raise ValueError()
                for key in ('lobby', 'owner', 'secret'):
                    if not isinstance(invite.get(key), str) or not 1 <= len(invite[key]) <= 128: raise ValueError()
                if len(invite['secret']) != 64 or any(c not in '0123456789abcdef' for c in invite['secret']): raise ValueError()
            except (ValueError, TypeError, KeyError):
                raise ValueError('Invalid EOS invitation. Paste the complete code from the current host lobby.') from None
            if any(invite.get(key) != peer.config[key + '_id'] for key in ('product', 'sandbox', 'deployment')):
                raise ValueError('The invitation targets a different EOS product or deployment. Use the same release on both PCs.')
            peer.call('join', invite['lobby'], invite['owner']); peer.wait_for('room_ready')
            peer.wait_for('select_peer')
            secret = invite['secret']
        # Prove bidirectional packet delivery before transferring personal data.
        # The secret is in the shared invitation, never in lobby attributes or logs.
        if role == 'join':
            send_json(peer, {'eos_probe': VERSION, 'secret': secret})
            if receive_json(peer) != {'eos_probe': VERSION, 'accepted': True}:
                raise ValueError('EOS host probe response was invalid.')
        else:
            probe = receive_json(peer)
            if probe.get('eos_probe') != VERSION or not secrets.compare_digest(str(probe.get('secret', '')), secret):
                raise ValueError('EOS peer invitation proof failed. No saves or Miis were transferred.')
            send_json(peer, {'eos_probe': VERSION, 'accepted': True})
        self.log('EOS P2P probe succeeded in both directions. Checking game compatibility…')
        if role == 'host':
            hello = receive_json(peer)
            self.compatible(hello.get('compat', {}))
            files = self.baseline(hello.get('miis'))
            self.install_baseline(files, self.config)
            common = dict(self.config); common.pop('paths', None)
            send_json(peer, {'files': files, 'config': common})
            if receive_json(peer) != {'ready': True}: raise ValueError('Guest setup did not complete.')
            send_json(peer, {'start': True})
        else:
            send_json(peer, {'compat': self.compat,
                'miis': [base64.b64encode(record).decode() for record in self.miis]})
            setup = receive_json(peer)
            self.install_baseline(setup.get('files'), setup.get('config'))
            send_json(peer, {'ready': True})
            if receive_json(peer) != {'start': True}: raise ValueError('Invalid EOS start message.')
        self.log('Connected through EOS. Host save and shared Miis are ready; launching both games.')
        return peer

    def validate_inputs(self, first, second, sequence):
        batches = []
        for channel, message in enumerate((first, second)):
            if message[:1] != b'I' or not 11 + 24 < len(message) <= 11 + MAX_MOTION:
                raise ValueError('Invalid controller packet.')
            batch = message[11:]
            version, seq, frame, assigned, mode, recenter, count = struct.unpack('>IQQBBBB', batch[:24])
            if (version != MOTION_VERSION or seq != sequence or assigned != channel or mode > 5 or
                recenter > 1 or not 1 <= count <= 128 or len(batch) != 24 + count * 181):
                raise ValueError(f'Input sequence/channel mismatch at interval {sequence}.')
            if mode != message[1 + channel]:
                raise ValueError('Invalid controller MotionPlus mode.')
            batches.append((batch, frame))
        differences = []
        if batches[0][1] != batches[1][1]: differences.append('render frame')
        if first[1:3] != second[1:3]: differences.append('requested MotionPlus modes')
        if first[3:11] != second[3:11]: differences.append('previous solver output')
        if differences:
            # Save only the failed boundary, not a continuous input trace. No
            # invitation, authentication secret or personal save is included.
            details = {'interval': sequence, 'differences': differences}
            for name, message, (_, frame) in zip(('host', 'guest'), (first, second), batches):
                details[name] = {'frame': frame, 'modes': list(message[1:3]),
                                 'solver': message[3:11].hex(),
                                 'samples': message[34],
                                 'capture': base64.b64encode(message[11:]).decode()}
            if self.folder.is_dir():
                (self.folder / 'desync.json').write_text(json.dumps(details, indent=2), encoding='utf-8')
            values = '; '.join(f'{name}: frame={details[name]["frame"]}, modes={details[name]["modes"]}, solver={details[name]["solver"]}'
                               for name in ('host', 'guest'))
            raise ValueError(f'Synchronization disagreement at interval {sequence}: {", ".join(differences)} differs. {values}. Failed boundary saved in desync.json.')
        return b'P' + struct.pack('>I', len(batches[0][0])) + batches[0][0] + batches[1][0]

    def input_delay(self, peer):
        # Fixed for the session: changing the delay mid-game would skip/repeat
        # inputs. Measure both directions before releasing either native boot.
        if self.role == 'host':
            timings = []
            for index in range(6):
                started = time.monotonic()
                send_json(peer, {'latency_probe': index})
                if receive_json(peer) != {'latency_probe': index}:
                    raise ValueError('Invalid latency probe response.')
                timings.append(time.monotonic() - started)
            rtt = max(timings[1:])
            delay = max(2, min(30, math.ceil(rtt * 0.5 * 60) + 2))
            send_json(peer, {'input_delay': delay, 'rtt_ms': round(rtt * 1000, 1)})
        else:
            for index in range(6):
                if receive_json(peer) != {'latency_probe': index}:
                    raise ValueError('Invalid latency probe request.')
                send_json(peer, {'latency_probe': index})
            settings = receive_json(peer)
            delay = settings.get('input_delay')
            rtt = settings.get('rtt_ms', 0) / 1000
            if type(delay) is not int or not 2 <= delay <= 30:
                raise ValueError('Invalid session input delay.')
        self.log(f'Input pipeline: {delay} frames ({delay * 1000 / 60:.0f} ms); measured round trip {rtt * 1000:.0f} ms. Late packets pause play; inputs are never predicted.')
        return delay

    def play(self, peer):
        delay = self.input_delay(peer)
        listener = self.track(socket.socket(socket.AF_INET, socket.SOCK_STREAM))
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(60)
        secret = secrets.token_hex(32)
        env = self.environment.copy()
        for key in ('RESORT_INPUT_RECORD', 'RESORT_INPUT_REPLAY'):
            env.pop(key, None)
        env.update(RESORT_NETPLAY_BRIDGE_PORT=str(listener.getsockname()[1]),
                   RESORT_NETPLAY_PLAYER='0' if self.role == 'host' else '1',
                   RESORT_NETPLAY_BRIDGE_SECRET=secret,
                   RESORT_NETPLAY_INPUT_DELAY=str(delay),
                   RESORT_USERDATA_DIR=str(self.run / 'UserData'))
        from process_environment import native_library_path
        with (self.folder / 'native.log').open('wb') as output:
            with native_library_path():
                self.child = subprocess.Popen([str(self.source / GAME_EXECUTABLE)], cwd=self.source,
                                          env=env, stdout=output, stderr=subprocess.STDOUT)
            local, _ = listener.accept()
            self.track(local)
            local.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            if receive(local, 64) != secret.encode():
                raise ValueError('Invalid native bridge authentication.')
            # Do not release either native boot until both brokers have a game.
            send_json(peer, {'native_ready': True})
            if receive_json(peer) != {'native_ready': True}:
                raise ValueError('The other runtime did not become ready.')
            send(local, b'R')
            # Receive independently of local simulation. Both peers send their
            # own capture immediately, then consume the same older input pair.
            incoming = queue.Queue(maxsize=delay + 32)
            receive_errors = []
            solver_reports = queue.Queue(maxsize=delay + 32)
            def read_peer():
                try:
                    while not self.cancelled.is_set():
                        packet = receive(peer, 2 * MAX_MOTION + 64)
                        if packet[:1] == b'I' and len(packet) <= MAX_MOTION + 11:
                            incoming.put(packet, timeout=20)
                        elif packet[:1] == b'Z' and self.role == 'join':
                            inflater = zlib.decompressobj()
                            report = inflater.decompress(packet[1:], 2 * MAX_MOTION + 65)
                            if (len(report) > 2 * MAX_MOTION + 64 or not inflater.eof or
                                    inflater.unconsumed_tail or inflater.unused_data):
                                raise ValueError('Invalid compressed MotionPlus report.')
                            solver_reports.put(report, timeout=20)
                        else:
                            raise ValueError('Unexpected online input/solver packet.')
                except Exception as error:
                    receive_errors.append(error)
            reader = threading.Thread(target=read_peer, name='netplay-input-receive', daemon=True)
            reader.start()
            def take_packet(packets):
                deadline = time.monotonic() + 20
                while True:
                    if receive_errors: raise receive_errors[0]
                    if self.cancelled.is_set(): raise ConnectionAbortedError('Session cancelled.')
                    try: return packets.get(timeout=0.1)
                    except queue.Empty:
                        if time.monotonic() >= deadline:
                            raise TimeoutError('The other player stopped providing controller data.')
            def check_report(report, request, sequence):
                if len(report) < 20 or report[:2] != b'D\x01':
                    raise ValueError('Invalid MotionPlus solver report.')
                seq, frame = struct.unpack('>QQ', report[2:18])
                local_seq, local_frame = struct.unpack('>QQ', request[15:31])
                if (seq != sequence or seq != local_seq or frame != local_frame or
                        report[18:20] != request[1:3]):
                    details = {'interval': sequence, 'stage': 'canonical_motionplus_report',
                               'expected': {'sequence': local_seq, 'frame': local_frame,
                                            'modes': list(request[1:3])},
                               'host_report': {'sequence': seq, 'frame': frame,
                                               'modes': list(report[18:20])}}
                    (self.folder / 'desync.json').write_text(json.dumps(details, indent=2), encoding='utf-8')
                    raise ValueError(f'MotionPlus report frame/mode disagreement at interval {sequence}: '
                                     f'local frame={local_frame}, modes={list(request[1:3])}; '
                                     f'host frame={frame}, modes={list(report[18:20])}. Saved in desync.json.')
            captures = []
            sequence = 0
            stalled = 0.0
            enqueue_time = 0.0
            solver_time = 0.0
            while not self.cancelled.is_set():
                request = receive(local, MAX_MOTION + 11)
                enqueue_started = time.monotonic()
                send(peer, request)
                enqueue_time += time.monotonic() - enqueue_started
                captures.append(request)
                started = time.monotonic()
                if sequence < delay:
                    # Explicit, identical neutral startup on both native runtimes.
                    agreed = b'W'
                else:
                    other = take_packet(incoming)
                    old = captures.pop(0)
                    first, second = (old, other) if self.role == 'host' else (other, old)
                    agreed = self.validate_inputs(first, second, sequence - delay)
                stalled += time.monotonic() - started
                send(local, agreed)
                # Canonical virtual-controller processing is host-owned. Its
                # outputs stream in parallel with future raw captures; no
                # internet acknowledgement is required for each solver report.
                solver_started = time.monotonic()
                stage = receive(local, 2 * MAX_MOTION + 64)
                if self.role == 'host':
                    check_report(stage, request, sequence)
                    send(peer, b'Z' + zlib.compress(stage, level=1))
                    send(local, b'A')
                else:
                    if stage != b'D': raise ValueError('Guest attempted to publish solver state.')
                    report = take_packet(solver_reports)
                    check_report(report, request, sequence)
                    send(local, report)
                solver_time += time.monotonic() - solver_started
                if sequence and sequence % 300 == 0:
                    self.log(f'Online interval {sequence}; average missing-input wait {stalled * 1000 / 300:.1f} ms/frame; average send enqueue {enqueue_time * 1000 / 300:.1f} ms/frame; average solver phase {solver_time * 1000 / 300:.1f} ms/frame; buffered remote inputs {incoming.qsize()}')
                    stalled = 0.0
                    enqueue_time = 0.0
                    solver_time = 0.0
                sequence += 1

    def run_session(self, role, address='0.0.0.0', port=42680, code=''):
        callback = self.log
        def report(line):
            callback(line)
            if self.folder.is_dir():
                with (self.folder / 'online.log').open('a', encoding='utf-8') as output:
                    output.write(line + '\n')
        self.log = report
        peer = None
        try:
            self.prepare()
            if self.cancelled.is_set():
                return
            peer = self.connect(role, address, port, code)
            self.play(peer)
        except Exception as error:
            if peer:
                try:
                    send(peer, b'E' + str(error).encode()[:2048])
                except OSError:
                    pass
            if not self.cancelled.is_set():
                self.log(f'Session stopped: {error}')
                raise
        finally:
            self.cancel()
            for connection in self.sockets:
                if hasattr(connection, 'finish'): connection.finish()
            if self.child:
                try:
                    self.child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.child.kill()
                    self.child.wait()
            self.log(f'Session saves and logs remain in {self.folder}. Personal saves were not changed.')

    def keep_guest_miis(self):
        if self.role != 'host':
            return 0
        original = Database(self.nand / DB)
        identities = {record[24:32] for _, record in original.records()}
        count = 0
        for _, record in Database(self.run / 'UserData/NAND' / DB).records():
            if record[24:32] not in identities:
                original.put(record)
                identities.add(record[24:32])
                count += 1
        if count:
            original.save()
        return count
