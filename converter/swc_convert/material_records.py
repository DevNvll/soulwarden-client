"""Rich material records (stage M0 of the materials track, docs/MATERIALS.md).

A source material is one sub-material of a `.mtl` file (CryEngine XML). Its rich record keeps all the values of the
source: the shader, the generation flags (the text and the number), the lighting values, the public parameters, and
each texture slot with its product, its UV modifiers, its sampler role, and its color space.

Two keys:
- The legacy key (`m_` and 10 hex digits, materials.shared_material): the name of the glTF material and of the Unreal
  material instance of the meshes that are imported now. It does not change: the meshes keep their slots.
- The render key (`r_` and 10 hex digits): the hash of the render rules, the values that the parent materials of the
  client use. Two source materials with the same render key look the same. Names (the display name of the
  sub-material, the surface type) are provenance only: they are not in the render rules.

The sampler roles come from the shader and its flags (the `.ext` files of the game and the decoded shader scripts of the
research, research/world-materials.md, section 3). A slot that the shader does not sample with the flags of the
material is inactive: it is in the rich record, not in the render rules.

Usage (tools/convert-materials.sh):
  audit                the records of the source materials of the converted meshes, the provenance of the mesh slots,
                       the key collisions, and the texture checks -> SWC_CONTENT/materials/
  instances [--candidate STAGES]  the spec of each material instance that changes and its textures (after audit);
                       --candidate m1,m2: also instances-candidate.json for the render check of stages under check
                       -> SWC_CONTENT/materials/instances.json, textures.json; then tools/apply-materials.sh
  proof-sheet [STAGE]  the pictures of the render check with the names of the columns
  decals export NAME... | specs | scan NAME...   the roads and the projected decals of regions (decals.py: the
                       road meshes, the decal list, layer.json for the client) and the specs of their materials
  frames LOG...        the frame time of runs with -SwcFrameStats, for each view place (the cost checks)
  cover scan|convert|pairs   the ground cover of region.vegetation (cover_materials.py): the descriptors, the cover
                       meshes (rule 7), the slot maps of the override pairs -> SWC_CONTENT/cover/
  no-nanite            the meshes that draw a translucent slot (own or of an override pair) and have Nanite (nanite.py,
                       AST-288 W2) -> SWC_CONTENT/materials/no-nanite.json; the import gives them no Nanite
  rebuild-set          the meshes of the M3 rebuild (mesh rule 7) -> SWC_CONTENT/materials/rule7-meshes.json
  rebuild-convert      convert them with rule 7 (the manifest records) and write rebuild-import.json (the list of
                       tools/import-gltf-list.sh --state: the existing mesh folders)
  chains               the exit check of M0: the source chains of the research proofs and the on/off overrides
  proof-plan [STAGE]   the meshes and variants of the render check of a stage (Scripts/material_proof.py)
  record MTL [INDEX]   print the rich records of one material file
"""
from __future__ import annotations

import functools
import hashlib
import json
import re
import struct
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

from . import env, manifest, materials, source

RECORD_VERSION = 1   # the rule of the rich records and of the render rules

FLAG = re.compile(r"%[A-Z0-9_]+")
# Old flag names of the source files, and the name that the shaders use now (research section 1, "Generation flags").
FLAG_ALIASES = {"%BUMP_MAP": "%NORMAL_MAP", "%SPEC_MAP": "%SPECULAR_MAP"}

# The flags that change what the parents draw (the other flags select hardware paths or editor helpers, for example
# %ALLOW_SILHOUETTE_POM and %ALLOW_SPECULAR_ANTIALIASING).
RENDER_FLAGS = {
    "%NORMAL_MAP", "%SPECULAR_MAP", "%DETAIL_MAPPING", "%DETAIL_MAPPING_UV_SET_2", "%BLENDLAYER", "%VERTCOLORS",
    "%DECAL", "%DECAL_WORLD_UVS", "%EMITTANCE_MAP", "%EMITTANCE_MAP_UV_SET_2", "%EMISSIVE_DECAL", "%DETAIL_BENDING",
    "%LEAVES", "%GRASS", "%TEMP_VEGETATION", "%PARALLAX_OCCLUSION_MAPPING", "%SILHOUETTE_PARALLAX_OCCLUSION_MAPPING",
    "%OFFSET_BUMP_MAPPING", "%DISPLACEMENT_MAPPING", "%TRANSMITTANCE", "%OVERLAY_MASK", "%COLOR_SAMPLER_OVERLAY_MASK",
    "%ALPHAMASK_DETAILMAP", "%OCCLUSION_MAP", "%TINT_MAP", "%TINT_COLOR_MAP", "%DIRT_MAP", "%FX_DISSOLVE",
    "%BLENDLAYER_IGNORE_VERTEX_ALPHA",
    # GeometryBeam (AST-207, swc_materials.beam_parent)
    "%NOISE", "%APPLY_SUN_COLOR", "%APPLY_FOG_COLOR", "%ENABLE_FADEOUT", "%VERTICAL_GRADIENT", "%GRADIENT_ALPHA",
    "%UV_VIGNETTING", "%RECEIVE_SHADOWS",
}
POM_FLAGS = {"%PARALLAX_OCCLUSION_MAPPING", "%SILHOUETTE_PARALLAX_OCCLUSION_MAPPING", "%OFFSET_BUMP_MAPPING",
             "%DISPLACEMENT_MAPPING"}

# The public parameters of each feature: a parameter is in the render rules only when its feature is on.
PARAMS = {
    "blend": ("BlendFactor", "BlendFalloff", "BlendLayer2Diffuse", "BlendLayer2Smoothness", "BlendLayer2Specular",
              "BlendLayer2Tiling", "BlendMaskTiling", "VertexAlphaBlendFactor"),
    "detail": ("DetailBumpScale", "DetailDiffuseScale", "DetailGlossScale"),
    "pom": ("HeightBias", "PomDisplacement", "SelfShadowStrength", "ObmDisplacement"),
    "decal": ("DecalAlphaMult", "DecalDiffuseOpacity", "DecalFalloff"),
    "emittance": ("EmittanceMapGamma",),
    "mask": ("Mask_R", "Mask_R_Color", "Mask_R_Override", "Mask_G", "Mask_G_Color", "Mask_G_Override", "Mask_B",
             "Mask_B_Color", "Mask_B_Override", "Mask_A", "Mask_A_Color", "Mask_A_Gloss", "Mask_A_GlossShift",
             "Mask_A_SpecColor", "Mask_A_SpecColor_Override", "ColorMaskAlphaInf", "ColorMaskOverride",
             "ColorMaskStrength"),
    "transmission": ("BackDiffuseMultiplier", "TransmittanceColor", "NormalViewDependency", "BackViewDep"),
    "leaves": ("CapOpacityFalloff", "BlendTerrainCol", "BlendTerrainColDist"),
    "bending": ("bendDetailFrequency", "bendDetailLeafAmplitude", "bendDetailBranchAmplitude"),
    "glass": ("TintColor", "TintCloudiness", "BumpScale", "BumpMapTile", "BackLightScale", "CloudinessReducesGloss",
              "BlurAmount", "RoughnessBoost", "RoughnessMaxFootprint", "FogColor", "FogDensity", "FogCutoffEnd"),
    "beam": ("StartColor", "EndColor", "FinalMultiplier", "SoftIntersectionFactor", "viewDependencyFactor",
             "fadingFeaturing", "baseUVScale", "dustUVScale", "dustTimeScale", "turbStrength", "turbRatio", "uvRot",
             "BackgroudAlphaNoiseSpeedX", "BackgroudAlphaNoiseSpeedY", "BackgroudAlphaNoiseSpeedZ", "SunColorInfluence",
             "FogColorInfluence", "FadeOutDistance", "uvVigFeaturing", "AmbStrength"),
}

# C8 (AST-275): the effect shaders Fxmeshadvancedtransp and Meshparticle (research/ast275-effect-shaders.md). Their rules go to
# rules["fx"] (_rules), which render_key leaves out: the render keys and the n_ instance names (the slot names of the converted
# meshes) of these records do not change. The stage "c8" gates their specs (fxmesh_spec, meshparticle_spec).
FX_SHADERS = {"fxmeshadvancedtransp": "fxmesh", "meshparticle": "meshparticle", "geometrybeamsimple": "beam simple"}
FX_FLAGS = {"%USE_SDF_2D", "%USE_GLOW_FRESNEL", "%USE_OUTSIDE_FRESNEL_ALPHA", "%USE_INSIDE_FRESNEL_ALPHA",
            "%USE_INTERSECTION_FADE", "%USE_INTERSECTION_GLOW", "%USE_PALETTE_MAP", "%USE_COMPLEX_COL",
            "%USE_COMPLEX_COL_OVERLAY", "%USE_COMPLEX_COL_DODGE", "%USE_PROC_GRADS", "%VERT_DEFORM_SINWAVE", "%VERTCOLORS",
            "%APPLY_SUN_COLOR", "%ENABLE_FADEOUT", "%DIFFUSE_MAP_2", "%DIFFUSE_MAP_3", "%DIFFUSE_MAP_4", "%USE_REFRACTION",
            "%FX_DISSOLVE", "%NOISE", "%RECEIVE_SHADOWS"}
# The shader defaults of the public parameters (study section 3: FX parent core, FX scope extensions, Meshparticle parent).
FX_DEFAULTS = {
    "fxmesh": {"GlobalColorFalloff": 1.0, "GlobalAlphaStrength": 1.0, "GlobalAlphaFill": 1.0, "GlobalAlphaFalloff": 1.0,
               "GlobalAlphaFromSource": 0.0, "SDF_2D_STRENGTH": 0.1, "SDF_2D_TILE_X": 1.0, "SDF_2D_TILE_Y": 1.0,
               "SDF_2D_SPEED_X": 0.0, "SDF_2D_SPEED_Y": 0.0, "SDF_2D_PHASE_X": 0.0, "SDF_2D_PHASE_Y": 0.0,
               "SOURCE_2D_TILE_X": 1.0, "SOURCE_2D_TILE_Y": 1.0, "SOURCE_2D_SPEED_X": 0.0, "SOURCE_2D_SPEED_Y": 0.0,
               "SOURCE_2D_PHASE_X": 0.0, "SOURCE_2D_PHASE_Y": 0.0, "FRESNEL_EXPONENT": 1.0, "FRESNEL_STRENGTH": 1.0,
               "FRESNEL_INVERT": 1.0, "FRESNEL_SDF_INFLUENCE": 0.0, "FRESNEL_SDF_MAP_MIX": 0.0, "FRESNEL_SDF_MAP_EXP": 1.0,
               "FRESNEL_OUT_FALLOFF": 1.0, "FRESNEL_OUT_STRENGTH": 1.0, "FRESNEL_OUT_SDF_INF": 0.0,
               "FRESNEL_IN_FALLOFF": 1.0, "FRESNEL_IN_STRENGTH": 1.0, "FRESNEL_IN_SDF_INF": 0.0,
               "INTERSECT_FADE_DIST": 2.0, "INTERSECT_FADE_FALLOFF": 1.0, "INTERSECT_FADE_INV": 0.0,
               "INTERSECT_GLOW_DIST": 2.0, "INTERSECT_GLOW_FALLOFF": 1.0, "INTERSECT_GLOW_INV": 0.0,
               "PALETTE_PHASE": 0.0, "PALETTE_SPEED": 1.0, "PALETTE_FALLOFF": 1.0, "PALETTE_SDF_INF": 0.0,
               "PALETTE_SOURCE_BLEND": 0.0, "COMPLEX_COL_TILE": 1.0},
    "meshparticle": {"FadeOutDistance": 0.2, "AmbStrength": 1.0, "Brightness": 1.0, "SunColorInfluence": 0.5,
                     "SoftIntersectionFactor": 1.0, "fadingFeaturing": 0.55, "viewDependencyFactor": 2.0},
    # GeometryBeamSimple (research/beamsimple.md, the shader defaults [R]).
    "beam simple": {"FinalMultiplier": 1.0, "SoftIntersectionFactor": 1.0, "StartColor": [1.0, 1.0, 1.0, 1.0],
                    "EndColor": [1.0, 1.0, 1.0, 0.0], "fadingFeaturing": 0.55, "AmbStrength": 0.12, "baseUVScale": 1.0,
                    "dustUVScale": 0.6, "dustTimeScale": 1.0, "turbStrength": 1.0, "turbRatio": 0.55, "uvRot": 0.0,
                    "BackgroudAlphaNoiseSpeedX": 0.1, "BackgroudAlphaNoiseSpeedY": 0.05, "BackgroudAlphaNoiseSpeedZ": 0.15,
                    "VolumetricScale": 0.7},
}
FX_SLOTS = {"fxmesh": {"Custom": "source", "Heightmap": "sdf", "Detail": "palette", "Specular": "complex"},
            "meshparticle": {"Diffuse": "diffuse", "Custom": "diffuse2", "Emittance": "layer mask"},
            "beam simple": {"Diffuse": "base", "Bumpmap": "turbulence", "Specular": "dust"}}

# Color spaces of the sampler roles (research section 3, "sRGB rules"): srgb = sRGB RGB and linear alpha; normal = the
# normal map compression; linear = data; file = the color space of the file (the emission maps, AST-207; the detail).
# The color space of each role (the sRGBLookup of the source sampler). A sampler with sRGBLookup decodes sRGB only for
# an sRGB file (file_srgb): the detail maps are linear files (383 of 385: data_texture), and 8 diffuse and 4 specular
# files are linear (a limit: they are imported as sRGB).
COLOR = {"diffuse": "srgb", "normal": "normal", "gloss": "linear", "specular": "srgb", "detail": "file",
         "diffuse2": "srgb", "normal2": "normal", "gloss2": "linear", "specular2": "linear", "blend mask": "linear",
         "overlay mask": "data", "transmission": "linear", "decal opacity": "linear", "height": "linear",
         "height2": "linear", "emittance": "file", "emittance intensity": "file", "occlusion": "linear",
         "tint mask": "srgb", "tint color": "srgb", "dirt": "srgb", "cloud gloss": "linear", "dust": "srgb",
         "turbulence": "file"}


def number(text: str):
    """@return A source value as a number or a list of numbers (rounded to 7 significant digits), else the text."""
    try:
        values = [float(f"{float(v):.7g}") for v in str(text).split(",")]
    except ValueError:
        return str(text)
    return values[0] if len(values) == 1 else values


@functools.lru_cache(maxsize=None)
def ext_flags(shader: str) -> dict[int, str]:
    """@return The generation flags of a shader (bit -> name) from its `.ext` file in the paks (empty with no file)."""
    path = f"shaders/{shader.lower()}.ext"
    if not shader or not source.has(path):
        return {}
    text = source.read(path).decode("utf-8", "replace")
    return {int(mask, 16): name for name, mask in re.findall(r"Name\s*=\s*(%\w+)\s*\n\s*Mask\s*=\s*(0x[0-9A-Fa-f]+)", text)}


def flags_of(element: ET.Element, resources: bool = True) -> tuple[list[str], list[str]]:
    """@return (the flags of a sub-material, the flags that are in only one of the two sources). The material loader
    of the engine uses StringGenMask when the material has it, else the bits of the numeric GenMask (through the `.ext`
    of its shader). The generated materials of the merged chunks (coatgen) have a GenMask that does not agree with
    their StringGenMask (for example 80000001 for %NORMAL_MAP%BLENDLAYER%DETAIL_MAPPING...): the text is the rule.
    Old names become the current names. `resources` False: without the flags of the texture resources (resource_flags)."""
    extra = resource_flags(element) if resources else set()
    named = {FLAG_ALIASES.get(f, f) for f in FLAG.findall(element.get("StringGenMask", "").upper())}
    bits = set()
    try:
        mask = int(element.get("GenMask", "0") or "0", 16)
    except ValueError:
        mask = 0
    for bit, name in ext_flags(element.get("Shader", "")).items():
        if mask & bit:
            bits.add(FLAG_ALIASES.get(name, name))
    if element.get("StringGenMask") is None:
        return sorted(bits | extra), []
    return sorted(named | extra), sorted(named ^ bits) if bits else []


# The texture dependencies of illum.ext (DependencySet; the retail parser 0x60271D6 $TEX_Normals -> dependency 0x4 = slot 1,
# 0x602722D $TEX_Specular -> 0x10 = slot 2, 0x6027427 $TEX_Emittance -> 0x100000 = slots 7 and 13; research/lights-emission
# dependency-table.json, gen-parser.txt) [R]: a supplied texture of the slot sets the flag.
ILLUM_DEPENDENCIES = {"Bumpmap": "%NORMAL_MAP", "Specular": "%SPECULAR_MAP", "Emittance": "%EMITTANCE_MAP", "Decal": "%EMITTANCE_MAP"}


def resource_flags(element: ET.Element) -> set[str]:
    """@return The flags that the engine adds from the texture resources of a sub-material before it picks the shader permutation
    (research/lights-emission.md "Illum emission" [R]): for Illum, a Bumpmap texture enables %NORMAL_MAP, a Specular texture
    %SPECULAR_MAP, an Emittance texture or a Decal intensity texture %EMITTANCE_MAP; the text and numeric masks are only inputs
    (the coal pit jav_prp_rockcoals_mat has the two emission textures and no flag; the far meshes of the places of interest,
    %IS_POI_IMPOSTOR, have the normal, gloss, and specular atlases and no flag: AST-261). The reset side of the dependencies
    (a flag of the text with no texture of its slot: 16 mesh materials with %NORMAL_MAP and no Bumpmap) is not applied [U]."""
    if element.get("Shader", "").lower() != "illum":
        return set()
    return {ILLUM_DEPENDENCIES[t.get("Map")] for t in element.findall("Textures/Texture")
            if t.get("Map") in ILLUM_DEPENDENCIES and t.get("File", "").strip()}


