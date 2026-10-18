# The connection of the client

This document has the client side of each contract with the Soulwarden backend. The specification is the Soulwarden
protocol notes (`~/Developer/azoth/docs/PROTOCOL_REVERSE_ENGINEERING_NOTES.md`, sections 1 to 4) and the backend
source (`apps/backend/src/server`). This project does not change the backend.

## The runs

| Run | Script | Data folder | Identity | Started by |
| --- | --- | --- | --- | --- |
| Player run | `tools/play.sh` | the data folder of the user (`SWC_PLAYER_DATA`, the same characters as the retail game) | the Steam account of the user | the user only |
| Development run | `tools/dev-run.sh` | `SWC_DEV_DATA` (`~/Games/soulwarden-client/dev-data`) | a made-up identity and a seed character | agents |

Both runs use `tools/lib/session.sh`:

```text
unshare --user --map-root-user --net          one network namespace; the script brings up lo
  check: the namespace has only the interface lo, else no start
  flock DATA/run.lock                         the lock of the Soulwarden launcher (held until the end)
  backend copy: --data-dir DATA --gamedata-dir backend/current/gamedata --http-host 127.0.0.1 --http-port 32200
                [--seed-character NAME]      no identity of the client, no switch of the Soulwarden harness
  wait for GET /healthz
  unshare --user --map-user=UID --map-group=GID   the client as the user (the engine refuses to run as root)
    env without the proxy variables: CLIENT
  at the end: POST /v1/shutdown (no service host), then SIGTERM, then SIGKILL
```

- The status of the script is the status of the client. Status 3: the lock is held. Status 4: the backend did not
  start.
- The backend of a run is the release copy `~/Games/soulwarden-client/backend/current` (`tools/backend-update.sh`).
- No process of this project has `SOULWARDEN_DEV_RUN=1`: each script removes it from its environment.

### The player run

`tools/play.sh [--check] [-- CLIENT ARGUMENTS]` checks, in this order, and starts only when each check passes:

1. The tools: `unshare`, `ip`, `flock`, `curl`, `awk`, `python3`.
2. The backend copy, and its version (`backend/current/release.json`) is equal to the current release of the user
   (`~/Games/soulwarden/releases/current/VERSION`). The database has a schema version: a different backend can make the
   database unusable for the retail game. A difference names `tools/backend-update.sh`.
3. The client: the engine and the module of the project (`tools/build.sh editor`).
4. The Steam account: the Steam id and the persona name of the most recent Steam login
   (`SWC_STEAM_DIR/config/loginusers.vdf`: the block with `MostRecent` 1, else the largest `Timestamp`; the rule of
   the Soulwarden launcher). Only these two values leave the file. They go to the client in its environment
   (`SWC_STEAM_ID`, `SWC_PERSONA_NAME`), never into a file. A message shows only the last 4 digits of the Steam id.
   `SWC_STEAM_ID=<17 digits>` selects a different account, `SWC_STEAM_ID=local` the fallback identity.
5. The data folder of the user, and the state of `run.lock` from `/proc/locks`: the check takes no lock.

`--check` starts nothing. Status 0: a start can begin now. The client of a player run logs the character list and does
not enter the world; `tools/play.sh -- -SwcPlay=first` enters the world with the first character. The client cannot
move or fight yet (stages 1.7 and milestone 3): a creature near the character can attack it while it stands. A player run holds `run.lock` of the data folder of the
user, as the Soulwarden launcher does: the retail game and a player run do not run at the same time, and a Soulwarden
release waits (status 3) while one of them runs. The client log goes to `~/Games/soulwarden-client/logs/play/`. In a
player run the client log has no character name (`SWC_RUN=player`).

### The development run

`tools/dev-run.sh [--window] [--enter [NAME]] [--create SPEC]... [--seed NAME] [--quit-when characters|world] [--stay S] [--seconds N] [--label TEXT] [-- CLIENT ARGUMENTS]`

- The data folder is `SWC_DEV_DATA`. The script refuses a value that is the data folder of the user. Each run clears
  `dev-data/logs/backend.log`; the database stays (the seed character and its position).
- The identity is made up: the Steam id `76561202255233000` (the range of the made-up ids of the backend tests) and the
  persona name `Swc Dev`. The backend makes the account at the first token request and gives it the seed character
  (default `Devwarden`).
