#!/usr/bin/env python3
"""Run the actual HLE display-list bridge without starting graphics or tracing."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
build = root / 'work/build'
commands = subprocess.check_output(
    ['ninja', '-t', 'commands', 'WiiCompiled'], cwd=build, text=True).splitlines()
with tempfile.TemporaryDirectory(prefix='resort-gx-') as temp:
    temp = Path(temp)
    # Reuse the runtime's include paths/defines, but not its PCH or dependency outputs.
    command = next(line for line in commands
                   if ' -c ' in line and 'unity_runtime_3_cxx.cxx' in line)
    args = shlex.split(command)
    flags = []
    i = 1
    while i < len(args):
        if args[i] == '-Xclang':
            i += 2
        elif args[i] in ('-o', '-c', '-MT', '-MF'):
            i += 2
        elif args[i] in ('-MD', '-Winvalid-pch'):
            i += 1
        else:
            flags.append(args[i])
            i += 1
    obj, exe = temp / 'tests.o', temp / 'tests'
    subprocess.run([args[0], *flags, '-c',
                    str(root / 'runtime/wsr/tests/resort_gx_display_list_tests.cpp'),
                    '-o', str(obj)], cwd=build, check=True)
    command = next(line for line in reversed(commands) if ' -o WiiCompiled ' in line)
    args = shlex.split(command.split(' && ')[1])
    output = args.index('-o')
    args[output + 1] = str(exe)
    args[output:output] = [str(obj), '-Wl,--wrap=main,--wrap=GXCallDisplayList']
    subprocess.run(args, cwd=build, check=True)
    subprocess.run([str(exe)], check=True)
