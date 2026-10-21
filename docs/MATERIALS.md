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
| Emittance | emittance | `%EMITTANCE_MAP` | the color space of the file (AST-207) |
| Decal | emittance intensity | `%EMITTANCE_MAP` or `%EMISSIVE_DECAL` | the color space of the file |
| Occlusion | occlusion | `%OCCLUSION_MAP` | linear |
| Environment | none (Unreal reflections) | never | - |

The flag of a texture resource (AST-210, 2026-10-20, `~/Games/soulwarden-client/research/lights-emission.md` "Illum emission" [R]):
for Illum, an Emittance texture or a Decal intensity texture enables `%EMITTANCE_MAP` before the engine picks the shader permutation;
the text and numeric masks are only inputs (`material_records.resource_flags`). The record keeps the flag in "resource flags", and its
render key stays the key of the record without the flag (`render_rules` "key"): the instance keys `n_RENDER` are the material names
of the converted meshes. The rule changed 6 specs of the deployed instances.json and no key: the coal pits (`n_4056f40760`,
`n_d2bf9e1964`: the map branch, EmittanceParams [1, 1, 1, 0], the coal image as sRGB with its alpha, the forge noise linear with its
own 2 x 2 UV transform, the color and the strength 600 of the record), the glass lantern `n_e7800ff06d`, the raft `m_ff4ead5ca3`, and
two VFX circles (`n_95053d7b9d`, `m_c127d56484`; the second is not imported). Open: the TexMod oscillators of the intensity map (U and V
type 2, rate 0.5, amplitude 0.2; W turn type 2) have no formula in the study: the client keeps the static UV.

The Glass shader has its own table: Specular is the tint mask (`%TINT_MAP` or `%SPECULAR_MAP`), Custom the tint
color (`%TINT_COLOR_MAP`), Diffuse the dirt (`%DIRT_MAP`), SecondSmoothness the cloud gloss.

The sRGB decode: a source sampler with `sRGBLookup = true` (diffuse, specular, detail, the diffuse of the blend
layer, the glass tint maps) decodes sRGB only when the file is sRGB: an `_SRGB` DXGI format, or the flag 0x800 in the
second reserved word of a legacy header (`material_records.file_srgb`; the two agree in all 4,478 files of these roles).
A sampler without it (the blend mask, the gloss, the overlay mask, the second normal, gloss, and specular) reads the
values as they are. Exception, the emission (AST-207, GPT-6.1-Sol study `research/emission-fogcards.md`): the Illum
shader samples the Emittance and Decal intensity maps through the default view of the texture. The retail loader puts
the sRGB flag of the file into that view (the DDS flag 0x800 or an `_SRGB` format), also with no `sRGBLookup`. Thus an
sRGB emission file is decoded (RGB only; the alpha stays linear), and a linear file stays linear. The converter gives an
sRGB emission file as an sRGB texture (the shared base texture) on the Color samplers `EmittanceSrgb` and `IntensitySrgb`,
and a linear file as `@data` on `Emittance` and `EmissiveIntensity` (`material_records.m4`; surface parent version 8). A
decoded PNG as data (`@data-decoded`, the first try) lost the dark values: the BC compression of near-black data gave the
dye flowers a green cast. Before AST-207 all emission
maps were raw: the dark values were 10 to 13 times too large (the dye flowers of the menu glowed white). The detail maps are linear files (383 of 385): they import as linear data
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

### M_SwcFoliage (stage M1; version 3)

The parent of the Vegetation shader: masked, two sided, the shading model Two Sided Foliage.

| Output | Value | Source |
| --- | --- | --- |
| Base color | diffuse map x `DiffuseColor` (the XML `Diffuse`) | the source shader: `cAlbedo *= PerMaterial_DiffuseColor.rgb` |
| Opacity mask | the alpha of the diffuse map; the clip value is `AlphaTest` | the source cutout |
| Normal | the normal map | |
| Roughness | 1 - `Smoothness` x gloss; `Smoothness` = max(1/127, `Shininess`/255) | research section 3: the source GGX squares (1 - s), and Unreal squares its Roughness |
| Specular | luminance(XML `Specular`) / 0.08 | Unreal F0 = 0.08 x Specular |
| Subsurface color | saturate(`BackDiffuseMultiplier` x transmission.r) x `TransmittanceColor` x the base color (version 3) | the transmission block of the source shader; the source multiplies all diffuse light, the back light too, by the albedo |