- Default: the client with no window and no sound (`UnrealEditor-Cmd ... -game -nullrhi -nosound`), in the
  foreground, `--seconds` at most (default 120). Logs: `~/Games/soulwarden-client/logs/dev-run/LABEL-client.log` and
  `LABEL-console.log`, and the backend log `dev-data/logs/backend.log`.
- `--window`: the client window (`UnrealEditor ... -game -windowed -nosound`) in gamescope on the agent desktop
  `soulwarden-client-dev` (`omarchy-agent-desktop`), never on the desktop of the user. gamescope runs outside the
  namespace: in a user namespace it refuses `/tmp/.X11-unix`, which the real root owns. The client in the namespace
  reaches the X server of gamescope through its socket file. The script returns when the window shows.
- `--enter`: the world entry with the first character after the character list (`-SwcPlay=first`); `--enter NAME`
  with the character NAME.
- `--create SPEC`: before the client starts, the create request of the retail client (protocol notes 2.2.3) makes a
  character of the development account (`tools/lib/dev_character.py`; a name that is in use is not an error). With
  `SWC_SESSION_BACKEND_ARGS=--dev-commands`, `gear=ITEM@SLOT+...` gives and equips items ([GEAR.md](GEAR.md)), and
  `place=X:Y:Z` sets the saved place of the character (engine meters; `/v1/dev/world/travel` of a character that is not
  in the world).
- Each process of a development run has the marks `SWC_DEV_RUN=1` and `SWC_SESSION_DATA` (its data folder).
  `tools/dev-stop.sh` stops the processes on the own `SWC_DEV_DATA` (SIGTERM, then SIGKILL after 15 s); the runs of the
  other tracks continue. `--all` stops the runs of all tracks. `tools/dev-stop.sh --check` has the status 1 while one
  runs on the own data folder (with `--all`: on any data folder).

### The derived data cache of the runs

The network namespace of a run has loopback only, thus the client cannot reach the Zen cache server of the host
(`[::1]:8558`): the engine logs `ZenLocal: Unable to reach ZenServer` and turns that store off at its first check. There is
no bridge of that port into the namespace (decision of the orchestrator, 2026-10-18). The client then uses the local file
store of the engine, `~/.config/Epic/UnrealEngine/Common/DerivedDataCache` (`Local: ... Writable`). All runs (the
development runs of each track and the player run) use it: the same user, the same file store.

Measurement (2026-10-18, two window runs one after the other, the same character and level): the file store had 9.3 GB
in 117,630 files; the second run wrote 16 new files in its time (the derived data came from the cache). The engine
initialization took 56.6 s and 60.3 s; about 36 s of it is the start of the modules of the editor binary before the shader
compile manager (`-game` with the uncooked content), not the cache. A window run gave 8 to 11 frames in a second under the
load of that time (three runs and an import). Thus a run does not build its derived data again.

One rule to know: the import commandlets (`tools/import.sh`, `tools/import-player.sh`, outside the namespace) use the Zen
store. Thus the first run after an import builds the derived data of the new assets one time into the file store (textures,
mesh data, shaders); the runs after it read it from the cache.

## The command line of the client

The run scripts give these values. The editor does not connect.

| Value | Effect |
| --- | --- |
| `-SwcConnect` | The login path starts with the game. |
| `-SwcHttp=HOST:PORT` | The HTTP listener of the backend (default `127.0.0.1:32200`). A host that is not loopback is replaced by `127.0.0.1`. |
| `-SwcPlay=first`, `-SwcPlay=ID`, `-SwcPlay=NAME` | After the character list: the world entry with the first character, the character ID, or (development runs) the character NAME. |
| `-SwcView=front` | The camera stands 3 m in front of the character (pictures). |
| `-SwcDrive=FILE` | A drive script of the player controller: the keys and the mouse through the input actions ([MOVEMENT.md](MOVEMENT.md)). |
| `-SwcQuitWhen=characters` | The client stops after the character list (status 0) or after a failure of the login path (status 1). |
| `-SwcQuitWhen=world` | The client stays `-SwcStay=SECONDS` in the world (default 10), leaves (the carrier disconnect), and stops (status 0). A session that ends before: status 1. |
| `-SwcQuitAfter=SECONDS` | The client stops after this time (status 0). |
| environment `SWC_STEAM_ID`, `SWC_PERSONA_NAME` | The values of the Steam stubs. |
| environment `SWC_RUN` | `development`: the log has the character names. Each other value: no character name in the log. |

