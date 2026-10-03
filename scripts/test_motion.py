#!/usr/bin/env python3
"""Test host math and the actual translated SDK using an existing native build."""
import pathlib,shlex,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parent.parent
build=root/'work/build'
with tempfile.TemporaryDirectory(prefix='resort-motion-') as temp:
    temp=pathlib.Path(temp)
    def run(args,**kw):subprocess.run(args,check=True,**kw)
    run(['clang++','-std=c++17','-I',str(root/'runtime/wsr/include'),str(root/'runtime/wsr/tests/resort_motion_tests.cpp'),'-o',str(temp/'math')])
    run([str(temp/'math')])
    run(['clang++','-std=c++17','-O2','-I',str(root/'runtime/wsr/include'),'-c',str(root/'runtime/wsr/tests/resort_motion_sdk_tests.cpp'),'-o',str(temp/'sdk.o')])
    commands=subprocess.check_output(['ninja','-t','commands','WiiCompiled'],cwd=build,text=True)
    link=next(line for line in reversed(commands.splitlines()) if ' -o WiiCompiled ' in line)
    args=shlex.split(link.split(' && ')[1])
    output=args.index('-o')
    args[output+1]=str(temp/'sdk')
    args[output:output]=[str(temp/'sdk.o'),'-Wl,--wrap=main']
    run(args,cwd=build)
    run([str(temp/'sdk'),str(root/'work/extracted/sys/main.dol')])
