# Soulwarden Client

A personal, local project: a new game client in Unreal Engine 5.8 for the Soulwarden backend. It uses the content
of the game copy of the user, converted on this computer. It replaces the retail New World client only in this
project. The Soulwarden project (`~/Developer/azoth`) keeps the retail client and the backend. Both projects run in
parallel.

This file has the rules and the common workflows. The plan is in [docs/ROADMAP.md](docs/ROADMAP.md), the design in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The research that started the project is
`~/Games/soulwarden/work/research/unreal-port.md` (Linear AST-135).

## Decisions of the user (2026-10-18)

- The project is personal. The repository is public on GitHub (github.com/DevNvll/soulwarden-client, MIT, decision of the
  user, 2026-10-19). Only the orchestrator pushes `main`; the tracks never push. Never commit game data, converted
  content, imported assets, evidence, logs, or a private value. The author of each commit is DevNvll with the GitHub
  noreply address (the config of this repository).
- The client uses the Soulwarden backend, with no change of the backend. This project builds only the client.
- The look can be different from the retail client for now (the default look of Unreal is acceptable).
- The feel of the gameplay and the UI must match the retail client.
- The client is an open world client: the whole world of the game, streamed. A small area is only a first test place.
- Identity: the Steam account of the user (read at run time, as the Soulwarden launcher reads it). The functions of
  the Steam authentication are stubs: the backend does not check a ticket.
- The client uses the same characters as the retail game: a player run uses the data folder of the user.

The roles (decision of the user, 2026-10-18): the T3 thread of the research AST-135 is the orchestrator of this
project. The Soulwarden orchestrator does only the backend.

Tracking: the Linear project "Soulwarden Client" (P-AST-7, team Astarteslabs) is the source of truth for the order
and the status of the work: one issue for each stage. A stage thread sets its issue to In Progress at its start and
writes its result as a comment at its end; the orchestrator sets it to Done. `docs/ROADMAP.md` keeps the plan.

Stage threads: each track works in its own git worktree and branch. A stage commits on its branch; when its checks
pass, it rebases its branch on `main` and merges it into `main` with a fast-forward. Two tracks do not edit the same
files at the same time; a shared document (this file, the roadmap) changes only in small, separate commits.

## Rules

### Safety

- All traffic stays on this computer. The backend listens only on loopback. The client connects only to
  127.0.0.1 (`USwcBackendSubsystem` refuses a different host). A run of the client and its backend uses a network
  namespace with loopback only.
- No game content goes into git: no game file, no converted file (glTF, PNG, JSON of the game), no imported asset
  (`unreal/SoulwardenClient/Content/Imported/` is ignored), no text or picture of the game, no game log, no capture.
  The only pictures in git are the screenshots of this client in `docs/screenshots/` that the user chose for the
  README (WebP, each below 500 KB, cropped to the game view; no picture of the retail client).
  Converted content stays in `~/Games/soulwarden-client/content`.
- Two kinds of runs:
  - A player run (`tools/play.sh`, started by the user): the data folder of the user (`~/.local/share/soulwarden`,
    the same characters as the retail game), with the `run.lock` of the Soulwarden launcher. While a player run holds
    the lock, a release of the Soulwarden project waits (status 3), as when the user plays the retail game.
  - A development run (agents): its own data folder (`~/Games/soulwarden-client/dev-data`) with a seed character.
    A development run never writes to the data folder of the user. It can use a copy of the database of the user (for
    example the characters of the user for a test) in its own data folder.
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
  the client in its environment.
- Parallel tracks: each track uses its own development data folder (`SWC_DEV_DATA=~/Games/soulwarden-client/dev-data-TRACK`)
  and its own agent desktop (`SWC_AGENT_DESKTOP=soulwarden-client-TRACK`), so that the runs of two tracks do not wait
  for one lock. Each run has its own network namespace: the ports do not collide.
- A window run never shows on a workspace of the user. The trap (2026-10-19): the agent desktop places a window only in the
  wait of `omarchy-agent-desktop run --wait` (at most 300 s); a session that waits first for the world lock shows its
  gamescope window too late, and that window goes to the focused workspace of the user. `tools/dev-run.sh --window` waits
  for the world lock before the launch, and its window guard (`tools/lib/window_guard.py`, log `LABEL-window-guard.log`)
  reads `hyprctl clients -j` (read only) for the life of the run: a window of the launch that is not on `__agent:DESKTOP`
  is moved with `omarchy-agent-desktop adopt`, else the run stops. Start window runs only with `tools/dev-run.sh`.
  A stale agent desktop (2026-10-19: its headless output showed a normal workspace, and the launch was refused): before a
  launch, `tools/dev-run.sh` and `tools/flythrough.sh` read `omarchy-agent-desktop status --json` (`tools/lib/agent-desktop.sh`);
  a stale or offline desktop with no window is removed (`cleanup`) and made again; with windows, the run ends with status 3
  and changes nothing.
  The trap of the lock gap (2026-10-19 02:43): a check that takes the shared world lock and releases it at once lets an
  import in before the session takes its own lock; the session then waits inside gamescope, and its window comes after
  the window wait. Thus `tools/dev-run.sh` holds the shared lock (and the run slot) through the launch, and the session
  takes its own at once (`SWC_WORLD_HELD=1`); a session that would have to wait ends with status 3 and opens no client.