## The Steam stubs (protocol notes 2.1)

`Net/SwcSteamStub` replaces the three Steam functions of the retail client:

| Steam function | Stub |
| --- | --- |
| `ISteamUser::GetSteamID` | `SWC_STEAM_ID` (17 digits, high 32 bits `0x01100001`) |
| `ISteamFriends::GetPersonaName` | `SWC_PERSONA_NAME` |
| `ISteamUser::GetAuthTicketForWebApi` | the local ticket of the Soulwarden game hook, as lower-case hexadecimal text |

```text
ticket      := "SOULWARDEN-LOCAL-TICKET" ";" "1" ";" steam-id ";" persona-hex
persona-hex := lower-case hexadecimal text of the UTF-8 persona name (128 bytes at most)
```

The backend does not check the ticket (stub authentication; it listens only on loopback). With no Steam id in the
environment the client uses the fallback identity of the hook: the Steam id `76561197970884513` (`0x0110000100A207A1`)
and the name `Soulwarden`.

## The HTTP login path (protocol notes 1 and 2)

`Net/SwcLoginClient` sends the requests of the retail client in their order. Each request goes to
`http://127.0.0.1:32200` with the header `X-Soulwarden-Host: <service host>` and `X-Soulwarden-Scheme: https`: the
backend finds the route of a request by this host, as for a request of the retail client through the game hook.

| Step | Request | Service host | Headers and body | What the client keeps |
| ---: | --- | --- | --- | --- |
| 1 | `GET /STEAM_APP_ID.1063730.json` | `soulwarden-channel.cloudfront.net` (the backend route takes each host with `cloudfront.net`) | none | the hosts of `publicApis[]` by `clientTag` (`JavelinGatewayServiceV2`, `JavelinGatewayService-CF`), and `omniTokenUrl` of `platformMetadata` |
| 2 | `POST /games/new-world/tokens` | the token service of step 1 (default `tokenservice.amazongames.com`) | `{"platformIdentityType":"steam","platformAuth":{"ticket":HEX},"fallbackToken":"","attributionData":{}}` | `accessToken`, `account.personaId`, `account.identityId` |
| 3 | `GET /prod/credentials/omni` | `JavelinGatewayService-CF` | `authorization: Bearer <accessToken>` | a reply 200 with `accessKeyId` |
| 4 | `POST /players/{personaId}/games/new-world/platforms/steam/entitlements/sync` | `client.entitlementservice.amazongames.com` | `x-amzn-token`, `x-amzn-playerid` (the Steam id); `{"syncTypes":[...5 types]}` | nothing (a failure does not stop the path) |
| 5 | `GET /players/{personaId}/games/new-world/platforms/steam/entitlements` | the same | the same headers | the aliases of `entitlements[]` |
| 6 | `GET /applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless` | `ags-javelin-remote-config.s3.amazonaws.com` (exact) | none | each member with a JSON value `true` or `false` (`javelin.enable-client-authoritative-melee`); a 404 leaves the defaults |
| 7 | `GET /prod/game/getlogininfo?channelId=STEAM_APP_ID.1063730` | `JavelinGatewayServiceV2` | `x-nw-auth: <accessToken>` | `LoginInfoList.Worlds[]` and `LoginInfoList.Characters[]` (`CharacterId`, `Name`, `WorldId`, `FtueCompleted`, `PublishedData`) |
| 8 | `POST /prod/game/login/queue/v2?tokenVersion=10` | `JavelinGatewayServiceV2` | `x-nw-auth`; `content-type: application/x-amz-json-1.1`; `{"LoginQueueRequest":{"CharacterId":ID,"WorldId":"","SteamAuthTicket":HEX}}` | `LoginQueueResponse.Token`: the login token of the registration (stage 1.5) |

- The order of steps 4 and 5 is proved: the retail client sends the sync and then the list, after the token and before
  the character list (protocol notes 4.29). The retail client asks for 12 remote configuration documents at its start;
  the backend has only the document of step 6, and the client asks only for it.
- The reply limit is 10 s for each request (protocol notes 2.2.3).
- A log line has the method, the service host, the path, the status, and the size of the reply: no token, no query, no
  Steam id (only its last 4 digits), and no persona name.
