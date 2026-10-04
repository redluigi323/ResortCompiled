# EOS netplay prototype

This is a Linux and Windows x86-64 transport prototype using EOS SDK 1.19.2.1. Device ID
authentication uses EOS Connect, with no Epic account sign-in, Auth interface,
social overlay, or friends access. It replaces the remote TCP/TLS transport;
the existing local game broker, input matching, compatibility checks, host-save
baseline, temporary guest Miis, and optional keep flow remain in use.

The developer config is `work/eos/client.json`, outside tracked source. The SDK
is in `eosstuff/SDK`, also ignored. `RESORT_EOS_CONFIG` may point to another config.
Config fields: `product_id`, `sandbox_id`, `deployment_id`, `client_id`,
`client_secret`. Do not post the config or include it in a public source commit.

## Portal policy

Assign a game client policy with **User required** enabled. Enable Connect
actions for device credential creation, login, and first-use user creation;
Lobbies actions for create, search/find, join, leave, destroy, and reading members;
and P2P access. Exact portal action labels depend on the SDK/service version.
The normal Peer2Peer preset can serve as a development starting point. A custom
policy can restrict it further. A trusted-server policy must not be shipped.

The provided `netplay-test` deployment is under the **Live** sandbox. These names
do not change the prototype's testing status. Both PCs must target identical
product, sandbox, and deployment IDs. Portal permission failures appear by EOS
result name in the session output; credentials are not logged.

## Build for local testing

From the repository root (commands also work in fish):

```sh
python3 scripts/build_eos.py
.venv-launcher/bin/python launcher/app.py
```

This builds `launcher/eos-local/libriisorted-eos.so` beside the official EOS
runtime. When bridge and credentials are available the online dialog defaults
to **Epic Online Services**. Direct LAN/ZeroTier remains a separate option.
The existing packaged 0.2.3 launcher does not contain this integration.

To create a complete EOS test archive:

```sh
.venv-launcher/bin/python scripts/release/package.py \
  --version 0.3.0-eos-preview --archive --eos-config work/eos/client.json
```

Give both testers the entire extracted launcher folder. They can locate an
existing Resort installation. The test build includes the SDK runtime and game
client credentials; **those credentials are extractable from a distributed
client**, even though the source snapshot omits them. Rely on the user-scoped
client policy, not on concealing client credentials. Keep this prototype build
private while validating portal configuration and packaging requirements.

## Host and join

1. Both PCs choose **Epic Online Services** in Online play.
2. Host starts a lobby; device login is automatic. Share the generated invitation
   privately. No host IP, ZeroTier network, or forwarded TCP port is required.
3. Guest pastes that invitation, selects Miis, and joins.
4. Look for **EOS P2P probe succeeded in both directions** on both PCs. A
   connection event identifies direct delivery or an Epic relay.
5. Both games launch after compatibility and save/Mii setup succeeds. Play
   Swordplay Duel as before. Waiting for a guest has no local deadline; setup
   and gameplay inactivity timeouts remain.
6. For the next run, enable **Force Epic relay** on both PCs. Verify that logs
   report the relay and compare latency and results with the normal run.

CLI equivalents, using the current developer runtime:

```sh
python3 scripts/netplay_online.py host work/netplay/eos-host-01 --transport eos
python3 scripts/netplay_online.py join work/netplay/eos-guest-01 --transport eos --invite 'HOST_INVITATION'
```

Add `--force-relay` on both sides for the relay test. Use a new session folder
each time. Two windows under the **same OS user** normally share a device identity;
the bridge rejects joining your own lobby. Test with two PCs or separate OS users.
Do not delete Device ID credentials merely to create another test player.

## Transport and limits

One SDK owner thread continuously ticks the platform and refreshes device login
before token expiry, even while the broker blocks waiting for native input. It
accepts only the selected lobby member on the Riisorted socket/channel, sends
reliable ordered 1024-byte packets, retries SDK queue backpressure, and bounds
both application and SDK buffering. EOS stream framing uses the same maximum
message sizes as direct transport. The host validates an invitation secret
before sending any save or Mii data. The guest checks the lobby owner against
the invitation. Host migration is disabled because the host owns the baseline.

