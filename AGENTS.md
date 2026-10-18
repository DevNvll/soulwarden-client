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
- Do not use the data of the Soulwarden project of the user: not `~/.local/share/soulwarden` (only its folder
  `gamedata`, read only, for the backend instance of this project), not the Proton prefixes, not
  `~/Games/soulwarden/releases`. The backend instance of this project has its own data folder
  (`~/Games/soulwarden-client/backend-data`).
- Do not start, stop, or control the retail client or a development run of the Soulwarden project.
- The Soulwarden repository is read only for this project. A change that the backend needs goes to the Soulwarden
  project (its orchestrator thread or its Linear project), not into this repository.

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
tools/import.sh [FILTER] [--force]              # import the converted content into /Game/Imported (no window)
tools/editor.sh                                 # open the editor (only when the user asks)
```

Paths and defaults are in `tools/env.sh` (each one can be set before). The converter writes to
`~/Games/soulwarden-client/content`, the logs go to `~/Games/soulwarden-client/logs`.
