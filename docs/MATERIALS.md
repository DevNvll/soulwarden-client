# The materials of the world

This document has the rules of the materials track (milestone "WF World fidelity", Linear AST-185): the materials of
the world meshes and the vegetation. The research is `~/Games/soulwarden-client/research/world-materials.md`
(outside the repository). Its stages M0 to M7 (section 6) are the stages of this track.

The world track keeps the cell builder, the level, the terrain, the collision, and the import pipeline
(`Scripts/import_content.py`, `tools/import.sh`). It also carries the material overrides of the placements
(`materialOverrideAssetId`) into the cells. This track keeps the material rules: `converter/swc_convert/materials.py`,
`converter/swc_convert/material_records.py`, the parent materials, and the parameters of the material instances.

## Two keys for each source material

A source material is one sub-material of a `.mtl` file. The converter gives it two keys.

| Key | Form | Use |
| --- | --- | --- |
| Legacy key | `m_` and 10 hex digits | The rules of stage 1.2 (`materials.shared_material`): the blend, two sided, the cutoff, the base color and normal textures. It is the name of the glTF material, of the mesh slot, and of the material instance `/SwcGameContent/Materials/KEY`. It does not change: the 17,305 world meshes keep their slots and their assets. |
| Render key | `r_` and 10 hex digits | The hash of the render rules: the values that the parents of this track use. Two source materials with the same render key look the same. |

The legacy key hides differences: different source materials can have one legacy key. Then one material instance
serves all of them. The audit counts these cases (below).

## The rich record (stage M0)

`material_records.source_record` keeps all values of a sub-material:

- The provenance: the `.mtl` path, the index of the sub-material, its name, its surface type. Names are not in the
  render rules.
- The shader and the generation flags. The engine loader uses `StringGenMask` when the material has it, else the
  bits of the numeric `GenMask` through the `.ext` file of the shader (`shaders/NAME.ext` in the paks). The generated
  materials of the merged chunks (`coatgen/`) have a `GenMask` that does not agree with their `StringGenMask`: the
  text is the rule. The old names `%BUMP_MAP` and `%SPEC_MAP` become `%NORMAL_MAP` and `%SPECULAR_MAP`.
- The lighting values (`Diffuse`, `Specular`, `Emittance`, `Opacity`, `AlphaTest`, `Shininess`) and all public
  parameters, as numbers with 7 significant digits. The XML colors are linear values: no sRGB decode.
- Each texture slot: the file, the asset id, the product (the `.dds` in the paks), the UV rules (tiling, offset,
  rotation, oscillators, clamp: only the values that are not the default), the alpha image (`.dds.a`), the sampler
  role, and the color space.

### Sampler roles

The shader and its flags give the role of a slot. A slot that the shader does not sample with these flags is
inactive: it stays in the rich record, not in the render rules. Sources: the `.ext` files and the decoded shader
scripts of the research (section 3).

| Slot | Role (Illum, Vegetation) | Active when | Color space |
| --- | --- | --- | --- |
| Diffuse | diffuse | always | sRGB RGB, linear alpha |
| Bumpmap | normal | `%NORMAL_MAP` | normal (BC5) |
| Smoothness | gloss (the alpha image of the normal map) | always | linear |
| Specular | specular | `%SPECULAR_MAP` | sRGB |
| Detail | detail | `%DETAIL_MAPPING` | the file: 383 of 385 are linear files (see below) |
| Custom | diffuse2 with `%BLENDLAYER`; overlay mask with `%OVERLAY_MASK` or `%COLOR_SAMPLER_OVERLAY_MASK` (`$CustomMap`) | one of these flags | sRGB / linear |
| [1] Custom, SecondSmoothness, Specular2, SubSurface | normal2, gloss2, specular2, height2 | `%BLENDLAYER` | normal / linear |
| Opacity | Vegetation: transmission. Illum: blend mask (`%BLENDLAYER`), decal opacity (`%DECAL`), transmission (`%TRANSMITTANCE`) | see the role | linear |
| Heightmap | height | a POM, offset bump, or displacement flag | linear |
| Emittance | emittance | `%EMITTANCE_MAP` | raw (no sRGB lookup) |
| Decal | emittance intensity | `%EMITTANCE_MAP` or `%EMISSIVE_DECAL` | raw |
| Occlusion | occlusion | `%OCCLUSION_MAP` | linear |
| Environment | none (Unreal reflections) | never | - |