- The retail client checks each world (`WorldStatus` `ACTIVE`, `WorldType` `OpenWorld`, the client version in
  `WorldVersion`). The client logs the values; the backend has one world.

Exit check of stage 1.4 (development run, 2026-10-18): the 7 requests of the path in their order, each with the status
200 and the route name of the backend (`channel.document`, `omni.create_token`, `gateway.omni_credentials`,
`entitlements.sync`, `entitlements.list`, `aws.s3_gameplay_configuration`, `gateway.get_login_info`), and the client log
has the seed character of the account.

## The game server (protocol notes 3 and 4)

The code has two layers:

| Layer | Path | Job |
| --- | --- | --- |
| Codec and session | `Net/Codec/` (plain C++, no engine header) | `SwcCarrier` (the carrier), `SwcRepMessages` (the frames, the registration, the readers of the world entry messages, the input report writer), `SwcRepClient` (the session: the phases, the own entity, the input report, the timers). The caller gives the time and the clear datagrams. |
| Vendored readers | `Net/Vendor/soulwarden/` | Unchanged copies of the wire codec of the backend (`server/hub/wire`) and of the state reader of the backend tests (`tests/support/state_decoder.h` and its three helpers). A fragment of a record has no length: the client must read each state type that the backend sends, to its end. `NOTICE.md` has the update rule. |
| Transport | `Net/SwcRepConnection` | One thread: the UDP socket on loopback, DTLS 1.2, and the session. The thread keeps the carrier alive also while the game thread loads a level. |
| Glue | `USwcBackendSubsystem`, `Net/SwcTypeIndex`, `Net/SwcSpace.h` | The login token, the certificate, the type index check, the log lines, the view of the own entity. |

### The order of the world entry

```text
POST /prod/game/login/queue/v2            the login token: RepAddress 127.0.0.1:32210 (loopback only, else no start)
GET  /v1/tls/ca.pem                       the certificate of the backend (management route: no service host)
check typeindex.json of the game copy     the 14 types of the client must have the indexes of the backend
UDP socket 127.0.0.1:any -> RepAddress    DTLS 1.2 handshake (cookie exchange of the backend), 10 s at most
carrier connect request (system 1, version 5), again each 250 ms until the connect ack (system 2)
    WaitingForREPConnection -> WaitingForActorGameConnection
RegistrationRequestV3Msg (19), reliable, channel 0
<- RegistrationResponseMsg (3), result 0
<- PlayerManagerSelfIdentificationMsg (1628)          -> WaitingForSpawnPoint
<- LevelInfoChangedMsg (1635): level, terrain, instance, reload, version
<- ReceivePlayerSpawnPointMsg (1617)
<- ReplicatedStateBundle (8), sequence 1: the level load -> WaitingForPlayerSpawn
<- the record of interest 1 with the character id of the login -> InGame
-> ActionListComponentServerFacet_ReceiveInputFromClient (1589), unreliable, channel 1, 30 each second
<- a movement state each 200 ms (a new time anchor and the id 0x8000 or 0x0000)
...
-> carrier disconnect (system 3, reason 0) at the leave; no DTLS close notice
```

### DTLS (protocol notes 3.1)

- The OpenSSL of the engine (1.1.1t, module `OpenSSL`; no GPL code). `DTLS_client_method` with DTLS 1.2 only, and the
  cipher suite `ECDHE-RSA-AES256-GCM-SHA384` (`TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384`, the cipher of the retail
  client).
- The trust store has only the certificate of the backend (`/v1/tls/ca.pem`, a self-signed RSA 2048 authority,
  `CN=Soulwarden Local`): the client checks the certificate chain (`SSL_VERIFY_PEER`), not a host name. The game hook
  does the same for the retail client: it replaces the embedded authority with the certificate of the backend.
- One DTLS record is one carrier datagram. The link MTU is 1,500 (a datagram of 1,038 bytes and the record overhead).

### The carrier (protocol notes 3.2)

`swc::net::Carrier` has the rules of the backend carrier in the two directions: the datagram header, the message
header with its compression flags, the chunks (1,024 bytes of data at most for one message, 1,038 bytes for one
datagram), the order of the reliable messages, an unreliable message that waits for the reliable message before it,
the acknowledgements (one number, a continuous range, or a bit list), the resend after 250 ms, the acknowledgement after
40 ms, and the keep-alive after 100 ms. The backend sends no compressed datagram; the client logs one and drops it.