The source (deferredshading `ThinTranslucencyBRDF`, vegetation.cfxb `frag_custom_end`): the light of a leaf is
Transmittance x the back term + the front term, and the sum is multiplied by the albedo. The transmission of Unreal
(`TwoSidedBxDF`: light x WrapNoL x Scatter x SubsurfaceColor) has no base color, so the albedo goes into the subsurface
color. Versions 1 and 2 did not have it: in the back light the leaves and the grass were near white (the subsurface
color about 0.6, the albedo about 0.03 linear; T3 first-set1 noon-road-low: L* 61 against 48 of retail, saturation
0.33 against 0.53). Unreal's Scatter lobe is view dependent (a glow against the sun): kept (the decision of the user).
`BlendTerrainCol` (the lerp of the diffuse to the terrain color, with distance) is 0 for all ground cover and for all
but one foliage material: only a fade at the end of the view distance; not done yet (it needs the terrain macro color).

The back light comes only from the lights in the source: `ThinTranslucencyBRDF` is in the deferred light, and the
ambient of the source has no back term. Unreal's Lumen also gathers the indirect light of the back face and adds
SubsurfaceColor x that light (`DiffuseIndirectComposite.usf`); the material cannot turn it off (one subsurface color
for the direct and the indirect light). Thus the project sets `r.Lumen.ScreenProbeGather.TwoSidedFoliageBackfaceDiffuse=0`
(DefaultEngine.ini `[SystemSettings]`, above the scalability group GlobalIlluminationQuality 3, which sets 1). Unreal
then adds SubsurfaceColor / pi to the diffuse color of the indirect light (a limit: about a third of the old indirect
back light for a blade with the same light on its two faces). The check (the matched views with the cover, main
c7e668e, evidence/WF3/colors-c7e668e/backlight; the bottom band L*, retail in brackets):

| Variant | fen-outside-noon (21.2) | fen-outside-south-noon (14.2) |
| --- | --- | --- |
| Before (the gather of the back face) | 38.8 | 35.6 |
| `TwoSidedFoliageBackfaceDiffuse 0` | 37.0 | 33.9 |
| All back light off (`swc.GroundCover.BackLight 0`) | 35.0 | |
| No indirect light | 36.1 | |
| No indirect light, no back light | 33.4 | |

In luminance, the back light of fen-outside-noon is 0.019: the direct part 0.012 (kept, as the source), the indirect
part about 0.007; the setting removes it. The rest of the gap is the light of the whole picture (E5).

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
| M4 emission (static switch `Emissive`) | `Emittance`, `EmissiveIntensity` (linear files, `@data`), `EmittanceSrgb`, `IntensitySrgb` (sRGB files, Color samplers), `EmittanceTransform`, `IntensityTransform` (the UV of each map, version 7), `EmittanceColor` (RGB, A), `EmittanceParams` (gamma, has map, the emittance map is sRGB, the intensity map is sRGB); the global `EmissiveScale` of `MPC_SwcMaterials` | `GetEmittance`: Emittance RGB x A x 0.1 x (map x lum^(gamma - 1) x intensity x map.a, or 0.5); no multiply by the diffuse or the tint |

The order is the order of the source forward pass: D = diffuse x XML Diffuse, the blend layer, x vertex RGB, the detail,
the overlay masks, then the F0 split. A static switch makes one shader for each combination that the instances use.

The unit of the emission (M4, agreed with the environment work E2): the source Emittance A is in kcd/m2, and
`GetMaterial_Emittance` (x 0.1) gives engine units of 10,000 cd/m2 (`RENDERER_LIGHT_UNIT_SCALE`). The Unreal scene
color is in cd/m2. The client subsystem `USwcMaterialScale` sets `EmissiveScale` = 10,000 x `swc.LightScale` (the
look scale of the local lights, default 1 from 2026-10-20, before 3), so that a fixture and its light stay in balance. The local lights of the
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

### M_SwcBeam and mesh rule 8 (AST-207, the stage label `beam`)

