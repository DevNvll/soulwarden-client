"""The terrain materials of the game for the Landscapes (world fidelity T0; research/terrain.md, sections 1, 2, and 6).

The world material (WORLDMAT) has a table of the regions (Tile X, Tile Y -> a .regionmat). A region material has an
ordered list of layers (a layer material .mtl, a splat map, AffectedTiles, Priority), a default material, and three
macro maps (color, normal, gloss). A layer material (shader Terraintilecomposite) has its textures (Diffuse, Bumpmap,
Smoothness, Specular, Heightmap) with their TexMod (TileU, TileV) and its PublicParams (the height blend: height scale
and offset, blend factor, falloff; the macro blend strengths). The splats are independent weights (no normalization):
the research recovers the alpha of the game shader (section 2, "Height blend").

Outputs (content folder):
- terrain/materials.json: each layer material: source path, root fields, PublicParams, and its textures with their
  role, color space, TexMod, DDS format and size, source hash, and the full-resolution PNG.
- terrain/textures/PRODUCT.png (and PRODUCT+gloss.png for a gloss in the .a part): full source resolution. Color maps
  (Diffuse, Specular) are sRGB; normals keep signed X and Y in R and G (Z rebuilt in B); gloss and heights are data.
- regions/NAME/layers.json: the world material link, the ordered layer list, the default material, the macro maps, the
  custom macro parameter switches, and the source hashes.
- regions/NAME/layers/SPLAT.r8: the original 2,048 x 2,048 weight (u8, row 0 = north, column 0 = west), decoded with
  the corrected BC4 rule; a BC1 splat (two custom maps of region 3_4) gives its red channel.
- regions/NAME/landscape-layers/SPLAT.r8: the same weight on the 2,041 x 2,041 vertex grid of the Landscape (the rule
  of terrain.landscape: the 2,048 samples plus the shared edge of the east and north neighbors with the same splat
  name, else the own edge; bilinear; row 0 = north).
- regions/NAME/macro/{color,normal,gloss}.png: the three macro maps at 2,048 x 2,048 (row 0 = north).
A layer whose splat is a small placeholder (16 x 16, an unused entry) has no weight file ("placeholder" in the list).
"""
from __future__ import annotations

import hashlib
import json
import re
import struct
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

from . import dds, env, source

WORLDMAT = "materials/terrain/newworld_vitaeeterna/jav_newworld_vitaeeterna.worldmat"


@dataclass(frozen=True)
class Terrain:
    """A terrain of the game: its world material, and the content folders of its regions and of its table of layer
    materials. The textures of the layer materials are shared (terrain/textures, written once for each product)."""
    worldmat: str
    regions: str
    table: str


# The world (the default of each function), and the terrain of the main menu level (stage F4b of docs/FRONTEND.md: the
# ground of the menu scene; its own folders, so that no file of the world changes).
WORLD = Terrain(WORLDMAT, "regions", "terrain/materials.json")
TERRAINS = {"newworld_vitaeeterna": WORLD,
            "nw_frontend_base": Terrain("materials/terrain/nw_frontend_base/nw_frontend_base.worldmat", "frontend/terrain/regions",
                                        "frontend/terrain/materials.json")}
VERSION = 1
SIZE = 2048
VERTICES = 2041
# The slots of a layer material: the role of the texture and the color space of its values.
SLOTS = {"Diffuse": ("diffuse", "sRGB"), "Specular": ("specular", "sRGB"), "Bumpmap": ("normal", "data"),
         "Smoothness": ("gloss", "data"), "Heightmap": ("height", "data")}


def content() -> Path:
    return env.content_dir()


def asset_ref(value: str | None, ending: str = "") -> str | None:
    """@return The pak path of an asset value "id={GUID}:SUB,type={...},hint={PATH}": the catalog first, else the hint."""
    if not value:
        return None
    match = re.search(r"id=(\{[0-9A-Fa-f-]+\}(?::[0-9A-Fa-f]+)?)", value)
    path = source.product_of_text(match.group(1), ending) if match else None
    if path is None:
        hint = re.search(r"hint=\{([^}]*)\}", value)
        path = hint.group(1).lower() if hint else None
        if path and ending and not path.endswith(ending):
            path = None
    return path


