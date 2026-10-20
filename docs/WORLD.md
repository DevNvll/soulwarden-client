# The world of the client

The main world of the game (`newworld_vitaeeterna`): 7 x 7 regions of 2,048 m (14.336 km x 14.336 km). This document
has the census of the world (stage 1.2), the shared assets and their pipeline (stage 1.2), and the disk budget. The
space map is in [ARCHITECTURE.md](ARCHITECTURE.md) ("Spaces").

## The census (stage 1.2)

`tools/convert.sh census` reads the game copy only (no conversion): the chunks, the capitals (with the spawned slices
and the meshes of the variants), and the distribution of each region, with the rules of stage P0 and 1.1. Output
(made from the game copy, not in git): `content/census/` (`world-census.json`, `regions.json`, `meshes.json`,
`textures.json`) and, for each region, `content/regions/NAME/placements.npz` (the mesh table, a mesh index, a source
index, and the engine matrices, 3 x 4 rows) and `meshes.json`. The census of the 49 regions takes about 3 minutes with
2 processes; a later run reads its cache.

Result (2026-10-18):

| Value | All 49 regions | The 26 regions of `playable.json` |
| --- | --- | --- |
| Placements | 5,890,959 | 4,446,353 |
| - chunk | 432,535 | |
| - capital | 270,242 | |
| - capital variant | 17,981 | |
| - distribution (plants, stones, trees) | 5,170,201 | |
| Unique meshes | 17,204 (118 are not in the game copy) | 17,198 |
| Triangles of the unique meshes (LOD 0, drawn) | 177.1 million | |
| Triangles of all placements (LOD 0, drawn) | 13.8 billion | |
| Textures that the materials use: base color | 1,954 (1.86 G texels at 1,024) | |
| Textures that the materials use: normal | 1,770 (1.71 G texels at 1,024) | |

- 12 regions have no placement (ocean). Some regions have only distribution (for example `r_+05_+05`: 299,939
  plants, 6 chunks, and 7 plot markers in its capitals).
- The largest region has 299,939 placements; the region of the proof area (`r_+04_+02`) has 221,103 placements and
  3,179 unique meshes.
- 13.8 billion triangles of all placements at LOD 0: the client needs Nanite (or the LOD files) and instancing.
- Each region has the far meshes of the game (`impostors/`, `poi_impostors/`): a candidate for the distance (stage 1.3).

## The shared place of the imported content

The imported assets are made from the game copy: they cannot be in git, and one copy serves the root checkout and each
worktree. The place is a content plugin outside the repository:

- `~/Games/soulwarden-client/unreal/SwcGameContent` (`SWC_GAME_CONTENT` in `tools/env.sh`), mount point
  `/SwcGameContent/`.
- `tools/content-link.sh` makes the plugin and links it into the checkout:
  `unreal/SoulwardenClient/Plugins/SwcGameContent` (a symbolic link; git ignores it). The tools that start Unreal
  (`import.sh`, `editor-script.sh`, `editor.sh`) call it. A new checkout runs it one time.
- One import at a time: `tools/import.sh` holds `~/Games/soulwarden-client/unreal/import.lock` (`flock -o`: the
  servers that the editor starts do not keep the lock). Do not import while an editor of another checkout has the
  same assets open.
- The world lock, a readers-writer lock: `~/Games/soulwarden-client/unreal/world-build.lock`
  (`tools/lib/world-lock.sh`). A run of the client streams the assets of the level: the external actors of the cells,
  the meshes, the collision meshes. A level build deletes and writes these files. A creature run that read such a
  file at that time stopped (`ReadFile` errno 2 on an external actor, then the assertion `IsValid` in
  `UnixPlatformFile.cpp:212`). The rule:
  - A writer holds the lock exclusively: each call of `tools/build-world.sh`, and each import that deletes assets or
    makes them again (`tools/import.sh --world`; `--force` and `tools/import-world.sh` use it, because section 6 deletes
    the old collision mesh). Other writers of streamed assets use it too (the mesh rebuild of the materials track).
  - A writer takes the world lock first, then import.lock. All tools use this order: no deadlock, and a writer that
    waits for the runs does not hold import.lock.
  - A run holds it shared from its start to its end: `tools/lib/session.sh` (thus `tools/dev-run.sh`, `tools/play.sh`,
    `tools/match-view.sh`, `tools/creature-check.sh`) and `tools/flythrough.sh`. Many runs share it.
  - A waiting writer goes first. flock gives a shared lock to a new run while a writer waits, and a stream of runs kept
    the writers waiting more than 25 min (2026-10-19). Thus a writer writes a marker (`world-build.writers/ID`, the
    process id of its shell) while it waits and works, and a new run waits while a marker of a live process is there.
  - A run waits at most `SWC_WORLD_RUN_WAIT` seconds (default 900) for a writer, then ends with status 3. A writer waits
    at most 3600 s (`tools/build-world.sh`) or the `--wait` time of `tools/import.sh`; then its status is 75.
  - The backend, the client, and the editor do not get the descriptor of the lock (`flock -o`, `7>&-`): the servers that
    they start can live after them.
- A long import releases import.lock at least every 10 minutes. `tools/import-world.sh NAME... | --playable | --all`
  calls `tools/import.sh --regions NAME --max-seconds 480 --wait 3600 --world` until the region is complete (the
  import script stops after the batch that passes the time) and waits 15 s between two calls: an import of another
  track that waits on the lock (`flock -w`) gets it there.
- The level build obeys the same rule. A region has three steps (the Landscape, the cell actors, the far meshes).
  `build_world.py` stops after the step that passes 400 s of work (`SWC_WORLD_MAX_SECONDS`), and
  `tools/build-world.sh` calls it again until the region is in the level. Each call writes one line to the output
  with the seconds that it held the lock. A normal call holds it for about 2 minutes. One call (`r_+05_+01`) held it
  for 17.5 minutes: a Landscape step of 454 s and 5 minutes in the exit of the editor.
- A long driver (`tools/build-world.sh --import --all`) runs as a detached process (`setsid nohup`) with its output
  in `~/Games/soulwarden-client/logs/build-world-all.txt`. It has no mark `SWC_DEV_RUN=1`: `tools/dev-stop.sh` does
  not stop it. Each call reads the scripts of the checkout again. A script change that needs a new C++ function goes
  in after the build of the module (`r_+03_+01` failed one time: `AttributeError` for a new function of
  `USwcEditorLibrary` that the module did not have yet). Do not build the module while a call runs.
- A commandlet does not scan the content of a plugin: a script calls
  `AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/SwcGameContent"], True)` first.
- The derived data cache of the engine (Zen, in `~/.config/Epic/UnrealEngine/Common`) is shared by all checkouts.
- A run in its network namespace cannot reach Zen: it uses only the file store
  `~/.config/Epic/UnrealEngine/Common/DerivedDataCache` (docs/NETWORK.md). The default graph of the editor puts Zen
  before the file store, and a Zen hit does not fill the file store (measured: 40 meshes, 0 new files). Thus a run
  built the derived data of each new mesh again, and the cells at the place did not load in 30 s.
- The editor commandlets (`import.sh`, `build-world.sh`, `editor-script.sh`, `warm-cache.sh`) use the graph
  `SWC_EDITOR_DDC` of `tools/env.sh`: the file store before Zen. A Zen hit then fills the file store with no new build
  (measured: 40 meshes in 0.5 s, 343 new files). The engine is an installed build: `-ddc=NoZenLocalFallback` uses
  `Engine/DerivedDataCache`, not the file store of the runs (`InstalledNoZenLocalFallback` uses it, but builds again
  all the data that is only in Zen).
