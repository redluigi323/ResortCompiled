# SPDX-License-Identifier: GPL-3.0-only
"""Read shared simulation identity and verify local release binaries separately."""
import hashlib
import json
from pathlib import Path
import re

MARKER = re.compile(rb'RIISORTED_SIMULATION_ID=([0-9a-f]{64})')

def simulation_identity(executable):
    tail = b''
    with Path(executable).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            match = MARKER.search(tail + block)
            if match: return match[1].decode('ascii')
            tail = block[-128:]
    raise ValueError('This runtime predates Windows/Linux crossplay. Update the runtime using the matching launcher release.')

def verify_local_release(runtime):
    """Installed releases check their own hashes; development builds use the marker."""
    runtime = Path(runtime)
    stamp = runtime / 'runtime-release.json'
    if not stamp.is_file(): return
    data = json.loads(stamp.read_text(encoding='utf-8'))
    files = data.get('runtime_files')
    if not isinstance(files, dict) or not files: raise ValueError('Invalid local runtime integrity manifest.')
    for name, expected in files.items():
        relative = Path(name)
        if (relative.is_absolute() or '..' in relative.parts or 'UserData' in relative.parts
                or not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected)):
            raise ValueError('Invalid local runtime integrity manifest.')
        file = runtime / relative
        if not file.is_file() or file.is_symlink(): raise ValueError(f'Repair the installed runtime: {name} is missing.')
        with file.open('rb') as stream: actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != expected: raise ValueError(f'Repair the installed runtime: {name} has changed.')