def fields(node) -> dict[str, str]:
    return {child.get("field"): child.get("value") for child in node if child.get("field")}


def source_hash(path: str, alpha: bool = False) -> str:
    """@return A hash of the source parts of a DDS (its head and all its split mip files)."""
    digest = hashlib.sha1()
    paks = source.paks()
    for part in [path + (".a" if alpha else "")] + [f"{path}.{i}{'a' if alpha else ''}" for i in range(1, 16)]:
        if paks.has(part):
            digest.update(source.read(part))
    return digest.hexdigest()[:16]


def dds_meta(path: str, alpha: bool = False) -> dict:
    head = source.read(path + (".a" if alpha else ""))
    height, width = struct.unpack_from("<II", head, 12)
    fmt = struct.unpack_from("<I", head, 128)[0] if head[84:88] == b"DX10" else head[84:88].decode("latin-1")
    return {"width": width, "height": height, "dxgi": fmt, "mips": struct.unpack_from("<I", head, 28)[0]}


def region_materials(terrain: Terrain = WORLD) -> dict[str, str]:
    """@return For each region name (r_+XX_+YY) its .regionmat (the Regions table of the world material)."""
    root = ET.fromstring(source.read(terrain.worldmat))[0]
    table = next(node for node in root if node.get("field") == "Regions")
    out = {}
    for entry in table:
        f = {x.get("field"): x.get("value") for x in entry.iter() if x.get("field")}
        x, y = int(f["Tile X"]), int(f["Tile Y"])
        out[f"r_+{x:02d}_+{y:02d}"] = asset_ref(f.get("Layers"), ".regionmat")
    return out


def splat_name(path: str) -> str:
    """@return The layer name of a splat path (jav_newworld_vitaeeterna_4.2.roads_splat.dds -> roads_splat)."""
    return path.rsplit("/", 1)[-1].rsplit(".dds", 1)[0].split(".", 2)[-1]


def region_layers(name: str, terrain: Terrain = WORLD) -> dict:
    """@return The layer list of a region (in the order of the source), its default material, and its macro maps."""
    path = region_materials(terrain).get(name)
    out = {"region": name, "version": VERSION, "worldmat": terrain.worldmat, "regionmat": path, "layers": []}
    if not path:
        return out
    root = ET.fromstring(source.read(path))[0]
    layers = next(node for node in root if node.get("field") == "Layers")
    for index, node in enumerate(layers):
        f = fields(node)
        splat = asset_ref(f.get("SplatMap"), ".dds")
        out["layers"].append({"order": index, "name": splat_name(splat) if splat else f"layer_{index}",
                              "material": asset_ref(f.get("Material"), ".mtl"), "splat": splat,
                              "priority": int(f.get("Priority", 255)), "affected tiles": int(f.get("AffectedTiles", 0))})
    extra = fields(root)
    out["default material"] = asset_ref(extra.get("Default Material"), ".mtl")
    out["macro"] = {role: asset_ref(extra.get(field), ".dds") for role, field in
                    (("color", "Macro ColorMap"), ("normal", "Macro NormalMap"), ("gloss", "Macro GlossMap"))}
    out["custom macro params"] = {"background": extra.get("EnableCustomBackgroundParams") == "true",
                                  "foreground": extra.get("EnableCustomForegroundParams") == "true"}
    return out


def decode(path: str, alpha: bool = False, max_size: int = 4096) -> tuple[np.ndarray, dict]:
    """@return (an array of the full image: (h, w) for one channel, (h, w, c) else, values 0..1) and its DDS meta."""
    data, w, h, fmt = dds.join(source.read, path, max_size, alpha)
    meta = {"width": w, "height": h, "dxgi": fmt}
    if fmt in (80, 81, 83, 84):
        array = dds.decode_bc45(data[148:], w, h, fmt)
        return (array[:, :, 0] if array.shape[2] == 1 else array), meta
    return np.asarray(dds.to_image(data, w, h, fmt)).astype(np.float32) / 255.0, meta


