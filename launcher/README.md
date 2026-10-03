# Resort release launcher

A native Qt desktop launcher with the supplied ResortCompiled logo banner. It installs a
user-supplied disc, validates the translated build's exact PAL revision, creates
a Mii collection, recovers face artwork from the disc, and launches the native runtime. It does not translate or
compile game code on the user's machine.

## Build a release

Use Python **3.11+**. Build on the operating system and architecture being shipped,
with that platform's already-built `Resortcompiled` and `dtk` executables.

```sh
python -m venv .venv-launcher
```

Activate the environment using the command for your shell:

| Shell | Command |
| --- | --- |
| Fish (Linux) | `source .venv-launcher/bin/activate.fish` |
| Bash / Zsh (Linux) | `source .venv-launcher/bin/activate` |
| PowerShell (Windows) | `.venv-launcher\Scripts\Activate.ps1` |

Python creates the Fish activation script automatically. If you already created
the environment, source `activate.fish`; you do not need to recreate it.

Then install the launcher dependencies and package the release:

```sh
python -m pip install -r launcher/requirements.txt
python scripts/release/package.py --version 0.2.0-netplay-preview --archive
```

For Windows, supply `--dtk path/to/dtk.exe` and a runtime folder containing
`Resortcompiled.exe` and its required DLLs. Distribution is the **entire**
`dist/ResortLauncher/` folder; the entry point is `ResortLauncher` or
`ResortLauncher.exe`. Python and Qt are bundled. On Linux, build against the
oldest supported distribution and ship the native runtime's required adjacent
libraries. The current game build's CPU and graphics requirements still apply.

To work on the Python UI without freezing:

```sh
python scripts/release/package.py --stage-only
python launcher/app.py
```

The packager copies an explicit runtime asset list plus adjacent DLLs/shared
libraries. It does **not** copy `out/UserData`, your extracted disc, saves, logs,
shader cache, mods, or development configuration. It copies available upstream
license files into the release.

## Flow and persistence

- Launcher preferences/logs live in Qt's per-user AppLocalDataLocation.
- Disc extraction uses the bundled decomp-toolkit with an argument vector; no
  shell interpolation and no runtime downloads or dependency installation.
- Extraction runs in a worker thread with cancellable subprocess handling and
  file-backed output, preserving UI responsiveness.
- Installs stage in a uniquely named sibling directory. Game ID and DOL SHA-256
  must match before the complete installation is renamed into `Resort/`.
- An existing `Resort/` is never overwritten. Locate it to use it, or choose
  another parent directory. This first version does not implement updates.
- Saves stay under `Resort/Runtime/UserData`. A portable marker and relative
  config paths allow moving the complete installation.
- New installations generate `RFL_DB.dat` locally. No existing personal Miis or
  NAND are required. **Create / manage Miis** also initializes a missing database
  for installations made with the older launcher.
- The installer extracts the **update partition**, opens its System Menu WAD,
  and recovers `RFL_Res.dat` from a U8 content archive. This is artwork recovery,
  not artwork generation. Only the named resource is retained; the temporary
  update files are removed. No Nintendo artwork is shipped in the payload.
- If a scrubbed dump has no update partition, installation completes with Play
  unavailable until **Set up face artwork** receives a complete disc. Optional
  FaceLib imports can provide the artwork instead. The selected disc is never
  modified, and no Wii system update is installed or executed.
- **Create / manage Miis** supports all Wii appearance fields, visual feature
  pickers, names, birthday, body size, colors, editing, deletion and individual
  `.mii`/`.miigx` (74-byte) and `.rsd` (76-byte, CRC) imports. Export is 74-byte
  `.mii`. The editor and collection browser have a live 3D **head** preview using
  the installation's Wii face/hair/nose/glasses models and textures. Drag to
  rotate, scroll to zoom, and use Front view to reset. Edits are coalesced over
  45 ms and preserve the viewing angle. Height/build and body type are saved to
  the Mii record; this head view does not display the sport-specific body.
- The preview requires desktop OpenGL 3.3, NumPy and PyOpenGL (included in
  `requirements.txt` and bundled in frozen releases). Update an existing source
  environment with `python -m pip install -r launcher/requirements.txt`. Missing
  artwork or a graphics initialization error appears inside the preview pane;
  the editor remains usable.
- **Import existing collection** accepts FaceLib, a NAND root or a folder
  containing `Wii/`. Either RFL file can be supplied separately. New visible
  Miis are merged into free slots, duplicates are skipped, existing IDs and
  profiles are preserved. Incoming hidden/parade Miis are not imported.
- Existing databases must have the expected RNOD/RNHD layout and valid CRC.
  Saves preserve hidden records and unedited data, keep a timestamped backup
  in `FaceLib/Backups`, then atomically replace the database. Edits retain Mii
  IDs so existing Resort profiles remain linked. Deletion can affect profiles.
  Editing/importing and resource setup hold the same installation lock as Play.
- Play preserves unrelated TOML fields/comments, selects the disc/NAND paths
  and edition overlay roots, and uses the runtime with no CLI arguments.
- A per-user launcher lock and per-installation game lock prevent duplicate
  launchers from modifying a running game's configuration.
- Failed starts/nonzero exits surface a message and a path to logs. Closing the
  launcher while installing offers cancellation; while playing it asks the user
  to close the game first, so a QProcess destructor cannot kill it accidentally.

## Riisorted integration point

Both editions use the same native executable and currently share saves/settings.
Original sets `paths.overlay_roots = []`. Riisorted sets it to the installation's
`Editions/Riisorted/files/`, which mirrors disc file paths. Riisorted now exposes
**Online play · Experimental**, with Host/Join, invitations, guest Mii selection,
and optional keeping of guest Miis after the session. Online progress stays in
an isolated session folder. See [online instructions](../docs/Riisorted-online-development.md).
Existing installations need the newly built `out/Resortcompiled` copied into their
`Runtime/Resortcompiled`, or a newly packaged runtime; both peers must use the same
build. Automatic updates and downloaded packs remain future work.

## Source and credits

The launcher is GPL-3.0 and adapts Mii format handling and feature-picker artwork
from Team Wheel Wizard. See [THIRD-PARTY.md](THIRD-PARTY.md) for the pinned source
revision, changes and license. Packaging includes launcher sources and assets
under `source/`, plus the build scripts. The native runtime remains separately
licensed. A launcher-only rebuild can use the shipped runtime payload with
`--runtime-dir` and `--dtk` pointing into the original release's payload folder.

## Handoff

No launcher execution, packaging run or automated/manual tests were performed
for the Mii changes and 3D preview, as requested. Source and disc inspection established that
this local Resort image's System Menu WAD contains `RFL_Res.dat` in
`00000004.app`; installation scans content archives instead of hardcoding that
index. In-game compatibility of newly created Miis remains for user verification.

User verification should cover first install with a complete and a scrubbed
disc, cancellation, Mii creation/editing, existing-database import and backups,
individual Mii import/export, 3D head appearance and rotation/zoom, appearance
and profile persistence in Resort,
both editions, reopening/relocating an install, and game exit/crash.
