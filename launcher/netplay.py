# SPDX-License-Identifier: GPL-3.0-only
"""Experimental two-player direct TLS netplay and the local native input broker.

No game/system artwork is transferred. Session saves never replace personal saves.
"""
from __future__ import annotations
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import secrets
import shutil
import socket
import ssl
import struct
import subprocess
import threading
import time
import tomllib

from mii_data import Database, checksum, validate_record

VERSION = 1
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
ASSETS = ('Resortcompiled', 'dsp_coef.bin', 'font_western.bin', 'font_japanese.bin',
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
        if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'AMD64'):
            raise ValueError('The first online build supports Linux x86-64 only.')
        if not (self.source / 'portable.txt').is_file():
            raise ValueError('Choose a portable runtime installation.')
        supported = False
        tail = b''
        with (self.source / 'Resortcompiled').open('rb') as executable:
            for block in iter(lambda: executable.read(1024 * 1024), b''):
                if b'RIISORTED_NETPLAY_V1' in tail + block:
                    supported = True
                    break
                tail = block[-64:]
        if not supported:
            raise ValueError('This installation has an older runtime. Update its Resortcompiled executable with the newly built out/Resortcompiled before using online play.')
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
        for name in ASSETS:
            item = self.source / name
            if item.is_dir():
                shutil.copytree(item, self.run / name)
            elif item.is_file():
                shutil.copy2(item, self.run / name)
        # Release runtimes resolve adjacent shared libraries via $ORIGIN.
        libraries = sorted({p.name for pattern in ('*.so', '*.so.*')
                            for p in self.source.glob(pattern) if p.is_file()})
        for name in libraries:
            shutil.copy2(self.source / name, self.run / name)
        (self.run / 'portable.txt').touch()
        (self.run / 'UserData/NAND').mkdir(parents=True)
        self.log('Checking runtime, artwork and game content…')
        self.compat = {
            'version': VERSION, 'runtime': sha(self.run / 'Resortcompiled'),
            'game': tree_hash(dvd), 'overlays': [tree_hash(p) for p in overlays],
            'artwork': sha(self.nand / ART),
            'assets': {name: tree_hash(self.run / name) if (self.run / name).is_dir() else sha(self.run / name)
                       for name in ASSETS if (self.run / name).exists()},
            'libraries': {name: sha(self.run / name) for name in libraries},
            'motion_conventions': [self.environment.get('RESORT_MPLS_SIGNS', '1,1,1'),
                                   self.environment.get('RESORT_MPLS_DIRSIGN', '1')],
        }
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
            listener.settimeout(180)
            self.invitation(invite)
            self.log(f'Waiting for the invited player on TCP port {port}…')
            raw, _ = listener.accept()
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
            raw = self.track(socket.create_connection((address, int(invite['port'])), timeout=20))
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

    def validate_inputs(self, first, second, sequence):
        batches = []
        for channel, message in enumerate((first, second)):
            if message[:1] != b'I' or not 11 + 24 < len(message) <= 11 + MAX_MOTION:
                raise ValueError('Invalid controller packet.')
            batch = message[11:]
            version, seq, frame, assigned, mode, recenter, count = struct.unpack('>IQQBBBB', batch[:24])
            if (version != VERSION or seq != sequence or assigned != channel or mode > 5 or
                recenter > 1 or not 1 <= count <= 128 or len(batch) != 24 + count * 181):
                raise ValueError(f'Input sequence/channel mismatch at interval {sequence}.')
            if mode != message[1 + channel]:
                raise ValueError('Invalid controller MotionPlus mode.')
            batches.append((batch, frame))
        if first[1:11] != second[1:11] or batches[0][1] != batches[1][1]:
            raise ValueError(f'Synchronization disagreement at interval {sequence}: frame, MotionPlus mode or previous solver output differs.')
        return b'P' + struct.pack('>I', len(batches[0][0])) + batches[0][0] + batches[1][0]

    def play(self, peer):
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
                   RESORT_NETPLAY_BRIDGE_SECRET=secret)
        with (self.folder / 'native.log').open('wb') as output:
            self.child = subprocess.Popen([str(self.run / 'Resortcompiled')], cwd=self.run,
                                          env=env, stdout=output, stderr=subprocess.STDOUT)
            local, _ = listener.accept()
            self.track(local)
            if receive(local, 64) != secret.encode():
                raise ValueError('Invalid native bridge authentication.')
            # Do not release either native boot until both brokers have a game.
            send_json(peer, {'native_ready': True})
            if receive_json(peer) != {'native_ready': True}:
                raise ValueError('The other runtime did not become ready.')
            send(local, b'R')
            sequence = 0
            while not self.cancelled.is_set():
                start = time.monotonic()
                request = receive(local, MAX_MOTION + 11)
                if self.role == 'host':
                    other = receive(peer, MAX_MOTION + 11)
                    agreed = self.validate_inputs(request, other, sequence)
                    send(peer, agreed)
                else:
                    send(peer, request)
                    agreed = receive(peer, 2 * MAX_MOTION + 5)
                    if agreed[:1] != b'P':
                        raise ValueError('Invalid host controller response.')
                send(local, agreed)
                if sequence % 300 == 0:
                    self.log(f'Online interval {sequence}; exchange wait {(time.monotonic() - start) * 1000:.0f} ms')
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
