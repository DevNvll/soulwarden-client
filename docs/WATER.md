# The water

Stage WF5 (Linear AST-187): the water of the world (the ocean, the water volumes, the rivers) and the wade and the swim of the
player. The content follows the source: the ocean level, the water shapes, the river curves and widths, the flow, the water kinds,
and the depth rules.

The rendering uses the advanced water of Unreal, the Water plugin (decision of the user, 2026-10-25, AST-200: "looking through
the water makes the water disappear. change the water to a more advanced water like one from unreal"). See "The Unreal water".
The content stays from the source: the ocean level, the shapes, the flow, the overrides, the depth classes, the wade, and the
swim. Only the look changes.

Replaced: the rule of 2026-10-20 (AST-200: "the water shader is not matching retail and its missing the foam in shallow
waters"), which replaced the decision of 2026-10-18 ("the rendering is the Unreal way"). That rule said that the look of the
water must match the retail pictures. The sections "The source water shader" to "The water sky cube from the source parts" are
its record. The source shader path stays in the client as a switch until the user accepts the Unreal water.

The research: `~/Games/soulwarden-client/research/wf5/` (the shapes, the curves, the materials, the quadtree) and
`~/Games/soulwarden-client/research/wf5-swim/` (the depth classes, the wade, the swim). The retail measurements:
`~/Games/soulwarden/work/evidence/unreal-references/set2-water/` (the matched views) and `set3-swim/` (the swim).

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/water.py`, `tools/convert-water.sh` | The surfaces of each region, the ocean and its overrides, the materials, the textures, and the quadtree of each region into `SWC_CONTENT/water`. `check`: the surfaces against the quadtree. `samples`: the points of the reader test. |
| `Scripts/water_materials.py`, `tools/import-water.sh` | The water textures and the source water material `/SwcGameContent/Water/M_SwcWaterSource` (translucent, unlit, the source formulas in a Custom node). |
| `Water/SwcWaterData` | Plain C++ (tested by `tools/test-water.sh`): the readers of the files, the quadtree query, the depth class rule, the ocean rule. |
| `Water/SwcWaterSubsystem` | The surfaces near the view (static meshes made at run time), the ocean that follows the view with its local overrides, the queries of the movement. |
| `Character/SwcCharacterMovement` | The depth class of the feet, the wade, and the swim. |
| `Character/SwcPlayerActions` | The blocks of the swim and the wade, the sheathe of the swim. |
| `Net/Codec` | The depth class byte of the input entry (mask bit 3). |

```sh
tools/convert-water.sh write      # needs the environment records (tools/convert-environment.sh write)
tools/convert-water.sh check      # the surfaces against the quadtree
tools/convert-water.sh samples    # the points of tools/test-water.sh
tools/convert-water.sh source-textures   # the inputs of the source shader: the linear foam map, the gloss map, the ocean cube
tools/import-water.sh             # the textures and M_SwcWaterSource
tools/test-water.sh               # the readers, the query, the depth classes
```

## The shapes (the source rules)

| Shape | Rule | Evidence |
| --- | --- | --- |
| A water volume | The surface is the top of the shape in the frame of the entity, through its full matrix (rotation and scale): a polygon prism M (x, y, Height); a box M (x, y, +Dz / 2) (a box is centered on the entity); a cylinder M (x, y, +Height / 2). The polygon is counterclockwise and can be concave: ear clipping. | 452 volumes agree with the quadtree surface: median 0 m, largest 0.028 m (research wf5 `wv-heights.json`) |
| A river | Segment i is the cubic Bezier V[i], Forward[i], Back[i + 1], V[i + 1] (the field `crc:6dcec137` is "Back"); 2 rivers are linear. The width is DefaultWidth + WidthAttribute[i], centered, linear between two vertices by the parameter of the segment [inference: the Lumberyard rule]. One cross-section at each SegmentLength. The surface is the matrix applied to the curve point. u across (W / TileWidth), v along (the length / TileLength): the flow goes to +v. | 200 rivers agree with the quadtree: median 0 m (`river-test.json`) |
| The ocean | The level of `terrain.json` (40 m). The material, the animation, and the water fog of the mission (`Ocean`, `OceanAnimation`, the time of day variables "Ocean fog color", "Ocean fog density"). 13 local overrides (WaterOceanComponent): the 7 storm cylinders of Cutlass Keys (`ocean_roughwater01`, transition 8 s), two beaches and a raid place (flat water, other waves and fog), 3 boxes of dungeons. | `ocean-records.json` |

The conversion (2026-10-18): 640 water volumes (3 exact copies removed), 277 rivers, 13 ocean overrides, 29 materials, 22,091
vertices, 20,262 triangles. 60 river triangles fold at tight bends (the radius is smaller than half of the width; the source rule
has the same fold).

`tools/convert-water.sh check`: 777 surfaces have quadtree water under them; 675 have a median height difference of 0.05 m or less.
The others are where the quadtree has a different body: fountains and dungeon volumes over the ocean cells (flags 10), small
fountains in a river, raised river parts.

## The rendering

- Each surface is a static mesh made at run time (`UStaticMesh::BuildFromMeshDescriptions`), with its own dynamic instance of
  `M_SwcWaterSource`. The surfaces near the view build first (at most 6 in a frame, in 3 km); they stay. No collision, no shadow.
  During a loading screen the view is the place of the character, and the screen ends only when the surfaces within 150 m
  are built (the loading gate of the world track (docs/WORLD.md "The loading"); a river surface came 0.4 s after the screen before).
- The ocean is a flat grid of 20 km at the ocean level that follows the view in steps of 312.5 m. The override of the place of the
  view (its cylinder or box) changes its values in its transition time.
- The ocean plane of the world level (the world track's `M_SwcOcean`) hides with no collision while it is in the level: a character
  walked on it at 40.000 m.
- `-SwcWater=Name=Value,...` (the command line) and `swc.WaterParams Name=Value,...` (the console, also in match-view variants) set
  values of each instance. `Debug=N` shows a part: 1 the soft edge a, the foam gap s, the foam f; 2 the reflection with the sun; 3 the
  water term; 4 the trace; 5 the transmittance T, the path, the depth gap; 6 the scene behind the water. A variant value stays for
  the next variants of the run (only Debug goes back to 0).

## The Unreal water (AST-200, 2026-10-25)

The client spawns the bodies of the Unreal Water plugin at run time from the source shapes (the orchestrator approved the plan on
2026-10-25; stage 1 proved it). The default is the source shader path (`swc.WaterMode 0`) until the start-at-place fault of
stage 2a is fixed. `-SwcWaterMode=Unreal` or `swc.WaterMode 1` selects the Unreal water; with no specs file the source path draws.
No body changes the Landscape (`bAffectsLandscape`
false). The depth classes, the wade, and the swim read only the quadtree. The Water plugin does not touch the pawns: its
collision profile is query only, and the camera ignores it.

### The body specs (stage 0)

`tools/convert-water.sh bodies write [--out DIR]` (`converter/swc_convert/water_bodies.py`) writes `unreal-bodies.json`. The
default folder is `SWC_CONTENT/water`. The file has the zone (the extent of the world regions), the ocean level, and the bodies
of each region. The bodies of a region have the order of `REGION.water`: body k of the spec is body k of the surfaces.

| Body | Rule |
| --- | --- |
| River | One spline point for each source vertex V[i] (world, engine m). The leave tangent of point i is 3 (Forward[i] - V[i]). The arrive tangent of point i + 1 is 3 (V[i + 1] - Back[i + 1]). Thus the cubic Hermite segment of the Unreal spline (key i + t) is the source Bezier segment. The matrix is affine, so the curve of the world control points is the world curve. A linear spline has the chords as tangents. The width is the source width (DefaultWidth + WidthAttribute) across the local tangent, through the matrix. It is linear by the key, as the water spline metadata of the plugin (CIM_Linear). The depth is the surface minus the floor of the quadtree at the vertex (none where the quadtree has no water). |
| Lake | A volume with a flat top (a height range of 0.01 m or less) and a simple outline. The outline is the prism polygon, the box corners, or the cylinder rim, at the surface z. |
| Custom | The other volumes: the client uses the surface mesh of `REGION.water`. |

`tools/convert-water.sh bodies check [--out DIR]` compares the specs with the surfaces of `REGION.water`. The test with no game data
is `tests/converter/test_water_bodies.py` (`tools/test-converter.sh`).

The result (2026-10-25): 277 rivers, 621 lakes, and 19 custom bodies. The 19 custom bodies are tilted, with a top height range of
0.014 to 0.89 m. No fault occurred in the names or the order. The largest distance of a cross-section center to the spline is
0.58 mm. The largest width difference is 1.03 mm. These values are the float32 precision of `REGION.water`. The lake outlines are
within 0.64 mm of the surface vertices, and the heights are within 5 mm. 325 of the 2,571 river vertices have no quadtree water
under them, so they have no depth.

### The client code of the Unreal water (stage 1)

| Part | Job |
| --- | --- |
| `SoulwardenClient.uproject`, `SoulwardenClient.Build.cs` | The Water plugin (it needs Landmass, Niagara, GeometryProcessing, and BlueprintMaterialTextureNodes) and the modules Water, GeometryCore, and MeshConversion. A plugin change makes the next cook a FULL cook. |
| `Water/SwcWaterBodies` | The river, lake, and custom bodies of the client. Their components are dynamic (`IsBodyDynamic`) and movable, so the plugin makes their render data at run time. `bAffectsLandscape` is false. `SwcBuildWaterInfoMeshes` builds the two water info meshes of a body at run time: the plugin builds them only in the editor, and a cooked game needs them. |
| `Water/SwcWaterUnreal.cpp` | The water zone (local tessellation, a 4 km window, 2,048 texels), the ocean at the ocean level over the zone, and the bodies within 3 km of the view (4 in a tick). The river spline is the source Bezier. The metadata of each point has the width, the depth, and the flow speed. |
| `Water/SwcWaterSubsystem` | The source path is the default for now. `-SwcWaterMode=Unreal` (at the start) and `swc.WaterMode 1` (at run time: it hides the actors of one path and shows the other) select the Unreal water. The ocean of the place (the local override and its transition) is the same for both paths (`UpdateOceanState`). The queries, the loading gate, and the shore foam decals are the same in both paths. |
| `Scripts/water_unreal_materials.py`, `tools/import-water-unreal.sh [--check]` | The material instances of each slot, as children of the plugin materials. `--check` writes them into the check plugin `SwcWaterCheck`, which is linked into this checkout only. |
| `Environment/SwcMeterProbe`, `Shaders/Private/SwcMeter.usf` | The meter skips the Single Layer Water pixels (shading model id 10). Such a pixel has its own dark base color in G-buffer C, not the ground under the water. |

The options of a run:
- `-SwcWaterMode=Unreal` selects the Unreal water at the start (`swc.WaterMode 1`). `-SwcWaterMode=Source` (`swc.WaterMode 0`) is the default.
- `-SwcWaterBodies=FILE` sets the specs file. The default is `SWC_CONTENT/water/unreal-bodies.json`.
- `-SwcWaterMaterials=/SwcWaterCheck/Water/Unreal` sets the folder of the instances. With no value, the materials of the plugin are used.
- `-SwcWaterInfoMesh=Plugin` keeps the info meshes of the plugin. This works only in an editor run.

The flow speed is a rule of the client [I]: WaterFlowSpeed x 0.25 x 0.25 x TileLength / Tilling m/s. This is the move of the base
normal sample of the source river shader.

The runtime rules (stage 1, 2026-10-25):
- The lake outline is counterclockwise in engine space. The mirror of y makes it clockwise in Unreal space, and the lake mesh of
  the plugin fills only a counterclockwise polygon. Thus the client turns the order of the points.
- The dilated outline (ShapeDilation / 2 outward) fails for 7 concave lakes and for the ocean of the client (its small island).
  Then the client tries a smaller dilation (10 m), and at last none.
- A custom body draws its own mesh and has no water info mesh.
- The profile WaterBodyCollision of the plugin has no response for WorldStatic, so it blocks that channel. The ground traces of the
  client (the loading check, the test floor) are WorldStatic single traces, and they stopped on the water. Thus the water collision
  overlaps WorldStatic. The underwater sweep of the plugin needs a blocking hit, so it uses ECC_Vehicle
  (`UWaterRuntimeSettings::CollisionChannelForWaterTraces`, set after the engine init), which the water
  blocks and the client does not trace.

### The source tone (decision of the user, 2026-10-25: "make sure the lakes have the same dark original tone")

The lakes, the rivers, the custom bodies, and the ocean keep the dark color of their source water fog (`swc.WaterSourceTone 1`, the
default; 0 gives the values of the plugin material). The ocean tone is from stage 2a (the decision of the orchestrator,
2026-10-25: "World fidelity is the goal"). The math is in `SwcWaterData` (`swc::water::volume_tone`, `ocean_tone`,
`tone_parameters`), and `tools/test-water.sh` tests it.

- The source volume term is Cfog + T (Cbehind - Cfog), with T = exp(-density x the path in m) for each channel and
  Cfog = FogColor x FogColorScale x the sun color (E / pi).
- Single Layer Water (`SingleLayerWaterShading.ush`) is T = exp(-(scattering + absorption) x the depth in cm), with the in-scatter
  light x scattering / extinction x (1 - T). The light is the sun E x the phase (isotropic: 1 / (4 pi)) plus the ambient light.
- Thus the extinction is density / 100 per cm, the same for each channel, as in the source. The albedo is
  `swc.WaterToneAlbedoScale` (4) x FogColor x FogColorScale, at most 0.95 [I: the ambient part is not counted].
- The encoding of the plugin material (read from the graph of Water_Material): the absorption coefficient per cm is
  1 / (Absorption.RGB x Absorption.A), and the scattering coefficient is (Scattering.RGB x Scattering.RGB + the foam) / 1000.
  The defaults (10, 150, 350, 8) and (1, 1, 1, 0.5) give the light turquoise of the plugin.
- The ocean (WaterFogVolume/OceanInto: Cdeep x I + Cbehind x T, with T = 2^(-k (L + the depth under the surface)),
  k = 1.4427 density, I = (2^(j L) - 1) / j, j = k (d.z - 1), and Cdeep = FogColor x fogColorMultiplier x the sun): deep water goes to
  Cdeep / (k (1 + |d.z|)). Thus the ocean albedo is 4 x FogColor x multiplier / (1.4427 density (1 + 0.5)), with 0.5 as the mean
  |d.z| of the views [I]. The extinction is density / 100 per cm, as for the volumes: Single Layer Water also takes the light path down
  to the floor (`MeanTransmittanceToLightSources`), as the source T does. The near color of the source (nearFogColor x the sun x T x
  sat(0.1 L)) has no term in Single Layer Water [I: left out]. A density of 0 (the magic realm box) is clear water. The values are
  those of the ocean of the place: the mission ocean or the first local override that has the view, blended in its transition time
  (8 s), as in the source path. The starting beach: the albedo (0.005, 0.059, 0.10), the extinction 0.003 per cm (dark blue).
- The client applies the tone to the water instance of each body, to the two transition instances of a river, and to the far mesh
  instance of the zone (the ocean tone). It applies the tone again when the plugin makes an instance again.

The check (run tone2; the mean CIELAB of a fixed water area against the source path; `work/water-unreal/tone_measure.py`):

| View | Plugin tone, dE | Albedo x2 | Albedo x4 | Albedo x8 |
| --- | --- | --- | --- | --- |
| lake-noon (FirstLight_BeastIsland_Lake_A) | 10.5 | 9.3 | 6.0 | 4.7 |
| river-forest-noon (the bank) | 35.5 | 11.1 | 10.2 | 14.7 |
| river-down-noon (from above) | 29.6 | 5.5 | 6.7 | 10.5 |

The rest of the bank difference is the pink sky in the source reflection (a* +14 against +6).

### The check of stage 1 (2026-10-25)

Evidence: `~/Games/soulwarden-client/evidence/water-unreal/`. `stage1-before-after.png` has the 7 views: the source path (run
before3) and the Unreal water (run after4). `tone2/tone-sheet.png` has the tone variants. `wu-flow.mp4` is the river of
river-forest at 12:00. The views are in `work/water-unreal/views` (`make_views.py`). Two of them are retail views of set2-water. The
others are own views: a lake, a river from above, the user's view down through the shallow water of the starting beach, and the
camera at the ocean surface and under it.

- All 7 views draw: the zone, the ocean, the rivers, and the lakes, spawned at run time. No body is left with no water info mesh.
- The user's view: the shallow water near the shore shows a visible water body over the sand. Before, the clear bed showed.
- Under the surface: the plugin underwater fog and the underside of the surface. Before, the scene showed as in air.
- The river video: the water band changes (the mean frame difference 5.2 of 255, against 1.65 in the top third). The flow speed
  of this river is 0.19 m/s (jav_fst_riverwater_a).

The GPU time (`-SwcFrameStats=2`, the median while each view is in place, the second pass of each run, 1280 x 720):

| View | Source path (ms) | Unreal water (ms) | Difference |
| --- | --- | --- | --- |
| lake-noon | 7.46 | 8.46 | +1.00 |
| river-forest-noon | 8.72 | 9.71 | +0.99 |
| river-down-noon | 6.75 | 7.70 | +0.95 |
| ocean-clear-noon | 6.86 | 7.93 | +1.07 |
| beach-down-noon | 5.80 | 6.22 | +0.42 |
| swim-surface-noon | 7.61 | 8.60 | +0.99 |
| swim-under-noon | 7.71 | 9.04 | +1.33 |

### Stage 2a: the Unreal water as the default (2026-10-25/26; the default stays the source path)

- The switch (`swc.WaterMode`, `-SwcWaterMode`), the ocean tone, and the far mesh tone (above).
- The check (2026-10-25 23:14 to 2026-10-26 00:45 UTC; evidence `~/Games/soulwarden-client/evidence/water-unreal/`):
  - The views before and after at 9 places (`stage2a-before-after.png`, `stage2a-variants.png`). The ocean tone: the storm coast
    dE 7.5 (11.4 with the plugin tone), the coast 1.8. The far view (ocean-far) is lighter than the source (L* 54 against 37) [open].
    The runtime switch works in both directions. GPU: +0 to +2.6 ms (the most under the surface: 7.41 against 10.05 ms).
  - The gameplay (`wu2a-water.mp4`): the classes at 0.22 and 0.90 m, the swim at 1.401 and 1.290 m, out at 0.20 m, the travel and the
    river wade at 0.23 and 0.91 m: the same as the source path (the movement reads only the quadtree).
  - THE FAULT (closed 2026-10-26: the cause and the fix are below, "The fix"): when a run starts at a beach, the plugin ocean does not draw there (the character swims on "dry" sand with the
    seagrass of the floor). After a travel away and back (418 m each way) the ocean draws at the same place (`wu2a-travel3-frames.png`).
    The window of the zone follows the view at the start, and the ocean component is visible and makes water mesh tiles (the
    diagnostic `swc.WaterZoneRefresh -1`). A water info texture each frame (`r.Water.WaterInfo.ForceUpdateWaterInfoNextFrames -1`) and a
    full zone rebuild each 3 s (`swc.WaterZoneRefresh 3`) do not help. Thus the teleport changes something else; the next step is to
    find it (for example the scene proxy of the water mesh or the view state of the view extension at the first frames).
  - The far ocean (the cause found offline, 2026-10-26): the water mesh adds its far mesh only with `FarDistanceMeshExtent > 0`
    (UWaterMeshComponent::RebuildWaterMesh); the default is 0. Past the 4 km window there was no ocean, and the far view showed the
    light sea floor of the Landscape. The client now sets 20 km (the source ocean grid of the client); the far instance has the
    ocean tone. To check in the next window.
  - The test of the fault for the next window (removed 2026-10-26: no kick helped): `swc.WaterKick N` (1 a new scene proxy of the water mesh, 2 new material instances
    of the ocean, 3 a zone rebuild with a dirty water mesh grid, 4 the components of the zone registered again, 5 the Unreal water
    hidden and shown), each 6 s apart in a run that starts at the beach (`work/water-unreal/run_kick.sh`). The kick that makes the
    ocean draw becomes the automatic start kick (`swc.WaterStartKick N`: once the window of the zone is valid, and 3 s later).
  - The third window (2026-10-26 04:34 to 05:13 UTC, evidence `wu2a-*` of that date), corrections to the analysis above:
    - The darkening of the rock and the shore in the first 20 to 30 s after a start is the same in the source path
      (`wu2a-cooked-source-fine.png`): it is not the fault.
    - The fault is larger than the near shore: in a run that starts at the starting beach, the plugin ocean is missing in the whole
      bay and in the far view (sand and seagrass to the cliff), also after 30 s, also in the cooked build of 46b2b5f3
      (`wu2a-start-compare.png`, `2a-far-compare.png`). The 418 m travel away and back does NOT fix it: the earlier reading of
      `wu2a-travel3-frames.png` was wrong (its last frame also shows a dry bay).
    - The views of the 2a-after run (a match-view run that started at an earlier saved place and travelled to the beach) drew the
      bay. Thus the start place, or a long travel to the beach, matters [I].
    - Not the cause: the PSO and shader gates of the water info texture (the first texture comes at 30 s world time; the mesh
      proxy exists from the start), texture streaming (`r.Streaming.FullyLoadUsedTextures 1`), the waves (the source waves give
      the same), the kicks 1 to 5 of `swc.WaterKick`, and no other water body in the bay (the specs).
    - The far mesh change (`FarDistanceMeshExtent` 20 km) is not checked: the fault hides the ocean in the far view too.
  - The study `~/Games/soulwarden-client/research/water-ocean-quadtree.md` (GPT-6.1-Sol, 2026-10-26; brief
    `research/briefs/ast200-2a-ocean-quadtree.txt`) proves no single cause, and it finds two real gaps [R]:
    - The ocean rectangle is centered on `SavedZoneLocation` (`bCenterOnWaterZone`, `UWaterBodyOceanComponent::CalcBounds`), and only
      the editor sets it. The client did not set it, so the ocean bounds were centered at 0, 0.
    - The far mesh of the plugin starts at the edge of the whole zone, not at the edge of the local window. A larger
      `FarDistanceMeshExtent` cannot fill the zone outside the per-view tree (about 6 km wide). This is separate work.
    - Other rules [R]: a per-view tree is rebuilt in full at each window move; the window moves only when the view leaves the
      update rectangle (about 1,850 m half extent), thus a 418 m travel does not move it; the water info texture stores no body index,
      and texels with no water give a negative depth, which makes the water clear (geometry present, no visible water).
    - The client code (2026-10-26; removed after the fix, because the check did not prove it): `-SwcWaterStartFix=N`. Bit 1: the client sets
      `SavedZoneLocation` to the zone center before the ocean finishes spawning, and after each spawn it calls `UpdateAll` with a
      shape change, applies the overlap rule again, builds the info meshes from the final shape, and refreshes the bounds and the render
      state of the info components. Bit 2: after each loading screen, when world rendering is on and the zone has its texture, a full
      zone rebuild, the ocean material instances, and `MarkGPUDataDirty`, at 0.5 s and again at 2 s. The diagnostic
      (`swc.WaterZoneRefresh -1`) also logs the ocean bounds, the saved zone center, and the body index.
    - The check (2026-10-26 06:49 to 06:58 UTC, an editor run, `wu2a-sf0`, `wu2a-sf3`, `wu2a-sf3t`; `2a-startfix-compare.png`): a fresh
      start at the beach with no travel, baseline (0) against treatment (3), with the tile bounds (`r.Water.WaterMesh.ShowTileBounds 1`)
      and the info meshes (`r.Water.WaterInfo.ShowSceneProxies 1`). The result: fix 3 does NOT repair the fault. Both runs look the same
      from 2 s to 40 s:
      - The water mesh has tiles over the bay and to the horizon (the tile bounds).
      - The ocean info mesh covers the bay (the info mesh proxies).
      - With the tile bounds only, the bay shows sand to the horizon: the tiles draw no visible water ("tiles with clear water").
      - Also in the baseline, the ocean bounds are centered on the zone (7168, -7168 m, half extent 11,168 m) and `SavedZoneLocation`
        is 7168, -7168 m. Thus the plugin sets it at run time, and the first gap of the study is not the cause at this place.
      - Thus the fault is in the surface pixels: the material clip from the water info texture (the depth or the height that the
        texture gives at the bay), not in the tree or the info meshes [I]. Bits 1 and 2 alone were not run (the plan ran them only if
        3 passed). The code stays behind the switch, and the cooked check is not necessary.
      - The build note: `WaterViewExtension.h` includes a private header of the plugin. The refresh calls
        `FWaterBodyManager::RequestGPUDataRebuild` (public; it calls `MarkGPUDataDirty`).
  - The study `~/Games/soulwarden-client/research/water-info-texture.md` (GPT-6.1-Sol, 2026-10-26; brief
    `research/briefs/ast200-2a-info-texture.txt`) and a check of the plugin source give the main candidate:
    - [R] The zone takes the water height range from the bounds of its bodies (`AWaterZone::UpdateWaterInfoTexture`: WaterZMin is the
      lowest `Bounds.Min.Z`). The bounds of the plugin ocean reach `ChannelDepth` below its level (`UWaterBodyOceanComponent::CalcBounds`),
      and `ChannelDepth` is 0 by default (`FWaterCurveSettings`). The client sets no channel depth (the ocean bounds extent Z is 0 in the
      diagnostic).
    - [R] The merge pass (`WaterInfoMerge.usf`) accepts water only when `WaterZ >= WaterZMin`, and it stores the height normalized to
      that range. The decoder (`WaterDataFunctions.ush`) treats a normalized height of 0 or less as "no water": the water height
      becomes the ground height less 256 cm. The opacity mask of `Water_Material` is 0 at that depth (offset -24 cm): the water is clear.
    - [I] Thus an ocean that is the lowest body of the zone is clear. Near the starting beach, all spawned bodies are higher than the
      ocean (the specs: the lowest lake or river within 3 km is 41.5 m), so the ocean is the minimum. In the whole world only one body
      is lower than 40 m (a lake at 39.28 m near 5088, 5669 m). Why the travel run `2a-after` drew the bay is not explained yet [U]: no
      body below 40 m is near its places.
    - [R] The terrain is an Unreal Landscape (the builder makes it); the ground capture takes only the Landscape and registered terrain
      components. The hidden source water, the clouds, the decals, and the shore meshes are not in the ground capture.
    - The test: `-SwcWaterOceanDepth=M` sets the channel depth of the plugin ocean (by reflection, before it finishes spawning). The
      diagnostic logs the water height range and the ground minimum of the zone. Pass: with M = 10 the range starts at 30 m and the
      bay draws water at a fresh start at the beach; with M = 0 the range starts at 40.000 m and the bay is clear.
    - The check (2026-10-26 07:30 to 07:39 UTC, an editor run; `wu2a-od0`, `wu2a-od10`, `wu2a-odtr`; `2a-oceandepth-compare.png`): PASS.
      - M = 0, a fresh start at the beach: the range is 40.000 to 233 m for the whole run, and the bay is clear from 2 s to 40 s.
      - M = 10, the same start: the range is 30.000 to 233 m, and the bay has water from 2 s.
      - M = 0, the start at the beach, the travel of `2a-after` to the river (1,979 m) and back: the range is 40.000 m before the travel
        (the bay is clear). After the travel it is 38.955 m: a body near the river has bounds below 40 m, which the specs do not show
        (the bounds of the river spline meshes). Back at the beach, the bay has water. This explains `2a-after`.
      - Thus the cause is proved: the ocean was the lowest body of the zone, and the plugin decodes the minimum of the range as "no
        water". A lake that is the lowest body has the same fault; a channel depth of the ocean protects all bodies above 40 - M m.
  - The fix (the word of the orchestrator, 2026-10-26 07:40 UTC): the client gives the plugin ocean a channel depth of 10 m by default
    (`SpawnUnrealZone`, by reflection before the ocean finishes spawning; `-SwcWaterOceanDepth=M` stays for a test).
    - The ocean does not change the Landscape (`bAffectsLandscape` false): the channel depth changes only the ocean bounds (Z extent
      10 m). The minimum of the water height range of each zone is then 30 m.
    - The lowest body of the world is a lake at 39.28 m (`Water_Volume`, region r_+02_+02, near 5088, 5669 m); the lowest river
      curve is at 40.5 m (the specs). All are above 30 m, so no body is at the minimum of the range: all are protected.
    - The height precision of the water info texture changes little (the range near the beach grows from 193 m to 203 m).
    - Removed, not proved: both bits of `-SwcWaterStartFix` (the `SavedZoneLocation` set: the plugin sets it at run time; the second
      `UpdateAll` and info mesh build; the refresh after each loading screen), `swc.WaterKick`, `swc.WaterStartKick`, and the rebuild
      mode of `swc.WaterZoneRefresh`. Kept: the diagnostic `swc.WaterZoneRefresh -1` (now with the water height range and the ground
      minimum of the zone) and the log of the first water info texture. `FarDistanceMeshExtent` 20 km stays until the far-ring task.
    - The check (2026-10-26 07:55 to 07:58 UTC, an editor run, `wu2a-fix`, `2a-fix-sheet.png`; `work/water-unreal/run_fixcheck.sh`): PASS.
      A fresh start at the beach with the default and no debug draw: the log has "the ocean channel depth 10.0 m"; the range is
      30.000 to 233 m for the whole run; the bay has water from 2 s; the view to the open sea at 30 s has water to the islands; no
      "Failed to compile" line. The cooked start comes after the next cook.
  - Thus the default stays the source path (the decision of the orchestrator, 2026-10-26 00:08 UTC). The full cook takes the plugin,
    its materials, and the specs. A later fix and the switch of the default need only a code change and an incremental cook.
- The cook: `DefaultGame.ini` cooks `/Water/Materials` and `/Water/Waves` (the client loads them by path), and
  `tools/lib/loose-data.txt` has `water/unreal-bodies.json`. The plugin change in the uproject and the Config change make the next
  cook a FULL cook. The deploy step before that cook: `tools/convert-water.sh bodies write` (the specs into `SWC_CONTENT/water`).
  No asset goes into the shared plugin: the client uses the plugin materials and sets the tone at run time.
- The gameplay check: `tests/drive/wu2a-water.txt` with `wu2a-water-routes.txt` (the beach: the view down, the walk into the water,
  the wade, the swim, the camera under the surface, the swim back and out; one travel to the river of river-forest and a walk into
  it). The view set: `work/water-unreal/views` (9 views with `ocean-storm-noon` and `ocean-far-noon`).

### The far ring (2026-10-26; a test, not the default)

- The study `~/Games/soulwarden-client/research/water-far-ring.md` (GPT-6.1-Sol; brief `research/briefs/ast200-2a-far-ring.txt`) [R]:
  - In the local mode the detailed water stops at the edge of the water info window (4 km): the material function `ClipDynamicBounds`
    discards the pixels outside it (the tree is 6.1 km wide, but its outer tiles are clipped).
  - The far mesh of the plugin always starts at the edge of the whole zone (22 km); no setting moves its inner edge.
  - The editor fills the gap with a static fallback mesh of the ocean (`Water_Material_Ocean_LOD`); its builder is editor only, and the
    client spawns none. Thus between the window and the zone edge nothing draws: the far view shows the sea floor.
- The test (behind start options, default off): `-SwcWaterZoneMode=Global` turns the local tessellation off (one tree and one water
  info capture for the whole zone; the far mesh then starts where the detailed water ends), and `-SwcWaterInfoResolution=N` sets the
  water info texture (default 2048). The diagnostic (`swc.WaterZoneRefresh -1`) logs the zone mode, the info extent, and the effective
  texture size (`r.Water.WaterInfo.RenderTargetResolutionMax` can clamp it).
- The texel size and the memory (the study, calculated): local 4 km at 2048: 1.95 m, 32 MiB plus 96 MiB of capture targets; global
  22.3 km at 2048: 10.9 m; at 8192: 2.7 m, 512 MiB plus about 1.5 GiB; at 12288: 1.8 m, about 4.5 GiB (only after a memory check).
- The check (`work/water-unreal/run_farring.sh LABEL local|global RES`, the views `ocean-far-noon`, `ocean-storm-noon`,
  `cliff-east-noon`, `cliff-southeast-noon` (a cliff top at 182.8 m, 659 m from the beach, open sea to the east), `beach-down-noon`,
  and `river-down-noon`; a VRAM log each 2 s, and the own run stops below 1 GiB free): local 2048 against global 2048 against
  global 8192. The pass values (test limits of the study): no sand where the source draws the ocean; no crack, strip, or double edge
  at the old window edge or at the zone edge; a shoreline shift of at most 1 texel (1.95 m) in the near-shore pair; the far tone within
  5 L* of the source; the GPU median and p95 at most 1 ms above local; a capture spike of at most 1 frame. If global fails the memory or
  time limits, the next option is the fallback mesh of the plugin, made by the client.
- The check (2026-10-26 09:16 to 09:40 UTC, editor runs; evidence `water-unreal/wfr-*`, `far-ring-local-vs-global2048.png`,
  `far-ring-sky-local-vs-global2048.png`):
  - The cliff views failed as views: the camera stands on a flat cliff top, and the ground hides the sea. Two new views
    `sky-east-noon` and `sky-south-noon` (the camera 300 m high above the cliff edge) show the far ocean.
  - Local 2048 (today): in the high views, a light band (no water) lies between the detailed water and the dark far mesh at the
    horizon. This is the gap of the study.
  - Global 2048: the water continues to the horizon with no band and no visible edge. In the six other views (the beach to the open
    sea, the storm coast, the near shore down through the water, the narrow river) there is no visible difference from local
    (also not at the shoreline). The GPU p50 of each view is the same within 0.1 ms (5.5 to 7.8 ms). The VRAM peak is the same
    (12.3 to 14.0 GB used for the whole machine).
  - Global 8192: FAIL. At the first capture of the whole zone the VRAM went from 10.7 to 23.8 GB used (of 24.6) in 4 s, and the
    sampler stopped the run (268 MiB free). The study estimate (about 2 GiB) was too low: the capture passes render the scene at the
    texture size. Do not use a large texture for the whole zone; 12288 is not tested.
  - Open: the high views have a light green haze in both modes (not a far-ring effect); the far tone against the source; the cooked
    check; a pan video for the seam at the zone edge.
- The cooked check (2026-10-26 17:06 to 17:27 UTC, the cooked build of 81182e19; evidence `water-unreal/wu2a-fixc*`,
  `2a-fix-cooked-sheet.png`, `wfrc-*`, `far-ring-cooked-local-vs-global.png`, `far-ring-cooked-source-vs-unreal.png`, `wu-views9c`,
  `views9-stage2a-vs-cooked.png`):
  - The start at the beach (`-SwcWaterMode=Unreal`): PASS. "the ocean channel depth 10.0 m", the range 30.000 to 233 m, the bay
    has water from 2 s, the first water info texture at 1.6 s.
  - A new fault of the cooked build [R by the log]: the diagnostic gives the ocean bounds at 0, 0 (half extent 11,168 m) and
    `SavedZoneLocation` 0, 0. In the editor the plugin sets it at run time (7168, -7168 m); in the cooked game only the editor code of
    the plugin can set it. Thus past about 11.2 km east or south of the world origin the ocean has no tiles: in the high views a
    straight edge (east) and a corner (south) end the water, in the local and in the global mode. The client now sets
    `SavedZoneLocation` to the zone center (by reflection, before the ocean finishes spawning; built, not yet cooked).
  - Global 2048 against local (cooked): the gap at the old window edge is closed up to that bounds edge. The GPU p50 is 0.2 to 0.7 ms
    higher (local 5.6 to 6.8 ms, global 6.2 to 7.5 ms); the VRAM peak 10.2 against 9.7 GB used. In `ocean-far-noon` the near open
    water is lighter in global (L* 63.6 against 53.2 in a near band): the 10.9 m texels change the depth near the shore.
  - The source path at the same views: the green haze of the high views is also in the source path: it is the fog of the place, not
    the water. The far tone (L* of a far band): `ocean-far-noon` source 44.1, local 47.5, global 45.4 (within 5); the high views
    are not usable for the tone until the bounds fault is fixed (the band crosses the gap).
  - The views pass at the 9 places (cooked, local 2048) against the stage 2a pictures (editor, before the depth fix): 7 places look the
    same. `beach-down-noon` is much lighter (pale sand under the shallow water against dark brown). In `swim-surface-noon` the camera
    (40.05 m) looks from under the plugin surface, and the underwater post process does not start (the swim is correct in the log).
    Both are open for the next look check. GPU p50 6.1 to 9.4 ms.
  - Not done: the pan video at the zone edge (it waits for the cooked bounds fix: the bounds edge comes first).
- The cooked check of the bounds fix (2026-10-26 17:35 to 17:46 UTC, the cooked build of 34fe062f; evidence `water-unreal/wfrb-*`,
  `wfrp-*`, `wfrpb-*`, `far-ring-cooked-bounds-fix.png`, `far-ring-cooked-fixed-local-vs-global.png`, `far-ring-pan-beach-local-vs-global.png`):
  - The diagnostic: the ocean bounds at 7168, -7168 m (half extent 11,168 m, Z 10 m) and `SavedZoneLocation` 7168, -7168 m for the
    whole run. PASS: the straight edge and the corner of the high views are gone.
  - Local 2048: a light band (no water) remains between the detailed water and the dark far mesh at the horizon of the high views:
    the gap past the 4 km window (the clip of the study).
  - Global 2048: the water continues to the horizon with no band and no edge in both high views.
  - The pan at the cliff top failed as a view: the character stood in a rock gap (the terrain height alone is not a good place). The
    pan at the start beach (heading 40 to 180 deg, pitch 0): local and global look the same at eye level, with no seam and no band
    (islands and the low angle hide the far gap).
  - Cost: GPU p50 of the high views local 7.06 and 7.38 ms, global 7.48 and 7.04 ms; the VRAM peak local 9.9 GB, global 9.7 GB.
    The near-shore lightening of global at `ocean-far-noon` (the earlier cooked check) is the open cost of global.

### Stage 2c: the dark tone of the source near the player (2026-10-26)

- The problem (the orchestrator, 2026-10-26: the Unreal water needs the dark tone of the source near the player before it can be the
  default): the near band L* (`work/water-unreal/tone3/near_lstar.txt`) at ocean-far-noon is 35.5 (source) against 52.3 (Unreal,
  cooked), at ocean-clear-noon 30.6 against 48.8, at beach-down-noon 39.6 against 86.7; the lakes and the rivers are within 5.
- The study `~/Games/soulwarden-client/research/water-near-tone.md` (GPT-6.1-Sol; brief `research/briefs/ast200-2c-near-tone.txt`):
  - beach-down: the first suspect is the exposure. The meter skips each Single Layer Water pixel (`SwcMeter.usf`, `FMeterSums::Add`);
    in an all-water view it has no accepted pixel, publishes no measure, and the last correction stays (the editor and the cooked
    runs: 3.06 EV apart). Not proved: the logs do not give the exposure at the shot.
  - The ocean channel depth (10 m) changes only the bounds and the height range, not the optics [R].
  - The open water: the client tone is already dark. Other terms of the ocean material add light: the foam scattering (added to the
    scattering), the opaque foam, the reflections, the phase (g 0.1), and the ambient light. Their shares need tests at a fixed exposure.
  - The source near color term needs a client material or pass; only if a residual remains after the tests.
- The source path is not touched by the meter fault [R by the code]: its water material is unlit and translucent, writes no G-buffer,
  and the meter (after the depth of field, `SubscribeToPostProcessingPass` AfterDOF) reads the shading model of the floor under it.
  Its pixels are counted (the denominator is the albedo of the floor).
- The test switches (all off by default; set at run time, no Config change):
  - `swc.MeterDiag 1`: a log line for each measure of the meter (at most each second): the accepted, sky, and water pixels, the
    exposure of the frame as an EV, E, and the age of the last good measure; also for a measure with no accepted pixel.
  - `swc.ExposureFixed 1` with `swc.ExposureFixedBias B` (default -13): the manual exposure of Unreal (no physical camera) with the
    bias B; the source meter keeps its correction.
  - The ocean instance only: `swc.WaterOceanFoamScatter S` (the scale of "Foam Scattering"; 0 none), `swc.WaterOceanFoamOpacity O`
    ("Foam Opacity"; 0 no opaque foam), `swc.WaterOceanAnisotropy G` (the phase g; 0 isotropic). Below 0 (below -1 for the
    anisotropy) the plugin value. A change applies the ocean tone again (a log line "the ocean tests").
  - The reflection modes: the engine cvar `r.Water.SingleLayer.Reflection` 0, 1 (the default), 2, 3.
- A change of `Shaders/` (for the meter fix later) makes the cooked build old (`tools/lib/cooked.sh` hashes Source, Lua, Config, and
  Shaders), but not a full cook: `tools/cook.sh` asks for a full cook only for a change of the config hash (Config/Default*.ini and
  the cooker options) or a small cooked folder. Thus an incremental cook with the compile of the changed global shader.

- The B window (2026-10-26 18:51 to 19:13 UTC and 2026-10-29 06:17 to 06:44 UTC, cooked builds 99ca53c and 9576cb5):
  - B1 [R by the log]: in the Unreal mode the view beach-down has 0 accepted meter pixels (all 10,360 sample pixels are Single Layer
    Water), the last good measure is 12 to 14 s old, and the frame EV is 10.37, against 12.89 at ocean-far in the same run and 13.87 for
    the source path at beach-down: 2.5 to 3.5 EV too bright. The same in the two orders.
  - The fixed exposure: `swc.ExposureFixedBias -13` gives EV 15.53 (the manual exposure adds about 2.5 EV); -10.5 gives EV 13.03.
  - B2 is NOT valid yet. Since the build of 34fe062 the match-view runs wait the whole place timeout (about 86 s) with "pawn 0" at each
    view, but the timeout line gives the pawn at the feet; with about 110 s for each view the runs pass the stay limit (120 s + 75 s for
    each view) and stop after one picture. In the runs of 2026-10-29 the pictures do not have the camera of the views (all show the
    same ground view of the starting beach): the camera override of match-view did not apply. The L* of those pictures is not a
    measure of the water. The wait line of match-view now logs the pawn, its place, and its distance from the feet (built, not cooked).
  - The cause (a static check of the logs, 2026-10-29 06:50 UTC) [R by the logs and the code]: the loading gate did not end
    ("world: the wait at the place failed after 90 s"). Then the view stays on the camera of the gate (`SwcWorldStreaming`
    `SetViewTarget(Source)`; the camera manager applies the override of match-view only when the view target is the player character),
    and the place check of match-view waits.
    - In the B2 runs (both builds) the blocking system is "meter: the exposure is 0.00 EV from the source meter": with
      `swc.ExposureFixed 1` the meter takes no measure, so its gate check (four measures in a row within 0.05 EV) never passes. Fixed:
      the meter check passes while the fixed exposure is on.
    - In B1 and in wfrb-global (the first place of a run) the blocking system is "surface: 0 road(s), 0 decal(s) to show, 0 region
      file(s) to read". The surface check also needs its last update at the gate place within 1 cm (`UpdatedAt.Equals(Place, 1.0)`),
      and it updates during a screen only when the gate is settled. Not a water change: for the world track.
- The B3 window (2026-10-29 06:56 to 07:20 UTC, the editor build of d08440d3, uncooked; the fixed exposure EV 13.03 in each shot;
  evidence `water-unreal/wnt-b3-*`, `near-tone-b3-source-vs-unreal.png`, `near-tone-ablation-ocean-far.png`):
  - The match-view check: no gate failure after the fix, the right cameras (the water pixels of the meter fit the views). The first
    place of an uncooked run waits for the load ("pawn None" while thousands of packages load).
  - The L* (far / middle / near), Unreal minus source at the same exposure: ocean-far -5.3 / +2.9 / +11.3; ocean-clear +6.0 / -1.3 /
    -4.0; beach-down -11.3 / -12.3 / -8.8. Thus the beach-down jump of the cooked run (+47) is the exposure fault alone (B1); at the
    same exposure the Unreal water is darker there.
  - The ablations at ocean-far (the near band): base +11.0, no foam +11.6, no foam scattering +11.2, isotropic phase +16.7,
    `r.Water.SingleLayer.Reflection 0` -15.2. With that cvar the sand floor under the water also goes away, so it is not a clean
    reflection test. The foam and the phase are not the cause. The pictures show the sand floor through the near water in the Unreal
    water (turquoise), and dark water with the reflections of the rocks in the source.
  - Next: the meter fix (the water pixels, a client shader change, an incremental cook); for ocean-far, a test of the floor
    transmission (the two-path attenuation of the source against the plugin path) and of the SSR alone (reflection modes 2 and 3).
- C, the meter fix (2026-10-29; not built at the commit): `swc.MeterWater` (default 1). After the base pass of a game view, on a
  frame that the meter measures, the client copies G-buffer B and C (the render targets "GBufferB" and "GBufferC" of the base pass:
  the floor under the water; the Single Layer Water pass writes its own values later, `DeferredShadingRenderer.cpp`: the base pass
  before `RenderSingleLayerWater`). The GPU reduction (`SwcMeter.usf`) then takes a water pixel with the luminance of the final pixel
  and the denominator of the floor under it (the study, "Repair meter input"). 0 gives the old rule (the water pixels skipped). The
  "meter diag" line gives the water pixels and the taken ones.
  - The source path has no Single Layer Water pixel (its water is unlit and translucent), so its measure does not change [R by the
    code]. The check: the 9 views in the source mode, each with `swc.MeterWater 1` and 0 (a variant): the EV of the frame within
    0.05 EV (`work/water-unreal/tone3/run_c.sh source`, `ev_pairs.py`).
  - A change of `Shaders/` makes the cooked build old but needs only an incremental cook.
  - The trap of the cook (2026-10-29 08:51 UTC, the first cook of C): the cook failed with status 25 ("FSwcMeterCS has unbound
    parameters OpaqueGBufferB, OpaqueGBufferC, UseOpaque"). The cooker loads the EDITOR modules of the main checkout (the global shader
    class of SwcShaders with its parameter struct), and `tools/cook.sh` builds only the game target. The old editor module did not
    know the new parameters of the changed shader. Thus after a change of a global shader and its parameter struct (Shaders/ and
    Source/SwcShaders): `tools/build.sh editor` in the main checkout first, then `tools/cook.sh`.
  - The source check (2026-10-29 07:42 to 08:15 UTC, an editor build): the 9 views with `swc.MeterWater` 1 and 0: 7 within 0.05 EV,
    ocean-clear -0.09 and beach-down -0.19, with "water 0, taken 0" in each frame (the new branch did not run). The A/A test (1, 1, 0
    and the reversed 0, 1, 1) gave the same EV sequence in the shot order in both passes (beach-down 13.85, 13.79, 13.67 to 13.69): the
    drift of the place in time, not the setting. PASS by the A/A rule. The frame time was not clean (+0.27 and +0.34 ms at one place,
    none at the other; a retail run used 42 to 45 % of the GPU). Thus the copies are now made only when the last measure saw Single
    Layer Water pixels: the source path never copies.
  - The check of the copy condition (2026-10-29 08:24 to 08:43 UTC, the editor build of c6157bf2): the reversed pass in the source mode
    (0, 1, 1) has a frame cost of -0.02 to +0.04 ms and the same EV sequence: PASS. The Unreal mode, automatic exposure: beach-down
    EV 11.60 with `swc.MeterWater 1` (all water pixels taken) against 10.36 with 0 (no measure) and 13.85 in the source; ocean-far 12.43
    against 12.83 and 13.14. C gives the all-water view a measure but does not close the gap: at the same exposure the Unreal water is
    darker at beach-down (B3), and a darker water pixel gives a lower E.
- The floor tests at ocean-far (the fixed exposure EV 13.03, Unreal minus source, far / middle / near): base -6.7 / +2.7 / +11.0;
  `swc.WaterOceanExtinction 2` -6.4 / -2.9 / +5.3; `r.Water.SingleLayer.Reflection 2` +17.7 / +27.6 / +23.6; 3: +15.7 / +22.7 / +21.6.
  The floor through the near ocean is a large part of the gap; the default reflection mode 1 is the darkest. The controls (lake-noon,
  river-down-noon, river-forest-noon): all near bands within 0.6; river-down middle -5.3, river-forest far +5.4.
- The extinction sweep (2026-10-29 09:02 to 09:21 UTC, the cooked build of 9147255 with C, the fixed exposure EV 13.03; evidence
  `water-unreal/wnt-d-*`), the near band, Unreal minus source, for `swc.WaterOceanExtinction` 1 / 1.5 / 2 / 3: ocean-far +11.6 / +8.1 /
  +4.3 / +0.8; ocean-clear -3.1 / 0.0 / -0.7 / -2.3 (all bands within 5 from 1.5 to 3); beach-down -10.3 / -12.4 / -13.7 / -16.7. At
  ocean-far `swc.WaterToneAlbedoScale` 2: near +2.4 but far -8.2; 1: -3.4 / -10.8 / -13.1 (all darker). No single extinction fits the
  three views: the deep near ocean needs about 2 to 3 times, the shallow water at beach-down is too dark at all scales. Its probable cause
  [I]: the near color term of the source (nearFogColor x the sun x T x sat(0.1 L); a light that has its peak at about 2 m of water) is
  not in Single Layer Water. The plugin rule of the floor is the same two-path rule as the source (`SingleLayerWaterShading.ush`:
  exp(-sigma WaterVolumeDepth) and exp(-sigma the vertical distance to the top); the view path is a depth difference along the view
  axis, about 10 % shorter than the ray here) [R]. The controls: all near bands within 1.6; river-down middle -6.6, river-forest far +5.6.
- The near color term of the source ocean (2026-10-29, the design `~/Games/soulwarden-client/research/water-near-term.md` option A,
  the word of the orchestrator; not built at the commit): the check plugin only (`tools/import-water-unreal.sh --near`,
  `Scripts/water_unreal_near.py`, the folder `/SwcWaterCheck/Water/Near`; never the shared plugin, never `/Water`).
  - `M_SwcWaterNear`: a copy of the plugin parent `Water_Material`. Between its final material attributes and the output: Get Material
    Attributes (the emissive, the opacity), a Custom node with N = gain x NearRGB x S x T x sat(0.1 L), an Add to the emissive, and Set
    Material Attributes. The emissive of Single Layer Water is added outside the floor transmittance (`BasePassPixelShader.usf`): an
    additive term under the surface, before the reflection composite. L is the ray length (the depth difference over the cosine of the
    pixel), T the floor transmittance of the plugin (exp(-sigma the depth gap) x exp(-sigma the vertical depth); `swc.WaterOceanNearT 1`:
    the source T), times (1 - opacity) x (1 - F) with a flat-surface Schlick F [a first approximation of EnvBRDF].
  - The copies of the ocean instance chain onto it; the last one has the slot name `MI_SwcWater_Ocean`. With
    `-SwcWaterMaterials=/SwcWaterCheck/Water/Near` only the ocean uses it; each other slot falls back to the plugin material.
  - The client (`UpdateUnrealOceanNear`, the Unreal mode only; the source path runs no new code): each 0.5 s the parameters SwcNearGain
    (`swc.WaterOceanNear`, default 0: no add), SwcNearColor (NearFogColor of the ocean of the place), SwcNearSun (the source sun / pi, a
    copy of the rule of the source path), SwcNearSigma (the density / 100 x `swc.WaterOceanExtinction`), SwcNearMode
    (`swc.WaterOceanNearT`), SwcNearDebug (`swc.WaterOceanNearDebug`). The plugin materials have no such parameters: no effect there.
  - The test (the editor build, `work/water-unreal/tone3/run_e.sh control | sweep | controls | source`): the gain-0 control first (the
    check parent against the plugin parent); then gain 1 with the extinction 1, 1.5, and 2 at the three beach views; the lakes and the
    rivers; the source mode. Stop with no default when the fit of N with the extinction fails.
  - The check (2026-10-29 10:28 to 11:02 UTC, the editor build of c19b2883, the check parent made at 10:32 UTC; the fixed exposure EV
    13.03; the source pictures: the editor run wnt-b3-source; evidence `water-unreal/wnt-e-*`, `near-term-sweep.png`):
    - Step 1: the script made M_SwcWaterNear and the ocean chain (all attributes from Break to Make; the parent has no customized UVs).
      The first version with Get / Set Material Attributes failed: those nodes make their pins only with an editor graph node, which a
      commandlet has not. Only the check plugin got new files. The first shader compile (the warm-up run) took the swap to +18.8 GB for
      a short time (a retail run of the backend project was also alive).
    - The gain-0 control (the check parent against the plugin parent): within 0.0 to 2.1 L* at the three views: PASS.
    - The sweep, the near band, Unreal minus source (N off x1; N on x1 / x1.5 / x2): ocean-far +10.5; +13.3 / +8.1 / +5.2; ocean-clear
      -4.0; -1.7 / +3.0 / +7.1; beach-down -9.9; -6.1 / -9.2 / -10.9. N adds about 2.5 to 4 L*; no gain-1 pair fits the three views
      (ocean-far needs x2 or more, ocean-clear at most x1.5, beach-down is outside at each scale). FAIL: no default, as planned. In the
      shallow water at beach-down the source N is small (the ramp sat(0.1 L) with L below 1 m), so the missing light there comes from
      another term [I] (the floor light, the reflection, or the source surface blend; not measured).
- The shore foam of the source ocean (2026-10-29; the study `~/Games/soulwarden-client/research/water-shallow-terms.md`, GPT-6.1-Sol;
  not built at the commit): the term that the Unreal water lacks in the shallow water at beach-down is the source shore foam, a white
  blend after the reflection in the band of the view-depth gap q below 1 / FoamSoftIntersectionFactor (0.83 m at the beach). A static
  replay of the source foam texture and formula matches the bright pattern of the source minus the Unreal picture with a correlation of
  0.929 on a held-out half [R by the measure; I for the model]. The source floor is not brighter: its T already has the down path.
  - The check parent `M_SwcWaterNear` version 3 (`Scripts/water_unreal_near.py`): a second Custom node SwcShoreTerm with the source rule
    (water_materials.py, the ocean foam: s, m from two samples of `T_SwcWater_FoamData`, the gloss `T_SwcWater_Gloss`, both read from
    the shared plugin and not changed; the source ocean coordinates); the first test is additive: gain x f x FoamAmount x 10000 on the
    emissive, with the minimum reflection shade 0.5 (an upper bias: the source blend is f x (FoamAmount x 10000 - C)).
  - The client: `swc.WaterOceanShore` (gain, default 0) and `swc.WaterOceanShoreDebug` (1: s, m / 2, f x 4); the values of the ocean of
    the place (FoamAmount, FoamTilling, FoamSoftIntersectionFactor, FoamTransitionSpeed, WindDirection, WaveSpeed, Tilling,
    SoftIntersectionFactor; WindScale 0.02) on the ocean instances, in the Unreal mode only.
  - The test (`work/water-unreal/tone3/run_e.sh shore`): each beach view with shore 1, shore 1 + near 1, and the extinction 1.5 and 2.
    Expected [I]: beach-down near band +6 to +9 L*, ocean-clear and ocean-far about 0. ocean-far (+10.5) stays a separate fault.
  - The check (2026-10-29 12:06 to 12:40 UTC, the editor build of 22f0ca98, the check parent version 3; the fixed exposure EV 13.03; the
    source: the editor run wnt-b3-source; evidence `water-unreal/wnt-f-*`, `shore-foam-sweep.png`): the gain-0 control within 2.2 L*
    (PASS). The near band, Unreal minus source (beach-down / ocean-clear / ocean-far): base -10.4 / -3.1 / +11.4; shore 1: -2.3 / +3.1 /
    +11.4; shore 1 + N 1: +1.1 / +7.8 / +12.9; + extinction 1.5: -1.1 / +7.0 / +8.1; + extinction 2: -1.3 / +5.6 / +4.3; shore 1 + N 0.5 +
    extinction 2 (a second run): -3.6 / +7.8 / (not run). The shore foam is the term of the shallow water (beach-down all bands within
    1.1 with shore + N). No tested setting has each near band within 5: the best (shore 1, N 1, extinction 2) misses ocean-clear by
    0.6, and the ocean-clear near band (the shore line with the moving foam) changes by about 2 L* between shots. No default.
  - The source blend (version 4 of the check parent, not built at the commit): the source C' = (1 - f) C + f x FoamAmount x 10000 as a
    coverage of the Single Layer Water surface. The opacity is the covered part (the water volume light and the floor get 1 - opacity,
    BasePassPixelShader.usf): the opacity takes f of the uncovered part, the base color of that new part is black, and the white is the
    emissive f x FoamAmount x 10000 [I: the reflection of the composite is not proved to scale with the opacity].
    `swc.WaterOceanShoreBlend` 1 (the default; with the shore gain 0 by default there is no effect), 0 the additive test of version 3.
    The drives: `run_e.sh repeat` (the additive best candidate three times at ocean-clear) and `run_e.sh blend`; `tone3/water_region.py`
    measures a water-only mask (the pixels of the lower half that the terms change). The near band of ocean-clear has no sea pixel in the
    terrain model of the study (land, the player, the shore line): its changes come from the shore edge.
  - The window of 2026-10-29 12:59 to 13:24 UTC (the editor build of 6aac3073): NOT VALID. Version 4 failed to compile ("Failed to compile
    Material for platform SF_VULKAN_SM6, Default Material will be used"; "(Node MakeMaterialAttributes) Error on property BaseColor": the
    float2 output of the cover node through component masks). The ocean drew with the default material in each run (the repeat and the
    blend runs: +35 to +46 L*). Version 5: two Custom nodes (SwcShoreOpacity float1, SwcShoreBase float3), no component masks. The
    rule for the next window: no run after a warm-up with a "Failed to compile" line.
  - The check of version 5 (2026-10-29 13:25 to 13:49 UTC, the editor build of 36496b20; the warm-up gate passed: 0 "Failed to compile";
    0 in each test run; the fixed exposure EV 13.03; the source: the editor run wnt-b3-source; evidence `water-unreal/wnt-g-*`,
    `shore-blend-sweep.png`):
    - The repeat (the additive best candidate, 3 shots at ocean-clear): the near band +7.0, +5.6, +3.0 (a spread of 4.0 L*); the base
      (no treatment) +6.3, against -3.1 and -4.0 in earlier runs. The near band of ocean-clear is a noisy measure (the shore line).
    - The source blend, the near band (beach-down / ocean-clear / ocean-far): base -10.0 / +6.7 / +11.9; shore + N + extinction 1.5: -1.5
      / -2.0 / +8.3; shore + N + extinction 2: -3.8 / +3.5 / +4.9; shore + extinction 2: -4.9 / -3.1 / +4.7. Thus shore 1 + N 1 +
      extinction 2 has each near band within 5 [one shot each; the ocean-clear spread above]. The far bands stay outside: ocean-far far
      -6.4, beach-down far -8.0 and middle -6.2.
    - The water-only region (the lower-half pixels that the treatment changes, 355,000 to 423,000 pixels: wide, it takes the shot drift too):
      shore + N + extinction 2: beach-down -5.3, ocean-clear -0.1, ocean-far +3.0.
    - Not done: the controls (the lakes, the rivers, the source mode, the underwater view, the far join) and the cooked build (it needs
      the check material in the shared plugin: the word of the orchestrator).
  - The controls and the repeat (2026-10-29 14:23 to 14:55 UTC, the editor build of f5868fb6; the compile gate passed; 0 compile faults in
    each run; evidence `water-unreal/wnt-k-*`, `shore-controls-faults.png`): the best setting (shore 1 + N 1 + extinction 2, the blend)
    against all off, at the same place.
    - The repeat, the near band, Unreal minus source (3 shots): ocean-far +5.3 / +5.5 / +5.7; ocean-clear +7.2 / +7.3 / +5.9; beach-down
      -4.7 / -2.3 / -3.8. FAIL at ocean-far (just above 5 each time) and ocean-clear; the single pass of the last window (+4.9, +3.5) was
      inside the spread. No default.
    - The source mode (the automatic exposure): the EV of the frame -0.02 and +0.02 EV, the bands within 2.2 L*: PASS (no new code runs).
    - The lake: within 0.6 L*: PASS. river-down -3.7 to -4.6 and river-forest up to -2.1: the grass and the wood are darker too (a change of
      the light between the shots, not the ocean switches) [I; an A/A pair would prove it].
    - The underwater view: FAIL. swim-surface -24 to -41 L*: the camera is under the surface in the best shot, and the underside of the
      surface shows as an opaque yellow-green plane (the blend adds opacity on the underside too). The terms need an underside gate
      (TwoSidedSign or the camera-underwater state; the study of the near term). swim-under -6.9 in the far band.
    - The far join: NOT VALID (the pictures are near black, L* about 3; the frame EV at the sky place is 17.03, not 13.03). The cause
      [R by the logs]: `swc.ExposureFixed 1` keeps the bias curve of the source rule on the post process, and Unreal applies that curve
      in the manual mode too (PostProcessEyeAdaptation.cpp, GetAutoExposureCompensationFromCurve). With the bias -10.5 the frame EV is
      the lower EV bound of the preset + 8.03 [I for the rule]: 13.03 in each beach, lake, river, and underwater run (the preset EV 5 to
      17), 17.03 at the sky place (firstlight_jungle_a, EV 9 to 18). The far-join check uses the bias -6.5 (-14.5 gave EV 21.03: a lower bias gives a higher EV) and is valid only when
      the meter lines of the shots give EV 13.03.
  - The underside gate (version 6 of the check parent, the word of the orchestrator 2026-10-29 14:56 UTC; not built at the commit): the
    Custom nodes SwcNearTerm, SwcShoreTerm, and SwcShoreWeight get the input Above (the TwoSidedSign of the pixel) and multiply their
    result by saturate(Above): the near term, the shore foam, and the blend (the weight f, thus the new opacity and the base color scale)
    are for the top side of the surface only. The underside keeps the plugin look, as the plugin coefficients of the water
    (x saturate(TwoSidedSign)). Expected: swim-surface as with all terms off; no change of the views from above.
  - The study of a depth-dependent extinction (GPT-6.1-Sol; brief `research/briefs/ast200-2c-depth-extinction.txt`, result
    `~/Games/soulwarden-client/research/water-depth-extinction.md`): first the source formula, then the Unreal parameters. The result
    (2026-10-29, read only):
    - The source has one ocean density for each local override: no density rule over the depth or the distance to the shore [R]. The
      depth enters only through the path lengths (the view path and the vertical light path), as in the plugin.
    - The pixel fit of the repeat error goes the other way: at ocean-far the error falls from +4.3 L* (3.6 to 5.3 m of model depth) to
      -7.3 (9 to 10 m). At the same depth (8 to 9 m), the dark source pixels are +6.1 and the bright ones -10.4: the Unreal water loses
      the spatial contrast of the source (the reflection) [I]. A depth ramp of the extinction or of the albedo is rejected.
    - The near band of ocean-clear is mostly land and shore line (no fixed water pixel in it); the water mask of ocean-clear is -6 to
      -7 L* too dark with the best setting. Two source runs (b3, d) differ by 7.9 L* in that near band: the band is not a water measure.
    - The N term must use the source density (0.3 per m) and not the test scale of `swc.WaterOceanExtinction` [R: a formula difference].
    - The next supported test [I]: the reflection normal of the ocean (the source ReflectionBumpScale 0.1: a flatter normal), as a
      parameter of the check parent, the top side only.
  - The acceptance rule of the near tone (the word of the orchestrator, 2026-10-29 15:18 UTC; it replaces the near band as the rule):
    1. The measure is the fixed water mask of each beach view (beach-down, ocean-clear, ocean-far): a pixel set from the terrain model
       (the pixels whose view ray meets the ocean at 40 m and then the terrain model below it, in a fixed rectangle of the view), made once
       by the study and saved as files (`work/water-unreal/tone3/masks/VIEW-noon.png`, with `SHA256SUMS`). Each later run uses the same
       files; the measure tool `tone3/water_mask.py` stops when a file changed.
    2. Each window takes 2 source shots of each beach view (`run_m.sh source`: the base and `@s2`). The reference is the mean of the two
       shots; their spread is the drift of the window.
    3. The limit (provisional): on the water mask, |L* (Unreal) - L* (source)| <= 5 and |hue (Unreal) - hue (source)| <= 15 degrees. The
       hue is atan2(mean b*, mean a*) of the mask. The user asked for the dark original tone: the hue counts.
    4. The old near band stays in each report beside the new measure.
    5. ocean-clear has two masks (the word of the orchestrator 2026-10-29 16:05 UTC): `ocean-clear-open-noon.png`, the open water right of
       the cliff reflection (the rectangle 1070 360 1230 450, 13,904 pixels, made once by `tone3/make_mask.py` with the geometry code of
       the study; the tool makes the first mask again bit for bit), with L* and hue; the first mask `ocean-clear-noon.png` with the hue
       only.
    - The source drift of the open water of ocean-clear [R]: in the mask rows the cliff reflection ends at x 1040 to 1050 in each of the 6
      source shots (b3, d, k, m); the open water right of it changes its lightness between shots (L* 45.7 and 60.9 in the two shots of
      the window of 15:44; 50.9 in b3), at a stable hue (205.6 to 210.1). The Unreal shots of the same mask are stable (37.2 to 38.4).
      Thus the L* drift of ocean-clear is a change of the source water over time, not the cliff edge [the cause is not known: the sky
      cube or the clouds, I]. The statement of 16:05 ("the edge of the cliff reflection moves") is corrected here.
  - The hue study (GPT-6.1-Sol, read only; brief `research/briefs/ast200-2c-hue.txt`, result
    `~/Games/soulwarden-client/research/water-hue.md`):
    - The stable error is too little blue against green [R]. The best setting keeps (as display-linear RGB of the fixed masks, against
      b3) at ocean-far R 0.91, G 0.98, B 0.82 of the source; ocean-clear 0.95 / 0.71 / 0.55; ocean-clear-open 0.70 / 0.54 / 0.38. The
      source B/G is 1.17 (far) and 0.99 (clear); the Unreal B/G 0.97 and 0.77. The later source pair gives the same blue loss.
    - The client puts no green bias in the coefficients [R]: the extinction is the same in each channel, and the albedo has the RGB ratio
      of the source deep fog color. The loss is in the rendered sum (the light, the surface weights, the reflection) [I]; which HDR term
      is not known [U].
    - A scalar albedo scale moves the hue only by 4 degrees (4 to 1: 188 to 194) and loses too much L*.
    - The supported test [I]: an ocean-only blue gain of the scattering albedo, with the extinction of each channel held (sigma_t =
      sigma_s + sigma_a fixed; albedo' = albedo x (1, 1, gain); the MID Scattering is sqrt(1000 sigma_s), the MID Absorption 1 / sigma_a).
      Gain 1.25 first, default 1. Expected by a display check: ocean-far hue +13 to +25 degrees at an L* change below 0.4; ocean-clear
      part of its hue; no fix of the L* of ocean-clear-open; beach-down little change.
  - The blue test (the word of the orchestrator 2026-10-29 16:21 UTC; not built at the commit): `swc::water::albedo_gain` (SwcWaterData.cpp;
    the extinction of each channel held, the albedo at most 0.95; 4 checks in `tools/test-water.sh`) and `swc.WaterOceanScatterBlue`
    (default 1: no change; ApplyUnrealTone, the ocean tone only: the main and the far ocean instances; lakes and rivers keep their tone).
    The window: the Mix sweep and an A/B/A of the blue gain (1 / 1.25 / 1) at ocean-far between two source shots; then at ocean-clear and
    beach-down only when ocean-far moves its hue toward the source by more than its A/A spread (`tone3/blue_gate.py`).
  - The window of the Mix sweep and the blue A/B/A (2026-10-29 16:52 to 17:25 UTC, the editor build of bdf3c5ff; the compile gate passed: 0
    "Failed to compile", swap +10.8 GB; 0 compile faults in each run; each of the 45 shots at EV 13.03; the backend release 2026-10-29-ef9d8aa,
    schema 21; evidence `water-unreal/wnt-n-*`, `near-blue-mix.png`; the table `work/water-unreal/tone3/n-water-mask.txt`):
    - The source shots (shot 1 before, shot 2 after the Unreal runs), the drift on the masks: beach-down 0.2 L*; ocean-clear 3.4 L* and
      6.4 degrees; ocean-clear-open 4.1 L* and 7.2 degrees; ocean-far 5.5 L* and 4.5 degrees (36.3 / 41.8).
    - The blue decision: yes (ocean-far: the source shot 1 hue 219.2; @b1a 187.8, @b125 204.6, @b1b 194.2; the move +13.6, the A/A
      spread 6.5).
    - The best setting with blue 1 (the 4 shots @b1a, @b1b, @m1a, @m1b) against blue 1.25 (one shot), dL* and d hue against the mean of
      the 2 source shots:

      | Mask | Blue 1: dL* (spread), d hue (spread) | Blue 1.25: dL*, d hue |
      |---|---|---|
      | ocean-far | +1.7 (2.5), -24.8 (6.5) | +2.3, -12.3: PASS |
      | ocean-clear (the hue only) | -8.9 (2.6), -25.4 (2.0) | -8.2, -17.1 |
      | ocean-clear-open | -15.3 (0.8), -36.4 (0.6) | -15.3, -26.9 |
      | beach-down | -5.9 (5.1), +10.2 (10.9) | -6.2, +10.7 |

      The blue gain moves the hue toward the source by +12.5 (far), +8.3 (clear), +9.5 (clear-open) degrees at no L* change; beach-down
      does not change (the floor and the foam), as the study expected. The lightness of the open water of ocean-clear (-15) stays.
    - The Mix sweep: the mean d hue for Mix 1 / 0.5 / 0.25 / 0.1: ocean-far -23.6 / -24.8 / -24.9 / -27.4; ocean-clear -24.9 / -25.6 /
      -26.1 / -23.1; ocean-clear-open -36.1 / -36.3 / -36.7 / -34.1; beach-down +7.8 / +9.8 / +13.4 / +16.9 (its spread 10.9). No effect
      beyond the shot spread at the ocean views; the flatter normal does not give the sharp mirror reflection of the source (the cliff
      and the statue at ocean-far): the source reflection is a different term, not the normal [I]. Mix stays 1.
  - The reflection study (GPT-6.1-Sol, read only; brief `research/briefs/ast200-2c-reflection.txt`, result
    `~/Games/soulwarden-client/research/water-reflection.md`):
    - The sharp cliff of the source path is the screen-space scene trace; the sky fill (the current-sky cube) replaces the material cube
      [R]. Unreal mode 1 (`r.Water.SingleLayer.Reflection 1`, the default) uses software Lumen reflections (the project uses Lumen; the
      imported meshes have no distance fields and no Lumen cards), then the captures and the skylight (`swc.SkyLightSource 1`: the
      imported world probe cube, not the current sky). The plugin roughness is 0.02 to 0.1, F0 about 0.018 (the source floor 0.065) [R].
    - Measures [R]: the source open water of ocean-clear has a tile SD 0.84 to 0.89 and a cliff silhouette step +4.8 to +7.7 L*; Unreal
      2.5 to 2.9 and -0.9 to +0.9; Mix 0.1 removes the fine texture (1.1) but not the dark mean (L* 37) nor the weak cliff step. Two
      faults [I]: too much small-scale distortion, and a weak or missing coherent reflection (the scene and the bright sky).
    - The supported next test [I]: `r.Water.SingleLayer.Reflection` 1 / 2 / 3 (1 Lumen, 2 the cubes only, 3 SSR with the cube fallback)
      at the blue 1.25 baseline, 2 shots each (order 1, 2, 3, 3, 2, 1), with source shots. An engine cvar for all Single Layer Water: a
      diagnostic only, never a shipped ocean-only change. Later, in the check parent only: a roughness override (plugin, 0.02, 0.005,
      0.0005), and a source-shaped sky term (the current-sky cube, the source Fresnel, the soft intersection, the clamp; default gain 0).
    - The earlier pictures by this rule (the source: b3 and d; the best setting r1 / r2 / r3): beach-down L* -5.1 / -4.2 / -5.7, hue
      +7.9 / +5.1 / +8.1; ocean-clear L* -7.9 / -8.0 / -9.2, hue -30.4 / -28.9 / -27.8; ocean-far L* -0.4 / -0.7 / -0.4, hue -20.4 / -21.1
      / -20.0. All fail but one beach-down shot: the deep water is as dark as the source, but greener (the source hue 214, the Unreal
      water 194). The two source runs differ by 3.5 L* at ocean-clear on the mask (7.9 in the near band) and by 0.1 at the other views.
  - Version 7 of the check parent (the word of the orchestrator 2026-10-29 15:18 UTC; not built at the commit; the defaults change
    nothing):
    - `SwcNearRho` (`swc.WaterOceanNearRho`, default 0 not set): the own density of N per m; below 0 the source density of the ocean of the
      place. Not set, N uses the floor sigma (with the test scale of `swc.WaterOceanExtinction`), as before.
    - `SwcNormalMix` (`swc.WaterOceanNormalMix`, default 1): a Custom node between Break Normal and Make Normal, normalize(lerp((0, 0, 1),
      the plugin normal, Mix)) on the top side; Mix 1 and the underside give the plugin normal unchanged. (0, 0, 1) is the flat normal of
      the horizontal water mesh in the tangent space and in the world space. The final normal also changes the refraction, the Fresnel,
      and the sun light: not a reflection-only test. The sweep 1 / 0.5 / 0.25 / 0.1 is for a later window.
  - The window of version 7 (2026-10-29 15:31 to 16:04 UTC, the editor build of 394ec7c0; the import 15:31:57 (the parent normal is in
    the tangent space); the compile gate passed (0 "Failed to compile", swap +8 GB); 0 compile faults in each run; evidence
    `water-unreal/wnt-m-*`, `near-gate-window.png`):
    - The source shots (the base and @s2; each shot at EV 13.03), on the fixed masks: beach-down L* 52.10 / 51.24, hue 95.2 / 96.4;
      ocean-clear 43.93 / 53.40, hue 195.5 / 189.9; ocean-far 41.93 / 37.96, hue 215.8 / 214.5. The drift of ocean-clear (9.5 L*) is
      larger than the limit: the edge of the cliff reflection in the mask moves between the two source shots. The L* of that mask is not a
      usable measure; its hue is (5.6 degrees).
    - beach-down, one shot of the best setting (version 7 with the defaults; above the water the same as version 5): water L* -6.7, hue
      +19.6: FAIL (the near band -2.1); the earlier shots of version 5: -4.2 to -5.7, hue +5 to +8 [the foam phase; one shot].
    - The underside: the swim-surface camera (5 cm above the mean ocean level) is above the waves in some shots and under them in others.
      This window: the base (all off) is under the waves and shows the yellow-green plane; @best is above. The last window: the reverse.
      Thus the plane is the underside look of the plugin, not the blend; the statement of 14:55 ("the blend adds opacity on the
      underside") was wrong. swim-surface is not a valid view for the gate. swim-under (the camera 0.8 m under): @best against the base
      far +3.5, middle -0.6, near -0.2 (the last window, version 5: far -6.9): the gate leaves the underside as the plugin [within the
      shot spread].
    - The far join at EV 13.03 (a second run with the bias -6.5: the bias -14.5 gave EV 21.03; a lower bias gives a higher EV; each shot
      at EV 13.03): @best against the base, sky-east far -0.1, middle -0.8, near -2.0; sky-south +0.4 / -0.3 / -1.4. No new seam; the
      green haze of the place is also in the source path and in the far-ring pictures of 2026-10-26. PASS.
    - The rivers, A/A (@a1 and @a2 all off around @best): river-down A/A spread 0.4 to 0.7 L*, @best minus the mean of a1 and a2 -0.1 to
      +0.2; river-forest spread 0.7 to 1.0, @best +0.1 to +0.6. The ocean switches do not change the rivers; the -4 of the last window was
      the light between the shots. PASS.
- The floor tests of the ocean (off by default): `swc.WaterOceanExtinction S` (S x the extinction of the ocean tone with the same
  albedo), `swc.WaterOceanMaskOffset` (cm; the plugin "Water Opacity Mask Offset", -24); the SSR test with the engine cvar
  `r.Water.SingleLayer.Reflection` 2 and 3 against 1 (`run_c.sh floor`).

### Stage 2b (after the full cook)

- The storm waves of the overrides. The GPT-6.1-Sol study `~/Games/soulwarden-client/research/water-storm-waves.md` (2026-10-25;
  brief `research/briefs/ast200-2b-storm-waves.txt`) found these rules:
  - The source has one shared FFT field for the whole ocean [R]. An override does not make a new field.
  - `WavesSize` S multiplies all the displacement (vertical and horizontal) linearly. `WavesAmount` M divides each wave length.
    The FFT time runs at 0.125 x the frame time, and no authored value changes it. The wind values do not turn the displacement;
    they only move the detail normals [R].
  - The 8 s transition blends only the size (and the wave speed value). The amount and the wind values change at the end. The
    override shape selects the values for the view only, so the whole ocean of the view changes [R]. One client ocean body thus
    agrees with the source.
  - The main wave lengths are 80 / M m and 40 / M m (the two copies of the field), both with a period of 6.40 s. The 4-sigma
    height scale is 2.006 x S m: 0.16 m for the mission ocean, 0.30 m at the beaches, 0.60 m in the Cutlass Keys storm, and 1.40 m
    for the "Ocean" override [I]. The real peak-to-trough height depends on the random field and the time [U].
  - The Gerstner waves of the plugin cannot match exactly. Unreal derives the period from the wave length, and the source uses one
    period for two lengths. The source also shears the horizontal displacement [P, I].
  - The rule for the client: keep one wave list, scale its amplitude and steepness with S (also in the transition), and divide its
    lengths by M at the end of a transition. The study gives a 24-wave explicit list as an approximation [I]. For a size of 0, use an
    outer scale of 0 (the generator clamps an amplitude to at least 0.0001 cm). Full parity needs a custom displacement evaluation.
  - The client code (2026-10-25, not built yet; the decision of the orchestrator: the approximation first):
    `swc::water::ocean_waves(S, M)` (SwcWaterData, tested by `tools/test-water.sh`; the list equals the 24-wave lists of the study for
    the four cases, within 3e-10) gives the 24 waves. `USwcOceanWaveGenerator` (Water/SwcWaterBodies) gives them to a
    UGerstnerWaterWaves of the ocean body. S is the WavesSize of the ocean of the place, blended in the transition (OceanNow). M is
    the WavesAmount of the target, applied at the end of the transition. A size or an amount of 0 gives no wave (the source
    displacement 0). In a transition the list is computed again at most each 0.1 s (each time rebuilds the GPU wave data).
    `swc.WaterOceanWaves 1` turns it on; the default 0 keeps the waves of the plugin until the check of stage 2b. The custom
    displacement in the material waits until a view shows that the approximation is not good enough.
- The shore foam decals on Single Layer Water (a deferred decal can project onto the water surface), the caustics (a static switch
  of the plugin material: it needs material instances), the river flow direction proof, and the cost in the cooked build.

## The source water shader (AST-200, 2026-10-20)

The study: `~/Games/soulwarden-client/research/water-shader.md` (GPT-6.1-Sol; the retail shaders water.cfx, watervolume.cfx,
WaterFogVolume, WaterReflPS, and the CPU uploads). The material `M_SwcWaterSource` is translucent and unlit; one Custom node has the
source formulas. It reads the opaque scene behind the water (the scene color and the scene depth of the translucency pass). Unreal
applies the air fog to its output. Single Layer Water could not do it: its composite adds its own reflection (EnvBRDF), its own
Fresnel, and its own light in the water. Units: one radiance unit of the source is 10,000 cd/m2; the sun color of the source is the
Unreal sun E / pi (`View.DirectionalLightColor`).

| Part | Rule | Source |
| --- | --- | --- |
| Kinds | Kind 0: the water volumes and the rivers (watervolume.cfx); Kind 1: the ocean (water.cfx, WaterFogVolume/OceanInto). | research "The rule for the client" |
| Coordinates | A river: q = (along, across) of the mesh UV [inference: the adapter of the research], B.xy = q Tilling - f t (0.25, 0), B.zw = (2 q Tilling - f t (1, 0)) DetailTilling. A volume: the mesh UV (the world m x SurfaceUScale, SurfaceVScale). The ocean: the world m x 0.005, the detail axes swapped, max(2, DetailTilling). | V-VS, O-VS |
| Normals | b = W(B.xy / 4) + W(B.xy); b.xy += W(B.zw).xy x DetailNormalsScale; b.xy x= NormalsScale x NormalStrength. W is still the static `oceanwaves_ddn` (the source field is generated at run time). NormalStrength 0.05 is a calibration of the client (0.15 gave sun glints that retail does not have). | V 0026-0044, O 0054-0077 |
| Volume water term | Cfog + T (Cbehind - Cfog), T = exp(-density x the view path in the water, m), Cfog = FogColor x FogColorScale x the sun color (FogColorAffectedBySun: true). | V 0158-0166, CPU 0x5EB8818 |
| Ocean water term | k = 1.4427 density; j = k (d.z - 1); I = (2^(j L) - 1) / j; T = 2^(-k (L + the depth of the scene point under the surface)); Cdeep I + Cshallow T sat(0.1 L) + Cbehind T, with Cdeep = FogColor x fogColorMultiplier x the sun and Cshallow = nearFogColor x the sun. The full ocean path clears the simple surface fog (cFogColorDensity 0: SurfaceFog 0). | Ocean-fog 0013-0041, water-uploads.md 2 |
| Fog colors and sun | The stored float RGB of FogColor and nearFogColor, with no sRGB decode (FogSrgb 0). The sun color S: the time-of-day sun RGB of the source at its luminance, Sun color / Y x Sun intensity (lux) / pi (`USwcEnvironmentSubsystem::GetSourceSunLight`; `swc.WaterSourceSun 0`: the Unreal sun light, the rule before part 3). | water-uploads.md 1 |
| Reflection | A screen-space trace of the opaque scene: the reflection normal mix(up, N, 0.3) (volumes) or mix(up, N, ReflectionBumpScale) (the ocean); 64 steps in the screen with a jitter, 2000 m, the thickness min(5 m, depth / 20), 16 binary steps; no hit on the far plane or in front of the surface; the alpha min(sat((0.6 - k) / 0.25), 0.95), k = dot(abs(forward), N). The sky fallback fills 1 - alpha. The volumes: (the scene x alpha + the sky) x RealtimeReflMul. The ocean: mix(the material cube x ReflectionScale, the trace, alpha), the RGBA of the trace clamped to one source unit. No history filter yet. | V-refl, O 0142 onward |
| Sky | SkyCubemapParams.x = 1 for the ocean and the volumes. The sky cube: a scene capture of only the sky (the sky sphere `SwcSkySphere` with the atmosphere, the clouds, and the fog) at the view, 128 texels, HDR, each 1 s (the capture rate is a decision of the client); the reflection direction, mip 0, gain 1. A trace coordinate outside the screen ends the trace with no hit: the sky fills it. No screen-edge fade of a hit. | water-uploads.md 3, 4 |
| Sun | sat(N.L) x ((p0 / 2 pi + 1 / pi) mu^p0 + (p1 / 2 pi + 1 / pi) mu^p1) x Specular x the sun, p0 = 4 x 255 g, p1 = 0.5 x 255 g, g = Shininess / 255. | V 0047-0059, O 0547-0554 |
| Fresnel | Volumes: sat(0.05 + 0.95 (1 - NoV)^5); the blend x the soft edge a x sat(zS - 0.33). The ocean: 0.065 + 0.935 (1 - NoV)^5 / (40 - 39 FresnelGloss) (0.59 at a grazing view), x a. | V 0168-0175, O 0555-0561 |
| Soft edge | a = sat(SoftIntersectionFactor x q), q = the view-depth gap behind the surface (m); the refraction offset N.xy x RefractionBumpScale x a (an offset onto a nearer surface takes the pixel itself). | V 0121-0157 |

The ocean values: the .mtl of the ocean material (Tilling 32, NormalsScale 1.5, SoftIntersectionFactor 1.2, RefractionBumpScale 0.6,
ReflectionBumpScale 0.1, FresnelGloss 0.98, ReflectionScale 0.2, Specular 0.423, Shininess 255), the fog of the override (the
starting beach: FogColor 0.0118, 0.137, 0.235, multiplier 0.07, density 0.3, near color 0.0039, 0.102, 0.118), and the environment
cube `environmentcube_worldocean_cm` (`T_SwcWater_OceanCube`, x 10,000 cd/m2) under the trace. The rules of the sky, the fog colors,
and the surface fog come from the second study `~/Games/soulwarden-client/research/water-uploads.md` (2026-10-20); they replace the
choices by the pictures of part 1 (FogSrgb 1, SurfaceFog 1, the screen sky sample). Not done yet: the view-direction bend of the trace
and the river reflection history (0.75 current + 0.25 previous).

### The check of the reflection and the color (2026-10-20)

The matched views (`tools/match-view.sh`; evidence `~/Games/soulwarden-client/evidence/AST-200/`, `part1-set2`, `part1-set3`), the
water dE and the water L* by `tools/lib/water_part.py` (`part1-water-part.txt`), against the E5 step 3 values
(`evidence/E5/step3-final/water-part.txt`). Two runs of the same code can differ by about 4 dE (the clouds and the sky).

| View | Water dE, step 3 | Water dE, AST-200 | Water L* (retail) | Saturation (retail) |
| --- | --- | --- | --- | --- |
| river-forest-noon | 26.5 | 16.8 | 48.4 (28.9) | 0.311 (0.369) |
| river-highlands-noon | 38.1 | 20.8 | 45.6 (22.5) | 0.361 (0.334) |
| ocean-clear-noon | 31.4 | 18.1 | 54.1 (36.7) | 0.269 (0.430) |
| ocean-storm-noon | 2.1 | 10.3 | 15.0 (28.6) | 0.505 (0.375) |
| swim-idle | 38.5 | 8.0 | 31.2 (24.4) | 0.479 (0.788) |
| swim-nav | 37.0 | 7.1 | 29.6 (21.7) | 0.487 (0.741) |
| The mean of the 6 views | 28.9 | 13.5 | | |

What the pictures show: the cliffs, the banks, and the houses reflect in the water; the swim water is deep blue with the sky in the
near water, and the swimmer is under the surface. Open:

- The scene light: the whole client picture is lighter than retail (the E5 contrast work, paused): the river beds and the sky in the
  reflections are too light. The storm coast has a black sky in the client (an environment fault); the ocean reflects it.
- Part 1 had choices by the pictures (sRGB fog colors, the ocean surface fog, the screen sky); part 2 replaces them by the rules of
  water-uploads.md. The part-1 trace also had blocks and bars at the sides of the screen: they came from the world probe at mip 0
  in the sky fallback (a probe of a forest place with its sun), not from the trace hits; the captured sky cube removes them.
- The normal field of the source (generated at run time), the ocean displacement and crest foam, the caustics, the river edge fades.
- The swim pose: the client swimmer is upright (the feet 1.4 m deep); the retail swimmer lies near the surface.

### The rules of the second study and the shore foam (part 2, 2026-10-20)

The same views with the rules of water-uploads.md (evidence `part2-set2`, `part2-set3`, `part2-water-part.txt`,
`part2-side-sheet.png`):

| View | Water dE, part 1 | Water dE, part 2 | Water L* (retail) | Saturation (retail) |
| --- | --- | --- | --- | --- |
| river-forest-noon | 16.8 | 21.4 | 52.8 (28.9) | 0.302 (0.369) |
| river-highlands-noon | 20.8 | 25.1 | 48.6 (22.5) | 0.345 (0.334) |
| ocean-clear-noon | 18.1 | 30.2 | 66.5 (36.7) | 0.243 (0.430) |
| ocean-storm-noon | 10.3 | 1.1 | 27.5 (28.6) | 0.383 (0.375) |
| swim-idle | 8.0 | 26.9 | 51.9 (24.4) | 0.401 (0.788) |
| swim-nav | 7.1 | 23.8 | 47.7 (21.7) | 0.454 (0.741) |
| The mean of the 6 views | 13.5 | 21.4 | | |

The source rules make the water lighter than the picture choices of part 1: the reflected sky of the client (the capture of its sky)
and the scene light are lighter than retail (the E5 contrast work), and with one water fog the body under the surface keeps more of
its light. The storm coast now agrees (dE 1.1). No block and no bar is left in the six views.

The shore foam (research water-shader.md "Shore particles and decals"; `tools/convert-water.sh shore`, SWC_CONTENT/water/shore.json):

- The emitters: the placed particle emitters (component 65bc817a, the effect name crc:56249fe1 / crc:701787f5) of
  Env_Island.Water.ShoreFoamWaveDecal, _wide, and _narrow: 178 emitters in r_+03_+01, r_+04_+00, and r_+04_+01 (95 in r_+04_+01, as
  in the research). Their local +y points to the land (the ground rises along +y at 176 of the 178).
- The sheets: the root and the children with Facing "Decal" of libs/particles/env_island.xml: the spawn delay, the life 5 s
  (Count 1, Continuous: one sheet at a time), LocalSpawnPosOffset and its random part, LocalInitAngles z, the SizeX, SizeY, Alpha, and
  Color curves by the particle age, and the level of detail (the base particle under the first LodParticle distance, then each level:
  for example ShoreFoamWaveDecal 0 to 60 m, its random sheet 0 to 30 m and 60 to 100 m). The splash children are not used.
- The curves: a key has t, v, d (the slope to its right), s (the slope to its left); a cubic Hermite curve between keys, 0 for a slope
  that is not written [inference: the spline evaluator of the engine]; the value = base x (1 + Random x (2 r - 1)) x the curve, r new for
  each sheet cycle (`swc::water::Curve`, `sheet_age`, `sheet_random`; tests in `tools/test-water.sh`).
- The placement [inference]: the sheet lies along the local x (the shore line), turned by its z angle; it runs up the beach along +y
  from its pivot (PivotY 1), with the sizes as half sizes; the front of the foam (the texture top) is at the land end.
- The decal `M_SwcShoreFoam` (water_on_shore_decal.mtl): the diffuse water_o_ondeck001 with its alpha x Diffuse (0.578, 0.784, 1) x
  the age color; the opacity the texture alpha x DecalDiffuseOpacity 0.7 x the age alpha; the normal water_o_onshore_001; Specular
  0.0212. The projection depth: the Thickness, at least 1 m to each side. The decals are made for the emitters in 120 m.
- The foam band of the water shader (part 1) stays: with the ocean values its strength is about 4 % in the first 0.42 m of the depth
  gap, so the visible beach foam comes from the decals.

Videos (before the rules of the second study; the foam and the swim are the same): `evidence/AST-200/ast200-shore.mp4` (the shore line
of the starting beach with the foam sheets in their 5 s cycles; the camera faces along the beach) and `ast200-swim.mp4` (the swim in
the bay: the cliff reflections, the waves, the swimmer under the surface). Drives: `tests/drive/ast200-shore*.txt`, `ast200-swim*.txt`.

Open: the splash children of the shore effects, the exact particle spline and random rules, the older `water_on_shore` particle
effect of the dungeon volumes, the ocean crest foam (the displacement), the caustics, the trace bend and the river reflection history.

### The water body and the surface level (part 3, 2026-10-22)

The views swim-idle, swim-nav, river-forest-noon, and ocean-clear-noon (`~/Games/soulwarden-client/evidence/AST-200/part3/`: the crops
retail | the Unreal sun | the source sun, `water-part.txt`, `swim-idle-parts.png` with the debug views 2, 3, 5, and 6).

- The parts of the swim-idle water (the debug views; the display values made linear): the water term Cw is dark teal (the floor at
  T near 0 from about 3 m of path, the body faint), near retail. The reflection (the sky cube where the trace has no hit, the cliffs
  where it hits) is about 10 times Cw in the near water; with the ocean Fresnel (measured blend weight 0.07 to 0.19, the formula gives
  0.07 to 0.14 at these angles) it makes the near water about 2 times Cw. Thus the pale, milky water is mainly the reflection, not the
  water fog: the fog, the density per metre, the path, and the single fog of the full ocean path agree with the study.
- The scene light: above the water (the sky and the cliffs) the client is 14 to 17 L* lighter than retail in the swim views and 10.6
  L* in river-forest (the E5 contrast work); the water there is 18 to 19 and 11 L* lighter. In ocean-clear the client is darker above
  the water (-3 L*) but 14 L* lighter on the water: the far water is pale cyan-white at a grazing view (Fresnel up to 0.59 of the sky
  cube, whose horizon is the hazy client sky).
- The rule now: the sun color S of the water is the source sun of the hour (research water-uploads.md 1 [R]), not the Unreal light
  (its color over the atmosphere: about 24 % brighter and bluer at noon; 40.6k, 35.5k, 36.1k cd/m2 against 38.4k, 30.4k, 23.5k).

| View | Water dE (Unreal sun) | Water dE (source sun) | Water L* (retail) | b* (retail) |
| --- | --- | --- | --- | --- |
| swim-idle | 15.5 | 14.3 | 40.0 (24.4) | -9.3 (-15.4) |
| swim-nav | 15.4 | 13.2 | 37.3 (21.7) | -8.6 (-12.0) |
| river-forest-noon | 10.8 | 10.3 | 40.5 (28.9) | +3.4 (+7.3) |
| ocean-clear-noon | 14.7 | 13.4 | 50.7 (36.7) | +4.5 (+0.2) |
| The mean of the 4 views | 14.1 | 12.8 | | |

Open [U]: the content and the level of the retail sky cube (`$SkyCubemap`: the sky draw, the fog stage, the clouds) and the exact
ocean reflection order (the clamp of the trace, the sky fill, the material cube); a static study is asked (AST-200).

### The reflection rules of the sky study (part 4, 2026-10-22)

The GPT-6.1-Sol study `~/Games/soulwarden-client/research/water-sky-reflection.md` (sections 1 to 5). Parent version 16 of
`M_SwcWaterSource`:

- The ocean (O): the final normal Nb = normalize(mix(U, N, ReflectionBumpScale)) (no foam normal yet) for the trace, the sky, the sun
  lobes, the Fresnel, and the facing test (before: the raw N for the sun and the Fresnel). The volumes (VR): the flat U for the trace and
  the sky; the full N for the sun and the Fresnel (V). Not done: the screen warp of the volume reflection target (V 0060-0119).
- Kept, as the study proves: the sky fill replaces the material cube (alpha 1), the ocean clamps the filled reflection to one source
  unit before the sun lobes; the volumes do not clamp. ReflectionScale 0.2 has no weight on the ocean sky fill.
- Not done (a limit): the sun depth shadow factor clamp(shadow, 0.35, 1) at the water point on the filled reflection (an unlit
  translucent material has no simple sun shadow in Unreal). In open views it is near 1.
- The sky cube: the source cube has the base sky (Mie and Rayleigh) and, by default, the cube fog stage; the second draw list (0x19)
  is not recovered, thus the clouds in the cube are not proved. `swc.WaterSkyCube` (bits: 1 the clouds, 2 the height and volumetric
  fog; default 3, the capture before part 4).

| View | e73ab25 | v16 | v16, cube 2 (no clouds) | v16, cube 0 (bare sky) | Water L* v16 / cube 0 (retail) |
| --- | --- | --- | --- | --- | --- |
| swim-idle | 14.3 | 15.5 | 14.0 | 9.2 | 41.2 / 35.7 (24.4) |
| swim-nav | 13.2 | 12.8 | 13.4 | 9.8 | 36.9 / 33.4 (21.7) |
| river-forest-noon | 10.3 | 8.9 | 11.3 | 13.1 | 39.6 / 35.8 (28.9) |
| ocean-clear-noon | 13.4 | 14.7 | 8.8 | 14.7 | 51.8 / 52.1 (36.7) |
| The mean (water dE) | 12.8 | 13.0 | 11.9 | 11.7 | |

The bare cube gives the deep blue of the retail swim water (saturation 0.78 against 0.79) but makes the river too blue (b* -8.0
against +7.3); no clouds helps the far ocean. Evidence: `~/Games/soulwarden-client/evidence/AST-200/part4/`.

### The water sky cube from the source parts (part 5, 2026-10-22)

The GPT-6.1-Sol study `~/Games/soulwarden-client/research/water-sky-cube-list.md`: the retail water sky cube has three parts in this
order: (1) the base SkyHDR sky with its Mie sun glow, no base sky fog on the normal frame; (2) the draw list 0x19: the three
Distanceclouds subsets of `objects/sky/skydome_a.cgf` with the override `objects/climax/materials/sky/ftue_sky.mtl` at the beach (not
the Unreal clouds); (3) the cube fog once on the composed cube. No stars at noon, no sun disk, no scene objects.

- The base sky: the sky sphere of the environment with the source sky model (`swc.SourceSky 1`), captured with the Unreal atmosphere
  off (bit 8 off: no sun disk).
- The source cloud layers (`python3 -B -m swc_convert.skycube write`, `tools/import-skycube.sh`; all new in `/SwcGameContent/Water/SkyCube`):
  one mesh for each dome subset (mesh rule 8: the vertex RGBA and the tangents), the parent `M_SwcDistanceClouds` (the normal
  DistanceCloudsPS: opacity = R(uv) x the vertex alpha; alpha = opacity ^ (AlphaSaturation / (Opacity + 0.01)); 8 red samples along the
  tangent-space light direction x StepSize; c = exp2(-Attenuation x density); RGB = (SkyColorMultiplier x CloudSky x (1 - c) +
  SunColorMultiplier x CloudSun x c) x the vertex RGB x 10,000 cd/m2; the TexMod oscillators: type 2 a scroll, type 4 a sine pan), and one
  instance for each layer. In the client the layers are components that only the scene captures draw, at 46 km about the view, in
  the provisional order of the CGF subsets (2, 1, 0 [U]). CloudSun = `USwcEnvironmentSubsystem::GetCloudSunSource` (lerp("Cloud
  shading: Sun light multiplier" x the main light, the custom color x its multiplier, its influence)); CloudSky 0 (the proved startup
  value; a live writer is not proved [U]).
- The cube fog: not ported. The source transport (one interval of the fog grid, old x T + Lfog) needs the slice count of the grid and
  the sun in-scatter inputs (a study is asked: `research/briefs/ast200-p5-cube-fog.md`). The capture fog of Unreal (bit 2) integrates to
  the sky sphere (46 to 50 km) and also fogs each layer: not the source rule.
- `swc.WaterSkyCube` bits: 1 the Unreal clouds, 2 the Unreal fog, 4 the source layers, 8 the Unreal atmosphere. The default stays 11
  (the capture of part 4) until the cube fog is ported.

| View | 11 (part 4) | 6 (base, layers, Unreal fog) | 2 (base, Unreal fog) | 4 (base, layers, no fog) | Water L* 11 / 4 (retail) |
| --- | --- | --- | --- | --- | --- |
| swim-idle | 19.1 | 14.6 | 15.7 | 11.0 | 44.0 / 28.9 (24.4) |
| swim-nav | 12.9 | 13.5 | 12.7 | 8.4 | 37.1 / 25.5 (21.7) |
| river-forest-noon | 10.2 | 9.8 | 10.1 | 3.8 | 41.0 / 31.7 (28.9) |
| ocean-clear-noon | 13.1 | 10.1 | 9.6 | 9.2 | 50.5 / 44.2 (36.7) |
| The mean (water dE) | 13.8 | 12.0 | 12.0 | 8.1 | |

The capture fog of Unreal makes the cube white; the layers alone change little in these clear noon views. Evidence:
`~/Games/soulwarden-client/evidence/AST-200/part5/` (retail | 11 | 6 | 2 | 4).

## The depth classes

The rule of the retail client (research wf5-swim, client code 0x6A0DBF0; `swc::water::depth_class`): the leaf of the water quadtree
at the integer cell of the feet; no water, or the feet below the floor of the leaf: None; depth = max(surface − feet z, 0); 0.2 m
or more VeryShallow, 0.9 m or more Shallow, 1.8 m or more Deep. No hysteresis. Where the quadtree has no water, the ocean counts
(40 m, floor 0): the class changes at the east beach of r_+04_+01, which has no quadtree water (set3-swim `swim-heights.json`,
place.water; `swc::water::with_ocean`).

The input entry has the class (mask bit 3, a u8) when it is above None, as the retail client (set3-swim `swim-wire.json`). In a swim
the class is 2 (Shallow): the feet are 1.400 m deep. The ground distance (mask bit 4) of a swimmer is the water below the feet.

## The wade (Shallow)

- The wade is the native Nav of the retail client (docs/MOVEMENT.md, "The native Nav"): 2.75 m/s for each key (the default move,
  the back key, Space), accel and decel 10 m/s², the walk key 1.65 m/s; no sprint in a wade (the retail wade W is in layer state 26,
  `set3-swim/nav-velocity.json`). The motion lists of the XML (the Wade conditionals of Nav, 1.65 m/s, and of Sprint, 2.5 m/s) are
  older than the compiled action.
- No jump, no dodge, no prone in a wade (IfWaterDepth LessThan Shallow of these actions; research `water-uses.json`). At
  VeryShallow prone goes to crouch.

## The swim

| Rule | Value | Source |
| --- | --- | --- |
| The start | the step at which the feet reach 1.400 m below the surface (on the ground or in a fall; a jump goes to the fall first) | set3-swim `swim-heights.json`, result.swim_start |
| The height | the feet stay 1.400 m below the surface, no bob; the head is at the surface | result.swimmer_height |
| The return after the entry | about 1.7 for each second (1.3 cm below, 0.6 cm 0.4 s later) [inference from 2 samples] | result.swim_entry_with_no_key |
| The motion | forward in the facing, 3.0 m/s, accel 3.0, decel 130 m/s², no gravity, no speed multiplier | playeractions_swim; measured 3.0 m/s (result.speeds_m_s) |
| The turn | the body turns to the direction of the input (InputRelativeFacing 0.10 for each 1/30 s) | playeractions_swim; measured 0 to 90 degrees in 1.3 s (result.body_turn_in_the_swim) |
| The exit | when the floor pushes the feet up to 1.29 m below the surface; the wade 0.5 s later | result.swim_exit (1.2893, 1.2901 m) |
| The fragments | Swim_Idle (`unarmed_swim_idle`), Swim_Nav (the blend space `swim_nav_blend`: MoveSpeed 0 to 3, TurnSpeed) | player.adb; research `swim-fragments.json` |
| The weapon | both hands sheathe at once with no clip; after the swim the weapon draws again with its animation (AutoUnsheathe) | playeractions_swim |
| The blocks | no attack, ability, weapon key, aim, dodge, jump, or interaction (only Reaction, Death, Respawn can start) | player.grid |

The swim does not start at the class Deep: the class stays Shallow in each retail swim sample (set3-swim, class_in_the_swim).

### The action limits in the swim (stage MVP-I, 2026-10-21)

The check: `tests/drive/mvpi-water.txt` with `tests/drive/mvpi-water-routes.txt` and the UI drive `tests/drive/mvpi-water-ui.txt`
(a copy of the database of the user, the bow and the rapier; the place of `wf5-swim.txt`). In the swim the drive tries each action;
then it swims out and tries them on the land. Evidence: `~/Games/soulwarden-client/evidence/mvp-i/mvpi-water1.*` (before the fix),
`mvpi-water2.*` to `mvpi-water5.*` (after).

| Action in the swim | Before (mvpi-water1) | After (mvpi-water2) |
| --- | --- | --- |
| The key of the weapon in the hand, the key of the other weapon | the other key switched the weapon (the server switched too) and drew it in the water | none ("none in the swim") |
| The sheathe key | (not tried) | none |
| A light attack, a heavy attack, the aim and a shot, the abilities Q, R, F | nothing to the server (the weapon is in its sheath) | the same |
| The quick slot key 3 (a potion) | the bit went to the server: the server used the item | none ("quick slot bit 404: none in the swim") |
| A dodge, a jump | none ("no dodge in the water"; no jump) | the same |
| E at a powder keg 1.5 m away | no request (the interactor is off in a swim) | the same |
| After the swim | | the weapon draws again (AutoUnsheathe), a shot is accepted, the potion is used (cooldown 31.2 s) |

The interaction after a swim: the interactor has no state of the swim (the block is the swim flag of each frame). The runs did not
show a use after a swim: the shore keg of the backend fixture did not show at this place (mvpi-water3), and the wolf that the shot
killed on the shore gave coin and no item, thus no bag (mvpi-water5). The uses on land in the fight runs work (the loot bag, Take All).

The rule: `ASwcPlayerController::UseWeapon`, `OnSheathe`, and `PressVirtualBits` (the quick slot bits 404, 117, 140, 513) do
nothing while `USwcCharacterMovement::IsSwimming` (the swim and its exit). player.grid starts only Reaction, Death, and Respawn in a
swim; the activation conditions of Use_QuickSlot_N refuse deep water. A swim that starts in an attack, a use, or an interaction:
the swim sheathes the weapon at once and the interactor ends its use (`Interactor->Update` with the swim as a block).

### The swim pose (AST-205, 2026-10-21)

The fault of AST-205 (the body upright with the feet 1.4 m deep) is in the matched views of `tools/match-view.sh` only: there the
character is put at the place with no swim state. In a run the client plays the swim fragments: Swim_Idle (`unarmed_swim_idle`,
upright, the head at the surface) and Swim_Nav (the blend space `swim_nav_blend`, the body flat at the surface), as retail set 3
(`swim-idle.png`, `swim-nav.png`). The picture: `~/Games/soulwarden-client/evidence/mvp-i/compare-swim-pose.png` (retail left,
client right; the run mvpi-water1). Differences: the client water is lighter and clearer, thus more of the legs shows; the body of the
client in Swim_Nav is about 10 cm lower [an estimate from the picture].

The moving swim (AST-205, 2026-10-22): the body of Swim_Nav is not flat in the source clip. The model-space pose of the source clips
(forward kinematics of the .caf on the male skeleton; the mean of 8 times of the cycle; the root at the feet, 1.400 m below the
surface) [R]:

| Clip | Head z | Pelvis z | Toes z | Body angle (toes to head) |
| --- | --- | --- | --- | --- |
| `unarmed_swim_idle` | 1.45 m | 0.92 m | -0.07 m | 85 degrees |
| `unarmed_swim_f` | 1.42 m | 1.10 m | 0.75 m | 28 degrees (20 to 42) |
| `unarmed_swim_l`, `unarmed_swim_r` | 1.39, 1.42 m | 1.07, 1.10 m | 0.65, 0.70 m | 33, 32 degrees |

At 3.0 m/s with no turn the grid of `swim_nav_blend` gives `unarmed_swim_f` alone (MoveSpeed 3 is the last cell). Thus the head is
at the surface and the toes are about 0.65 m deep, in retail and in the client. The Swim_Nav fragment has no procedural layer for
the pose (only the bubble effects). The side-by-side video of retail set 3 (`evidence/ast205/ast205-swim-side.mp4`, the take
swim-walk-in-out and the client run ast205-swim1 of `tests/drive/wf5-swim.txt`; frames: `ast205-swim-side-frames.png`) shows the
same pose: the upper body at the surface and the legs down at an angle. The retail legs show only as a faint pale shape in the dark
water; the clear, light client water shows all of the legs. The difference is the look of the water (AST-200), not the pose.
Other differences of the video: the gear (the retail seed character wears armor; the dev character has light clothes) and the
camera of the client is a little higher.

## Check (development runs, 2026-10-18)

- `tools/test-water.sh` (90 checks): the quadtree query of the client against the reader of the converter at 19,600 points of the 49
  regions (9,253 with water), the surfaces against water.json, the depth classes 1 mm on each side of each limit, the ocean rule.
- `tools/test-movement.sh`: the wade conditionals of the data. `tools/test-net.sh`: the depth class byte and the ground distance in
  the reader of the backend.
- `tests/drive/wf5-swim.txt` (`tools/creature-check.sh wf5-swim 36 ...`, the place and the keys of the retail video
  set3-swim/swim-walk-in-out): evidence `~/Games/soulwarden-client/evidence/WF5/wf5-swim.mp4` and the logs.
- The matched views of `set2-water` (`tools/match-view.sh set2-water`): `evidence/WF5/match-final/` (the eight views: two rivers,
  the clear coast, the storm coast, at 12:00 and 00:00). In that run the camera of the two clear-coast views turned to the
  north-east (a fault of the view setup); `evidence/WF5/match-ocean/` has the two noon ocean views with the same code and the right
  camera. The calibrations: `evidence/WF5/cal-a` to `cal-f`.
- The flow and the waves at 12:00 (`tests/drive/wf5-flow.txt`, `wf5-waves.txt` with `tests/drive/wf5-noon-routes.txt`):
  `evidence/WF5/wf5-flow.mp4` (the river of river-forest), `wf5-waves.mp4` (the storm coast of Cutlass Keys). The difference of two
  frames 3 s apart is in the water band (the mean change 3.7 of 255 in the middle third of the picture).

What the views show: the shapes and the shore lines of the rivers and the ocean are in the places of the retail pictures. The calm
rivers reflect the banks; the clear coast is turquoise over the sand; the storm coast is rougher. Differences: the river beds of the
terrain material are light sand (the retail beds are dark mud and grass: the highland river looks turquoise); the retail storm water
is green-yellow in its fog; the shore foam lines of the retail beaches (particles) are not in the client.

| Event | Retail (set3-swim) | Client |
| --- | --- | --- |
| None to VeryShallow | 0.215 / 0.249 m (the wire z) | 0.21 m |
| VeryShallow to Shallow (the wade) | 0.920 / 0.937 m | 0.91 m |
| The swim start, the feet | 1.400 m, z 38.600 | 1.400 m, z 38.600 |
| The swim speed | 3.0 m/s | 3.000 m/s |
| The swim exit | 1.289 to 1.290 m, the wade 0.5 s later | 1.289 m, 0.5 s |
| Shallow to VeryShallow, VeryShallow to None | 0.887 to 0.903 m, 0.182 to 0.198 m | 0.90 m, 0.19 m |
| The wade speed | 2.73 to 2.79 m/s | 2.75 m/s (the native Nav) |
| The run | 5.40 to 5.50 m/s | 5.50 m/s |

## The water color check (2026-10-19)

(History: the check of the Single Layer Water material before AST-200; see "The source water shader" for the rule now.)

The retail water of the rivers is dark and reflective; the client water was a light, clear turquoise. The cause: the light values
of the calibrations (FogLightScale 15, ExtinctionScale 0.4) were chosen by eye, and Unreal counts the extinction per centimeter:
an extinction of 0.2 per centimeter made the water an opaque, bright medium. The rule of the source shaders ("The light in the
water") replaces them.

The measure: `python3 -B tools/lib/water_part.py LABEL=FOLDER,...` on the folders of `tools/match-view.sh`: the color report of the
water pixels only (the view rays that meet the water surface of the quadtree before the terrain), and the L* difference of the
water minus the L* difference of the whole picture (the water against the light of the whole picture). The views: the four noon
views of `set2-water` and the two swim pictures of `set3-swim` (the set folder `evidence/water-color/set3-views`: the two pictures
with view names; its swim-nav.json is made from the nearest sample of the retail swim-nav.json). Evidence:
`~/Games/soulwarden-client/evidence/water-color/` (e5-before: the old values as `-SwcWater` arguments; e5-rule: the source rule).

The light of E5 steps 1 and 2a (main c82e988), the water dE (CIEDE2000) and the water dL − all dL:

| View | Old values | Source rule |
| --- | --- | --- |
| river-forest-noon | 39.6, +11.4 | 30.5, +4.5 |
| river-highlands-noon | 53.1, +19.7 | 45.0, +14.7 |
| ocean-clear-noon | 34.2, +8.2 | 36.5, +9.1 |
| ocean-storm-noon | 31.5, +4.1 | 39.2, +9.8 |
| swim-idle | 48.7, +6.5 | 43.9, +2.9 |
| swim-nav | 49.4, +6.5 | 44.0, +2.6 |
| The mean of the 6 views | 42.8, +9.4 | 39.8, +7.3 |

In the light before E5 (the four views of set 2): the mean water dE 27.2 (old values) and 24.4 (source rule); the rivers 33.6 to
24.6 and 47.8 to 39.3, the ocean 15.5 to 19.4 and 12.0 to 14.3.

What the numbers show: with the source rule, the rivers are clear. The bed shows through near the bank, and the reflection of the
sky makes most of the color, as in retail. The rivers and the deep ocean of the swim views come nearer to retail. The two coast
views go farther: with the old values the coast water was an opaque cyan medium, near the turquoise of the retail coast by chance;
now it is clear and shows the light sand and the reflection of the near white client sky. The rest of the difference is not in the
water values:

- the sky: the client sky is near white and hazy (E5 step 2b is open), the retail sky is blue or purple; the clear water reflects it
  (the swim views: the retail ocean is a saturated blue, S 0.79; the client S 0.19);
- the light of the whole picture: in the light of E5 steps 1 and 2a the whole picture is +27 to +31 L* (set 2); the water follows
  it (+2.6 and +2.9 over the picture in the swim views);
- the river beds: the client beds under the clear water look light; the retail beds are dark. The layers under the water are
  the layers of the source (r_+04_+02: `lakes_splat`, the river stones, weight about 0.9 under the water of the two river views
  and 0.01 on the dry ground, drawn late: order 29 of 33; `lakeedge_splat`, the edge mud, at the edges); no cell under the water
  falls to the default material, and the macro color under the water is the same as on the dry ground. The river stones are dark
  (albedo 0.067 linear, gloss 0.14, F0 0.058; the forest soil 0.059). Thus the light beds come from the light of the whole
  picture; measure them again after E5 step 2b;
- the ocean Fresnel: the source stops at about 0.59 at a grazing view (FresnelGloss), Unreal goes to 1. At the swim view (about 80
  degrees) the source gives about 0.27, Unreal 0.43. Single Layer Water has no input for it.

## Not done, open points

- The upper body of the wade (`Water_Deep_Upperbody`, `unarmed_walk_water_ub`), Swim_Death (two clips in sequence), the drown of a
  crouch in shallow water (the statuses Breath and Drowning), and the blocks of fishing, gathering, the mounts, and the inventory in
  deep water: not in this stage.
- The exit action (0.5 s) plays Swim_Nav and has no motion of its own: its retail motion is not known.
- The edge fades of the rivers (WidthFadeDist, the length fades), the ocean crest foam (FoamCrestAmount), the vertex waves, and the
  caustics: not used (AST-200 open points).
- 18 volumes are tilted (up to 6.4 degrees); the quadtree cannot tell if their surface tilts. The full matrix is used [inference].
- The night views: the light of the client at night is the work of the environment track (E1); the water uses the light of the scene.
