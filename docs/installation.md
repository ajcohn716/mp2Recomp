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
   you do not need to extract it separately.
3. Open `mp2recomp-installer` on Linux or `mp2recomp-installer.exe` on Windows.
   Drag your ROM onto the window, choose an installation directory, and press **Go**.
4. Wait for **Installed. Ready to play.** The installer can launch the game when
   it finishes. Later, use the application shortcut, `./play` from the Linux
   installation directory, or `mp2recomp.exe` on Windows.

The installer accepts `.z64`, `.v64`, and `.n64` copies of the supported ROM and
converts the byte order locally. Other regions and modified ROMs are rejected.
The supported big-endian ROM has SHA-1
`166eda1c05670d337e2c3f15a5db528ae1e5d6e3` and is 32 MiB.

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
precompiled RT64, N64ModernRuntime, and networking libraries. You do not need to
install Python, Visual Studio, MSYS2, or development packages separately.
Your graphics driver must still provide Vulkan support.

Game code and audio microcode are generated and compiled on your computer.
Installation still takes time: this is a local build, not just file extraction.
Explicitly supplied older SDKs without precompiled libraries use the full local
build path. No ROM, generated game code, or game assets are sent to the cloud build.

Steam support is a separate source-build option. The installer dependency packs
do not include the Steamworks SDK; see [multiplayer setup](netplay.md).

## If something goes wrong

| Problem | What to try |
| --- | --- |
| ROM rejected | Use an unmodified 32 MiB USA ROM. The installer accepts all three byte orders; renaming a different ROM will not make it compatible. |
| Dependency pack missing or rejected | Use the matching SDK from the same release, keep it compressed beside the installer, or select it in advanced options. |
| Installation fails during compilation | Keep the installer log, retry with fewer compiler jobs, and reuse the same build/cache directory. |
| Game will not open a graphics window | Check that your graphics driver supports Vulkan. On Linux, the video self-test below can help identify the display backend. |
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

Only explicitly allowlisted open-source dependency archives enter the runtime
library set. Game and compiled fixture code are excluded. Before packaging, a
fresh ROM-free consumer build must link the libraries, pass self-tests, and
compile no dependency objects. The installer checks the runtime recipe, archive
checksums, and compiler/platform compatibility before using them.

For installer-only changes, workflow dispatch can reuse a verified SDK run via
`sdk_run_id`. Its dependency pins and toolchain recipe must match; leave the field
blank to rebuild both SDKs. The first release with precompiled runtime libraries
must rebuild both SDKs: older SDK artifacts cannot be reused by the release packager.
