#!/usr/bin/env python3
"""Extract Microsoft app-local x64 runtime DLLs without executing the installer."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
work = ROOT / 'work/windows'
output = work / 'vcredist'; output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=work, prefix='vcredist-') as folder:
    folder = Path(folder)
    subprocess.run(['cabextract', '-q', '-d', str(folder), str(work / 'downloads/vc_redist.x64.exe')], check=True)
    payloads = [e.attrib for e in ET.parse(folder / '0').iter() if e.tag.endswith('Payload')]
    cabinet = next(e for e in payloads if e.get('FilePath', '').lower() == 'packages\\vcruntimeminimum_amd64\\cab1.cab')
    files = folder / 'dlls'; files.mkdir()
    subprocess.run(['cabextract', '-q', '-d', str(files), str(folder / cabinet['SourcePath'])], check=True)
    for file in files.glob('*.dll_amd64'):
        shutil.copy2(file, output / file.name.removesuffix('_amd64'))
    license = next(e for e in payloads if e.get('FilePath') == 'license.rtf')
    shutil.copy2(folder / license['SourcePath'], output / 'Microsoft-Visual-C++-license.rtf')
for name in ('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
    if not (output / name).is_file(): raise RuntimeError(f'Microsoft runtime is missing {name}')
print(f'Windows app-local Microsoft runtime: {output}')
