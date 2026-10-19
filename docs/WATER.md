# The water

Stage WF5 (Linear AST-187): the water of the world (the ocean, the water volumes, the rivers) and the wade and the swim of the
player. The content follows the source: the ocean level, the water shapes, the river curves and widths, the flow, the water kinds,
and the depth rules. The rendering is the Unreal way (decision of the user, 2026-10-18): Single Layer Water with the reflections,
the refraction, and the light in the water of Unreal. The source colors, the source density, and the source foam are its inputs.
The retail water pictures are the reference for the places, the shapes, and the colors, not for the shader look.

The research: `~/Games/soulwarden-client/research/wf5/` (the shapes, the curves, the materials, the quadtree) and
`~/Games/soulwarden-client/research/wf5-swim/` (the depth classes, the wade, the swim). The retail measurements:
`~/Games/soulwarden/work/evidence/unreal-references/set2-water/` (the matched views) and `set3-swim/` (the swim).

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/water.py`, `tools/convert-water.sh` | The surfaces of each region, the ocean and its overrides, the materials, the textures, and the quadtree of each region into `SWC_CONTENT/water`. `check`: the surfaces against the quadtree. `samples`: the points of the reader test. |
| `Scripts/water_materials.py`, `tools/import-water.sh` | The three water textures and the base material `/SwcGameContent/Water/M_SwcWater` (Single Layer Water). |
| `Water/SwcWaterData` | Plain C++ (tested by `tools/test-water.sh`): the readers of the files, the quadtree query, the depth class rule, the ocean rule. |
| `Water/SwcWaterSubsystem` | The surfaces near the view (static meshes made at run time), the ocean that follows the view with its local overrides, the queries of the movement. |
| `Character/SwcCharacterMovement` | The depth class of the feet, the wade, and the swim. |
| `Character/SwcPlayerActions` | The blocks of the swim and the wade, the sheathe of the swim. |
| `Net/Codec` | The depth class byte of the input entry (mask bit 3). |

```sh
tools/convert-water.sh write      # needs the environment records (tools/convert-environment.sh write)
tools/convert-water.sh check      # the surfaces against the quadtree
tools/convert-water.sh samples    # the points of tools/test-water.sh
tools/import-water.sh             # the textures and M_SwcWater
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
  `M_SwcWater`. The surfaces near the view build first (at most 6 in a frame, in 3 km); they stay. No collision, no shadow.
- The ocean is a flat grid of 20 km at the ocean level that follows the view in steps of 312.5 m. The override of the place of the
  view (its cylinder or box) changes its values in its transition time. The strength of its wave normals is the wave size of the
  source (WavesSize: 0.08 the mission, 0.15 the beaches, 0.3 the storm), the speed WavesSpeed × (1 + WindSpeed / 50), the
  direction WindDirection [inference: mappings of the client].
- The ocean plane of the world level (the world track's `M_SwcOcean`) hides with no collision while it is in the level: a character
  walked on it at 40.000 m.
- `M_SwcWater` (Single Layer Water):
  - the waves: the wave normal map of the source shaders (`engineassets/textures/oceanwaves_ddn.dds`, an inference of the research:
    the .mtl files have no normal map slot) at two scales, moved by the wind (the ocean, the volumes: world UV) or along the river
    (the UV of the river mesh, WaterFlowSpeed of the .mtl); Tilling, DetailTilling, NormalsScale, DetailNormalsScale of the .mtl;
  - the light in the water: the hue of the source fog color, its brightness (the largest component × the color scale ×
    FogLightScale), and the extinction (the fog density × ExtinctionScale): scattering = extinction × albedo, absorption =
    extinction × (1 − albedo);
  - the foam of the source (FoamAmount, FoamTilling, FoamSoftIntersectionFactor, the foam map `engineassets/shading/waterfoam.dds`)
    where the water behind the surface is shallow (SceneDepthWithoutWater); the opacity of Single Layer Water is the foam.
- Calibration (the matched views of `set2-water`): `-SwcWater=Name=Value,...` sets parameters of each instance with no import.
  The values of the parent: NormalStrength 0.04, FogLightScale 15, ExtinctionScale 0.4, Roughness 0.02 (the calibrations cal-a to
  cal-f in `~/Games/soulwarden-client/evidence/WF5/`: a smaller normal strength gives the calm mirror rivers of the retail views; a
  larger light scale gives milky rivers).

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

## Not done, open points

- The upper body of the wade (`Water_Deep_Upperbody`, `unarmed_walk_water_ub`), Swim_Death (two clips in sequence), the drown of a
  crouch in shallow water (the statuses Breath and Drowning), and the blocks of fishing, gathering, the mounts, and the inventory in
  deep water: not in this stage.
- The exit action (0.5 s) plays Swim_Nav and has no motion of its own: its retail motion is not known.
- The edge fades of the rivers (WidthFadeDist, the length fades), the ocean crest foam (FoamCrestAmount), the vertex waves, the
  caustics, the shore foam particles (`water_on_shore`), and the source reflection cubemaps: not used.
- 18 volumes are tilted (up to 6.4 degrees); the quadtree cannot tell if their surface tilts. The full matrix is used [inference].
- The night views: the light of the client at night is the work of the environment track (E1); the water uses the light of the scene.
