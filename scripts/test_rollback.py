#!/usr/bin/env python3
"""Check the rollback core; optionally relink the translated SDK restore probe.

No game startup, graphics window, gameplay trace or personal save writes.
Windows requires the existing LLVM-MinGW/Wine setup. Native builds must already
be current when --sdk is used. These tests do not certify whole-game rollback.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows', action='store_true')
    parser.add_argument('--sdk', action='store_true')
    parser.add_argument('--continuations', action='store_true')
    parser.add_argument('--workers', action='store_true', help='Relink the real suspended guest-worker checkpoint probe')
    args = parser.parse_args()
    compiler = (ROOT / 'work/windows/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64/bin/x86_64-w64-mingw32-clang++'
                if args.windows else Path('clang++'))
    prefix = ['bash', str(ROOT / 'scripts/windows/wine.sh')] if args.windows else []
    include = ['-I', str(ROOT / 'runtime/wsr/include')]
    source = ROOT / 'runtime/wsr/src/netplay'
    def run(command, **kwargs):
        subprocess.run([str(item) for item in command], check=True, **kwargs)
    with tempfile.TemporaryDirectory(prefix='resort-rollback-') as directory:
        temp = Path(directory)
        core = temp / ('core.exe' if args.windows else 'core')
        run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', *(['-static'] if args.windows else []),
             *include, ROOT / 'scripts/tests/rollback_tests.cpp', source / 'rollback.cpp',
             source / 'checkpoint.cpp', source / 'session.cpp', '-o', core])
        run([*prefix, core])
        if args.continuations:
            libco = ROOT / 'external/wiicompiled/runtime/third_party/libco'
            co_object = temp / 'libco.o'
            c_compiler = str(compiler).removesuffix('++')
            run([c_compiler, '-O2', '-DNVALGRIND', '-c', libco / 'libco.c', '-o', co_object])
            continuation = temp / ('continuation.exe' if args.windows else 'continuation')
            run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra',
                 *(['-static'] if args.windows else ['-pthread']), *include, '-I', libco,
                 ROOT / 'scripts/tests/continuation_tests.cpp', ROOT / 'runtime/wsr/src/host_context.cpp',
                 ROOT / 'runtime/wsr/src/host_context_rewind.cpp', co_object, '-o', continuation])
            run([*prefix, continuation])
            legacy = temp / ('legacy-context.exe' if args.windows else 'legacy-context')
            run([compiler, '-std=c++17', '-O2', *(['-static'] if args.windows else ['-pthread']),
                 *include, '-I', libco, ROOT / 'runtime/wsr/tests/host_context_tests.cpp',
                 ROOT / 'runtime/wsr/src/host_context.cpp', ROOT / 'runtime/wsr/src/host_context_rewind.cpp',
                 co_object, '-o', legacy])
            run([*prefix, legacy])
            print('Default context backend handoffs passed.', flush=True)
        if not args.sdk and not args.workers:
            return
        build = ROOT / ('work/windows/build' if args.windows else 'work/build')
        native = 'WiiCompiled.exe' if args.windows else 'WiiCompiled'
        obj = temp / 'sdk.o'
        run([compiler, '-std=c++17', '-O2', *include, '-c',
             ROOT / ('scripts/tests/worker_checkpoint_tests.cpp' if args.workers else 'scripts/tests/rollback_sdk_tests.cpp'), '-o', obj])
        commands = subprocess.check_output(['ninja', '-t', 'commands', 'WiiCompiled'], cwd=build, text=True)
        line = next(line for line in reversed(commands.splitlines()) if f' -o {native} ' in line)
        link = shlex.split(next(part for part in line.split(' && ') if f' -o {native} ' in part))
        # Keep the Windows probe beside the native runtime's DLLs, then remove
        # only this unique harness file. It is never added to a release payload.
        if args.windows:
            fd, name = tempfile.mkstemp(prefix='rollback-sdk-', suffix='.exe', dir=ROOT / 'out/windows')
            os.close(fd)
            executable = Path(name)
        else:
            executable = temp / 'sdk'
        try:
            index = link.index('-o')
            link[index + 1] = str(executable)
            link[index:index] = [str(obj), '-Wl,--wrap=main']
            link = [item for item in link if not item.startswith('-Wl,--out-implib,')]
            run(link, cwd=build)
            dol = ROOT / 'work/extracted/sys/main.dol'
            run([*prefix, executable, 'Z:' + str(dol).replace('/', '\\') if args.windows else dol])
        finally:
            if args.windows:
                executable.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