The Glass shader has its own table: Specular is the tint mask (`%TINT_MAP` or `%SPECULAR_MAP`), Custom the tint
color (`%TINT_COLOR_MAP`), Diffuse the dirt (`%DIRT_MAP`), SecondSmoothness the cloud gloss.

The sRGB decode: a source sampler with `sRGBLookup = true` (diffuse, specular, detail, the diffuse of the blend
layer, the glass tint maps) decodes sRGB only when the file is sRGB: an `_SRGB` DXGI format, or the flag 0x800 in the
second reserved word of a legacy header (`material_records.file_srgb`; the two agree in all 4,478 files of these roles).
A sampler without it (the blend mask, the gloss, the overlay mask, the emission, the second normal, gloss, and
specular) reads the values as they are. The detail maps are linear files (383 of 385): they import as linear data
(`texture:PRODUCT@data`; the 2 sRGB files as `@data-decoded`, with the decoded RGB). Limit: 8 diffuse and 4 specular
files are linear (mostly default textures); they import as sRGB.

Important: the cutout of a masked material is the alpha of the diffuse map, for both shaders. Before stage M1, the
converter put the Opacity map into the alpha of the base color. That is wrong: for Vegetation the Opacity map is
the transmitted light.

### Textures

- `dds.py` reads the legacy header (`DXT1`, `DXT3`, `DXT5` with no DX10 part): the 5 default textures
  (`textures/defaults/white.dds` and others, 53 diffuse slots). It reads the A8 alpha image (DXGI 65).
- A texture key has its color space: `texture:PRODUCT` is the sRGB base color with its own alpha or a normal map
  (the PNGs of stage 1.2), `texture:PRODUCT@linear` is the red channel of the main image (a transmission map, a
  mask), and `texture:PRODUCT.a@linear` is the alpha image (a gloss). The one-channel PNGs are in
  `content/textures/m1-1024/`. Unreal imports them as linear data, `TC_Alpha` (BC4 from the red channel), and the
  parents sample them with the sampler type Alpha (the value is `.rrrr`).

## The audit (stage M0)

```sh
tools/convert-materials.sh audit       # content/materials/records.json, slots.json, audit.json
tools/convert-materials.sh chains      # the exit check: the source chains of the research proofs
```

- Sources: the sub-materials of the 17,305 converted world meshes, the material overrides of the region metadata
  (`materialOverrideAssetId` in `region.slicedata`: 608 files), the material overrides of the mesh components in the
  chunk, capital, and distribution slices (for example the lantern "on" and the forge "off" materials of Oxboro), and
  the far meshes.
- The slot provenance (`slots.json`): for each mesh, each source sub-material index with the glTF material name of its
  primitives. The glTF is the truth. The converter writes one primitive for each drawn subset, in the order of the
  mesh chunks and their subsets. The audit reads the same order from the chunk headers (no `.cgfheap`) and checks the
  index count of each primitive. A mesh converted before a reader fix has the key of that time (for example the
  RGBA8 diffuse of the dead oak leaves, DXGI 29): the primitives give its real key.
- The texture check reads the DDS header of each active slot (no decode). Result: no texture that the converter
  cannot read.

## The material instances (stage M1 and later)

```sh
tools/convert-materials.sh instances --candidate m1,m2   # instances.json (deployed), instances-candidate.json, textures.json
tools/apply-materials.sh               # the textures, the parents, and the instances of instances.json (with the import lock)
tools/convert-materials.sh proof-plan m2 && tools/material-proof.sh m2   # the render check of a stage
```

