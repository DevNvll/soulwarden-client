# The particle effects

Linear AST-210: the particle effects of the game (fire, candles, torches, smoke, waterfalls, leaves, the effects of the abilities).
The details audit of the user (2026-10-20, "still missing a bunch of details of the original game") ranks them second. The rule is
the study `~/Games/soulwarden-client/research/particles.md` (640 libraries, 8,330 top effects, 38,692 child effects, 207,015 world
components): an offline converter, generated Niagara systems from a small set of shared templates, and one placement runtime.

The order of the stages (research "The order of work"): 1 the reader and the support report; 2 the fire (the menu campfire, the
torches, the candles); 3 smoke and dust; 4 waterfalls; 5 the shore splash children; 6 leaves and insects; 7 the ability and weapon
effects. Each stage keeps the attributes that it does not express in its support report.

## The parts

| Part | Job |
| --- | --- |
| `converter/swc_convert/particles.py`, `tools/convert-particles.sh` | The reader of the libraries and of the placements, the name checks, the support report (stage 1). |
| `converter/swc_convert/particle_systems.py` | The systems for the importer: the resolved values of each emitter in Unreal units, the textures, the materials, and the attributes of each emitter that the client does not express. `check`: the values of the three worked examples of the study. |
| `Scripts/particle_systems.py`, `tools/import-particles.sh` | The atlases, the shared materials `M_SwcParticleAlpha`, `M_SwcParticleAdditive`, `M_SwcParticleLit`, the collection `MPC_SwcParticles`, one instance for each look, and one Niagara system for each effect root in `/SwcGameContent/Particles`. |
| `SoulwardenClientEditor/SwcParticleBuilder.cpp` | `SwcEditorLibrary.BuildParticleSystem`: one stateless (lightweight) Niagara emitter for each source emitter. |
| `Effects/SwcParticleEffects` | The effect interface (Start, Stop, Restart, SetParameters, StartList) and the world placements near the view, with the check "effects" of the loading gate. |
| `Menu/SwcMenuPreview` | The menu scene starts the list `particles/menu.json` within the scene radius (60 m) and stops it at its end. |

```sh
tools/convert-particles.sh library           # the libraries -> SWC_CONTENT/particles/libraries, index.json
tools/convert-particles.sh menu              # the components of the menu level -> menu.json
tools/convert-particles.sh placements        # the world placements of the 49 regions -> placements/REGION.json (about 30 min)
tools/convert-particles.sh names             # the name checks -> names.json
tools/convert-particles.sh report            # the support of each attribute -> support.json
tools/convert-particles.sh systems [KEY...]  # the systems of the effect roots (default: the stage 2 list) -> systems.json, textures
tools/convert-particles.sh check             # the worked examples of the study
tools/import-particles.sh [--force]          # the import (the lock; a system is made again when its hash changes)
tools/test-converter.sh                      # tests/converter/test_particles.py (no game data)
```

## Stage 1: the reader (2026-10-20)

The outputs are in `SWC_CONTENT/particles` (never in git).

| Output | Content |
| --- | --- |
| `libraries/LIB.json` | Each library: the pak path, the SHA-256, the root attributes, its nodes in file order (the key, the name, the tag, the parent index, the raw `Params`, the LOD blocks with their distance, `Active`, and raw `Params`, the raw `DynamicParams`). |
| `index.json` | The libraries, the counts, the 200 attribute names with their counts, the key table (lower-case key -> library, node), the collisions. |
| `placements/REGION.json` | The particle components of the region (the walk of `environment.py`): the effect, the world matrix (rows 3 x 4, engine m), the settings that differ from the defaults, the entity, the end of the slice chain. |
| `menu.json` | The 30 components of the menu level with the world matrix, the Unreal transform, and the distance to the Player Entity. |
| `names.json` | The name checks of all placements. |
| `support.json` | Each attribute: its count, its status, and a note. |

The results (the counts of the study are the reference):