def write_png(array: np.ndarray, role: str, out: Path) -> None:
    """Writes a decoded texture by role: a normal keeps signed X and Y (0.5 = 0) in R and G and its rebuilt Z in B."""
    out.parent.mkdir(parents=True, exist_ok=True)
    if role == "normal" and array.ndim == 3 and array.shape[2] >= 2:
        x, y = array[:, :, 0] * 2 - 1, array[:, :, 1] * 2 - 1
        z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
        array = np.stack([(x + 1) / 2, (y + 1) / 2, (z + 1) / 2], 2)
    if array.ndim == 3 and array.shape[2] == 4 and float(array[:, :, 3].min()) >= 1.0:
        array = array[:, :, :3]
    image = (np.clip(array, 0, 1) * 255 + 0.5).astype(np.uint8)
    temporary = out.with_name("." + out.name + ".tmp.png")
    Image.fromarray(image).save(temporary)
    temporary.replace(out)


def material_record(path: str) -> dict:
    """@return The record of a layer material, and writes its textures at full resolution (once)."""
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    params = root.find("PublicParams")
    record = {"source": path, "shader": root.get("Shader", ""),
              "fields": {k: root.get(k) for k in ("Diffuse", "Specular", "Shininess", "SurfaceType", "MtlFlags", "StringGenMask")
                         if root.get(k) is not None},
              "public params": dict(params.attrib) if params is not None else {}, "textures": []}
    for texture in root.findall("./Textures/Texture"):
        slot = texture.get("Map", "")
        role, space = SLOTS.get(slot, (slot.lower(), "data"))
        product = (source.product_of_text(texture.get("AssetId", ""), ".dds") if texture.get("AssetId") else None) \
            or (texture.get("File", "").lower().replace("\\", "/").rsplit(".", 1)[0] + ".dds")
        # A Smoothness that names a _ddna product is the separate gloss texture (.a) of that product.
        alpha = role == "gloss" and source.has(product + ".a")
        entry = {"slot": slot, "role": role, "color space": space, "product": product, "alpha part": alpha,
                 "texmod": dict(texture.find("TexMod").attrib) if texture.find("TexMod") is not None else {}}
        if not source.has(product):
            entry["error"] = "not in the pak files of the game copy"
            record["textures"].append(entry)
            continue
        entry.update(dds_meta(product, alpha))
        entry["source hash"] = source_hash(product, alpha)
        out = content() / "terrain" / "textures" / (product[:-4] + ("+gloss" if alpha else "") + ".png")
        if not out.is_file():
            try:
                array, _meta = decode(product, alpha)
                write_png(array, role, out)
            except Exception as error:   # a format that the converter does not decode: the record says it
                entry["error"] = str(error)[:120]
        entry["file"] = str(out.relative_to(content())) if out.is_file() else None
        record["textures"].append(entry)
    return record


def resample(weights: np.ndarray, east: np.ndarray | None, north: np.ndarray | None, corner: np.ndarray | None) -> np.ndarray:
    """@return A 2,048 x 2,048 weight (row 0 = north) on the 2,041 x 2,041 Landscape grid (row 0 = north): the rule of
    terrain.landscape (2,049 samples with the shared edge, bilinear)."""
    w = weights[::-1].astype(np.float64)   # row 0 = south (local y 0), as the heights
    grid = np.empty((SIZE + 1, SIZE + 1))
    grid[:SIZE, :SIZE] = w
    grid[:SIZE, SIZE] = east[::-1][:, 0] if east is not None else w[:, SIZE - 1]
    grid[SIZE, :SIZE] = north[::-1][0, :] if north is not None else w[SIZE - 1, :]
    grid[SIZE, SIZE] = corner[::-1][0, 0] if corner is not None else grid[SIZE - 1, SIZE - 1]
    t = np.linspace(0.0, SIZE, VERTICES)
    i0 = np.minimum(t.astype(int), SIZE - 1)
    f = t - i0
    rows = grid[i0, :] * (1 - f)[:, None] + grid[i0 + 1, :] * f[:, None]
    out = rows[:, i0] * (1 - f)[None, :] + rows[:, i0 + 1] * f[None, :]
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)[::-1]


def neighbor(name: str, dx: int, dy: int) -> str:
    x, y = (int(v) for v in name[2:].split("_"))
    return f"r_+{x + dx:02d}_+{y + dy:02d}"


