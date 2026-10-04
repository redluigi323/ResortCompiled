# Riisorted: live online prototype

For another player's installation and ZeroTier connection, use the
[player setup guide](Player-setup.md). The same-PC two-window test succeeded;
remote testing remains pending.

The first implementation connects two Linux x86-64 PCs and runs the existing
local multiplayer game on both. Host controls Wii Remote 1; guest controls Wii
Remote 2. Each PC uses its own normal keyboard/mouse or supported controller
mapping. Start with **Swordplay Duel**, choosing two players in the normal menus.
Both players see the normal local multiplayer camera. This does not yet modify
the game's menus or create new multiplayer sports.

## Connect through the launcher

Both players need the same newly built native executable, extracted game content,
Riisorted overlays, and Mii artwork. Different personal Mii collections are allowed.
Launcher preview 0.2.2 and newer installs its bundled runtime when opening or
locating an existing installation, preserving UserData. For the development CLI,
use the latest built portable `out` folder. The bridge rejects unsupported old runtimes.

1. Select **Riisorted**, then **Online play · Experimental**.
2. Host chooses **Host a session**, selects a TCP port (default **42680**), and
   starts. Initial content hashing can take time. Hosting currently requires the
   system `openssl` command.
3. Host copies the invitation and shares it plus their IP address with the guest.
   Invitations contain a connection secret; share them privately.
4. Guest chooses **Join a session**, enters the host IP and invitation, selects
   which Miis to bring, then joins. Both games launch after setup finishes.
5. Navigate to local two-player Swordplay Duel. Guest supplies the second remote.
   **Leave session** stops both sides once the connection closes.

Start with PCs on the same LAN, using the host's LAN IP address. For direct
internet connections, the host needs a reachable TCP port, normally by allowing
it through their firewall and forwarding it on their router. There is no relay,
matchmaking, or automatic NAT traversal. The invitation supplies the selected port.

## Connect from the project directory

These commands work in Bash and fish without activating the launcher virtualenv.
Python 3.11 or newer is required. Close the normal game before starting.
Each folder name must be new, on its respective PC.

Host:

```sh
python3 scripts/netplay_online.py host work/netplay/online-host-01
```

Guest, using the host's printed invitation:

```sh
python3 scripts/netplay_online.py join work/netplay/online-guest-01 --address HOST_IP --invite 'INVITATION'
```

The default runtime is the portable `out` directory; `--source-out` selects
another portable runtime. Host may specify `--port` and `--address` to change its
port and bind address.

## Saves, Miis, and logs

The host's starting save and selected system data initialize both isolated
sessions. Guest Miis are merged into a temporary host collection and sent to both
peers. Identical Miis are deduplicated; conflicting identities or exceeding the
100-Mii capacity stop setup with an error. Guest can select fewer Miis to bring.
Game content and Nintendo Mii artwork are checked locally and are not transferred.

Online progress stays in the session folder and is not automatically copied to
either personal save. Afterward, the launcher host can choose **Keep guest Miis**;
this adds missing Miis to the personal collection using its normal backup/save
path. It does not import game progress. Guest personal data is unchanged.

Launcher sessions live beneath the launcher's data directory in `Netplay/`.
CLI sessions live in the folder supplied to the command. Relevant files:

- `online.log`: setup, interval progress, and connection/synchronization errors.
- `native.log`: native process output.
- `game/UserData/Logs/`: the runtime's normal diagnostic logs.
- `game/UserData/NAND/`: isolated session save and Mii collection.

## Current synchronization and limits

The native runtime sends mapped sensor samples before the translated MotionPlus
solver. The host pairs both players' samples for each input interval. Both games
apply channel 1 then channel 2, with separate calibration and input histories.
Network waits are excluded from guest time-base reads, VI timing, sleep deadlines,
and audio elapsed-time sampling. Missing input waits instead of prediction;
connection timeouts stop the session. Packet sizes and input sequence/channel
metadata are bounded and checked.
The host's initial lobby wait has no deadline and ends only on cancellation or
connection. CLI `--address` binds the host listener to that IPv4 address; the GUI
provides a network device/address picker with ZeroTier preferred.

Build/content fingerprints are checked before save exchange. During play, frame
indices, requested MotionPlus modes, and the previous interval's solver digest
must agree. A disagreement stops play rather than continuing silently. Transport
uses TLS 1.3 and pins the host certificate to the invitation before guest data is
sent, using Python's [SSL API](https://docs.python.org/3/library/ssl.html).

**This does not guarantee synchronized gameplay yet.** Guest execution is still
driven by host time outside network waits. CPU scheduling, other game state,
resource completion, and rendering behavior can differ without the solver digest
detecting it. Complete state checks, deterministic scheduling, recovery snapshots,
and rollback remain future work. Input exchange currently waits for a round trip
each interval, so higher latency can slow gameplay; adaptive input buffering is
also outstanding. Reaching the lobby does not establish that a whole match stays
in sync.

The code was compiled and Python syntax checked. Gameplay and two-PC testing are
left to the user. For the first run, compare menu progress and both sword motions,
then the end of a round; keep both session log directories if screens disagree
or the session stops.