| Check | Result |
| --- | --- |
| Libraries | 641: the 640 of `libs/particles/` and the external `cfx_arcanesurge.xml`; all ParticleVersion 33. |
| Nodes | 47,030 Particles (the 47,022 of the study and 8 of the external library), 959 folders, 37,025 LOD blocks, 200 attribute names. |
| Keys | 47,944 lower-case keys; 26 collisions (two nodes with one lower-case key) are listed, never merged. |
| World placements | 207,015 components in the 49 regions, 149,530 slices read, 0 failures (the same as the study). |
| Menu | 30 components, 16 within 60 m of the Player Entity. |
| Names | 630 effect names (148,365 components) resolve exactly. 50 names (58,680 components) have no node: they are reported, not repaired (research "Name failures"). No name needs a different case. |
| Worked examples | `check`: 31 values of the campfire, the torch, and the shore splash agree with the study (the children, the defaults, the random ranges, the atlas grids, the LOD bands, the material). |
| Tests | `tests/converter/test_particles.py`: the parameter text, the splines (byte keys, tangent types, equal times), the one-sided random range, the default bases, the LOD bands. |

The parameter rules (research "Parameter text, curves, and random values"; [R] retail code, [I] inference):

- A value is a base, then named fields; a field in parentheses is a vector (x, y, z, w) or a spline (keys `t, v, d, s, flags` with ";").
  A text with no base has the default base of its attribute (a size base is 1).
- A scalar random: the range [base x (1 - Random), base] [R]. A spline: t and v are bytes / 255 [R]; d is the out tangent, s the in
  tangent; the flags: bits 0-2 the in type, bits 3-5 the out type (0 smooth, 1 custom, 2 zero, 3 step, 4 linear) [P: the CryEngine
  spline flags]. The smooth type is the Catmull-Rom slope of the two neighbours [I].
- A node starts from the standard defaults [R], then its own attributes. A child does not inherit the visual values of its parent.
- The LOD bands: the base node shows below the first LOD distance, each LOD block from its distance. An emitter that turns on again
  farther away is reported (the first pass keeps the outer bound).

The support report has 200 attributes: 28 used, 26 approximations, 9 editor fields with no visual effect, 24 for later stages (3, 6,
7), and 113 not supported (the collision, the beams and the ribbons, the target attraction, the confinement, the weather occlusion,
the indoor and underwater gates, and others; the full list is in `support.json`). Each system of `systems.json` lists, for each
emitter, the attributes with a value that it does not express, with their raw text.

## Stage 2: the fire (2026-10-20)

The effects: `Buildables.Campfire.Idle_Medium` (the menu campfire), `Buildables.Torch.Idle` (16,080 in the world),
`Buildables.Global.Candle_Flame_01` (23,729), and `Bld_Global_Ambience.Candle01` (the menu and inn candles).

### The systems

One Niagara system for each effect root, one stateless (lightweight) emitter for each Particles node of its tree. A stateless emitter
is data only (the modules of the default template with distributions; no script graph, no compile of a graph): the converter values
go into the modules directly. A root with Alpha 0 (the campfire root) is a controller: it has no emitter, its children stay.

