# Standalone Steam networking helper

`mp2recomp-steam-helper` is an independent MIT-licensed program providing
reliable Steam P2P text and byte streams. It can be built without the game, a
ROM, SDL, N64ModernRuntime, or other emulator components. Only this executable
links Valve's proprietary runtime. The GPL game launches it on demand and
communicates through pipes; no game objects or memory are shared.

The v0.2.2 installer includes the compiled helper and the appropriate runtime
library. Players need Steam running and signed in, but no Steamworks SDK.
The graphical game starts it asynchronously to receive invitations during play;
offline and Direct TCP play still work without it. Steam networking and the
friend/invite UI share one helper. This alpha uses App ID 480.

## Generic stream protocol, version 2

Run `mp2recomp-steam-helper --stdio`. Standard input receives commands; standard
output emits events. Diagnostics use standard error. Every record consists of
a 32-bit little-endian payload length, one ASCII kind byte, a 32-bit little-endian
connection ID, and the payload bytes. Payloads are limited to 16,408 bytes.

| Kind | Direction | Meaning |
| --- | --- | --- |
| H | Helper → client | Startup payload `2:STEAM_ID`; initialization succeeded |
| L | Both | Listen for connections; reply acknowledges the listener |
| C | Client → helper | Connect; payload is the remote Steam ID |
| A | Helper → client | Accepted connection with a helper-assigned ID |
| O | Helper → client | Connection opened |
| T / B | Both | Text / arbitrary byte message for a connection |
| X | Both | Close or connection-closed event |
| E | Helper → client | Fatal error text; service exits |
| U | Both | Close listener and all connections; echo connection ID as reset acknowledgement |
| f | Both | Request friends currently playing App ID 480 / start of refreshed list |
| g | Helper → client | Friend record: `STEAM_ID\nDISPLAY_NAME` |
| p | Both | Publish opaque connect data (empty clears private invite lobby) / ready flag `1` or empty |
| i | Client → helper | Invite eligible friend; payload is their Steam ID |
| v | Helper → client | Incoming lobby invitation: `LOBBY_ID\nDISPLAY_NAME` |
| j | Client → helper | Accept invitation; payload is the lobby ID |
| r | Helper → client | Accepted lobby's opaque connect data |
| s | Helper → client | Nonfatal social operation result or error text |
| Q | Client → helper | Shut down |

Social records use connection ID zero. Display names have control characters
replaced with spaces. Connect data is limited to 512 bytes. The helper uses
`ISteamFriends::GetFriendGamePlayed` and rechecks friendship and App ID 480 at
send time. A private `ISteamMatchmaking` lobby carries opaque connect data;
`LobbyInvite_t` and `GameLobbyJoinRequested_t` notify the client, which explicitly
accepts via `j`. No lobby is joined merely by receiving an invite. `LobbyEnter_t`
provides connect data after acceptance; the temporary guest membership is then
released. A social failure does not terminate an active network session.
Version 2 requires a matching helper; version 1 helpers are rejected clearly.

Outgoing connection IDs are 1 through 0x7fffffff; accepted IDs start at
0x80000000. The service limits connections and queues. There are no controller,
ROM, save, emulator, or other game-specific fields in this protocol. EOF closes
the service. `--self-test` checks protocol framing without starting Steam.

## Licensing and build inputs

See [LICENSE-STEAM-HELPER](../LICENSE-STEAM-HELPER) for authored MIT files and
[Valve's notice](../steam-helper/STEAMWORKS-NOTICE.txt) for the proprietary runtime.
The game and installer retain GPL-3.0-or-later; upstream licenses are unchanged.
Only designated Valve runtime files are bundled. SDK headers are used privately
to compile the helper and never enter the public dependency pack.

Steamworks references: [friends](https://partner.steamgames.com/doc/api/ISteamFriends),
[matchmaking and lobby callbacks](https://partner.steamgames.com/doc/api/ISteamMatchmaking).
