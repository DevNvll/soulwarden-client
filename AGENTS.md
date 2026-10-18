# Soulwarden Client

A personal, local project: a new game client in Unreal Engine 5.8 for the Soulwarden backend. It uses the content
of the game copy of the user, converted on this computer. It replaces the retail New World client only in this
project. The Soulwarden project (`~/Developer/azoth`) keeps the retail client and the backend. Both projects run in
parallel.

This file has the rules and the common workflows. The plan is in [docs/ROADMAP.md](docs/ROADMAP.md), the design in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The research that started the project is
`~/Games/soulwarden/work/research/unreal-port.md` (Linear AST-135).

## Decisions of the user (2026-10-18)

- The project is personal. The git repository is local only: no remote, no push.
- The client uses the Soulwarden backend, with no change of the backend. This project builds only the client.
- The look can be different from the retail client for now (the default look of Unreal is acceptable).
- The feel of the gameplay and the UI must match the retail client.
- The client is an open world client: the whole world of the game, streamed. A small area is only a first test place.
- Identity: the Steam account of the user (read at run time, as the Soulwarden launcher reads it). The functions of
  the Steam authentication are stubs: the backend does not check a ticket.
- The client uses the same characters as the retail game: a player run uses the data folder of the user.

The roles (decision of the user, 2026-10-18): the T3 thread of the research AST-135 is the orchestrator of this
project. The Soulwarden orchestrator (thread 02915e32-02d5-4ff1-a796-cc7c5cf30798) does only the backend.

Tracking: the Linear project "Soulwarden Client" (P-AST-7, team Astarteslabs) is the source of truth for the order
and the status of the work: one issue for each stage. A stage thread sets its issue to In Progress at its start and
writes its result as a comment at its end; the orchestrator sets it to Done. `docs/ROADMAP.md` keeps the plan.

Stage threads: each track works in its own git worktree and branch. A stage commits on its branch; when its checks
pass, it rebases its branch on `main` and merges it into `main` with a fast-forward. Two tracks do not edit the same
files at the same time; a shared document (this file, the roadmap) changes only in small, separate commits.

## Rules

### Safety and privacy

- All traffic stays on this computer. The backend listens only on loopback. The client connects only to
  127.0.0.1 (`USwcBackendSubsystem` refuses a different host). A run of the client and its backend uses a network
  namespace with loopback only.
- No Steam id, account name, persona name, or character name of the user goes into a file of the repository. A log
  line shows only the last 4 digits of a Steam id.
- No game content goes into git: no game file, no converted file (glTF, PNG, JSON of the game), no imported asset
  (`unreal/SoulwardenClient/Content/Imported/` is ignored), no text or picture of the game, no game log, no capture.
  Converted content stays in `~/Games/soulwarden-client/content`.
- Two kinds of runs:
  - A player run (`tools/play.sh`, started by the user): the data folder of the user (`~/.local/share/soulwarden`,
    the same characters as the retail game), with the `run.lock` of the Soulwarden launcher. While a player run holds
    the lock, a release of the Soulwarden project waits (status 3), as when the user plays the retail game.
  - A development run (agents): its own data folder (`~/Games/soulwarden-client/dev-data`) with a seed character.
    A development run never opens the data folder of the user.
  - Only a development session can give its backend extra arguments (`SWC_SESSION_BACKEND_ARGS` of
    `tools/lib/session.sh`, for example `--dev-commands` for the development routes; `tools/dev-build.sh`) with the mark
    `SOULWARDEN_HARNESS=1` for the backend process alone; session.sh refuses them for the data folder of the user.
- The backend of a run is the copy of a Soulwarden release in `~/Games/soulwarden-client/backend/current`
  (`tools/backend-update.sh`), never `~/Developer/azoth/apps/backend/dist`: the backend stage threads rebuild that file
  before their live check. Update the copy on purpose, after a release.
- The shared database has a schema version (`PRAGMA user_version`). A backend migrates an older database forward and
  refuses a newer one ("database schema N is newer than this backend"). Thus a player run uses only the backend of
  the current release of the user: `tools/play.sh` compares `backend/current/release.json` with
  `~/Games/soulwarden/releases/current/VERSION` and refuses a different version (run `tools/backend-update.sh`).
  Never give a player run a release that is newer than the current release: then the retail game refuses the
  database. The Soulwarden orchestrator tells this project when a backend commit changes the schema version.
- The Steam account: read only the Steam id and the persona name of the most recent login of the Steam installation
  (`config/loginusers.vdf`, as the Soulwarden launcher does). Never open a Steam credential. The two values go to
  the client in its environment, never into a file; a log line shows only the last 4 digits of the Steam id.
- Parallel tracks: each track uses its own development data folder (`SWC_DEV_DATA=~/Games/soulwarden-client/dev-data-TRACK`)
  and its own agent desktop (`SWC_AGENT_DESKTOP=soulwarden-client-TRACK`), so that the runs of two tracks do not wait
  for one lock. Each run has its own network namespace: the ports do not collide.
- A process of this project never has `SOULWARDEN_DEV_RUN=1` in its environment: `tools/dev-stop.sh` of Soulwarden
  stops such processes, and its `--check` fails while one is alive.