| Source | Niagara (stateless module, renderer) |
| --- | --- |
| Count, ParticleLifeTime, Continuous | Spawn rate Count / life [R: the nominal rate], or a burst of Count; the life range |
| SpawnDelay, EmitterLifeTime, PulsePeriod | The loop delay, a single loop, the loop duration |
| SizeX, SizeY (half sizes), MaintainAspectRatio | InitializeParticle sprite size 2 x size x 100 cm, SizeX the width (U), SizeY the height (V) (the source quad is -1..1 [R]); MaintainAspectRatio: the height is the width x the aspect of the atlas cell [I]; ScaleSpriteSize: the age curves (32 samples) |
| OrientToVelocity | The velocity-aligned sprite: SizeY and V along the velocity [R, particle-rules.md 3]; no sprite rotation [I] |
| Facing CameraX | The custom alignment along the local Z (the sprite turns about the upright axis to the camera) [R] |
| Color, Alpha | The particle color: base x random; ScaleColor: the age color and alpha curves; the alpha is one fade (no erosion) [R] |
| EmissiveLighting, DiffuseLighting, Material Emittance with GLOW_MAP | The two color terms of the material (below) [R, particle-rules.md 1] |
| Speed, EmitAngle, FocusAngle, FocusGravityDir | AddVelocity in a cone about local +Z [R: "default (Z axis)"]; EmitAngle Random sets the inner angle [R]; FocusAngle widens the cone [I] |
| GravityScale, Acceleration, AirResistance | GravityForce -9.8 x scale m/s2 [R] plus the acceleration; Drag (the stateless solver is exponential, as the source GPU path) |
| Turbulence3DSpeed | CurlNoiseForce strength [I: not the source rule] |
| LocalSpawnPosOffset, LocalSpawnPosRandomOffset, LocalOffsetRoundness | ShapeLocation: the offset, a box (roundness 0) or an ellipsoid |
| TextureTiling | SubUVAnimation: the frames from FirstTile, one cycle in the life (fps 0), a loop at fps, or a random variant; the renderer grid and the frame blend |
| PivotX, PivotY | The renderer pivot 0.5 + 0.5 x pivot |
| LOD bands, CameraMinDistance, CameraMaxDistance | The distance culling of each emitter (it sleeps beyond its band; its particles finish) |
| SoftParticle | The static switch Soft: 1 - exp(-d^2 K), d the scene depth gap in m (the source rule of ApplySoftIntersection [R]; K = 1 [I]) |
| BlendType, DiffuseLighting | The material: Additive -> `M_SwcParticleAdditive`; alpha with DiffuseLighting > 0 -> `M_SwcParticleLit` (translucent, lit by the light volume at each vertex, no direction); else `M_SwcParticleAlpha` |

The materials follow the two color terms of the source particle shader (`~/Games/soulwarden-client/research/particle-rules.md`, from the
brief of 2026-10-20) [R]:

```text
opacity  = saturate(atlas.a x particle alpha x AlphaScale x soft fade)
ambient  = atlas.rgb x particle color x Tint x Ambient                 (Ambient = EmissiveLighting)
glow     = opacity x atlas.rgb x glow map.rgb x GlowColor              (GlowColor = 0.1 x Emittance.rgb x Emittance.a of the .mtl
                                                                        with GLOW_MAP; the glow map is the Detail slot at the same
                                                                        atlas frame; no particle color, no tint)
emissive = (ambient + glow) x EmissionUnit x EmissiveScale             (additive: x opacity, the non-alpha path)
base     = atlas.rgb x particle color x Tint x Diffuse                 (M_SwcParticleLit only: the scene light term, DiffuseLighting)
```

EmissionUnit (`MPC_SwcParticles`, `swc.Effects.Emission`) is 1: the terms are in source shader units, and EmissiveScale of
`MPC_SwcMaterials` is 10,000 cd/m2 x swc.LightScale (1 since E5). For the torch: Ambient 100, GlowColor (5, 0.857, 0). Not done: the
time-of-day factor F = 1 + (B - 1) x TodBrightnessInfluence (B, the global particle brightness, is not traced: F = 1), the light of the
source light volume and probe (the lit material uses the translucency light volume of Unreal), the SDF UV warp and mask.

The textures: the diffuse map of the `.mtl` of the Material attribute, else the Texture attribute (a `.tif` name is the `.dds`
product). A file that is sRGB keeps its bytes; a linear file is encoded to sRGB for the import (all particle atlases sample as sRGB).

### The runtime

`USwcEffectSubsystem` (`Effects/SwcParticleEffects`):

- The effect interface: `Start(effect, transform, settings, parent)` (a pooled component), `Stop(component, kill)`, `Restart`,
  `SetParameters` (the tint and the alpha scale through the user parameters `User.SwcTint` and `User.SwcAlphaScale`, the scale of the
  placement x GlobalSizeScale as the component scale, Time Scale as the time dilation), and `StartList(file, radius)` (the menu).