- One spec for each material key that changes. The spec comes from the render rules of the main source of the key
  (the source of the most mesh slots). The other render keys of the key are in the spec ("other render keys").
- `instances.json` has the specs of the deployed stages (`material_records.DEPLOYED`): the import pipeline and
  `tools/apply-materials.sh` apply only this file. `instances-candidate.json` has the specs of the stages under check:
  only the render check reads it (transient instances in `/SwcGameContent/Test/MaterialProof`). A stage goes into
  `DEPLOYED` when its check passes.
- The specs cover each key that has a source, also a key that the manifest does not have yet (for example an override
  material that the world track imports later): the hook of the import applies its spec at its first import.
- `Scripts/apply_materials.py` imports the textures of the specs, makes the parents (`Scripts/swc_materials.py`), and
  sets each instance `/SwcGameContent/Materials/KEY` from its spec. The asset path does not change: no mesh import
  and no cell build. The state of each instance is `materials:KEY` in `import-state.json` (the hash of the spec and
  the rule versions of `swc_materials.py`). A key that `tools/import.sh` did not import yet is skipped.
- The script holds the import lock, as `tools/import.sh`, and stops after `--max-seconds`.
- The import pipeline calls the same code: at the end of its step 4, `Scripts/import_content.py` calls
  `swc_materials.apply_specs(keys, state)` with the material keys that the step made or set again. A key with no spec
  keeps the rules of stage 1.2. A failure of this call goes into the import report; the import continues. Test:
  `tests/converter/test_swc_materials.py` (a stub of the module `unreal`).

### M_SwcFoliage (stage M1)

The parent of the Vegetation shader: masked, two sided, the shading model Two Sided Foliage.

| Output | Value | Source |
| --- | --- | --- |
| Base color | diffuse map x `DiffuseColor` (the XML `Diffuse`) | the source shader: `cAlbedo *= PerMaterial_DiffuseColor.rgb` |
| Opacity mask | the alpha of the diffuse map; the clip value is `AlphaTest` | the source cutout |
| Normal | the normal map | |
| Roughness | 1 - `Smoothness` x gloss; `Smoothness` = max(1/127, `Shininess`/255) | research section 3: the source GGX squares (1 - s), and Unreal squares its Roughness |
| Specular | luminance(XML `Specular`) / 0.08 | Unreal F0 = 0.08 x Specular |
| Subsurface color | saturate(`BackDiffuseMultiplier` x transmission.r) x `TransmittanceColor` | the transmission block of the source shader |

Unreal's foliage response is not the same shader as the retail transmission model: a side-by-side check with the
retail client decides.

### M_SwcSurface and M_SwcSurfaceMasked (stage M2)

The parents of the Illum shader. `M_SwcSurfaceMasked` takes the cutout from the alpha of the diffuse map.

| Output | Value | Source |
| --- | --- | --- |
| UV | TexCoord0 x `UVTransform`.rg + `UVTransform`.ba | the TexMod tiling and offset of the diffuse slot |
| Base color, metallic | `REFLECTANCE_HLSL` of D = diffuse map x `DiffuseColor` and S = specular map x `SpecularColor` | research section 3 |
| Specular | saturate(luminance(S) / 0.08) | Unreal F0 = 0.08 x Specular |
| Roughness | 1 - `Smoothness` x gloss | as M_SwcFoliage |
| Normal | the normal map | |

The F0 split: a dielectric (luminance of S at most 0.08) has the base color D, metallic 0. Above 0.08 the specular input
is 1, and u = 1 - metallic solves 0.08 u^2 - (s + d) u + d = 0 (s, d: the luminances of S and D). Then the luminances
of the diffuse color and of F0 stay the same as in the source. The base color goes from S (u = 0: a metal) to D / u.
Limits: a colored F0 of a dielectric becomes one gray value; the rotation and the oscillators of the TexMod are not
shown; the normal, gloss, and specular maps use the tiling of the diffuse slot (`instances` counts the differences).

### Shared legacy keys and the n_ instances (A1)

