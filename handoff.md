# ResortCompiled / Riisorted handoff

Updated 2026-10-04. Development paused at the user's request for a PC migration and a future native Apple Silicon/macOS port.

## Project and priorities

ResortCompiled is a native Wii Sports Resort port based on WiiCompiled; Riisorted is the edition intended for quality-of-life additions and online play. Supported disc: PAL RZTP01. The user supplies their own disc. Current work targets Linux and Windows x86-64 crossplay. Next priority is completing safe rollback, followed by real two-player testing; macOS/arm64 is a new port, not an existing supported release.

The user prefers handling gameplay testing themselves. Avoid continuous instruction tracing. Keep build concurrency low: this PC has roughly 8 GiB RAM and previously exhausted memory during compilation. Use one compiler job and build platforms sequentially. The user uses fish; activate the launcher environment with `source .venv-launcher/bin/activate.fish`, or call its Python directly.

## What exists

- SDL3 controller support, DualSense sensors and virtual Wii Remote/MotionPlus input; keyboard/mouse fallback and F10 controls settings. Motion mapping remains a gameplay-sensitive area.
- PySide6 release launcher: owned-disc installation, managed runtime, regular/Riisorted edition selection, Mii creation/import, RFL generation and 3D previews.
- EOS device identity login, private invitations and P2P transport without an Epic account sign-in. Host saves are used in an isolated session; guest Miis are available for the session, with an optional keep action.
- Runtime/content are shared between sessions; mutable data is redirected using `RESORT_USERDATA_DIR`, avoiding an entire runtime copy per session.
- Crossplay compatibility uses a shared simulation source identity, rather than requiring identical Windows/Linux executable hashes. Local package integrity still checks its own executable.
- Host-authoritative MotionPlus SDK processing and bounded input buffering. The live game remains synchronized-input netplay; rollback is groundwork only.

Relevant reading: `README.md`, `docs/Player-setup.md`, `docs/Release-build.md`, `docs/Riisorted-online-development.md`, and especially `docs/Netplay-rollback-roadmap.md`.

## Latest real online result and unresolved issue

EOS connectivity works across Linux/Windows. Early 9–10 FPS performance improved significantly. Preview 0.3.5 reached Swordplay, but players saw different winners and then different movement without the controller-state guard stopping the session. Matching controller/MotionPlus state does **not** establish matching game state. Loading stalls are a user observation, not a proven root cause. Rollback must address deterministic simulation and state coverage; it cannot automatically repair nondeterminism outside its snapshots.

Last supplied host evidence on the old PC:

`~/.local/share/Riisorted/Resort Launcher/Netplay/host-1791095594504102452-a1a02e/online.log` and `native.log`.

The native log includes invalid reads at `0xC841D4BC`, `0xC840949C`, `0xC84234B8`, active function `0x801CF97C -> 0x8026928C`, and saved LR `0x8026C3A0` in `0x8026C268`. Preserve these logs privately if continuing investigation. They are not checked into source.

## Rollback implementation status

### Completed foundations

- `runtime/wsr/include/netplay/rollback.h` and `src/netplay/rollback.cpp`: transport-independent input history, prediction, corrected replay, confirmation callbacks, bounded windows/memory and fail-closed validation. Motion prediction holds pose/buttons while removing transient swing velocity; it is not a complete physical orientation solver.
- `netplay/checkpoint.h`, `checkpoint.cpp`, `guest_ram_checkpoint.cpp`: immutable shared 32 KiB pages, budgeted storage, MEM1/MEM2 physical RAM capture, restore validation and local digest. This digest is not yet a cross-peer canonical game-state hash.
- `SimulationClock::Save/Restore`: deterministic tick/remainder state. The live guest clock still needs integration with rollback.
- Explicit opt-in `HostContext::Backend::Rewindable`: suspended x64 native continuation capture/restore with owned guarded stacks, identity/generation/thread checks, floating-point state and stack images. The default execution backend remains unchanged.
- `GuestThreadManager` worker checkpoints: suspended worker continuation plus saved CPU/scheduling metadata, stable inventory validation and restore. The main guest thread and scheduler are excluded.

### Verification actually completed

Linux and Windows native game builds completed sequentially. The rollback core/page/clock harness and translated MotionPlus SDK restore/replay harness passed on Linux and Windows under Wine. The standalone continuation probe and original context handoff probe passed on Linux and Windows under Wine. The actual guest-manager checkpoint harness passed on Linux.

**The Windows actual guest-manager harness stalled under Wine and remains unverified.** Its executable compiled after renaming a helper that collided with the Windows `Yield` macro. Do not report that test as passed. The runner currently lacks a subprocess timeout; add a bounded timeout before retrying. Real Windows testing is still required for the experimental stack/TIB switching path.

Test entry point: `scripts/test_rollback.py`; relevant switches are `--sdk`, `--continuations`, `--workers`, and `--windows`. Harness sources live in `scripts/tests/`. Do not run multiple large relink/build checks together.

### Still needed before enabling rollback in a real game

