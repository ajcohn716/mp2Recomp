# Mario Party 2 Recompiled

[Downloads](https://github.com/ajcohn716/mp2Recomp/releases) ·
[Release notes](docs/releases/v0.3.1.md) · [GPLv3 or later](LICENSE)

A native PC recompilation of **Mario Party 2** for Linux and Windows x86-64.

The US game boots through the logos and animated opening to the title screen.
Game CPU code runs natively, with RT64 for graphics and N64ModernRuntime for
system services.

**This is an early, experimental port.** Boot and title-screen behavior have been
tested on Linux; full boards, minigames, long sessions, and Windows game boot
still need validation. Some title prompt glyphs appear as blocks. See
[recorded validation and limits](docs/building.md#recorded-validation-and-limits).

## Quick start

You need a Linux or Windows **x86-64** computer with a **Vulkan-capable graphics
driver** and your own unmodified **Mario Party 2 USA ROM**. The game accepts
`.z64`, `.v64`, and `.n64` files; other regions and modified ROMs are rejected.

1. Open [Releases](https://github.com/ajcohn716/mp2Recomp/releases) and download
   `mp2recomp-game-linux-x86_64.tar.gz` or `mp2recomp-game-windows-x86_64.zip`.
2. Extract the archive into a writable folder. For automatic detection, create
   a `games/` folder beside the game executable and put your ROM inside it.
3. Open `./play` on Linux or `mp2recomp.exe` on Windows. If prompted, select
   your ROM. The game remembers it for future launches. Press **Enter** to
   skip the opening, use the **arrow keys** to move and press **X** or
   **Space** for A. Press **F1** for settings.

To upgrade, extract the new package and copy `userdata/` from your previous
installation to bring over your ROM, saves, and settings. The portable package
ships the compiled game and runtime libraries. Players supply the ROM separately.

The [v0.2.3 source installer](docs/installation.md) builds the game on the
player's computer. For development, see [building from source](docs/building.md).

## Find what you need

| I want to… | Guide |
| --- | --- |
| Play, upgrade, or package the game | [Prebuilt packages](docs/prebuilt-game.md) |
| Use the source installer | [Source installer](docs/installation.md) |
| Change controls, sound, or graphics | [Controls and settings](#controls-and-settings) |
| Play with friends | [Multiplayer](docs/netplay.md) |
| Find logs, record a problem, or replay a checkpoint | [Debugging](docs/debugging.md) |
| Compile or understand the port | [Building and implementation](docs/building.md) |
| Prepare an installer release | [Release maintainer notes](docs/installation.md#for-release-maintainers) |

## Controls and settings

Keyboard controls work immediately:

| N64 input | Keyboard |
| --- | --- |
| Start | Enter |
| A | X or Space |
| B | C |
| Z | Z |
| Analog stick | Arrow keys |
| L / R | Q / E |
| C up / down / left / right | I / K / J / L |
| Quit | Escape or close window |

Press **F1** for settings. From a source checkout, you can also launch with
`./run.sh --settings`. Escape or **Done** closes settings. The game keeps running
while settings are open, but game input is suppressed.

- **Input:** select an SDL-compatible controller or keyboard-only input, change
  the left-stick deadzone, and remap bindings. Click a binding and press its new
  input; Escape cancels. Controllers can be plugged in during play. Arrow keys
  always control the analog stick.
- **Audio:** adjust master volume, left/right levels, or mute.
- **Graphics:** change render resolution (original, 2x–4x, or fit window), aspect
  ratio (4:3, 16:9, or fit window), fullscreen, and output filtering while playing.
- **Netplay:** create or join a room, copy its connection code, see connected
  players, invite Steam friends, accept invitations during play, and start or
  leave the session. See [multiplayer setup](docs/netplay.md).

Settings save automatically in `userdata/settings.cfg`. **Reset defaults**
restores the current Input, Audio, or Graphics page. Offline play uses controller
port 1. The runtime's ROM copy and saves live in `userdata/`; the offline EEPROM
save is `userdata/saves/mp2.us.bin`. Back up `userdata/` to keep a copy of your
settings and progress.

## Play with friends (experimental)

Two to four players can share a room, each using their own supported ROM and a
matching build. **Version 0.2.2 installers include Steam and Direct TCP support.**
Players do not need the Steamworks SDK.

Open **F1 → Netplay**. For LAN play, choose **Direct TCP** and use the host's LAN
address. Internet Direct TCP hosts also need port forwarding. Steam-enabled
builds use the **Spacewar test App ID 480** and require each player to run Steam, signed in
on their own account and computer.

Creating or joining a room restarts the game into a lobby. The host invites
friends currently playing Spacewar or shares the connection code privately,
then starts when everyone has joined. Friends can receive and accept invites
while playing offline; they do not need to host a room. Online saves are
separate from offline saves. Cross-account Steam sessions, separate home networks,
full boards/minigames, and Linux/Windows cross-play still need validation.
Follow the [multiplayer guide](docs/netplay.md) for the complete steps and limits.

## Find logs or record a problem

Logs are saved automatically under `userdata/debug/`. Press **F8** to record the
next 15 seconds of video frames and audio. Logs can contain private room codes;
review them before sharing. For command-line controls and replay checkpoints,
see [debugging](docs/debugging.md).

## Source and upstream work

This repository contains host integration source, configuration, and build
scripts. Supply your own ROM to generate game code locally. ROMs, assets,
screenshots, memory dumps, Ghidra/decompiler output, generated symbol tables,
generated game/microcode C, compiled binaries, and saves are excluded.
Before committing, follow the [source-only repository check](docs/building.md#before-committing).

This work builds on these projects; their respective licenses and authorship
remain applicable:

* [Mario Party 2 decompilation](https://github.com/Rainchus/marioparty2)
* [N64Recomp](https://github.com/N64Recomp/N64Recomp)
* [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
* [RT64](https://github.com/rt64/rt64)
* [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp), used as an
  integration reference for the runtime and renderer interfaces
* [Splat](https://github.com/ethteck/splat) and
  [spimdisasm](https://github.com/Decompollaborate/spimdisasm)

N64ModernRuntime is GPL-3.0 and RT64 is MIT; see the dependency license files.
The generated game code and assets are not included as redistributable source.

## License

Copyright (C) 2026 Andrew Cohn and contributors.

The game integration, installer and build scripts are free software:
you can redistribute them and/or modify them under the GNU General Public License
as published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version (`GPL-3.0-or-later`).

The standalone Steam helper and its shared transport/protocol headers are
MIT-licensed; see [LICENSE-STEAM-HELPER](LICENSE-STEAM-HELPER) for the exact file
scope. It runs as a separate process; the GPL game does not link Valve's runtime.
Valve's bundled runtime files retain Valve's terms.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full terms.

Third-party components retain their own licenses and copyright notices; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This license does not grant
rights to Nintendo/Hudson game code, ROMs, assets, names or trademarks. This is
an independent, unofficial project and is not affiliated with Nintendo or Hudson.
