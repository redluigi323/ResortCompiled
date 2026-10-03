#!/usr/bin/env python3
"""Configure the checked-in WSR runtime/map for a build from your own disc.

No Mario Kart Wii reference or re-keying is required for release builds.
"""
from pathlib import Path
import hashlib
import re
import yaml

root = Path(__file__).resolve().parent.parent
template = root / 'projects/wsr/recomp.yml'
text = template.read_text()
document = yaml.safe_load(text)
dol = root / 'work/extracted/sys/main.dol'
boot = root / 'work/extracted/sys/boot.bin'
if not dol.is_file() or not boot.is_file():
    raise SystemExit('Extract your own disc with scripts/extract.sh first.')
if boot.read_bytes()[:6] != b'RZTP01':
    raise SystemExit('This build requires PAL Wii Sports Resort RZTP01.')
with dol.open('rb') as stream:
    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
if digest != document['inputs']['dol']['sha256']:
    raise SystemExit('main.dol does not match the supported PAL revision.')
replacements = {
    r'(?m)^(    path: ).*main\.dol$': str(dol),
    r'(?m)^(    path: ).*MAP\.txt$': str(root / 'projects/wsr/MAP.txt'),
    r'(?m)^(  native_registration_root: ).*$': str(root / 'runtime/wsr/src'),
    r'(?m)^(  root: ).*$': str(root / 'work/generated'),
}
for pattern, value in replacements.items():
    text, count = re.subn(pattern, lambda match: match[1] + value, text)
    if count != 1: raise SystemExit('Unsupported project template layout.')
destination = root / 'external/wiicompiled/projects/wsr/recomp.yml'
if not (root / 'external/wiicompiled/translator').is_dir():
    raise SystemExit('Run bash setup.sh before configuring the project.')
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(text)
print(f'Configured {destination} from the checked-in map/runtime.')
