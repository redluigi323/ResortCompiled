# Play Riisorted online with another player

## Get the same preview

Both players should download the **same Linux x86-64 preview archive** from the
[ResortCompiled releases page](https://github.com/redluigi323/ResortCompiled/releases).
If no binary is listed yet, the maintainer must attach the archive produced by
[the release build instructions](Release-build.md). A GitHub source ZIP is not
the playable launcher.

This first package is built on Linux Mint 22 / Ubuntu 24.04, needs glibc **2.38+**,
and targets Linux x86-64. Windows online play is not implemented yet. Each player
needs their own supported **PAL Wii Sports Resort RZTP01** disc dump, with this
`main.dol` SHA256:

```text
855e4ffe4f8d69d172c44b05ec3a4ddb89645bebfadca41c98effab69111d448
```

1. Extract the entire archive. Keep `ResortLauncher` and `_internal` together.
2. Install the Linux runtime prerequisites. On Mint 22 / Ubuntu 24.04:

   ```sh
   sudo apt install openssl libxcb-cursor0 libxkbcommon-x11-0 libegl1 libopengl0
   ```

   Keep your GPU's Vulkan/OpenGL drivers installed. Python and Qt are bundled;
   players do not need the compiler or development environment.
3. Run `./ResortLauncher` from the extracted `ResortLauncher` folder.
4. Choose your disc and an installation folder. The launcher extracts and checks
   it, then prepares the game. A complete disc provides Mii face artwork. A
   scrubbed disc may require **Set up face artwork** with your complete dump.
5. Create Miis in the launcher or optionally import your existing collection.
6. Select **Riisorted**. Do not use an old installed runtime for this test: use
   a fresh installation of this package. Existing saves can be imported later.

## Connect the two PCs with ZeroTier

ZeroTier supplies a virtual LAN; Riisorted still exchanges its own encrypted
two-player input stream. No Epic Online Services account is used.

Follow ZeroTier's official [installation and quickstart guide](https://docs.zerotier.com/quickstart/).
Install ZeroTier One on both PCs. One player creates a **private** network in
ZeroTier Central. Both players join its Network ID, and the network owner
authorizes both devices. Note each device's **managed IPv4 address** in Central.
These are the addresses Riisorted uses. ([ZeroTier network setup](https://docs.zerotier.com/start/))

You can join an already created network from Linux with:

```sh
sudo zerotier-cli join YOUR_NETWORK_ID
sudo zerotier-cli listnetworks
```

Verify that both devices show an authorized network and assigned addresses before
hosting. Normal router port forwarding is not needed for this virtual LAN setup.
If the host firewall blocks incoming traffic, allow TCP **42680** from the guest's
ZeroTier address. For UFW, substitute the actual guest address:

```sh
sudo ufw allow from GUEST_ZEROTIER_IP to any port 42680 proto tcp
```

## Host and join

**Host:** open **Riisorted → Online play · Experimental → Host a session**.
Keep port **42680**, start, and wait for the invitation. Send the invitation and
your **ZeroTier managed IP** privately to the guest. Content hashing can take time;
the invitation is generated when the host is ready. Start after both PCs have
finished installing and joining ZeroTier.

**Guest:** open **Online play → Join a session**, paste the invitation, enter the
host's ZeroTier IP, choose the Miis to bring, and join. Do not enter `127.0.0.1`:
that address only works for two windows on the same PC.

Both games launch when setup finishes. Host controls **Wii Remote 1** and guest
controls **Wii Remote 2**. Navigate to **Swordplay → Duel → two players**. Both
screens show the existing local multiplayer view. Each player uses the normal
keyboard/mouse or supported controller mapping on their PC. Keep the game focused.

The host's starting save initializes both sessions. Guest Miis are merged into
a temporary collection; host can choose **Keep guest Miis** afterward. Online
progress stays in separate session data and does not overwrite personal saves.

## What to report from the first remote match

The same-PC test succeeded. Two-PC internet play has not yet been verified.
The implementation waits for both inputs every interval, so higher latency can
slow gameplay. Solver disagreement stops the session, but full game-state checks
and deterministic scheduling are still incomplete. ZeroTier connectivity does
not itself prevent game desynchronization.

Compare menu progress, sword movement, hit results, and round endings on both PCs.
If it stops or the screens differ, keep **both players'** `online.log`, `native.log`,
and `game/UserData/Logs` from their session folders. Launcher sessions are under
its per-user data directory in `Netplay/`; the dialog prints the folder location.
Avoid sharing invitations, certificate private keys, or the NAND/save directories
in public reports. Report the preview version, ping to the host's ZeroTier IP,
controllers used, and what each screen did.

### Common setup errors

- **Connection timeout:** check authorization, managed IP, host firewall, and
  that host is waiting with a current invitation. A host invitation expires when
  its lobby stops; start again if it timed out.
- **Compatibility mismatch:** use the same archive on both PCs, the same supported
  game revision, and identical overlays. Build hashes are checked exactly;
  separately compiling the same sources may produce different executable hashes.
- **Mii conflict or collection full:** select fewer guest Miis, or resolve duplicate
  identities in the creator before joining. The collection holds 100 Miis.
- **Older runtime:** make a fresh installation from this preview package.

For developer CLI commands, see [online development](Riisorted-online-development.md).
