# Prebuilt game packages

[Back to the README](../README.md) · [Build from source](building.md)

## Play

Extract the archive for your platform into a writable folder. To detect your
ROM automatically, create a `games/` folder beside the executable and put
your unmodified 32 MiB Mario Party 2 USA ROM directly inside it. The game
checks `.z64`, `.v64`, and `.n64` files regardless of filename or extension case.

Open `mp2recomp.exe` on Windows or `./play` on Linux. If the game needs a ROM
and finds none it can use, select one in the picker. The game validates the
dump and keeps a local copy in `userdata/mp2.us.z64` for later launches.

You can also put `Mario Party 2 (USA).z64` beside the game, or launch with
`--rom` and an absolute path. The game still needs a Vulkan graphics driver.
For an upgrade, extract the new package and copy your existing `userdata/`
folder into it. This brings over the ROM, saves, and settings.

## Build a package locally

Build each package on its target platform with Python 3.12 or later and the
matching mp2Recomp SDK archive and JSON metadata. Use a build machine with your
own supported ROM. The SDK and compiler run on that machine. The portable
package contains the linked game, runtime libraries, and player instructions.
The linked executable contains the ROM-derived game and audio code. Keep the
private build directory, ROM, and generated sources separate from the package.

On Linux, from the repository root:

```sh
python tools/package_game.py stage \
  --rom "/path/to/Mario Party 2 (USA).z64" \
  --sdk-archive /path/to/mp2recomp-sdk-linux-x86_64.tar.gz \
  --sdk-metadata /path/to/mp2recomp-sdk-linux-x86_64.json \
  --build-dir /path/to/private-build --output /path/to/game
python tools/package_game.py archive --stage /path/to/game --output /path/to/artifacts
```

On Windows, use a short path for the private build directory. Supply the
directory containing the x64 Visual C++ redistributable DLLs (`msvcp140.dll`,
`vcruntime140.dll`, and `vcruntime140_1.dll`).

```powershell
python tools/package_game.py stage `
  --rom 'C:\Games\Mario Party 2 (USA).z64' `
  --sdk-archive C:\Downloads\mp2recomp-sdk-windows-x86_64.zip `
  --sdk-metadata C:\Downloads\mp2recomp-sdk-windows-x86_64.json `
  --vc-runtime-dir C:\BuildTools\vcredist-x64 `
  --build-dir C:\mp2-build --output C:\mp2-game
```

You can pass `--sdk-dir` instead of the archive and metadata when you already
have the matching SDK extracted and relocated. Pick a new `--output` directory
for each stage and reuse `--build-dir` to keep the build and SDK cache.

For a Windows release, sign the staged executables and DLLs with a publicly
trusted publisher certificate, retaining valid upstream signatures. Use
`tools/windows_signing.ps1 -Mode Prepare -Root C:\mp2-game -Catalog C:\mp2-build\sign.txt`
from PowerShell 7 to list files that need a signature. After signing:

```powershell
python tools/package_game.py archive --stage C:\mp2-game --output C:\Artifacts
```

The archive command checks the staged payload and writes the archive and its
`.sha256` file. On Windows it checks embedded signatures before writing the
release ZIP by default. Use `--unsigned-release` to package an unsigned Windows
release, or `--unsigned-test` for a ZIP labeled for local testing. Test an
extracted copy so the player's ROM and saves go into that copy instead of the
staging folder. For a signed Windows build, test first launch with Smart App
Control enabled.