- The world placements: the region files near the view are read off the game thread; a placement gets a component when the view is
  within the view distance of its effect (the outer LOD bound; 50 m when the effect has no LOD end [I]) and goes back to the pool
  beyond 1.1 x that distance. A placement with Visible or Enable false waits for its owner (stage 7). The same effect at the same
  place twice (a slice placed two times) is one placement.
- The view distance of an emitter (particle-rules2.md b [R]): Dmax = screen height / vertical FOV x max(e_ParticlesMinDrawPixels, 0.125)
  x M x S x V, M the largest SizeX or SizeY base x ViewDistanceAdjust over the emitters (systems.json "maxSize"; the torch 0.4 m),
  S = GlobalSizeScale x the placement scale, V the View Distance Multiplier; P = `swc.Effects.MinDrawPixels`, 1.0 (the registered and
  shipped default; 0 turns the rule off). The LOD bounds are not multiplied by V. Not done: the per-particle pixel fade.
- GlobalSizeScale x the placement scale is the component scale. The stateless simulation space has no scale: ApplyOwnerScaleToAttributes
  applies S to the initial speed and the sprite size, and the forces keep their world values (particle-rules2.md d) [R]. Open: the spawn
  offsets under S.
- The loading gate (AST-203): the check "effects" waits until each placement within `SwcLoadingGate::NearCm` (150 m) of the place
  and within its range has its component and has run 1 s. Thus no flame grows after a loading screen.
- `-SwcNoEffects` or `SWC_EFFECTS=0` turns the world placements off; `swc.Effects.Show 0` hides the effects; `swc.Effects.Test
  KEY,COUNT[,DISTANCE_M]` places copies in front of the camera (no argument: removes them); `swc.Effects.Dump` logs the emitters and
  the particle counts of each running effect. The drive step `exec LINE` runs a console line in a check.

### The checks

The retail references (a development run of the retail client with the tools of the Soulwarden project, 2026-10-20; cropped to the
game area): `evidence/P2/retail-menu1.mp4` (the menu campfire), `retail-torch1.mp4` and `retail-torch2.mp4` (the torch posts of the
Oxboro town hall at 23:00), `retail-candle1.mp4` (the candles of the inn at 23:00).

The client takes: `p2-menu2.mp4` (the menu), `p2-torch1.mp4` (the same torch view at 23:00, then the inn candles). The side-by-side
videos (the full frames at half size on top, a close crop of each below): `sbs-campfire.mp4` (2.5x), `sbs-torch-post.mp4` and
`sbs-torch-wall.mp4` (3x), `sbs-candles.mp4` (2.2x). All in `~/Games/soulwarden-client/evidence/P2/`.

The visible differences (2026-10-20):

| Effect | Difference with retail | Cause, next step |
| --- | --- | --- |
| Campfire flames | The client flames are taller and brighter yellow; retail has short orange flames between the logs. | The size rule (the brief, question 3) and the glow map of the fire materials. |
| Campfire smoke | Retail has a thick dark-red plume up to the tent top; the client shows no smoke. | The smoke is lit (DiffuseLighting 0.2, emission 0.1): with the light volume it is near black and thin. Stage 3 (the lit smoke, the probe and the back light). |
| Campfire embers | Retail shows sparks in the smoke; the client sparks are not seen. | The embers are 2 cm sprites (the size rule); the GPU motion blur of the source (stage 7). |
| Campfire logs | The client logs have white streaks; retail logs glow orange. | Not a particle: the emission of the log mesh (the materials track, AST-207). |
| Torch flame | Smaller, thinner (less opaque), and more red at the edge in the client; retail is a solid orange-yellow flame with a white-yellow core, a little above the torch head. | The glow map of `fire_torch_idle.mtl` (the [2] Custom map) and the alpha clip upload are not used (the brief, questions 1 and 2). |
| Torch flame motion | The client flame flickers in place; retail flames lick upward. | The velocity-aligned sprite has no Stretch and no age curve of the drag yet. |
| Candles | The client candle flames are a few pixels (not visible in the crops); retail shows clear flames of about 6 x 18 px at 3.5 m. | The sprites draw (swc.Effects.Dump: 2 flame and 2 glow particles each; a test at 2 m shows them), but small and faint: the size rule and the owner scale (the brief, questions 3 and 5). |
| View distance | Not compared in a video. | View Distance Multiplier is not applied (the brief, question 4). |

