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

Version 0.2.1 installers include a standalone `mp2recomp-steam-helper` process and
Valve's `steam_api64.dll` (Windows) or `libsteam_api.so` (Linux). The helper's
authored source and shared generic transport/protocol headers are MIT-licensed;
see LICENSE-STEAM-HELPER for the exact scope. It links only Valve's runtime and
standard system/compiler libraries, not the GPL game or N64ModernRuntime.
The game uses process pipes to communicate with this independent service.

Valve's proprietary runtime retains Valve's terms and is distributed as an SDK
Redistributable with the helper under section 1.1(b) of the Steamworks SDK Access
Agreement: https://partner.steamgames.com/documentation/sdk_access_agreement/.
The full Steamworks SDK and headers are private build inputs and are not part of
public packs or tracked source. See steam-helper/STEAMWORKS-NOTICE.txt, also
installed beside the helper. This alpha uses the Spacewar test App ID 480.
No Valve affiliation or endorsement is implied. Third-party GPL licenses are
unchanged; this project does not grant exceptions on upstream authors' behalf.

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
