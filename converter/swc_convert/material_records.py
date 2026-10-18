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
    "%IGNORE_BLEND_VERTEX_ALPHA",
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
}

# Color spaces of the sampler roles (research section 3, "sRGB rules"): srgb = sRGB RGB and linear alpha; normal = the
# normal map compression; linear = data; raw = the source sampler has no sRGB lookup (emittance).
COLOR = {"diffuse": "srgb", "normal": "normal", "gloss": "linear", "specular": "srgb", "detail": "srgb",
         "diffuse2": "srgb", "normal2": "normal", "gloss2": "linear", "specular2": "linear", "blend mask": "linear",
         "overlay mask": "data", "transmission": "linear", "decal opacity": "linear", "height": "linear",
         "height2": "linear", "emittance": "raw", "emittance intensity": "raw", "occlusion": "linear",
         "tint mask": "srgb", "tint color": "srgb", "dirt": "srgb", "cloud gloss": "linear"}


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


def flags_of(element: ET.Element) -> tuple[list[str], list[str]]:
    """@return (the flags of a sub-material, the flags that are in only one of the two sources). The material loader
    of the engine uses StringGenMask when the material has it, else the bits of the numeric GenMask (through the `.ext`
    of its shader). The generated materials of the merged chunks (coatgen) have a GenMask that does not agree with
    their StringGenMask (for example 80000001 for %NORMAL_MAP%BLENDLAYER%DETAIL_MAPPING...): the text is the rule.
    Old names become the current names."""
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
        return sorted(bits), []
    return sorted(named), sorted(named ^ bits) if bits else []


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
    """@return The family of a shader: illum, vegetation, glass, or other (effects, hair, skin: legacy rules)."""
    s = shader.lower()
    return s if s in ("illum", "vegetation", "glass") else "other"


def source_record(mtl: str, index: int, element: ET.Element | None) -> dict:
    """@return The rich record of one sub-material (`element` None: the default material of a missing slot)."""
    if element is None:
        return {"mtl": mtl, "index": index, "missing": True, "shader": "", "family": "other", "flags": [],
                "lighting": {}, "params": {}, "textures": {}}
    flags, differ = flags_of(element)
    record: dict = {
        "mtl": mtl, "index": index, "name": element.get("Name", ""), "surface": element.get("SurfaceType", ""),
        "shader": element.get("Shader", ""), "family": family_of(element.get("Shader", "")), "flags": flags,
        "genMask": element.get("GenMask", ""), "mtlFlags": int(element.get("MtlFlags", "0") or 0),
        "lighting": {k: number(element.get(k)) for k in ("Diffuse", "Specular", "Emittance", "Opacity", "AlphaTest",
                                                          "Shininess", "Emissive") if element.get(k) is not None},
        "params": {}, "textures": {}}
    if differ:
        record["flag differences"] = differ
    params = element.find("PublicParams")
    if params is not None:
        record["params"] = {k: number(v) for k, v in sorted(params.attrib.items())}
    for t in element.iter("Texture"):
        slot = t.get("Map", "")
        if slot in record["textures"]:
            continue   # the first texture of a slot (as materials.shared_material)
        product = materials.texture_product(t)
        role, rule = role_of(record["shader"], slot, set(flags))
        entry = {"file": t.get("File", ""), "assetId": t.get("AssetId", ""), "product": product,
                 "role": role, "rule": rule}
        if role is not None:
            entry["color"] = COLOR[role]
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


def blend_of(record: dict) -> tuple[str, float | None]:
    """@return (opaque, masked, or translucent; the cutoff) of a record, with the rules of materials.shared_material."""
    if record["shader"] == "Glass":
        return "translucent", None
    cutoff = record["lighting"].get("AlphaTest", 0.0)
    cutoff = cutoff if isinstance(cutoff, float) else 0.0
    return ("masked", round(cutoff, 3)) if cutoff > 0 else ("opaque", None)


def render_rules(record: dict, normal_map: bool = True) -> dict:
    """@return The render rules of a rich record: the values that change the picture (no names, no inactive slots).
    `normal_map` False: the rule of the far meshes (no normal map, terrain.impostors)."""
    if record.get("missing"):
        return {"missing": True}
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
          "glass": record["family"] == "glass"}
    params = {}
    for feature, names in PARAMS.items():
        if on[feature]:
            params.update({n: record["params"][n] for n in names if n in record["params"]})
    rules["params"] = dict(sorted(params.items()))
    return rules


def render_key(rules: dict) -> str:
    return "r_" + hashlib.sha1(json.dumps(rules, sort_keys=True).encode()).hexdigest()[:10]


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
def sub_materials(mtl: str) -> tuple:
    root = ET.fromstring(source.read(mtl).decode("utf-8", "replace"))
    subs = root.find("SubMaterials")
    return tuple(subs) if subs is not None else (root,)


def primitive_slots(mesh: str, gltf_file: Path) -> list[tuple[int, str]] | None:
    """@return The (source sub-material index, glTF material name) of each primitive of a converted mesh, in the order
    of mesh.convert (the mesh chunks, their subsets, no subset that is not drawn), or None when the primitives do not
    match the subsets (the index counts are the check). Only the chunk headers: no .cgfheap."""
    from . import cgf
    f = cgf.read_chunks(source.read(mesh))
    reference = cgf.material_guid(f) or ""
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
            m = subs[mat] if 0 <= mat < len(subs) else None
            if m is not None and materials.not_drawn(m):
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
        out.append((mat, names[prim["material"]]))
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