- `tools/warm-cache.sh NAME... | --level` fills the file store for content made before: it loads the world level and
  the meshes of the regions with the renderer (with `-nullrhi` the editor does not cache the render data), in calls of
  about 5 minutes with import.lock.

Folders of the plugin:

| Folder | Content |
| --- | --- |
| `Base/` | The base materials `M_SwcOpaque`, `M_SwcMasked`, `M_SwcTranslucent` and the mesh pipelines `P_SwcMeshNanite`, `P_SwcMesh` (made by the import script) |
| `Textures/STEM_HASH/` | One texture |
| `Materials/m_HASH` | One material instance |
| `Meshes/STEM_HASH/` | One static mesh |
| `Characters/`, `Test/` | Characters and test assets (the default glTF pipeline) |

## Shared assets (stage 1.2)

- Texture: one PNG for each texture product, rule, and size cap (`content/textures/v2-1024/PATH.png`; the cap is
  1,024 for the base color and the normal maps), and one `Texture2D`. Base color: sRGB, the default compression; a
  PNG with an alpha of 255 everywhere is RGB, thus DXT1 (a 1-bit alpha of BC1 stays). Normal: linear,
  `TC_Normalmap`, the green channel of the engine (no flip; the glTF PNGs of the characters flip it).
- Material: one record for each set of rules: the blend (opaque, masked, translucent), two-sided (`MtlFlags` 0x2),
  the alpha cutoff, the color factor, the base color texture, and the normal texture. The key of a record is `m_` and
  10 hex digits of the hash of its rules: two meshes with the same rules use one material. Each record is one
  `MaterialInstanceConstant` of a base material. The look was not a goal until 2026-10-18 (no specular, gloss, or emissive map); now world fidelity is a goal (docs/ROADMAP.md).
- Not drawn (`materials.not_drawn`): the shaders Nodraw and NoDraw (collision; both spellings are in the game data),
  GeometryBeam (light beams, fog spheres), shadow proxies, and decals (`%DECAL`: the game draws them over another
  surface). A mesh with nothing to draw is `empty` in the manifest (no asset, no placement). A masked material with
  an Opacity map has that map as the alpha of its base color.
- Static mesh: a glTF with no texture; each glTF material is a key. The pipeline `P_SwcMeshNanite` (a copy of the
  glTF pipeline of the engine) makes no material and no texture; the pipeline `SwcMaterialLinkPipeline` (C++, editor
  module) after it gives each slot the material instance `/SwcGameContent/Materials/KEY` before the build. Two rules
  found in stage 1.2: a material that a script sets after the import makes Unreal build the mesh a second time (a
  large merged mesh then took about 30 s); and `InterchangeManager.import_asset` does not save: the script saves each
  batch of 16 meshes (a save waits for the builds of the batch, which run at the same time) and then collects the
  garbage. A mesh with a translucent (glass) material has no Nanite (`P_SwcMesh`): Nanite draws only opaque and masked
  materials. The world meshes have no mesh distance field and no Lumen cards (see the import time below).
- Skeletal meshes with their clips (characters, creatures) use the default glTF pipeline of the engine (kind
  `character`): one folder for each character, with its skeleton, meshes, physics asset, and animation sequences.
- Each primitive of a glTF has only the vertices that it uses (before stage 1.2, each primitive had all vertices of
  its chunk).

Why three base materials and not one: the blend mode is a property of a material. A material instance can override
it, but an override gives that instance its own shaders (a static permutation). Three parents let all instances of
one blend mode share the shaders of their parent.

Asset counts (the exit check of stage 1.2):

| Content | Rule of stage P0 (textures in each glTF) | Shared rule (stage 1.2) |
| --- | --- | --- |
| Proof area of stage 1.1 (757 meshes), measured | 2,725 textures, 1,466 material instances | 449 textures, 253 material instances |
| Region `r_+04_+02` (3,148 meshes) | about 18,212 textures, 8,672 material instances (computed from the census) | 1,216 textures, 716 material instances (imported) |

## The incremental pipeline

- Converter: `mesh.VERSION` is the rule of the static meshes (6 in stage 1.3), `materials.TEXTURE_VERSION` the rule
  of the shared PNGs (2). The manifest entry of a mesh has the
  version and the hash of its glTF. `tools/convert.sh world NAME...` (or `--playable`, `--all`) converts only the
  meshes that are missing, have an older version, or are forced (`--force`), in 4 processes, and writes the manifest
  one time. A texture PNG exists one time for each cap. The game copy does not change.
- Import: `SwcGameContent/import-state.json` has, for each imported asset, the hash of the glTF (a mesh), the size and
  time of the PNG (a texture), or the rule version of the script (a material). `tools/import.sh` imports only the
  assets that are new or changed. `--regions NAME...` selects the meshes of regions and their materials and
  textures.

```sh
tools/convert.sh census                  # the census of the 49 regions
tools/convert.sh world r_+04_+02         # the meshes of regions, shared textures and materials (--playable, --all)
tools/import.sh --regions r_+04_+02      # only the new or changed assets of these regions (one call)
tools/import-world.sh --all              # all regions in calls of 8 minutes; the lock is free between two calls
```

## The disk budget (stage 1.2)

`tools/convert.sh budget --ddc-bytes N` measures the unit costs on the imported regions (the plugin assets, their
glTF and PNG files, the triangles and texels of the census) and scales them to the census of the 49 regions
(`content/census/budget.json`). Measured on region `r_+04_+02` (3,148 meshes, 18.8 million triangles, 1,216
textures, 716 material instances), 2026-10-18:

| Unit | Value |
| --- | --- |
| Mesh asset (plugin) | 46.7 bytes / triangle |
| glTF + bin (converted) | 57.8 bytes / triangle |
| Texture asset (plugin) | 1.22 bytes / texel |
| PNG (converted) | 1.09 bytes / texel |
| Texture cache (DXT1 0.5, DXT5 and BC5 1 byte / texel, x 4/3 for the mips; computed) | 1.27 bytes / texel |
| Material instance | 4.5 KB |
| Mesh cache (the growth of the derived data cache during the imports; an upper bound: the cache is shared with the other tracks, and some test builds had distance fields) | 105 bytes / triangle |

Estimate for the world (177.1 million triangles; 3.57 G texels of base color and normal maps at 1,024):

| Part | GB |
| --- | --- |
| Converted content: glTF | 10.2 |
| Converted content: PNG | 3.9 |
| Converted content: placements (census) | 0.3 |
| Plugin: meshes | 8.3 |
| Plugin: textures | 4.3 |
| Plugin: materials | 0.0 |
| Derived data cache: meshes (upper bound) | 18.6 |
| Derived data cache: textures | 4.5 |
| Total | 50.1 |

The world level of stage 1.3 (the Landscape actors, the cell actors, the streaming data) adds to this. 235 GB were free
on 2026-10-18 after the region import; the limit of the project is 150 GB of new data, with 50 GB free at least.

Import time: Interchange builds each mesh (Nanite). With a mesh distance field and Lumen cards, the build of 20 meshes
took 80 s; without them, 14 s. The world meshes have no distance field and no Lumen cards (pipeline version 4).

## The open world level (stage 1.3)

One World Partition level: `/SwcGameContent/World/NewWorld` (in the shared plugin, not in git). It is the default
game map of the client (`GameDefaultMap` in `Config/DefaultEngine.ini`): a development run and a player run start on
it.