The GeometryBeam shader of the source (the fog sheets and fog cards, the fog spheres; the GPT-6.1-Sol study
`research/emission-fogcards.md`, "The rule for the client" 2). Before AST-207 the mesh rule 6 dropped these materials as
"not drawn": the 34 fog sheets and 8 fog cards of the menu scene were `empty`.

- Mesh rule 8 (`mesh.RULE_BEAMS`): rule 7, and a GeometryBeam sub-material is drawn (`materials.is_beam`; the procedural
  light beams of `%USE_AS_BEAMPROC` stay not drawn). The default rule of the meshes of `mesh.BEAM_FOLDERS`
  (`objects/volumetrics/`, `objects/vfx/mesh/`). The vertex RGBA of the source goes into the glTF (rule 7). The material
  record of a beam is translucent: no Nanite. A world mesh with a beam slot in another folder keeps its rule until a
  conversion with rule 8 (world work). The overrides (`overrides.VERSION` 3) and the audit see the beam slots.
- The spec (`material_records.beam_spec`, family `beam`): the base map (Diffuse, sRGB) with the stretch oscillators of its
  TexMod, the dust map (Specular, sRGB), the turbulence color (Bumpmap; no map: black, version 5 of the parent: the empty slot
  samples engineassets/textures/white_ddn.dds raw, signed BC5, about (0.0045, 0.0045, 0), research beach-sheet-inputs.md;
  version 4 had the flat normal (0.5, 0.5, 1), which made the dust term of a near beach sheet 10 to 13 times too strong), the XML
  Diffuse and Opacity, the public parameters (the main `PublicParams`, not the `MaterialLayers` entry), and the flags
  (`BeamFlags`: `%NOISE`, `%APPLY_SUN_COLOR`, `%ENABLE_FADEOUT`, `%VERTICAL_GRADIENT`; `BeamFlags2`: `%UV_VIGNETTING`,
  `%GRADIENT_ALPHA`, `%APPLY_FOG_COLOR`).
- The parent (`swc_materials.beam_parent`): Additive, Unlit, two sided, the depth test on, no depth write. The vertex
  shader (`BEAM_VS_HLSL`, two vertex interpolators) makes the angular fade q = saturate(dot(W, N)^2 x
  viewDependencyFactor)^2, B = FinalMultiplier x vertex RGBA x Diffuse x q, the color gradient lerp(StartColor, EndColor,
  u) and the vertex alpha curve, and the coordinate of the noise (the object position in source metres x (0.05, 0.05, 0.1)
  + the time x the noise speed). The pixel shader (`BEAM_PS_HLSL`): the source noise volume (`engineassets/textures/
  noise3d.dds`, 64^3 A8, as the tiles of `T_SwcNoise3D`, trilinear with wrap), the dust and turbulence scrolls, the depth
  fade F = min(saturate(s x min(zs - z, h, z)), saturate(s x (z - zn))) in source metres (s = SoftIntersectionFactor,
  h = the height above the water plane `WaterLevel` 40 m, zn = `NearClip` 0.1 m), the sun tint and the normalized fog tint,
  x (1 + AmbStrength), and the alpha min(base.r, base.a) x the UV vignette x (1 - exp2(-k B.a^2)). The source blend is
  SRC_ALPHA, ONE: the parent gives RGB x alpha as the emissive color, with opacity 1, x `EmissiveScale` x `BeamScale`. The
  conversion of the source units is 10,000 cd/m2 with no look scale: the light of the sheets comes from the sun and the
  ambient of the source, not from a local light, so `USwcMaterialScale` sets `BeamScale` = `swc.BeamScale` (default 1) /
  `swc.LightScale`. The asset default of `BeamScale` is 0: a client without this code draws no beams. The fog of the scene
  attenuates the result (Unreal translucency fog).