def mesh_slots(item: tuple[str, str]) -> tuple[str, list | None, str | None]:
    """@return (mesh, its primitive slots or None, an error) for a worker process."""
    mesh, gltf_file = item
    try:
        return mesh, primitive_slots(mesh, env.content_dir() / gltf_file), None
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
    """@return (mesh, material) of each entity of a slice whose static mesh component names a material file: the
    material override of the component (research near-entity-evidence: for example the forge "off" and the lantern
    "on" materials of the chunks of Oxboro)."""
    from . import region as region_module
    out = []
    for e in source.walk(source.parse(data)[0]):
        comps = e.child(region_module.COMPONENTS)
        if comps is None:
            continue
        for comp in comps.children:
            for f in comp.children:
                if f.name != region_module.SMRN:
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
    meshes = sorted((k, e["file"]) for k, e in assets.items() if e["kind"] == "static" and k.endswith(".cgf"))
    sources: dict[str, dict] = {}           # "mtl#index" -> {"legacy", "far legacy", "render", "record", "use"}
    rules_of: dict[str, dict] = {}          # render key -> render rules
    slots: dict[str, list] = {}             # mesh -> [[source index, legacy key, render key], ...]
    failed = {}

    def add_source(mtl: str, index: int, element, use: str, normal_map: bool = True) -> dict | None:
        name = f"{mtl}#{index}"
        if element is not None and materials.not_drawn(element):
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
        reference = cgf.material_guid(cgf.read_chunks(source.read(mesh))) or ""
        mtl, subs = materials.load_mtl(reference, mesh) if reference else ("", [])
        out = []
        for index, name in sorted(set(pairs)):
            element = subs[index] if 0 <= index < len(subs) else None
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

SPEC_VERSION = 2      # the rule of the specs (2: the surface parents of M2)
DEPLOYED = ("m1", "m2", "m3a")  # the stages that passed their check: instances.json (the import pipeline applies it)
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


def data_texture(product: str) -> dict:
    """Writes all channels of a texture product as an RGBA PNG for linear data (once) and @return its record: an
    overlay mask (the Custom slot with %OVERLAY_MASK or %COLOR_SAMPLER_OVERLAY_MASK). Unreal imports it as linear
    data with alpha (TC_Masks)."""
    key = f"texture:{product}@data"
    out = env.content_dir() / "textures" / f"d{LINEAR_VERSION}-{CAP}" / (product[:-4] + ".png")
    if not out.is_file():
        from . import dds
        data, w, h, fmt = dds.join(source.read, product, CAP)
        image = dds.to_image(data, w, h, fmt).convert("RGBA")
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
    if rules.get("missing") or rules["blend"] not in ("masked", "opaque"):
        return None
    if rules["blend"] == "opaque" and not (rules["family"] == "illum" and "m2" in STAGES):
        return None
    maps = rules["textures"]

    def use(record: dict) -> str:
        textures[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
        return record["asset"]

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
        spec["vectors"]["TransmittanceColor"] = (params.get("TransmittanceColor") or [1.0, 1.0, 1.0])[:3] + [1.0]
        spec["scalars"]["BackDiffuseMultiplier"] = float(params.get("BackDiffuseMultiplier", 1.0))
        spec["scalars"]["Smoothness"] = max(1 / 127, float(rules["shininess"]) / 255)
        spec["scalars"]["Specular"] = min(1.0, luminance(rules["specular"]) / 0.08)
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
    spec["scalars"]["Smoothness"] = max(1 / 127, float(rules["shininess"]) / 255)
    flags = set(rules["flags"])
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
    textures: dict = {}
    summary: dict = {}
    for label, stages in (("instances", DEPLOYED), ("instances-candidate", tuple(candidate))):
        if not stages:
            continue
        STAGES = stages
        specs, errors, counts, limits = {}, {}, {}, {}
        for legacy, renders in sorted(uses.items()):
            main = sorted(renders.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]
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
            if spec is None:
                continue
            spec.update({"render": main, "source": source_of.get(main), "slots": renders[main],
                         "other render keys": {k: v for k, v in renders.items() if k != main}})
            specs[legacy] = spec
            kind = spec["parent"] or "base color only"
            counts[kind] = counts.get(kind, 0) + 1
            for limit in uv_limits(records["render rules"][main]):
                limits[limit] = limits.get(limit, 0) + 1
        write_json(out_dir() / f"{label}.json", {"version": SPEC_VERSION, "stages": list(stages), "instances": specs})
        summary[label] = {"stages": list(stages), "instances": len(specs), "by parent": counts,
                          "with other render keys": sum(1 for s in specs.values() if s["other render keys"]),
                          "UV limits": limits, "errors": len(errors), "first errors": dict(list(errors.items())[:5])}
    STAGES = DEPLOYED
    for key, record in textures.items():
        record["unreal"] = manifest.unreal_folder("texture", key)
    write_json(out_dir() / "textures.json", {"version": SPEC_VERSION, "textures": textures})
    summary.update({"textures": len(textures), "new textures (not in the manifest)": sum(1 for k in textures if k not in assets),
                    "seconds": round(time.time() - started, 1)})
    return summary


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
            element = subs[index] if index < len(subs) else None
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
    if not argv or argv[0] not in ("audit", "instances", "proof-plan", "proof-sheet", "chains", "record", "decals"):
        print(__doc__)
        return 2
    if argv[0] == "audit":
        result = audit()
    elif argv[0] == "instances":
        result = instances(tuple(argv[2].split(",")) if len(argv) > 2 and argv[1] == "--candidate" else ())
    elif argv[0] == "chains":
        result = chains()
    elif argv[0] == "decals":
        from . import decals
        result = decals.main(argv[1:])
    elif argv[0] == "proof-sheet":
        result = proof_sheet(argv[1] if len(argv) > 1 else "m1")
    elif argv[0] == "proof-plan":
        result = proof_plan(argv[1] if len(argv) > 1 else "m1")
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