One legacy key can serve source materials with different render rules (920 keys). The instance of a legacy key has
the rules of its main source (the render key of the most mesh slots, `content/materials/keys.json`). Each other
render key of the key has its own instance `n_` and 10 hex digits of its render key, made by the apply step
("create" in its spec). `materials.instance_key(element)` gives the key of the instance with the rules of a source
sub-material: the legacy key for the main rules, else its `n_` key. The override slot map of the world track
(`overrides.py`) uses it for the override materials (A2), and mesh rule 7 names the mesh slots with it (A3).

### M3a, M3b, M4 in the surface parents

| Part | Parameters | Source |
| --- | --- | --- |
| M3a overlay masks | `MaskTex` (the Custom slot, linear RGBA), `MaskMode`, `MaskColorR/G/B`, `MaskAmount`, `MaskOverride`, `MaskExtra`, `MaskSpecColor` | `AddMasking` (charactermasking.cfib) after the diffuse factor |
| M3b blend layer (static switch `Blend`) | `Diffuse2`, `Normal2`, `Gloss2`, `Specular2`, `BlendMask`, `BlendParams` (BlendFactor, BlendFalloff, -, ignore vertex alpha), `BlendLayer2Diffuse`, `BlendLayer2Specular`, `BlendLayer2Smoothness` (/256), `BlendLayer2Tiling`, `BlendMaskTiling` | b = saturate(abs(vertex alpha x mask.r x (1 + BlendFactor)) ^ BlendFalloff) (shadelib `GetLayerBlendingValue`); lerp of the diffuse, specular, smoothness, and tangent normal |
| M3b vertex colors | `UseVertexColor` | `%VERTCOLORS`: albedo x vertex RGB |
| M3b detail (static switch `Detail`) | `Detail` (linear data), `DetailTransform`, `DetailScales` (bump, diffuse, gloss, alpha mask) | `GetDetailMap`: sample.garb x 2 - 1; normal.xy += x y; D += D x z; s += s x w |
| M4 emission (static switch `Emissive`) | `Emittance`, `EmissiveIntensity` (raw data), `EmittanceColor` (RGB, A), `EmittanceParams` (gamma, has map); the global `EmissiveScale` of `MPC_SwcMaterials` | `GetEmittance`: Emittance RGB x A x 0.1 x (map x lum^(gamma - 1) x intensity x map.a, or 0.5) |

The order is the order of the source forward pass: D = diffuse x XML Diffuse, the blend layer, x vertex RGB, the detail,
the overlay masks, then the F0 split. A static switch makes one shader for each combination that the instances use.

The unit of the emission (M4, agreed with the environment work E2): the source Emittance A is in kcd/m2, and
`GetMaterial_Emittance` (x 0.1) gives engine units of 10,000 cd/m2 (`RENDERER_LIGHT_UNIT_SCALE`). The Unreal scene
color is in cd/m2. The client subsystem `USwcMaterialScale` sets `EmissiveScale` = 10,000 x `swc.LightScale` (the
look scale of the local lights, default 3), so that a fixture and its light stay in balance. The local lights of the
data do not switch by the hour (E2): the fixtures stay on. The asset default of `EmissiveScale` is 1 (the offline
render checks).

### M_SwcGlass (M4, approved: the stage label `glass`)

The parent of the Glass shader (70 specs): Translucent, the shading model Thin Translucent (lighting mode Surface
ForwardShading).

| Part | Parameters | Source (glass.cfxb) |
| --- | --- | --- |
| Tint | `TintColor`, `TintColorMap` (Custom, %TINT_COLOR_MAP) | tint = saturate(TintColor x map) |
| Cloudiness | `TintCloudiness`, `TintMask` (Specular, %TINT_MAP with %SPECULAR_MAP) | c = saturate(TintCloudiness x K.r x K.g); tint = saturate(tint + 1 - K.r) |
| Dirt | `Dirt` (Diffuse, %DIRT_MAP), `DiffuseColor` | the lit dirt over the glass with its alpha; F0 goes to 0.03 |
| F0, gloss | `SpecularColor`, `Gloss`, `Smoothness`, `CloudinessReducesGloss` | %SPECULAR_MAP: F0 = K.rgb (K.b with %TINT_MAP); gloss x saturate(1 - K.g x CloudinessReducesGloss) |
| Normal | `Normal`, `BumpMapTile` | the UV x BumpMapTile |
| Flags | `GlassFlags` (tint mask, specular map, dirt, -) | |