def uv_rules(texture: ET.Element) -> dict:
    """@return The UV rules of a texture slot that are not the default: tiling, offset, the rotation and the
    oscillators of the TexMod element, and clamp (IsTileU/IsTileV = 0)."""
    out: dict = {}
    for mod in texture.iter("TexMod"):
        values = {k: number(v) for k, v in mod.attrib.items()}
        tile = [values.pop("TileU", 1.0), values.pop("TileV", 1.0)]
        offset = [values.pop("OffsetU", 0.0), values.pop("OffsetV", 0.0)]
        if tile != [1.0, 1.0]:
            out["tile"] = tile
        if offset != [0.0, 0.0]:
            out["offset"] = offset
        if values.get("TexMod_TexGenType", 0) == 0:
            values.pop("TexMod_TexGenType", None)
        if values.get("TexMod_bTexGenProjected", 0) == 0:
            values.pop("TexMod_bTexGenProjected", None)
        if values.get("TexMod_RotateType", 0) == 0:   # no rotation: the rotation values are not used
            for k in [k for k in values if "Rotate" in k]:
                values.pop(k)
        for axis in ("U", "V"):   # an oscillator of type 0 does not move
            if values.get(f"TexMod_{axis}OscillatorType", 0) == 0:
                for k in [k for k in values if k.startswith(f"TexMod_{axis}Oscillator")]:
                    values.pop(k)
        if values:
            out["mod"] = dict(sorted(values.items()))
    clamp = [texture.get("IsTileU", "1") == "0", texture.get("IsTileV", "1") == "0"]
    if any(clamp):
        out["clamp"] = clamp
    return out


def role_of(shader: str, slot: str, flags: set[str]) -> tuple[str | None, str]:
    """@return (the sampler role of a texture slot, the rule) for a shader and its flags; the role is None when the
    shader does not sample the slot with these flags."""
    s = shader.lower()
    blend = "%BLENDLAYER" in flags
    if s == "glass":
        table = {"Bumpmap": ("normal", "always"), "Smoothness": ("gloss", "always"),
                 "Specular": ("tint mask", "%TINT_MAP or %SPECULAR_MAP") if flags & {"%TINT_MAP", "%SPECULAR_MAP"} else (None, "no %TINT_MAP"),
                 "Custom": ("tint color", "%TINT_COLOR_MAP") if "%TINT_COLOR_MAP" in flags else (None, "no %TINT_COLOR_MAP"),
                 "Diffuse": ("dirt", "%DIRT_MAP") if "%DIRT_MAP" in flags else (None, "no %DIRT_MAP"),
                 "SecondSmoothness": ("cloud gloss", "always")}
        return table.get(slot, (None, "not a glass slot"))
    if s == "geometrybeam":   # BeamPS: the base, the dust (the $Specular sampler), the turbulence color ($NormalMap)
        table = {"Diffuse": ("diffuse", "always"),
                 "Specular": ("dust", "%NOISE") if "%NOISE" in flags else (None, "no %NOISE"),
                 "Bumpmap": ("turbulence", "%NOISE") if "%NOISE" in flags else (None, "no %NOISE")}
        return table.get(slot, (None, "not a GeometryBeam slot"))
    if slot == "Diffuse":
        return "diffuse", "always"
    if slot == "Bumpmap":
        return ("normal", "%NORMAL_MAP") if "%NORMAL_MAP" in flags else (None, "no %NORMAL_MAP")
    if slot == "Smoothness":
        return "gloss", "always (the alpha image of the normal map)"
    if slot == "Specular":
        return ("specular", "%SPECULAR_MAP") if "%SPECULAR_MAP" in flags else (None, "no %SPECULAR_MAP")
    if slot == "Detail":
        return ("detail", "%DETAIL_MAPPING") if "%DETAIL_MAPPING" in flags else (None, "no %DETAIL_MAPPING")
    if slot == "Custom":
        if blend:
            return "diffuse2", "%BLENDLAYER"
        if flags & {"%OVERLAY_MASK", "%COLOR_SAMPLER_OVERLAY_MASK"}:
            return "overlay mask", "%OVERLAY_MASK or %COLOR_SAMPLER_OVERLAY_MASK ($CustomMap)"
        return None, "no %BLENDLAYER and no overlay mask"
    if slot == "[1] Custom":
        return ("normal2", "%BLENDLAYER") if blend else (None, "no %BLENDLAYER")
    if slot == "SecondSmoothness":
        return ("gloss2", "%BLENDLAYER") if blend else (None, "no %BLENDLAYER")
    if slot == "Specular2":
        return ("specular2", "%BLENDLAYER") if blend else (None, "no %BLENDLAYER")
    if slot == "SubSurface":
        return ("height2", "%BLENDLAYER") if blend else (None, "no %BLENDLAYER")
    if slot == "Opacity":
        if s == "vegetation":
            return "transmission", "Vegetation: saturate(BackDiffuseMultiplier * Opacity.r) * TransmittanceColor"
        if blend:
            return "blend mask", "%BLENDLAYER"
        if "%DECAL" in flags:
            return "decal opacity", "%DECAL"
        if "%TRANSMITTANCE" in flags:
            return "transmission", "%TRANSMITTANCE"
        return None, "Illum: the cutout is the diffuse alpha"
    if slot == "Heightmap":
        return ("height", "POM, offset bump, or displacement") if flags & POM_FLAGS else (None, "no height flag")
    if slot == "Emittance":
        return ("emittance", "%EMITTANCE_MAP") if "%EMITTANCE_MAP" in flags else (None, "no %EMITTANCE_MAP")
    if slot == "Decal":
        if "%EMITTANCE_MAP" in flags or "%EMISSIVE_DECAL" in flags:
            return "emittance intensity", "%EMITTANCE_MAP or %EMISSIVE_DECAL"
        return None, "no emission"
    if slot == "Occlusion":
        return ("occlusion", "%OCCLUSION_MAP") if "%OCCLUSION_MAP" in flags else (None, "no %OCCLUSION_MAP")
    if slot == "Environment":
        return None, "Unreal reflections"
    return None, "unknown slot"


def family_of(shader: str) -> str:
    """@return The family of a shader: illum, vegetation, glass, beam (GeometryBeam), or other (effects, hair, skin:
    legacy rules)."""
    s = shader.lower()
    return "beam" if s == "geometrybeam" else s if s in ("illum", "vegetation", "glass") else "other"


def source_record(mtl: str, index: int, element: ET.Element | None) -> dict:
    """@return The rich record of one sub-material (`element` None: the default material of a missing slot)."""
    if element is None:
        return {"mtl": mtl, "index": index, "missing": True, "shader": "", "family": "other", "flags": [],
                "lighting": {}, "params": {}, "textures": {}}
    flags, differ = flags_of(element)
    added = sorted(set(flags) - set(flags_of(element, resources=False)[0]))
    record: dict = {
        "mtl": mtl, "index": index, "name": element.get("Name", ""), "surface": element.get("SurfaceType", ""),
        "shader": element.get("Shader", ""), "family": family_of(element.get("Shader", "")), "flags": flags,
        "genMask": element.get("GenMask", ""), "mtlFlags": int(element.get("MtlFlags", "0") or 0),
        "lighting": {k: number(element.get(k)) for k in ("Diffuse", "Specular", "Emittance", "Opacity", "AlphaTest",
                                                          "Shininess", "Emissive") if element.get(k) is not None},
        "params": {}, "textures": {}}
    if differ:
        record["flag differences"] = differ
    if added:
        record["resource flags"] = added   # the key stays the key without them (render_rules "key")
    params = element.find("PublicParams")
    if params is not None:
        record["params"] = {k: number(v) for k, v in sorted(params.attrib.items())}
    for t in element.iter("Texture"):
        slot = t.get("Map", "")
        if slot in record["textures"]:
            continue   # the first texture of a slot (as materials.shared_material)
        product = materials.texture_product(t)
        entry = {"file": t.get("File", ""), "assetId": t.get("AssetId", ""), "product": product}
        entry.update(role_fields(record["shader"], slot, set(flags)))
        uv = uv_rules(t)
        if uv:
            entry["uv"] = uv
        if product is not None and source.has(product + ".a"):
            entry["alpha image"] = True
        record["textures"][slot] = entry
    gloss = record["textures"].get("Smoothness")
    normal = record["textures"].get("Bumpmap")
    if gloss is None and normal is not None and normal.get("alpha image"):
        record["gloss from normal"] = True   # no Smoothness slot: the gloss is the alpha image of the normal map
    return record


def role_fields(shader: str, slot: str, flags: set[str]) -> dict:
    role, rule = role_of(shader, slot, flags)
    return {"role": role, "rule": rule, **({"color": COLOR[role]} if role is not None else {})}


def without_resource_flags(record: dict) -> dict:
    """@return The record as the flags of its text and numeric masks alone give it (the flags and the texture roles)."""
    flags = [f for f in record["flags"] if f not in record["resource flags"]]
    textures = {}
    for slot, t in record["textures"].items():
        entry = {k: v for k, v in t.items() if k not in ("role", "rule", "color")}
        entry.update(role_fields(record["shader"], slot, set(flags)))
        textures[slot] = entry
    return {**{k: v for k, v in record.items() if k != "resource flags"}, "flags": flags, "textures": textures}


# The TexMod oscillator types of the source (ETexModMoveType): 2 is ETMM_Constant, a scroll with no wrap. [P] The rule of the public
# CryEngine source (Lumberyard SEfResTexture::UpdateWithModifier): offset = amplitude x rate x real time, written into the texture
# matrix before the tiling, so the UV speed is amplitude x rate x tile. The retail executable is not traced: a retail video at a
# waterfall checks the speed and the direction (AST-290). The other types (1 fixed, 3 jitter, 4 pan, 5 and 6 stretch) are not
# expressed.
OSC_CONSTANT = 2
OSC_AMPLITUDE_DEFAULT = 1.0   # [P] the default of m_OscAmplitude, when the XML has no amplitude


def uv_pan(uv: dict | None) -> list[float]:
    """@return The UV speed (u, v per second, after the tiling) of the type 2 oscillators of the UV rules of a slot."""
    uv = uv or {}
    mod, tile = uv.get("mod") or {}, uv.get("tile", [1.0, 1.0])
    out = []
    for i, axis in enumerate(("U", "V")):
        if mod.get(f"TexMod_{axis}OscillatorType") == OSC_CONSTANT:
            speed = mod.get(f"TexMod_{axis}OscillatorAmplitude", OSC_AMPLITUDE_DEFAULT) * mod.get(f"TexMod_{axis}OscillatorRate", 0.0)
            out.append(round(speed * tile[i], 7))
        else:
            out.append(0.0)
    return out


# AST-290 (the decision of the orchestrator 2026-10-25, "b"): only the tilings that a retail video checked get the flow. The cliff fall
# (jav_wld_waterfall_cliff_h, tile 0.75) agreed with the [P] speed within about 10 %; the kit fall (tile 0.25 and 0.1) did not give a
# clean check (a Meshparticle twin sheet; research in work/look-1024/flow). A material whose scrolled slots (FLOW_ROLES) have a tile
# outside FLOW_TILES on a scrolled axis keeps no flow (no UVPan, no EmissionPan, no translucent liquid) until a next retail check. The
# emission slots count only for a material that draws its emission (render_rules "emittance").
FLOW_TILES = (0.75, 1.0)
FLOW_ROLES = ("diffuse", "emittance", "emittance intensity")


def flow_tested(textures: dict, emission: bool) -> bool:
    """@return True when each axis with a type 2 scroll of the slots FLOW_ROLES (textures by role; the emission slots only with
    `emission`) has a tile in FLOW_TILES."""
    low, high = FLOW_TILES
    for role in FLOW_ROLES if emission else FLOW_ROLES[:1]:
        uv = (textures.get(role) or {}).get("uv") or {}
        mod, tile = uv.get("mod") or {}, uv.get("tile", [1.0, 1.0])
        for i, axis in enumerate(("U", "V")):
            if mod.get(f"TexMod_{axis}OscillatorType") == OSC_CONSTANT and not low <= float(tile[i]) <= high:
                return False
    return True


def flow_of(record: dict) -> dict:
    """@return {"opacity": Opacity} for a flowing liquid (AST-290: the waterfalls, the fountain flows, the lava falls), else {}: an
    Illum record with Opacity < 1, no AlphaTest, and a type 2 scroll on its Diffuse slot. The source draws an Illum material with
    Opacity < 1 in the blended pass [I]; the client draws it on M_SwcSurfaceTranslucent (stage "flow"). The general rule of the
    Illum materials with Opacity < 1 and no scroll (117 sources) stays open."""
    if record.get("family") != "illum":
        return {}
    light = record["lighting"]
    opacity, cutoff = light.get("Opacity", 1.0), light.get("AlphaTest", 0.0)
    if not isinstance(opacity, float) or opacity >= 1.0 or (isinstance(cutoff, float) and cutoff > 0):
        return {}
    diffuse = record["textures"].get("Diffuse") or {}
    if not any(uv_pan(diffuse.get("uv"))):
        return {}
    emittance = light.get("Emittance")
    emission = isinstance(emittance, list) and len(emittance) >= 4 and any(emittance[:3]) and bool(emittance[3])   # as render_rules
    if not flow_tested({t.get("role"): t for t in record["textures"].values() if t.get("role")}, emission):
        return {}
    return {"opacity": opacity}


def blend_of(record: dict) -> tuple[str, float | None]:
    """@return (opaque, masked, or translucent; the cutoff) of a record, with the rules of materials.shared_material."""
    if record["shader"] == "Glass" or record["shader"].lower() == "geometrybeam":
        return "translucent", None
    if record["name"] in materials.BLENDED_ILLUM and record["family"] == "illum":   # AST-207: the alpha blended vista card
        return "translucent", None
    cutoff = record["lighting"].get("AlphaTest", 0.0)
    cutoff = cutoff if isinstance(cutoff, float) else 0.0
    return ("masked", round(cutoff, 3)) if cutoff > 0 else ("opaque", None)


def render_rules(record: dict, normal_map: bool = True) -> dict:
    """@return The render rules of a rich record: the values that change the picture (no names, no inactive slots).
    `normal_map` False: the rule of the far meshes (no normal map, terrain.impostors). A record with resource flags keeps the
    render key of the record without them ("key"): the instance keys n_RENDER are the names of the converted mesh slots."""
    if record.get("missing"):
        return {"missing": True}
    if record.get("resource flags"):
        return {**_rules(record, normal_map), "key": render_key(render_rules(without_resource_flags(record), normal_map))}
    return _rules(record, normal_map)


def _rules(record: dict, normal_map: bool) -> dict:
    flags = set(record["flags"])
    blend, cutoff = blend_of(record)
    light = record["lighting"]
    rules: dict = {"family": record["family"], "shader": record["shader"].lower(), "blend": blend,
                   "two sided": bool(record["mtlFlags"] & materials.TWO_SIDED),
                   "flags": sorted(flags & RENDER_FLAGS),
                   "diffuse": (light.get("Diffuse") or [1.0, 1.0, 1.0])[:3],
                   "specular": (light.get("Specular") or [0.04, 0.04, 0.04])[:3],
                   "shininess": light.get("Shininess", 255.0)}
    if cutoff is not None:
        rules["cutoff"] = cutoff
    if blend == "translucent":
        rules["opacity"] = light.get("Opacity", 1.0)
    emittance = light.get("Emittance")
    if isinstance(emittance, list) and len(emittance) >= 4 and any(emittance[:3]) and emittance[3]:
        rules["emittance"] = emittance[:4]
    textures = {}
    for slot, t in record["textures"].items():
        if t["role"] is None or t["product"] is None:
            continue
        if not normal_map and t["role"] in ("normal", "normal2"):
            continue
        textures[t["role"]] = {"product": t["product"], **({"uv": t["uv"]} if "uv" in t else {})}
    if "gloss" not in textures and "normal" in textures and record.get("gloss from normal"):
        textures["gloss"] = {"product": textures["normal"]["product"]}
    rules["textures"] = dict(sorted(textures.items()))
    on = {"blend": "%BLENDLAYER" in flags, "detail": "%DETAIL_MAPPING" in flags, "pom": bool(flags & POM_FLAGS),
          "decal": "%DECAL" in flags, "emittance": "emittance" in rules or "emittance" in textures,
          "mask": bool(flags & {"%OVERLAY_MASK", "%COLOR_SAMPLER_OVERLAY_MASK"}),
          "transmission": record["family"] == "vegetation" or "%TRANSMITTANCE" in flags,
          "leaves": record["family"] == "vegetation", "bending": "%DETAIL_BENDING" in flags,
          "glass": record["family"] == "glass", "beam": record["family"] == "beam"}
    params = {}
    for feature, names in PARAMS.items():
        if on[feature]:
            params.update({n: record["params"][n] for n in names if n in record["params"]})
    rules["params"] = dict(sorted(params.items()))
    flow = flow_of(record)
    if flow:
        rules["flow"] = flow
    kind = FX_SHADERS.get(rules["shader"])
    if kind is not None:   # C8: the effect shaders (not in the render key)
        fx_textures = {}
        for slot, role in FX_SLOTS[kind].items():
            t = record["textures"].get(slot)
            if t is not None and t.get("product") is not None:
                fx_textures[role] = {"product": t["product"], **({"uv": t["uv"]} if "uv" in t else {})}
        rules["fx"] = {"kind": kind, "flags": sorted(flags & FX_FLAGS), "mtlFlags": record["mtlFlags"],
                       "params": {n: record["params"][n] for n in sorted(FX_DEFAULTS[kind]) if n in record["params"]},
                       "opacity": light.get("Opacity", 1.0), "emittance": (light.get("Emittance") or [0.0, 0.0, 0.0, 0.0])[:4],
                       "textures": fx_textures}
    return rules


# The parts of the render rules that the render key leaves out: they do not rename the instance keys (the slot names of the converted
# meshes). "fx": the effect shaders (C8); "flow": the flowing liquids (AST-290, flow_of).
NOT_IN_RENDER_KEY = ("fx", "flow")