1. Give the main guest execution thread a separately owned continuation; define a quiescent frame boundary reachable from the outer scheduler.
2. Retain/recreate worker lifetimes across the rollback window, rather than accepting only a stable live worker inventory.
3. Account for host heap/TLS/RAII lifetimes referenced by captured stacks. Stack plus physical RAM alone is insufficient and can retain invalid pointers.
4. Snapshot/restore CPU and scheduling state, VI/timers/alarms, async completion ordering, DVD/DSP and all relevant HLE state. Audit host-time and host-thread nondeterminism.
5. Handle graphics feedback and presentation; defer or discard speculative audio, saves and other effects until confirmation.
6. Connect EOS input transport to the rollback adapter; add canonical game-state checks and useful desync captures.
7. Test original/corrected replay across actual game frame boundaries, then Swordplay and Linux/Windows crossplay with latency/loss/jitter.

There is no enabled gameplay rollback setting, no launcher rollback checkbox, and no new release archive containing the latest continuation work. Do not expose this as a supported mode yet.

## Build and release state

Last prepared local archives are `dist/linux/ResortCompiled-0.3.5-eos-preview-linux-x86_64.tar.gz` and `dist/windows/ResortCompiled-0.3.5-eos-preview-windows-x86_64.zip`, with checksum files. These predate the newest rollback foundations. The user uploads release archives themselves.

Their simulation identity was `b79f6633dd1b56c894bc927429e63b0bb9f67a3ac0188c663e5e3023ca19026b`. The latest Linux/Windows development builds share `248e539bc518d06c60cf699d165d3a0b7bbcd21e5dad5e100f52abfd8f9b8828`. Any further source change can change that marker; recompute and rebuild both players consistently. Current online protocol is version 5.

WiiCompiled upstream is pinned to `a88b7b502b620d38384e39aa7813c4ebae9a1f0c`. Normal builds use checked-in runtime/project mappings; `scripts/rekey.py` is a research regeneration tool and is not needed for ordinary owned-disc builds.

Linux setup/build sequence (see scripts and release docs for details):

```sh
bash setup.sh
bash scripts/extract.sh /absolute/path/to/owned/Resort.rvz
python3 scripts/configure_project.py
bash scripts/translate.sh
bash scripts/build.sh --jobs 1
python3 -m venv .venv-launcher
.venv-launcher/bin/python -m pip install -r launcher/requirements.txt
```

Windows build uses `scripts/windows/` with LLVM-MinGW; `JOBS=1 bash scripts/windows/build.sh`. Packaging uses `scripts/release/package.py`; inspect its options and EOS configuration before packaging. The Linux release convenience script currently chooses its own default job count; use the explicit sequence above on small-memory machines.

## Moving to another PC

Clone `https://github.com/redluigi323/ResortCompiled.git`. Source publication excludes generated game code, extracted disc content, executables, package payloads, development environments and personal data. Recreate these locally.

Privately transfer if wanted:

- Your owned disc image.
- Your EOS SDK (`eosstuff/SDK`) and local client configuration (`work/eos/client.json`). Keep the latter private, with restrictive permissions; never commit it or print its secret. SDK redistribution/build packaging must follow its terms.
- Personal launcher library/settings/saves under `~/.local/share/Riisorted/Resort Launcher`, plus development `out/UserData` if needed.
- The important session logs above and any local release archives you still want to distribute.
- `work/generated`/`work/extracted` are optional large caches, regenerable from your disc. Linux toolchains/Wine prefixes are not macOS tools.

A fresh clone should not require any old absolute home-directory path. If configuration still contains old paths, rerun project configuration and update the installed disc/runtime location through the launcher.

## Native Apple Silicon/macOS starting points

There is existing Apple groundwork in `src/platform/macos/co_switch.S`, the legacy Apple arm64 branch of `host_context.cpp`, `guest_flat_memory_macos.cpp`, and macOS context ABI/memory tests. It has not been validated as a complete ResortCompiled macOS port. Preserve Darwin's reserved x18 handling.

Port tasks:

- Adapt setup/tool downloads/build scripts: current dependencies, `nproc`, `/proc/meminfo`, linker flags and tool URLs assume Linux. Build arm64 dependencies and translated code natively; use conservative compilation concurrency.
- Audit runtime CMake, SDL3/Aurora/Dawn graphics and platform libraries on arm64/macOS.
- Add macOS launcher executable/library discovery and PyInstaller packaging. Current package platform choices are Linux/Windows; macOS needs dylib install names/rpaths and a usable app bundle/signing strategy.
- Add macOS EOS SDK/bridge build and transport support. `netplay/live.cpp` currently selects Linux/Windows socket paths; confirm EOS SDK arm64 availability privately from the SDK distribution.
- The new rewindable native continuation backend is x64-only. Implement and validate arm64 register/FP/stack snapshots before claiming macOS rollback support; existing legacy assembly is only the starting point.
- Audit floating-point/PPC math determinism across arm64 and x86-64, then compare actual frame state and corrected replays across all platforms. Sharing a source compatibility marker alone cannot guarantee architectural determinism.

Continue from `docs/Netplay-rollback-roadmap.md`; keep experimental continuation work separate from enabling live gameplay until state coverage and replay safety are established.
