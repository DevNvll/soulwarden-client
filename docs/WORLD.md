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
- A long import releases import.lock at least every 10 minutes. `tools/import-world.sh NAME... | --playable | --all`
  calls `tools/import.sh --regions NAME --max-seconds 480 --wait 3600` until the region is complete (the import script
  stops after the batch that passes the time) and waits 15 s between two calls: an import of another track that
  waits on the lock (`flock -w`) gets it there.
- The level build obeys the same rule. A region has three steps (the Landscape, the cell actors, the far meshes).
  `build_world.py` stops after the step that passes 240 s of work (`SWC_WORLD_MAX_SECONDS`), and
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
  `MaterialInstanceConstant` of a base material. The look is not a goal now (no specular, gloss, or emissive map).
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
  had the macro color). At bias 0 the ground was still pale. The look is not a goal now: engine sun, sky, and fog.
- A new converter rule can make a mesh empty (mesh rule 6). `build_world.py` then removes the components of these
  meshes from the cells of the regions built before (`USwcEditorLibrary::RemoveInstanceComponentsOf`; 25 to 47
  components for each region).
- The client (`Source/SoulwardenClient/World/`): the character has a World Partition streaming source. Before the
  spawn of the character, `SwcWorldStreaming::IsReadyAt` puts a streaming source at the place of the character (a
  sphere of 80 m, the highest priority: only the cells at the place, not all the cells in the loading range of 1 km)
  and waits until those cells are loaded and a line from 2 m above the place down 50 m finds ground. With no ground,
  it gives up 3 s after the cells (the player code then uses its test floor); the limit is 30 s. Then the player
  track uses that ground (no test floor), and the input reports carry the position.
- `-SwcGroundTest=X,Y,Z;...` (engine m, no backend) runs that wait for each place and logs the time and the ground.
  Measured with the region import beside it (2026-10-18): the place of the development character 4.6 s, ground =
  the Landscape at 138.24 m (with the sphere of the whole loading range: more than 30 s); a region not in the level
  yet: no ground after 3.0 s.
- The far meshes of the game (`impostors/`, `poi_impostors/` of each region: one mesh for each cell of 128 m) are
  groups of 512 m (`terrain.impostors`, 767 groups, 68.5 million triangles), always loaded, with the tag
  `SwcImpostor` and no collision. The client hides a group while its center is within the loading range of the view
  (USwcWorldSubsystem): they fill the distance where the cells are not loaded. They have no normal map: the normal
  maps of the impostor atlases are not tangent-space maps (the far meshes were black under the sun with them).
- The ocean is a flat plane at 40 m with no collision.
- `-SwcFlythrough=PLAN.json` (USwcWorldSubsystem, ASwcFlythrough): a camera flies along a path (engine points), with
  a streaming source; it logs the frame rate and the streaming each second, can write a picture every N frames, and
  ends the client with a summary line. `terrain.flythrough_plan` writes a plan from a route.