- `MPC_SwcEffects` (`USwcMaterialScale`, 4 times in a second): `SunColorSource` = the source sun of the hour on the ground
  (from 2026-10-20, with `swc.SunSource 1`: "Sun color" / its luminance x "Sun intensity" / pi / 10,000, research/beam-scale.md;
  the sun light of Unreal holds the light before its atmosphere, bluer and stronger at a low sun), else the main directional
  light color x its intensity / pi / 10,000 (the source units; the luminance of the water work), `FogColorSource` = the
  inscattering color of the height fog. Its own collection: a new parameter of `MPC_SwcMaterials` would compile all the world
  materials again. The log has the value each 10 s ("the sun of the effects"). In the menu at 16.7 h it is 3.573, 2.325, 1.838
  (the light gave about 4.48, 3.53, 4.72: the pink haze): the menu 21.8 / +24.4 -> 17.4 / +20.4 (dE / L*, with the menu ground of
  543638c). The sheets within 30 m of the Player Entity make all of the rest (`swc.MenuBeamBand 1`: 17.2 / +20.1; `2`, the far
  sheets only: 13.3 / +8.6; no sheets: 13.5 / +8.3); open: the rule that fades them in the source.
- Version 5 of the parent (2026-10-20): the shared UV of the source (research beach-sheet-inputs.md): UV0 turned by uvRot,
  then the stretch of the TexMod about UV zero, then its tiling and offset; the dust map samples the same UV x dustUVScale +
  the noise scroll (version 4 scaled about 0.5, did not turn the base map, and did not stretch the dust map).
- Version 1 and 2 of the parent did not compile (a wrong pin of the VertexColor node): Unreal drew the default material,
  a wall of gray squares in the menu (2026-10-20, 15:43 to 16:13 UTC). After an import, grep the client log for "Failed to
  compile".
- The menu check (AST-207, f120 of the drive f4-scene-ui against B6-04): with no sheets (`swc.BeamScale 0`) ΔE 18.1, L*
  +18.1 (before AST-207: 18.45, +19.9); with the sheets ΔE 28.5, L* +32.0 (with the look scale 3 in the conversion, the
  first try: ΔE 38.9, L* +42.1). The sheets make the mountain pale and the lava two thin streams, as in retail, but the haze
  is too bright and too thick. The scene without sheets is already about 3.4 times too bright (L* 38.6 against 20.5), and
  the additive sheets scale with it: the exposure of the menu is the next point (not a value of the sheets).
- Limits: no `%RECEIVE_SHADOWS` (the shadow samples; no menu material has it), no vertex deformation wave, no LOD fade;
  the relative view distance of `%ENABLE_FADEOUT` is the camera distance / `MaxViewDistance` 200 m (an inference); the
  TexMod stretch timing is an inference; the water plane is the world ocean level.
- `%ENABLE_FADEOUT`: the relative view distance is the camera distance / min(`MaxViewDistance` 8,000 m, `ViewDistRatio`
  60 x the bounding radius of the object): the CryEngine rule of `e_ViewDistRatio` (an inference). Thus a vista sheet of
  the menu (scale 114 to 201, 0.9 to 2.5 km away) does not fade; a fixed distance of 200 m (version 1) hid them.
- The water plane and the near clip of the menu (2026-10-20, the UI track): the source shader takes h = the world z -
  `PerFrame_WaterLevel.z` (`geometrybeam@beamps-397e1691` 0022; v5 is the absolute world position, `beamvs` 0042) and
  z - `PerView_NearFarClipDist.x`. The menu terrain has its own ocean level, 78 m (`nw_frontend_base/terrain.json`; the
  world 40 m), and the menu camera has a near clip of 0.2 m. The scene record has `environment.waterLevel`, and
  `ASwcMenuPreview` gives each M_SwcBeam material of its parts one dynamic instance with `WaterLevel` and `NearClip`
  (the parent and the world stay). The sheets in front of the camp (42 within 40 m of the camera, most with the material
  `fogsheet_foggybeach_cool`, s = 0.03) lost about 40 % of their light. That `.z` holds the ocean level is still the
  inference of the study.
- The menu scene (`frontend.py`): a mesh of `objects/volumetrics/` or `objects/vfx/` is in the subset only when it is a
  static mesh of rule 8; a mesh of `mesh.BEAM_FOLDERS` of an older rule goes to the wanted list. The override materials of
  the scene are audit sources ("menu override"), so that their slot keys have specs.

### M_SwcSurfaceTranslucent (AST-207, a mechanism with no use yet)