The source color is lerp(lerp(lerp(background x tint, lit tint, c), lit dirt, dirt.a), the reflection, Fresnel).
Unreal (ThinTranslucentCommon.ush) gives the background x TransmittanceColor x (1 - Opacity) + the lit BaseColor x
Opacity + the specular. Thus TransmittanceColor = tint, Opacity = c + a - c x a, BaseColor = (tint x c x (1 - a) +
dirt x a) / Opacity. Unreal also squares the Fresnel loss and darkens the tint at grazing views (a better model; the
decision of the user to keep what Unreal does better). Limits: no refraction offset (`BumpScale`), no back light
(`BackLightScale`).

### Mesh rule 7 (M3, an option of `mesh.convert` until the rollout)

- COLOR_0: the source vertex colors (bytes B, G, R, A of the interleaved vertex). Unreal stores a vertex color as sRGB
  bytes (`StaticMeshBuilder`: `ToFColor(true)`) and the material reads those bytes as they are. The converter writes the
  linear value whose sRGB encoding is the source byte: the material gets the source byte / 255 (wind and blend data
  are not colors). The alpha is linear.
- The slot of each sub-material is named by `materials.instance_key`: one slot for each set of render rules (no
  collapsed slot, no wrong rules of a shared key).
- The cost measure on Oxboro (the rebuild set of `r_+04_+02`: 1,924 meshes, 16.9 M triangles) imports into
  `/SwcGameContent/Test/M3` (`tools/import-gltf-list.sh`), apart from the world: 829.6 s of import, 0 failures, the
  plugin +1.3 % (727.7 MB against 718.2 MB).
- The manifest entry of a rule 7 mesh keeps `materials` (the keys of the material records: the import pipeline makes
  their instances) and adds `slots` (the slot names). The audit reads an `n_` slot name as the legacy key of its
  sub-material.

The rebuild (M3 with A3; decision of the orchestrator: after the world pass, step c, never at the same time as a cell
build of the world track):

```sh
tools/convert-materials.sh rebuild-set       # content/materials/rule7-meshes.json: mesh.convert gives them rule 7
tools/convert-materials.sh rebuild-convert   # the rule 7 glTFs, their manifest records, materials/rebuild-import.json
tools/import-gltf-list.sh ~/Games/soulwarden-client/content/materials/rebuild-import.json REPORT.json \
    --max-seconds 300 --wait 7200            # again until "incomplete" is false
```

- The set: each mesh with a collapsed slot, a slot whose rules are not the main rules of its legacy key (A3), vertex
  data (`%VERTCOLORS`, `%BLENDLAYER`, `%DETAIL_BENDING`), or mesh decal subsets: 13,372 meshes, 163.8 M triangles.
- The mesh decal companions (option B of the orchestrator): rule 7 writes the drawable `%DECAL` subsets of a mesh
  (`mesh.is_mesh_decal`) into a second glTF, `STEM_decals.gltf`, with the manifest key `decals:MESH` (no Nanite:
  Nanite draws only surface materials). The main mesh stays Nanite, and its entry names the companion (`"decals"`).
  The slots of a companion are `dm_` instances of `M_SwcDecal` (the render key of the sub-material; with
  `%VERTCOLORS` the static switch `UseVertexAlpha`: the source decal alpha x the vertex alpha). Their specs are in
  `content/decals/instances.json` (`decals.specs`, from `content/decals/mesh-materials.json`). 575 meshes, 397,803
  companion triangles, 123 `dm_` materials. The cells place a companion at the transforms of its mesh (the world
  track, step d).