```sh
tools/convert.sh level --all             # Landscape heights with the cookies, macro color maps, cell files
tools/build-world.sh --import --all      # each region: import its content (tools/import-world.sh), then add it to the level
```

- Terrain: one Landscape for each region (`converter/swc_convert/terrain.py`): 8 x 8 components of 255 quads, 2,041 x
  2,041 vertices over 2,048 m (a quad of 100.39 cm), from the 2,049 samples of the region and its shared edge
  (bilinear). Z scale 128 x 204800 / 65535 = 400.006 cm: one step of the Landscape is one step of the source
  (2048 / 65535 m), and the actor is at Z = 1,024.02 m. The heights have the terrain cookies of the capitals: the rule
  of the client in the vendored `navcore cookies` (`converter/vendor/soulwarden/navcore.cpp`, compiled with
  `-ffp-contract=off`). Check: at the place of the development character (8880, 4250) the terrain is 138.31 m, and the
  backend has the character at 138.50 m.
- The Landscape proxies (2 x 2 components) are always loaded (not spatially loaded): the terrain to the horizon, with
  the Landscape LOD. The material `M_SwcLandscape` shows the macro color map of the region (one instance for each
  region).
- Placements: `converter/swc_convert/cells.py` writes the instances of each region in Unreal space (the map of stage
  1.1) by cells of 256 m: one actor for each cell with one instanced static mesh component for each mesh. Region
  `r_+04_+02`: 216,456 instances, 64 actors, 12,709 components. Effect cards (`fx_*`) and the meshes that are not in
  the game copy are left out.
- Streaming: the main runtime partition of the level has cells of 256 m and a loading range of 1,024 m. The sun, the
  sky, the sky light, and the fog are always loaded.
- Exposure: an unbound post-process volume (always loaded) sets a manual exposure, bias -1. With the auto exposure of
  the game, the ground near the character was white: the sunny ground is the brightest surface (in the unlit view it
  had the macro color). At bias 0 the ground was still pale. The look was not a goal until 2026-10-18: engine sun, sky, and fog; now world fidelity is a goal (docs/ROADMAP.md).
- A new converter rule can make a mesh empty (mesh rule 6). `build_world.py` then removes the components of these
  meshes from the cells of the regions built before (`USwcEditorLibrary::RemoveInstanceComponentsOf`; 25 to 47
  components for each region).
- The cells again (`tools/build-world.sh --cells`, when `CELLS_VERSION` of `build_world.py` changes): a call destroys
  the old cell actors of a region in one step, then makes the new ones in chunks of 8 actors
  (`USwcEditorLibrary::SpawnInstanceActorRange`), each with the time check. Measured in the final pass (2026-10-18):
  - The reader loads a mesh, its collision mesh, and its override materials only when an actor of the chunk uses it.
    Before, each call and each chunk loaded all the meshes of the region (3,448 in `r_+01_+02`), and the count step
    took 6 to 9 minutes.
  - The cells calls run with `-nullrhi`. With the renderer, each chunk compiled the shader maps of the new material
    instances (106 in one call): 8 dense actors took 201 s, with `-nullrhi` 51 s. The Landscape step keeps the
    renderer (its edit layers use the GPU).
  - A component gets all its instances before its registration. Each call logs the count of components with a first
    instance away from its place in the file (0 in the pass).
  - The bounds: while the mesh of a new component compiles (async), its bounds stay at the world origin
    (`UInstancedStaticMeshComponent::UpdateBounds` waits). The descriptors of that session then show actors with
    bounds from the cell to the origin. The saved actors have the right bounds. `build_world.py` writes the saved
    bounds of the cell actors of earlier calls to `~/Games/soulwarden-client/logs/cell-bounds/REGION.json`: the
    largest distance of a bounds center from its cell center is 85 to 134 m, with or without the renderer (14
    regions).
- The client (`Source/SoulwardenClient/World/`): the character has a World Partition streaming source. Before the
  spawn of the character, `SwcWorldStreaming::IsReadyAt` puts a streaming source at the place of the character from
  the server (a sphere of 80 m, the highest priority: only the cells at the place, not all the cells in the loading
  range of 1 km) and waits until those cells are loaded and a line from 2 m above the place down 50 m hits a ground
  near the place. The place of the server is the authority: its Z is about 0.5 m above the feet. A ground near the
  place is at most 1.5 m below it. A ground farther below (for example the Landscape 2.06 m below the floor of the inn
  of Oxboro) means that the cells of the floor are not there yet: the wait goes on, and the character does not fall.
  When the cells are loaded and the place still has no ground near it 3 s later (no ground at all, or only a ground
  far below: a place in the air), the wait ends: the character drops to the ground below, or the player code uses its
  test floor when the line hits nothing. Example: the development route `90 travel 9230 4168 116.0` is 4.43 m above
  the Landscape with no mesh there; before this rule (2026-10-19) that wait went on to the limit. The limit is 90 s
  for cells that do not load. While it waits, the client logs a line each 5 s: the state of the cells and the first
  surface below the place. Then
  the player track uses that ground (no test floor), and the input reports carry the position. The spawn code that
  calls the wait (`SwcBackendSubsystem`) is shared with the player track.
- `-SwcGroundTest=X,Y,Z;...` (engine m, no backend) runs that wait for each place and logs the time and the ground.
  Measured with the region import beside it (2026-10-18): the place of the development character 4.6 s, ground =
  the Landscape at 138.24 m (with the sphere of the whole loading range: more than 30 s); a region not in the level
  yet: no ground after 3.0 s.
- In a development run (the network namespace: only the file store of the derived data cache, see above), measured
  after `tools/warm-cache.sh` (2026-10-18): the cells of the place load in 13 to 17 s, then the character spawns on
  the ground. The other cells in 1 km load in the background in about 40 s while the client runs at about 27 frames
  per second. Before the warm-up, the cells did not load in 30 s. The pictures of `-SwcFrames` slow the client to
  about 9 frames per second (then the cells of the place do not load in 30 s): for a walk video, capture the agent
  desktop (`omarchy-agent-desktop screenshot`, about 0.3 s for each picture, no load on the client).
- The far meshes of the game (`impostors/`, `poi_impostors/` of each region: one mesh for each cell of 128 m) are
  groups of 512 m (`terrain.impostors`, 767 groups, 68.5 million triangles), always loaded, with the tag
  `SwcImpostor` and no collision. The client hides a group while its center is within the loading range of the view
  (USwcWorldSubsystem): they fill the distance where the cells are not loaded. They have no normal map: the normal
  maps of the impostor atlases are not tangent-space maps (the far meshes were black under the sun with them).
- The ocean is a flat plane at 40 m with no collision.
- `-SwcFlythrough=PLAN.json` (USwcWorldSubsystem, ASwcFlythrough): a camera flies along a path (engine points), with
  a streaming source; it logs the frame rate and the streaming each second, can write a picture every N frames, and
  ends the client with a summary line. `terrain.flythrough_plan` writes a plan from a route.

## The loading (AST-203, 2026-10-20)

The work of the priority of the user (2026-10-20: "prioritize optimizations on loading terrain"), with the rule of the user of the
same day: when a loading screen ends (the world entry, a travel, a respawn), all content near the character is there and
visible; no object appears, changes its mesh, or changes its material in view after it.