def render_key(rules: dict) -> str:
    """@return The render key of render rules: a hash of the rules without NOT_IN_RENDER_KEY (C8: the rules of the effect shaders
    do not rename the keys)."""
    if "key" in rules:
        return rules["key"]
    keyed = {k: v for k, v in rules.items() if k not in NOT_IN_RENDER_KEY}
    return "r_" + hashlib.sha1(json.dumps(keyed, sort_keys=True).encode()).hexdigest()[:10]


def legacy_key(element: ET.Element | None, normal_map: bool = True) -> str:
    """@return The key of the current material instance of a sub-material (materials.shared_material, unchanged: the
    glTF material name of the converted meshes). It reads the shared PNGs that exist (a missing one is written)."""
    return materials.shared_material(element, normal_map)[0]["name"]


# The audit (M0): all converted static meshes of the census, their source slots, and the keys.

def out_dir() -> Path:
    path = env.content_dir() / "materials"
    path.mkdir(parents=True, exist_ok=True)
    return path


def write_json(path: Path, value) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=1, sort_keys=True))
    temporary.replace(path)


@functools.lru_cache(maxsize=4096)
def sub_materials(mtl: str) -> tuple | list:
    root = ET.fromstring(source.read(mtl).decode("utf-8", "replace"))
    subs = root.find("SubMaterials")
    return tuple(subs) if subs is not None else materials.SingleMaterial([root])


def primitive_slots(mesh: str, gltf_file: Path, rule: int = 6) -> list[tuple[int, str]] | None:
    """@return The (source sub-material index, glTF material name) of each primitive of a converted mesh, in the order
    of mesh.convert (the mesh chunks, their subsets, no subset that is not drawn), or None when the primitives do not
    match the subsets (the index counts are the check). Only the chunk headers: no .cgfheap."""
    from . import cgf
    f = cgf.read_chunks(source.read(mesh))
    from . import props
    reference = props.own_reference(f, mesh)
    subs = materials.load_mtl(reference, mesh)[1] if reference else []
    order = []
    for chunk in f.of_type(cgf.T_MESH):
        _flags, _flags2, nverts, _nindices, _nsubsets, subsets_id, _va = struct.unpack_from("<7i", chunk.data, 0)
        if nverts == 0 or subsets_id not in f.by_id:
            continue
        sub = f.by_id[subsets_id].data
        _sflags, count = struct.unpack_from("<2i", sub, 0)
        for i in range(count):
            _fi, ni, _fv, _nv, mat = struct.unpack_from("<5i", sub, 16 + i * 36)
            m = materials.sub_material(subs, mat)
            if m is not None and materials.not_drawn(m, beams=rule >= 8):   # mesh rule 8 draws GeometryBeam
                continue
            order.append((mat, ni))
    doc = json.loads(gltf_file.read_text())
    names = [m["name"] for m in doc.get("materials", [])]
    prims = [p for m in doc["meshes"] for p in m["primitives"]]
    if len(prims) != len(order):
        return None
    out = []
    for (mat, ni), prim in zip(order, prims):
        if doc["accessors"][prim["indices"]]["count"] != ni:
            return None
        name = names[prim["material"]]
        if name.startswith("n_"):   # mesh rule 7 names a slot by its instance key: the legacy key of its sub-material
            name = legacy_key(materials.sub_material(subs, mat))
        out.append((mat, name))
    return out


def texture_check(product: str, alpha: bool = False) -> str | None:
    """@return None when the converter reads and decodes the texture (dds.join and dds.to_image of the small mips of
    the base file: no large split file), else the reason."""
    from . import dds
    path = product + (".a" if alpha else "")
    if not source.has(path):
        return "no alpha image" if alpha else "not in the game copy"
    try:
        data, w, h, fmt = dds.join(source.read, product, 4, alpha)
        dds.to_image(data, w, h, fmt)
    except Exception as error:
        return str(error)[:80]
    return None


def mesh_slots(item: tuple[str, str, int]) -> tuple[str, list | None, str | None]:
    """@return (mesh, its primitive slots or None, an error) for a worker process."""
    mesh, gltf_file, rule = item
    try:
        return mesh, primitive_slots(mesh, env.content_dir() / gltf_file, rule), None
    except Exception as error:
        return mesh, None, str(error)[:120]


def override_mtls() -> dict[str, list[str]]:
    """@return The material files that the region metadata names as a material override (materialOverrideAssetId of
    the meshes of the slice variants and of the static mesh components; research section 1), with their regions.
    Cached in SWC_CONTENT/materials/override-mtls.json (the game copy does not change)."""
    cache = out_dir() / "override-mtls.json"
    if cache.is_file():
        return json.loads(cache.read_text())
    import gc
    field = source.field_id("materialOverrideAssetId")
    out: dict[str, set] = {}
    folder = "sharedassets/coatlicue/newworld_vitaeeterna/regions/"
    for path in sorted(p for p in source.paks().entries if p.startswith(folder) and p.endswith("/region.slicedata")):
        roots = source.parse(source.read(path))
        for node in source.walk(roots[0]):
            if node.name != field:
                continue
            asset = source.asset_id(node)
            mtl = source.product(asset[0], asset[1], ".mtl") if asset else None
            if mtl:
                out.setdefault(mtl, set()).add(path.split("/")[-2])
        del roots
        gc.collect()
    result = {k: sorted(v) for k, v in sorted(out.items())}
    write_json(cache, result)
    return result


def slice_overrides(data: bytes) -> list[tuple[str, str]]:
    """@return (mesh, material) of each entity of a slice whose static or instanced mesh component names a material
    file: the material override of the component (research near-entity-evidence: for example the forge "off" and the lantern
    "on" materials of the chunks of Oxboro)."""
    from . import region as region_module
    out = []
    # The static mesh render node and the instanced mesh render node (the instanced mesh components of the settlement
    # slices: the lane walls, fences, gates, and poplars of Oxboro; research/settlements.md E5).
    nodes = {region_module.SMRN, source.field_id("Instanced Mesh Render Node")}
    for e in source.walk(source.parse(data)[0]):
        comps = e.child(region_module.COMPONENTS)
        if comps is None:
            continue
        for comp in comps.children:
            for f in comp.children:
                if f.name not in nodes:
                    continue
                mesh, mtls = None, []
                for node in source.walk(f):
                    path = region_module.asset_path(node) if node.value is not None else None
                    if path and path.endswith(".cgf") and mesh is None:
                        mesh = path
                    elif path and path.endswith(".mtl"):
                        mtls.append(path)
                if mesh:
                    out.extend((mesh, m) for m in mtls)
    return out


def region_overrides(name: str) -> dict[str, list[str]]:
    """@return For one region: each material override of a mesh component (slice_overrides) in its chunk slices,
    capital slices (and the slices that they spawn at the start), and distribution slices -> the meshes."""
    from . import region as region_module
    from nav_slices import read_slice
    folder = region_module.WORLD + "regions/" + name + "/"
    F = source.field_id
    paths: set[str] = set()
    if source.has(folder + "region.chunks"):
        root = source.parse(source.read(folder + "region.chunks"))[0]
        for ch in root.child(F("chunks")).children:
            if source.number(ch.child(F("chunkType"))) == 2:
                continue
            a = source.asset_id(ch.child(F("AssetId")))
            path = source.product(a[0], a[1], ".dynamicslice") if a else None
            if path:
                paths.add(path)
    for cpath in sorted(p for p in source.paks().entries
                        if p.startswith(folder + "capitals/") and p.endswith(".capitals.json")):
        for c in json.loads(source.read(cpath)).get("Capitals", []):
            aid = str(c.get("sliceAssetId", ""))
            path = source.product_of_text(aid, ".dynamicslice") if aid.startswith("{") else None
            if path is None:
                key = region_module.slice_key(c.get("sliceName", ""))
                path = next((p for p in ("slices/" + key + ".dynamicslice", key + ".dynamicslice") if source.has(p)), None)
            if path:
                paths.add(path)
    if source.has(folder + "region.distribution"):
        names = region_module.read_distribution(source.read(folder + "region.distribution"))[0]
        for n in names:
            n = n.lower()
            p = n if n.endswith(".dynamicslice") else n + ".dynamicslice"
            p = p if p.startswith("slices/") else "slices/" + p
            if source.has(p):
                paths.add(p)
    out: dict[str, set] = {}
    todo, seen = sorted(paths), set()
    while todo:
        path = todo.pop()
        if path in seen or len(seen) > 200000:
            continue
        seen.add(path)
        try:
            data = source.read(path)
            pairs = slice_overrides(data)
            spawners = read_slice(data).spawners
        except Exception:
            continue
        for mesh, mtl in pairs:
            out.setdefault(mtl, set()).add(mesh)
        for sp in spawners:
            if sp.slice is not None and sp.on_start:
                child = source.product(sp.slice[0], sp.slice[1], ".dynamicslice")
                if child and child not in seen:
                    todo.append(child)
    return {k: sorted(v) for k, v in out.items()}


def component_mtls(workers: int = 4) -> dict[str, dict]:
    """@return The material overrides of the mesh components of all regions (region_overrides): material -> its
    meshes and regions. Cached in SWC_CONTENT/materials/component-mtls.json."""
    cache = out_dir() / "component-mtls.json"
    if cache.is_file():
        return json.loads(cache.read_text())
    from concurrent.futures import ProcessPoolExecutor
    from . import region as region_module
    folder = region_module.WORLD + "regions/"
    names = sorted({p[len(folder):].split("/")[0] for p in source.paks().entries
                    if p.startswith(folder) and p.endswith("/region.chunks")})
    with ProcessPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(region_overrides, names))
    out: dict[str, dict] = {}
    for name, result in zip(names, results):
        for mtl, meshes in result.items():
            entry = out.setdefault(mtl, {"meshes": set(), "regions": set()})
            entry["meshes"].update(meshes)
            entry["regions"].add(name)
    result = {k: {"meshes": sorted(v["meshes"]), "regions": sorted(v["regions"])} for k, v in sorted(out.items())}
    write_json(cache, result)
    return result


def far_mesh_mtls() -> dict[str, set]:
    """@return The material files of the far meshes (terrain.impostors: the cells of impostors.json of each region)
    and their group keys."""
    from . import region as region_module
    out: dict[str, set] = {}
    folder = region_module.WORLD + "regions/"
    for path in sorted(p for p in source.paks().entries if p.startswith(folder) and p.endswith("impostors.json")):
        name = path[len(folder):].split("/")[0]
        for cell in json.loads(source.read(path)).get("impostors", []):
            mesh = source.product_of_text(cell.get("meshAssetID", ""), ".cgf")
            if mesh is None:
                continue
            try:
                from . import cgf
                reference = (cgf.material_guid(cgf.read_chunks(source.read(mesh))) or "").lstrip("/")
                mtl = materials.load_mtl(reference, mesh)[0] if reference else None
            except Exception:
                mtl = None
            if mtl:
                out.setdefault(mtl, set()).add(name)
    return out


def audit(workers: int = 4) -> dict:
    """Writes SWC_CONTENT/materials/: records.json (the rich records and the render rules of all source materials of
    the converted world meshes, the material overrides of the regions, and the far meshes), slots.json (the source
    slot and the material key of each primitive group of each mesh), audit.json (the collisions, the coverage of the
    material keys of the manifest, and the texture checks). @return The summary."""
    from concurrent.futures import ProcessPoolExecutor
    started = time.time()
    assets = manifest.load()["assets"]
    # The static meshes: the .cgf files and the .skin parts of the .cdf props (AST-275 C6, props.py).
    meshes = sorted((k, e["file"], e.get("version", 6)) for k, e in assets.items()
                    if e["kind"] == "static" and k.endswith((".cgf", ".skin")))
    sources: dict[str, dict] = {}           # "mtl#index" -> {"legacy", "far legacy", "render", "record", "use"}
    rules_of: dict[str, dict] = {}          # render key -> render rules
    slots: dict[str, list] = {}             # mesh -> [[source index, legacy key, render key], ...]
    failed = {}

    def add_source(mtl: str, index: int, element, use: str, normal_map: bool = True) -> dict | None:
        name = f"{mtl}#{index}"
        if element is not None and materials.not_drawn(element, beams=True):   # a beam: mesh rule 8, M_SwcBeam
            return None
        if name not in sources:
            record = source_record(mtl, index, element)
            rules = render_rules(record)
            key = render_key(rules)
            rules_of[key] = rules
            sources[name] = {"record": record, "render": key, "use": set()}
        s = sources[name]
        s["use"].add(use)
        field = "legacy" if normal_map else "far legacy"
        if field not in s:
            s[field] = legacy_key(element, normal_map)
        return s

    with ProcessPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(mesh_slots, meshes, chunksize=64))
    print(f"audit: primitive slots of {len(results)} meshes, {round(time.time() - started)} s", file=sys.stderr)
    from . import cgf
    for mesh, pairs, error in results:
        if pairs is None:
            failed[mesh] = error or "the primitives do not match the subsets"
            continue
        from . import props
        reference = props.own_reference(cgf.read_chunks(source.read(mesh)), mesh)   # a .cdf prop part: its registry .mtl (C6)
        mtl, subs = materials.load_mtl(reference, mesh) if reference else ("", [])
        out = []
        for index, name in sorted(set(pairs)):
            element = materials.sub_material(subs, index)
            s = add_source(mtl, index, element, "mesh")
            out.append([index, name, s["render"] if s else None])
        slots[mesh] = out
    print(f"audit: {len(sources)} sources of the meshes, {round(time.time() - started)} s", file=sys.stderr)
    overrides = override_mtls()
    for mtl in overrides:
        for index, element in enumerate(sub_materials(mtl)):
            add_source(mtl, index, element, "override")
    components = component_mtls(workers)
    for mtl in components:
        if not source.has(mtl):
            continue
        for index, element in enumerate(sub_materials(mtl)):
            add_source(mtl, index, element, "component override")
    # The override materials of the menu scene (AST-207: the fog sheets of the vista; content/frontend/nighthaven.json) and of
    # the creation scene (stage Q3: the light beams, the fog sheets, the lanterns of the ship; frontend/creation-scene.json).
    for name in ("nighthaven.json", "creation-scene.json"):
        scene = env.content_dir() / "frontend" / name
        if not scene.is_file():
            continue
        for mtl in sorted({m["material"] for m in json.loads(scene.read_text())["meshes"] if m.get("material")}):
            if source.has(mtl):
                for index, element in enumerate(sub_materials(mtl)):
                    add_source(mtl, index, element, "menu override")
    from . import cover_materials
    if (cover_materials.out_dir() / "descriptors.json").is_file():   # the overrides of the ground cover (T3)
        for mtl in cover_materials.cover_mtls():
            if source.has(mtl):
                for index, element in enumerate(sub_materials(mtl)):
                    add_source(mtl, index, element, "cover")
    far = far_mesh_mtls()
    for mtl in far:
        for index, element in enumerate(sub_materials(mtl)):
            add_source(mtl, index, element, "far mesh", normal_map=False)
    print(f"audit: {len(sources)} sources with the overrides and the far meshes, {round(time.time() - started)} s",
          file=sys.stderr)
    # The render keys of each legacy key (the slots of the meshes, the overrides, the far meshes).
    by_legacy: dict[str, dict[str, int]] = {}
    for mesh, out in slots.items():
        for _index, legacy, render in out:
            if render:
                by_legacy.setdefault(legacy, {}).setdefault(render, 0)
                by_legacy[legacy][render] += 1
    for s in sources.values():
        for field in ("legacy", "far legacy"):
            if s.get(field) and s["use"] != {"mesh"}:
                by_legacy.setdefault(s[field], {}).setdefault(s["render"], 0)
    shared = {k: v for k, v in by_legacy.items() if len(v) > 1}
    collapsed = []   # meshes with two source slots of one legacy key and different render rules (one Unreal slot)
    for mesh, out in slots.items():
        groups: dict[str, set] = {}
        for _index, legacy, render in out:
            groups.setdefault(legacy, set()).add(render)
        for legacy, renders in groups.items():
            if len(renders) > 1 and None not in renders:
                collapsed.append({"mesh": mesh, "legacy": legacy, "render": sorted(renders),
                                  "fields": differing_fields([rules_of[r] for r in renders]),
                                  "slots": [i for i, l, _r in out if l == legacy]})
    manifest_keys = {e["name"] for e in assets.values() if e["kind"] == "material"}
    uncovered = sorted(manifest_keys - set(by_legacy))
    # Texture checks of the active slots (the header of each product and of the alpha image of a gloss).
    checked: dict[tuple, str | None] = {}
    errors = []
    for name, s in sources.items():
        for slot, t in s["record"]["textures"].items():
            if t["role"] is None:
                continue
            if t["product"] is None:
                errors.append({"source": name, "slot": slot, "role": t["role"], "reason": "no product",
                               "file": t["file"]})
                continue
            alpha = t["role"] in ("gloss", "gloss2") and t.get("alpha image", False)
            key = (t["product"], alpha)
            if key not in checked:
                checked[key] = texture_check(*key)
            if checked[key]:
                errors.append({"source": name, "slot": slot, "role": t["role"], "reason": checked[key],
                               "product": t["product"]})
    write_json(out_dir() / "records.json", {
        "version": RECORD_VERSION,
        "sources": {k: {**v["record"], "use": sorted(v["use"]), "render": v["render"],
                        **{f: v[f] for f in ("legacy", "far legacy") if v.get(f)}} for k, v in sources.items()},
        "render rules": rules_of})
    write_json(out_dir() / "slots.json", {"version": RECORD_VERSION, "meshes": slots})
    reasons: dict[str, int] = {}
    for e in errors:
        reasons[f"{e['role']}: {e['reason']}"] = reasons.get(f"{e['role']}: {e['reason']}", 0) + 1
    fields: dict[str, int] = {}
    for c in collapsed:
        for f in c["fields"]:
            fields[f] = fields.get(f, 0) + 1
    summary = {
        "version": RECORD_VERSION, "meshes": len(meshes), "meshes with slots": len(slots),
        "meshes with no slot match": len(failed), "source materials": len(sources),
        "sources by use": {u: sum(1 for s in sources.values() if u in s["use"])
                           for u in ("mesh", "override", "component override", "far mesh")},
        "override material files": len(overrides), "component override material files": len(components),
        "far mesh material files": len(far),
        "render keys": len(rules_of), "legacy keys": len(by_legacy),
        "legacy keys with more than one render key": len(shared),
        "meshes with collapsed slots": len({c["mesh"] for c in collapsed}),
        "collapsed slot groups by field": dict(sorted(fields.items(), key=lambda kv: -kv[1])),
        "manifest material keys": len(manifest_keys), "manifest material keys with no source": len(uncovered),
        "texture checks": {"products": len(checked), "failed": sum(1 for v in checked.values() if v),
                           "slot errors by reason": reasons},
        "flag differences (StringGenMask and GenMask)": sum(1 for s in sources.values()
                                                             if s["record"].get("flag differences")),
        "seconds": round(time.time() - started, 1)}
    write_json(out_dir() / "audit.json", {**summary, "shared legacy keys": shared, "collapsed": collapsed,
                                          "no slot match": failed, "keys with no source": uncovered,
                                          "texture errors": errors})
    return summary


