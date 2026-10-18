# Architecture of the Soulwarden client

## Parts

| Part | Path | Job |
| --- | --- | --- |
| Converter | `converter/swc_convert` | Reads the paks of the game copy (Oodle through the library of the installed engine) and writes glTF, PNG, and JSON into `~/Games/soulwarden-client/content`, plus `manifest.json`. |
| Soulwarden readers | `converter/vendor/soulwarden` | An unchanged copy of the pak, object stream, slice, terrain, and shape readers of the Soulwarden repository (MIT, commit e0a3d97). Update by a new copy, never by an edit. |
| Unreal project | `unreal/SoulwardenClient` | The client: the C++ module `SoulwardenClient`, the config, the import script. |
| Character | `Source/SoulwardenClient/Character/`, `converter/swc_convert/player.py` | The character of a player: the modular body on one skeleton (Leader Pose), its look from the appearance and paperdoll states ([CHARACTER.md](CHARACTER.md)); its movement, animation, and keys ([MOVEMENT.md](MOVEMENT.md)) and its camera ([CAMERA.md](CAMERA.md)) with the values of the game data. |
| Editor tools | `unreal/SoulwardenClient/Source/SoulwardenClientEditor` | An editor module for the scripts: wait for compilation, render a scene capture to PNG, spawn actors in a commandlet. |
| Imported assets | `unreal/SoulwardenClient/Content/Imported` | The Unreal assets of the converted content. Made from the game copy: not in git. |
| Tools | `tools/` | `env.sh` (paths), `convert.sh`, `build.sh`, `import.sh`, `editor.sh`, `editor-script.sh`, `backend-update.sh`, `play.sh`, `dev-run.sh`, `dev-stop.sh`, `lib/session.sh`, `lib/dev_character.py`, `test-net.sh`, `convert-player.sh`, `import-player.sh`. |
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

Stage 1.1 proved this map in Unreal (the proof is below). The code is `converter/swc_convert/space.py`.

| Space | Units | Axes | Hand |
| --- | --- | --- | --- |
| Engine: the game data, the placements, the backend | m | X east, Y north, Z up | right |
| glTF of the converter | m | (x, z, -y): Y up | right |
| Unreal | cm | (x, -y, z) x 100: X east, Y south, Z up | left |

- Position: Unreal = C x engine, with C = diag(100, -100, 100). Example: engine (9000, 4204, 151.66) m is Unreal
  (900000, -420400, 15166) cm. The main world (7 x 7 regions of 2,048 m) covers Unreal X 0 to 1,433,600 and
  Y 0 to -1,433,600. The engine origin is the Unreal origin (no offset).
- The two steps of C: the converter writes (x, z, -y) (`gltf.z_up_to_y_up`, a rotation, no mirror). The
  Interchange glTF importer maps (X, Y, Z) to (X, Z, Y) and multiplies by 100 (engine source:
  `GLTFCore/Private/GLTF/ConversionUtilities.h`, `ConvertVec3`; `InterchangeGltfPrivate.h`,
  `GltfUnitConversionMultiplier`).
- Mirror: the determinant of C is negative. The right-handed coordinates become left-handed coordinates, and the
  world is not mirrored. An Unreal view and a software view (engine space) from the same camera show the same
  picture.
- Mesh vertices: the importer applies C. The local space of a mesh asset is C x the local space of the source mesh.
- Mesh nodes: a `.cgf` node chunk (0x824) has a matrix for its object: rows = the images of X, Y, Z, then the
  translation in centimeters. The converter applies the chain of the parent nodes to the vertices
  (`cgf.node_matrices`). 37 of the 784 mesh nodes with geometry in the proof area have a matrix that is not the
  identity (for example the ornament on the top of a claim totem, 4.65 m above its root).
- One glTF node for each mesh: Interchange makes one static mesh asset for each glTF node. Thus the converter
  writes all chunks of a `.cgf` as the primitives of one glTF mesh with one node. (Before stage 1.1, 8 meshes of
  the proof area became 2 to 4 assets each, and a placement showed only the first part.)
- Placement: a placement is a 4 x 4 matrix M (column vectors, m). The Unreal matrix is C M C^-1: the linear part
  is S L S with S = diag(1, -1, 1), the translation is 100 S t.
- Rotation: a quaternion (x, y, z, w) becomes (-x, y, -z, w). An engine yaw of a degrees (from east toward north)
  is an Unreal yaw of -a degrees (Unreal yaw goes from X toward Y, and Unreal Y is south).
- Scale: the value does not change. A placement with a negative determinant stays a mirror (a negative X scale).
- Shear: an Unreal transform (FTransform) has no shear. The converter keeps the scale of each axis and the nearest
  rotation. In the proof area, 43 of 4,800 placements have a shear (capitals: a parent with a non-uniform scale and
  a rotated child). The largest shear is 1.8 %: the error is at most 1.8 % of the size of the mesh.
- Terrain: the height PNG of a region has row 0 = north = the smallest Unreal Y, and column 0 = west = the smallest
  Unreal X.
- Directions (a light, a camera): Unreal = (x, -y, z). The software renderer has a vertical field of view; the
  Unreal field of view is horizontal: 2 atan(tan(v / 2) x width / height).

The proof (stage 1.1):

```sh
tools/convert.sh space-proof                       # marker glTF, terrain patch, plan, two software pictures
tools/import.sh                                    # the meshes of the proof area
tools/editor-script.sh space_proof.py --render     # place, read back, compare, render (no window)
```

The marker is a mesh with three arms of different length and color (X 3 m red, Y 2 m green, Z 1 m blue). The plan
places four markers with known transforms (identity; yaw 30; yaw 30, pitch 20, roll 10, scale 1.5; a mirror of X)
and all placements of the proof area (region `r_+04_+02`, 300 m around the settlement). Unreal reads the vertices
back and compares them with the engine positions through C. The pictures and the report are in
`~/Games/soulwarden-client/evidence/1.1/`.

Result (2026-10-18): the 96 vertices of each marker and its three arm tips are at the expected Unreal positions
(error 0.0000 cm). 22 game placements (6 chunk, 6 capital, 4 capital variant, 6 distribution) have the expected
Unreal bounds (error 0.0000 cm). The placement with the largest shear (1.8 %, a cloth of 3.6 m) has an error of
1.12 cm. The Unreal views and the software views of the same cameras show the same picture
(`space-proof-sheet.png`).

Rules of an editor script that renders (`tools/editor-script.sh --render`):

- `-AllowCommandletRendering -RenderOffscreen` starts the renderer with no window; `-NoTextureStreaming` loads all
  mips.
- A commandlet does not tick: the script calls `SwcEditorLibrary.FinishCompilation` (shaders, textures, meshes),
  and `SwcEditorLibrary.CaptureToPng` renders warm-up frames before the picture.
- A commandlet has no placement subsystem: `EditorActorSubsystem.spawn_actor_from_object` crashes. The scripts use
  `SwcEditorLibrary.SpawnMeshActor` and `SpawnActorOfClass`.
- The Python log lines (`unreal.log`) are only in the full log of the project (`Saved/Logs/SoulwardenClient.log`).

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
