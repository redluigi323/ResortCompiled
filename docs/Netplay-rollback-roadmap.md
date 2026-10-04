# Netplay timing and the rollback path

## Current finding (2026-10-04)

The user connected Linux and Windows over direct EOS P2P in preview 0.3.3.
The reported session used a 7-frame delay at 57 ms measured RTT, ran around
56 FPS, and stopped at capture interval 1983 immediately after selecting
Swordplay from the menu. Steady-state missing-input waits were 0.5–0.6 ms;
the first 300-interval average included a large startup/loading wait.
The old failure text does not say whether frame, requested MotionPlus mode,
or solver output differed. That cause cannot be recovered from those logs.

Preview 0.3.4 saves only the failed boundary to `desync.json`, including both
original captures and frame/mode/solver values. It does not record a continuous
input trace. Do not suppress the synchronization check to continue a match.

The VI deadline and audio poll time derive from `GuestClock::SchedulingNow`.
Previously every bridge exchange excluded all its elapsed time, even normal
loopback/EOS enqueue overhead. This stretched the real-time budget for video
and audio on every input interval. The correction counts up to 2 ms of normal
exchange work toward guest time and excludes any excess. The guest clock
remains frozen during the exchange; only this bounded amount is retained when
it ends. This addresses clock overhead, not slow hardware or every desync.

Delay now estimates one-way travel as half the measured RTT and adds two frames
for jitter, bounded to 2–30 frames. At 57 ms RTT that chooses 4 frames, about
67 ms, instead of 7/117 ms. Asymmetric latency and large jitter can still exhaust
the buffer; missing inputs then pause play. Delay stays fixed during a session
so no capture is silently repeated or dropped. Send-enqueue time is reported
separately from missing-input waits.

## Direction: rollback, beginning with deterministic execution