For code-based lookup the EOS lobby uses **public advertised admission**, with
an exact lobby-ID search; our launcher has no public lobby browser. The secret
is not a lobby attribute. This gates game data, but does not make EOS lobby
membership private: someone who discovers the lobby could occupy its second
slot and interrupt setup. Restart with a new invitation if that occurs. Stronger
admission, rate limiting, and reconnect support remain future work.

EOS provides connectivity, not game determinism or rollback. Starting with
0.3.3, both peers send input captures immediately and apply the same captures
a fixed number of frames later. Pre-game round-trip probes choose 2–30 frames
of delay, using a half-RTT estimate plus two frames for jitter from preview
0.3.4. This trades input latency for smoother
frame pacing rather than paying a round trip on every frame. Both peers check
the original frame, mode and previous solver hash before applying the paired
inputs; these checks are delayed by the same window. Missing packets still
pause simulation, never predict swings. This is not a complete game-state hash
or a guarantee against every source of desynchronization.

New sessions execute the installed runtime in place with an absolute
`RESORT_USERDATA_DIR` override. Only session saves, Miis, configuration, caches
and logs live in `Netplay/<session>/game/UserData`; executables, DLLs, fonts and
bootstrap content are no longer copied into every session. Old session folders
are retained for manual inspection/cleanup. Don't move/update an installation
while a session is running.

Windows requests 1 ms timer resolution while its EOS pump is active and restores
it when the transport closes. Both platforms must use the new protocol/build;
older invitations are incompatible. Keep both players'
`online.log`, `native.log`, and normal runtime logs if the match diverges. Do not
post invitations or credential files. GUI, actual EOS login, relay delivery,
and gameplay have not been tested by the coding agent.

Windows packaging from Linux is documented in [Windows-build.md](Windows-build.md).
Both players must use matching simulation builds. Windows/Linux crossplay uses
a platform-independent source identity; binary integrity is checked locally.
Gameplay validation across platforms is still required.

Preview 0.3.4 corrects routine bridge time accounting and saves `desync.json`
only on a failed boundary. See [the rollback roadmap](Netplay-rollback-roadmap.md)
for the current Swordplay loading failure and remaining deterministic-state work.

## MotionPlus authority (preview 0.3.5)

The 0.3.4 failed boundary reports from both PCs were identical: capture 947,
render frame 948, modes [5,5], with different previous MotionPlus solver digests.
The failed captures are future raw inputs, not the older samples that produced
those digests; they cannot reconstruct the preceding live calibration state.
A harness ran 3,000 translated solver sample steps on Linux and Windows from
identical initialization and produced byte-identical results. That excludes a
platform math discrepancy in that tested sequence, not every possible sequence.

Online play now treats the host as the authority for both virtual MotionPlus
controllers' SDK processing. Both PCs still supply independent mapped raw motion
captures. The host processes each agreed pair with the translated SDK, then
publishes every sample's valid flag/15 result words and the two SDK channel
calibration states. The guest uses those exact reports and mirrors SDK state,
so game queries see the canonical calibration state as well as sensor outputs.
The three guest raw-buffer pointers at SDK offsets 3488–3499 are excluded and
remain local. Only this device state is mirrored; it is not general game-memory
recovery, a RAM snapshot or rollback. Offline input processing is unchanged.

Report sequence, render frame, requested modes and sample counts must match
before applying it. Existing input, mode/frame and solver-digest guards remain
active. An invalid report or absent required guest SDK state stops play. Game
state outside the device model still has nondeterministic scheduling, and other
sources of desync remain possible.

Canonical reports are bounded, compressed with zlib at level 1, then sent through
the reliable ordered EOS stream in parallel with upcoming captures. Decompression
has a fixed output cap and rejects trailing/truncated data. There is no internet
acknowledgement per report; the host continues after its local send is queued.
The guest follows the host's report stream, which can introduce a presentation
offset and a wait if packets are late. The delayed-socket native harness retained
approximately 60 input intervals/second with a 90 ms RTT and full-size channel
states; actual EOS traffic, Swordplay transitions and gameplay still need testing.

Both players must update launcher and runtime to 0.3.5 (session protocol 5).
