#!/usr/bin/env python3
"""Build the EOS C ABI bridge natively or cross compile Windows x86-64."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, default=ROOT / 'eosstuff/SDK')
parser.add_argument('--output', type=Path, default=ROOT / 'launcher/eos-local')
parser.add_argument('--platform', choices=('linux', 'win32'), default=sys.platform)
parser.add_argument('--compiler', help='C++ compiler; for Windows use LLVM-MinGW clang++')
args = parser.parse_args()
sdk = args.sdk.resolve(); output = args.output.resolve()
if not (sdk / 'Include/eos_sdk.h').is_file(): parser.error('Choose the SDK folder containing Include and Bin.')
windows = args.platform == 'win32'
name = 'EOSSDK-Win64-Shipping.dll' if windows else 'libEOSSDK-Linux-Shipping.so'
library = sdk / 'Bin' / name
if not library.is_file(): parser.error(f'The {args.platform} x86-64 EOS runtime library is missing.')
compiler = args.compiler or ('x86_64-w64-mingw32-clang++' if windows else 'c++')
output.mkdir(parents=True, exist_ok=True)
shutil.copy2(library, output / library.name)
command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-shared',
           '-I', str(sdk / 'Include'), str(ROOT / 'launcher/eos_bridge.cpp')]
if windows:
    xaudio = sdk / 'Bin/x64/xaudio2_9redist.dll'
    if not xaudio.is_file(): parser.error('Keep the EOS SDK Bin/x64 XAudio runtime alongside the SDK DLL.')
    shutil.copy2(xaudio, output / xaudio.name)
    import_library = sdk / 'Lib/EOSSDK-Win64-Shipping.lib'
    if not import_library.is_file(): parser.error('The EOS Windows import library is missing.')
    command += [str(import_library), '-static-libstdc++', '-static-libgcc', '-o', str(output / 'riisorted-eos.dll')]
else:
    command += ['-fPIC', '-L', str(output), '-lEOSSDK-Linux-Shipping', '-Wl,-rpath,$ORIGIN', '-pthread',
                '-o', str(output / 'libriisorted-eos.so')]
subprocess.run(command, check=True)
print(f'EOS bridge built in {output}')
