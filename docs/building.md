# Building from source

[Back to the README](../README.md) · [Use the installer instead](installation.md)

Use this guide if you want to change the code or build with your own development
tools. For playing the current build, the installer bundles the toolchain for you.
The commands below run from the repository root on Linux.

## Before you begin

You need Git, Python with `venv` and `ensurepip`, Clang/Clang++, CMake 3.24 or
newer, Ninja, pkg-config, and ripgrep (`rg`). Install development libraries for
SDL2, SDL2_ttf, GTK3, Fontconfig, xkbcommon, OpenSSL, and libcurl (7.71 or newer),
plus Noto Sans or DejaVu Sans for settings text. Running the game also needs a
Vulkan graphics driver. On x86-64, the audio microcode requires SSE4.1.

Check the requirements recognized by the installer:

```sh
python install.py --check-system
```

If your system calls Python 3 `python3`, use that command for the Python examples.
For `tools/regenerate.sh`, make sure `python` resolves to Python 3, for example
by activating a Python virtual environment first.

Linux boot has been verified with these dependencies on an AMD RX 7800 XT.
Windows compilation is covered by CI, but actual Windows game boot still needs
testing. Use the [Windows installer](installation.md) for the bundled toolchain;
these shell commands describe the Linux development workflow.

## 1. Fetch the pinned dependencies

```sh
python tools/bootstrap.py
```

Exact revisions are recorded in [dependencies.json](../dependencies.json).
Bootstrap fetches missing checkouts and their submodules. If an existing checkout
has a different revision, it stops and leaves that checkout untouched.

## 2. Supply your ROM

Place your unmodified big-endian USA ROM in the repository root with the exact
filename `Mario Party 2 (USA).z64`. The supported SHA-1 is
`166eda1c05670d337e2c3f15a5db528ae1e5d6e3`.
The manual generation path expects this byte order; use the installer to convert
`.v64` or `.n64` copies. You can validate a ROM without building:

```sh
python install.py --rom "Mario Party 2 (USA).z64" --validate-rom
```

## 3. Generate and compile

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
It writes diagnostic logs under `build/`, including `splat.log`, `recompile.log`,
and `rsp-recompile.log`. ROM contents and generated game C are local build
inputs/outputs and are excluded by `.gitignore`.

After changing host-side C++ code, rerun only the `cmake --build` command.
Regenerate when you change the recompiler configuration or symbol-generation inputs.

## 4. Run your build

```sh
./run.sh
```

The executable is `build/native/mp2recomp`. The launcher changes into the project
directory so ROM and save paths resolve correctly. Press **Enter** to skip the
opening, or use an automatic Start press after 650 video interrupts (VI ticks):

```sh
./run.sh --auto-start 650
```

`--rom /path/to/file.z64` overrides the runtime ROM location; the runtime checks
its XXH3 hash. This option does not change the ROM path used during generation.
See the [README controls](../README.md#controls-and-settings),
[debugging guide](debugging.md), and [Steamworks build option](netplay.md#build-with-steamworks).

## Use system tools through the installer

To build and install outside the checkout while using your system dependencies:

```sh
python install.py --source-build --rom "/path/to/game.z64" --prefix "/path/to/mp2recomp"
```

Choose installation and build/cache directories outside the source checkout and
separate from each other. Add `--gui` for the graphical installer; in source mode
it requires `python -m pip install -r tools/installer-requirements.txt` (use a virtual
environment). See [installation options](installation.md#install-from-a-terminal).

## How it works

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

## Recorded validation and limits

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

## Check your changes

For the headless settings integration check:

```sh
./tools/test_settings.sh
```

See [debugging verification](debugging.md#verification) for capture and replay
checks, and [multiplayer tests](netplay.md#tests) for transport and game sessions.
Those sections explain which checks require a ROM, graphics driver, or Steam.
Settings text uses Segoe UI/Arial on Windows and Noto Sans/DejaVu Sans on Linux.

## Before committing

Before committing, stage only authored integration files and run:

```sh
python tools/audit_repository.py
```

The audit checks the Git index against an explicit publication allowlist and
rejects binary content, oversized files, symlinks, generated game code markers,
and ROM-derived assembly/decompiler output. New authored files must be reviewed
and added to that allowlist before they can pass. It does not replace source
review for provenance.