- The import replaces the meshes in their folders (the asset paths stay) and writes the hash of each mesh into the
  import state of the pipeline (`import-state.json`): `tools/import.sh` sees them as up to date. A call also holds
  `world-build.lock` exclusively when it exists (runs stream these meshes).
- The instances of the slots (`n_`, `dm_`) must exist first: the import tool applies their specs at the start of
  each call (a spec that is up to date is skipped).
- Then the world track builds the cells with overrides again: the slot names changed (A2b: `overrides.py` names the
  slots with `materials.instance_key`).

### M6 wind (M_SwcFoliage version 2; the stage label m6)

The source (modificatorvt.cfib `_VTBending`, `_DetailBending`, `_MainBending`; vegetation.cfxb):

- The Vegetation shader reads the vertex RGB as bend data, not as a color: R = the leaf edge amplitude, G = the branch
  phase, B = 1 - the branch amplitude. The foliage albedo is not multiplied by the vertex RGB.
- The vertex alpha is an occlusion: the diffuse and the specular color x vertex alpha (the deferred path). Version 2
  does this; with no vertex colors Unreal reads white (no change).
- Detail bending (175 specs with `%DETAIL_BENDING`): `BendDetail` = (`bendDetailFrequency`, `bendDetailLeafAmplitude`,
  `bendDetailBranchAmplitude`; the defaults of the shader 1, 0.2, -0.5). Four triangle waves of the time + the vertex
  phase + the object phase (dot(the instance origin, 2)); the speed is the wind strength x the bend of the instance x
  `SimpleWindNoise` (the instance origin and the time). The leaf edge moves along the vertex normal.xy, the branch
  along Z.
- Main bending: xy += the wind x the bend x (z x scale)^2, with the length kept. The source computes the bend vector
  on the CPU (not in the shader text): here the wind x the bend of the instance, and scale = 1 / the height of the mesh
  (the local bounds). A retail video calibrates it.
- The global wind: `WindVector` of `MPC_SwcMaterials`, the EnvState `WindVector` of the mission of the main world
  (1, 0, 0: its length is the strength; `content/materials/globals.json`).
- The bend of an instance: `SwcWind`, the custom primitive data 0 of the instanced component (the cells of the world
  track): 0 = no motion (the default). 1 for a slice mesh component with `ReceiveWind` (Oxboro: 1,561 vegetation
  components with it, 861 without it), Bending / 100 of the descriptor for the ground cover (0, 0.1, or 1).
- `WIND_HLSL` works in the engine space of the source (meters, (x, -y, z) / 100 of Unreal) and gives the local offset;
  a transform gives the world position offset.

Check: `tools/material-proof.sh m6` (the rule 7 test meshes with `SwcWind` 0, 1, 4: the bend in a still picture);
the motion needs a video side by side with a retail development video.

## The ground cover (for T3 of the world track)

```sh
tools/convert-materials.sh cover scan      # the descriptor tables of the 49 region.vegetation -> content/cover/descriptors.json
tools/convert-materials.sh cover convert   # the cover meshes that the manifest does not have, with rule 7
tools/convert-materials.sh cover pairs     # the slot maps of the (mesh, override) pairs -> content/cover/pairs.json
```

- The world: 6,517 descriptors, 357 meshes, 81 override files, 493 pairs, 172,811,292 records (the research total).
  A descriptor has the mesh, AutoMerge, Bending / 100 (0, 0.1, or 1), and the material override (the fall01 and fall02
  variants of the grass).
- The cover meshes convert with rule 7 (the bend data in the vertex colors): 131 new meshes now (8,990 triangles);
  the 226 meshes that the manifest has get rule 7 in the M3 rebuild (the set has all cover meshes).
- `pairs.json` has the format of `content/overrides/pairs.json` (each slot -> the instance key of the override
  sub-material with the same index: A2, A2b). `material-keys.json` has the 97 manifest keys for `tools/import.sh
  --keys` (the overrides and the material records of the new meshes); the 29 n_ keys come from the apply step.
