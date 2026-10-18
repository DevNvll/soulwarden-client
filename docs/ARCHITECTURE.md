# Architecture of the Soulwarden client

## Parts

| Part | Path | Job |
| --- | --- | --- |
| Converter | `converter/swc_convert` | Reads the paks of the game copy (Oodle through the library of the installed engine) and writes glTF, PNG, and JSON into `~/Games/soulwarden-client/content`, plus `manifest.json`. |
| Soulwarden readers | `converter/vendor/soulwarden` | An unchanged copy of the pak, object stream, slice, terrain, and shape readers of the Soulwarden repository (MIT, commit e0a3d97). Update by a new copy, never by an edit. |
| Unreal project | `unreal/SoulwardenClient` | The client: the C++ module `SoulwardenClient`, the config, the import script. |
| Imported assets | `unreal/SoulwardenClient/Content/Imported` | The Unreal assets of the converted content. Made from the game copy: not in git. |
| Tools | `tools/` | `env.sh` (paths), `convert.sh`, `build.sh`, `import.sh`, `editor.sh`, `backend-update.sh`, `play.sh`, `dev-run.sh`, `dev-stop.sh`, `lib/session.sh`, `test-net.sh`. |
| Backend | `~/Games/soulwarden-client/backend/current` | A copy of a Soulwarden release (backend and game data), unchanged. `tools/backend-update.sh` updates it. |

## The content pipeline

```text
game copy (paks) --converter--> content/ (glTF + PNG + JSON, manifest.json) --tools/import.sh--> /Game/Imported
```

- Static mesh: `.cgf` + `.cgfheap` -> one glTF with its sub-materials; textures as PNG in `content/textures/`.
- Character: `.cdf` -> skeleton (`.chr`), skins (`.skin`), materials, and clips (`.caf`) -> one glTF with a skin and
  animations. Unreal makes a Skeleton, Skeletal Meshes, Physics Assets, and Anim Sequences.
- Region: `region.heightmap` -> `height16.png` (row 0 = north), the macro map -> `macro_diff.png`, the chunks, capitals,
  and distribution -> `placements-*.json` (mesh path and a 4 x 4 matrix in engine space).

Proved formats and their limits: research AST-135, section 3.

## Spaces

- Engine space of the game data: meters, X right, Y forward, Z up, right-handed.
- glTF: the converter writes (x, z, -y): Y up, right-handed, meters.
- Unreal: centimeters, Z up, left-handed. The importer converts glTF. Stage 1.1 proves the full map for the
  placements.

## Run model (stage 1.4)

```text
player run (tools/play.sh, the user):
  the checks (tools/play.sh --check): backend copy = current release of the user, Steam account, lock state
  tools/lib/session.sh: unshare (user + network namespace, loopback only)
    flock ~/.local/share/soulwarden/run.lock          (the lock of the Soulwarden launcher)
    +-- backend copy: --data-dir ~/.local/share/soulwarden --gamedata-dir backend/current/gamedata
    +-- the Unreal client as the user (the Steam id and persona of the user in its environment, never in a file)

development run (tools/dev-run.sh, agents):
  tools/lib/session.sh: unshare (user + network namespace, loopback only)
    flock ~/Games/soulwarden-client/dev-data/run.lock
    +-- backend copy: --data-dir ~/Games/soulwarden-client/dev-data --seed-character NAME
    +-- the Unreal client (-nullrhi), or its window in gamescope on the agent desktop soulwarden-client-dev
```

- The backend is the copy of a Soulwarden release (`~/Games/soulwarden-client/backend/current`,
  `tools/backend-update.sh`), not the working copy of the Soulwarden repository.
- A player run uses the same characters as the retail game. It holds the same lock as the Soulwarden launcher: the
  retail game and a player run of this client do not run at the same time on one database, and a Soulwarden release
  waits while one of them runs. A later option: a player run that finds the lock held joins the network namespace of
  the running backend (both clients in one world).
- No process of this project has `SOULWARDEN_DEV_RUN=1` (the mark of the Soulwarden development runs). The processes of
  a development run have the mark `SWC_DEV_RUN=1` (`tools/dev-stop.sh`).
- The Steam authentication of the client is a stub: the backend does not check a ticket (Soulwarden protocol notes 2.1).
- [NETWORK.md](NETWORK.md) has the details of the runs and the client side of each contract.

## The connection

`USwcBackendSubsystem` (a game instance subsystem) owns the connection. The client speaks the original wire
protocol (Soulwarden protocol notes, sections 2 to 4): the HTTP login path, DTLS 1.2 with the backend certificate,
the GridMate carrier (datagram at most 1,038 bytes), the envelope with the type index of `typeindex.json`, the
registration, the state bundles (records of interest numbers, fragments for component slots of the slice of an
entity group), and the input report 1589. The backend encoders (`apps/backend/src/server/rep/states`) are the
specification of the client decoders. The code is in `Source/SoulwardenClient/Net/`: the codec and the session in plain
C++ (`Net/Codec/`, tested with bytes of the backend by `tools/test-net.sh`), an unchanged copy of the wire codec and of
the state reader of the backend tests (`Net/Vendor/soulwarden/`), and the transport thread with the DTLS of the engine
OpenSSL (`Net/SwcRepConnection`). [NETWORK.md](NETWORK.md) has the client side of each contract.