### Frames and the type index (protocol notes 3.3)

```text
client -> server := crc32:u32 size:u32 payload        payload := route:16 bytes, envelope
server -> client := size envelope                     (one or more in one carrier message)
envelope := flags:u8 present:u8 type:size body        (the client writes flags 0; it reads the flags 0x01 and 0x02)
```

The route of a component call is the actor id of the entity (the input report: the own actor id). The registration
has the route of 16 zero bytes (the backend reads no route for it). `Net/SwcTypeIndex` reads `typeindex.json` of the game
copy (`SOULWARDEN_GAME_DIR`, read only; 7,052 type ids) and checks the type id at the index of each type of the client
(`swc::net::kUsedTypes`: 10 message types and 10 state types). A difference stops the world entry: the build of the game
copy is not the build of the backend (the retail client gets the result -90, `TypeIndexMismatch`).

### The registration (protocol notes 3.4)

The body has the layout of the made-up client of the backend tests (`tests/support/client_frames.h`): u32 1, the
version map (6 entries, the text `1.400.6031.6004151`), a text, the login token (the values of `LoginQueueResponse.Token`;
`IssueTime` and `GenerateTime` are the low 32 bits of the millisecond values), the client data (the Steam id of the stub,
and `steam|` with the hexadecimal text of the local ticket), two identities in the text form, and one flag. A result
that is not 0 ends the session with the text key of the result (protocol notes 3.6), as do the messages "kicked" (271)
and "rejected" (1630).

### The world entry and the own entity (protocol notes 4.1 to 4.6)

- The client keeps the self identification (the session actor and the player manager), the level information, and the
  rules of the retail client for a state bundle: no bundle before the level information; the instance of each bundle is
  the instance of the level information; the reliable bundles are 1, 2, 3, ... with no gap (after a gap the client drops
  each later bundle); the stops of the control block remove an entity.
- The record of interest 1 is the own entity: the metadata state (the player slice `A660EEEB-...`, sub id 2, and the
  actor id; `G` is its first 8 bytes, little-endian), the player state (character id, name, connected), the movement
  state (`worldPosAbs`, `rotation`, `lookDir`, `teleportAndMigrationId`, `timeAnchor`, `dataBits`, `moreDataBits`, the
  id), the vitals state, the appearance state (12 index bytes), and the paperdoll state (the owner view: the two pairs of
  parallel lists of slots and items; the own entity keeps paperdoll slot -> item index, and a slot with no entry keeps its
  item). The entity is the local player only when its character id is the character id of the login: then the phase is
  InGame. A change of the appearance or of the paperdoll builds the body again ([CHARACTER.md](CHARACTER.md)).
- The records of the other interest numbers (the creatures, the objects, the spells) keep their metadata and position
  for the stages 1.9 and later.
  With `RepClient::Options::keepOtherRecords` the session also keeps each such record and each stop, in the order of the
  bundles, for `take_other_updates()`; `USwcBackendSubsystem::TakeOtherUpdates` gives them to the entity table of the
  creatures ([CREATURES.md](CREATURES.md)).
- A fragment of a state type that the reader does not know stops the reader for the rest of that bundle; the client
  logs the type one time. The live runs of this stage had none.

### The read path of the own entity (the HUD)

`USwcBackendSubsystem::GetLocalPlayer(swc::net::RepClient::OwnEntity&)` gives a copy of the decoded own entity while the
client is in the world (InGame). `GetServerTimeMicroseconds()` gives the server clock now, and `GetCameraYawDegrees()` the
yaw of the camera of the player (0 = north, positive = to the left, the convention of the input report). Each value of
`OwnEntity` and its source (backend encoders `apps/backend/src/server/rep/states`, protocol notes):