An Illum sub-material of `materials.BLENDED_ILLUM` (by name; empty now) is alpha blended: its render rules have the blend
`translucent` and the XML Opacity, its spec is the surface spec (with M4) on `M_SwcSurfaceTranslucent` (the surface parent,
Translucent, Surface ForwardShading; opacity = the diffuse alpha x `Opacity`), and its mesh has no Nanite. It is for the
vortex card of the menu vista (`FTUE_level_vortex_mat`: Opacity 0.65, no AlphaTest, an emittance map), which a later
maelstrom thread does (research `maelstrom.md`; the second card has a Distanceclouds override). The general rule is open: 720
Illum sources of the world have an Opacity under 1 and no AlphaTest (many with Opacity 0), and they draw opaque.
### Shadow proxies (AST-207)

A sub-material that the game uses only to cast the shadow of its mesh (the "Shadow" sub-material 4 of the spruces:
Illum, Specular 1, AlphaTest 0.3, the branch texture) is not drawn (`materials.is_shadow_proxy`, in `materials.not_drawn`).
The source data has no flag for it (no `MTL_FLAG_NODRAW` 0x400; the spruces mark their bark and branches
`MTL_FLAG_NOSHADOW` 0x20 and not the proxy, but most of the 807 "shadow" sub-materials of the records have siblings that
cast shadows): the name is the only sign. The rule: the lower-case name ends in `shadow`, `shadoww`, `shadow` + digits,
`shadow_` + one letter, or `shadow proxy` / `shadow_proxy` / `shadoww_proxy` and a suffix ("Shadow", "shadow2",
"shadow_a", "..._mat__shadow", "Shadoww_Proxy_01a"; not "shadowbox"). Before, only "shadow_proxy" matched: the F0 split
made the spruce proxies white metal (white glittering patches in the trees). The proxy is not kept as a shadow caster:
in Unreal the bark and branches cast the shadow. The 19 spruce meshes are converted and imported again (rule 7); the other
meshes with a proxy change at their next conversion.

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
- The Lumen mesh data (the decision of the user): a main mesh of 1 m or more imports with a mesh distance field
  (resolution scale 1) and Lumen cards (12), the settings of the Lumen copies of E2 (evidence/E2/lumen):
  `swc_materials.lumen_pipelines` makes `P_SwcMeshNaniteLumen` and `P_SwcMeshLumen`, copies of the world pipelines
  with these two values (the world pipelines do not change). Not for the decal companions and not for the far
  meshes. The item flag `lumen` of `rebuild-import.json` (`material_records.gltf_size`). The import is about 5 times
  slower with them (about 4,100 triangles per second against about 20,000): the decision of the user is to add them
  later in a background job (after step d), not in the rebuild. 120 Oxboro meshes have them from the first call.
- Then the world track builds the cells with overrides again: the slot names changed (A2b: `overrides.py` names the
  slots with `materials.instance_key`).

Step c (2026-10-19): 13,505 meshes (163.8 M triangles; with the 131 new cover meshes) and 575 companions, 14,080
items in 28 calls, 8,232 s of import (Oxboro 742 s, the rest 7,490 s; 7.4 h of wall time with the locks of the other
tracks), 0 failures. The rebuild folders have 6.86 GB in the plugin (the companions 59 MB); the DDC grew at most
5.2 GB. 3 material keys had a record but no instance (11 uses): `tools/import.sh --keys` made them, and the 7 meshes
with these slots imported again. Then M3b and the M4 emission deployed (`DEPLOYED`): 3,616 specs changed (Blend
1,787, Detail 1,395, UseVertexColor 1,750, Emissive 510).

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
Deployed on 2026-10-20 (the world track, the decision of the orchestrator): `DEPLOYED` has `m6` (only the switch `Wind`
and `BendDetail`). The video against retail shows almost no motion: see docs/WORLD.md "The wind flag" (the main
bending has no time term).

## The camera fade of the vegetation (M_SwcFoliage version 4, 2026-10-20)