The measure (development runs of Devwarden, one Unreal process at a time; the tools in `~/Games/soulwarden-client/work/world`:
`entry.sh`, `entry-video.sh`, `timeline.py`, `export.sh` (an Insights trace exported with no window), `cold_cache.py` (the page
cache of our files dropped with `posix_fadvise`, no root)). The world entry from the process start to the spawn, before: 94.9 s
(the first run after a restart): the start to `LoadMap` 9 s, `LoadMap` 26.7 s, the wait for the Asset Registry 23.2 s, the
start of the environment and the UI 8 s, the login 4 s, the cells of the place 23.3 s.

The causes and the changes:

| Cause | Change | Gain |
| --- | --- | --- |
| The Asset Registry: the editor binary in -game scans all 76,197 files with no cache (-game reads a cache, never writes one); the engine init waits for it | `-AssetGatherAll=false` (`SWC_CLIENT_ARGS` of `tools/env.sh`; the client code makes no registry query) | 23 s |
| Zen: in its network namespace a client finds the Zen server of a host commandlet but cannot reach it (30.1 s wait) | the client DDC graph with no ZenLocal (`SWC_CLIENT_DDC`; the file store has the data) | 0 to 30 s |
| The async loader: an uncooked client has no loading thread; it loads only in its slice of the game thread | 300 ms in each frame during the loading screen (`SwcBackendSubsystem`, from 50 ms) | the wait 24.9 -> 19.6 s |
| The start screen of the UI ended when the controller had any pawn: the engine spawned a default pawn at the origin, 25 s before the character (the user saw the world load) | no default pawn (`SwcGameMode`) | the screen ends at the spawn |
| The content systems loaded after the screen, near the camera (at the origin before the spawn): the ground cover, the persons, the creatures, the surface layer, the water surfaces, the collision chunks, the time-of-day blend of the environment, and the engine work (shader jobs, assets that build, textures that stream) | the loading gate (below) | the late content within 150 m in the 10 s after the screen: 1 -> 0 (see the check) |
| The ground cover built two batches in one update (a frame of 2.4 s) | a time budget (4 ms in play, 50 ms during a screen) | the longest frame in the 5 s after the spawn: 1.0 to 2.6 s -> 0.17 s |
| The camera stayed at the origin of the world (the world entry) or at the old place (a travel) during the screen: the eye adaptation, Lumen, the sky capture, and the fog started at the place only at the spawn, and the streaming source of the player loaded the cells about the origin | a camera at the place during the screen and the light check (below) | the mean luma of the video from 0.5 to 10 s after the screen: before -25 % (p1, day) and +10 % in the first 2.5 s (gate3); after within 2 % (the shrine) and 3 % (the road start); the exposure after the screen within 0.07 to 0.25 EV |
| The far meshes hid by their distance alone: bare ground up to 1 km until the cells came | a group hides when the cells under it are in the world | the cells within 300 m / 500 m / 1,000 m in the world after the screen: before 17.9 / 35.7 / 50.5 s (the sprint); after 3.9 / 5.2 / 8.3 s (the world entry), 1.9 / 3.6 / 6.0 s (the shrine), 0.2 / 1.9 / 7.9 s (the travel) |
| The wait of a travel had the 5 ms slice of the engine | 300 ms in each frame during the wait of a travel or a respawn | the travel of 426.7 m: 9.5 s -> 3.7 s (with the cells of the entry place already loaded) |

The loading gate (`World/SwcLoadingGate`, `SwcWorldStreaming::IsReadyAt`): during a loading screen the place of the character is
the view of the content systems (`SwcLoadingGate::GetView`), so they load in parallel with the cells. Each system registers a
check "ready near this place" (`SwcLoadingGate::Register`): the ground cover (its batches within its range plus 16 m: the camera
arm and the first steps; without it a travel built one batch at 100 to 142 m 0.6 s after the screen), the persons (the
visible persons within 150 m have their actor), the creatures (the creatures within 150 m show their body), the surface layer
(the roads within 300 m, the decals within 150 m), the water (the surfaces within 150 m), the collision chunks (the batches
within 150 m). The wait ends when the cells within 150 m are loaded (a streaming source of 150 m, the highest priority), the
place has ground, every check is ready, and the engine work is done (`SwcLoadingGate::EngineReady`: no shader job, no asset
that builds, no texture that streams; at most 20 s more). The objects (the gathering nodes) already build at the place of the
server before the spawn. The environment selects its time-of-day volumes at the place of the screen with no blend from
another place.

The light (the rule of 2026-10-20: the light is settled when the screen ends). Before, the camera stayed at the origin of the
world during the screen of the world entry (no character yet) and at the old place during a travel, so the eye adaptation,
Lumen, the real-time sky capture, and the volumetric fog started to work at the place only at the spawn (F5: the beach was dark
at the spawn and much brighter 10 s later). Now the streaming source actor of the wait has a camera at the place (3.5 m behind
and 2 m above the feet, along the facing of the server, 10 degrees down), and it is the view of the player during the wait (a
travel: from 1 s after its start, when the loading screen of the UI shows), with an eye adaptation of 20 EV per second (the
camera settings; the engine default is 3 up and 1 down). The wait also needs 12 frames at the place with an exposure within
0.05 EV (`SwcLoadingGate::GetLight`: a scene view extension reads the exposure and the mean scene luminance of the game view;
at most 10 s after the content; the rule "3 s after the cells with no ground near the place" also waits for the engine work and the
light: a travel ended 3.7 s after the cells while the collision of its floor still built, and the character fell 1.62 m). The
source of the player controller is at that camera too: the cells of its loading range
start to load at the place behind the screen. During the wait of a travel or a respawn the loader also has 300 ms in each frame.

The results (development runs of 2026-10-20, `work/world/runs.sh`; late content within 150 m in the 10 s after the screen):

| Run | Wait of the screen | Late content | Notes |
| --- | --- | --- | --- |
| The world entry at the road start (`lv-entry-c1`) | 32.7 s (74.5 s from the process start; gate3 81.9 s) | 0 | the light settled in 102 frames at the place |
| The world entry through the front end, PLAY (`lv-front-c1`) | 27.9 s | 0 | the same gate and light as `-SwcPlay` |
| The world entry at the Shrine of the Lion (`lv-shrine-c1`) | 43.2 s | 0 | see the shrine below |
| The long travel of 426.7 m (`lv-travel-c1`) | 3.7 s | 0 | |
| The 60 s sprint along the road (`lv-road-c1`, 75 s window) | 44.8 s | 7 | all 7: creatures of a kind not in memory (below); frames p50 12.8 ms, p95 23 ms |
| The world entry with `-SwcGateRing=300` (`lv-ring300-c1`) | 43.8 s | 0 | +11 s for the cells within 300 m, which come 3.9 s after the screen without it: not used |

The far meshes (`USwcWorldSubsystem`): a group of 512 m within the loading range of 1,024 m hid by its distance alone, so the
ground there was bare until its cells came (the cells within 300 m were all in the world 17.9 s after the screen, within 500 m
35.7 s, within 1,000 m 50.5 s: the sprint run). Now a group hides when the cells under it are in the world (a World Partition
query of the part of the group within the range), at once when it is within 256 m of the view. The view is the place of the
screen during a screen.