| Field of `OwnEntity` | State (slot, type), group and field | Unit, notes |
| --- | --- | --- |
| `health`, `healthMax` | vitals (41, 15), group 0 fields 0 and 4 | f32 |
| `vitalsStamina`, `vitalsStaminaMax` | vitals, group 0 fields 1 and 6 | f32 (the values before a stamina state) |
| `vitalsMana`, `vitalsManaMax` | vitals, group 1 fields 0 and 3 | f32 |
| `dead` | vitals, group 0 field 2 `vitalsData`, bit 0 of its first byte | the character is dead (client getter 0x684AC70) |
| `hasStamina`, `stamina`, `staminaMax` | stamina (42, 4297), group 0 fields 0 and 1 | f32; the copy of the server: the retail client predicts its stamina itself |
| `hasMana`, `mana`, `manaMax` | mana (47, 1652), group 0 fields 0 and 1 | f32; the mana meter of the retail HUD reads this component, and it divides the current value by the maximum of the client (the first field 1 plus the MaxMana parts of the attributes): the current value can be larger than field 1 (protocol notes 4.5) |
| `abilityBars` | ability (46, 185), group 0 field 0 `persistentAbilityData` | for each ability table, in the sequence of the field: the table id, and for each place the ability id (u32, the CRC-32 of the lower-case id text) and its button number |
| `cooldowns[0..2]` | cooldown timers (45, 2932), group 0 fields 0 to 2 | the map of place N of the bars: ability id, end, minimum end, in microseconds of the server clock; an indefinite cooldown has the end 0xFFFFFFFFFFFFFFFF and the minimum end 0 |
| `itemCooldowns` | cooldown timers, group 0 field 3 (ccdmap) | the CRC-32 of the lower-case item id, the end in microseconds |
| `equipment` | paperdoll (62, 3183), group 1 fields 1 to 3 and group 2 field 3 | paperdoll slot -> item index on the wire (0: empty); the item with index N is entry N - 1 of the item catalog |
| `sheatheMap`, `activeMap` | paperdoll, group 2 fields 0 and 2 | bit N (byte N / 8, bit N % 8 of the 7 bytes) is paperdoll slot N |
| `appearance`, `position`, `rotation`, `name`, `characterId`, `actor` | appearance, movement, player, metadata | as above |

The server clock: the registration reply gives the first value; `REPClient::TimeSynchMsg` (type 335, serverTime:u64 in
nanoseconds, each 3 s) corrects it: a difference of more than 1 s at once, a smaller one with a slow correction of 2 ms for
each tick of 10 ms (protocol notes 4.43, live L8: +0.5 s slow, +5 s at once; the limit of 1 s is an inference). The
cooldown ends and `server_time_ns` / 1000 use the same clock: the time that is left is `end - GetServerTimeMicroseconds()`.

### The input report (protocol notes 4.7)

From InGame the client sends one report each 1/30 s on channel 1: the newest entry and the 4 entries before it (as a
live client). The body is `target:u64 entity:u64 header count entry*` with the target of the action list facet:
`G + 0x3C591A4EA3C2AD75` (the component id of the action list of the entity `RootPlayer` in `slice-ids.json`; the backend
log names the facet). Each entry has the values of a client that stands (the backend tests and the captures): the flags
`0x20004F0`, the permissions 15, the ground value 180, 19 action words with the bits 475, 497, 506, 569, the step time
1/30 s.

With the player of the game thread (stage 1.7, `RepClient::set_motion`; [MOVEMENT.md](MOVEMENT.md)) each entry has:

- the flags `0x20084F0` (with `0x8000`) and the position field: the feet of the character after the movement of the
  last game frame, as the retail client with the game hook of Soulwarden sends it (the height in the 16 bits of the
  wire, cut as the client cuts it). The field goes to the feet at 10 m/s at most in each step (up 10 m/s, down
  60 m/s; a distance of more than 20 m, a travel, goes at once): at a low frame rate a frame has the move of more
  than one step, and the backend refuses a step above 20 m/s (`world/position_report.h`). These speeds are decisions
  of an agent, below the limits of the backend; after a long frame the field follows the feet some steps later;
- the camera position of the camera of the player, and the look (mask bit 6): the yaw in radians from north to the
  left, the pitch in radians, positive down;
- the action bits 14, 43, 416, 542 of the W key while it is down (the backend reads bit 14), with the rest bits; the
  bits of a weapon key in one entry: 9 and 40 (key 1), 355 and 306 (key 2) (protocol notes 4.15; [GEAR.md](GEAR.md));
- the attack input (stage P2.7, [ACTIONS.md](ACTIONS.md)): the press bits 555, 203, and 408, the hold bit 191 at each
  step after the press while the button is down, the release bit 567. The attack driver adds them at the thread of the
  session (`RepClient::Options::inputStep`, called for each entry with its input number) and steps the attack graph at
  the same input numbers;