`camera_fade` of swc_materials.py: the source helper `GetCameraDissolveOpacity` (common.cfib; research/ground-cover.md
"Camera fade"): d = the 3D distance of the pixel to the render camera, o = saturate((d - 2) / (3 - 2)), a fragment with
o <= 0.99 is discarded where the noise frac(52.9829189 x frac(dot(pixel, (0.06711056, 0.00583715)))) is above o. The mask
multiplies the opacity mask (the depth pass has the same cutout). The values: `VegCameraFade` of `MPC_SwcMaterials`
(version 3; method 1, start 2 m, end 3 m); `swc.Foliage.CameraFade 0` sets the method 0 (no fade).

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
- The loading (2026-10-20, the loading gate of the world track (docs/WORLD.md "The loading")): during a loading screen the layer updates at the place of the
  character in each frame, and the screen ends only when the roads within 300 m and the decals within 150 m are shown.
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
- `M_SwcDecal` version 4 and 5 (the matched view noon-wall-grass with the layer on: the moss decals at the foot of the town
  wall made dark gray rectangles on the wall):
  - The angle fade of the source deferred decal (deferredshading.cfxb `getDecalAttenuation`): the alpha x
    smoothstep(0.55, 1, dot(the normal of the receiving surface, the decal normal)), for the projected decals only
    (static switch `Projected`: the `dp_` instances of `decals.specs`; the road ribbons and the mesh decals keep `d_`).
    The DBuffer has no normal yet: the face normal from the derivatives of the world position. The absolute value of
    the dot: the X axis of our decals is the source decal normal (`X = front`), the reverse of the Unreal convention
    (the projection along +X); the box is symmetric, so the projection does not depend on it. Open: the depth fade
    of the source along the box.
  - The Color and the Opacity of the decal entity (`g_DecalDiffuse` of the source; the moss of the wall: 0.25):
    `USwcSurfaceLayer` sets them with `SetDecalColor`, the material reads `DecalColor` (RGB x the base color, A x the
    alpha). Before, the decals had the opacity 1.
  - Version 5: the view bias of the mesh decals in the vertex shader from the camera position (`CameraVectorWS` is a
    pixel input: versions 3 and 4 did not compile, and Unreal drew the default material on all decals). The render
    check now compiles each parent again after its "parents ready" line, so that such a fault shows.
  - The check (evidence/WF3/m5-cost-quiet/wall-decals-v3-v5.png): the dark rectangles on the wall are gone.
- `M_SwcDecal` version 6: the normal of the projected decals. Unreal turns the tangent normal (x, y, z) of a projected
  decal into -z X + y Y + x Z of the decal axes (MaterialTemplate.ush `TransformTangentNormalToWorld` with
  `DECAL_PRIMITIVE`), because its decals project along +X. Our X is the source decal normal, out of the surface (4,991
  of the first 5,000 terrain decals of r_+04_+01 have X up). Version 5 thus had the normals into the surface: no sun on
  the decal, a black patch (the seaweed decals around the rock of the matched view ocean-clear-noon). Version 6 changes
  the sign of z for the projected decals (static switch `Projected`): the flat normal points out of the surface, x
  stays along U (Z), y along V (Y), as on a mesh. The road ribbons and the mesh decals use the tangents of their mesh.
- `M_SwcDecal` version 7 and `decals.py` version 5: the reflectance of the decal. The source zpass encodes all the
  G-buffer channels of a decal, the reflectance too, and blends them with the decal alpha (commonzpass `EncodeGBuffer`,
  `SetGBufferBlending`). The specs now have the specular map and SpecularColor (XML Specular), and the parent writes
  Specular = lum(map x SpecularColor) / 0.08, as the surface parents (Metallic 0: a limit for a decal above 0.08).
  Before, the DBuffer had the Unreal default 0.5 (a reflectance of 0.04) at the full decal alpha; the road decals of the
  source have 0.001 to 0.01 (Unreal 0.01 to 0.12). The check (the matched view noon-road-low, evidence/WF3/colors-5f83073/
  decal-test and colors-c7e668e/decal-test2): the G-buffer of the ribbon was right (the base color 61, 57, 52 against
  the terrain 52, 48, 43; the normal up; the roughness 0.8 to 0.9), and the light alone was the same with the layer on
  and off; with no dynamic shadows the ribbon was still pale (208, 189, 169 against 151, 112, 77), with no reflections
  and with no indirect light too. The extra light was the sun specular of a dark rough surface at a grazing view.