The loading gate: the wait of a world entry logs the check "effects" (for example "0 effect(s) to start, 49 warming"); the screen
ends after the effects near the place ran 1 s.

The frame cost (`p2-cost1`, the Oxboro inn at 23:00, -SwcFrameStats=5, the toggles late in one run; the GPU is shared with the runs of
two other tracks, so a 5 s window can show a jump to 40 to 50 ms that is not the effects):

| State | Game thread p50 (ms) | GPU p50 (ms) |
| --- | ---: | ---: |
| The 58 world effects near the inn hidden (swc.Effects.Show 0) | 21.4 to 22.5 | 11.9 to 12.0 |
| The 58 world effects shown (the torches and the candles within their range) | 17.7 to 19.2 | 11.7 to 12.4 |
| Plus 400 test candles in view at 3 m | 33.7 to 43.5 | 11.2 to 12.1 |
| Plus 100 test torches in view at 3 m | 26.2 to 32.7 (the later windows are the other runs) | 12.7 to 13.2 |

The GPU cost of the effects is in the noise. The game thread pays about 0.04 ms for each running system (the 400 candles): many
small effects need a shared simulation later (research "The cost of each method": Niagara Data Channels or one system for each
cell and material family). With the source LOD bands, the inn has 58 running systems.

### Not done, open points (stage 2)

- The rules of the brief to a static study (the orchestrator sends it): the emission and the glow map of a particle, the alpha clip
  upload, the sprite axes and the size, View Distance Multiplier, GlobalSizeScale.
- CameraX (upright) sprites face the camera: the custom alignment of the stateless module drew nothing in the first check.
- ApplyOwnerScaleToAttributes does not scale the sprites (a test with the component scale 5 and 10 showed no change): GlobalSizeScale
  has no effect on the sprite size yet.
- The bonfires of the stage list are not in the system list yet (no world placement near the checks).
- The menu: 5 of the 16 near components start (the campfire and the candles); the teleport auras, the fireflies, the gnats, the leaves,
  and the fog plumes wait for their stages.
- The cooked build: the systems load by path from `/SwcGameContent/Particles` (DirectoriesToAlwaysCook, the world track).

### The fire fix with the brief (2026-10-20, second round)

The rules of `research/particle-rules.md` in the converter, the materials, and the builder: the half sizes with SizeY along the
velocity (no exchange of the axes), the two color terms (the ambient term with EmissiveLighting, the glow term of the Detail map with
0.1 x Emittance), the custom alignment of CameraX, the owner scale of the sprite size, and the view distance rule (off by default).

The takes: `p2c-torch.mp4` (the torch posts at 23:00, then the inn candles), `p2e-menu-on.mp4` and `p2e-menu-off.mp4` (the menu with
and without the effects: `-ExecCmds="swc.Effects.Show 0"`). The side-by-side videos: `sbs-torch-post-b.mp4`, `sbs-torch-wall-b.mp4` (3x;
the client camera of this take is a few degrees off, so the client crop has its own place), `sbs-candles-b.mp4`, `sbs-campfire-b.mp4`
and `sbs-campfire-c.mp4`; the still crops `torches-x3.png`, `candles-x2.png`, and `campfire-x2.png` (retail, client off, client on).

| Effect | Now | Difference with retail |
| --- | --- | --- |
| Torch | An orange-yellow flame with a bright core (the ambient term 100 and the glow (5, 0.86, 0)). | The client flame is about half the size and sits in the torch head; retail has a larger flame above the head. |
| Candles | Small upright flames above the candles (they did not show before: CameraX now keeps the sprite upright). | The client flames are about 1.5 times smaller; retail has a whiter core. |
| Campfire flames | Short, broad orange flames over the logs (SizeX across, SizeY along the velocity). | Near retail in shape; the client logs still have the white emission streaks (the materials track). |
| Campfire smoke and sparks | A faint red haze over the flames. | Retail has a thick dark-red plume up to the tent top and sparks in it: the lit smoke needs the source light terms (stage 3), the sparks the motion blur (stage 7). |

