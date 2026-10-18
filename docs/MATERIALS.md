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
| Detail | detail | `%DETAIL_MAPPING` | sRGB RGB, linear alpha |
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

## The material instances (stage M1 and later; the Unreal scripts come with stage M1)

```sh
tools/convert-materials.sh instances   # content/materials/instances.json, textures.json
tools/apply-materials.sh               # the textures, the parents, and the instances (with the import lock)
tools/convert-materials.sh proof-plan m1 && tools/material-proof.sh m1   # the render check of a stage
```

- One spec for each material key that changes. The spec comes from the render rules of the main source of the key
  (the source of the most mesh slots). The other render keys of the key are in the spec ("other render keys").
- `Scripts/apply_materials.py` imports the textures of the specs, makes the parents (`Scripts/swc_materials.py`), and
  sets each instance `/SwcGameContent/Materials/KEY` from its spec. The asset path does not change: no mesh import
  and no cell build. The state of each instance is `materials:KEY` in `import-state.json` (the hash of the spec and
  the rule versions of `swc_materials.py`). A key that `tools/import.sh` did not import yet is skipped.
- The script holds the import lock, as `tools/import.sh`, and stops after `--max-seconds`.

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