- list 3, the melee contacts (stage P2.8, [ACTIONS.md](ACTIONS.md)): each item 96 bytes (`write_melee_contact`: the
  provider `0xFFFFFFFF`, the group reference, the receiver reference with its facet target, the point, the normal, and
  the direction in engine space). The contacts of a frame go into the next entry only (`RepClient::queue_contacts`;
  `tools/test-net.sh` reads them with the reader of the backend);
- in the air, the ground value: the half float of the distance to the ground (10 m at most; an inference from the
  protocol notes "0 to 10; 10 in a fall").

The backend log then has `position source changed: the position field of the input reports, the exact position`, and
each report goes through its check (the input number, the numbers, the speed). With no player (before the character
exists, and in a player run on the test floor) the entry has the camera of a character at rest at the start of a
session: 0.5 m east, 4.0 m south, and 1.65 m above the position of the own entity, north, no pitch, and no position
field: the backend then uses its camera estimate, and the camera axis goes through the known position, thus the
position of the character does not change.

### The life, the travel, and the interactions (stage P2.10; protocol notes 4.16, 4.18, 4.20, 4.35, 4.36)

The component calls of the root entity of the player slice: `target:u64 entity:u64 body`, the target `G + the component id`
(a 64-bit sum, the carry dropped: Player `DF0CE8C98D27619D`, PlayerCutscene `D49931510E1DD23F`, Interactor
`6C5718DBFE6DF612`). The client sends its calls on channel 0 (reliable) with the route of the own actor.

| Type | Direction | Body | The client |
| --- | --- | --- | --- |
| 3950 OnPlayerDeath | server | `time:f32` (5 s) | `deathNotices`, `respawnScreenTime`, the steady time of the notice; the debug text "YOU DIED: the respawn screen in N s" |
| 1938 RequestRespawn | client | `random:u8 id:text`: 18 bytes, random 1 and no id ("Random Location") | `RepClient::request_respawn`: only for a dead character after the time of the notice; `loadingScreen` until 3954 |
| 3951 OnTeleportWithLoadingScreen | server | `kind:u8` | `loadingScreen` |
| 3954 OnPlayerTeleport | server | none | the loading screen ends |
| 3746 CancelCurrentInteraction | server | none | the client cancels its committed interaction: the block goes away and 2473 goes out at once (0x490EDA0, 0x4916150) |
| 6709 OnServerCutsceneEndRequestFinalized | server | none | a count (the end of a lock of the input; the client has no cutscene yet) |
| 2473 RequestCancelCommittedInteractionFromClient | client | the reference: `actor:16 bytes G:u64 target:u64` (48 bytes) | `RepClient::cancel_interaction` |
| 3743 ReceiveInteractionResult | server | the reference, `action:u8 result:u8` and 21 bytes (71 bytes) | for the current request only: result 1 accepted; result 2 clears the request (no 2473) |

- The travel: the client takes the place of a movement state of the server only when the low 4 bits of field 31 are not
  its own teleport number (`takes_travel`; the start number 0, the world entry 1, then `number % 15 + 1`). Each such state
  is one more `travelRevision`; the subsystem moves the character there after the cells of the new place are loaded (with
  no position field in that time), north, with the camera north ([MOVEMENT.md](MOVEMENT.md)). A place with no ground gets
  the test floor.
- The respawn of the backend: 3746, 6709, the movement state with the next teleport number and the vitals state with the
  dead mark clear, then 3954 (no 3951).
- The interaction block (mask bit 8 of an input entry, after the target data): the request of the player while it is
  current (`RepClient::set_interaction`), 131 bytes with the hold time, empty texts, and no vectors; an older entry with no
  request after a newer one with a request has bit 8 with the present byte 0 (else the reader of the backend gives it the
  block of the newer entry). [INTERACTIONS.md](INTERACTIONS.md) has the rules of the use.
- The loot of the owner (protocol notes 4.10, 4.41, 4.42) in the read path (`RepClient::OwnEntity`, each with a count of its
  changes): the bag of the character (the Container of slot 61: the full vector and the journal), the LootTracker (slot 33:
  the 58-byte claims, Rolled 1, Looted 2), the selected Container (slot 76: the open bag), and the GlobalStorage map (slot 77: the
  items of each bag). The take of the open bag: 2939 TakeAllItems (48 bytes) and 2942 TakeItem (68 bytes: the instance, the
  quantity), with the target `G + 5E6EA4CE9484F842` (PlayerTrade) and the reference of the own GlobalStorage Container: the own
  actor, G, and `((00006914A15737B2 XOR DAEE4E2555650AB7 XOR G) + 476865E50327A1C7)` (`slice-ids.json`), not the bag.
