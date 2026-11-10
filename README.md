# Mario Party 2 Recompiled v0.1

[Download v0.1](https://github.com/ajcohn716/mp2Recomp/releases/tag/v0.1) ·
[Release notes](docs/releases/v0.1.md) · [GPLv3 or later](LICENSE)

The US ROM boots through the Nintendo/Hudson logos and the animated opening
to the **Mario Party 2 title screen**. The game CPU code runs as statically
recompiled native code; graphics use RT64 and OS services use N64ModernRuntime.

This repository contains host-side integration source, configuration, and build
scripts only. Supply your own US ROM to generate the game code locally. ROMs,
assets, screenshots, memory dumps, Ghidra/decompiler output, generated symbol
tables, generated game/microcode C, compiled binaries, and saves are excluded.

## Installer (Linux and Windows x86-64)

The `Installer release` GitHub Actions workflow builds a self-contained Qt
installer and a matching dependency pack for each platform. These contain the
Python interpreter, compiler, build tools and open-source runtime dependencies.
Players do not need to install Python, Visual Studio, MSYS2 or development
packages. A working graphics driver with Vulkan support is still required.
The Windows version is a testing candidate until installation and game boot
have been verified on a Windows computer.

Download and extract the installer archive. The workflow includes its matching
SDK archive beside the executable, so this download also works offline. Keep
its entire directory intact.
Open `mp2recomp-installer.exe` on Windows or `mp2recomp-installer` on Linux,
drag your US ROM onto the window, choose an installation directory and press
**Go**. The installer validates the ROM, generates the game code locally,
builds the executable, and can launch it when installation finishes. It accepts
big-endian `.z64`, byte-swapped `.v64` and little-endian `.n64` copies of the
supported ROM. Other regions and modified ROMs are rejected.

The same installer supports command-line use:

```sh
mp2recomp-installer --rom "/path/to/Mario Party 2 (USA).z64" \
  --prefix "/path/to/mp2recomp" --jobs 4 --launch
```

On Windows, run `./mp2recomp-installer.exe` in PowerShell with Windows paths.
Use `--help` for all options, including build/cache locations and shortcuts.
Your settings and saves survive reinstalling into the same directory.

The matching dependency pack downloads on first use and is checked against
its release-specific SHA-256 checksum. For offline use, place the SDK archive
beside the installer executable, or select it in the GUI's advanced options:

```sh
mp2recomp-installer --rom "/path/to/game.z64" \
  --dependency-pack "/path/to/mp2recomp-sdk-linux-x86_64.tar.gz"
```

Releases include Linux and Windows installers built by the `Installer release`
GitHub Actions workflow. Each installer archive includes its matching offline
SDK. Tagged `v*` builds prepare a draft release for verification before publication.
Manual workflow runs also provide downloadable Actions artifacts.
No ROM, generated game code or game assets are sent to the cloud build.
For installer-only changes, workflow dispatch can reuse a verified SDK run via
`sdk_run_id`. Its dependency pins and toolchain
recipe must match; leave the field blank to rebuild both SDKs.

A developer checkout can use existing system dependencies instead:
`python install.py --source-build --rom "/path/to/game.z64"`, or add `--gui`.
The GUI requires `pip install -r tools/installer-requirements.txt` in this mode.

The Linux SDK builds pinned SDL 2.30.10 with both Wayland and X11 support,
including libdecor and its decoration plugins. Packaging checks force Wayland
on a headless compositor and create a Vulkan-capable window without X11 fallback.
To check the installed backend on a Wayland desktop, run
`SDL_VIDEODRIVER=wayland ./play --video-self-test` from the game directory.

## Run a local build

From this directory:

```sh
./run.sh
```

Build first using the instructions below, or use the installer above.

Press **Enter** to skip the opening and reach the title screen. For a repeatable
title-screen boot with an automatic Start press after 650 VI ticks:

```sh
./run.sh --auto-start 650
```

The executable is `build/native/mp2recomp`. The launcher changes into the project
directory so ROM and save paths resolve correctly. It requires your own
US ROM named `Mario Party 2 (USA).z64`, with SHA-1
`166eda1c05670d337e2c3f15a5db528ae1e5d6e3`. `--rom /path/to/file.z64`
overrides its location; the runtime validates the ROM's XXH3 hash before use.

Keyboard controls:

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

Press **F1** to open the settings window, or launch with `./run.sh --settings`.
Settings are organized into **Input**, **Audio**, **Graphics**, and **Netplay** tabs.
Click a keyboard/controller binding and press its replacement input; Escape
cancels a binding capture. Select a connected SDL-compatible controller, adjust
its left-stick deadzone, or choose keyboard-only input. Devices can be plugged
in while the game is running. Arrow keys always control the analog stick.

Master volume, left/right channel levels, and mute are applied to the game audio.
Settings save automatically in `userdata/settings.cfg`; Reset defaults restores
the current page (bindings/deadzone, audio, or graphics). Existing settings files
are loaded automatically. Escape or Done closes settings. Game input is
suppressed while settings is open; the game simulation continues. The settings
window uses Segoe UI/Arial on Windows and Noto Sans/DejaVu Sans on Linux.
Run `./tools/test_settings.sh` for the headless settings integration check.

The Graphics tab applies render resolution (original, 2x–4x, or fit window),
aspect ratio (4:3, 16:9, or fit window), fullscreen, and output filtering live.
The Netplay tab lets you create a room with a host address/port or paste a
friend's connection code to join. Creating or joining from offline play restarts
the game into the room lobby so everyone boots from the same state. Copy the
code, check connected players, and start or leave the room from that same tab.

Offline play connects controller port 1. Saves and the runtime's ROM copy live in
`userdata/`; the EEPROM file is `userdata/saves/mp2.us.bin`.

## Debugging

Logs persist automatically under `userdata/debug/`. Press **F8** for a
15-second frame/audio capture. Launch with `--debug-controls` for JSON status,
screenshots, captures and scripted input via `python tools/debug_client.py`.
Use `--debug-checkpoints` for loadable replay states: **F5** saves, **F9** loads,
and **F6** resumes.
See [debug controls and artifact format](docs/debugging.md).

## Experimental room multiplayer

Launch `./run.sh --netplay` (installed Linux: `./play --netplay`; Windows:
`mp2recomp.exe --netplay`). The host enters their public IP/DNS name and forwards
TCP port 7777, creates a room, and copies its connection code. Friends paste the
code to join, then the host starts. Two to four clients synchronize N64 controller
inputs over encrypted direct connections. Each player controls one assigned port.

No shared backend, relay, matchmaking or public-IP lookup service is used.
Connection codes contain the host address and session key, so share them privately.
See [netplay setup, port forwarding and tests](docs/netplay.md). Session saves
are isolated from offline saves. This is experimental; complete boards/minigames,
separate home networks and Linux/Windows cross-play still need validation.

## Rebuild

Linux boot has been verified with Clang, CMake, Ninja,
SDL2, SDL2_ttf, GTK3 and OpenSSL development libraries, and Vulkan on an AMD RX 7800 XT. The audio
microcode implementation requires SSE4.1 on x86-64. Windows compilation
is covered by the Windows CI build; actual Windows game boot still needs testing.

Dependencies and exact revisions are recorded in `dependencies.json`. To
populate a fresh source directory without replacing existing checkouts:

```sh
python tools/bootstrap.py
```

Put the US ROM at the path above, then regenerate code and compile:

```sh
./tools/regenerate.sh
cmake -S . -B build/native -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/native --target mp2recomp -j 8
```

The generation script installs its Python dependencies in a local virtual
environment, disassembles the ROM with Splat/spimdisasm, converts function
boundaries to N64Recomp metadata, and recompiles CPU code and audio microcode.
It writes diagnostic logs under `build/`. ROM contents and generated game C
are local build inputs/outputs and are excluded by `.gitignore`.

## Implementation

* `mp2.syms.toml` contains 8,528 function symbols in 117 sections: entry,
  main, and 115 overlays. `tools/generate_symbols.py` derives it from the
  community decompilation's Splat map and full disassembly.
* N64Recomp emits 8,405 C function definitions in 169 translation units.
  OS functions use the runtime's native replacements; selected entry/process
  routines have the host implementations described below.
* `src/overlays.cpp` registers generated function tables. `src/platform.cpp`
  wraps cartridge DMA so a newly loaded overlay updates native function
  lookup. The boot-logo overlay begins at ROM `0x36D9A0`; the title/intro
  overlay begins at `0x3D4D20`, both loaded at RAM `0x80102800`.
* `src/process.cpp` replaces four Hudson process functions with native fibers
  (`ucontext` on Linux and Win32 fibers on Windows). Each process keeps a native stack and a guest register
  context, preserving suspension through nested recompiled calls. The
  original scheduler's sleep, child-wait, priority-list order, and cleanup
  behavior are retained. This avoids invalid native `setjmp`/`longjmp`
  continuations after a C function has returned.
* `src/overlays.cpp` also supplies the small entry wrapper: clear the original
  BSS, establish the guest stack, and call the recompiled main routine.
* `src/renderer.cpp` bridges graphics tasks and VI registers to RT64 at
  original resolution and aspect ratio.
* `aspMain.toml` recompiles the ROM's audio microcode at offset `0xBF560`,
  size `0xE20`, IMEM address `0x1080`. Its 16 command targets come from the
  original dispatch table at ROM `0xD2090`.
* `src/main.cpp` supplies SDL audio and keyboard input. Audio length reporting
  is bounded to the current simulated AI DMA instead of SDL's entire queue,
  preventing unsigned buffer-size underflow during startup.

Ghidra analysis was used locally to cross-check the process scheduler; overlay
boundaries come from the community project. Ghidra projects, disassembly,
decompiler output, and generated analysis files are not distributed here.

## Validation and limits

Validated on November 9, 2026:

* CMake/Clang build completes; an immediate rebuild reports no work to do.
* The native build passes the logos, renders the animated opening, accepts
  an automatic Start input, and renders the title screen.
* `SDL_VIDEODRIVER=x11 ./run.sh --frames 3000 --auto-start 650` runs for
  3,000 VI ticks (about 50 seconds) and exits with status 0. No missing function,
  RSP, or unsupported SDK errors occurred in that run. Logs and screenshots
  remain local and are excluded from the repository.
* A separate 1,500-VI run under GDB renders the opening and exits normally.
* EEPROM initialization produces the expected 512-byte save with the
  `HUDSON` header. Existing saves are reused on later runs.

This is a boot/title proof of concept, not a validated full-game port.
Boards, minigames, multiplayer, long sessions, and audio/graphics fidelity
have not been comprehensively tested. Some title prompt glyphs appear as
blocks during local testing and need further renderer investigation.
No mod support is exposed by this frontend.

Two unused low-level SDK functions are stubbed in `mp2.toml`:
`__LeoBootGame3` (64DD boot) and `func_8009D24C_9DE4C` (an exception/scheduler
fragment). Additional unsupported 64DD/cop0 SDK entry points are explicit
abort traps in `src/platform.cpp`. None were reached during the boot test.

## Upstream work

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

## Source-only repository check

Before committing, stage only authored integration files and run:

```sh
python tools/audit_repository.py
```

The audit checks the Git index against an explicit publication allowlist and
rejects binary content, oversized files, symlinks, generated game code markers,
and ROM-derived assembly/decompiler output. New authored files must be reviewed
and added to that allowlist before they can pass. It does not replace source
review for provenance.

## License

Copyright (C) 2026 Andrew Cohn and contributors.

The authored integration code, installer and build scripts are free software:
you can redistribute them and/or modify them under the GNU General Public License
as published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version (`GPL-3.0-or-later`).

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full terms.

Third-party components retain their own licenses and copyright notices; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This license does not grant
rights to Nintendo/Hudson game code, ROMs, assets, names or trademarks. This is
an independent, unofficial project and is not affiliated with Nintendo or Hudson.
