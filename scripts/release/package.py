#!/usr/bin/env python3
"""Stage a clean release payload, then freeze the launcher on the target OS.

This packages an already-built native runtime. It does not compile or run the
translated game, run tests, or copy the development installation's UserData.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'launcher'))
from runtime_identity import simulation_identity
LAUNCHER = ROOT / "launcher"
GAME_ID = "RZTP01"
DOL_SHA256 = "855e4ffe4f8d69d172c44b05ec3a4ddb89645bebfadca41c98effab69111d448"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / "out")
    parser.add_argument("--dtk", type=Path, default=ROOT / "tools/bin" / ("dtk.exe" if sys.platform == "win32" else "dtk"))
    parser.add_argument("--version", default="0.1.0-preview")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--stage-only", action="store_true", help="Stage launcher/payload for running the Python UI without freezing")
    parser.add_argument("--archive", action="store_true", help="Create a versioned archive and SHA256 checksum after freezing")
    parser.add_argument('--eos-config', type=Path, help='Local EOS client JSON to embed in this test build (never in the source snapshot)')
    parser.add_argument('--eos-bridge-dir', type=Path, help='Use an already compiled EOS bridge and SDK library')
    parser.add_argument('--eos-sdk', type=Path, default=ROOT / 'eosstuff/SDK', help='EOS SDK directory, used only with --eos-config')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.version):
        parser.error('Version must contain only letters, digits, dots, underscores and hyphens.')
    if sys.platform not in ("linux", "win32"):
        parser.error("Release packaging currently targets Linux and Windows.")
    executable = "Resortcompiled.exe" if sys.platform == "win32" else "Resortcompiled"
    runtime = args.runtime_dir.resolve()
    required = (executable, "dsp_coef.bin", "font_western.bin", "font_japanese.bin", "wii_bootstrap")
    for name in required:
        if not (runtime / name).exists():
            parser.error(f"Missing runtime payload: {runtime / name}")
    if not args.dtk.is_file():
        parser.error(f"Supply the target platform's decomp-toolkit binary: {args.dtk}")
    # Stage beside the final payload so replacement stays on the same filesystem.
    with tempfile.TemporaryDirectory(prefix=".payload-", dir=LAUNCHER) as temporary:
        staged = Path(temporary) / "payload"
        target = staged / "runtime"; target.mkdir(parents=True)
        for name in required:
            source = runtime / name
            if source.is_dir():
                shutil.copytree(source, target / name)
            else:
                shutil.copy2(source, target / name)
        for name in ("cacert.pem",):
            if (runtime / name).is_file():
                shutil.copy2(runtime / name, target / name)
        # Adjacent platform dependencies are part of the native runtime, unlike
        # saves, caches, screenshots, disc data, mods, or developer config.
        for pattern in ("*.dll", "*.so", "*.so.*"):
            for source in runtime.glob(pattern):
                if source.is_file():
                    shutil.copy2(source, target / source.name)
        if sys.platform == 'linux':
            # Inspect our trusted build's dependencies; never launch the game.
            dependency_output = subprocess.check_output(['ldd', str(target / executable)], text=True)
            if 'not found' in dependency_output:
                parser.error('Native runtime has missing shared libraries:\n' + dependency_output)
            for line in dependency_output.splitlines():
                match = re.match(r'\s*(\S+) => (/\S+)', line)
                if match and match[1].startswith('libpng'):
                    shutil.copy2(match[2], target / match[1])
            adjacent = Path(sys.executable).parent / 'patchelf'
            patchelf = str(adjacent) if adjacent.is_file() else shutil.which('patchelf')
            if not patchelf:
                parser.error('Install launcher/requirements.txt to get the Linux patchelf packaging tool.')
            subprocess.run([patchelf, '--set-rpath', '$ORIGIN', str(target / executable)], check=True)
            for library in target.glob('*.so*'):
                subprocess.run([patchelf, '--set-rpath', '$ORIGIN', str(library)], check=True)
        (staged / "tools").mkdir()
        tool = staged / "tools" / ("dtk.exe" if sys.platform == "win32" else "dtk")
        shutil.copy2(args.dtk.resolve(), tool)
        if sys.platform != "win32":
            tool.chmod(tool.stat().st_mode | 0o111)
            (target / executable).chmod((target / executable).stat().st_mode | 0o111)
        (staged / "release.json").write_text(json.dumps({
            "schema": 1, "version": args.version, "platform": sys.platform,
            "netplay_simulation_id": simulation_identity(target / executable),
            "architecture": platform.machine(), "game_id": GAME_ID,
            "dol_sha256": DOL_SHA256, "required_free_bytes": 6 * 1024**3,
            "runtime_files": {p.relative_to(target).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in sorted(target.rglob('*')) if p.is_file()},
        }, indent=2), encoding="utf-8")
        if args.eos_config:
            if platform.machine().lower() not in ('x86_64', 'amd64'):
                parser.error('The EOS bridge targets x86-64.')
            # Validate without emitting the contents or credentials on failure.
            try:
                eos_config = json.loads(args.eos_config.read_text())
                if any(not isinstance(eos_config.get(key), str) or not eos_config[key]
                       for key in ('product_id', 'sandbox_id', 'deployment_id', 'client_id', 'client_secret')):
                    raise ValueError()
            except (ValueError, OSError):
                parser.error('Invalid local EOS client configuration.')
            if args.eos_bridge_dir:
                names = ('riisorted-eos.dll', 'EOSSDK-Win64-Shipping.dll', 'xaudio2_9redist.dll') if sys.platform == 'win32' else ('libriisorted-eos.so', 'libEOSSDK-Linux-Shipping.so')
                (staged / 'eos').mkdir()
                for name in names:
                    source = args.eos_bridge_dir / name
                    if not source.is_file(): parser.error(f'Missing EOS bridge dependency: {source}')
                    shutil.copy2(source, staged / 'eos' / name)
            else:
                subprocess.run([sys.executable, str(ROOT / 'scripts/build_eos.py'),
                                '--sdk', str(args.eos_sdk), '--output', str(staged / 'eos')], check=True)
            (staged / 'eos/client.json').write_text(json.dumps(eos_config, indent=2), encoding='utf-8')
            (staged / 'eos/client.json').chmod(0o600)
            notices = args.eos_sdk.resolve().parent / 'ThirdPartyNotices'
            if not notices.is_dir(): parser.error('Keep the EOS ThirdPartyNotices folder alongside the SDK.')
            shutil.copytree(notices, staged / 'eos/ThirdPartyNotices')
        if sys.platform == 'win32':
            # The renderer and EOS SDK use Microsoft's runtime. Keep its real
            # redistributable DLLs adjacent; never collect Wine's system stubs.
            if args.eos_config:
                for source in target.glob('*140*.dll'):
                    shutil.copy2(source, staged / 'eos' / source.name)
        licenses = staged / "licenses"; licenses.mkdir()
        if sys.platform == 'win32':
            shutil.copy2(runtime / 'LLVM-MinGW-LICENSE.TXT', licenses / 'LLVM-MinGW-LICENSE.TXT')
            shutil.copy2(ROOT / 'work/windows/vcredist/Microsoft-Visual-C++-license.rtf',
                         licenses / 'Microsoft-Visual-C++-license.rtf')
        for source, name in ((ROOT / "external/wiicompiled/LICENSE", "WiiCompiled-LICENSE"),
                             (ROOT / "external/wiicompiled/aurora-main/LICENSE", "Aurora-LICENSE")):
            if source.is_file():
                shutil.copy2(source, licenses / name)
        shutil.copy2(LAUNCHER / "LICENSE", licenses / "ResortLauncher-GPL-3.0.txt")
        shutil.copy2(LAUNCHER / "THIRD-PARTY.md", licenses / "ResortLauncher-credits.md")
        shutil.copytree(LAUNCHER / "licenses", licenses, dirs_exist_ok=True)
        png_license = ROOT / 'work/build/_deps/png-src/LICENSE'
        if png_license.is_file():
            shutil.copy2(png_license, licenses / 'libpng-LICENSE.txt')
        payload = LAUNCHER / ("payload-win32" if sys.platform == "win32" else "payload")
        # Only this script's generated payload may be replaced.
        if payload.exists():
            if not (payload / "release.json").is_file():
                parser.error(f"Refusing to replace unrecognized directory: {payload}")
            shutil.rmtree(payload)
        staged.rename(payload)
    print(f"Staged release payload: {payload}")
    if args.stage_only:
        print("Launch the UI with: python launcher/app.py")
        return
    # An onedir bundle avoids unpacking the 100+ MB runtime on every launch.
    # Build natively per OS/architecture; PyInstaller does not cross-compile.
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean",
               "--onedir", "--windowed", "--name", "ResortLauncher",
               "--distpath", str(args.output.resolve()),
               "--workpath", str(ROOT / "work" / ("launcher-build-windows" if sys.platform == "win32" else "launcher-build")),
               "--specpath", str(ROOT / "work" / ("launcher-spec-windows" if sys.platform == "win32" else "launcher-spec")),
               # PyOpenGL chooses a backend at runtime. The default hook only
               # collects the build machine's backend, omitting EGL when an
               # X11-built launcher is run on a Wayland desktop.
               "--collect-submodules", "OpenGL.platform",
               "--add-data", f"{payload}{';' if sys.platform == 'win32' else ':'}payload",
               "--add-data", f"{LAUNCHER / 'assets'}{';' if sys.platform == 'win32' else ':'}assets",
               str(LAUNCHER / "app.py")]
    if sys.platform == 'win32':
        # Wine's Qt introspection can miss plugins even though the Qt DLLs are
        # collected successfully. Declare the desktop plugins explicitly.
        from importlib.util import find_spec
        qt_package = Path(find_spec('PySide6').origin).parent
        windows_plugins = ('platforms/qwindows.dll', 'styles/qmodernwindowsstyle.dll',
                           'imageformats/qgif.dll', 'imageformats/qico.dll',
                           'imageformats/qjpeg.dll', 'imageformats/qsvg.dll',
                           'iconengines/qsvgicon.dll')
        for relative in windows_plugins:
            plugin = qt_package / 'plugins' / relative
            if not plugin.is_file(): parser.error(f'Missing Windows Qt plugin: {plugin}')
            destination = (Path('PySide6/plugins') / Path(relative).parent).as_posix()
            command.extend(['--add-binary', f'{plugin};{destination}'])
    subprocess.run(command, check=True, cwd=ROOT)
    release_dir = args.output.resolve() / "ResortLauncher"
    if sys.platform == 'win32':
        plugin = release_dir / '_internal/PySide6/plugins/platforms/qwindows.dll'
        if not plugin.is_file(): parser.error('Windows packaging omitted the Qt platform plugin.')
        # Verify its non-system dependencies beside the Qt package or bundle.
        import pefile
        image = pefile.PE(str(plugin), fast_load=True)
        image.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        for entry in image.DIRECTORY_ENTRY_IMPORT:
            name = entry.dll.decode('ascii')
            if name.lower().startswith(('qt6', 'msvcp', 'vcruntime')):
                roots = (release_dir / '_internal/PySide6', release_dir / '_internal')
                if not any((root / name).is_file() for root in roots):
                    parser.error(f'Windows Qt platform plugin dependency is missing: {name}')
        image.close()
        # PyInstaller may classify payload EXEs/DLLs as binaries and rewrite
        # their PE headers. Preserve the exact runtime covered by release.json.
        shutil.copytree(payload / 'runtime', release_dir / '_internal/payload/runtime',
                        dirs_exist_ok=True)
    packaged_payload = release_dir / '_internal/payload'
    integrity = json.loads((packaged_payload / 'release.json').read_text(encoding='utf-8'))
    for name, expected in integrity['runtime_files'].items():
        with (packaged_payload / 'runtime' / name).open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != expected:
            parser.error(f'Packaged runtime integrity mismatch: {name}')
    shutil.copy2(LAUNCHER / "RELEASE-README.txt", release_dir / "README.txt")
    shutil.copytree(payload / "licenses", release_dir / "licenses", dirs_exist_ok=True)
    source_dir = release_dir / "source"
    launcher_source = source_dir / "launcher"; launcher_source.mkdir(parents=True, exist_ok=True)
    for source in LAUNCHER.glob("*.py"):
        shutil.copy2(source, launcher_source / source.name)
    shutil.copy2(LAUNCHER / 'eos_bridge.cpp', launcher_source / 'eos_bridge.cpp')
    for name in ("LICENSE", "THIRD-PARTY.md", "README.md", "RELEASE-README.txt", "requirements.txt"):
        shutil.copy2(LAUNCHER / name, launcher_source / name)
    for name in ("assets", "licenses"):
        shutil.copytree(LAUNCHER / name, launcher_source / name, dirs_exist_ok=True)
    build_source = source_dir / "scripts/release"; build_source.mkdir(parents=True, exist_ok=True)
    for name in ("package.py", "import_mii_icons.py", "build.sh"):
        shutil.copy2(ROOT / "scripts/release" / name, build_source / name)
    docs = release_dir / 'docs'; docs.mkdir(exist_ok=True)
    for name in ('Player-setup.md', 'Release-build.md', 'Riisorted-online-development.md', 'EOS-netplay.md', 'Windows-build.md', 'Netplay-rollback-roadmap.md'):
        shutil.copy2(ROOT / 'docs' / name, docs / name)
    # The complete native source snapshot is supplied beside the binary. Disc
    # images, translations, NAND data and developer directories are excluded.
    shutil.copytree(ROOT / 'scripts/windows', source_dir / 'scripts/windows', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    for name in ('runtime', 'patches', 'projects', 'docs'):
        shutil.copytree(ROOT / name, source_dir / name, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    for source in (ROOT / 'scripts').glob('*'):
        if source.is_file(): shutil.copy2(source, source_dir / 'scripts' / source.name)
    for name in ('README.md', 'LICENSE', 'setup.sh'):
        shutil.copy2(ROOT / name, source_dir / name)
    (source_dir / 'DEPENDENCIES.txt').write_text(
        'WiiCompiled source: https://github.com/patchzyy/wiicompiled\n'
        'Pinned commit: a88b7b502b620d38384e39aa7813c4ebae9a1f0c\n'
        'Run setup.sh to fetch that revision and apply the included patches.\n'
        'Game translations must be generated locally from your own disc.\n', encoding='utf-8')
    if args.archive:
        args.output.mkdir(parents=True, exist_ok=True)
        release_platform = 'windows' if sys.platform == 'win32' else sys.platform
        release_arch = 'x86_64' if platform.machine().lower() == 'amd64' else platform.machine()
        base = args.output.resolve() / f'ResortCompiled-{args.version}-{release_platform}-{release_arch}'
        archive = Path(shutil.make_archive(str(base), 'zip' if sys.platform == 'win32' else 'gztar',
                                         root_dir=release_dir.parent, base_dir=release_dir.name))
        with archive.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        archive.with_name(archive.name + '.sha256').write_text(f'{digest}  {archive.name}\n')
        print(f'Archive: {archive}')
    print(f"Release folder: {release_dir}\nDistribute the entire folder together.")


if __name__ == "__main__":
    main()
