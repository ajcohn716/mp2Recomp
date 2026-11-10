# Third-party notices

mp2Recomp's authored integration is GPL-3.0-or-later; see LICENSE. Upstream
components retain their licenses and copyright notices. Exact source revisions
are recorded in dependencies.json. tools/bootstrap.py retrieves those revisions
and their recursive submodules for development builds.

| Component | License | Source |
| --- | --- | --- |
| N64ModernRuntime | GPL-3.0 | https://github.com/N64Recomp/N64ModernRuntime |
| Zelda64Recomp (integration reference) | GPL-3.0 | https://github.com/Zelda64Recomp/Zelda64Recomp |
| N64Recomp | MIT | https://github.com/N64Recomp/N64Recomp |
| RT64 | MIT; bundled components have their own licenses | https://github.com/rt64/rt64 |
| DirectX-Headers | MIT | https://github.com/microsoft/DirectX-Headers |
| libdatachannel | MPL-2.0; bundled components have their own licenses | https://github.com/paullouisageneau/libdatachannel |
| OpenSSL 3 | Apache-2.0 | https://github.com/openssl/openssl |
| libcurl | curl license (MIT-style) | https://curl.se/ |
| SDL2 and SDL2_ttf | zlib | https://github.com/libsdl-org |
| Python | PSF-2.0 and bundled notices | https://www.python.org/ |
| PySide6 / Qt | LGPL-3.0 / GPL-3.0 and module-specific terms | https://code.qt.io/ |
| PyInstaller | GPL-2.0-or-later with bootloader exception | https://github.com/pyinstaller/pyinstaller |
| Splat | MIT | https://github.com/ethteck/splat |
| spimdisasm | MIT | https://github.com/Decompollaborate/spimdisasm |

Optional Steam-enabled source builds also link Valve's Steamworks runtime. The
Steamworks SDK is supplied separately and retains Valve's terms; it is not part
of the open-source dependency pack or this repository. The integration uses
Spacewar App ID 480 for standalone builds. See https://partner.steamgames.com/.

The installer SDK contains source trees for N64Recomp, N64ModernRuntime, RT64,
DirectX-Headers and libdatachannel, including their bundled license files.
It also includes precompiled static libraries from these open-source runtime
dependencies. Their matching sources, build recipes and license notices remain
in the same SDK; ROM-derived game libraries and microcode are not packaged.
Only the Splat configuration and symbol metadata from the community Mario Party 2
decompilation are used for local generation; game source and assets are excluded.

The installer contains the reviewed integration source under its bundled
`source` directory, and LICENSE and these notices are also placed beside the
installer. The release includes a matching integration source archive.
SDK toolchains are assembled from conda-forge (Linux) or MSYS2 (Windows); their
package metadata and upstream license notices remain inside the SDK. The build
recipes are tools/dependency-environment.yml and .github/workflows/release.yml.
See each bundled component's license for its applicable terms.

Game code is generated only on the user's computer from a user-supplied ROM.
Nintendo/Hudson game content is not covered by this project's license and is
not included in release downloads.
