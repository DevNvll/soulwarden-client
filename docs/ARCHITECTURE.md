# Architecture of the Soulwarden client

## Parts

| Part | Path | Job |
| --- | --- | --- |
| Converter | `converter/swc_convert` | Reads the paks of the game copy (Oodle through the library of the installed engine) and writes glTF, PNG, and JSON into `~/Games/soulwarden-client/content`, plus `manifest.json`. |
| Soulwarden readers | `converter/vendor/soulwarden` | An unchanged copy of the pak, object stream, slice, terrain, and shape readers of the Soulwarden repository (MIT, commit e0a3d97). Update by a new copy, never by an edit. |
| Unreal project | `unreal/SoulwardenClient` | The client: the C++ module `SoulwardenClient`, the config, the import script. |
| Imported assets | `unreal/SoulwardenClient/Content/Imported` | The Unreal assets of the converted content. Made from the game copy: not in git. |
| Tools | `tools/` | `env.sh` (paths), `convert.sh`, `build.sh`, `import.sh`, `editor.sh`. |
| Backend | `~/Developer/azoth/apps/backend/dist/soulwarden-backend` | The Soulwarden backend, unchanged. This project starts its own instance with its own data folder. |

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

## Run model (stage 1.3)

```text
unshare (user + network namespace, loopback only)
  +-- soulwarden-backend --data-dir ~/Games/soulwarden-client/backend-data --gamedata-dir (read only)
  +-- the Unreal client (-SwcHttp=127.0.0.1:PORT)
```

The retail client of the Soulwarden project runs in its own namespace with its own backend. Thus both can run at
the same time with no port conflict.

## The connection

`USwcBackendSubsystem` (a game instance subsystem) owns the connection. The client speaks the original wire
protocol (Soulwarden protocol notes, sections 2 to 4): the HTTP login path, DTLS 1.2 with the backend certificate,
the GridMate carrier (datagram at most 1,038 bytes), the envelope with the type index of `typeindex.json`, the
registration, the state bundles (records of interest numbers, fragments for component slots of the slice of an
entity group), and the input report 1589. The backend encoders (`apps/backend/src/server/rep/states`) are the
specification of the client decoders.
