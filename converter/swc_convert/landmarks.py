"""The landmarks of the world (AST-209, research/maelstrom.md): far meshes that the client shows as one persistent actor, not as
cell instances. The sky maelstrom of the Shattered Mountain: the six mesh instances of the capital ShatteredMountainVoretex
(r_+04_+04), their two pivots (CenterPivot_Top stays still; CenterPivot_Bottom turns about its local Z: the script
transformovertime of VortexScale), the material override of each placement, and the TexMod of each texture slot.

Usage: python3 -B -m swc_convert.landmarks write
Output: SWC_CONTENT/landmarks/maelstrom.json, the LOD 0 glTFs of the five meshes (landmarks/maelstrom/meshes/), and the PNGs of
the textures (the texture records of the materials pipeline). tools/import-landmarks.sh imports them into
/SwcGameContent/Landmarks/Maelstrom; World/SwcLandmarks.cpp places them."""
from __future__ import annotations

import json
import math
import shutil
import sys
import xml.etree.ElementTree as ET

import numpy as np

from . import env, manifest, material_records, materials, region, source, space

MAELSTROM = {
    "region": "r_+04_+04",
    "slice": "coatgen/ddc9dbfa/shatteredmountainvoretex_740c9ad19b46c2a54e564968055a643e.dynamicslice",
    "meshes": "objects/vfx/mesh/shattered_mountain/",
    "pivots": {"top": "CenterPivot_Top", "bottom": "CenterPivot_Bottom"},
}
# The rotation of CenterPivot_Bottom (scripts/test/transformovertime.luac on VortexScale; research/maelstrom.md "Entity motion"):
# each 0.05 s the source local rotation gets (0, 0, -0.0025) rad (no delta time; the interval accumulator resets to 0). The yaw
# sign changes in Unreal: +0.0025 rad = +0.143239 degrees about the local Z of the pivot each step (one turn: 125.664 s).
ROTATION = {"pivot": "bottom", "step seconds": 0.05, "step degrees": math.degrees(0.0025)}
# The mesh folders of the landmarks: cells.write leaves them out of the cells (only the maelstrom places these meshes in the world).
LANDMARK_MESHES = (MAELSTROM["meshes"],)
PARENT_FIELD = source.field_id("m_parentId")
UNREAL_FOLDER = "/SwcGameContent/Landmarks/Maelstrom"


def out_dir():
    return env.content_dir() / "landmarks"


def slice_entities(path: str) -> dict[int, dict]:
    """@return id -> {name, parent, tm (4 x 4, the m_worldTM of the slice)} of each entity of a slice."""
    out = {}
    for e in source.walk(source.parse(source.read(path))[0]):
        comps = e.child(region.COMPONENTS)
        if comps is None:
            continue
        name = e.child(region.NAME)
        tm, parent = None, None
        for comp in comps.children:
            for f in comp.children:
                if f.name == region.WORLD_TM and f.value is not None and len(f.value) == 48:
                    tm = region.az_matrix(f.value)
                if f.name == PARENT_FIELD:
                    c = f.children[0] if f.children else f
                    parent = int.from_bytes(c.value, "big") if c.value and len(c.value) == 8 else None
        out[region.entity_id(e)] = {"name": source.text(name) if name is not None else "", "parent": parent, "tm": tm}
    return out


def matrix4(m) -> np.ndarray:
    m = np.asarray(m, dtype=np.float64)
    if m.shape == (4, 4):
        return m
    out = np.eye(4)
    out[:3, :4] = m[:3, :4]
    return out


