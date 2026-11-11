# Installing and upgrading

[Back to the README](../README.md) · [Build from source](building.md)

Use the packaged installer for the quickest setup. It includes the tools needed
to build the game from your own ROM. You need a Linux or Windows x86-64 computer,
a working Vulkan graphics driver, and an unmodified **Mario Party 2 USA ROM**.
Windows installation and actual game boot still need testing on a Windows computer.

## Install in a few steps

1. Download the installer archive for your platform from the
   [releases page](https://github.com/ajcohn716/mp2Recomp/releases).
   Choose `mp2recomp-installer-linux-x86_64.tar.gz` on Linux or
   `mp2recomp-installer-windows-x86_64.zip` on Windows.
2. Extract the whole archive. Keep its files together, including the compressed
   SDK archive beside the installer. The SDK is the bundled dependency pack;
   you do not need to extract it separately. Put your ROM beside the installer
   executable for automatic detection.
3. Open `mp2recomp-installer` on Linux or `mp2recomp-installer.exe` on Windows.
   Confirm the detected ROM (or drag your ROM onto the window), choose an
   installation directory, and press **Go**.
4. Wait for **Installed. Ready to play.** The installer can launch the game when
   it finishes. Later, use the application shortcut, `./play` from the Linux
   installation directory, or `mp2recomp.exe` on Windows.

The installer accepts `.z64`, `.v64`, and `.n64` copies of the supported ROM and
converts the byte order locally. Other regions and modified ROMs are rejected.
The supported big-endian ROM has SHA-1
`166eda1c05670d337e2c3f15a5db528ae1e5d6e3` and is 32 MiB.

Automatic detection checks `.z64`, `.v64`, and `.n64` files beside the installer
executable first, then in the current working directory. Filenames and extension
case do not matter; only a ROM that passes validation is selected. If none is
found, drag a ROM onto the window or click the drop area to browse for one.

## Upgrade and keep your saves

Run the new installer and select your existing installation directory. Settings
and saves are preserved. Back up `userdata/` if you want a separate copy before
upgrading; the offline save is `userdata/saves/mp2.us.bin`.

## Install from a terminal

Run these commands from the extracted installer directory. Replace the example
paths with your own. On Linux:

```sh
./mp2recomp-installer --rom "/path/to/Mario Party 2 (USA).z64" \
  --prefix "/path/to/mp2recomp" --jobs 4 --launch
```

On Windows, in PowerShell:

```powershell
.\mp2recomp-installer.exe --rom "C:\Games\Mario Party 2 (USA).z64" --prefix "C:\Games\mp2recomp" --jobs 4 --launch
```

Use `--help` for build/cache locations, shortcuts, and other options. Lower
`--jobs` if compilation uses too much memory.

Providing `--rom` selects that file and starts a terminal installation; add
`--gui` to open the window with that file selected instead. Without `--rom`,
the installer opens the GUI with any automatically detected ROM selected.
Use `--validate-rom` to check an automatically detected ROM without installing.

## Offline installation and dependency packs

The release installer archive includes its matching SDK, so it can install
offline. Keep the SDK archive beside the executable. If it is missing, the
installer downloads the matching pack and checks its release-specific SHA-256
checksum. You can also select it in the GUI's advanced options or pass it explicitly:

```sh
./mp2recomp-installer --rom "/path/to/game.z64" \
  --dependency-pack "/path/to/mp2recomp-sdk-linux-x86_64.tar.gz"
```

The pack includes Python, a compiler, build tools, open-source dependencies, and
a precompiled Steam helper with its runtime library. You do not need to
install Python, Visual Studio, MSYS2, or development packages separately.
Your graphics driver must still provide Vulkan support.

Game code and audio microcode are generated and compiled on your computer.
Installation still takes time: this is a local build, not just file extraction.
No ROM, generated game code, or game assets are sent to the cloud build.

Version 0.2.2 includes Steam friends and in-game invitations automatically. No Steamworks SDK download
or development headers are needed by players. Install Steam separately, start it,
and sign in before using **F1 → Netplay → Steam**. Each player needs their own
Steam account and computer. Offline and Direct TCP play work without Steam.
See [multiplayer setup](netplay.md).

## If something goes wrong

| Problem | What to try |
| --- | --- |
| ROM rejected | Use an unmodified 32 MiB USA ROM. The installer accepts all three byte orders; renaming a different ROM will not make it compatible. |
| Dependency pack missing or rejected | Use the matching SDK from the same release, keep it compressed beside the installer, or select it in advanced options. |
| Installation fails during compilation | Keep the installer log, retry with fewer compiler jobs, and reuse the same build/cache directory. |
| Windows reports `No such file or directory` under `mp2recomp-cache/dependencies`, especially for a long shader filename | Use the v0.2.3 or newer installer and its matching SDK. Older installers can exceed Windows' path-length limit when unpacking dependencies. Selecting an offline SDK archive or another build directory does not change the dependency cache location. |
| Game will not open a graphics window | Check that your graphics driver supports Vulkan. On Linux, the video self-test below can help identify the display backend. |
| Steam helper missing or damaged | Reinstall using the complete v0.2.2 archive and matching dependency pack. Keep the installed helper and its runtime files together. |
| Steam initialization fails | Start Steam and sign in under the same OS user as the game, or choose Direct TCP. |
| Need logs after a crash | Look in `userdata/debug/` inside the game directory; see [debugging](debugging.md). Logs can contain private room codes. |

The Linux SDK uses pinned SDL 2.30.10 with Wayland and X11 support, including
libdecor and its decoration plugins. To check Wayland from the installation directory:

```sh
SDL_VIDEODRIVER=wayland ./play --video-self-test
```

Packaging checks also create a Vulkan-capable window on a headless Wayland
compositor without falling back to X11.

## For release maintainers

The `Installer release` GitHub Actions workflow builds each platform's installer
and matching dependency pack. Tagged `v*` builds prepare a draft release for
verification before publication; manual runs also provide Actions artifacts.

For installer-only changes, workflow dispatch can reuse a verified SDK run via
`sdk_run_id`. Its dependency pins and toolchain recipe must match; leave the field
blank to rebuild both SDKs.

Version 0.2 introduces precompiled open-source runtime libraries. Its first
release must rebuild both SDKs; older SDK artifacts cannot be reused by the
release packager. Only explicitly allowlisted dependency archives enter the
runtime library set. Game code, microcode, and compiled test fixtures are
excluded. Before packaging, a fresh ROM-free consumer build must link the
libraries, pass its self-tests, and compile no dependency objects. The installer
checks the runtime recipe, archive checksums, and compiler/platform compatibility
before using them.

A compiler is still bundled because installation generates, compiles, and links
game code locally from the user's ROM. Explicitly supplied older SDKs without
precompiled libraries retain the full local build path.


Version 0.2.1 also builds the standalone MIT Steam helper from private Steamworks
SDK 1.65 inputs. The workflow reads checksum-pinned input from the repository's
`MP2_STEAMWORKS_SDK_00` through `_25` Actions secrets. Headers are used only on
build runners. Public packs contain the helper, the platform runtime library,
metadata and license notices, never the full Steamworks SDK or headers. SDK
reuse requires matching runtime and Steam-helper recipes; v0.2 SDKs cannot be
used for v0.2.1. `tools/build_dependency_pack.py` requires `--steamworks-sdk PATH`
when building release packs locally. The GPL game communicates with the helper
over process pipes and does not link Valve's runtime.

Version 0.2.2 requires freshly rebuilt SDKs with the protocol-2 Steam helper.
Do not reuse v0.2.1 SDK artifacts: the game and helper protocol must match.
The graphical game initializes Steam at launch, so friends playing offline
can receive invitations without creating their own room.

Version 0.2.3 requires freshly rebuilt SDKs to omit unused SPIRV-Cross shader
regression inputs and reference outputs. Do not reuse v0.2.2 SDK artifacts.
SDK extraction, checksum verification, cache publication and source copying use
extended Windows paths for file operations, without requiring users to enable
the system long-path policy. Build commands keep ordinary paths for toolchain
compatibility. The release workflow tests the actual Windows SDK with
`LongPathsEnabled=0`, including extraction into a long user-cache path and
copying its sources. This does not remove path limits imposed by external
compilers or other build tools for arbitrarily deep custom build directories.