The open rules for the next round: the time-of-day brightness B, the inputs P and M of the view distance, the size difference of the
torch and the candle flames (a factor of about 1.5 to 2 with the half-size rule), and the gravity under the component scale.

After the rebase on E5 (swc.LightScale 1), take `p2f-torch.mp4` (`torch-candles-lightscale1.png`: retail left, client right): the candle
flames show, orange, a little smaller than retail; the torch flame is paler (pink-white) and about half the size of the retail flame.

### The second round (2026-10-20, particle-rules2.md)

The rules: B, the evaluated "Particle Brightness Multiplier" of the scene preset (`USwcEnvironmentSubsystem::ParticleBrightness`), goes to
TodBrightness of `MPC_SwcParticles`; each material instance has TodInfluence (the torch 0.5, the candles 1); the materials multiply both
color terms by F = 1 + (B - 1) x TodInfluence (r_ParticlesTOD 2). The aspect of the atlas cell reduces one axis (aspectX = min(cellW/cellH,
1), aspectY = min(cellH/cellW, 1)). The stretch of a velocity-aligned sprite is in its height curve (2 x speed x min(Stretch(age), age),
the mean speed of the source force rule, S = 1) [I: the stateless emitter has no geometric stretch]. The view distance rule is on with P
1.0. The forces are not scaled by S.

The run values: B 1.5 in the menu (16.7 h; the brief 1.5), B 0.5 in Oxboro at 00:54 (the brief: 0.4 at 23:00).

The takes: `p2g-torch.mp4`, `p2g-menu.mp4`; the side-by-side videos `sbs-torch-post-g.mp4`, `sbs-torch-wall-g.mp4` (3x),
`sbs-candles-g.mp4`; the crops `fire-round2-x3.png` (the two torches 3x, the candles) and `campfire-round2-x2.png`.

| Effect | Now | Difference with retail |
| --- | --- | --- |
| Torch | A longer flame (the stretch), F 0.75 at night. | The bright flame is pale (near white) where retail is orange-yellow; it still sits in the torch head. |
| Candles | Small orange upright flames. | About 1.5 times smaller (open in the brief too). |
| Menu campfire | With B 1.5 a bright white-yellow blob over the logs. | Retail has orange flames and orange logs, and a dark-red smoke plume. |

The campfire logs (the request of the orchestrator): the menu fire meshes are `jav_prp_fireplacepile_round` (instance `n_532218a1e5`,
`jav_prp_fireplacepile_mat#1`: Emittance (1, 1, 1) x 200, the sRGB coal map `jav_prp_coalpile_diff` with its alpha as the mask, the
linear intensity map `jav_prp_forge_noise_mask` at 2 x 2) and `jav_prp_rock_pit_coals` (instance `n_4056f40760`, the material
`jav_prp_rockcoals_mat` "Rock": Emittance (0.98, 0.46, 0.18) x 600 with an Emittance map but no `%EMITTANCE_MAP` flag, thus the
constant 0.5: 30 source units over the whole pit). The checks (an editor script and the files): the coal map is sRGB and decoded
(EmittanceSrgb, sRGB, TC_Default with alpha, alpha mean 0.049), the intensity file is linear and sampled raw, the instance values
are those of the rule (research/emission-fogcards.md). Thus the map, the alpha, and the color space are right, and no shared rule was
changed. The measurement of the menu: the top 5 % of the fire region is orange (224, 165, 78) in retail and a neutral white
(235, 235, 234) in the client; the whole client scene is about 1.4 times brighter (mean 129 against 91). The torch flame shows the same
neutral white. [I] The common cause is the HDR to display step of the client (the filmic tone curve of Unreal turns very bright colors
white; the retail picture keeps their hue) and the exposure of the menu, not the material data: a question for the color pipeline (E5).

