# Resortcompiled — Research Notes (Sept 29, 2026)

Goal: native PC port of **Wii Sports Resort** (WSR) via static recompilation, then add
(1) frame interpolation, (2) arbitrary resolution / aspect ratio, (3) online play.
Miis are out of scope (made externally, e.g. mii.nxw.pw).

---

## TL;DR

- **Use WiiCompiled as the base.** Its translator is already game-agnostic (YAML manifest, DOL + REL support), its
  runtime already HLEs the Wii-only SDK layers aurora lacks (WPAD/KPAD, AX audio, NAND/ISFS, IOS, net sockets),
  and its **aurora fork already does frame interpolation and live aspect-ratio changes**. Goals 1 and 2 are mostly
  "port what's there"; goal 3 is new work.
- **The runtime is not game-agnostic yet.** ~550 HLE hooks are keyed to MKW-PAL *addresses*
  (`OSSleepThread_HLE_801aa9b8`, etc.). We need a WSR symbol map and re-keying of every hook. This is the bulk of
  the early work.
- **Biggest WSR-specific gaps:** Wii MotionPlus (WiiCompiled has none), IR pointer (WiiCompiled has none yet), and
  a WSR symbol map (no WSR decomp exists; Wii Sports' `ogws` decomp is the closest relative).
- **Online:** start with deterministic **lockstep/delay-based input sync** (like Dolphin netplay), P2P with NAT
  hole-punching. Rollback is a later upgrade, since recomps can snapshot guest RAM far cheaper than an emulator.
- **License:** building on WiiCompiled makes the project **GPLv3**. Aurora is MIT. Ship no Nintendo code; translate
  on the user's machine from their own disc (same as WiiCompiled / Deep Sea).

---

## 1. The landscape

| Project | What it is | Relevance |
|---|---|---|
| [WiiCompiled](https://github.com/patchzyy/wiicompiled) (patchzyy) | Full MKW PAL static recomp. C# translator → C++ → Clang (x86-64-v3 / ARM64). Win/Linux/macOS. GPLv3. | **Primary base.** Translator + Wii runtime + aurora fork. |
| [aurora](https://github.com/encounter/aurora) (encounter) | Source-level GC/Wii compat layer: GX on WebGPU/Dawn (D3D12/Vulkan/Metal), VI, PAD, DVD (nod), CARD, SDL3, ImGui, RmlUi. MIT. Powers Metaforce, Dusklight. | Graphics/window/input backbone. **GameCube SDK surface only** — no WPAD/KPAD/NAND/AX (checked the headers). |
| [DolRecomp](https://github.com/ExpansionPak/DolRecomp) (ExpansionPak) | PowerPC → C static recompiler for DOL/REL (+ experimental Wii U RPX). CPU-only, GPLv3, SMC unhandled. Public ~June 2026. | Alternative translator. No Wii runtime of its own. |
| [ModernGekko](https://github.com/ExpansionPak/ModernGekko) / [template](https://github.com/ExpansionPak/ModernGekko-Template) | Runtime for DolRecomp ports. GPLv3, self-described as early. | Less mature than WiiCompiled's runtime for Wii. |
| [Deep Sea](https://github.com/AH64-dll/DeepSea) (AH64-dll) | Wind Waker native port: DolRecomp + ModernGekko + Dolphin code; 415 RELs; 60fps via interpolated frames; widescreen. | Proof DolRecomp scales; good reference for interpolation + mod-loader design. |
| [RecompCore](https://github.com/aharonahdoot/RecompCore) | Dolphin fork with a CPU core that runs recompiled code natively, interpreter fallback. Wii untested. | Interesting as a **debugging oracle** (lockstep-compare against the interpreter). |
| [ogws](https://github.com/doldecomp/ogws) | Matching decomp of **Wii Sports** (US Rev 1), ~35% per decomp.dev. | Closest code relative to WSR — reference for engine/library structure and names. |
| [decomp-toolkit (dtk)](https://github.com/encounter/decomp-toolkit) | DOL/REL analysis: function boundaries, built-in Metrowerks/SDK signature DB, `dol config`, `rel merge`. | **How we get a WSR function map.** |

No existing WSR decomp or recomp was found.

## 2. How WiiCompiled works (from reading the repo)

**Translator** (`translator/`, .NET 8): 4 steps driven by a YAML manifest:
1. `translate-recursive <entry> --project m.yml` — walks the call graph from the entry point, lifts PPC → IR/SSA →
   C++. Discovery is purely recursive unless `function_map` (a `hexaddr name` MAP file) seeds more boundaries;
   no heuristic scanning.
2. `generate-data-init` — emits `.data/.rodata/.sdata` initializer + `RuntimeConfig.h`.
3. `emit-build-shards` — CMake shard graph.
4. CMake + Ninja + Clang builds `runtime/` + generated code into one exe.

Manifest essentials: DOL path (+ SHA-256 pin), optional RELs with load address, `memory.base/size`,
`sda_base`/`sda2_base` (r13/r2 — read from `__init_registers`), entry points, function map,
`allow_unsupported_instructions` (must be false to ship). `projects/examples/generic-dol.yml` is the starting
template for another game.

**Runtime** (`runtime/src`, ~35k lines C++): flat guest memory, fiber-based OS thread scheduler, HLE for
OS (threads, alarms, interrupts, messages), GX (FIFO/display lists → aurora), VI (+ frame pacing), AX/DSP audio,
PAD/WPAD/KPAD, DVD/NAND/ISFS, IOS network sockets + SSL, Riivolution, F10 ImGui settings overlay,
Dolphin-syntax input expressions, real Wii Remotes over Bluetooth.

**How hooks bind:** `REGISTER_NATIVE_FUNCTION(0x801A65F8, ...)` macros plus ~550 functions whose names end in
the MKW address. The translator regex-scans runtime sources to build a native-override index. **Every one of
those addresses is MKW-PAL-specific** and must be re-mapped for WSR.

**Determinism:** they claim 100% physics parity proven by MKW ghost replays matching Wii/Dolphin. That matters
hugely for our online plan (see §5).

## 3. What WSR needs that MKW didn't

1. **Symbol map for WSR.** Run dtk on WSR's `main.dol` (+ RELs if any — to verify) to get function boundaries and
   SDK signature hits (OS, GX, VI, AX, WPAD, KPAD, NAND, DVD, likely NW4R and EGG). Cross-reference ogws naming.
   Output → `projects/wsr/MAP.txt` in WiiCompiled's format.
2. **Re-key the HLE layer.** For each MKW hook, find the WSR address of the same SDK function. WSR (mid-2009) and
   MKW (2008) use close RVL SDK revisions, so most bodies should match; signature matching does most of it.
   Suggest refactoring hooks to be **keyed by symbol name + per-game address table** instead of hard-coded
   addresses — useful upstream contribution to WiiCompiled too.
3. **Wii MotionPlus** — WSR requires it; WiiCompiled's `wpad.cpp`/`kpad.cpp` have no MotionPlus support.
   Need: MotionPlus extension detection/activation handshake, gyro data in WPAD status (incl. passthrough modes
   with Nunchuk for archery/canoe etc.), calibration data. Input sources:
   - Real Wii Remote Plus / MotionPlus over Bluetooth (SDL3's Wii HIDAPI driver exposes gyro).
   - **Gyro controllers mapped to emulated MotionPlus** (DualSense, Switch Pro, Joy-Con, DS4) — Dolphin does this;
     aurora already has gyro input. This is the path most players will use.
4. **IR pointer** — WSR menus and several sports use it; WiiCompiled lists "no IR pointer yet". Map mouse / gyro /
   real IR (DolphinBar or Wiimote IR camera data) into the KPAD `pos` path.
5. **Game-specific patches** — widescreen/aspect hooks (MKW's live in `gx_dynamic_aspect.h`, built on
   `EGG::Screen`; if WSR also uses EGG — likely, verify via dtk — the same approach ports over), and any SMC or
   quirky code the translator flags.
6. **Region pick.** WiiCompiled pins one revision (PAL `RMCP01`). We should pick one WSR revision
   (e.g. `RZTE01` US or `RZTP01` PAL) and hash-pin it.

## 4. Goals 1 & 2: interpolation and resolution

**Frame interpolation** — WiiCompiled's aurora fork (`aurora-main/lib/gx/frame_interpolation.hpp`) does it at
the renderer: each perspective draw is identified across frames by hash (geometry/pipeline/texture/matrix
topology), and 1–3 intermediate frames are rendered by interpolating its transforms (rigid TRS for single
matrices, coefficient lerp for skinning palettes). Targets 120/180 fps; exposed as
`aurora_set_frame_interpolation_fps()`. It's **game-agnostic**, so it should work on WSR out of the box,
with the same "experimental, artifacts" caveat. Expect WSR-specific tuning: particles (water/spray in
Wakeboarding/Power Cruising), 2D/UI draws, camera cuts (must detect and disable interpolation on cuts).
Alternative approach: Deep Sea's game-side `frame60-accum`.

**Any resolution / aspect** — aurora already provides internal-resolution scaling and widescreen. Arbitrary
aspect (ultrawide, portrait) needs a per-game FOV/camera hook; WiiCompiled's MKW version is a Hor+/Vert+
patch of `EGG::Screen` records. For WSR: find the camera/projection setup + 2D layout (NW4R `lyt`) and apply
the same pattern; HUD anchoring may need relocation for ultrawide.

## 5. Goal 3: online play (the big one)

WSR has no network code, so we can't reuse Nintendo WFC like MKW does. We add a **native side-channel** in the
runtime. Nintendo's own **Wii Sports Club** (Wii U, 2013–14) added online to these same sports — a good
reference for UX and which sports translate well online.

**Model: deterministic input sync.** Because the recomp is deterministic (per WiiCompiled's ghost-parity
result), every peer runs the full game and only **controller input per frame** is exchanged. What gets synced
is the full per-frame `WPADStatus`/`KPADStatus` stream per player: buttons, accelerometer, **MotionPlus gyro
samples**, IR, extension data. Gyro makes packets bigger than typical netplay but still tiny (< 1 KB/frame).

Phases:
1. **Lockstep with input delay** (Dolphin netplay style). Simplest; fine for turn-based sports
   (Bowling, Golf, Archery, Frisbee Golf, Basketball 3-pt contest) and acceptable at low ping for real-time ones.
2. **Rollback** (for Swordplay, Table Tennis, Basketball pickup). Needs save/load state: guest memory
   (MEM1 24 MB + MEM2 64 MB) + CPU/fiber contexts + runtime HLE state (audio, GX FIFO, timers). Heavy but
   feasible with dirty-page tracking and a restricted "sim-only" snapshot. Libraries: [GekkoNet](https://github.com/HeatXD/GekkoNet)
   (P2P rollback SDK), or GGPO-style custom. Audio/visual side effects during resim must be suppressed.
3. **Determinism guardrails**: per-frame RAM checksums exchanged between peers to detect desyncs early; pin
   RNG seed and system clock (`OSGetTime`/RTC HLE) to netplay-synced values; same build on all peers
   (x86-64 vs ARM64 float behavior must be verified identical).

**Transport / P2P:** UDP with NAT hole-punching + relay fallback. Options: Valve GameNetworkingSockets (ICE/STUN
built in), libjuice/ENet + a tiny rendezvous server, or Steam networking if ever distributed that way.
Lobby/room-code UI in ImGui or RmlUi (both in aurora).

**Game-side glue:** online play means "local multiplayer where players 2–4 are remote". Hook the controller
connection path so remote players appear as connected Wii Remotes, then drive the game's existing 2–4P modes.
Mii data for remote players gets exchanged at lobby time and written into the guest's Mii database
(RFL) before the match.

## 6. Proposed roadmap

1. **Setup:** fork WiiCompiled; dump WSR (one pinned revision); run dtk (`dol info`, `dol config`,
   `rel merge` if RELs) to produce a function map; write `projects/wsr/recomp.yml` from `generic-dol.yml`.
2. **Translate** with `allow_unsupported_instructions: true` first, iterate until clean, then flip to false.
3. **Boot:** re-key OS/VI/GX/DVD/NAND HLE hooks to WSR addresses until the title screen renders.
4. **Input:** MotionPlus + IR pointer in WPAD/KPAD; gyro-controller mapping; real Wii Remote Plus.
5. **Audio, saves, all 12 sports playable.** Validate against Dolphin (and RecompCore-style lockstep compare).
6. **Polish goals 1–2:** interpolation tuning, aspect/FOV hooks, HUD.
7. **Online:** lockstep MVP for Bowling → expand → rollback.

## 7. Open questions / to verify

- Does WSR ship RELs, or is it all in `main.dol`? (dtk will show.)
- Does WSR use EGG like Wii Sports/MKW? (Likely; confirm via signatures.)
- Exact RVL SDK revision in WSR vs MKW (affects how many HLE bodies carry over unchanged).
- Any "AI code" policy friction: WiiCompiled accepts AI-assisted code; DolRecomp restricts it.
- GPLv3 obligations if we mix in code from Deep Sea/ModernGekko (also GPL — compatible).

## Sources

- WiiCompiled: https://github.com/patchzyy/wiicompiled (README, `translator/README.md`, `projects/`, runtime source read directly)
- aurora: https://github.com/encounter/aurora
- DolRecomp: https://github.com/ExpansionPak/DolRecomp · GBAtemp announcement: https://gbatemp.net/threads/dolrecomp-a-wii-gamecube-static-recompiler-was-recently-made-public.682687/
- ModernGekko: https://github.com/ExpansionPak/ModernGekko
- Deep Sea: https://github.com/AH64-dll/DeepSea
- RecompCore: https://github.com/aharonahdoot/RecompCore
- ogws (Wii Sports decomp): https://github.com/doldecomp/ogws
- decomp-toolkit: https://github.com/encounter/decomp-toolkit
- GekkoNet: https://github.com/HeatXD/GekkoNet
- Dolphin netplay guide: https://dolphin-emu.org/docs/guides/netplay-guide/
- Wii Sports Resort: https://en.wikipedia.org/wiki/Wii_Sports_Resort
- Wii Sports Club: https://en.wikipedia.org/wiki/Wii_Sports_Club