The check (`World/SwcLateContentProbe`, `-SwcLateProbe[=SECONDS]`): at the end of each loading screen it takes the visible
primitive components within 400 m of the character, then for 10 s logs each component within 150 m that becomes visible, changes
its mesh, or changes a material ("late: ..."), and the engine work at the start and the end ("late content: N", 0 is the target).
A new component beyond its draw distance from the camera (or the end cull distance of its instances: the ground cover culls at
100 m) draws nothing and does not count; when the character comes near it, that is the draw distance, not a late load. The
distance of an instanced component is the one of its nearest instance (the bounds of a new instanced component are a coarse
sphere; some included the origin of the world). A new creature or effect of the server is an "entity": the server sends it at
its interest radius (`interestRadius` of the creature data); it is listed, not counted. A creature that shows after its record
(its assets load) counts. The probe also logs the light each second ("late: light": the exposure and the mean scene
luminance) and the times when the cells within 300 m, 500 m, and 1,000 m are in the world.

The Shrine of the Lion (9716 1881, region r_+04_+00; a fact of the UI track: its cells did not load in 44 s, at the dry sand
in about 12 s). The region was not in `warm-state.json` (13 of the 49 regions were warmed on 2026-10-18), so the first entry built
the derived data that the file store did not have (79 textures and about 850 assets in the first 26 s of the wait). The warm-up
of r_+04_+00 (`tools/warm-cache.sh r_+04_+00`): 6 calls, about 45 min, and the file store grew by only 68 MB (most of the data was
in it from earlier runs there; the new work of the commandlet was the editor shader maps of the materials, in Zen, which the runs
cannot read). Thus the warm-up of the other 36 regions (7,521 new meshes, some hours) was not done: the first run in a region
fills the file store. The entry after the warm-up (`lv-shrine-c1`): the cells within 150 m were in at about 40 s (the engine still
built 52 assets and 43 shader jobs at 21 s: the meshes and the materials of the region for the client), then the place had no
ground within 1.5 m (the platform had no collision: its collision mesh was deleted, see "The collision of the game", AST-206), and the wait ended 43.2 s after its start.
`warm_cache.py` counts its limit of 300 s from the end of the level load (the level load alone took more
than 300 s in a new checkout, and then each call warmed one batch of 200 meshes).

The usage flags (`tools/fix-usage-flags.sh`, `Scripts/usage_flags.py`): a material with no usage flag for its mesh is drawn with
the default material of the engine ("missing usage flag Nanite! Default Material will be used in game"). The tool reads these
lines of the client logs, sets the flags on the base materials, and saves them (the world lock and the import lock). The client
logs of 2026-10-19 and 20 had 24 materials with no Nanite flag. The 2 world materials are instances of `M_SwcGlass`: its flag is
set and saved. The 22 weapons and tools of the character (the Interchange import of AST-202; the spear of the development
character too) are instances of the Interchange glTF master `/InterchangeAssets/gltf/M_Default`, a content of the installed
engine that the editor does not save. Since AST-206 the player import moves them to the project copy `M_Default_Weapon` with the
Nanite and skeletal mesh flags (docs/GEAR.md "The weapon materials").

Open points: a creature of a kind that is not in memory shows 0.2 to 1.9 s after its record (its assets load async; a preload of
the creature kinds of a region needs their list in the client data); the
cells within 150 m are now the longest part of the wait (48 s in the first gated run: the uncooked meshes of the
cells build from the DDC); the always-loaded Landscape of the 49 regions is about 12 s of `LoadMap` (a second runtime grid
moves it out of `LoadMap`, but the visible terrain must still load before the screen ends, so the gain is only the overlap with
the login; not done); a cooked build removes the registry, the World Partition streaming generation (12 s at each start), the
editor PostLoad, and the DDC builds (the estimate is the next step).

## The collision of the game (P4.1 part 1)

The game has the collision of a mesh in its physics file: the path of the `.cgf` with `.rnr`. It has boxes, spheres,
cylinders, capsules, convex hulls, triangle meshes, and compounds of them. The reader is the reader of the Soulwarden
navigation tools (`converter/vendor/soulwarden/nav_shapes.py`, unchanged). 7,700 of the static meshes have a physics
file (2.5 million triangles). A mesh with no physics file (plants, cloth, books, small props) has no collision in the
game.

```sh
tools/convert.sh collision                # content/collision/PATH.swccol for each mesh with a physics file
tools/import-world.sh NAME...             # import_content.py section 6 makes the collision meshes (COLLISION_VERSION)
tools/build-world.sh --cells NAME...      # new cell actors with the collision instances (CELLS_VERSION)
```

- `collision.py` writes the triangles of all the shapes of a mesh in the space of the mesh (engine meters, the same
  space as its glTF: the bounds of 298 of 300 sampled meshes are in the bounds of the render mesh).
- Section 6 of `import_content.py`: the render mesh gets no collision (no simple shape, no complex collision mesh).
  A collision-only static mesh `/SwcGameContent/Collision/FOLDER/C_NAME` (no Nanite, no distance field, no Lumen
  cards, no ray tracing, never drawn) uses its own triangles as its collision (UseComplexAsSimple).
- The cells of the level (`SwcEditorLibrary::SpawnInstanceActors`): each render component has no collision; a mesh
  with a collision mesh gets a second component with hidden instances of it at the same transforms (tag
  `SwcCollision`). The creature track's layer finds the instances of a place in both (SwcLevelInstances).
- Faults found on the way (the rules that came from them):
  - A render mesh with a ComplexCollisionMesh crashed: its async post-load cooked the body on a cache miss while the
    other mesh still compiled (`check(HasValidRenderData())` in GetPhysicsTriMeshDataCheckComplex). Thus no render
    mesh has a ComplexCollisionMesh.
  - The key of a cooked body is `BodySetupGuid`: a cook before the build of its triangles was empty, and the cache
    kept it. The cook waits for the build of the collision mesh.
  - The corners of a triangle keep their order (with the mirror of the space map, the front still faces out). The
    complex collision ignores the back of a triangle: with the order turned, a line down went through each top face.
  - A new build of an existing collision mesh kept its old triangles: section 6 deletes the old asset first.
  - The save of a commandlet keeps the files of destroyed external actors (the next load had them again):
    `build_world.py` deletes the files of the cell actors that it destroys.
  - The record `"collision"` of a mesh in the manifest is the input of section 6: with no record, section 6 deletes the
    collision mesh (state `none:6`). Only `collision.py` writes the record. A new conversion of a mesh replaced its whole
    entry (`manifest.add_many`), so the record went away, and the next import deleted the collision mesh. The cells keep the
    path of the deleted asset: the place lost its collision with no change of a cell or a level file (AST-206). Thus
    `manifest.add_many` keeps the fields of `KEPT_FIELDS` ("collision") of the old entry when the new entry has the same kind
    and does not have them (`tests/converter/test_manifest_fields.py`). After a mesh conversion, `tools/convert.sh collision`
    is not necessary for the record; run it when a physics file changes.
    The count (2026-10-20, before the repair): 5,599 static meshes with a collision file had no record. 115 of them had their
    collision mesh deleted already (an empty folder in `Collision/`; 126,504 placements in 27 regions, for example
    `jav_struc_ruins_column_c_cap_ai`: the platform of the Shrine of the Lion and the stone at the travel place 8525.9 4540.0;
    pirate planks; ruins arches). The other 5,484 still had their collision mesh: the next import of their region would have
    deleted it. The repair: `tools/convert.sh collision` (all records again), then `tools/import.sh --keys KEYS --world` with the
    115 keys (section 6 made the 115 collision meshes again, at the same paths: the cells did not change). A check of the
    state: no folder of `Collision/` is empty, and each static mesh with a collision file has a record and the import state
    `HASH:COLLISION_VERSION`.
    The check (development runs, evidence `~/Games/soulwarden-client/evidence/AST-206/`): the world entry at the shrine
    (`ast206-shrine-c1.mp4`) lands at 42.47 m on the platform (cell 5 7; before: 41.39 m, the Landscape); the travel of 426.7 m
    (`ast206-travel-c1.mp4`) lands at 200.475 m on the stone (`ISM_189` of r_+04_+02 cell 1 1, as on 2026-10-19 23:49; before:
    1.62 m lower). `tools/convert.sh collision-chunks` again gave the same 93,437 colliders: the chunks of these meshes were
    written before the deletion.