def splat_weights(path: str) -> np.ndarray | None:
    """@return The 2,048 x 2,048 u8 weight of a splat (row 0 = north), or None for a placeholder."""
    array, meta = decode(path, max_size=SIZE)
    if meta["width"] != SIZE or meta["height"] != SIZE:
        return None
    if array.ndim == 3:
        array = array[:, :, 0]   # a BC1 splat: the red channel (research 1)
    return np.clip(np.rint(array * 255), 0, 255).astype(np.uint8)


def export_region(name: str, terrain: Terrain = WORLD) -> dict:
    """Writes the terrain material data of one region and the records of its materials. @return A report."""
    info = region_layers(name, terrain)
    folder = content() / terrain.regions / name
    (folder / "layers").mkdir(parents=True, exist_ok=True)
    (folder / "landscape-layers").mkdir(parents=True, exist_ok=True)
    report = {"region": name, "layers": len(info["layers"]), "weights": 0, "placeholders": 0, "errors": []}
    own = {}
    for layer in info["layers"]:
        if not layer["splat"] or not source.has(layer["splat"]):
            layer["error"] = "no splat in the pak files"
            report["errors"].append(layer["name"])
            continue
        layer["splat meta"] = dds_meta(layer["splat"])
        layer["source hash"] = source_hash(layer["splat"])
        weights = splat_weights(layer["splat"])
        if weights is None:
            layer["placeholder"] = True
            report["placeholders"] += 1
            continue
        own[layer["name"]] = weights
        weights.tofile(folder / "layers" / f"{layer['name']}.r8")
        layer.update({"weights": f"layers/{layer['name']}.r8", "nonzero": int(np.count_nonzero(weights)),
                      "mean": round(float(weights.mean()) / 255, 6)})
        report["weights"] += 1
    # The Landscape grid needs the shared edge of the east, north, and northeast neighbors (the same splat name).
    neighbors = {}
    for key, (dx, dy) in {"east": (1, 0), "north": (0, 1), "corner": (1, 1)}.items():
        other = region_layers(neighbor(name, dx, dy), terrain)
        neighbors[key] = {layer["name"]: layer["splat"] for layer in other["layers"] if layer.get("splat")}
    for layer in info["layers"]:
        weights = own.get(layer["name"])
        if weights is None:
            continue
        edges = {}
        for key in ("east", "north", "corner"):
            path = neighbors[key].get(layer["name"])
            edges[key] = splat_weights(path) if path and source.has(path) else None
        resample(weights, edges["east"], edges["north"], edges["corner"]).tofile(
            folder / "landscape-layers" / f"{layer['name']}.r8")
        layer["landscape weights"] = f"landscape-layers/{layer['name']}.r8"
        layer["shared edges"] = {key: edges[key] is not None for key in edges}
    for role, path in (info.get("macro") or {}).items():
        if not path or not source.has(path):
            continue
        array, meta = decode(path, max_size=SIZE)
        write_png(array, "normal" if role == "normal" else role, folder / "macro" / f"{role}.png")
        info["macro"][role] = {"source": path, "file": f"macro/{role}.png", "color space": "sRGB" if role == "color" else "data",
                               "source hash": source_hash(path), **meta}
    info["grid"] = {"weights": f"{SIZE} x {SIZE} u8, row 0 = north, column 0 = west",
                    "landscape weights": f"{VERTICES} x {VERTICES} u8, row 0 = north (the vertices of landscape.r16)"}
    (folder / "layers.json").write_text(json.dumps(info, indent=1))
    # The records of the layer materials (terrain/materials.json, one write).
    table_path = content() / terrain.table
    table_path.parent.mkdir(parents=True, exist_ok=True)
    table = json.loads(table_path.read_text()) if table_path.is_file() else {"version": VERSION, "materials": {}}
    wanted = {layer["material"] for layer in info["layers"] if layer.get("material")}
    if info.get("default material"):
        wanted.add(info["default material"])
    for path in sorted(wanted - set(table["materials"])):
        try:
            table["materials"][path] = material_record(path)
        except Exception as error:
            table["materials"][path] = {"source": path, "error": str(error)[:120]}
    temporary = table_path.with_suffix(".tmp")
    temporary.write_text(json.dumps(table, indent=1))
    temporary.replace(table_path)
    report["materials"] = len(wanted)
    return report