- The GPU (24 GB) is shared by the clients of all tracks and the retail game processes; a client needs about 6 GB.
  `tools/dev-run.sh --window` (and `tools/flythrough.sh`) takes, in this order: the world lock, a run slot, then the
  free GPU memory (`tools/lib/gpu-gate.sh`). The slots: `SWC_RUN_SLOTS` (default 2) client runs that render at a time,
  each with a lock on `~/Games/soulwarden-client/unreal/run-slot-N.lock` for its whole life (the session holds it; the
  lock ends with the process, also with `tools/dev-stop.sh`); the run log shows "run slot N". A live retail development
  run (a process with `SOULWARDEN_DEV_RUN=1`, read only from `/proc/PID/environ`) counts as one slot: next to it, only
  `SWC_RUN_SLOTS` - 1 clients of this project render. The count and the take are in one short hold of
  `run-slot-gate.lock`. Then the free memory (nvidia-smi, read only) must be at least `SWC_GPU_FREE_MIB` (default 9216):
  it covers the programs of the user, which take no slot. Each wait logs one line each 30 s and ends with status 3 after
  `SWC_GPU_WAIT` seconds (default 900). A memory check alone has a race: two runs saw the same free memory and both
  started (2026-10-19). Free memory at the start is not enough next to a retail run: a client passed the 9 GB check and
  had a Vulkan out-of-memory fault at frame 1, because a retail capture (about 4 GB) ran and the two other clients grew to
  7 to 8 GB after their start (2026-10-19). The trap: "Failed to allocate Device Memory"
  in a client log is the shared GPU, not a client defect, until a run with enough free memory shows the same fault.
- A process of the Unreal client never has `SOULWARDEN_DEV_RUN=1` in its environment: `tools/dev-stop.sh` of Soulwarden
  stops such processes, and its `--check` fails while one is alive.
- Retail reference runs (decision of the user, 2026-10-18): an agent of this project may start a development run of
  the retail client with the tools of the Soulwarden project, for reference pictures, videos, and measurements
  (`tools/scenarios`, `tools/dev-run.sh`, `tools/hx`, and the capture scripts in
  `~/Games/soulwarden/work/evidence/unreal-references/tools`). Conditions: no other development run of the Soulwarden
  project is alive (`~/Developer/azoth/tools/dev-stop.sh --check` gives 0) and the Soulwarden orchestrator does not use
  runs at that time (parallel work). Follow the run rules of `~/Developer/azoth/AGENTS.md`: its own Proton prefix and
  data folder, the mark `SOULWARDEN_DEV_RUN=1`, input only through `tools/hx`, gamescope on an agent desktop. Stop only
  the own run. Never use the Proton prefix or the data folder of the user. Write the results under
  `~/Games/soulwarden/work/evidence/unreal-references/`, never into `~/Developer/azoth`.
  Each client retail reference run uses its own Proton prefix, data folder, and desktop (rule of 2026-10-22; the trap: the run lock
  of the Soulwarden `tools/dev-run.sh` is per data folder, but all development runs share the default prefix and the desktop
  `soulwarden-game`; a zoom change of a client thread wrote into the shared prefix while a Soulwarden client started):
  1. The own prefix: a copy of the development prefix (`cp -a`, made once while no development run is alive; for example
     `~/Games/soulwarden-client/work/proton-ref`), selected with `SOULWARDEN_COMPAT_DIR=.../proton-ref/1063730`. Never the prefix of
     the user; never change a file of the shared development prefix (a saved option such as `m_cameraZoom` changes only in the copy).
  2. The own data folder (`SOULWARDEN_DATA_DIR`) and the own desktop (`SOULWARDEN_DESKTOP_ID`, for example `soulwarden-client-ref`).
     `tools/snap.sh` of the reference tools takes any marked gamescope: select the gamescope whose environment has the own prefix.
  3. Before a start: NO other process with `SOULWARDEN_DEV_RUN=1` is alive, in ANY prefix. Two retail runs at one time are not
     supported (they share the agent desktop, and `gamescopectl` with no `GAMESCOPE_WAYLAND_DISPLAY` takes `gamescope-0`). Since
     backend 1.8f (commit 5174ff8) the Soulwarden `tools/dev-run.sh` stops only the earlier runs of its own data directory; while
     a run of a different data directory is alive (also a backend stage run), it starts nothing, prints that run and its stop
     command, and exits with an error. Then do not start and do not stop that run: do other work and try later. Before 1.8f it
     stopped every run (2026-10-22: a client retail run stopped the final live check of a backend stage). Stop only the own run:
     `tools/dev-stop.sh --data-dir OWN_DATA_DIR` of Soulwarden, or the own processes by their prefix (the mark and
     `SOULWARDEN_COMPAT_DIR` in `/proc/PID/environ`). Never `tools/dev-stop.sh` of Soulwarden with no option: it stops each run.
