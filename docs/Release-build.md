# Build and package a netplay preview

## Linux source build

The checked-in runtime, symbol map and patches are sufficient for normal release
builds. A Mario Kart Wii reference dump is only needed for researching/re-keying
the runtime, not building this version. Do not run `scripts/rekey.py` as part of
a release build; it regenerates the runtime.

On Linux x86-64 with Python 3.11+, a supported PAL Resort disc, and enough space
for extracted data and the large translated build:

```sh
git clone https://github.com/redluigi323/ResortCompiled.git
cd ResortCompiled
bash setup.sh
bash scripts/release/build.sh /absolute/path/to/WiiSportsResort.rvz 0.2.0-netplay-preview
```

These commands work from fish as well as Bash because shell scripts are explicitly
run through Bash. `setup.sh` installs dependencies and fetches the pinned
WiiCompiled revision. On a machine with dependencies already installed, use
`bash setup.sh --no-apt`. The build helper checks the disc revision, configures
local paths from the checked-in project template, translates the game, compiles
it, creates a launcher virtualenv, and freezes/packages the launcher.

Memory limits native compile concurrency automatically. An initial build can
take considerable time and disk space. Run on the oldest Linux distribution you
intend to support; the resulting binary's glibc requirements follow the build
machine. Current locally produced preview needs glibc 2.38+.

## Package an already built runtime

```sh
python3 -m venv .venv-launcher
.venv-launcher/bin/python -m pip install -r launcher/requirements.txt
.venv-launcher/bin/python scripts/release/package.py --version 0.2.0-netplay-preview --archive
```

This does not recompile or launch the game. Linux packaging copies libpng and
rewrites native library lookup to `$ORIGIN`, so the release does not depend on a
maintainer's build directory. Native session copies include those adjacent
libraries. The full Qt/Python launcher, runtime assets, credits, setup docs and
source snapshot are included. Disc images, extracted game files, generated game
translations, private NAND data, caches, logs, invites and developer configuration
are excluded from the source snapshot and personal data from the payload.

Output:

- `dist/ResortLauncher/`: complete unpacked release.
- `dist/ResortCompiled-VERSION-linux-x86_64.tar.gz`: share this entire archive.
- The matching `.sha256`: upload alongside it.

## Publish the preview on GitHub

Commit the source to ResortCompiled first. On its **Releases → Draft a new release**
page, choose that commit, create a version tag such as `v0.2.0-netplay-preview`,
mark it **pre-release**, and attach the archive plus checksum. Include the player
setup guide link and make clear that the same-PC test passed while remote testing
is still pending. Both testers should download that one archive.

The connected GitHub tools can publish source commits but do not expose release
asset uploading. The archive can also be shared directly with a tester before a
GitHub binary release is attached. Do not commit the archive or runtime executable
to the source tree.

Suggested release notes:

> Experimental Linux x86-64 Riisorted netplay preview. Includes disc installation,
> Mii creation with 3D previews, Host/Join invitations, two virtual Wii Remotes,
> host-save session setup and temporary guest Miis. Connect through ZeroTier for
> the first two-PC tests. Swordplay Duel is the first target. The same-PC test
> passed; remote gameplay is not verified and desync-free play is not guaranteed.
> Requires your own supported PAL RZTP01 disc and Linux glibc 2.38+.

Gameplay testing remains with the user. Packaging checks are static inspection,
Python/shell syntax checks, and compilation; they are not match verification.
