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

`--check` starts nothing. Status 0: a start can begin now. A player run holds `run.lock` of the data folder of the
user, as the Soulwarden launcher does: the retail game and a player run do not run at the same time, and a Soulwarden
release waits (status 3) while one of them runs. The client log goes to `~/Games/soulwarden-client/logs/play/`. In a
player run the client log has no character name (`SWC_RUN=player`).

### The development run

`tools/dev-run.sh [--window] [--seed NAME] [--quit-when characters] [--seconds N] [--label TEXT] [-- CLIENT ARGUMENTS]`

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
- Each process of a development run has the mark `SWC_DEV_RUN=1`. `tools/dev-stop.sh` stops them (SIGTERM, then
  SIGKILL after 15 s); `tools/dev-stop.sh --check` has the status 1 while one runs.

## The command line of the client

The run scripts give these values. The editor does not connect.

| Value | Effect |
| --- | --- |
| `-SwcConnect` | The login path starts with the game. |
| `-SwcHttp=HOST:PORT` | The HTTP listener of the backend (default `127.0.0.1:32200`). A host that is not loopback is replaced by `127.0.0.1`. |
| `-SwcQuitWhen=characters` | The client stops after the character list (status 0) or after a failure of the login path (status 1). |
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