- The Soulwarden repository is read only for this project. A change that the backend needs goes to the orchestrator of
  this project, which reports it to the user, never as an edit of `~/Developer/azoth`. No thread of this project sends
  a report, a request, or a fact to the Soulwarden orchestrator thread (decision of the user, 2026-10-18).
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
- Commit only when the user asks or a stage is complete and checked. The tracks do not push; the orchestrator pushes `main`.

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
tools/cook.sh [--map /Game/PATH/MAP]           # cook and stage the client (docs/WORLD.md "The cooked build"; the lock, 20 GB scope); then tools/dev-run.sh --cooked
tools/dev-run.sh --quit-when characters         # a development run with no window: the login path to the character list
tools/dev-run.sh --window                       # a development run with the client window on the agent desktop
tools/dev-run.sh --enter --quit-when world      # a development run that enters the world, stays 10 s, and leaves
tools/test-net.sh                               # the tests of the network codec with bytes of the backend (no engine)
tools/test-actions.sh                           # the tests of the attack graph and its parity with the backend sequence (no engine)
tools/test-movement.sh                          # the tests of the movement rules, the position check, the speed table and the statuses (no engine)
tools/test-ranged.sh                            # the tests of the ranged and magic actions, the mana and heal bytes, the parity of the presses (no engine)
tools/convert-ui.sh hud                         # the canvases, scripts, textures, fonts, texts, and game data of the HUD
tools/test-ui.sh --preview                      # the tests of the UI runtime and the HUD scripts (no engine), a picture
tools/convert-ui.sh items|uistates              # the item data of the inventory screens, the UI state table (stage P2.4)
tools/convert-ui.sh world                       # the canvases and the data of the UI in the world views: plates, prompts, numbers, ticker (stage WF7)
tools/convert-ui.sh worldmap                    # the world map: its data (territories, POIs, shapes, landmarks), the canvas, all map tiles (stage MVP-C)
tools/convert-ui.sh options                     # the Options screen: its canvas, the option defaults of the client program, the input profiles (stage P2.3)
tools/convert-ui.sh gamemenu                    # the escape menu and the NavBar: their canvases, textures, and the textures that navbar.luac sets by path (docs/UI.md "The game menu")
tools/test-menu.sh                              # the tests of the menu document reader, the camera curves, and the light rule of the menu scene (no engine)
tools/convert-frontend.sh clips|scene           # the menu idle clip (then tools/import-player.sh) and the record of the NightHaven scene (docs/FRONTEND.md F4)
tools/menu-check.sh LABEL SECONDS -- ARGS       # a window run of the front end, recorded (docs/FRONTEND.md; the drives in tests/drive/f*-ui.txt)
tools/test-items.sh                             # the tests of the bag and the equipment with the states of the backend encoders (no engine)
tools/dev-build.sh --preset NAME                # a build preset for the dev character (SWC_DEV_DATA must be set; --list: read only)
tools/convert-player.sh tables|base|wanted      # the player character: databases, base skeletons, the parts that a run asked for
tools/convert-player.sh movement|roads          # the movement rules of the data, the road maps of the regions (stage P3.3)
tools/import-player.sh                          # import the player base and parts onto one skeleton for each gender
tools/convert-player.sh all-looks               # the wanted list of all the looks a player can equip (SWC_WORK/player/wanted-all.json; AST-215)
tools/prepare-looks.sh [--only K]               # convert and import that list in chunks, a background job (the lock is free between the calls; logs/prepare-looks)
tools/dev-stop.sh                               # stop the development runs on the own SWC_DEV_DATA (--all: of all tracks)
tools/convert-creatures.sh convert --near 8880 4250 300   # the creatures near a place (--all: the world; wanted: the client list)
tools/convert-creatures.sh markers              # the marker type of each creature slice (the plates of the UI; no import)
tools/import.sh creature-                       # import the converted creatures into /SwcGameContent
tools/test-creatures.sh                         # the tests of the creature entity table with states of the backend encoders
tools/creature-check.sh LABEL STAY ROUTES [DRIVE]   # a video check of the creatures with the development routes
python3 -B tools/lib/retail_replay.py TAKE.json tests/drive/p3g-NAME [--weapon 1|2] [--zoom M]   # a client replay of a retail take (stage P3.G)
python3 -B tools/lib/retail_compare.py TAKE.json CLIENT_LOG --backend BACKEND_LOG   # the values of a replay against its take
python3 -B tools/lib/side_by_side.py TAKE.json RETAIL.mp4 CLIENT_LOG CLIENT.mp4 RECORDER_START OUT.mp4 --part A B TEXT   # the side-by-side video
tools/convert-objects.sh write                  # the objects of the level data for the object layer (docs/OBJECTS.md; meshes: the depleted node meshes)
tools/convert-environment.sh write              # the environment data: the mission, the time presets, the lights and volumes of each region, the sky (check: the E0 proof)
tools/match-view.sh SET [VIEW...]               # the matched views of a set of retail references: client pictures, side-by-side pictures, a contact sheet
tools/match-view.sh SET VIEW --variant VIEW@NAME='CVAR VALUE;...'   # one more picture with console values; the next view sets those values back (from 2026-10-19; before, they stayed on)
tools/test-environment.sh                       # the tests of the world clock, the environment rules, the local light rules, and the environment reader (no engine)
tools/convert-environment.sh cookies            # the projector textures of the local lights (then tools/import-cookies.sh: the lock)
tools/lumen-test.sh --wait 7200                 # the Lumen mesh data test: the inn and two houses with distance fields and cards (client: swc.LumenTest 1)
tools/convert-water.sh write|check|samples       # the water of the world: the surfaces, the ocean, the materials, the quadtrees (docs/WATER.md)
tools/convert-water.sh source-textures|shore    # the inputs of the source water shader; the shore foam emitters and their sheets (shore.json)
tools/import-water.sh                           # the water textures, the source water material M_SwcWaterSource, the shore foam decal M_SwcShoreFoam
tools/test-water.sh                             # the tests of the water readers, the depth query, and the depth classes (no engine)
tools/convert-scenes.sh write|convert            # the quest scenes: spawners, slices, timelines, whispers, cinematics; the actors and clips (docs/QUESTS.md Q5)
tools/test-scenes.sh                            # the tests of the scene key curves (the worked beach camera pair; no engine)
tools/convert-persons.sh write --near X Y R     # the persons of the settlements, their timelines, lines, and cameras (docs/PERSONS.md; no --near: the world)
tools/convert-persons.sh convert                # the bodies, items, and clips of the persons (then tools/import-player.sh)
tools/convert-materials.sh audit                # the rich material records, the slot provenance, and the key collisions (docs/MATERIALS.md)
tools/convert-materials.sh instances           # the specs of the material instances (then tools/apply-materials.sh: the lock, bounded calls)
tools/material-proof.sh STAGE --wait 7200       # the render check of a materials stage (offscreen; tools/convert-materials.sh proof-plan STAGE first)
tools/convert-materials.sh decals export NAME   # the roads and decals of regions (then tools/import-surface-layer.sh NAME; the client shows it by default, -SwcNoSurfaceLayer turns it off)
tools/convert.sh cover NAME... | --all          # the ground cover of the regions (regions/NAME/cover.bin; the client shows it by default, -SwcNoGroundCover turns it off; docs/WORLD.md T3)
tools/convert-particles.sh library|menu|placements|names|report|systems|check   # the particle effects: the libraries, the placements, the support report, the systems (docs/PARTICLES.md)
tools/convert-particles.sh skills                # the effect keys of the skills and the creature spells, the hits, the trails, the statuses (after convert-player.sh actions and convert-creatures.sh effects; docs/EFFECTS.md)
tools/import-particles.sh [--force]             # the particle atlases, materials, and Niagara systems (the lock; then the client plays them near the view)
(cd converter && python3 -B -m swc_convert.landmarks write)   # the landmarks: the sky maelstrom (docs/WORLD.md "The landmarks")
tools/import-landmarks.sh [--force]             # the meshes, parents, and instances of the landmarks, all new (the lock; World/SwcLandmarks places them)
(cd converter && python3 -B -m swc_convert.skycube write)    # the source cloud layers of the water sky cube (docs/WATER.md "The water sky cube")
tools/import-skycube.sh [--force]              # their meshes, masks, parent M_SwcDistanceClouds, and instances, all new (the lock)
```

The runs and the connection: [docs/NETWORK.md](docs/NETWORK.md).

Paths and defaults are in `tools/env.sh` (each one can be set before). The converter writes to
`~/Games/soulwarden-client/content`, the logs go to `~/Games/soulwarden-client/logs`.