def differing_fields(rules: list[dict]) -> list[str]:
    """@return The render rule fields (textures by role, parameters by name) that differ between rule sets."""
    out = set()
    first = rules[0]
    for other in rules[1:]:
        for k in set(first) | set(other):
            if k == "textures":
                a, b = first.get(k, {}), other.get(k, {})
                out.update(f"texture {r}" for r in set(a) | set(b) if a.get(r) != b.get(r))
            elif k == "params":
                a, b = first.get(k, {}), other.get(k, {})
                out.update(f"param {p}" for p in set(a) | set(b) if a.get(p) != b.get(p))
            elif first.get(k) != other.get(k):
                out.add(k)
    return sorted(out)


# The instance step (M1 and later): one spec for each material key (legacy key) of the converted content. The spec
# comes from the render rules of the main source of the key (the source of the most mesh slots). The Unreal script
# Scripts/apply_materials.py sets the material instance of the key from its spec (the asset path does not change).

SPEC_VERSION = 3      # the rule of the specs (2: the surface parents of M2; 3: the n_ instances of A1)
DEPLOYED = ("m1", "m2", "m3a", "glass", "m3b", "m4", "beam", "m6", "c8")  # the stages that passed their check: instances.json (the import pipeline applies it)
# c8 (2026-10-24, AST-275, the decision of the orchestrator): the parents of Fxmeshadvancedtransp and Meshparticle (the light ray
# cards matched retail: the shaft contrast 26.2 against 25.9; no measurable GPU time).
# m6 (2026-10-20, the world track, the decision of the orchestrator): only the wind of the foliage (the static switch Wind and
# BendDetail); the cells give SwcWind to the components with receiveWind (cells version 6).
STAGES = DEPLOYED      # the stages of the specs that spec_of() makes now
LINEAR_VERSION = 1    # the rule of the one-channel PNGs (textures/m1-CAP/: the red channel, or the alpha image)
CAP = 1024            # the largest size (the cap of the base color and normal maps of stage 1.2)


def linear_texture(product: str, alpha: bool = False) -> dict:
    """Writes one channel of a texture product as a grayscale PNG (once) and @return its record: the alpha image of
    the product (a gloss: the .a parts of a normal map) or the red channel of the main image (a transmission map, a
    mask). Unreal imports it as linear data (TC_Alpha: BC4, sampled from R)."""
    suffix = ".a" if alpha else ""
    key = f"texture:{product}{suffix}@linear"
    out = env.content_dir() / "textures" / f"m{LINEAR_VERSION}-{CAP}" / (product[:-4] + suffix + ".png")
    if not out.is_file():
        from . import dds
        from PIL import Image
        data, w, h, fmt = dds.join(source.read, product, CAP, alpha)
        image = dds.to_image(data, w, h, fmt)
        if image.mode != "L":
            image = image.getchannel("R")
        out.parent.mkdir(parents=True, exist_ok=True)
        temporary = out.with_name(f".{out.name}.tmp.png")
        image.save(temporary)
        temporary.replace(out)
    return {"asset": key, "file": str(out.relative_to(env.content_dir())), "color": "linear", "cap": CAP}


# The DXGI formats with an sRGB view, and the sRGB flag of the game's DDS header (the second reserved word; for example
# 0x200800 for a BC1_UNORM_SRGB diffuse, 0x200000 for a BC7_UNORM detail map). The two agree in all 4,478 files of
# the sRGB roles.
SRGB_FORMATS = {29, 72, 75, 78, 91, 93, 99}
SRGB_FLAG = 0x800


@functools.lru_cache(maxsize=16384)
def file_srgb(product: str) -> bool:
    """@return True when a texture file is sRGB (an _SRGB DXGI format, or the sRGB flag of a legacy header). A sampler
    of the source with sRGBLookup = true decodes sRGB only for such a file; the other files are read as they are."""
    head = source.read(product)[:148]
    if head[84:88] == b"DX10":
        return struct.unpack_from("<I", head, 128)[0] in SRGB_FORMATS
    return bool(struct.unpack_from("<I", head, 36)[0] & SRGB_FLAG)


def data_texture(product: str, decode: bool = False, raw_rg: bool = False) -> dict:
    """Writes all channels of a texture product as an RGBA PNG for linear data (once) and @return its record: an
    overlay mask (the Custom slot with %OVERLAY_MASK or %COLOR_SAMPLER_OVERLAY_MASK), a detail map. Unreal imports it
    as linear data with alpha (TC_Masks). `decode`: the file is sRGB and the source sampler decodes it (sRGBLookup):
    the PNG has the decoded RGB values (the alpha stays). `raw_rg`: a two-channel file (BC5) as the shader samples it: R and
    G as stored, B 0, A 1 (the decoder of a normal map rebuilds B as the normal Z; research beamsimple.md: the turbulence map of
    GeometryBeamSimple reads the raw R and G with B zero)."""
    suffix = "-decoded" if decode else "-rg" if raw_rg else ""
    key = f"texture:{product}@data" + suffix
    out = env.content_dir() / "textures" / (f"d{LINEAR_VERSION}-{CAP}" + suffix) / (product[:-4] + ".png")
    if not out.is_file():
        from . import dds
        data, w, h, fmt = dds.join(source.read, product, CAP)
        image = dds.to_image(data, w, h, fmt).convert("RGBA")
        if decode:
            import numpy as np
            from PIL import Image
            a = np.asarray(image).astype(np.float64) / 255.0
            rgb = a[..., :3]
            a[..., :3] = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
            image = Image.fromarray(np.round(a * 255.0).astype(np.uint8), "RGBA")
        if raw_rg:
            import numpy as np
            from PIL import Image
            a = np.asarray(image).copy()
            a[..., 2] = 0
            a[..., 3] = 255
            image = Image.fromarray(a, "RGBA")
        out.parent.mkdir(parents=True, exist_ok=True)
        temporary = out.with_name(f".{out.name}.tmp.png")
        image.save(temporary)
        temporary.replace(out)
    return {"asset": key, "file": str(out.relative_to(env.content_dir())), "color": "data", "cap": CAP}


def color_texture(product: str, role: str) -> dict:
    """@return The record of a shared texture of stage 1.2 (materials.shared_texture: the PNG is written once): the
    base color with its own alpha (srgb), or a normal map."""
    record = materials.shared_texture(product, role)
    return {"asset": record["asset"], "file": record["file"], "color": "normal" if role == "normal" else "srgb",
            "cap": record["cap"]}


