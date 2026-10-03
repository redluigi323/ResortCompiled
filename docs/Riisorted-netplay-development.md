# Riisorted online development: first foundation

## What exists

- A versioned, endian-independent motion input format, including buttons,
  acceleration, pointer state, gyro, orientation, sample duration, recenter
  actions, and requested MotionPlus mode. Samples are ordered oldest first.
- A bounded two-player lockstep queue. It returns inputs only when both players
  have supplied the next interval. Duplicate packets are accepted only when
  identical; conflicting packets and inputs beyond the window are rejected.
- An integer simulation clock with exact rational interval advancement.
- A central `GuestClock::ReadTimeBase()` entry point for PPC time-base reads.
  It still uses offline wall time; the logical clock is not activated in gameplay.
- Sleep deadlines and stranded-thread repair timers now use Broadway ticks from
  that same clock. Multiple repaired threads wake in sorted guest-address order.
  VI event timing and audio elapsed-time sampling use `GuestClock::SchedulingNow()`;
  host presentation pacing continues to use its host clock.
- Opt-in recording and playback of the virtual Wii Remote's mapped inputs before
  the translated MotionPlus solver. Playback runs that solver again, preserving
  its guest-state updates and calibration behavior rather than substituting only
  finished KPAD output bytes.
- Per-batch comparisons of the translated MotionPlus outputs. A diagnostic
  records the first differing batch and the number of differing batches observed.
  These comparisons cover the solver outputs, not the rest of the game state.
- A local development helper that snapshots initial NAND/settings, preserves the
  runtime assets, hashes game/overlay content, and launches against isolated saves.

**Recording is not a deterministic replay guarantee.** The logical simulation
clock does not currently govern runtime execution. A separate
[live online prototype](Riisorted-online-development.md) now provides direct TLS
transport, host/join setup, two virtual remotes, and temporary save/Mii sharing.
It checks MotionPlus outputs but does not hash complete game state. Normal
offline input mappings remain the same.

The motion format can later feed another game integration point without coupling
networking to Swordplay-specific addresses. The current tape integration targets
WSR's existing virtual remote and KPAD path.

## User-run recording and playback

The executable is rebuilt into `out/Resortcompiled`. The helper needs Python
3.11 or newer and the existing configured portable `out` folder. Commands work
in fish and Bash; no virtual-environment activation is needed.

From the project directory:

```sh
python3 scripts/netplay_session.py record work/netplay/swordplay-01
```

Play a short Swordplay Duel session using the DualSense or keyboard/mouse, then
close the window normally. The folder name must be new. Initial setup hashes
game content and copies the starting NAND; this can take time and disk space.
The helper does not import the resulting progress back into your normal save.

Then:

```sh
python3 scripts/netplay_session.py replay work/netplay/swordplay-01
```

Playback starts from the stored initial save/configuration and uses recorded
motion rather than live gameplay input. It creates a new run folder each time.
The F10 UI and window close remain available. If the recorded stream completes,
the game exits successfully. Physical Bluetooth Wii Remotes are not supported by
this first recording path; use the virtual remote.

After rebuilding, use a new session name such as `swordplay-02` to exercise the
new runtime. Existing sessions deliberately replay their preserved executable;
replaying `swordplay-01` does not test changes in the newly built `out` executable.

To record from another portable runtime installation:

```sh
python3 scripts/netplay_session.py record work/netplay/swordplay-02 --source-out /path/to/Runtime
```

## What to report

- Whether normal offline controls still behave as before.
- Whether recording reaches Swordplay and captures a complete short run.
- During playback, the first visible difference, especially a different menu,
  calibration, sword pose, round result, or timing.
- Any `Motion playback diverged at batch ...` message and that run's logs.
- Any `first MotionPlus solver output difference at batch ...` message. Playback
  continues after a solver difference so the user can observe the game behavior.
  Zero solver differences still does not prove complete simulation agreement.

The tape validates batch sequence, rendered-frame label, and MotionPlus mode.
Those checks are input-schedule diagnostics, **not full game-state comparisons**.
Identical labels do not prove identical simulation. Frame labels are temporary;
the deterministic scheduler will replace them with simulation intervals.

## Session folders

```text
work/netplay/swordplay-01/
  manifest.json             runtime/content hashes and motion convention settings
  assets/                   preserved executable and required runtime resources
  initial/UserData/         immutable starting settings and NAND copy
  motion.riisinput          mapped motion batches, checksums, clean end marker
  motion.riisinput.solver   per-batch MotionPlus output fingerprints
  record/UserData/          recording run's separate saves and Logs
  replay-.../UserData/      each playback run's separate saves and Logs
```

The development helper copies the local NAND to reproduce the baseline. This is
local-only; it does not define what future peers will exchange. Online session
transfer will use the restricted file set described in the design plan.

Game files and overlays are read from their original locations; their hashes
must match before playback. Do not edit them while a run is active. The captured
executable stays inside the session even after the normal runtime is rebuilt.

The recording has bounded, checksummed packets and is flushed per batch. Normal
window close seals it explicitly because this runtime bypasses `atexit` from
guest fibers. Interrupted/crashed recordings may retain a usable prefix, but
playback reports missing data/end markers rather than quietly calling it a
complete run. More than 128 solver samples in one captured batch is an explicit
backlog error, not silently dropped motion.

Only one helper process may use a session at a time. A force-killed helper can
leave `.session.lock`; remove it only after its child game process is gone.

## Next runtime work

1. Define deterministic execution accounting inside a retrace interval, including
   guest polling loops. Do not freeze time for a whole frame or advance it from
   arbitrary renderer callbacks.
2. Convert VI, alarm, sleep, audio, and storage completion scheduling to that
   clock and ordered event boundaries. Sleep deadlines now use clock ticks;
   VI/audio clock access is centralized, but event advancement is still driven
   by wall time. Keep presentation waits independent.
3. Establish a canonical state inventory and comparisons across replay runs.
4. Refactor remote/solver state for multiple channels and independent calibration.
5. Connect the input gate to simulation, then implement transport and session UI.

Gameplay acceptance remains with the user. Build completion and Python syntax
checks do not establish synchronized gameplay.

## First user run

The `swordplay-01` recording contains 4,973 batches and 15,102 motion samples and
has a clean end marker. Its recording/playback logs contain no reported input
schedule mismatch. The user observed broadly consistent actions but a different
final-round knockout outcome. That is evidence of remaining outcome divergence,
not proof of deterministic simulation; the current logs cannot establish its
cause. The next recording includes solver comparisons to narrow the difference.