def lod0_mesh(product: str) -> dict:
    """Writes the glTF of a converted mesh with its first primitive only (the converter put the source $lod nodes into the same
    mesh as more primitives: they would draw as more layers) and @return its record (the file and the Unreal folder)."""
    entry = manifest.load()["assets"][product]
    src = env.content_dir() / entry["file"]
    doc = json.loads(src.read_text())
    for mesh in doc["meshes"]:
        mesh["primitives"] = mesh["primitives"][:1]
    stem = product.rsplit("/", 1)[-1][:-4]
    folder = out_dir() / "maelstrom" / "meshes"
    folder.mkdir(parents=True, exist_ok=True)
    for buffer in doc.get("buffers", []):
        uri = buffer.get("uri", "")
        if uri and not uri.startswith("data:"):
            name = stem + "_" + uri.rsplit("/", 1)[-1]
            shutil.copyfile(src.parent / uri, folder / name)
            buffer["uri"] = name
    out = folder / (stem + ".gltf")
    out.write_text(json.dumps(doc))
    return {"file": str(out.relative_to(env.content_dir())), "unreal": f"{UNREAL_FOLDER}/Meshes/{stem}",
            "triangles": sum(doc["accessors"][p["indices"]]["count"] // 3 for m in doc["meshes"] for p in m["primitives"])}


def texmod(texture: ET.Element) -> dict:
    """@return The UV rule of a texture slot: tile, offset, and the rotation (type 2: angle = t x 1000 x amplitude degrees +
    phase, about the centre; research/maelstrom.md "Texture motion") as degrees per second."""
    out = {"tile": [1.0, 1.0], "offset": [0.0, 0.0], "center": [0.5, 0.5], "degrees per second": 0.0}

    def number(value, default):
        try:
            return float(value)
        except (TypeError, ValueError):
            return default
    for mod in texture.iter("TexMod"):
        out["tile"] = [number(mod.get("TileU"), 1.0), number(mod.get("TileV"), 1.0)]
        out["offset"] = [number(mod.get("OffsetU"), 0.0), number(mod.get("OffsetV"), 0.0)]
        out["center"] = [number(mod.get("TexMod_URotateCenter"), 0.5), number(mod.get("TexMod_VRotateCenter"), 0.5)]
        if int(number(mod.get("TexMod_RotateType"), 0)) == 2:
            out["degrees per second"] = 1000.0 * number(mod.get("TexMod_WRotateAmplitude"), 0.0)
    # TileU and OffsetU can also be attributes of the TexMod element or of the Texture element (the core material).
    for axis, index in (("U", 0), ("V", 1)):
        for holder in [texture] + list(texture.iter("TexMod")):
            if holder.get("Tile" + axis):
                out["tile"][index] = number(holder.get("Tile" + axis), 1.0)
            if holder.get("Offset" + axis):
                out["offset"][index] = number(holder.get("Offset" + axis), 0.0)
    return out


def numbers(text: str | None) -> list[float]:
    return [float(v) for v in text.split(",")] if text else []


def texture_record(product: str, kind: str, textures: dict) -> str:
    """Writes the PNG of a texture (the rules of the materials pipeline) and @return its key; `kind`: srgb (a color sampler with
    alpha), normal, data (linear RGBA), linear (one channel: red)."""
    if kind in ("srgb", "normal"):
        record = material_records.color_texture(product, "normal" if kind == "normal" else "base")
    elif kind == "data":
        record = material_records.data_texture(product)
    else:
        record = material_records.linear_texture(product)
    key = record["asset"]
    textures[key] = {"file": record["file"], "color": record["color"], "cap": record["cap"],
                     "unreal": manifest.unreal_folder("texture", key)}
    return key


def material_spec(mtl: str, textures: dict) -> dict:
    """@return The instance spec of a vortex material (parent "vortex layer" for Illum, "vortex core" for Fxmeshadvancedtransp)."""
    element = ET.fromstring(source.read(mtl))
    shader = element.get("Shader", "")
    public = element.find("PublicParams")
    params = dict(public.attrib) if public is not None else {}
    slots = {t.get("Map"): t for t in element.iter("Texture")}
    spec = {"source": mtl, "shader": shader, "textures": {}, "uv": {}, "scalars": {}, "vectors": {}}
    emittance = numbers(element.get("Emittance")) + [0.0] * 4
    spec["vectors"]["EmittanceColor"] = emittance[:4]
    spec["scalars"]["Opacity"] = float(element.get("Opacity", "1"))
    if shader == "Illum":
        spec["parent"] = "vortex layer"
        spec["vectors"]["DiffuseColor"] = (numbers(element.get("Diffuse")) + [1.0] * 4)[:3] + [1.0]
        spec["scalars"]["EmittanceMapGamma"] = float(params.get("EmittanceMapGamma", "1"))
        spec["scalars"]["BackDiffuseMultiplier"] = float(params.get("BackDiffuseMultiplier", "0"))
        spec["vectors"]["TransmittanceColor"] = (numbers(params.get("TransmittanceColor")) + [0.0] * 4)[:4]
        spec["scalars"]["NormalViewDependency"] = float(params.get("NormalViewDependency", "0"))
        for slot, parameter, kind in (("Diffuse", "Diffuse", "srgb"), ("Bumpmap", "Normal", "normal"),
                                      ("Emittance", "Emittance", None), ("Decal", "Intensity", None),
                                      ("Opacity", "Transmission", "linear")):
            texture = slots.get(slot)
            product = materials.texture_product(texture) if texture is not None else None
            if product is None:
                continue
            if kind is None:   # the emission maps by the color space of the file (AST-207): an sRGB file decodes, a linear file stays
                kind = "srgb" if material_records.file_srgb(product) else "data"
                spec["scalars"][parameter + "Srgb"] = 1.0 if kind == "srgb" else 0.0
            spec["textures"][parameter + ("Linear" if kind == "data" else "")] = texture_record(product, kind, textures)
            spec["uv"][parameter] = texmod(texture)
        # The rim terms of the source (%RIM_DIFFUSE_LIGHTING): kept in the spec for a later rim path [I].
        spec["rim"] = {k: v for k, v in params.items() if k.lower().startswith("rim")}
    else:
        spec["parent"] = "vortex core"
        spec["scalars"]["GlobalColorFalloff"] = float(params.get("GlobalColorFalloff", "1"))
        spec["scalars"]["GlobalAlphaFalloff"] = float(params.get("GlobalAlphaFalloff", "1"))
        texture = slots.get("Custom")   # the active source map of the zero-flag shader variant ($CustomMap, sRGB lookup)
        product = materials.texture_product(texture)
        spec["textures"]["Source"] = texture_record(product, "srgb", textures)
        spec["uv"]["Source"] = texmod(texture)
    return spec


def write() -> dict:
    """Writes SWC_CONTENT/landmarks/maelstrom.json. @return A report."""
    entities = slice_entities(MAELSTROM["slice"])
    by_name = {}
    for ident, e in entities.items():
        by_name.setdefault(e["name"], []).append(ident)
    placements, _ = region.Region(MAELSTROM["region"]).placements()
    parts = [p for p in placements if p["source"] == "capital" and p["mesh"].startswith(MAELSTROM["meshes"])]
    # The capital matrix C (T(capital) S(75)): placement = C x m_worldTM of the slice.
    capitals = [matrix4(p["matrix"]) @ np.linalg.inv(entities[p["entity"]]["tm"]) for p in parts]
    capital = capitals[0]
    spread = max(float(np.abs(c - capital).max()) for c in capitals)
    pivots = {}
    for key, name in MAELSTROM["pivots"].items():
        ident = by_name[name][0]
        pivots[key] = {"name": name, "entity": str(ident), "world": space.unreal_transform(capital @ entities[ident]["tm"])}
    pivot_of = {int(pivots[k]["entity"]): k for k in pivots}
    textures, specs, meshes, out_parts = {}, {}, {}, []
    for p in parts:
        mesh = p["mesh"]
        if mesh not in meshes:
            meshes[mesh] = lod0_mesh(mesh)
        material = p["material"]
        if material not in specs:
            specs[material] = material_spec(material, textures)
        parent = entities[p["entity"]]["parent"]
        out_parts.append({"name": entities[p["entity"]]["name"], "entity": str(p["entity"]), "mesh": mesh, "material": material,
                          "pivot": pivot_of.get(parent, "top"), "world": space.unreal_transform(matrix4(p["matrix"]))})
    doc = {"landmark": "maelstrom", "folder": UNREAL_FOLDER, "region": MAELSTROM["region"], "slice": MAELSTROM["slice"],
           "draw range m": 10000, "rotation": ROTATION, "pivots": pivots, "parts": out_parts, "meshes": meshes,
           "materials": specs, "textures": textures}
    out = out_dir() / "maelstrom.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1))
    return {"file": str(out), "parts": len(out_parts), "meshes": {m.rsplit("/", 1)[-1]: v["triangles"] for m, v in meshes.items()},
            "materials": len(specs), "textures": len(textures), "capital spread": round(spread, 6),
            "pivots of the parts": sorted({p["pivot"] for p in out_parts})}


def main(argv: list[str]) -> int:
    if argv[:1] == ["write"]:
        print(json.dumps(write(), indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