- Check: the ground test (`-SwcGroundTest`) logs each static surface on its line with its mesh. At the Inn of Oxboro
  (r_+04_+02): the floor 139.89 m (C_everfall_floor_9x15_a), the foundation 139.88 m, the porch 140.14 m.
- The collision chunks (AST-201, 2026-10-20). The game merges the render meshes of a part of a region (CoATGen) into the mesh
  chunks of `coatgen/HASH/NAME_mesh_chunk_X_Y_Z` (merged meshes and instanced meshes). A merged .cgf has no physics chunk and no
  .rnr: the collision of the part is its collision chunk `NAME_collision_chunk_X_Y_Z` (chunkType 2 of `region.chunks`), one
  collider for each piece, the physics file of its mesh at its place. `region.py` skips these chunks (no render mesh), so the
  merged structures (decks, stairs, house walls, fences) had no collision: the character walked on the ground under the stairs of
  a deck, the planks at its hips. 30 regions have 1,174,821 such colliders of 5,741 meshes; 5,022 of the meshes have a
  collision mesh of section 6 (1,096,001 colliders). Most colliders are also a placement of the level (the instanced pieces):
  the cells have their collision.

  ```sh
  tools/convert.sh collision-chunks [REGION...]   # content/regions/NAME/collision-chunks.bin (no import, no cell build)
  ```

  `converter/swc_convert/collision_chunks.py` writes the other colliders (r_+04_+02: 7,240 of 47,559, 463 meshes; 39 colliders
  of meshes with no collision mesh, 162 primitive shapes are not in it yet). The client places them at run time
  (`World/SwcCollisionChunks`): each batch of 128 m within 200 m of the character gets one hidden instanced component for each
  collision mesh, with the settings of the collision instances of the cells (BlockAll, tag `SwcCollision`). The batches within
  160 m load their meshes at once, so the collision is there at the world entry; `-SwcNoCollisionChunks` turns it off. Later,
  the cells can take these colliders (a cell build of the world track).
- During a loading screen the batches build at the place of the character, and the screen ends only when the batches within
  150 m are built (the loading gate of the world track (docs/WORLD.md "The loading")). Open point (a cell build): move these colliders into the cells, also the
  1,094 colliders of meshes with no collision mesh and the 3,793 primitive shapes that the chunks do not place. The 1,094 are not
  the fault of the manifest record (AST-206): their meshes have no entry in the manifest (they are only in the merged chunks, for
  example `everfall_living_story_pillar_a` and the collision-only `barricade_*_col`), so no collision mesh is made for them.

## Gathering nodes in the level (P4.1)

The level shows only the healthy mesh of a gathering node. The creature track shows the depleted mesh at run time when
the server says that a node is depleted, and hides the healthy instance (SwcLevelInstances).

- Rule 1: the master slices of `slices/gatherables` name the entity of the depleted mesh `DepletedMesh` or `Depleted`
  (both entities of a node have Visible = 1). `region.slice_meshes` leaves them out.
- Rule 2: the variation tables (VariationData: StartMesh and DepletedMesh of each variant, 432 pairs). A DepletedMesh is
  not placed where the StartMesh of its row stands (within 0.1 m); for example the BaseMesh of the alchemy nodes.
- A distribution row places the meshes of its variant (`region.slicedata`, as a capital does), not the meshes of the
  master slice: before, each master_bush row showed honeysuckle_a.
- The world (49 regions): 3,545,784 placements in place of 5,890,959. In r_+04_+02: 137,786 in place of 221,103;
  53 single stumps and fragments with no healthy mesh at their place stay as decorations.
- A placement goes into the cell of its own place (no clip; 0.11 % of the placements are more than 200 m outside the
  square of their region).

## Material overrides of the placements (P4.1)

A placement can replace the material of its mesh: the field `Material Override Asset` of the mesh render node of a
slice entity, or `materialOverrideAssetId` of a mesh of the slice metadata (a variant). Near Oxboro the fall variants of
the poplars and the pawpaw bushes select their fall materials, and the lanterns select their on and off materials. The
override replaces all the sub-materials of the mesh, by index. The asset id of the metadata is a struct (guid, subId).

```sh
tools/convert.sh census --force          # placements.npz: the override of each placement (materials, material)
tools/convert.sh overrides --all         # the slot maps of the pairs (mesh, override), their material records
tools/import.sh --keys ~/Games/soulwarden-client/content/overrides/material-keys.json   # the override materials
tools/build-world.sh --cells --all       # cells version 4: the groups by (mesh, override), the slot materials
```

- `overrides.py`: the slots of an imported mesh have the names of the material keys of their sub-materials. For a pair
  (mesh, override), each slot gets the key of the override sub-material with the same index (the current material
  rules). Two sub-materials of a mesh with one key are one slot; when the override gives them two keys, the first one
  wins. r_+04_+02: 1,000 pairs, 490 change a slot, 2 merged slots, 23 errors (a mesh or a material that is not in the
  game copy).
- `cells.py`, file version 2: an entry is a pair (mesh, override) with its slot list; the spawn sets the component
  material of each slot (`/SwcGameContent/Materials/KEY`). The world: 8,476 entries with an override, 587,188 instances.
- The materials track owns the material rules (materials.py, the parent materials, the instance parameters): its
  instances change in place, with no new cell build.
- The cell step of a region has two calls: the destroy of the old cell actors, then the spawn (about 5 minutes for a
  large region with the collision and the material slots).

## Instanced mesh components of the slices (S0, research/settlements.md)

A slice entity can have an instanced mesh component: its `Instanced Mesh Render Node` has the fields of the static node
in its `BaseClass1` (`Visible`, `Static Mesh`, `Material Override Asset`), and its child `Instance Transforms` has the
transform of each instance in the space of the entity. `region.slice_meshes` places each instance with the entity world
matrix times the instance transform, and keeps the entity id and the instance index (placements.npz: `entity`,
`instance`). The walls, fences, gates, and poplars of the settlement lanes are such instances (the static startup
children of the settlement slices). At the spawn lane of Oxboro: 69 new instances in the 30 m box (40 fence parts, 4
gates, 2 poplars, and more); the south fence (fence_a_400 instance 11 and fence_b instance 10 at one origin), the north
gate (iron_gate_a instance 6), and the west poplar (poplartree_full_a instance 41, fall01) are at the positions of the
research (0.0004 m or less). r_+04_+02: 193,980 placements, 56,194 of them instances. Cells version 5.
The five house files at the lane are parts of one house: all of them stay. The cobblestones are road splines (the
materials track).

## The wind flag and the decal companions (cells version 3, step d)

- The wind: the mesh render node of a slice entity has the bool `receiveWind` in its `Render Options` (an instanced
  node: in its `BaseClass1`; the field id does not depend on the case of the name). `region.slice_meshes` keeps it,
  and placements.npz has the array `wind`. All 49 regions: 337,636 of 5,190,540 placements; Oxboro: 8,040 of 193,980
  (mostly ground plants and bushes). The variant meshes of the slice metadata and the distribution rows have no field
  of this name: no wind (a variant mesh has two fields with no known name, `0x755c7d72` a bool and `0x9ae4b28c` a
  u32; they are not used).
