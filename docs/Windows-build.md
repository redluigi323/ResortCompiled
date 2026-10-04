# Windows edition (built on Linux)

Target: Windows 10/11, x86-64 with AVX2 (the native runtime uses x86-64-v3). Extract the whole ZIP, open `ResortLauncher.exe`,
and install from your own PAL Wii Sports Resort disc image (RZTP01).
Python, the EOS SDK, Wii system artwork and a developer checkout are not required
on the player's computer. The launcher installs its bundled runtime alongside
that player's extracted game. Miis can be created in the launcher.

EOS uses device login: no Epic account sign-in is requested. Share invitations
privately. Windows/Linux crossplay uses a shared simulation source identity. Both players
must use corresponding builds of this release and identical game content. Each
platform verifies its own executable and libraries locally; differing ELF/PE files
are expected. Existing input and state checks stop a session on detected desyncs.
Crossplay needs gameplay testing on two computers.

## Build host

The game uses the Linux-hosted LLVM-MinGW Clang toolchain. The launcher uses
Windows Python under Wine because PyInstaller builds for its running platform.
The game is never executed as part of packaging.

1. Prepare/extract/translate the game using the existing project setup.
2. Run `bash scripts/windows/prepare.sh` on the Linux Mint/Ubuntu build host.
   The host needs `cmake`, `ninja`, `rsync`, `clang`, `curl`, `cabextract`
   and Python with pip. This prepares tools under `work/windows`, without changing system packages.
   Wine needs access to its process socket directory during packaging.
3. Keep the official EOS SDK in `eosstuff/SDK` and the local client configuration
   in `work/eos/client.json` (the latter is never copied to the source snapshot).
4. Run `bash scripts/windows/build.sh`. Default parallelism is one job to limit
   memory use. Set `JOBS=2` on machines with spare RAM; translated shards remain
   limited to one concurrent compiler.
5. Run `bash scripts/windows/package.sh 0.3.1-eos-preview`.

Outputs are in `out/windows` and `dist/windows`. The ZIP contains the launcher,
Windows native runtime, graphics DLLs, EOS DLL and bridge, extraction utility,
licenses and source snapshot. No uploading is performed.

A custom compiler can be selected with `LLVM_MINGW_ROOT=/absolute/path`.
Incremental builds use `work/windows/build`; Linux build products are separate.
Do not distribute an individual EXE: the adjacent `_internal` folder is required.

For public distribution, retain the included third-party notices and the native
source snapshot, and review Epic's SDK distribution agreement for your product.

## Windows Qt plugin packaging

The release packager explicitly includes `qwindows.dll` under
`_internal/PySide6/plugins/platforms`, plus the image and style plugins used by
the launcher. It checks the platform plugin and its Qt/Microsoft DLL dependencies
before producing the archive. This avoids relying on plugin discovery under Wine.
The 0.3.2 Windows launcher fix retains the 0.3.1 simulation identity and can pair
with the 0.3.1 Linux release.