# Groups of layer names for a data-location map (T0 exit check): the road layers, the grass layers, the soil layers.
GROUPS = {"road": ("roads_splat", "mountainroads_splat", "coastroads_splat", "damnedroads_splat"),
          "grass": ("grass_splat", "rough_splat", "forestgrass_splat", "mountainrough_splat", "deadgrass_splat",
                    "deadrough_splat", "deadforestgrass_splat", "deadmountainrough_splat", "coastgrass_splat",
                    "coastrough_splat"),
          "soil": ("soil_splat", "coastsoil_splat", "sand_splat", "lakeedge_splat", "marshmud_splat")}
GROUP_COLORS = {"road": (222, 120, 30), "grass": (40, 170, 40), "soil": (150, 110, 70)}


def layer_map(name: str, area: tuple[float, float, float, float], out: Path, marks: list[dict], scale: int = 4) -> dict:
    """Writes a labeled map of the raw splat weights of a region (no blend: data location only): the macro color map,
    dimmed, and over it the group with the largest weight where that weight is at least 0.35 (road, grass, soil).
    `area` = (x0, y0, x1, y1) in engine meters; `marks`: {"label", "x", "y", "heading" (optional), "color"}.
    @return The share of each group in the area."""
    from PIL import ImageDraw
    folder = content() / "regions" / name
    ox, oy = (int(v) * SIZE for v in name[2:].split("_"))
    x0, y0, x1, y1 = area
    c0, c1 = int(x0 - ox), int(x1 - ox)
    r0, r1 = int(SIZE - 1 - (y1 - oy)), int(SIZE - 1 - (y0 - oy))   # row 0 = north
    macro = np.asarray(Image.open(folder / "macro" / "color.png").convert("RGB"))[r0:r1, c0:c1].astype(np.float32)
    weights = {}
    for group, layers in GROUPS.items():
        stack = [np.fromfile(folder / "layers" / f"{n}.r8", np.uint8).reshape(SIZE, SIZE)[r0:r1, c0:c1]
                 for n in layers if (folder / "layers" / f"{n}.r8").is_file()]
        weights[group] = np.max(stack, axis=0).astype(np.float32) / 255 if stack else np.zeros(macro.shape[:2], np.float32)
    names = list(weights)
    stack = np.stack([weights[g] for g in names])
    best = stack.argmax(0)
    strong = stack.max(0) >= 0.35
    image = macro * 0.45
    shares = {}
    for i, group in enumerate(names):
        mask = strong & (best == i)
        image[mask] = np.array(GROUP_COLORS[group], np.float32) * 0.85 + macro[mask] * 0.15
        shares[group] = round(float(mask.mean()), 3)
    picture = Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)).resize(((c1 - c0) * scale, (r1 - r0) * scale), Image.NEAREST)
    draw = ImageDraw.Draw(picture)

    def to_px(x, y):
        return (x - x0) * scale, (y1 - y) * scale

    for mark in marks:
        px, py = to_px(mark["x"], mark["y"])
        color = tuple(mark.get("color", (255, 255, 255)))
        draw.ellipse([px - 6, py - 6, px + 6, py + 6], outline=color, width=3)
        if "heading" in mark:
            import math
            a = math.radians(mark["heading"])
            draw.line([px, py, px + 40 * math.sin(a), py - 40 * math.cos(a)], fill=color, width=3)
        draw.text((px + 9, py - 6), mark["label"], fill=color)
    legend = [("road layers", GROUP_COLORS["road"]), ("grass layers", GROUP_COLORS["grass"]), ("soil layers", GROUP_COLORS["soil"]),
              ("macro color (dim): no layer >= 0.35", (120, 120, 120))]
    for i, (text, color) in enumerate(legend):
        draw.rectangle([10, 10 + 22 * i, 26, 26 + 22 * i], fill=tuple(color))
        draw.text((32, 12 + 22 * i), text, fill=(255, 255, 255))
    draw.text((10, picture.height - 20), f"{name}: x {x0:.0f}..{x1:.0f}, y {y0:.0f}..{y1:.0f} (engine m), north up, "
              f"{scale} px/m, raw splats (corrected BC4)", fill=(255, 255, 255))
    out.parent.mkdir(parents=True, exist_ok=True)
    picture.save(out)
    return shares