- `cells.py` groups the instances by (mesh, override, wind). The component of a group with wind gets the custom
  primitive data 0 `SwcWind` = 1 (`SetDefaultCustomPrimitiveDataFloat`) for the wind of the vegetation material (E1).
- The decal companions (the materials track, mesh rule 7): Nanite does not draw deferred decal materials, so the
  `%DECAL` subsets of a mesh are a small non-Nanite mesh of their own. The manifest entry of the main mesh names it
  (`"decals": "decals:MESH"`). Each group of such a mesh is followed by a group of its companion with the same
  transforms: one more component in the same cell actor, with no slot map, no wind, no shadow, and no collision.
- File version 3: each entry has a flags byte (bit 0 the wind, bit 1 a decal companion). The reader reads versions 1
  to 3. Check of the converter on `r_+04_+02` (a temporary folder): the census gives the same arrays as before and
  the new `wind`; the version 3 file has the same instances of each mesh in each actor as the version 2 file (64
  actors, 188,681 instances; 175 entries with wind, 8,021 instances).

## The terrain material of the game (world fidelity T1)

```sh
tools/convert.sh terrain-layers --all   # T0: the layer lists, the splats (2,048 and 2,041 grids), the macro maps, the textures
tools/convert.sh terrain-plan --all     # the plan of each region and the slices of the texture arrays
tools/import-terrain.sh                 # the arrays, M_SwcTerrain, and MI_SwcTerrain_REGION of each region with layers
tools/build-world.sh --terrain NAME...  # the Landscape of a region again, with one target layer for each slot
```

- The game composes the layers of a region into its terrain tiles (`TerrainTileCompositePS`): the diffuse, gloss,
  specular, material height, and normal of each layer material; the macro color overlays the diffuse
  (`OverlayBlend`, after its saturation changes toward its luminance), the macro gloss overlays the gloss, the macro
  normal adds to the detail normal; the alpha of a layer is the height blend of its splat
  (`GetTerrainMaterialLayerBlendingValue`). The splats are independent weights (no normalization).
- The draw order is the list order of the region material, not the Priority: at the bed of the highland river of
  Oxboro the gravel splat (Priority 17) is 1.0 and the lake layers (Priority 10 and 15) come after it in the list. In
  the order of Priority the gravel covered the river.
- `converter/swc_convert/terrain_material.py`: the plan (`regions/NAME/terrain-plan.json`: the base material, then the
  active layers in list order as slots L00, L01, ...; their textures and parameters; the macro sets of the world
  material), the reference composite (numpy, the formulas of the game; the offline check), and the slices of three
  texture arrays (one slice for each of the 72 terrain materials, 2,048 x 2,048): albedo (sRGB), normal (BC5), masks
  (BC7: R gloss, G height, B specular).
- `M_SwcTerrain` (`Scripts/import_terrain.py`): the base material at alpha 1, then 57 slots (the largest list), each a
  Landscape layer switch around a custom node with the composite of the game; then the distance fade to the macro
  maps (500 m, a blend of 50 m: preset 7) and the Unreal outputs (roughness = 1 - gloss, specular = reflectance /
  0.08). A slot that a Landscape component does not have is pruned. The Landscape of a region has one target layer
  for each slot (`USwcEditorLibrary::CreateLandscapeWithLayers`, the layer infos `/SwcGameContent/Terrain/LayerInfo/LI_Lnn`
  with no weight blend: the raw weights).
- The color spaces of the upload of the source values are not traced. The instance has the readings as parameters
  (`Readings`: the decode of the diffuse and specular maps; `Macro Near Scales` w: the decode of the macro color map).
  The rule (the decision of the user, 2026-10-19): the reading that gives the retail ground colors in the matched
  views (the bottom band of the color report).
- Measured at Oxboro (2026-10-19), the bottom band of the color report, client against retail (CIEDE2000), before
  (the macro color map only) and after (the literal reading: all maps decoded from sRGB, the values of the game as
  linear): noon-road-low 25.3 -> 2.9 (L* 76.1 -> 49.9, retail 47.8), noon-wall-grass 15.0 -> 5.9, river-forest-noon
  42.1 -> 14.2, river-highlands-noon 24.2 -> 22.5; the mean 26.7 -> 11.3. The rest in the river views is the water
  (the clear water shows the bed) and the ground cover of the banks (T3).
- All 28 regions with a layer list have the layered Landscape (2026-10-19; 21 regions have an empty list: the macro
  Landscape stays). The bottom band of sets 1, 2, and 4 (11 noon views), the mean CIEDE2000 24.5 -> 15.5:
  noon-road-low 25.3 -> 5.8, noon-wall-grass 15.0 -> 4.4, noon-n 14.2 -> 16.0 (the retail ground there is the cobblestone
  road of the surface layer, S1), river-forest 42.1 -> 22.5, river-highlands 24.2 -> 24.3, ocean-clear 7.7 -> 11.8,
  ocean-storm 18.2 -> 19.4, cleave-outside 22.2 -> 9.9, cleave-inside 34.2 -> 13.8, fen-outside 39.8 -> 21.4,
  fen-inside 26.6 -> 21.0. The rest of the river, beach, and fen views is the water and the ground cover (T3).
- GPU memory: the three arrays are about 1 GB. Each client of this project needs about 6 GB of the 24 GB GPU; with
  other clients and a retail game process on it, a client stopped with "Failed to allocate Device Memory".

## The ground cover of the game (world fidelity T3)

```sh
tools/convert.sh cover NAME... | --all   # regions/NAME/cover.bin: the descriptors and the packed records (cover.py)
```

- `region.vegetation` of a region has the grass, ferns, leaf piles, and small plants of the game: a descriptor table
  (the mesh, the material override, AutoMerge, Bending / 100) and 128 x 128 tiles of 16 m with groups of packed records
  (research/terrain.md, section 3). Oxboro: 322 descriptors, 10.3 million records, 159 MB. All 49 regions: about 172
  million records, 2.8 GB. The cover stays packed on disk: no Landscape Grass, no new scatter, no world partition actor.
- The tile index is row x 128 + column; the row is the 16 m step north from the south edge of the region (measured:
  the records of tile 129 are at local x 22.8 to 24.8 m, y 16.8 to 30.9 m).
- `cover.bin`: the descriptor table in Unreal terms (the object path of the mesh, FOLDER/STEM/StaticMeshes/STEM.STEM;
  the override slots of `cover/pairs.json` of the materials track; the bending), then the bytes of
  `region.vegetation` unchanged.
- The client (`World/SwcGroundCover`, on by default since stage T3: `-SwcNoGroundCover` or `SWC_GROUND_COVER=0` turns
  it off; `-SwcCoverRange=M`, default 100 m): it reads the file of a region off the game thread (Oxboro 0.5 s), loads the
  meshes and override materials async, and makes the batches of 4 x 4 tiles (64 m) within the range, the nearest first
  (two in each update of 0.25 s): one instanced component (HISM) for each descriptor of a batch, with the source
  transforms (the rotation of the engine space as the cells: the quaternion (-x, y, -z, w)), the override materials,
  and the bending as the custom primitive data 0 "SwcWind". No collision. Not traced yet (T4): the shadows (off), the
  fade and the view distance of the source (the end cull distance is the range).
