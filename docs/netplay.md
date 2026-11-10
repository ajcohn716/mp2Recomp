# Play with friends

[Back to the README](../README.md) · [Installation](installation.md) · [Build from source](building.md)

Multiplayer (netplay) is experimental and supports two to four players. Each
person runs the game locally with their own supported Mario Party 2 USA ROM and
controls one N64 controller port. Use matching builds; the game checks source
compatibility before letting a player join.

## Choose a connection method

| Method | What you need | Setup |
| --- | --- | --- |
| Direct TCP on a LAN | Installer or source build; host's LAN address | [Direct room steps](#direct-tcp-rooms); no router forwarding needed |
| Direct TCP over the internet | Installer or source build; host's public address and TCP port forwarding | [Direct room steps](#direct-tcp-rooms) |
| Steam | Version 0.2.1 installer or a source build with the helper; Steam running for every player | [Steam room steps](#steam-rooms); no port forwarding needed |

**Version 0.2.1 installers include Steam support.** Players need Steam installed,
running and signed in; they do not need the Steamworks SDK. Source developers can
[build the standalone helper](#build-with-steamworks) separately. Steam is the
default transport; Direct TCP remains available without Steam.

Creating or joining a room restarts the game into a lobby. Finish or save your
offline game first; it cannot be continued online. Everyone must join before
the host starts. Online saves are kept separate from offline saves.

## What to expect in a room

The host is player 1; guests receive ports 2–4 in join order. Unoccupied ports
report disconnected. Creating or joining during offline play restarts the game
into the lobby; an offline game cannot be continued online. Every player joins
before boot. Late joining, reconnecting, spectators and host migration are not
supported. Disconnects and detected state mismatches stop the session.

Each player's normal keyboard/controller bindings apply to their assigned port.
Settings suppress that player's input while the game continues. The game still
chooses human/CPU players through its own menus.

## Steam rooms

Install version 0.2.1 or use a source build with the helper. Start Steam and sign in before creating
or joining a room. Each player needs a separate Steam account and computer.
This alpha uses the **Spacewar test App ID 480**, including standalone launches.
Steam may display the session as Spacewar. Friend invitations require matching
game and protocol-2 helper builds from these sources; the older v0.2.1
protocol-1 helper must be rebuilt or replaced.

1. Open **F1 → Netplay**, or launch `./run.sh --netplay` (installed Linux:
   `./play --netplay`; Windows: `mp2recomp.exe --netplay`).
2. Leave **Transport: Steam (App ID 480)** selected and click **Create room**.
3. Click **Invite friends** to see Steam friends currently playing **Spacewar
   (App ID 480)**. Select **Invite** beside a friend. **Refresh friends** updates
   the list; friends playing other games or with unavailable game activity are
   excluded. Only the host can invite, before starting and while the room has space.
4. Friends receive an in-game **Steam invitations** panel, including during
   offline or netplay games. Click **Accept** to leave the current game/room and
   join the invited room, or **Dismiss** to keep playing. Accepting during a game
   restarts into the lobby; current game progress is not carried into the room.
   **F1 → Netplay → Steam invites** reopens pending invitations. Steam's own
   invite Join button also opens the in-game confirmation.
   Alternatively, use **Copy code** and share the complete `MP2S-...` code
   privately. Friends paste it into **Join with a connection code** and click
   **Join room**. The code automatically selects Steam regardless of their
   current transport selection.
5. When everyone is connected, the host clicks **Start game**.

No public-IP lookup, router port forwarding, web room server, or separate
matchmaking backend is needed for Steam rooms. The code contains the host's
Steam identity and a fresh random session key. Networking uses
`ISteamNetworkingSockets::CreateListenSocketP2P` / `ConnectP2P`, Steam's rendezvous
service, and Steam Datagram Relay support. Both sides use virtual port **0**;
TCP port settings do not apply. Valve's Steam service is required. Invitations
use a private Steam matchmaking lobby containing the room's connection code; it closes when the host starts or
leaves. The existing authenticated P2P connection still carries game traffic.
Expired invitations cannot join a started or closed room.

Command-line examples from a source checkout:

```sh
./run.sh --netplay --netplay-transport steam
./run.sh --join 'MP2S-PASTE_THE_FULL_CODE_HERE'
```

## Direct TCP rooms

1. Open **F1 → Netplay** and select **Transport: Direct TCP**.
2. On the host, enter the address guests will use: the host's LAN IP for the same
   network, public IP or DNS name for the internet, or `127.0.0.1` for tests on
   one computer.
3. For internet play, forward TCP port **7777** on the host's router to the gaming
   computer. Allow the chosen port through the host's firewall. Guests do not
   need to forward ports. LAN play does not need router forwarding.
4. The host clicks **Create room**, then shares the complete `MP2-...` code privately.
5. Guests paste it into **Join with a connection code** and click **Join room**.
   When everyone is connected, the host clicks **Start game**.

Direct TCP game traffic does not use Steam. The graphical game can still listen
for Steam invitations while playing offline or over Direct TCP.

Direct mode can make a background HTTPS request to
`https://ipv4.icanhazip.com/` to prefill the public IPv4 address. Only that service
sees the lookup's outbound IP; no ROM, save, session key or code is sent to it.
The lookup times out after five seconds and allows manual entry on failure.
**Detect public IP** retries. Manual edits and explicit `--host-address` values
are preserved. Steam mode never starts this lookup.

```sh
./run.sh --netplay --host-address 203.0.113.10 --host-port 7777
./run.sh --join 'MP2-PASTE_THE_FULL_CODE_HERE'
```

`--host-address` selects Direct TCP for compatibility with existing scripts.
An explicit `--netplay-transport steam` after it switches back to Steam. The IP
above is an example. Direct invitations include the address, port and session
key. Router external/internal ports must match. Carrier-grade NAT may prevent
hosting direct rooms; Steam rooms provide an alternative. Direct mode has no
relay fallback. Public-IP lookup does not configure port forwarding; with a VPN,
the detected IP may be its exit address.

## Saves and compatibility

Before boot, the host sends a copy of its 512-byte EEPROM over each encrypted
peer connection. Every client uses that same initial save and random seed.
Session saves are separate from offline saves:
`userdata/netplay/session-p<slot>-<unique-id>/saves/mp2.us.bin`.
Netplay never overwrites `userdata/saves/mp2.us.bin`. Settings still use the usual
settings file. Keep a host session save if you want to resume it later; replacing
an offline save is a separate manual action with a backup.

Everyone needs the same authored source build and their own supported ROM. A
source fingerprint is checked before assigning a controller slot. ROM bytes are
not sent over the network. Complete game determinism across operating systems
is still unverified; use matching platform builds for initial testing.

## Transport and privacy

Both transports carry the same ordered, reliable session protocol. Steam uses
reliable messages with Nagle disabled; Direct TCP uses libdatachannel WebSockets.
Input netplay waits for missing inputs, with three frames of buffering. Rollback
is not implemented; loss and latency can cause visible stalls.

Both retain the room's OpenSSL AES-256-GCM authentication. The random 256-bit key
is in the invitation and never sent in the handshake. Fresh peer nonces and
HKDF-SHA256 derive separate directional keys; authenticated sequence numbers
reject replays, reflection and reordering. Authentication and build checks occur
before a guest receives a controller slot, save or input stream. Message sizes,
connections, queued work and pending frames are bounded.

Anyone with the full code can join an unstarted room. Share codes privately.
Steam rooms expose the host's Steam ID in the invitation; Direct rooms expose
the host address. Diagnostic logs contain codes and should stay private. Starting
a new room generates a new key. Codes expire when the host leaves; session codes
are never used as save-directory names.

## If someone cannot connect

| Symptom | What to check |
| --- | --- |
| Steam helper unavailable | Reinstall version 0.2.1 with its matching dependency pack, keep the helper/runtime files together, or select Direct TCP. |
| Steam initialization fails | Start Steam, sign in, and run it as the same OS user as the game. Each remote player needs their own account and computer. |
| Direct TCP works on LAN but not over the internet | Check the public address, host firewall, and TCP forwarding. External/internal ports must match. Carrier-grade NAT can prevent hosting. |
| Public-IP detection fails or shows a VPN address | Enter the reachable host address manually; detection does not configure your router. |
| Build mismatch | Use matching game builds and supported ROMs on every computer. |
| Cannot join a game already in progress | Create a new room and have everyone join before starting. Late joining and reconnecting are not supported. |
| A session stalls or stops | Missing input can stall play; disconnects or detected state mismatches stop it. Keep logs private because they can contain room codes. |

## Build with Steamworks

First complete the [source-build prerequisites and code generation](building.md).
Obtain the Steamworks SDK (validated with version 1.65) from Valve and extract
it outside tracked source, for example `build/steamworks-sdk`. It must contain
`public/steam/steam_api.h` and `redistributable_bin`. No SDK headers or Valve
binaries are committed or fetched by `tools/bootstrap.py`. The SDK is an explicit
development input for compiling the helper; release installers already include
the compiled helper and permitted runtime files.

```sh
cmake -S . -B build/native -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DMP2_STEAMWORKS_SDK="$PWD/build/steamworks-sdk"
cmake --build build/native --target mp2recomp -j 6
```

CMake builds `mp2recomp-steam-helper` as an independent MIT executable and copies
it and `libsteam_api.so` or `steam_api64.dll` beside the game. Only the helper
links Valve's runtime and includes SDK headers. The game talks to it through
process pipes; no emulator or ROM-derived code is linked into the helper.
Keep the helper and runtime files with the executable when moving a build.
Initialization sets `SteamAppId` and `SteamGameId` to 480. Steam must run under
the same OS user. The graphical game starts the helper asynchronously to receive
invitations during play, and shares it with Steam netplay rooms. Offline and
Direct TCP play still work if Steam or the helper is unavailable. Start Steam
and use **Steam invites → Refresh Steam** to retry an initial connection failure.
Headless automated games start the helper only if they need a Steam transport;
interactive lobbies also listen for invitations.

To build only the helper, without a ROM or emulator dependencies:

```sh
cmake -S steam-helper -B build/steam-helper -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DMP2_STEAMWORKS_SDK="$PWD/build/steamworks-sdk"
cmake --build build/steam-helper -j 4
./build/steam-helper/mp2recomp-steam-helper --self-test
```

A source build can instead use an existing precompiled bundle with
`-DMP2_STEAM_HELPER_DIR=/path/to/sdk/steam`. Without either build option, Steam
also works if the matching helper/runtime files are already beside the game.
Otherwise the game reports the missing helper and Direct TCP remains usable.
The Steam SDK/runtime retain Valve's terms. See [helper protocol and licensing](steam-helper.md).

## Tests

Run these from the repository root. The Direct TCP transport tests need neither
a ROM nor Vulkan, and do not require Steam or its SDK:

```sh
python tools/bootstrap.py
cmake -S . -B build/netplay -G Ninja -DMP2_NETPLAY_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/netplay --target netplay_test netplay_steam_social_test -j 4
./build/netplay/netplay_test
./build/netplay/netplay_steam_social_test
```

To also test Steam, supply the SDK and run Steam signed in as the same OS user:

```sh
python tools/bootstrap.py
cmake -S . -B build/steam-netplay -G Ninja -DMP2_NETPLAY_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DMP2_STEAMWORKS_SDK="$PWD/build/steamworks-sdk"
cmake --build build/steam-netplay --target netplay_test netplay_steam_test netplay_steam_social_test -j 4
./build/steam-netplay/netplay_test
./build/steam-netplay/netplay_steam_social_test
./build/steam-netplay/netplay_steam_test
./tools/test_settings.sh
```

`netplay_test` runs real Direct TCP peers: four-controller ownership/order under
asymmetric scheduling, common seed/save, desync detection, wrong keys, full rooms,
late joining, slot reuse and disconnects. It also checks both invitation formats
and cryptographic tamper/replay/wrong-key/reflection rejection. No external
service is contacted. Use the first test recipe above for SDK-free builds.

`netplay_steam_social_test` exercises the shared helper process protocol with a
local fixture: asynchronous startup, friend/invite metadata, accepting and
dismissing, socket routing during UI polls, and reuse across room changes. It
requires neither Steam nor its SDK. The settings tests cover friend paging,
host-only controls, in-game acceptance, dismissal, invalid replies and retries.

`netplay_steam_test` requires a running, signed-in Steam client. It exercises the
real Steam API under App ID 480, checks the Spacewar friend filter, creates and
clears a private invitation lobby, creates a P2P listener and encrypted UDP loopback
pair, checks reliable text/binary ordering and bounds, and shuts down/reinitializes
the API. It does not use a mock SDK. It does **not** simulate multiple Steam users
or prove that a remote relay connection works.

Actual-game tests require a local ROM and full build, plus Python 3.10+:

```sh
python tools/test_game_netplay.py --frames 1200
```

The script acquires a machine-wide lease, starts four isolated game processes
over Direct TCP, supplies scripted four-port input, and compares private traces.
Children are cleaned up on failure; offline saves are untouched. `--save PATH`
supplies a copy of an initialized EEPROM. Logs and memory-derived checkpoints
remain under ignored `build/`. The default test port is 17777; `--port` changes it.

Validated locally on November 10, 2026: Linux full-game Steam room creation,
real Steam API listener and UDP loopback checks (SDK 1.65), both SDK-enabled and
SDK-free direct transport tests, settings integration, and four actual game
processes with 1,200 matching input/RNG/checkpoint frames over Direct TCP.

## Synchronization and current limits

MP2's normal VI-driven acquisition queues up to eight controller samples.
Asynchronous timing could make peers consume different numbers of them. Online
mode disables acquisition and inserts exactly one agreed sample before each
call to the original input processor. Button edges/repeats still use game code.
Wire samples contain integer N64 button/stick values. Game-facing clock reads
use logical frame time; audio and display pacing retain host clocks.

Every 60 frames clients compare a canonical checkpoint covering RNG, board/player
state and overlay history. A mismatch stops all clients. This checkpoint does
**not** cover every minigame's private state, native fiber stacks or all guest
memory, so it is a desync detector rather than a complete determinism proof.
The private test traces compare those checkpoints on every input frame.

The port was a boot/title proof of concept before netplay. Four-client menu
checks don't establish that every board or minigame works. Steam sessions across
separate accounts/home networks, Linux/Windows cross-play, complete
boards/minigames, long sessions, and hostile-network soak testing still need
validation. Rollback isn't implemented.

Dependencies: [libdatachannel](https://github.com/paullouisageneau/libdatachannel)
(MPL-2.0) and [OpenSSL](https://www.openssl.org/) retain their respective licenses.

Steamworks API references: [initialization](https://partner.steamgames.com/doc/sdk/api),
[P2P networking](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets).