def luminance(rgb: list[float]) -> float:
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def spec_of(rules: dict, legacy: dict, textures: dict) -> dict | None:
    """@return The instance spec of a material key from the render rules of its main source (None: no change of the
    instance of stage 1.2). M1: the foliage parent for the Vegetation shader (the diffuse alpha is the cutout, the
    Opacity map is the transmission), and the base color with its own alpha for each other masked material whose
    base color had the Opacity map as its alpha. `legacy` is the manifest record of the key; `textures` collects
    the texture records."""
    if rules.get("missing"):
        return None

    def use(record: dict) -> str:
        textures[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
        return record["asset"]

    if "fx" in rules and rules["fx"]["kind"] == "beam simple":
        if "beamsimple" in STAGES:
            return beam_simple_spec(rules, use)
    elif "fx" in rules and "c8" in STAGES:
        return fxmesh_spec(rules, use) if rules["fx"]["kind"] == "fxmesh" else meshparticle_spec(rules, use)
    if rules["family"] == "glass" and "glass" in STAGES:
        return glass_spec(rules, use)
    if rules["family"] == "beam" and "beam" in STAGES:
        return beam_spec(rules, use)
    if rules["family"] == "illum" and rules.get("flow") and "flow" in STAGES and rules["blend"] == "opaque":
        # AST-290: a flowing liquid (flow_of) is blended: M_SwcSurfaceTranslucent, opacity = the diffuse alpha x the XML Opacity.
        # Its meshes get no Nanite (nanite.py: the parent "surface translucent").
        spec = surface_spec(rules, use)
        spec["parent"] = "surface translucent"
        spec["scalars"]["Opacity"] = float(rules["flow"]["opacity"])
        return spec
    if rules["family"] == "illum" and rules["blend"] == "translucent" and "m4" in STAGES:
        # An alpha blended Illum material (materials.BLENDED_ILLUM): M_SwcSurfaceTranslucent, opacity = the diffuse alpha x
        # the XML Opacity.
        spec = surface_spec(rules, use)
        spec["parent"] = "surface translucent"
        spec["scalars"]["Opacity"] = float(rules.get("opacity", 1.0))
        return spec
    if rules["blend"] not in ("masked", "opaque"):
        return None
    if rules["blend"] == "opaque" and not (rules["family"] == "illum" and "m2" in STAGES):
        return None
    maps = rules["textures"]

    if rules["family"] == "vegetation":
        spec = {"parent": "foliage", "textures": {}, "vectors": {}, "scalars": {},
                "two sided": rules["two sided"], "cutoff": rules["cutoff"]}
        if "diffuse" in maps:
            spec["textures"]["BaseColor"] = use(color_texture(maps["diffuse"]["product"], "base"))
        if "normal" in maps:
            spec["textures"]["Normal"] = use(color_texture(maps["normal"]["product"], "normal"))
        if "gloss" in maps:
            product = maps["gloss"]["product"]
            spec["textures"]["Gloss"] = use(linear_texture(product, alpha=source.has(product + ".a")))
        if "transmission" in maps:
            spec["textures"]["Transmission"] = use(linear_texture(maps["transmission"]["product"]))
        params = rules["params"]
        # The source values (research section 2, "Cutout and transmitted light"; section 3, "Source equations").
        spec["vectors"]["DiffuseColor"] = rules["diffuse"] + [1.0]
        # The shader default of TransmittanceColor is (1, 1, 0.6) (research/foliage-specular-blend.md Part A [R]).
        spec["vectors"]["TransmittanceColor"] = (params.get("TransmittanceColor") or [1.0, 1.0, 0.6])[:3] + [1.0]
        spec["scalars"]["BackDiffuseMultiplier"] = float(params.get("BackDiffuseMultiplier", 1.0))
        spec["scalars"]["Smoothness"] = max(1 / 127, float(rules["shininess"]) / 255)
        spec["scalars"]["Specular"] = min(1.0, luminance(rules["specular"]) / 0.08)
        # Part A [R]: the leaf normal of the source, N = normalize(N0 + NormalViewDependency x V), only for %LEAVES or %GRASS
        # (vegetation@common_zpassps; default 0.5), and the specular map (%SPECULAR_MAP): F0 = XML Specular x the map (the parent:
        # Specular x the luminance of the map; a white fallback).
        flags = set(rules["flags"])
        if flags & {"%LEAVES", "%GRASS"}:
            nvd = params.get("NormalViewDependency", 0.5)
            spec["scalars"]["NormalViewDependency"] = float(nvd[0] if isinstance(nvd, list) else nvd)
        if "%SPECULAR_MAP" in flags and "specular" in maps:
            spec["textures"]["SpecularMap"] = use(color_texture(maps["specular"]["product"], "base"))
        if "m6" in STAGES and "%DETAIL_BENDING" in rules["flags"]:
            # M6: the detail bending of the source (modificatorvt.cfib; the defaults of its parameters: 1, 0.2, -0.5).
            def bend(name: str, default: float) -> float:
                v = params.get(name, default)
                return float(v[0] if isinstance(v, list) else v)
            spec["vectors"]["BendDetail"] = [bend("bendDetailFrequency", 1.0), bend("bendDetailLeafAmplitude", 0.2),
                                             bend("bendDetailBranchAmplitude", -0.5), 0.0]
        if "m6" in STAGES:
            spec["switches"] = {"Wind": True}   # the main bending of all vegetation; the detail bending with BendDetail
        return spec
    if rules["family"] == "illum" and "m2" in STAGES:
        return surface_spec(rules, use)
    if "+opacity:" in str(legacy.get("base", "")) and "diffuse" in maps:
        return {"parent": None, "textures": {"BaseColor": use(color_texture(maps["diffuse"]["product"], "base"))}}
    return None


def uv_transform(uv: dict | None) -> list[float]:
    """@return UVTransform (tile u, tile v, offset u, offset v) of the UV rules of a slot: the source TexMod tiling and
    offset. The rotation and the oscillators are not in the parents (a limit, counted by instances())."""
    uv = uv or {}
    return list(uv.get("tile", [1.0, 1.0])) + list(uv.get("offset", [0.0, 0.0]))


def surface_spec(rules: dict, use) -> dict:
    """@return The spec of an Illum material (stage M2): M_SwcSurface or M_SwcSurfaceMasked with the diffuse map and
    the XML Diffuse, the specular map and the XML Specular (F0), the gloss (the alpha image of the normal map) and
    Smoothness = max(1/127, Shininess/255), the normal map, and the UV transform of the diffuse slot."""
    maps = rules["textures"]
    spec = {"parent": "surface masked" if rules["blend"] == "masked" else "surface", "textures": {}, "vectors": {},
            "scalars": {}, "two sided": rules["two sided"]}
    if rules["blend"] == "masked":
        spec["cutoff"] = rules["cutoff"]
    if "diffuse" in maps:
        spec["textures"]["BaseColor"] = use(color_texture(maps["diffuse"]["product"], "base"))
    if "normal" in maps:
        spec["textures"]["Normal"] = use(color_texture(maps["normal"]["product"], "normal"))
    if "gloss" in maps:
        product = maps["gloss"]["product"]
        spec["textures"]["Gloss"] = use(linear_texture(product, alpha=source.has(product + ".a")))
    if "specular" in maps:
        spec["textures"]["Specular"] = use(color_texture(maps["specular"]["product"], "base"))
    spec["vectors"]["DiffuseColor"] = rules["diffuse"] + [1.0]
    spec["vectors"]["SpecularColor"] = rules["specular"] + [1.0]
    spec["vectors"]["UVTransform"] = uv_transform(maps.get("diffuse", {}).get("uv"))
    if "flow" in STAGES and any(uv_pan(maps.get("diffuse", {}).get("uv"))) and flow_tested(maps, bool(rules.get("emittance"))):
        # AST-290: the scroll of the base UV (the type 2 oscillators of the Diffuse slot; the surface parents add frac(Time x UVPan.rg)).
        spec["vectors"]["UVPan"] = uv_pan(maps["diffuse"]["uv"]) + [0.0, 0.0]
    spec["scalars"]["Smoothness"] = max(1 / 127, float(rules["shininess"]) / 255)
    flags = set(rules["flags"])
    if "m3b" in STAGES:
        m3b(rules, spec, use)
    if "m4" in STAGES:
        m4(rules, spec, use)
    if "overlay mask" in maps and "m3a" in STAGES and flags & {"%OVERLAY_MASK", "%COLOR_SAMPLER_OVERLAY_MASK"}:
        # M3a (M3 part 1): the overlay masks (swc_materials.MASK_HLSL). %OVERLAY_MASK wins when both flags are set.
        params = rules["params"]

        def value(name: str, default: float = 0.0) -> float:
            v = params.get(name, default)
            return float(v[0] if isinstance(v, list) else v)

        def color(name: str) -> list[float]:
            v = params.get(name, [0.0, 0.0, 0.0])
            return (list(v) + [0.0, 0.0, 0.0])[:3] + [0.0] if isinstance(v, list) else [float(v)] * 3 + [0.0]

        spec["textures"]["MaskTex"] = use(data_texture(maps["overlay mask"]["product"]))
        spec["scalars"]["MaskMode"] = 1.0 if "%OVERLAY_MASK" in flags else 2.0
        spec["vectors"]["MaskColorR"] = color("Mask_R_Color")
        spec["vectors"]["MaskColorG"] = color("Mask_G_Color")
        spec["vectors"]["MaskColorB"] = color("Mask_B_Color")
        spec["vectors"]["MaskSpecColor"] = color("Mask_A_SpecColor")
        spec["vectors"]["MaskAmount"] = [value("Mask_R"), value("Mask_G"), value("Mask_B"), value("Mask_A_Gloss")]
        spec["vectors"]["MaskOverride"] = [value("Mask_R_Override"), value("Mask_G_Override"),
                                           value("Mask_B_Override"), value("Mask_A_SpecColor_Override")]
        spec["vectors"]["MaskExtra"] = [value("Mask_A_GlossShift"), value("ColorMaskStrength"),
                                        value("ColorMaskOverride"), value("ColorMaskAlphaInf")]
    return spec


HAIR_PRIMARY_CAP = 0.045      # the primary highlight color of the source Hair: min(S x XML Specular, 0.045) [R]
UE_HAIR_F0 = 0.04652          # the fiber F0 of the Unreal Hair BSDF (HairBsdf.ush)


def hair_spec(record: dict, rules: dict, use) -> dict:
    """@return The spec of a Hair sub-material (research hair-shader.md section 5, the first stage: the masked parent M_SwcHair
    with the Unreal Hair shading model, not the surface F0 split). Kept from the source: D = the diffuse map x XML Diffuse
    (x the vertex color), the overlay masks on D and S (mask A gloss does not change the lobes), S = the specular map (sampled
    with no %SPECULAR_MAP), XML Specular (the primary color), the material gloss clamped to 0.5 ... 0.9 (not the gloss image),
    the secondary color, width, and shift, the shift variation, the diffuse wrap, the direction map (%DIRECTION_MAP: the
    Detail slot, linear XY), the alpha controls (AlphaTest, AlphaBlendMultiplier, XML Opacity), and the variants %SOLID_HAIR
    and %THIN_HAIR. The Unreal inputs [I]: Specular = 0.5 x lum(min(S x XML Specular, 0.045)) / 0.04652 (in the parent),
    Roughness = (4 / n1)^0.25 with n1 = 2^(10 g + 1), Scatter 1, Backlit 0, the tangent along the strand (the binormal, or the
    direction map)."""
    maps, params, flags = record["textures"], record["params"], set(record["flags"])

    def value(name: str, default: float) -> float:
        v = params.get(name, default)
        return float(v[0] if isinstance(v, list) else v)

    def color(name: str, default: list[float]) -> list[float]:
        v = params.get(name, default)
        v = list(v) if isinstance(v, list) else [float(v)] * 3
        return (v + [0.0, 0.0, 0.0])[:3] + [1.0]

    spec = {"parent": "hair", "textures": {}, "vectors": {}, "scalars": {}, "switches": {}, "two sided": True}
    diffuse = maps.get("Diffuse") or {}
    if diffuse.get("product"):
        spec["textures"]["BaseColor"] = use(color_texture(diffuse["product"], "base"))
    specular = maps.get("Specular") or {}
    if specular.get("product"):
        spec["textures"]["Specular"] = use(color_texture(specular["product"], "base"))
    normal = rules["textures"].get("normal")
    if normal:
        spec["textures"]["Normal"] = use(color_texture(normal["product"], "normal"))
    detail = maps.get("Detail") or {}
    if "%DIRECTION_MAP" in flags and detail.get("product"):
        spec["textures"]["Direction"] = use(data_texture(detail["product"]))
        spec["switches"]["DirectionMap"] = True
    light = record["lighting"]
    spec["vectors"]["DiffuseColor"] = (light.get("Diffuse") or [1.0, 1.0, 1.0])[:3] + [1.0]
    spec["vectors"]["SpecularColor"] = (light.get("Specular") or [1.0, 1.0, 1.0])[:3] + [1.0]
    spec["vectors"]["SecondaryColor"] = color("SecondaryHighlightColor", [0.7, 0.7, 0.7])
    spec["vectors"]["UVTransform"] = uv_transform(diffuse.get("uv"))
    gloss = min(max(max(1 / 127, float(light.get("Shininess", 255.0)) / 255), 0.5), 0.9)
    n1 = 2.0 ** (10.0 * gloss + 1.0)
    spec["scalars"]["Gloss"] = gloss
    spec["scalars"]["HairRoughness"] = (4.0 / n1) ** 0.25
    spec["scalars"]["SecondaryWidth"] = value("SecondaryHighlightWidth", 1.5)
    spec["scalars"]["SecondaryShift"] = value("SecondaryHighlightShiftAmount", 0.1)
    spec["scalars"]["ShiftVariation"] = value("ShiftVariation", 0.0)
    spec["scalars"]["DiffuseWrap"] = value("DiffuseWrap", 0.5)
    spec["scalars"]["Scatter"] = 1.0
    # The coverage (section 4): the core where the raw alpha >= AlphaTest; with the blend passes (not %SOLID_HAIR) a dithered
    # edge where the alpha >= 0.05, by the blend opacity saturate(a x AlphaBlendMultiplier) x XML Opacity [I: one masked draw].
    spec["scalars"]["AlphaTest"] = float(light.get("AlphaTest", 0.0) or 0.0)
    spec["scalars"]["AlphaBlendMultiplier"] = value("AlphaBlendMultiplier", 1.0)
    spec["scalars"]["Opacity"] = float(light.get("Opacity", 1.0))
    spec["scalars"]["BlendEdge"] = 0.0 if "%SOLID_HAIR" in flags else 1.0
    if "%VERTCOLORS" in flags:
        spec["scalars"]["UseVertexColor"] = 1.0
    mask_map = maps.get("Custom") or {}
    if flags & {"%OVERLAY_MASK", "%COLOR_SAMPLER_OVERLAY_MASK"} and mask_map.get("product"):
        spec["textures"]["MaskTex"] = use(data_texture(mask_map["product"]))
        spec["scalars"]["MaskMode"] = 1.0 if "%OVERLAY_MASK" in flags else 2.0
        mask_color = lambda name: color(name, [0.0, 0.0, 0.0])[:3] + [0.0]
        spec["vectors"]["MaskColorR"] = mask_color("Mask_R_Color")
        spec["vectors"]["MaskColorG"] = mask_color("Mask_G_Color")
        spec["vectors"]["MaskColorB"] = mask_color("Mask_B_Color")
        spec["vectors"]["MaskSpecColor"] = mask_color("Mask_A_SpecColor")
        spec["vectors"]["MaskAmount"] = [value("Mask_R", 0.0), value("Mask_G", 0.0), value("Mask_B", 0.0), 0.0]
        spec["vectors"]["MaskOverride"] = [value("Mask_R_Override", 0.0), value("Mask_G_Override", 0.0),
                                           value("Mask_B_Override", 0.0), value("Mask_A_SpecColor_Override", 0.0)]
        spec["vectors"]["MaskExtra"] = [0.0, value("ColorMaskStrength", 0.0), value("ColorMaskOverride", 0.0),
                                        value("ColorMaskAlphaInf", 0.0)]
    if not spec["switches"]:
        del spec["switches"]
    return spec


def m3b(rules: dict, spec: dict, use) -> None:
    """Adds M3b to a surface spec (swc_materials.surface_parent version 4): the vertex colors (%VERTCOLORS: the albedo
    x the vertex RGB), the blend layer (%BLENDLAYER: the second diffuse, normal, gloss, specular, the blend mask, the
    blend factor and falloff, the tiling), the detail map (%DETAIL_MAPPING). Defaults: the shader scripts of the source
    (blendanddissolve.cfib, illum.cfxb)."""
    maps, params, flags = rules["textures"], rules["params"], set(rules["flags"])

    def value(name: str, default: float) -> float:
        v = params.get(name, default)
        return float(v[0] if isinstance(v, list) else v)

    def color(name: str, default: list[float]) -> list[float]:
        v = params.get(name, default)
        v = list(v) if isinstance(v, list) else [float(v)] * 3
        return (v + [0.0, 0.0, 0.0])[:3] + [1.0]

    if "%VERTCOLORS" in flags:
        spec["scalars"]["UseVertexColor"] = 1.0
    switches = {}
    if "%BLENDLAYER" in flags and "diffuse2" in maps:
        switches["Blend"] = True
        spec["textures"]["Diffuse2"] = use(color_texture(maps["diffuse2"]["product"], "base"))
        if "normal2" in maps:
            spec["textures"]["Normal2"] = use(color_texture(maps["normal2"]["product"], "normal"))
        gloss2 = maps.get("gloss2") or ({"product": maps["normal2"]["product"]} if "normal2" in maps
                                        and source.has(maps["normal2"]["product"] + ".a") else None)
        if gloss2:
            spec["textures"]["Gloss2"] = use(linear_texture(gloss2["product"], alpha=source.has(gloss2["product"] + ".a")))
        if "specular2" in maps:
            spec["textures"]["Specular2"] = use(data_texture(maps["specular2"]["product"]))
        if "blend mask" in maps:
            spec["textures"]["BlendMask"] = use(linear_texture(maps["blend mask"]["product"]))
        spec["vectors"]["BlendParams"] = [value("BlendFactor", 0.5), value("BlendFalloff", 32.0), 0.0,
                                          1.0 if "%BLENDLAYER_IGNORE_VERTEX_ALPHA" in flags else 0.0]
        spec["vectors"]["BlendLayer2Diffuse"] = color("BlendLayer2Diffuse", [1.0, 1.0, 1.0])
        spec["vectors"]["BlendLayer2Specular"] = color("BlendLayer2Specular", [0.2314, 0.2314, 0.2314])
        spec["scalars"]["BlendLayer2Smoothness"] = value("BlendLayer2Smoothness", 10.0) / 256.0
        spec["scalars"]["BlendLayer2Tiling"] = value("BlendLayer2Tiling", 1.0)
        spec["scalars"]["BlendMaskTiling"] = value("BlendMaskTiling", 1.0)
        # The layer 2 UV (research/foliage-specular-blend.md Part B [R, D]): the TexMod tiling and offset of the Custom slot on the
        # raw UV0 (not the layer-1 transform), times BlendLayer2Tiling; the parents also sample the mask at UV0 x BlendMaskTiling.
        spec["vectors"]["Layer2Transform"] = uv_transform(maps["diffuse2"].get("uv"))
    if "%DETAIL_MAPPING" in flags and "detail" in maps:
        switches["Detail"] = True
        # The source detail sampler decodes sRGB only for an sRGB file (383 of the 385 detail maps are linear).
        product = maps["detail"]["product"]
        spec["textures"]["Detail"] = use(data_texture(product, decode=file_srgb(product)))
        spec["vectors"]["DetailTransform"] = uv_transform(maps["detail"].get("uv"))
        spec["vectors"]["DetailScales"] = [value("DetailBumpScale", 0.5), value("DetailDiffuseScale", 0.5),
                                           value("DetailGlossScale", 0.5),
                                           1.0 if "%ALPHAMASK_DETAILMAP" in flags else 0.0]
    if switches:
        spec["switches"] = switches


def m4(rules: dict, spec: dict, use) -> None:
    """Adds M4 to a surface spec (swc_materials.EMISSIVE_HLSL, surface parent version 7): the Emittance RGB and its
    fourth value (a multiplier, not an opacity), the emittance map and the intensity map of the Decal slot, each with its
    own UV transform (the source _TCMMatrixEmittance and _TCMMatrixEmissiveMultiplier), EmittanceMapGamma. Only when the
    Emittance RGB and A are not zero. The color space of the two maps is the color space of the file (AST-207): the
    source samples them through the default view of the texture, which is sRGB for an sRGB file (the flag 0x800 or an
    _SRGB format, file_srgb), also with no sRGBLookup. An sRGB file is an sRGB texture (the shared base texture: the GPU
    decodes the RGB, the alpha stays linear) on the Color samplers EmittanceSrgb and IntensitySrgb (EmittanceParams z and
    w = 1); a linear file is linear data (`@data`) on Emittance and EmissiveIntensity. A decoded PNG as data (`@data-decoded`)
    lost the dark values: the BC compression of near-black data gave a green cast."""
    emittance = rules.get("emittance")
    if not emittance:
        return
    maps = rules["textures"]
    spec.setdefault("switches", {})["Emissive"] = True
    spec["vectors"]["EmittanceColor"] = list(emittance[:4])
    has_map = "emittance" in maps
    srgb = [0.0, 0.0]
    for i, (role, parameter, srgb_parameter, transform) in enumerate((
            ("emittance", "Emittance", "EmittanceSrgb", "EmittanceTransform"),
            ("emittance intensity", "EmissiveIntensity", "IntensitySrgb", "IntensityTransform"))):
        if role in maps:
            product = maps[role]["product"]
            if file_srgb(product):
                spec["textures"][srgb_parameter] = use(color_texture(product, "base"))
                srgb[i] = 1.0
            else:
                spec["textures"][parameter] = use(data_texture(product))
            spec["vectors"][transform] = uv_transform(maps[role].get("uv"))
    if "flow" in STAGES:
        # AST-290: the scroll of the emittance map and of the intensity map (the moving glow of the ghosts, the corrupted, the lava).
        pan = uv_pan(maps.get("emittance", {}).get("uv")) + uv_pan(maps.get("emittance intensity", {}).get("uv"))
        if any(pan) and flow_tested(maps, True):
            spec["vectors"]["EmissionPan"] = pan
    gamma = rules["params"].get("EmittanceMapGamma", 1.0)
    spec["vectors"]["EmittanceParams"] = [float(gamma[0] if isinstance(gamma, list) else gamma), 1.0 if has_map else 0.0,
                                          *srgb]


def glass_spec(rules: dict, use) -> dict:
    """@return The spec of a Glass material (stage M4, swc_materials.glass_parent): TintColor, TintCloudiness, the tint
    color map (Custom), the tint mask (Specular: %TINT_MAP with %SPECULAR_MAP), the dirt (Diffuse with %DIRT_MAP and the
    XML Diffuse), the XML Specular (F0), the gloss and Smoothness, the normal map and BumpMapTile."""
    maps, params, flags = rules["textures"], rules["params"], set(rules["flags"])

    def value(name: str, default: float) -> float:
        v = params.get(name, default)
        return float(v[0] if isinstance(v, list) else v)

    spec = {"parent": "glass", "textures": {}, "vectors": {}, "scalars": {}, "two sided": rules["two sided"]}
    if "normal" in maps:
        spec["textures"]["Normal"] = use(color_texture(maps["normal"]["product"], "normal"))
    if "gloss" in maps:
        product = maps["gloss"]["product"]
        spec["textures"]["Gloss"] = use(linear_texture(product, alpha=source.has(product + ".a")))
    if "tint color" in maps:
        spec["textures"]["TintColorMap"] = use(color_texture(maps["tint color"]["product"], "base"))
    tint_mask = "tint mask" in maps and "%SPECULAR_MAP" in flags   # the source reads it only with %SPECULAR_MAP
    if tint_mask:
        spec["textures"]["TintMask"] = use(color_texture(maps["tint mask"]["product"], "base"))
    if "dirt" in maps and "%DIRT_MAP" in flags:
        spec["textures"]["Dirt"] = use(color_texture(maps["dirt"]["product"], "base"))
    tint = params.get("TintColor", [1.0, 1.0, 1.0])
    tint = list(tint) if isinstance(tint, list) else [float(tint)] * 3
    spec["vectors"]["TintColor"] = (tint + [1.0, 1.0, 1.0])[:3] + [1.0]
    spec["vectors"]["SpecularColor"] = rules["specular"] + [1.0]
    spec["vectors"]["DiffuseColor"] = rules["diffuse"] + [1.0]
    spec["vectors"]["GlassFlags"] = [1.0 if tint_mask and "%TINT_MAP" in flags else 0.0, 1.0 if tint_mask else 0.0,
                                     1.0 if "%DIRT_MAP" in flags and "dirt" in maps else 0.0, 0.0]
    spec["scalars"]["TintCloudiness"] = value("TintCloudiness", 0.0)
    spec["scalars"]["CloudinessReducesGloss"] = value("CloudinessReducesGloss", 0.0)
    spec["scalars"]["BumpMapTile"] = value("BumpMapTile", 1.0)
    spec["scalars"]["Smoothness"] = max(1 / 127, float(rules["shininess"]) / 255)
    return spec


# The defaults of the GeometryBeam parameters (the shader of a material with no value; most materials set them all).
BEAM_DEFAULTS = {"StartColor": [1.0, 1.0, 1.0, 1.0], "EndColor": [1.0, 1.0, 1.0, 1.0], "FinalMultiplier": 1.0,
                 "SoftIntersectionFactor": 1.0, "viewDependencyFactor": 2.0, "fadingFeaturing": 0.55, "baseUVScale": 1.0,
                 "dustUVScale": 0.6, "dustTimeScale": 1.0, "turbStrength": 1.0, "turbRatio": 0.55, "uvRot": 0.0,
                 "BackgroudAlphaNoiseSpeedX": 0.1, "BackgroudAlphaNoiseSpeedY": 0.05, "BackgroudAlphaNoiseSpeedZ": 0.15,
                 "SunColorInfluence": 0.5, "FogColorInfluence": 0.0, "FadeOutDistance": 0.2, "uvVigFeaturing": 0.0,
                 "AmbStrength": 0.12}


NOISE_VOLUME = "engineassets/textures/noise3d.dds"   # the Noise3D of GeometryBeam: 64 x 64 x 64, A8 (the first mip)


def noise_picture() -> Path:
    """Writes SWC_CONTENT/materials/base/T_SwcNoise3D.png (once): the 64 slices of the source noise volume in a square of
    8 x 8 tiles (512 x 512, gray; the slice z at the tile (z mod 8, z div 8)). swc_materials.beam_parent imports it with
    no mips and reads it with Load: BEAM_NOISE_HLSL makes the trilinear sample with wrap of the source sampler."""
    out = out_dir() / "base" / "T_SwcNoise3D.png"
    if not out.is_file():
        import numpy as np
        from PIL import Image
        data = source.read(NOISE_VOLUME)
        flags, size = struct.unpack_from("<I", data, 80)[0], struct.unpack_from("<I", data, 88)[0]
        height, width, depth = struct.unpack_from("<I", data, 12)[0], struct.unpack_from("<I", data, 16)[0], \
            struct.unpack_from("<I", data, 24)[0]
        if (flags, size, width, height, depth) != (2, 8, 64, 64, 64):
            raise ValueError(f"{NOISE_VOLUME}: not a 64^3 A8 volume")
        volume = np.frombuffer(data, np.uint8, 64 ** 3, 128).reshape(64, 64, 64)   # z, y, x
        sheet = volume.reshape(8, 8, 64, 64).transpose(0, 2, 1, 3).reshape(512, 512)
        out.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(sheet, "L").save(out)
    return out


def beam_spec(rules: dict, use) -> dict:
    """@return The spec of a GeometryBeam material (AST-207, swc_materials.beam_parent: Additive, Unlit, two sided): the
    base map (Diffuse, sRGB) with the oscillators of its TexMod, the dust map (Specular, sRGB), the turbulence color
    (Bumpmap; none: the flat normal default), the XML Diffuse and Opacity, the public parameters (BEAM_DEFAULTS for a
    missing one; the main PublicParams, not the MaterialLayers entry), and the flags of the shader (BeamFlags: %NOISE,
    %APPLY_SUN_COLOR, %ENABLE_FADEOUT, %VERTICAL_GRADIENT; BeamFlags2: %UV_VIGNETTING, %GRADIENT_ALPHA, %APPLY_FOG_COLOR).
    Limits: %RECEIVE_SHADOWS (the shadow samples) and the vertex deformation wave are not in the parent."""
    maps, params, flags = rules["textures"], rules["params"], set(rules["flags"])

    def value(name: str):
        v = params.get(name, BEAM_DEFAULTS[name])
        if isinstance(BEAM_DEFAULTS[name], list):
            v = list(v) if isinstance(v, list) else [float(v)] * 3
            return (v + [1.0, 1.0, 1.0, 1.0])[:4]
        return float(v[0] if isinstance(v, list) else v)

    spec = {"parent": "beam", "textures": {}, "vectors": {}, "scalars": {}, "two sided": True}
    if "diffuse" in maps:
        spec["textures"]["BaseColor"] = use(color_texture(maps["diffuse"]["product"], "base"))
    if "dust" in maps:
        spec["textures"]["Dust"] = use(color_texture(maps["dust"]["product"], "base"))
    if "turbulence" in maps:
        product = maps["turbulence"]["product"]
        spec["textures"]["Turbulence"] = use(data_texture(product, decode=file_srgb(product)))
    mod = (maps.get("diffuse", {}).get("uv") or {}).get("mod", {})
    # The oscillators of the base map: the type, the rate, and the amplitude of U and V (type 5: the stretch).
    spec["vectors"]["BaseOscillator"] = [float(mod.get("TexMod_UOscillatorRate", 0.0)) if mod.get("TexMod_UOscillatorType") == 5 else 0.0,
                                         float(mod.get("TexMod_VOscillatorRate", 0.0)) if mod.get("TexMod_VOscillatorType") == 5 else 0.0,
                                         float(mod.get("TexMod_UOscillatorAmplitude", 0.0)) if mod.get("TexMod_UOscillatorType") == 5 else 0.0,
                                         float(mod.get("TexMod_VOscillatorAmplitude", 0.0)) if mod.get("TexMod_VOscillatorType") == 5 else 0.0]
    spec["vectors"]["UVTransform"] = uv_transform(maps.get("diffuse", {}).get("uv"))
    spec["vectors"]["DiffuseColor"] = rules["diffuse"] + [float(rules.get("opacity", 1.0))]
    spec["vectors"]["StartColor"] = value("StartColor")
    spec["vectors"]["EndColor"] = value("EndColor")
    spec["vectors"]["NoiseSpeed"] = [value("BackgroudAlphaNoiseSpeedX"), value("BackgroudAlphaNoiseSpeedY"),
                                     value("BackgroudAlphaNoiseSpeedZ"), 0.0]
    spec["vectors"]["BeamFlags"] = [1.0 if f in flags else 0.0 for f in ("%NOISE", "%APPLY_SUN_COLOR", "%ENABLE_FADEOUT",
                                                                         "%VERTICAL_GRADIENT")]
    spec["vectors"]["BeamFlags2"] = [1.0 if f in flags else 0.0 for f in ("%UV_VIGNETTING", "%GRADIENT_ALPHA",
                                                                          "%APPLY_FOG_COLOR")] + [0.0]
    for name in ("FinalMultiplier", "SoftIntersectionFactor", "viewDependencyFactor", "fadingFeaturing", "baseUVScale",
                 "dustUVScale", "dustTimeScale", "turbStrength", "turbRatio", "uvRot", "SunColorInfluence",
                 "FogColorInfluence", "FadeOutDistance", "uvVigFeaturing", "AmbStrength"):
        spec["scalars"][name] = value(name)
    return spec


FX_ADDITIVE = 0x4   # the additive bit of MtlFlags (study section 1, "Blend, culling, fog, and emission units")


def beam_simple_spec(rules: dict, use) -> dict:
    """@return The spec of a GeometryBeamSimple material (research/beamsimple.md; swc_materials.beam_simple_parent, stage
    "beamsimple"): the base map (Diffuse, sRGB), with %NOISE the dust map (Specular, sRGB; none: white) and the turbulence color
    (Bumpmap, raw data), the XML Diffuse and Opacity, the public parameters (FX_DEFAULTS["beam simple"] for a missing one), the
    type 2 TexMod rotation of the Diffuse slot (Rotate: degrees each second = TexMod_WRotateAmplitude, the centre), and the blend
    (RawAdditive: the additive bit of MtlFlags with Opacity below 1 gives ONE, ONE; else SRC_ALPHA, ONE). Limits: the shadow
    samples of %RECEIVE_SHADOWS (H = 1), a TexMod rotation of another type, the U and V oscillators."""
    fx = rules["fx"]
    params, flags = fx["params"], set(fx["flags"])

    def v(name):
        value = params.get(name, FX_DEFAULTS["beam simple"][name])
        if isinstance(FX_DEFAULTS["beam simple"][name], list):
            value = list(value) if isinstance(value, list) else [float(value)] * 3
            return [float(x) for x in (value + [1.0, 1.0, 1.0, 1.0])[:4]]
        return float(value[0] if isinstance(value, list) else value)

    opacity = float(fx["opacity"])
    noise = "%NOISE" in flags
    spec = {"parent": "beam simple", "textures": {}, "vectors": {}, "scalars": {}, "switches": {}, "two sided": True}
    if "base" in fx["textures"]:
        spec["textures"]["BaseColor"] = use(color_texture(fx["textures"]["base"]["product"], "base"))
    if noise and "dust" in fx["textures"]:
        spec["textures"]["Dust"] = use(color_texture(fx["textures"]["dust"]["product"], "base"))
    if noise and "turbulence" in fx["textures"]:
        # The raw R and G of the two-channel Bumpmap with B zero (research beamsimple.md "Inputs"; a normal map PNG has B = Z).
        spec["textures"]["Turbulence"] = use(data_texture(fx["textures"]["turbulence"]["product"], raw_rg=True))
    limits = ["%RECEIVE_SHADOWS: the shadow term is 1"] if "%RECEIVE_SHADOWS" in flags else []
    mod = (fx["textures"].get("base", {}).get("uv") or {}).get("mod", {})
    rotate = [0.0, 0.5, 0.5, 0.0]
    if mod.get("TexMod_RotateType") == 2:
        rotate = [float(mod.get("TexMod_WRotateAmplitude", 0.0)), float(mod.get("TexMod_URotateCenter", 0.5)),
                  float(mod.get("TexMod_VRotateCenter", 0.5)), 0.0]
    elif mod.get("TexMod_RotateType"):
        limits.append(f"TexMod rotation type {mod.get('TexMod_RotateType')}")
    if any(mod.get(f"TexMod_{axis}OscillatorType") for axis in "UV"):
        limits.append("TexMod U or V oscillator")
    spec["vectors"]["Rotate"] = rotate
    spec["vectors"]["DiffuseColor"] = [float(x) for x in rules["diffuse"]] + [opacity]
    spec["vectors"]["StartColor"] = v("StartColor")
    spec["vectors"]["EndColor"] = v("EndColor")
    spec["vectors"]["NoiseSpeed"] = [v("BackgroudAlphaNoiseSpeedX"), v("BackgroudAlphaNoiseSpeedY"),
                                     v("BackgroudAlphaNoiseSpeedZ"), 0.0]
    for name in ("FinalMultiplier", "SoftIntersectionFactor", "fadingFeaturing", "AmbStrength", "baseUVScale", "dustUVScale",
                 "dustTimeScale", "turbStrength", "turbRatio", "uvRot"):
        spec["scalars"][name] = v(name)
    spec["scalars"]["Noise"] = 1.0 if noise else 0.0
    spec["scalars"]["RawAdditive"] = 1.0 if (fx["mtlFlags"] & FX_ADDITIVE and opacity < 1.0) else 0.0
    if limits:
        spec["limits"] = limits
    return spec


def fxmesh_spec(rules: dict, use) -> dict:
    """@return The spec of an Fxmeshadvancedtransp material (C8, swc_materials.fxmesh_parent): M_SwcFxMesh (alpha over: the
    material state of the client for Opacity below 1 without the additive bit) or M_SwcFxMeshAdd (the declared additive pass),
    Unlit, two sided, no depth write. The maps: Custom (the source RGBA), Heightmap (the SDF, red), Detail (the palette, red),
    Specular (the complex color); all sRGB. The public parameters packed in vectors (FX_DEFAULTS for a missing one), the
    options as static switches. Limits: the procedural gradients, the FX sine, and VertexDeform type 2 (zero wave) are not in
    the parent ("limits")."""
    fx = rules["fx"]
    params, flags = fx["params"], set(fx["flags"])

    def v(name):
        value = params.get(name, FX_DEFAULTS["fxmesh"][name])
        return float(value[0] if isinstance(value, list) else value)

    opacity = float(fx["opacity"])
    additive = bool(fx["mtlFlags"] & FX_ADDITIVE) or opacity >= 1.0
    spec = {"parent": "fxmesh additive" if additive else "fxmesh", "textures": {}, "vectors": {}, "scalars": {},
            "switches": {}, "two sided": True}
    for role, name in (("source", "SourceMap"), ("sdf", "SdfMap"), ("palette", "PaletteMap"), ("complex", "ComplexColorMap")):
        if role in fx["textures"]:
            spec["textures"][name] = use(color_texture(fx["textures"][role]["product"], "base"))
    spec["vectors"]["EmittanceColor"] = [float(x) for x in fx["emittance"]]
    spec["vectors"]["DiffuseColor"] = [float(x) for x in rules["diffuse"]] + [1.0]
    spec["vectors"]["SdfTS"] = [v("SDF_2D_TILE_X"), v("SDF_2D_TILE_Y"), v("SDF_2D_SPEED_X"), v("SDF_2D_SPEED_Y")]
    spec["vectors"]["SdfPS"] = [v("SDF_2D_PHASE_X"), v("SDF_2D_PHASE_Y"), v("SDF_2D_STRENGTH"), 0.0]
    spec["vectors"]["SourceTS"] = [v("SOURCE_2D_TILE_X"), v("SOURCE_2D_TILE_Y"), v("SOURCE_2D_SPEED_X"), v("SOURCE_2D_SPEED_Y")]
    spec["vectors"]["SourcePS"] = [v("SOURCE_2D_PHASE_X"), v("SOURCE_2D_PHASE_Y"), 0.0, 0.0]
    spec["vectors"]["Global"] = [v("GlobalColorFalloff"), v("GlobalAlphaStrength"), v("GlobalAlphaFill"), v("GlobalAlphaFalloff")]
    spec["vectors"]["FresnelGlow"] = [v("FRESNEL_EXPONENT"), v("FRESNEL_STRENGTH"), v("FRESNEL_INVERT"), v("FRESNEL_SDF_INFLUENCE")]
    spec["vectors"]["FresnelGlowSdf"] = [v("FRESNEL_SDF_MAP_MIX"), v("FRESNEL_SDF_MAP_EXP"), 0.0, 0.0]
    spec["vectors"]["FresnelOut"] = [v("FRESNEL_OUT_FALLOFF"), v("FRESNEL_OUT_STRENGTH"), v("FRESNEL_OUT_SDF_INF"), 0.0]
    spec["vectors"]["FresnelIn"] = [v("FRESNEL_IN_FALLOFF"), v("FRESNEL_IN_STRENGTH"), v("FRESNEL_IN_SDF_INF"), 0.0]
    spec["vectors"]["IntersectFade"] = [v("INTERSECT_FADE_DIST"), v("INTERSECT_FADE_FALLOFF"), v("INTERSECT_FADE_INV"), 0.0]
    spec["vectors"]["IntersectGlow"] = [v("INTERSECT_GLOW_DIST"), v("INTERSECT_GLOW_FALLOFF"), v("INTERSECT_GLOW_INV"), 0.0]
    spec["vectors"]["Palette"] = [v("PALETTE_PHASE"), v("PALETTE_SPEED"), v("PALETTE_FALLOFF"), v("PALETTE_SDF_INF")]
    spec["vectors"]["PaletteComplex"] = [v("PALETTE_SOURCE_BLEND"), v("COMPLEX_COL_TILE"), 0.0, 0.0]
    spec["scalars"]["Opacity"] = opacity
    spec["scalars"]["GlobalAlphaFromSource"] = v("GlobalAlphaFromSource")
    for flag, switch in (("%USE_SDF_2D", "UseSdf2D"), ("%USE_GLOW_FRESNEL", "UseGlowFresnel"),
                         ("%USE_OUTSIDE_FRESNEL_ALPHA", "UseOutsideFresnelAlpha"), ("%USE_INSIDE_FRESNEL_ALPHA", "UseInsideFresnelAlpha"),
                         ("%USE_INTERSECTION_FADE", "UseIntersectionFade"), ("%USE_INTERSECTION_GLOW", "UseIntersectionGlow"),
                         ("%VERTCOLORS", "UseVertexColors"), ("%USE_PALETTE_MAP", "UsePaletteMap")):
        spec["switches"][switch] = flag in flags
    complex_on = "%USE_COMPLEX_COL" in flags
    overlay, dodge = "%USE_COMPLEX_COL_OVERLAY" in flags, "%USE_COMPLEX_COL_DODGE" in flags
    spec["switches"]["UseComplexOverlay"] = complex_on and overlay and not dodge
    spec["switches"]["UseComplexDodge"] = complex_on and dodge and not overlay
    limits = sorted(flags & {"%USE_PROC_GRADS", "%VERT_DEFORM_SINWAVE", "%USE_REFRACTION", "%FX_DISSOLVE"})
    if limits:
        spec["limits"] = limits
    return spec


def meshparticle_spec(rules: dict, use) -> dict:
    """@return The spec of a Meshparticle material (C8, swc_materials.meshparticle_parent: Translucent alpha over, Unlit, two
    sided, no depth write): the layers Diffuse and Custom (%DIFFUSE_MAP_2), the layer mask (the Emittance slot; none: white),
    all sRGB; the XML Diffuse and Opacity; the UV transform of each slot and the type-2 movement of the Custom slot (UV =
    Tile x (UV0 + t x Rate x Amplitude) + Offset); the public parameters (FX_DEFAULTS for a missing one). Limits: a TexMod
    rotation, an oscillator type other than 2, and the layers 3 and 4."""
    fx = rules["fx"]
    params, flags = fx["params"], set(fx["flags"])

    def v(name):
        value = params.get(name, FX_DEFAULTS["meshparticle"][name])
        return float(value[0] if isinstance(value, list) else value)

    spec = {"parent": "meshparticle", "textures": {}, "vectors": {}, "scalars": {}, "switches": {}, "two sided": True}
    for role, name in (("diffuse", "DiffuseMap"), ("diffuse2", "DiffuseMap2"), ("layer mask", "LayerMask")):
        if role in fx["textures"]:
            spec["textures"][name] = use(color_texture(fx["textures"][role]["product"], "base"))
    limits = []
    custom_uv = (fx["textures"].get("diffuse2") or {}).get("uv") or {}
    mod = custom_uv.get("mod", {})
    move = [0.0, 0.0]
    for i, axis in enumerate(("U", "V")):
        kind = mod.get(f"TexMod_{axis}OscillatorType", 0)
        if kind == 2:
            move[i] = float(mod.get(f"TexMod_{axis}OscillatorRate", 0.0)) * float(mod.get(f"TexMod_{axis}OscillatorAmplitude", 1.0))
        elif kind:
            limits.append(f"Custom oscillator type {kind}")
    for role in ("diffuse", "diffuse2", "layer mask"):
        if any("Rotate" in k for k in ((fx["textures"].get(role) or {}).get("uv") or {}).get("mod", {})):
            limits.append(f"{role}: TexMod rotation")
    spec["vectors"]["DiffuseUV"] = uv_transform((fx["textures"].get("diffuse") or {}).get("uv"))
    spec["vectors"]["CustomUV"] = uv_transform(custom_uv)
    spec["vectors"]["CustomMove"] = move + [0.0, 0.0]
    spec["vectors"]["MaskUV"] = uv_transform((fx["textures"].get("layer mask") or {}).get("uv"))
    spec["vectors"]["DiffuseColor"] = [float(x) for x in rules["diffuse"]] + [float(fx["opacity"])]
    spec["vectors"]["Light"] = [v("AmbStrength"), v("Brightness"), v("SunColorInfluence"), v("SoftIntersectionFactor")]
    spec["vectors"]["Fade"] = [v("fadingFeaturing"), v("viewDependencyFactor"), v("FadeOutDistance"), 0.0]
    spec["scalars"]["Opacity"] = float(fx["opacity"])
    spec["switches"]["ApplySunColor"] = "%APPLY_SUN_COLOR" in flags
    spec["switches"]["EnableFadeout"] = "%ENABLE_FADEOUT" in flags
    spec["switches"]["UseDiffuseMap2"] = "%DIFFUSE_MAP_2" in flags
    limits += sorted(flags & {"%DIFFUSE_MAP_3", "%DIFFUSE_MAP_4", "%USE_REFRACTION", "%FX_DISSOLVE"})
    if limits:
        spec["limits"] = limits
    return spec


def uv_limits(rules: dict) -> list[str]:
    """@return The UV rules of a spec that the parents do not show: a rotation or an oscillator, and a slot whose
    tiling differs from the tiling of the diffuse slot."""
    out = []
    maps = rules["textures"]
    first = uv_transform(maps.get("diffuse", {}).get("uv"))
    for role, t in maps.items():
        uv = t.get("uv") or {}
        if "mod" in uv:
            out.append(f"{role}: rotation or oscillator")
        if role in ("normal", "gloss", "specular") and uv_transform(uv) != first:
            out.append(f"{role}: other tiling")
    return out


@functools.lru_cache(maxsize=1)
def main_keys() -> dict[str, str]:
    """@return The main render key of each legacy key (SWC_CONTENT/materials/keys.json, written by instances())."""
    path = out_dir() / "keys.json"
    return json.loads(path.read_text())["main render keys"] if path.is_file() else {}


def legacy_entry(records: dict, name: str) -> dict:
    """@return The stage 1.2 base color rule of a source: "+opacity:" when it is masked and has an Opacity map."""
    record = records["sources"][name]
    opacity = (record["textures"].get("Opacity") or {}).get("product")
    return {"base": "+opacity:" + opacity} if opacity and blend_of(record)[0] == "masked" else {}


def legacy_spec(records: dict, name: str, textures: dict) -> dict:
    """@return The spec of the stage 1.2 rules of one source sub-material (materials.shared_material: the old base
    parents M_SwcOpaque, M_SwcMasked, M_SwcTranslucent with BaseColor, Normal, BaseColorFactor, two sided, cutoff):
    an n_ instance of a source that no stage rule changes."""
    mtl, index = name.rsplit("#", 1)
    subs = sub_materials(mtl)
    element = materials.sub_material(subs, int(index))
    record, used = materials.shared_material(element, "far legacy" not in records["sources"][name]
                                             or bool(records["sources"][name].get("legacy")))
    for t in used:
        textures[t["asset"]] = {"file": t["file"], "color": "normal" if t["role"] == "normal" else "srgb", "cap": t["cap"]}
    spec = {"parent": "legacy " + record["blend"], "textures": {}, "vectors": {"BaseColorFactor": record["factor"]},
            "scalars": {}, "two sided": record["two sided"]}
    if "cutoff" in record:
        spec["cutoff"] = record["cutoff"]
    if "base" in record:
        spec["textures"]["BaseColor"] = record["base"]
    if "normal" in record:
        spec["textures"]["Normal"] = record["normal"]
    return spec


def instances(candidate: tuple[str, ...] = ()) -> dict:
    """Writes SWC_CONTENT/materials/instances.json (the spec of each material key that changes with the deployed
    stages DEPLOYED, with the main source and the other render keys of the key) and textures.json (the texture records
    of all specs: the key, the PNG, the color space, the Unreal folder). The import pipeline applies instances.json
    (swc_materials.apply_specs). `candidate`: stages under check; their specs go to instances-candidate.json, which
    only the render check reads. Reads records.json and slots.json (tools/convert-materials.sh audit) and the manifest.
    @return The summary."""
    global STAGES
    started = time.time()
    records = json.loads((out_dir() / "records.json").read_text())
    slot_table = json.loads((out_dir() / "slots.json").read_text())["meshes"]
    assets = manifest.load()["assets"]
    uses: dict[str, dict[str, int]] = {}     # legacy key -> render key -> mesh slots
    for out in slot_table.values():
        for _index, legacy, render in out:
            if render:
                uses.setdefault(legacy, {}).setdefault(render, 0)
                uses[legacy][render] += 1
    source_of: dict[str, str] = {}
    for name, s in records["sources"].items():
        source_of.setdefault(s["render"], name)
        for field in ("legacy", "far legacy"):
            if s.get(field) and "mesh" not in s["use"]:
                uses.setdefault(s[field], {}).setdefault(s["render"], 0)
    # AST-290: "flow" is not in the render rules of an earlier audit (records.json); it is not in the render key, so it is added here
    # from the first source of each render key.
    for render, name in source_of.items():
        rules = records["render rules"].get(render)
        if rules is not None and "flow" not in rules:
            flow = flow_of(records["sources"][name])
            if flow:
                rules["flow"] = flow
    textures: dict = {}
    summary: dict = {}
    # The main render key of each legacy key: the render key of the most mesh slots (keys.json: materials.instance_key).
    # A main of the earlier keys.json stays while it is still a render key of its legacy key: new sources (for example
    # the ground cover) do not rename the n_ instances that the meshes and the cells use.
    earlier = main_keys()
    mains = {legacy: earlier[legacy] if earlier.get(legacy) in renders
             else sorted(renders.items(), key=lambda kv: (-kv[1], kv[0]))[0][0] for legacy, renders in uses.items()}
    write_json(out_dir() / "keys.json", {"version": SPEC_VERSION, "main render keys": mains})
    main_keys.cache_clear()
    for label, stages in (("instances", DEPLOYED), ("instances-candidate", tuple(candidate))):
        if not stages:
            continue
        STAGES = stages
        specs, errors, counts, limits = {}, {}, {}, {}
        for legacy, renders in sorted(uses.items()):
            main = mains[legacy]
            # A key that the manifest does not have yet (for example an override material that the world track imports
            # later): the hook of import_content.py applies its spec at its first import. The base color of stage 1.2
            # had the Opacity map as its alpha when the source is masked and has an Opacity map.
            entry = assets.get("material:" + legacy)
            if entry is None:
                record = records["sources"][source_of[main]]
                opacity = (record["textures"].get("Opacity") or {}).get("product")
                entry = {"base": "+opacity:" + opacity} if opacity and blend_of(record)[0] == "masked" else {}
            try:
                spec = spec_of(records["render rules"][main], entry, textures)
            except Exception as error:
                errors[legacy] = str(error)[:160]
                continue
            if spec is not None:   # None: the instance of the key keeps the rules of stage 1.2
                spec.update({"render": main, "source": source_of.get(main), "slots": renders[main],
                             "other render keys": {k: v for k, v in renders.items() if k != main}})
                specs[legacy] = spec
                kind = spec["parent"] or "base color only"
                counts[kind] = counts.get(kind, 0) + 1
                for limit in uv_limits(records["render rules"][main]):
                    limits[limit] = limits.get(limit, 0) + 1
            # A1: an instance n_RENDER for each other render key of the legacy key (materials.instance_key gives it to a
            # source sub-material): its own rules; the stage 1.2 rules of its own source when no stage rule applies.
            for render in renders:
                if render == main:
                    continue
                try:
                    other = spec_of(records["render rules"][render], legacy_entry(records, source_of[render]), textures)
                    if other is None or other["parent"] is None:
                        base = legacy_spec(records, source_of[render], textures)
                        if other is not None:   # M1 base color only: the base color with its own alpha
                            base["textures"]["BaseColor"] = other["textures"]["BaseColor"]
                        other = base
                except Exception as error:
                    errors["n_" + render[2:]] = str(error)[:160]
                    continue
                other.update({"create": True, "render": render, "source": source_of.get(render),
                              "slots": renders[render], "legacy": legacy})
                specs["n_" + render[2:]] = other
                counts["n_ " + str(other["parent"])] = counts.get("n_ " + str(other["parent"]), 0) + 1
        write_json(out_dir() / f"{label}.json", {"version": SPEC_VERSION, "stages": list(stages), "instances": specs})
        summary[label] = {"stages": list(stages), "instances": len(specs), "by parent": counts,
                          "with other render keys": sum(1 for s in specs.values() if s.get("other render keys")),
                          "UV limits": limits, "errors": len(errors), "first errors": dict(list(errors.items())[:5])}
    STAGES = DEPLOYED
    if "beam" in DEPLOYED:
        noise_picture()
    for key, record in textures.items():
        record["unreal"] = manifest.unreal_folder("texture", key)
    write_json(out_dir() / "textures.json", {"version": SPEC_VERSION, "textures": textures})
    write_json(out_dir() / "globals.json", globals_of())
    summary.update({"textures": len(textures), "new textures (not in the manifest)": sum(1 for k in textures if k not in assets),
                    "seconds": round(time.time() - started, 1)})
    return summary


def globals_of() -> dict:
    """@return The global values of MPC_SwcMaterials from the level data: WindVector, the EnvState WindVector of the
    mission (content/environment/mission.json of the environment work; engine axes -> Unreal axes (x, -y, z); its
    length is the strength of the wind). The default (1, 0, 0) is the value of the main world."""
    path = env.content_dir() / "environment" / "mission.json"
    wind = [1.0, 0.0, 0.0]
    if path.is_file():
        text = json.loads(path.read_text()).get("environment", {}).get("EnvState", {}).get("WindVector")
        if text:
            x, y, z = (float(v) for v in str(text).split(","))
            wind = [x, -y, z]
    return {"version": SPEC_VERSION, "WindVector": wind}


# The render check of a stage (Scripts/material_proof.py): meshes in three variants side by side.
PROOF_MESHES = {   # (label, mesh, override material or None: then the third column is the candidate spec)
    "m1": [("poplar", "objects/nature/veg_trees/trees_forest/poplar/jav_fst_poplartree_full_e.cgf",
            "objects/nature/veg_trees/trees_forest/poplar/jav_fst_poplartree_a_fall01_mat.mtl"),
           ("pawpaw", "objects/nature/veg_bushes/bushes_forest/pawpaw/jav_fst_pawpawbush_c.cgf",
            "objects/nature/veg_bushes/bushes_forest/pawpaw/jav_fst_pawpawbush_a_fall01_mat.mtl")],
    "m2": [("plot-walls", "slices/structures/plots/house_plots/ef_msq_2_house_plots/ef_2story_9x15_ab__28236438930.cgf", None),
           ("plot-stone", "slices/structures/plots/house_plots/ef_msq_2_house_plots/ef_2story_9x15_ab__935090250.cgf", None),
           ("plot-roof", "slices/structures/plots/house_plots/ef_msq_2_house_plots/ef_2story_9x15_ab__11223487900.cgf", None),
           ("plot-shell", "slices/structures/plots/house_plots/ef_msq_2_house_plots/ef_2story_9x15_ab__19224261670.cgf", None),
           ("lamp", "objects/crafting/props/props_everfall/decor/everfall_lightpole_a.cgf", None),
           ("boulder", "objects/nature/rock_boulders/boulders_forest/jav_fst_boulder_sm_b.cgf", None),
           ("timber", "objects/crafting/architecture/crafting_structures/baseframe/jav_struc_craft_t2_abnd.cgf", None)],
}


# The render check of M3 and M4 on the meshes of rule 7 of the test import (/SwcGameContent/Test/M3; their slots are
# the instance keys): the instances as they are now, and the candidate specs.
M3_TEST = Path.home() / "Games/soulwarden-client/scratch-m3"
PROOF_M3 = [("plot-shell", "slices/structures/plots/house_plots/ef_msq_2_house_plots/ef_2story_9x15_ab__19224261670.cgf"),
            ("plot-walls", "slices/structures/plots/house_plots/ef_msq_2_house_plots/ef_2story_9x15_ab__28236438930.cgf"),
            ("timber", "objects/crafting/architecture/crafting_structures/baseframe/jav_struc_craft_t2_abnd.cgf"),
            ("lamp", "objects/crafting/props/props_everfall/decor/everfall_lightpole_a.cgf"),
            ("boulder", "objects/nature/rock_boulders/boulders_forest/jav_fst_boulder_sm_b.cgf"),
            # M4 glass: the window panes (the tint mask and the dirt) and a small bottle (the tint mask as F0)
            ("windows", "objects/crafting/architecture/living_structures/everfall/housing/everfall_living_story_9x15_h.cgf"),
            ("tincture", "objects/crafting/props/shopwares/alchemyprops/tincture_t1.cgf")]


# M6: the wind of the foliage on rule 7 test meshes with detail bending: the same candidate spec with SwcWind (the custom
# primitive data 0) 0, 1, and 4 (a still picture shows the bend; the motion needs a video).
PROOF_M6 = [("poplar", "objects/nature/veg_trees/trees_forest/poplar/jav_fst_poplartree_full_a.cgf"),
            ("pawpaw", "objects/nature/veg_bushes/bushes_forest/pawpaw/jav_fst_pawpawbush_a.cgf"),
            ("groundcover", "objects/nature/veg_grass/grass_jungle/junglegroundcover/jav_jgl_groundcover_a.cgf")]
WIND_COLUMNS = {"still": 0.0, "wind": 1.0, "wind x4": 4.0}


def proof_plan_test(stage: str) -> dict:
    """Writes proof-STAGE.json for the meshes of the test import (rule 7): columns "now" and "candidate" (m6: the
    candidate in the columns of WIND_COLUMNS, with their custom primitive data)."""
    rows_in = {r["mesh"]: r for r in json.loads((M3_TEST / "list.json").read_text())}
    folders = {i["file"]: i["folder"] for i in json.loads((M3_TEST / "import-list.json").read_text())}
    candidates = json.loads((out_dir() / "instances-candidate.json").read_text())["instances"]
    assets = manifest.load()["assets"]
    slot_table = json.loads((out_dir() / "slots.json").read_text())["meshes"]
    rows = []
    for label, mesh in (PROOF_M6 if stage == "m6" else PROOF_M3):
        if assets.get(mesh, {}).get("version", 0) >= 7:   # after the M3 rebuild: the world mesh of rule 7
            keys, folder = assets[mesh]["slots"], assets[mesh]["unreal"]
        elif mesh in rows_in:   # a mesh of the test import: its slots are the instance keys
            keys, folder = rows_in[mesh]["materials"], folders[rows_in[mesh]["file"]]
        elif mesh in slot_table:   # a mesh of the plugin (rule 6): its slots are the legacy keys
            keys, folder = sorted({legacy for _i, legacy, _r in slot_table[mesh]}), assets[mesh]["unreal"]
        else:
            continue
        if stage == "m6":
            variants = {column: {key: ({"spec": candidates[key]} if key in candidates else {"instance": key})
                                 for key in keys} for column in WIND_COLUMNS}
            rows.append({"label": label, "mesh": mesh, "unreal": folder, "override": None, "columns": list(variants),
                         "variants": variants, "custom data": dict(WIND_COLUMNS)})
            continue
        variants = {"now": {}, "candidate": {}}
        for key in keys:
            variants["now"][key] = {"instance": key}
            variants["candidate"][key] = {"spec": candidates[key]} if key in candidates else {"instance": key}
        rows.append({"label": label, "mesh": mesh, "unreal": folder, "override": None,
                     "columns": list(variants), "variants": variants})
    plan = {"stage": stage, "rows": rows}
    write_json(out_dir() / f"proof-{stage}.json", plan)
    return {"stage": stage, "rows": [{"label": r["label"], "slots": len(r["variants"][r["columns"][0]]),
                                      "columns": r["columns"]} for r in rows]}


# The foliage check (M_SwcFoliage version 3: the back light x the albedo): rule 7 world meshes with the fall overrides of
# the placements (content/overrides/pairs.json) and of the ground cover (content/cover/pairs.json): the instances as they
# are now.
PROOF_FOLIAGE = [("poplar fall01", "objects/nature/veg_trees/trees_forest/poplar/jav_fst_poplartree_full_a.cgf",
                  "objects/nature/veg_trees/trees_forest/poplar/jav_fst_poplartree_a_fall01_mat.mtl"),
                 ("pawpaw fall01", "objects/nature/veg_bushes/bushes_forest/pawpaw/jav_fst_pawpawbush_c.cgf",
                  "objects/nature/veg_bushes/bushes_forest/pawpaw/jav_fst_pawpawbush_a_fall01_mat.mtl"),
                 ("grass fall01", "objects/nature/veg_grass/grass_forest/mediumgrass/jav_fst_medgrass_a.cgf",
                  "objects/nature/veg_grass/grass_forest/mediumgrass/jav_fst_grasses_a_fall01_mat.mtl"),
                 ("grass fall02", "objects/nature/veg_grass/grass_forest/mediumgrass/jav_fst_medgrass_a.cgf",
                  "objects/nature/veg_grass/grass_forest/mediumgrass/jav_fst_grasses_a_fall02_mat.mtl")]


def proof_plan_foliage(stage: str) -> dict:
    """Writes proof-STAGE.json: the meshes of PROOF_FOLIAGE in their world folders, one column "now" with the instance of
    the override of each slot."""
    assets = manifest.load()["assets"]
    pairs = {}
    for path in (env.content_dir() / "overrides" / "pairs.json", env.content_dir() / "cover" / "pairs.json"):
        if path.is_file():
            pairs.update(json.loads(path.read_text()))
    rows = []
    for label, mesh, override in PROOF_FOLIAGE:
        entry = assets.get(mesh)
        if not entry:
            continue
        slots = entry.get("slots") or entry.get("materials", [])
        mapping = pairs.get(f"{mesh}|{override}", {}).get("slots", {})
        variants = {"now": {slot: {"instance": mapping.get(slot, slot)} for slot in slots}}
        rows.append({"label": label.replace(" ", "-"), "mesh": mesh, "unreal": entry["unreal"], "override": override,
                     "columns": ["now"], "variants": variants})
    write_json(out_dir() / f"proof-{stage}.json", {"stage": stage, "rows": rows})
    return {"stage": stage, "rows": [{"label": r["label"], "slots": r["variants"]["now"]} for r in rows]}


def proof_plan(stage: str) -> dict:
    """Writes SWC_CONTENT/materials/proof-STAGE.json: for each proof mesh, its Unreal folder and three variants of its
    slots: "stage 1.2" (the manifest record of each slot key: the old rules), "now" (the material instance of the slot
    key as it is now), and either "override" (the key of the same source slot of the override material of the region
    data: its instance, or its spec when the asset is not imported) or "candidate" (the spec of the slot key in
    instances-candidate.json: the stages under check)."""
    assets = manifest.load()["assets"]
    slot_table = json.loads((out_dir() / "slots.json").read_text())["meshes"]
    specs = json.loads((out_dir() / "instances.json").read_text())["instances"]
    candidate_file = out_dir() / "instances-candidate.json"
    candidates = json.loads(candidate_file.read_text())["instances"] if candidate_file.is_file() else {}
    rows = []
    for label, mesh, override in PROOF_MESHES[stage]:
        if mesh not in slot_table:
            raise ValueError(f"{mesh}: no slots (not converted, or not in the audit)")
        third = "override" if override else "candidate"
        variants = {"stage 1.2": {}, "now": {}, third: {}}
        subs = sub_materials(override) if override else ()
        for index, legacy, _render in slot_table[mesh]:
            if legacy in variants["stage 1.2"]:
                continue   # a collapsed slot: its first source slot
            variants["stage 1.2"][legacy] = {"legacy": assets["material:" + legacy]}
            variants["now"][legacy] = {"instance": legacy}
            if not override:
                variants[third][legacy] = {"spec": candidates[legacy]} if legacy in candidates else {"instance": legacy}
                continue
            element = materials.sub_material(subs, index)
            if element is None or materials.not_drawn(element):
                variants[third][legacy] = {"instance": legacy}
                continue
            key = legacy_key(element)
            variants[third][legacy] = ({"instance": key, **({"spec": specs[key]} if key in specs else {})}
                                       if "material:" + key in assets else {"spec": specs.get(key), "key": key})
        rows.append({"label": label, "mesh": mesh, "unreal": assets[mesh]["unreal"], "override": override,
                     "columns": list(variants), "variants": variants})   # columns: the order (the file sorts keys)
    plan = {"stage": stage, "rows": rows}
    write_json(out_dir() / f"proof-{stage}.json", plan)
    return {"stage": stage, "rows": [{"label": r["label"], "slots": len(r["variants"]["stage 1.2"]),
                                      "columns": r["columns"]} for r in rows]}


def proof_sheet(stage: str, evidence: Path | None = None) -> dict:
    """Writes proof-STAGE-sheet.png next to the pictures of Scripts/material_proof.py: for each mesh, the front and
    back pictures with the names of the three columns. @return The path."""
    from PIL import Image, ImageDraw
    evidence = evidence or env.work_dir() / "evidence" / "WF3" / stage
    plan = json.loads((out_dir() / f"proof-{stage}.json").read_text())
    tiles = []
    for row in plan["rows"]:
        for side in ("front", "back"):
            path = evidence / f"proof-{row['label']}-{side}.png"
            if path.is_file():
                tiles.append((f"{row['label']}, sun {'behind the camera' if side == 'front' else 'behind the meshes'}",
                              Image.open(path).convert("RGB")))
    if not tiles:
        return {"sheet": None}
    width = max(t.width for _l, t in tiles)
    sheet = Image.new("RGB", (width, sum(t.height + 34 for _l, t in tiles)), (20, 24, 30))
    draw = ImageDraw.Draw(sheet)
    y = 0
    for label, tile in tiles:
        draw.text((8, y + 10), label, fill=(230, 230, 230))
        names = plan["rows"][0]["columns"] if plan["rows"] else []
        for i, name in enumerate(names):
            draw.text((tile.width * (2 * i + 1) // 6 - 50, y + 10), name, fill=(230, 200, 120))
        sheet.paste(tile, (0, y + 34))
        y += tile.height + 34
    out = evidence / f"proof-{stage}-sheet.png"
    sheet.save(out)
    return {"sheet": str(out)}


# The M3 rebuild (A3 included): the meshes that need mesh rule 7, their conversion, and the list of the import into the
# existing mesh folders (tools/import-gltf-list.sh: the import state of the pipeline is written, so that
# tools/import.sh sees them as up to date).
REBUILD_FLAGS = {"%VERTCOLORS", "%BLENDLAYER", "%DETAIL_BENDING"}


def mesh_decal_subsets(mesh: str) -> tuple[str, int, str | None]:
    """@return (mesh, the count of its subsets that rule 7 draws as mesh decals, an error) for a worker process: the chunk
    headers and the material file (no .cgfheap)."""
    from . import cgf
    from . import mesh as mesh_module
    try:
        f = cgf.read_chunks(source.read(mesh))
        from . import props
        reference = props.own_reference(f, mesh)
        subs = materials.load_mtl(reference, mesh)[1] if reference else []
        count = 0
        for chunk in f.of_type(cgf.T_MESH):
            _flags, _flags2, nverts, _nindices, _nsubsets, subsets_id, _va = struct.unpack_from("<7i", chunk.data, 0)
            if nverts == 0 or subsets_id not in f.by_id:
                continue
            sub = f.by_id[subsets_id].data
            _sflags, n = struct.unpack_from("<2i", sub, 0)
            for i in range(n):
                mat = struct.unpack_from("<5i", sub, 16 + i * 36)[4]
                count += mesh_module.is_mesh_decal(materials.sub_material(subs, mat))
        return mesh, count, None
    except Exception as error:
        return mesh, 0, str(error)[:120]


def decal_meshes(workers: int = 4) -> list[str]:
    """@return The converted meshes with mesh decal subsets (%DECAL: the companions of rule 7). Cached in
    SWC_CONTENT/materials/decal-meshes.json (delete it to scan again)."""
    path = out_dir() / "decal-meshes.json"
    if path.is_file():
        return json.loads(path.read_text())["meshes"]
    from concurrent.futures import ProcessPoolExecutor
    slot_table = json.loads((out_dir() / "slots.json").read_text())["meshes"]
    with ProcessPoolExecutor(max_workers=workers) as pool:
        rows = list(pool.map(mesh_decal_subsets, sorted(slot_table), chunksize=32))
    found = sorted(m for m, n, _e in rows if n)
    write_json(path, {"meshes": found, "errors": sum(1 for _m, _n, e in rows if e)})
    return found


def rebuild_set() -> dict:
    """Writes SWC_CONTENT/materials/rule7-meshes.json: each converted mesh with a collapsed slot (source slots with
    different render rules), a slot whose rules are not the main rules of its legacy key (A3: it needs its n_ instance),
    vertex data (%VERTCOLORS, %BLENDLAYER, %DETAIL_BENDING), or mesh decal subsets (the companions). mesh.convert gives
    these meshes rule 7. @return The counts."""
    rules = json.loads((out_dir() / "records.json").read_text())["render rules"]
    slot_table = json.loads((out_dir() / "slots.json").read_text())["meshes"]
    collapsed = {c["mesh"] for c in json.loads((out_dir() / "audit.json").read_text())["collapsed"]}
    mains = main_keys()
    census_file = env.content_dir() / "census" / "meshes.json"
    census = {r["mesh"]: r for r in json.loads(census_file.read_text())} if census_file.is_file() else {}
    parts: dict[str, set] = {"collapsed": set(), "A3": set(), "vertex data": set(), "mesh decals": set(decal_meshes())}
    # The override materials of the menu scene (AST-207: the fog sheets of the vista; content/frontend/nighthaven.json).
    scene = env.content_dir() / "frontend" / "nighthaven.json"
    if scene.is_file():
        for mtl in sorted({m["material"] for m in json.loads(scene.read_text())["meshes"] if m.get("material")}):
            if source.has(mtl):
                for index, element in enumerate(sub_materials(mtl)):
                    add_source(mtl, index, element, "menu override")
    from . import cover_materials
    if (cover_materials.out_dir() / "descriptors.json").is_file():
        parts["cover"] = set(cover_materials.cover_meshes())   # the bend data of the wind (the vertex colors)
    for mesh, out in slot_table.items():
        if mesh in collapsed:
            parts["collapsed"].add(mesh)
        for _index, legacy, render in out:
            if not render:
                continue
            if mains.get(legacy, render) != render:
                parts["A3"].add(mesh)
            if set(rules[render].get("flags", [])) & REBUILD_FLAGS:
                parts["vertex data"].add(mesh)
    union = sorted(set().union(*parts.values()))

    def triangles(meshes) -> float:
        return round(sum(census.get(m, {}).get("drawn triangles", 0) for m in meshes) / 1e6, 1)

    counts = {k: {"meshes": len(v), "M triangles": triangles(v)} for k, v in parts.items()}
    counts["rebuild"] = {"meshes": len(union), "M triangles": triangles(union)}
    write_json(out_dir() / "rule7-meshes.json", {"version": SPEC_VERSION, "counts": counts, "meshes": union})
    from . import mesh as mesh_module
    mesh_module.rule7_meshes.cache_clear()
    return counts


LUMEN_MIN_M = 1.0   # the Lumen mesh data only for a mesh of at least 1 m (the rule of the Lumen copies of E2: a smaller
                    # mesh is below the detail of the distance fields)


def gltf_size(path: Path) -> float:
    """@return The largest extent (m) of the POSITION bounds of a glTF file (0 when it has none)."""
    doc = json.loads(path.read_text())
    lo, hi = None, None
    for m in doc.get("meshes", []):
        for p in m["primitives"]:
            a = doc["accessors"][p["attributes"]["POSITION"]]
            if "min" in a and "max" in a:
                lo = a["min"] if lo is None else [min(x, y) for x, y in zip(lo, a["min"])]
                hi = a["max"] if hi is None else [max(x, y) for x, y in zip(hi, a["max"])]
    return max(h - l for l, h in zip(lo, hi)) if lo else 0.0


def rebuild_one(path: str) -> dict:
    from . import mesh as mesh_module
    try:
        # force: the check of an existing output reads the whole manifest (slow for each mesh); a conversion again gives
        # the same file and hash.
        return mesh_module.convert(path, force=True, write_manifest=False, rule=mesh_module.RULE_COLORS)
    except Exception as error:
        return {"mesh": path, "error": str(error)[:160]}


def rebuild_convert(workers: int = 4) -> dict:
    """Converts the meshes of rule7-meshes.json with rule 7, writes their
    manifest records one time, and writes SWC_CONTENT/materials/rebuild-import.json: the list of
    tools/import-gltf-list.sh (each glTF, its existing mesh folder, Nanite, the manifest key and hash). @return The counts."""
    from concurrent.futures import ProcessPoolExecutor
    from . import mesh as mesh_module
    started = time.time()
    todo = json.loads((out_dir() / "rule7-meshes.json").read_text())["meshes"]
    with ProcessPoolExecutor(max_workers=workers) as pool:
        reports = list(pool.map(rebuild_one, todo, chunksize=16))
    converted = [r for r in reports if "entry" in r]
    if converted:
        manifest.add_many(mesh_module.records_of(converted))
    # The materials of the mesh decal companions (decals.specs: dm_ instances of M_SwcDecal).
    mesh_decals: dict[str, dict] = {}
    for r in converted:
        for key, m in r.get("companion", {}).get("decals", {}).items():
            mesh_decals.setdefault(key, {**m, "uses": 0})["uses"] += 1
    decal_folder = env.content_dir() / "decals"
    decal_folder.mkdir(parents=True, exist_ok=True)
    write_json(decal_folder / "mesh-materials.json", {"version": SPEC_VERSION, "materials": mesh_decals})
    from . import decals
    decal_specs = decals.specs()
    assets = manifest.load()["assets"]
    from . import nanite
    no_nanite = nanite.listed(env.content_dir())   # AST-288 W2: a translucent slot through an override pair or a spec
    items = []
    for path in todo:
        entry = assets.get(path)
        if not entry or entry.get("version") != mesh_module.RULE_COLORS:
            continue
        for key in (path, entry.get("decals")):
            e = assets.get(key) if key else None
            if e:
                file = env.content_dir() / e["file"]
                # The Lumen mesh data (the decision of the user): the main meshes of 1 m or more; not the companions.
                lumen = key == path and gltf_size(file) >= LUMEN_MIN_M
                items.append({"key": key, "hash": e["hash"] + (nanite.NO_NANITE_STAMP if key in no_nanite else ""),
                              "file": str(file), "folder": e["unreal"],
                              "nanite": e.get("nanite", True) and key not in no_nanite, "lumen": lumen, "slots": e.get("slots", [])})
    write_json(out_dir() / "rebuild-import.json", items)
    errors = [r for r in reports if "error" in r]
    companions = [r["companion"] for r in converted if "companion" in r]
    return {"meshes": len(todo), "converted": len(converted), "errors": len(errors),
            "companions": len(companions), "companion triangles": sum(c["triangles"] for c in companions),
            "mesh decal materials": len(mesh_decals), "decal specs": decal_specs,
            "with the Lumen mesh data": sum(1 for i in items if i["lumen"]), "first errors": errors[:5], "import list": len(items),
            "M triangles converted": round(sum(r.get("triangles", 0) for r in converted) / 1e6, 1),
            "seconds": round(time.time() - started, 1)}


def frames(logs: list[str]) -> dict:
    """@return The frame time of runs with -SwcFrameStats (Source/SoulwardenClient/World/SwcFrameStats): for each log and
    each view place (the "view m" of the lines, to 1 m), the median of the p50 and p95 values of its windows after
    the first window at the place (it still loads)."""
    import re
    import statistics
    out = {}
    for log in logs:
        places: dict[str, list[dict]] = {}
        for line in Path(log).read_text(errors="replace").splitlines():
            m = re.search(r"SWC FRAMES (\{.*\})", line)
            if not m:
                continue
            row = json.loads(m.group(1))
            if row["frames"] < 10 or row["view m"] == [0.0, -0.0, 0.0] or row["view m"] == [0.0, 0.0, 0.0]:
                continue
            places.setdefault(" ".join(f"{v:.0f}" for v in row["view m"]), []).append(row)
        result = {}
        for place, rows in places.items():
            rows = rows[1:] or rows
            result[place] = {"windows": len(rows),
                             **{k: round(statistics.median(r[k] for r in rows), 2)
                                for k in ("frame p50 ms", "frame p95 ms", "gpu p50 ms", "gpu p95 ms")}}
        out[Path(log).name] = result
    return out


# The exit check of M0: the source chains of the research proofs (research/world-materials/proof/*-inputs.json) and the
# on/off overrides of the entities near Oxboro (near-entity-evidence.json) come out of the records unchanged.
RESEARCH = Path.home() / "Games/soulwarden-client/research/world-materials"


def chains() -> dict:
    """@return The check of the research chains against the records (records.json): each source attribute, public
    parameter, and texture product of the proof inputs; the material and emission of each override near Oxboro."""
    records = json.loads((out_dir() / "records.json").read_text())["sources"]
    out: dict = {"proofs": {}, "overrides": {}, "failed": 0}
    for path in sorted((RESEARCH / "proof").glob("*-inputs.json")):
        doc = json.loads(path.read_text())
        mtl = doc["source material"]
        rows = []
        for used in doc["used"]:
            name = f"{mtl}#{used['slot']}"
            record = records.get(name) or source_record(mtl, used["slot"], sub_materials(mtl)[used["slot"]])
            problems = []
            for k, v in used["attributes"].items():
                if k in ("Diffuse", "Specular", "Emittance", "Opacity", "AlphaTest", "Shininess"):
                    mine = record["lighting"].get(k)
                    if mine is None or not same(number(v), mine):
                        problems.append(f"{k}: {v} != {mine}")
            for k, v in used["params"].items():
                if not same(number(v), record["params"].get(k)):
                    problems.append(f"param {k}: {v} != {record['params'].get(k)}")
            for t in used.get("textures", []):
                slot = t.get("Map") or t.get("attributes", {}).get("Map")
                product = t.get("product")
                if product and record["textures"].get(slot, {}).get("product") != product:
                    problems.append(f"texture {slot}")
            if record["shader"] != used["shader"]:
                problems.append("shader")
            rows.append({"source": name, "name": used["name"], "ok": not problems, "problems": problems,
                         "render": render_key(render_rules(record)),
                         "roles": {slot: t["role"] for slot, t in record["textures"].items()}})
            out["failed"] += bool(problems)
        out["proofs"][path.stem] = {"source material": mtl, "slots": rows}
    near = RESEARCH / "near-entity-evidence.json"
    for row in json.loads(near.read_text()) if near.is_file() else []:
        for mtl in row.get("materials", []):
            if mtl in out["overrides"] or not source.has(mtl):
                continue
            emission = {}
            for i, element in enumerate(sub_materials(mtl)):
                if materials.not_drawn(element):
                    continue
                rules = render_rules(source_record(mtl, i, element))
                if "emittance" in rules or "emittance" in rules["textures"]:
                    emission[i] = {"emittance": rules.get("emittance"),
                                   "map": rules["textures"].get("emittance", {}).get("product")}
            out["overrides"][mtl] = {"mesh": row["mesh"], "in records": any(k.startswith(mtl + "#") for k in records),
                                     "emissive slots": emission}
    return out


def same(a, b, tolerance: float = 1e-6) -> bool:
    """@return True when two source values are equal as numbers (a short list and a long list with a 4th value of 1:
    the XML of older files writes 3 values)."""
    if isinstance(a, list) and isinstance(b, list):
        n = min(len(a), len(b))
        return all(abs(x - y) <= tolerance * max(1.0, abs(x)) for x, y in zip(a[:n], b[:n]))
    if isinstance(a, float) and isinstance(b, float):
        return abs(a - b) <= tolerance * max(1.0, abs(a))
    return a == b


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] not in ("audit", "instances", "proof-plan", "proof-sheet", "chains", "record", "decals",
                                   "frames", "rebuild-set", "rebuild-convert", "cover", "bodies", "no-nanite"):
        print(__doc__)
        return 2
    if argv[0] == "audit":
        result = audit()
    elif argv[0] == "instances":
        result = instances(tuple(argv[2].split(",")) if len(argv) > 2 and argv[1] == "--candidate" else ())
    elif argv[0] == "chains":
        result = chains()
    elif argv[0] == "frames":
        result = frames(argv[1:])
    elif argv[0] == "cover":
        from . import cover_materials
        result = cover_materials.main(argv[1:])
    elif argv[0] == "rebuild-set":
        result = rebuild_set()
    elif argv[0] == "rebuild-convert":
        result = rebuild_convert()
    elif argv[0] == "bodies":
        from . import bodies
        result = bodies.main(argv[1:])
    elif argv[0] == "no-nanite":
        from . import nanite
        result = nanite.write()
    elif argv[0] == "decals":
        from . import decals
        result = decals.main(argv[1:])
    elif argv[0] == "proof-sheet":
        result = proof_sheet(argv[1] if len(argv) > 1 else "m1")
    elif argv[0] == "proof-plan":
        stage = argv[1] if len(argv) > 1 else "m1"
        result = (proof_plan_test(stage) if stage in ("m3", "m4", "m6") else proof_plan_foliage(stage)
                  if stage == "foliage" else proof_plan(stage))
    else:
        mtl = argv[1].lower()
        subs = sub_materials(mtl)
        indices = [int(argv[2])] if len(argv) > 2 else range(len(subs))
        result = {}
        for i in indices:
            record = source_record(mtl, i, subs[i])
            rules = render_rules(record)
            result[i] = {"record": record, "render key": render_key(rules), "render rules": rules,
                         "legacy key": None if materials.not_drawn(subs[i]) else legacy_key(subs[i])}
    print(json.dumps(result, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