- Measured at Oxboro (2026-10-19): 16 batches, 786 components, 166,818 instances within 100 m. The cover stands where
  retail has it (the matched views). Its color is the point of the materials track (M_SwcFoliage: the back light of
  the source is multiplied by the diffuse color).
- `swc.GroundCover.Show 0` hides the cover and keeps its batches: one matched-view run gives each view with and without
  the cover (`tools/match-view.sh SET VIEW --variant VIEW@nocover='swc.GroundCover.Show 0'`).

### The T3 check (2026-10-19)

The matched views of sets 1, 2, and 4 in the light of E5 steps 1 and 2a (main c82e988), with M_SwcFoliage version 3,
each view with and without the cover in one run (`~/Games/soulwarden-client/evidence/T3/e5-set1`, `e5-set2`, `e5-set4`;
settle 45 s). In each view the batches were in place before the picture: Oxboro 24 batches, 1,200 components, 254,748
instances; the rivers 15 batches, 111,892 instances; the fen 21 batches, 141,031 instances.

The bottom band (the ground), CIEDE2000 and the L* difference to retail, with the cover / with no cover, and the
saturation (retail in the last column):

| View | dE with / no cover | L* with / no cover | Saturation with / no cover / retail |
| --- | --- | --- | --- |
| noon-road-low | 10.2 / 17.8 | −10.5 / −19.9 | 0.495 / 0.513 / 0.532 |
| noon-wall-grass | 10.3 / 6.8 | +12.0 / +7.9 | 0.535 / 0.576 / 0.588 |
| noon-n | 1.6 / 1.4 | −1.6 / −1.5 | 0.449 / 0.448 / 0.452 |
| river-forest-noon | 25.2 / 30.6 | +26.4 / +31.4 | 0.466 / 0.362 / 0.631 |
| river-highlands-noon | 28.7 / 29.4 | +33.3 / +36.2 | 0.392 / 0.196 / 0.458 |
| fen-inside-noon | 23.7 / 24.6 | +26.4 / +27.0 | 0.267 / 0.233 / 0.647 |
| fen-outside-noon | 10.5 / 7.3 | +13.2 / −1.1 | 0.450 / 0.293 / 0.515 |
| fen-outside-south-noon | 11.3 / 5.4 | +14.6 / +6.2 | 0.437 / 0.296 / 0.508 |

What the views show: the cover stands where retail has it in each view (the grass of the fen, the flowers and the tall
grass of the river banks, the grass of the Oxboro walls); with no cover the ground is bare terrain. The cover brings the
saturation of the ground near retail in seven of the eight views (river-highlands 0.196 to 0.392, retail 0.458). The
brightness: the river and fen-inside views are +26 to +36 L* with and without the cover (the light of the whole picture,
E5 step 2b is open). In noon-wall-grass and the two fen-outside views the cover adds 4 to 14 L*: the client grass is
lighter than the retail grass. The retail grass is darker near the ground; the client cover has no shadow and no terrain
color at the foot of the blades (T4 and the materials track: BlendTerrainCol of the source vegetation shader).

The cost (main 047009b with T3, the cover on by default, `evidence/T3/cost-main`): pairs at the end of one run, the
cover shown and hidden at the same place (`-SwcFrameStats=2`, the median of the 2 s windows while the view is in place).
noon-road-low: GPU p50 41.3 ms with the cover, 42.0 ms with no cover; noon-wall-grass: 42.5 and 43.2 ms; the frame p50
42.7 / 43.7 and 42.9 / 43.4 ms. Oxboro: 21 batches, 1,033 components, 215,825 instances within 100 m. Two other clients
used the GPU at that time (about 42 ms in each view, also with no cover): the cost of the cover is below that noise
(about 1 ms or less). An exact number needs a quiet GPU window.

The light grass (the remark of the user, 2026-10-19: "the colors still washed out"). The fen views with the light of E5
step 2b (main 5f83073), one run with test variants (`evidence/T3/2b-set4`), the bottom band L* (retail in brackets):

| View | The cover | No cover | The cover with shadows | The cover with no back light |
| --- | --- | --- | --- | --- |
| fen-inside-noon (27.4) | 53.1 | 55.3 | 52.9 | 53.6 |
| fen-outside-noon (21.2) | 34.5 | 20.2 | 34.5 | 30.4 |
| fen-outside-south-noon (14.2) | 29.8 | 21.1 | 29.7 | 25.7 |

What the source rules and the numbers show:

- The shadows of the cover (`swc.GroundCover.Shadow 1`): no change (0.0 to 0.2 L*).
- The back light (`swc.GroundCover.BackLight 0`: BackDiffuseMultiplier 0): 4.1 L* of the 14.3 and 8.7 L* that the cover
  adds in the two fen-outside views. The source grass of these places is `%LEAVES` (not `%GRASS`): its deferred light is a
  wrap light on both sides (shadelib `ThinTranslucencyBRDF`: T x saturate((-N.L + w) wn) + saturate((N.L + w) wn), T =
  saturate(BackDiffuseMultiplier x opacity) x TransmittanceColor), only from the lights; the ambient of the source has no
  back light. Unreal's two-sided foliage also passes the sky light from the back face through the subsurface color
  (`DiffuseIndirectComposite.usf`; with `r.Lumen.ScreenProbeGather.TwoSidedFoliageBackfaceDiffuse 0` it adds the subsurface
  color / pi to the diffuse color instead). In these views the camera looks north and the sun is to the north-west
  (elevation 47.8 degrees): the faces toward the camera are the back faces, and the back light makes most of their light.
  A fix is in M_SwcFoliage (the materials track).
- The albedo: the source cover of fen-outside (the records within 24 m, the 12 largest descriptors, the diffuse map
  where the alpha passes the cutoff x DiffuseColor, linear) has a luminance of 0.061; the terrain layers under it 0.048
  (+27 %, mostly green). Thus the cover must make that ground somewhat lighter in the same light (about +3 L* at L* 20).
- The root darkening of the source grass shader (`cDiffuseMap *= Color0.w`, the vertex alpha): the vertex alpha of the
  fen grass meshes is 1.0 at the roots and the tips (no darkening in the data). The diffuse colors of the `.mtl` (0.491,
  0.497) are the linear values of the sRGB bytes 185 and 186: the client multiplies them as linear (right).
- `NormalViewDependency` (0.3 and 0.5 on these materials) bends the G-buffer normal of the blades toward the camera in the
  source (clusterforward_commonzpass); M_SwcFoliage does not have it. On the normals of the grass meshes at these views it
  lowers the front sun term by 15 to 55 % and raises the sky term by 0 to 13 %: a small part, mostly darker.
- The rest (about +7 L* in fen-outside) is the light of the whole picture: the client has almost no dark part
  (0.01 to 0.03 against 0.14 and 0.30 in retail; the E5 work of the creature track, the sky occlusion).

The river beds (the open point of the water color check, docs/WATER.md): the layers under the water are the layers of
the source. In r_+04_+02 `lakes_splat` (the river stones) has a weight of about 0.9 under the water of the two river views
and 0.01 on the dry ground; `lakeedge_splat` (the edge mud) is at the edges; both have T1 slots (L23, L24). They have a
slot in each region with their weights (where a plan has them inactive, the region has no weight file for them). No cell
under the water falls to the default material, and the macro color under the water is the same as on the dry ground.
The river stones are dark (albedo 0.067 linear, the forest soil 0.059). Thus the light look of the beds comes from the
light of the whole picture, not from the terrain layers.