Keep EOS identity/lobbies/P2P and isolated host-save/Mii setup. The replication
layer needs deterministic execution and reversible state before prediction.
GGPO's reference API requires save/load and exact frame advancement callbacks:
[GGPO callbacks](https://github.com/pond3r/ggpo/blob/master/src/include/ggponet.h),
[developer guide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md).
Adding a transport or input prediction alone does not provide those callbacks.

### 1. Diagnose the Swordplay transition and establish a simulation boundary

Use the failure pair to distinguish different modes/frames from different
results with the same requested modes. Audit sport-loading completion order,
MotionPlus calibration, guest time/RNG seeding and callback delivery at that
boundary. Add canonical state checks at chosen boundaries; the current digest
covers MotionPlus output, not every game object or guest byte.

`g_gxFrameCount` is currently the input interval boundary. Presentation must be
separated from a logical simulation tick before rollback; a render frame is not
necessarily one complete, independently replayable gameplay update.

### 2. Make guest events follow shared logical progress

Audit `guest_clock.cpp`, `hle/vi.cpp`, `hle/os/os_alarm.cpp`, `os_sleep.cpp`,
`fiber_manager.cpp`, `hle/audio/audio.cpp`, NAND/DVD completion and GX readback.
Both peers must dispatch the same game-visible events in the same order.
Preserve guest polling-loop progress; freezing PPC time for a complete frame
without execution accounting can deadlock polling and waits. Renderer waits
and the audio device must not select simulation order. The 0.3.4 clock-budget
fix is still based on local elapsed time and is not this deterministic clock.

### 3. Define and implement complete reversible state

| State | Current restore obstacle / requirement |
| --- | --- |
| Guest memory and CPU | Serialize memory regions, current/saved registers and guest allocator state at a quiescent point |
| Guest contexts | `host_context.cpp` uses Windows Fibers / Linux libco. Handles and suspended native C++ stacks have no portable restore API |
| Scheduler/timers | Save thread states, retraces, alarms, sleeps, logical clock and pending interrupt/callback order |
| HLE storage/DVD | Preserve queued completions and guest results; speculative writes must not reach personal or session saves until confirmed |
| Wii input/MotionPlus | Save both remote solver/calibration histories, recenter state, applied sequence and sample queues |
| Graphics feedback | Canonical game-visible EFB results and completion order; GPU handles/presentation queues cannot be restored as guest state |
| Audio | Save guest DSP/AX state; replay must not emit duplicate sounds or submit speculative output twice |

Native fiber continuation is the largest whole-runtime snapshot obstacle.
Reconstructable execution continuations or a deliberately selected game update
boundary are required; a RAM dump plus PPC registers cannot resume arbitrary
suspended translated C++ calls. Snapshots remain local to each peer; crossplay
compares canonical state instead of transferring host pointers between OSes.

### 4. Prove restoration before adding prediction

At an agreed boundary: save, advance several frames with known inputs, restore,
and replay those inputs. Require matching canonical state and game outcomes,
including sport loading, round endings and recenter/calibration. Only after this
passes should an EOS packet carry speculative frame inputs.

### 5. Add a bounded rollback window

Predict conservatively: retain held buttons/pose where appropriate, avoid
inventing repeated angular velocity or accelerometer spikes. On late input,
restore the last confirmed snapshot and replay corrected frames without display
or audio duplication. Bound CPU replay work and snapshot memory; pause when the
window is exhausted rather than applying an uncorrectable swing. Add pacing
that keeps peers close without permanently slowing the audio clock.

First gameplay target remains Swordplay Duel. Future true-online sport mods can
use a smaller gameplay state boundary, but that requires a verified inventory
of sport state and authoritative game hooks. It is a separate implementation
from arbitrary whole-runtime rewind, not a shortcut already supported here.

## Test ownership

The agent runs compilation, protocol/clock harnesses and artifact checks.
The user runs actual Swordplay loading/matches and Linux/Windows crossplay.
Preview 0.3.4 is a timing/diagnostic update; rollback and the reported desync
cause are not claimed fixed by its synthetic tests.

## Follow-up: preview 0.3.5

Both 0.3.4 boundary reports identify the same solver-only divergence at capture
947/render frame 948 with modes [5,5]. Linux/Windows translated SDK harness outputs
matched byte-for-byte from equal initialization. Online MotionPlus device reports
and scalar calibration state are now host-owned and shared with the guest;
see [EOS-netplay.md](EOS-netplay.md#motionplus-authority-preview-035). This addresses
duplicate device-state processing while retaining game mode/frame guards. It does
not establish deterministic gameplay or implement rollback. Actual sport loading
and matches still need user testing.

## Rollback implementation started (2026-10-04, after the 0.3.5 match)

The host log for `host-1791095594504102452-a1a02e` continued past input interval
5,400 with matching controller checks. The players nevertheless observed
different winners and movement. This is gameplay-state divergence outside the
MotionPlus digest. The log contains missing-input waits and invalid guest reads
in the `0x801CF97C -> 0x8026928C` path; it does not prove that a loading stall
caused the divergent result. There is no full match-state digest in this build.

Implemented source, included in the native build but **not connected to live
gameplay or advertised as a new playable release**:

- `runtime/wsr/include/netplay/rollback.h` and `src/netplay/rollback.cpp`:
  two-player logical-frame history, prediction callback, late-input correction,
  save/load and corrected replay, ordered confirmation callbacks, and effect
  invalidation callbacks. Default prediction window is eight frames, capped at
  thirty. Snapshot bytes default to 64 MiB; input size and future buffering are
  bounded. A full prediction window pauses advancement. Invalid/conflicting or
  stale input is rejected. Adapter failures stop the session rather than letting
  an uncertain state continue. Replay work is bounded by the frame window.
- `PredictMotion`: holds buttons, pose and pointer, clears angular velocity,
  incremental angles and recenter, and replaces the acceleration pulse with a
  unit-length held-gravity estimate. It produces one sample at 1/60 second.
  This is an initial prediction policy, not a validated Swordplay feel change.
- `netplay/checkpoint.h`, `checkpoint.cpp`, `guest_ram_checkpoint.cpp`:
  immutable RAM images sharing unchanged 32 KiB pages. Cached and uncached guest
  windows are represented once per physical RAM bank. Retained page allocation
  is capped at 256 MiB by default; reference metadata is additional bounded
  storage. Restore validates the complete layout before writing. The canonical
  digest includes layout and byte content. It is a local restoration diagnostic,
  not a cross-peer gameplay-state checksum. Full scanning is a correctness-first
  implementation; dirty-page tracking/performance work remains before 60 Hz use.
- `SimulationClock::Save/Restore`: exact tick count, rational interval remainder
  and rate. The live `GuestClock` still follows local wall time. This does not
  silently activate deterministic scheduling or change existing EOS sessions.

### Reproducible implementation checks

```bash
python3 scripts/test_rollback.py --sdk
python3 scripts/test_rollback.py --windows --sdk
```

The native runtime must have been rebuilt first; both builds use one compile
job. The Windows check uses the local Wine/toolchain setup. No gameplay startup,
graphics window, continuous trace or personal save modification is involved.

The core harness compares two peers with delayed/reordered inputs to a
known-input baseline over hundreds of frames. It checks actual rewind/replay,
identical final state and confirmed effect streams, duplicates, malformed/stale
inputs, prediction exhaustion, memory caps, immutable page restoration and
fractional clock rewind. The effect checks exercise the adapter contract;
they do not claim the game's audio/NAND backends are already speculative-safe.

The component harness calls the actual translated MotionPlus SDK, captures both
physical RAM banks and the CPU context at a **returned SDK-call boundary**,
advances twenty samples, restores and replays them. It requires identical SDK
outputs, RAM digest and relevant CPU registers, then repeats with corrected
inputs. Linux and Windows under Wine passed. Approximately 152 MiB of page
storage was retained across these images; unchanged RAM was not copied for
each image. This proves that component at that boundary, not a running match.

### Required next integration

1. Make translated execution continuations restorable at a logical game-update
   boundary. The present Windows Fibers/Linux libco handles retain suspended
   native C++ stacks. CPU registers and RAM alone cannot recreate their locals,
   return paths or lifetime state. Do not attach RAM restore to `KPADRead` or
   `g_gxFrameCount` and call that a game savestate.
2. Register scheduler, VI, alarms, sleeps, input histories and every pending
   callback/completion as checkpoint state. Replace local elapsed time and
   host-worker readiness with deterministic, recorded logical events. Audit
   invalid guest reads from the new run before treating either simulation as a
   trustworthy baseline.
3. Isolate game-visible graphics feedback and stage audio/save effects by
   logical frame. Wire the core's `discardFrom`/`commit` callbacks to those real
   backends; replay must not duplicate writes or sounds.
4. Require save/advance/restore/replay equality for a whole Swordplay update,
   including loading and round completion. Add a canonical gameplay-state
   digest that actually covers winner/score/physics. Canonical controller
   digests cannot detect the disagreement in this run.
5. Then replace the broker's mandatory agreed-input wait with EOS frame-input
   submission and bounded prediction/correction. Lobby identity, isolated host
   saves, shared session Miis and Linux/Windows compatibility remain applicable.
   The 0.3.5 host-owned MotionPlus processing also needs a replay-capable device
   adapter; canonical live reports alone cannot solve predicted past frames.

EOS integration and in-game lobby menus are later layers. The current archives
remain 0.3.5; they do not contain an enabled rollback match implementation.

## Suspended continuation checkpoint work

An explicit `HostContext::Backend::Rewindable` now provides local suspended
continuation images on Linux/Windows x64. Default runtime execution still uses
the original platform backend; there is no launcher switch or environment flag
that enables this in matches. macOS/other architectures reject selection.

The new backend uses the vendored libco register block with owned fixed-address
stacks. A guard page separates the control block from the descending stack.
Images copy the saved register block and used stack tail (including a 128-byte
margin), plus the floating-point environment and explicit MXCSR controls.
Handles and native pointers stay local. Only the owning scheduler thread may
capture/restore a suspended worker. Current contexts, the scheduler itself,
wrong-thread operations, stale generations and changed layouts are rejected.
Workers must remain alive across the retained images; retired stacks are not
resurrected. Each owned stack is bounded to 64 KiB–16 MiB.

The Windows experimental path switches the thread's stack base/limit fields
alongside libco. This uses platform internals and requires real Windows testing
before game use; Wine validation does not establish compatibility with every
Windows configuration. The default Windows Fibers backend is retained.
Microsoft documents the TEB as an internal structure whose layout may change:
[TEB documentation](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-teb).

`GuestFiberManager::CaptureSuspendedWorkers/RestoreSuspendedWorkers` also
checkpoints worker CPU registers and scheduling metadata. Every continuation
and worker identity is validated before the first stack write. A changed worker
inventory, pending deletion, retired worker or non-quiescent caller is rejected.
This API deliberately excludes the main/scheduler continuation, guest RAM,
timers, pending interrupts and CPU TLS. It is a scheduler component checkpoint,
not a full game savestate.

Reproducible checks, after rebuilding native binaries:

```bash
python3 scripts/test_rollback.py --continuations --workers
python3 scripts/test_rollback.py --windows --continuations --workers
```

The continuation probe restores twenty nested native call frames, live stack
locals and floating-point/NI controls; original and corrected input replays
must match their respective baselines. It checks owning thread, active/scheduler
capture rejection, stale context rejection and changed owner identity. It also
runs the original default-backend handoff check. These probes passed on Linux
and Windows under Wine.

The relinked actual guest-manager probe restores a suspended guest worker's
native stack, saved CPU metadata and both physical RAM banks, then requires
identical original and corrected replays. It rejects altered identities and
changed worker topology. This component passed on Linux. The Windows executable
compiled, but the Wine run stalled and remains unverified; add a subprocess
timeout before retrying. Standalone Windows continuation checks did pass.
No gameplay startup, graphics window, personal save mutation or continuous
instruction trace is involved.

### Remaining execution-boundary work

This removes the opaque-stack obstacle for **live suspended workers with stable
topology**, not for arbitrary game execution. The main guest thread presently
shares the outer scheduler's native stack; it must run on a separately owned
context so the controller can stop and restore it from outside. Frame-boundary
handoff and guest-worker lifetime retention/recreation are still required.

Raw stack images also retain pointers to host heap objects and thread-local
state. Replaying across host allocation/free, mutex ownership or other RAII
lifetimes is unsafe until those lifetimes are either checkpointed or excluded
from the selected boundary. Timers/VI/alarms/completion order, host heap state,
graphics feedback and speculative audio/save effects remain separate required
domains. Whole-Swordplay restore/replay and canonical score/physics comparisons
must pass before enabling predicted online frames. No new playable rollback
archive has been packaged.