- Do not open the Proton prefixes of the Soulwarden project or its development data.
- Do not start, stop, or control the retail client or a development run of the Soulwarden project.
- The Soulwarden repository is read only for this project. A change that the backend needs goes as a message to the
  Soulwarden orchestrator (thread 02915e32-02d5-4ff1-a796-cc7c5cf30798), never as an edit of `~/Developer/azoth`.
- The Soulwarden orchestrator tells this project when a backend commit changes the wire bytes (the golden file of its
  case T2) or the start options of the backend. Then check the client and update the backend copy.

### The backend contract

- The client speaks the original wire protocol of the backend: the HTTP login path, DTLS 1.2, the carrier, the
  registration, the state bundles, the input report. The specification is the Soulwarden protocol notes
  (`~/Developer/azoth/docs/PROTOCOL_REVERSE_ENGINEERING_NOTES.md`, sections 2 to 4) and the encoders of the backend
  (`apps/backend/src/server/rep/states`). The client decoders follow them byte for byte.
- A client test uses bytes that the backend makes (its test program or a run), never bytes of a capture of the user.

### Feel and UI first

- A value that changes the feel (a speed, an acceleration, a time of an animation, a camera distance, a key) comes
  from the game data or the client data, not from a guess. Write its source next to the value.
- Animations play the original clips with the original timings (the fragments of the Mannequin data).
- The UI uses the layout, the sprites, the fonts, and the texts of the original canvases. The behavior of a screen
  follows the original scripts.
- The check of a feel or a UI result is a side-by-side picture or video with the retail client (taken in a
  development run of the Soulwarden project by its tools, or by the user), plus the opinion of the user.
- Do not spend work on the look (shaders, master materials) until the user asks.

### Work

- Python: run each tool with `python3 -B`. The converter needs only Python 3, numpy, and Pillow (numba only for the
  previews).
- Resources: heavy jobs run with `nice -n 10` and a memory limit (`systemd-run --user --scope -p MemoryMax=8G`).
  The C++ build uses 4 parallel actions (`SWC_BUILD_JOBS`).
- The Unreal editor window opens on the desktop of the user: an agent uses the commandlets (`tools/import.sh`, no
  window) and opens the editor only when the user asks. Never connect to an editor that the user runs.
- Evidence: pictures and videos of each stage in `~/Games/soulwarden-client/evidence/STAGE/`. A movement or an
  animation needs a video.
- Documents use short sentences and simple words (ASD-STE100 Simplified Technical English).
- Commit only when the user asks or a stage is complete and checked. No remote.

## Common workflows

```sh
tools/convert.sh proofs                         # the four proofs of the research (stage P0 check)
tools/convert.sh mesh PATH...                   # static meshes -> glTF (game paths, lower case)
tools/convert.sh character CDF --clip CAF ...   # a character with skins and clips -> glTF
tools/convert.sh region r_+04_+02 --area X Y SIZE --meshes   # terrain, macro map, placements, and their meshes
tools/convert.sh preview region r_+04_+02 --area 8780 4100 300   # a software picture, no Unreal
tools/build.sh editor                           # build the C++ module for the editor
tools/backend-update.sh [RELEASE]               # copy the backend and its game data of a Soulwarden release
tools/import.sh [FILTER] [--force]              # import the converted content into /Game/Imported (no window)
tools/editor.sh                                 # open the editor (only when the user asks)
tools/play.sh --check                           # check the player run, start nothing (only the user starts tools/play.sh)
tools/dev-run.sh --quit-when characters         # a development run with no window: the login path to the character list
tools/dev-run.sh --window                       # a development run with the client window on the agent desktop
tools/dev-run.sh --enter --quit-when world      # a development run that enters the world, stays 10 s, and leaves
tools/test-net.sh                               # the tests of the network codec with bytes of the backend (no engine)
tools/test-actions.sh                           # the tests of the attack graph and its parity with the backend sequence (no engine)
tools/test-movement.sh                          # the tests of the movement rules, the position check, the speed table and the statuses (no engine)
tools/convert-ui.sh hud                         # the canvases, scripts, textures, fonts, texts, and game data of the HUD
tools/test-ui.sh --preview                      # the tests of the UI runtime and the HUD scripts (no engine), a picture
tools/dev-build.sh --preset NAME                # a build preset of the backend for the dev character (--list)
tools/convert-player.sh tables|base|wanted      # the player character: databases, base skeletons, the parts that a run asked for
tools/convert-player.sh movement|roads          # the movement rules of the data, the road maps of the regions (stage P3.3)
tools/import-player.sh                          # import the player base and parts onto one skeleton for each gender
tools/dev-stop.sh                               # stop the development runs on the own SWC_DEV_DATA (--all: of all tracks)
tools/convert-creatures.sh convert --near 8880 4250 300   # the creatures near a place (--all: the world; wanted: the client list)
tools/import.sh creature-                       # import the converted creatures into /SwcGameContent
tools/test-creatures.sh                         # the tests of the creature entity table with states of the backend encoders
tools/creature-check.sh LABEL STAY ROUTES [DRIVE]   # a video check of the creatures with the development routes
tools/convert-objects.sh write                  # the objects of the level data for the object layer (docs/OBJECTS.md; meshes: the depleted node meshes)
```

The runs and the connection: [docs/NETWORK.md](docs/NETWORK.md).

Paths and defaults are in `tools/env.sh` (each one can be set before). The converter writes to
`~/Games/soulwarden-client/content`, the logs go to `~/Games/soulwarden-client/logs`.