- The deploy of a decal parent and of the decal specs: `tools/material-proof.sh STAGE` makes the parent of a new version,
  `tools/import-surface-layer.sh` with all the regions applies the `d_` and `dp_` instances, and
  `tools/apply-materials.sh dm_ --part decals` the `dm_` instances of the mesh decal companions (versions 6 and 7: 71 and
  31 specular maps, 660 and 123 instances, no failure).
- The check of versions 6 and 7 (main c7e668e with the cover, evidence/WF3/colors-c7e668e/decal-v7; the bottom band of
  the color report and the boxes of `tools/lib/material_bands.json`, client against retail):
  - noon-road-low: the bottom band dE 1.9, L* +1.9 with the layer (the layer off: dE 0.8; version 5: L* +7.1). The road,
    sunlit: L* 64.4, C 27.3 against 59.0, 24.4 (dE 5.2; version 5: L* 70.5, C 6.9, dE 14.3); shadowed: dE 3.7.
  - noon-n: the bottom band dE 11.9, L* +12.3 with the layer (version 5: L* +22.5); the layer off: dE 1.4, but that ground
    is bare dirt with no cobbles (a mean color near retail by chance). The cobbles have the retail pattern; sunlit L*
    65.5 against 55.6 (dE 9.3; version 5: 14.0), shadowed 49.5 against 37.7. The whole picture with the layer off is
    L* +9.5 (the top band +17.3): the rest of the gap is the light of the picture (E5).
  - ocean-clear-noon: no black patch; the seaweed is olive (149, 143, 95). The bottom band dE 20.2 with the layer, 22.1
    without (the light of the picture and the water).
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
  most 300 m). On by default since the M5 cost check; `-SwcNoSurfaceLayer` or `SWC_SURFACE_LAYER=0` turns it off,
  the console variable `swc.SurfaceLayer 0` hides it in a run.
- The M5 cost check (2026-10-19, two quiet GPU windows: no other client, no import; `-SwcFrameStats=5`, the median GPU
  p50 of the 5 s windows while a view is in place; the layer on, then off with `swc.SurfaceLayer 0`, then on again in
  one run). GPU p50 ms, on / off: noon-road-low 7.82 / 7.46, 7.89 / 7.59, 8.50 and 8.35 / 8.13; noon-wall-grass
  8.38 / 8.12, 8.34 / 8.14, 9.72 and 8.63 / 8.81; river-forest-noon 9.65 / 10.10, 11.60 and 10.55 / 11.01;
  river-highlands-noon 10.19 / 9.97, 11.02 and 10.08 / 10.47. The GPU time falls by up to 1 ms within a run (the
  first views are measured first): the cost of the layer is at most about 0.3 ms at Oxboro (597 k road triangles
  within 1 km, about 2,400 decals within 300 m) and within the noise at the river places.
- Open: the dirt road maps have holes in their alpha; the terrain road layer (T1 of the world track) fills them.

## Material colors (the matched views, sunlit and shadowed apart)

```sh
python3 -B tools/lib/material_bands.py tools/lib/material_bands.json FOLDER [VIEW...]   # FOLDER: a tools/match-view.sh folder
```

- `tools/lib/material_bands.json` has, for each matched view, boxes of one material each (fractions of the 1280 x 720
  picture), and light references (a neutral material of the same orientation, marked 1).
- In each box, each picture alone is split into a sunlit and a shadowed class by the Otsu threshold of L* (no split when
  the classes are less than 12 L* apart). The shadow fill of the client (the E5 work) does not move the sunlit values.
- For each class: the share, L*, the chroma, the hue (CIELAB of the mean linear color), CIEDE2000 client against retail;
  for the sunlit class the client / retail ratio of the linear RGB, divided by the ratio of the references (the light
  cast): near 1 in each channel, the material follows the light.
- Output: FOLDER/material-bands.json and material-bands.txt.

## Cost checks

`-SwcFrameStats=SECONDS` (`USwcFrameStats`) logs the p50, p95, and largest frame and GPU time of each window with the
view place; `tools/convert-materials.sh frames LOG...` gives the median for each place (the first window at a place
still loads). Run the matched views with no picture capture, the same views with and without the change.
