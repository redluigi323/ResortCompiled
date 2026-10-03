#!/usr/bin/env python3
"""Prepare isolated saves and run the experimental Riisorted motion recorder.

Python 3.11+. These are local development sessions, not network connections.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tomllib
import uuid

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ('Resortcompiled', 'dsp_coef.bin', 'font_western.bin',
          'font_japanese.bin', 'cacert.pem', 'wii_bootstrap')
MOTION_ENV = ('RESORT_MPLS_SIGNS', 'RESORT_MPLS_DIRSIGN')


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(block)
    return sha.hexdigest()


def manifest(folder: Path) -> dict[str, str]:
    return {path.relative_to(folder).as_posix(): digest(path)
            for path in sorted(folder.rglob('*')) if path.is_file()}


def resolved(base: Path, value: str) -> Path:
    path = Path(value)
    return (path if path.is_absolute() else base / path).resolve()


def replace_paths(text: str, values: dict) -> str:
    # Preserve all non-path settings. Nested/multiple paths tables are rejected
    # rather than silently rewriting an unsupported source configuration.
    matches = list(re.finditer(r'^\s*\[paths\]\s*(?:#.*)?$', text, re.MULTILINE))
    if len(matches) != 1 or re.search(r'^\s*\[paths\.', text, re.MULTILINE):
        raise ValueError('Expected one plain [paths] section in Config.toml')
    start = matches[0].start()
    following = re.search(r'^\s*\[', text[matches[0].end():], re.MULTILINE)
    end = matches[0].end() + following.start() if following else len(text)
    entries = '\n'.join(f'{key} = {json.dumps(value, ensure_ascii=True)}'
                        for key, value in values.items())
    result = text[:start] + '[paths]\n' + entries + '\n\n' + text[end:]
    tomllib.loads(result)
    return result


def prepare(session: Path, source: Path) -> dict:
    if not (source / 'portable.txt').is_file():
        raise ValueError('--source-out must be a portable runtime folder containing portable.txt')
    if not (source / 'Resortcompiled').is_file():
        raise ValueError('Build out/Resortcompiled first')
    userdata = source / 'UserData'
    text = (userdata / 'Config.toml').read_text(encoding='utf-8-sig')
    paths = dict(tomllib.loads(text).get('paths', {}))
    if not paths.get('dvd_root'):
        raise ValueError('Configure dvd_root before recording')
    dvd = resolved(userdata, paths['dvd_root'])
    if not (dvd / 'sys/main.dol').is_file():
        raise ValueError('dvd_root must point to an extracted game with sys/main.dol')
    nand = resolved(userdata, paths.get('nand_root') or 'NAND')
    if not nand.is_dir():
        raise ValueError('Run the game normally once to initialize its NAND before recording')
    paths['dvd_root'] = str(dvd)
    paths['nand_root'] = 'NAND'
    overlays = [resolved(userdata, value) for value in paths.get('overlay_roots', [])]
    if any(not path.is_dir() for path in overlays):
        raise ValueError('An overlay directory is missing')
    paths['overlay_roots'] = [str(path) for path in overlays]
    if paths.get('retro_rewind_root'):
        raise ValueError('This development helper does not support retro_rewind_root')

    session.mkdir(parents=True, exist_ok=False)
    assets = session / 'assets'
    assets.mkdir()
    for name in ASSETS:
        item = source / name
        if item.is_dir():
            shutil.copytree(item, assets / name)
        elif item.is_file():
            shutil.copy2(item, assets / name)
    initial = session / 'initial/UserData'
    initial.mkdir(parents=True)
    print('Copying the starting NAND into the isolated session…', flush=True)
    shutil.copytree(nand, initial / 'NAND')
    for name in ('keyboard_bindings.dat',):
        if (userdata / name).is_file():
            shutil.copy2(userdata / name, initial / name)
    (initial / 'Config.toml').write_text(replace_paths(text, paths), encoding='utf-8')
    print('Hashing runtime and game content…', flush=True)
    data = {
        'version': 1,
        'source': str(source),
        'assets': manifest(assets),
        'initial': manifest(session / 'initial'),
        'content': [{'root': str(path), 'files': manifest(path)} for path in [dvd, *overlays]],
        'motion_environment': {key: os.environ.get(key) for key in MOTION_ENV},
    }
    (session / 'manifest.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    return data


def verify(session: Path) -> dict:
    data = json.loads((session / 'manifest.json').read_text(encoding='utf-8'))
    if data.get('version') != 1:
        raise ValueError('Unsupported session manifest version')
    print('Checking the saved starting state and game content…', flush=True)
    for name in ('assets', 'initial'):
        if manifest(session / name) != data[name]:
            raise ValueError(f'Session {name} changed; create a new recording')
    for content in data['content']:
        if manifest(Path(content['root'])) != content['files']:
            raise ValueError(f'Game/overlay content changed: {content["root"]}')
    return data


def launch(session: Path, data: dict, replay: bool) -> int:
    tape = session / 'motion.riisinput'
    if replay and not tape.is_file():
        raise ValueError('This session has no motion recording')
    if not replay and tape.exists():
        raise ValueError('Recording already exists; choose a new session folder')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S')
    run = session / ('replay-' + stamp + '-' + uuid.uuid4().hex[:6] if replay else 'record')
    run.mkdir()
    for item in (session / 'assets').iterdir():
        if item.is_dir():
            shutil.copytree(item, run / item.name)
        else:
            shutil.copy2(item, run / item.name)
    shutil.copytree(session / 'initial/UserData', run / 'UserData')
    (run / 'portable.txt').touch()
    env = os.environ.copy()
    for key in ('RESORT_INPUT_RECORD', 'RESORT_INPUT_REPLAY', *MOTION_ENV):
        env.pop(key, None)
    for key, value in data['motion_environment'].items():
        if value is not None:
            env[key] = value
    env['RESORT_INPUT_REPLAY' if replay else 'RESORT_INPUT_RECORD'] = str(tape)
    print(f'Running isolated {"playback" if replay else "recording"}: {run}', flush=True)
    print('Logs and saves stay in this run folder; no progress is copied back.', flush=True)
    child = subprocess.Popen([str(run / 'Resortcompiled')], cwd=run, env=env)
    try:
        return child.wait()
    except KeyboardInterrupt:
        # An interrupted child must be gone before the session lock is released.
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
        print('Interrupted recording may lack a clean end marker.', file=sys.stderr)
        return 130


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('record', 'replay'))
    parser.add_argument('session', type=Path, help='New folder for record; existing folder for replay')
    parser.add_argument('--source-out', type=Path, default=ROOT / 'out')
    args = parser.parse_args()
    session = args.session.resolve()
    if args.mode == 'record':
        data = prepare(session, args.source_out.resolve())
    else:
        data = verify(session)
    lock = session / '.session.lock'
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(str(os.getpid()) + '\n')
        return launch(session, data, args.mode == 'replay')
    finally:
        lock.unlink(missing_ok=True)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError) as error:
        print(f'Session setup failed: {error}', file=sys.stderr)
        raise SystemExit(1)