- The objects of the server (a fixture, a loot bag): the client keeps the place of their PositionInTheWorld state (type 13,
  field 0: `x:f32 y:f32 height:u16` with the height range -100 m to 2000 m, not the range of a movement state) in the
  snapshot (`FSnapshot::Objects`).

### The timers of the client contract

| Timer | Rule of the retail client | The client |
| --- | --- | --- |
| 30 s | `connection_ingame_timeout`: no game message for 30 s ends the connection (protocol notes 4.1) | The session ends with this cause and sends the carrier disconnect. The thread measures the time: a game thread that loads a level does not stop it. |
| 2 s | `javelin.alc-server-timeout-for-input`: with no movement state for 2 s the client clears the actions of each new input (protocol notes 4.6) | The entries have no action bits while the last movement state of the own entity is older than 2 s; the log has one line at each change. |

### The view

`Net/SwcSpace.h` maps a position of the wire to Unreal and back (`SwcEngineToUnreal`, `SwcUnrealToEngine`; proved by
stage 1.1). At InGame the client puts the character of the user ([CHARACTER.md](CHARACTER.md)) at the position of the own
entity with the facing of its movement state, and the player controller possesses it ([MOVEMENT.md](MOVEMENT.md)). The
snapshot of the connection has the other entities with a position (`Others`: the interest number and the position); the
target lock of the camera takes its targets from them ([CAMERA.md](CAMERA.md)).

### The tests (`tools/test-net.sh`)

`tests/net/codec_tests.cpp` builds with the system compiler (no engine) and uses only bytes that the backend makes:

- The golden session: the 16 server datagrams of case T2 of the backend tests
  (`apps/backend/tests/golden/one_session.hex`) go to a `RepClient`: it reaches InGame with the character id, the name,
  the actor id, `G`, the slice asset, the spawn position, the rotation, the permissions, the heartbeat id 0x8000, the
  vitals, and the appearance of the scenario; each bundle is read to its last byte; a carrier of the backend reads each
  datagram of the client (one registration on channel 0, the input reports on channel 1, which the input reader of the
  backend reads: the camera of the spawn, no position field); with a player (`set_motion`) the input reader of the
  backend reads the position field, the camera, the look, the forward key, and the 5 entries of each report; 2 s with
  no movement state clear the actions; 30 s with no game message end the session.
- The writers: the registration body, the client frame, the local ticket, and an input report of three entries are
  equal byte for byte to the writers of the made-up client of the backend tests; the input reader of the backend reads
  the report; the facet target of the action list is the value of the research (`90A8748FA3C2AD77` for serial 1).
- The life, the travel, and the interactions (stage P2.10): the bodies of the backend writers 3950, 3951, 3954, 3746, 6709,
  3743 (`server/rep/health_messages`, `travel_messages`, `lock_messages`) to the readers of the client; the client bodies 1938
  and 2473 to the readers of the backend; the interaction block byte for byte as the block of the made-up client, and a
  report with a request in the newest entry only, read by the input reader of the backend; the teleport numbers of a travel;
  the ids, the focus, and the use of `Interact/SwcInteraction` ([INTERACTIONS.md](INTERACTIONS.md)); the take messages 2939 and
  2942 read by the reader of the backend (the targets of the backend fixture), and the readers of the LootTracker, GlobalStorage,
  and Container values (the full vector, a journal that sets and adds slots, an emptied slot).
- The carrier: the client carrier against the carrier of the backend: the handshake, a reliable message of 3,000 bytes
  in chunks to the server and of 2,500 bytes to the client, an unreliable message, a lost datagram of the server that
  comes again in its order, the 1,038-byte limit, and the disconnect.

Exit check of stage 1.5 (development runs, 2026-10-18): the client enters the world of the unchanged backend (the
release copy `2026-10-17-e0a3d97`) at the place of the character and logs its own entity and position; a session of
180 s had 972 state bundles, 887 movement states, 5,400 input reports (each with a target of the id rule in the
backend), no unknown state type, no resend, and the end cause `carrier close`.
