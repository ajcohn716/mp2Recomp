# Direct room multiplayer

Experimental input netplay for two to four players using the supported Mario
Party 2 USA ROM. Each player runs the game locally and owns one N64 controller
port. One player's computer hosts the room and distributes agreed input frames.

There is no shared room server, relay, matchmaking backend, account service,
STUN service or public-IP lookup. The client never contacts any of those services.
The host listens on one TCP port. A shareable connection code contains the host
address, port and a fresh random session key, so it is longer than a short lobby
code. Code generation and joining don't require third-party infrastructure.

## Host a game

1. Forward TCP port **7777** on your router to your gaming computer's LAN IP.
   Allow inbound TCP 7777 through that computer's firewall. Use a stable LAN IP
   or router DHCP reservation so the forwarding rule stays valid.
2. Find your router's public IP address yourself, or use a DNS name you control
   that points to it. The app doesn't discover your public address externally.
3. Launch `./run.sh --netplay` (installed Linux: `./play --netplay`; Windows:
   `mp2recomp.exe --netplay`). Enter that public IP/DNS name and TCP port.
4. Click **Create room**, then **Copy code**. Share the whole `MP2-...` code
   privately with friends. It grants access to this session.
5. When everyone is connected, click **Start game**. The host is player 1;
   guests get ports 2–4 in join order. Unoccupied ports report disconnected.

Guests launch with `--netplay`, paste the code and click **Join room**. They don't
need to forward a port, run a service, or enter a separate address. The address
fields are for creating a room; joining reads the address from the code.

For LAN play, advertise the host's LAN IP instead; no router forwarding is
needed. `127.0.0.1` works only when all clients run on the same computer. The
initial address field uses that local-test default; replace it for online play.
An alternative port can be chosen, but the router's external and internal port
numbers must match the advertised port in this version.

You can prefill the fields from the command line:

```sh
./run.sh --netplay --host-address 203.0.113.10 --host-port 7777
./run.sh --join 'MP2-PASTE_THE_FULL_CODE_HERE'
```

The IP above is an example address. Each new hosted session generates a new key
and code. Codes stop working when the host exits. Everyone joins before boot;
late join, reconnection, spectators and host migration are not supported.
Disconnects and detected state mismatches stop the session.

Some ISPs put customers behind carrier-grade NAT, where router forwarding alone
cannot expose the host's port. In that case another player with a reachable
connection must host, or the host must arrange a public address with their ISP.
There is deliberately no relay fallback.

Each player's usual keyboard/controller bindings apply to their assigned port.
F1 opens settings; settings suppress that player's input while the game continues.
The game still chooses human/CPU players through its own menus.

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

## Transport

The host-to-guest connections use reliable ordered TCP streams through
libdatachannel's WebSocket implementation. Gameplay stays on the host and guest
computers. TCP retransmission may cause visible stalls on lossy links; input
netplay waits rather than inventing missing inputs. Three frames of buffering
let inputs travel ahead of the frame that consumes them.

Messages are authenticated and encrypted with OpenSSL AES-256-GCM. The random
256-bit key is embedded in the invitation, never sent in the connection handshake.
Fresh client/host nonces and HKDF-SHA256 derive separate directional keys for
each connection. Authenticated sequence numbers reject replays and reordered
records. Authentication must succeed before a guest receives a slot, save or
input stream. Connections, message sizes, queued work and pending frames are
bounded. This is a friends-only room, not an internet matchmaking service.
Anyone given the full code can join an unstarted room; share it privately.

The host address is visible to everyone with the code. Starting a new room changes
the key, but doesn't change the host's address. Diagnostic logs contain connection
codes; keep them private. Session codes are never used as save-directory names.

## Build and test

`tools/bootstrap.py` fetches pinned libdatachannel source and submodules.
OpenSSL development files are required alongside existing build dependencies.
The installer SDK and CI recipes include the new source and OpenSSL.
After changing recompiler hooks in `mp2.toml`, regenerate game code locally.
The transport tests don't need a ROM or Vulkan:

```sh
python tools/bootstrap.py
cmake -S . -B build/netplay -G Ninja -DMP2_NETPLAY_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/netplay --target netplay_test -j 4
./build/netplay/netplay_test
```

These tests run real direct socket connections to a local host. They check
four-controller input ownership/order under asymmetric scheduling, common
seed/save state, injected desynchronization, bad invitation keys, full rooms,
late joins, slot reuse and disconnects. Cryptographic checks reject tampering,
replays, wrong keys and reflected records. No external service is contacted.

Actual-game tests need a local ROM and full build, plus Python 3.10+ (stdlib only):

```sh
python tools/test_game_netplay.py --frames 1200
```

This acquires a machine-wide test lease, starts four isolated game processes,
supplies scripted four-port input, and compares private traces. Its children are
cleaned up on failure. No offline saves are modified. `--save PATH` supplies a
copy of an existing EEPROM for initialized-save testing. Logs and memory-derived
checkpoints stay under ignored `build/`. The default local test port is 17777;
`--port` changes it.

Locally verified on November 9, 2026: four actual Linux game processes completed
2,400 matching input frames, RNG states and canonical gameplay checkpoints using
an initialized EEPROM; the native room window completed create, join, copy-code
and start with two actual clients. Transport/authentication tests and the existing
settings, installer and native-fiber checks pass.

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
checks don't establish that every board or minigame works. Separate home
networks, Linux/Windows cross-play, complete boards/minigames, long sessions and
hostile-network soak testing still need validation. Rollback isn't implemented.

Dependencies: [libdatachannel](https://github.com/paullouisageneau/libdatachannel)
(MPL-2.0) and [OpenSSL](https://www.openssl.org/) retain their respective licenses.
