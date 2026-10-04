RIISORTED · RESORT LAUNCHER

1. Extract the complete download to a folder on your computer.
2. Open ResortLauncher (ResortLauncher.exe on Windows).
3. Choose your Wii Sports Resort disc image and an installation folder.
4. Install the game, then use Create / manage Miis to make your own Miis.
5. Choose an edition and press Play.

RUNTIME INSTALLATION
The launcher installs the native runtime in Resort/Runtime and the disc files in
Resort/Game. Preview 0.2.2 checks and repairs older/missing runtime files when you
open or locate an installation and before playing. Runtime/UserData keeps your
saves, Miis and settings. Previous runtime files remain in .runtime-backup-*.
The runtime comes from this release package; no source checkout or compiler is
required. Keep the whole extracted launcher folder, including _internal, together.

SUPPORTED GAME
PAL Wii Sports Resort, game ID RZTP01.
The launcher also checks main.dol against this release's supported revision:
855e4ffe4f8d69d172c44b05ec3a4ddb89645bebfadca41c98effab69111d448
Supported image containers: ISO, RVZ, WBFS, WIA, CISO, GCZ.
Allow at least 6 GB of free space. Your disc image is left untouched.

MII DATA
A fresh Mii database is created automatically. Create and edit your Miis in the
launcher; importing an existing collection is optional. The creator and Mii list
show an interactive 3D head preview. Drag to rotate, scroll to zoom, and press
Front view to reset. Your edits update the preview automatically. Head artwork
comes from your installation; finish face artwork setup first if prompted.
The preview requires desktop OpenGL 3.3. Height/build are saved for the game;
sport bodies are shown in-game.

Face artwork is recovered from the System Menu package in your disc's update
partition. If your dump omits that partition, use Set up face artwork with a
complete dump, or optionally import artwork from an existing FaceLib folder.
No system update is installed. Your disc image is never modified.

Import existing collection accepts a FaceLib or Wii/Dolphin NAND folder.
New Miis are merged, duplicates skipped, and existing Miis kept. Individual Wii
Mii files can also be imported/exported inside the creator. Existing databases
are backed up to Runtime/UserData/NAND/shared2/menu/FaceLib/Backups before edits.
Close the game before editing Miis. Deleting a Mii can affect its game profiles.
No personal NAND or Nintendo face artwork is included in this package.

EDITIONS
Wii Sports Resort launches with no Riisorted file overlays.
Riisorted includes experimental two-player direct online play on Linux x86-64.
Both editions share offline saves and settings. F10 opens in-game settings.

ONLINE PLAY (EXPERIMENTAL)
Choose Riisorted, then Online play. Host starts a session and shares the invitation
and host IP address. Guest enters both and selects Miis to bring. Use identical
builds/game content; start with local two-player Swordplay Duel. Hosting currently
requires the system openssl command. Default host TCP port is 42680; internet
connections require a reachable port. No relay or automatic NAT traversal exists.
Host saves and guest Miis initialize separate session data. Online progress stays
in the session folder; host can choose Keep guest Miis afterward. Personal game
saves are unchanged. Connection/input checks stop on detected disagreements, but
complete game synchronization is not guaranteed yet. Higher latency can slow play.
Session folders and logs are under the launcher's data directory, in Netplay/.
See docs/Player-setup.md for installing this preview and connecting using ZeroTier.
This Linux build requires glibc 2.38+ (Mint 22 / Ubuntu 24.04 recommended).

YOUR INSTALLATION
The selected installation folder contains a Resort directory with:
  Game/                   extracted disc data
  Runtime/                native game and supporting runtime files
  Runtime/UserData/       settings, saves, NAND, caches and game logs
  Editions/Riisorted/     future Riisorted file additions

Move the entire Resort directory together if you relocate it, then choose
Locate installation in the launcher. Keep the launcher open while playing.
Use Open logs after a failure. Cancelling installation removes unfinished data.

SOURCE AND CREDITS
The launcher is GPL-3.0 and includes adaptations from Team Wheel Wizard.
See licenses/ResortLauncher-credits.md and source/launcher/THIRD-PARTY.md.
Launcher source, assets and build scripts are included under source/.