- The materials: the audit reads the override files as the use "cover"; 127 keys (110 foliage, 14 surface,
  3 surface masked). The foliage parent has the Diffuse-alpha cutout, the Opacity map as the back light
  (transmission), two sided, and the wind.
- Stable names: a main render key of `keys.json` stays while it is still a render key of its legacy key (new sources
  do not rename the n_ instances).

## The surface layer (S1 with M5)

```sh
tools/convert-materials.sh decals export NAME...   # roads and decals of regions: content/decals/NAME/layer.json, the road meshes
tools/import-surface-layer.sh NAME... --max-seconds 300 --wait 7200   # the decal instances and the road meshes (the lock)
```

- `decals.py` walks the chunk slices and the capital slices (with the slices that they spawn at the start) and reads
  the roads (a spline component and a road component of one entity) and the projected decals (DecalConfiguration).
  The world: 24,713 roads, 286,082 decals (240,315 placed: visible, with a material), 658 decal materials.
- A road is a ribbon mesh: the cubic Bezier of each segment (the vertex, its forward handle, the back handle of the
  next vertex), the width interpolated between the vertices, the heights of the Landscape + 4 cm. The vertices are at
  most 1 m apart, along and across (the Landscape grid; the source road builder clips its mesh to the terrain units).
  With rows every SegmentLength (2 m) and five vertices across, 22 % of the ribbon triangles were under the ground,
  and the mesh decal failed the depth test there (straight-edged patches of the ground in the matched view
  noon-road-low); with 1 m, 0.55 % of the 1,222 Oxboro ribbons (p99.9 2.2 cm), 760 k triangles. U = distance /
  TileLength along the road, V across (the road maps fade their alpha along V). One glTF for each 256 m cell and sort
  priority; no Nanite (a decal material).
- `M_SwcDecal` version 3 moves the vertices of a mesh decal toward the camera along their view rays (no shift on the
  screen): 0 within 20 m, then 4 mm for each meter, at most 1 m (the Landscape LOD far away). A projected decal does
  not use it.
- The material of a dirt road (for example `jav_mnt_road_dirt_traffic_md_decal_a.mtl`) follows the source: the diffuse
  map x XML Diffuse (0.491: no `%TEMP_TERRAIN`), the alpha = the diffuse alpha (and the vertex alpha). Over the pale
  ground of the client it is a dark strip with soft holes; over the road dirt layer of the terrain (`roads_splat`,
  `jav_fst_terrain_roaddirt_a_nw.mtl`; T1 of the world track) it blends into the dirt, as in retail
  (evidence/WF3/s1/road-check). Not shown: POM and the specular map of the road material.
- A terrain decal (ProjectionType 2) projects straight down: the footprint |X| x |Y|, the depth |Z| x Depth of the
  entity matrix, the heading of X. Other decals project along their Z axis.
- `M_SwcDecal`: domain Deferred Decal, blend Translucent (DBuffer color, normal, roughness). Opacity =
  saturate(pow(DiffuseColor.w x diffuse.a x the decal opacity map x DecalAlphaMult, DecalFalloff)). One opacity for all
  channels: DecalDiffuseOpacity is a limit.
- The client: `USwcSurfaceLayer` (a world subsystem) reads `layer.json` of the regions near the view, places the road
  meshes within 1 km and the decals within their view distance (Max View Distance x View Distance Multiplier, at
  most 300 m). Opt-in until the cost check at a second dense place: `-SwcSurfaceLayer` or `SWC_SURFACE_LAYER=1`.
- Open: the dirt road maps have holes in their alpha; the terrain road layer (T1 of the world track) fills them.

## Cost checks

`-SwcFrameStats=SECONDS` (`USwcFrameStats`) logs the p50, p95, and largest frame and GPU time of each window with the
view place; `tools/convert-materials.sh frames LOG...` gives the median for each place (the first window at a place
still loads). Run the matched views with no picture capture, the same views with and without the change.
