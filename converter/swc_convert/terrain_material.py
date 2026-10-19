"""The plan of the terrain material of a region (world fidelity T1; research/terrain.md, sections 2 and 6).

The game draws the layers of a region into its terrain tiles (TerrainTileCompositePS of terraintilecomposite.cfxb):
each layer material gives a diffuse (PerMaterial_DiffuseColor x the diffuse map), a reflectance (the Specular field x
the specular map), a gloss (Shininess / 255 x the smoothness map), a detail normal, and a material height; the macro
color (its saturation changed toward its luminance) overlays the diffuse, the macro gloss overlays the gloss, and the
macro normal adds to the detail normal; the alpha of the layer is the height blend of its independent splat
(GetTerrainMaterialLayerBlendingValue). The layers are composed in the order of the list of the region material with
source-over alpha. Not in the order of Priority (the inference of the research): at the bed of the highland river of
Oxboro (8457.9 5055.1) the gravel splat (highlanderosion, Priority 17) is 1.0, with lakes (Priority 10) 0.92: in the
order of Priority the gravel covered the river bed, but the retail bed is dark mud and grass; in the list order the
lake layers are the last ones. Priority is probably the rank of a layer for the budget of layers of a tile
(coat_terrainMaxLayerCompositesPerTile), not traced. The final terrain shader (ApplyTerrainVirtualTexture of coatlicue-terrain.cfxb) fades the tile
result to the macro set between the view distance and the view distance minus the blend distance.

The client (Unreal): the Landscape of a region has one target layer for each active layer of the region, named by its
rank in the list order (L00 is drawn first), with the raw weights of its splat on the vertex grid
(landscape-layers/SPLAT.r8; the layer info has no weight blend). One shared master material composes the slots in
rank order; the material instance of the region gives each slot its textures and values. A slot of a layer that a
Landscape component does not have is pruned (LandscapeLayerSwitch).

Output: regions/NAME/terrain-plan.json (the slots, the base material, the macro maps and parameters, the texture files).
Not traced in the source: the color space of the upload of the macro colors (MacroColor 0.3019608 and 0.7294118 are
multiples of 1/255) and the meaning of Background and Foreground: the plan keeps the source values and both readings.
"""
from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

from . import env, source, terrain_layers

VERSION = 1
SLOTS = 67   # the largest layer list of a region (research section 1)
ROLES = ("diffuse", "normal", "gloss", "specular", "height")
PUBLIC = {"g_materialLayerHeightScale": 1.0, "g_materialLayerHeightOffset": 0.0, "g_materialLayerBlendFactor": 1.0,
          "g_materialLayerBlendFalloff": 1.0, "g_macroBlendStrength": 1.0, "g_macroGlossBlendStrength": 1.0,
          "g_macroDiffuseSaturation": 1.0}   # the defaults of the shader parameters (terraintilecomposite.cfxb)


def numbers(text: str | None, count: int, default: float) -> list[float]:
    values = [float(v) for v in (text or "").replace(",", " ").split()] if text else []
    return (values + [default] * count)[:count]


def world_macro_params() -> dict:
    """@return The two macro parameter sets of the world material (BackgroundMacroMaterialParams and
    ForegroundMacroMaterialParams: MacroColorScale, MacroColor, MacroGlossScale, MacroNormalScale,
    MacroSpecularReflectance) and the parallax values (POM*)."""
    root = ET.fromstring(source.read(terrain_layers.WORLDMAT))[0]
    out = {}
    for node in root:
        field = node.get("field")
        if field in ("BackgroundMacroMaterialParams", "ForegroundMacroMaterialParams"):
            f = {c.get("field"): c.get("value") for c in node.iter() if c is not node and c.get("field")}
            out[field[:-len("MacroMaterialParams")].lower()] = {
                "color scale": numbers(f.get("MacroColorScale"), 1, 1.0)[0],
                "color": numbers(f.get("MacroColor"), 4, 1.0)[:3],
                "gloss scale": numbers(f.get("MacroGlossScale"), 1, 1.0)[0],
                "normal scale": numbers(f.get("MacroNormalScale"), 1, 1.0)[0],
                "specular reflectance": numbers(f.get("MacroSpecularReflectance"), 1, 0.0)[0]}
        elif field and field.startswith("POM"):
            out[field] = numbers(node.get("value"), 1, 0.0)[0]
    return out


def material_values(record: dict) -> dict:
    """@return The values of one layer material for its slot: the textures by role (content files), the tiling (the
    TexMod of the Diffuse map: the shader uses one material UV for all maps), and the parameters."""
    f = record.get("fields", {})
    public = {k: float(record.get("public params", {}).get(k, v)) for k, v in PUBLIC.items()}
    textures = {t["role"]: t["file"] for t in record.get("textures", []) if t.get("file")}
    diffuse = next((t for t in record.get("textures", []) if t["role"] == "diffuse"), {})
    tile = [float(diffuse.get("texmod", {}).get("TileU", 1.0)), float(diffuse.get("texmod", {}).get("TileV", 1.0))]
    return {"material": record["source"], "textures": {r: textures.get(r) for r in ROLES}, "tile": tile,
            "diffuse color": numbers(f.get("Diffuse"), 3, 1.0), "specular color": numbers(f.get("Specular"), 3, 0.0),
            # PerMaterial_SpecularColor.w: the gloss of the material (Shininess, 0 to 255)
            "gloss scale": numbers(f.get("Shininess"), 1, 255.0)[0] / 255.0,
            "height scale": public["g_materialLayerHeightScale"], "height offset": public["g_materialLayerHeightOffset"],
            "blend factor": public["g_materialLayerBlendFactor"], "blend falloff": public["g_materialLayerBlendFalloff"],
            "macro blend": public["g_macroBlendStrength"], "macro gloss blend": public["g_macroGlossBlendStrength"],
            "macro saturation": public["g_macroDiffuseSaturation"]}


def plan(name: str) -> dict:
    """Writes regions/NAME/terrain-plan.json. @return The plan (no slots for a region with an empty layer list)."""
    folder = env.content_dir() / "regions" / name
    layers = json.loads((folder / "layers.json").read_text())
    table = json.loads((env.content_dir() / "terrain" / "materials.json").read_text())["materials"]
    # The active layers: a weight file (not a placeholder), a priority below 255, and some weight.
    active = [l for l in layers.get("layers", []) if l.get("landscape weights") and l.get("priority", 255) != 255
              and l.get("nonzero", 0) > 0 and l.get("material") in table]
    active.sort(key=lambda l: l["order"])   # the list order of the region material (see the module text)
    if len(active) > SLOTS:
        raise ValueError(f"{name}: {len(active)} active layers, the master material has {SLOTS} slots")
    rx, ry = (int(v) for v in name[2:].split("_"))
    out = {"region": name, "version": VERSION, "origin m": [rx * 2048.0, ry * 2048.0], "size m": 2048.0,
           "slots": [], "base": None, "macro": {r: (layers.get("macro", {}).get(r) or {}).get("file") for r in
                                               ("color", "normal", "gloss")},
           "macro params": world_macro_params(), "custom macro params": layers.get("custom macro params")}
    default = layers.get("default material")
    if default in table:
        out["base"] = material_values(table[default])
    for rank, layer in enumerate(active):
        out["slots"].append({"slot": f"L{rank:02d}", "splat": layer["name"], "priority": layer["priority"],
                             "weights": layer["landscape weights"], "nonzero": layer["nonzero"],
                             **material_values(table[layer["material"]])})
    files = {f for s in out["slots"] + ([out["base"]] if out["base"] else []) for f in s["textures"].values() if f}
    out["texture files"] = sorted(files)
    out["inactive"] = [l["name"] for l in layers.get("layers", []) if l not in active]
    temporary = folder / "terrain-plan.tmp"
    temporary.write_text(json.dumps(out, indent=1))
    temporary.replace(folder / "terrain-plan.json")
    return out


def plan_all(names: list[str]) -> dict:
    """@return A report of the plans of the regions with a layer list."""
    report = {"regions": 0, "slots": 0, "largest": 0, "textures": set(), "without layers": []}
    for name in names:
        if not (env.content_dir() / "regions" / name / "layers.json").is_file():
            report["without layers"].append(name)
            continue
        p = plan(name)
        if not p["slots"]:
            report["without layers"].append(name)
            continue
        report["regions"] += 1
        report["slots"] += len(p["slots"])
        report["largest"] = max(report["largest"], len(p["slots"]))
        report["textures"].update(p["texture files"])
    report["textures"] = len(report["textures"])
    return report


def plan_path(name: str) -> Path:
    return env.content_dir() / "regions" / name / "terrain-plan.json"


# The reference of the composite (the offline check of the Unreal material; numpy, no engine). Engine meters, the plan of
# a region. The material UV: u = x * TileU, v = -y * TileV (v increases to the south: the rows of an image go north to
# south; the research inference "repeats per meter"). The region UV: u = (x - x0) / 2048, v = 1 - (y - y0) / 2048
# (TerrainTileCompositeVS flips the region TC).

def srgb_to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c: np.ndarray) -> np.ndarray:
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def overlay(base: np.ndarray, top: np.ndarray) -> np.ndarray:
    """OverlayBlend of the tile shader."""
    return np.where(base >= 0.5, 1 - 2 * (1 - base) * (1 - top), 2 * base * top)


def layer_alpha(height: np.ndarray, splat: np.ndarray, factor: float, falloff: float) -> np.ndarray:
    """GetTerrainMaterialLayerBlendingValue of the tile shader."""
    blend = np.clip(np.clip(splat * 2 - 1, 0, 1) + height, 0, 1)
    return np.clip((splat * blend * (1 + factor)) ** falloff, 0, 1)


def sample(image: np.ndarray, px: np.ndarray, py: np.ndarray, wrap: bool) -> np.ndarray:
    """Bilinear sample of an image (h, w[, c]) at pixel coordinates (the pixel centers at +0.5)."""
    h, w = image.shape[:2]
    fx, fy = px - 0.5, py - 0.5
    x0, y0 = np.floor(fx).astype(np.int64), np.floor(fy).astype(np.int64)
    tx, ty = fx - x0, fy - y0
    if image.ndim == 3:
        tx, ty = tx[..., None], ty[..., None]

    def at(x, y):
        x, y = (x % w, y % h) if wrap else (np.clip(x, 0, w - 1), np.clip(y, 0, h - 1))
        return image[y, x]

    top = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx
    bottom = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx
    return top * (1 - ty) + bottom * ty


def load_png(relative: str | None, kind: str) -> np.ndarray | None:
    """@return A texture of the content folder as floats 0..1: "color" decodes sRGB to linear, "data" stays."""
    if not relative:
        return None
    image = np.asarray(Image.open(env.content_dir() / relative)).astype(np.float32) / 255.0
    if image.ndim == 3 and image.shape[2] == 4:
        image = image[:, :, :3]
    return srgb_to_linear(image) if kind == "color" else image


def composite(name: str, area: tuple[float, float, float, float], step: float = 0.1, macro_srgb: bool = True,
              macro_set: str = "foreground", detail_srgb: bool = True, spec_srgb: bool = True,
              near_color: float | None = None) -> dict:
    """The tile composite of the game for an area (x0, y0, x1, y1, engine m) of a region at `step` m: the base material,
    then the slots in their order, each with its height blend alpha (source-over). The readings of the color spaces (not
    traced in the source): macro_srgb, detail_srgb, spec_srgb (True: decode the map from sRGB, its DXGI format; False:
    the raw values), near_color (another value of the macro color factor). @return The linear albedo (h, w, 3),
    the gloss, the normal (h, w, 3; tangent space: x east, y south), the reflectance, and the coverage of each slot."""
    p = json.loads(plan_path(name).read_text())
    folder = env.content_dir() / "regions" / name
    ox, oy = p["origin m"]
    x0, y0, x1, y1 = area
    xs = np.arange(x0, x1, step) + step / 2
    ys = np.arange(y1, y0, -step) - step / 2   # row 0 = north
    x, y = np.meshgrid(xs, ys)
    rpx, rpy = (x - ox) / 2048 * 2048, (1 - (y - oy) / 2048) * 2048
    params = p["macro params"][macro_set]
    macro_color = load_png("regions/" + name + "/" + p["macro"]["color"], "color" if macro_srgb else "data")
    factor = np.array([near_color] * 3 if near_color is not None else params["color"], np.float32)
    macro_color = sample(macro_color, rpx, rpy, False) * factor * params["color scale"]
    macro_gloss = sample(load_png("regions/" + name + "/" + p["macro"]["gloss"], "data"), rpx, rpy, False) * params["gloss scale"]
    mn = sample(load_png("regions/" + name + "/" + p["macro"]["normal"], "data"), rpx, rpy, False)[..., :2] * 2 - 1
    macro_normal = np.concatenate([mn, np.full(mn.shape[:2] + (1,), 1.0 / params["normal scale"])], 2)
    macro_normal /= np.linalg.norm(macro_normal, axis=2, keepdims=True)
    luminance = (macro_color @ np.array([0.2126, 0.7152, 0.0722], np.float32))[..., None]
    shape = x.shape
    albedo, gloss = np.zeros(shape + (3,), np.float32), np.zeros(shape, np.float32)
    nxy, reflect = np.zeros(shape + (2,), np.float32), np.zeros(shape, np.float32)
    coverage = {}
    for slot in [dict(p["base"], slot="base")] + p["slots"]:
        if slot["slot"] == "base":
            splat = np.ones(shape, np.float32)
        else:
            weights = np.fromfile(folder / "layers" / (slot["splat"] + ".r8"), np.uint8).reshape(2048, 2048).astype(np.float32) / 255
            splat = sample(weights, rpx, rpy, False)
            if float(splat.max()) <= 0:
                continue
        tu, tv = slot["tile"]
        textures = slot["textures"]

        def at(role, kind):
            image = load_png(textures.get(role), kind)
            if image is None:
                return None
            h, w = image.shape[:2]
            return sample(image, x * tu * w, -y * tv * h, True)

        diffuse = at("diffuse", "color" if detail_srgb else "data")
        diffuse = (diffuse if diffuse is not None else np.ones(shape + (3,), np.float32)) * np.array(slot["diffuse color"], np.float32)
        spec = at("specular", "color" if spec_srgb else "data")
        r = slot["specular color"][0] * (spec[..., 0] if spec is not None else 1.0)
        g = at("gloss", "data")
        g = slot["gloss scale"] * (g if g is not None and g.ndim == 2 else (g[..., 0] if g is not None else np.ones(shape, np.float32)))
        hgt = at("height", "data")
        hgt = np.clip(slot["height scale"] * (hgt if hgt is not None and hgt.ndim == 2 else 0.5) + slot["height offset"], 0, 1)
        n = at("normal", "data")
        dxy = n[..., :2] * 2 - 1 if n is not None else np.zeros(shape + (2,), np.float32)
        modified = luminance + (macro_color - luminance) * slot["macro saturation"]
        diffuse = diffuse + (overlay(modified, diffuse) - diffuse) * slot["macro blend"]
        g = g + (overlay(macro_gloss, g) - g) * slot["macro gloss blend"]
        combined = np.concatenate([macro_normal[..., :2] * params["normal scale"] + dxy, macro_normal[..., 2:]], 2)
        combined /= np.linalg.norm(combined, axis=2, keepdims=True)
        a = np.ones(shape, np.float32) if slot["slot"] == "base" else layer_alpha(hgt, splat, slot["blend factor"], slot["blend falloff"])
        albedo += (diffuse - albedo) * a[..., None]
        gloss += (g - gloss) * a
        nxy += (combined[..., :2] - nxy) * a[..., None]
        reflect += (r - reflect) * a
        coverage[slot["slot"] + " " + slot.get("splat", slot["material"].rsplit("/", 1)[-1])] = round(float(a.mean()), 4)
    nz = np.sqrt(np.clip(1 - (nxy ** 2).sum(2), 0, 1))
    return {"albedo": albedo, "gloss": gloss, "normal": np.concatenate([nxy, nz[..., None]], 2), "reflectance": reflect,
            "coverage": coverage}


def reference_pictures(name: str, area: tuple[float, float, float, float], out: Path, step: float = 0.1, **options) -> dict:
    """Writes the albedo (sRGB, scaled to a mean of 0.45 for the eye; the scale is in the report) and a shaded picture
    (a sun 55 degrees high from the south-east on the composite normal) of the reference composite. @return A report."""
    result = composite(name, area, step, **options)
    albedo = result["albedo"]
    n = result["normal"] * np.array([1.0, -1.0, 1.0], np.float32)   # tangent y south -> engine y north
    sun = np.array([0.4, -0.4, 0.82], np.float32)
    sun /= np.linalg.norm(sun)
    shaded = albedo * (0.3 + 0.7 * np.clip(n @ sun, 0, 1))[..., None]
    out.mkdir(parents=True, exist_ok=True)
    mean = float(albedo.mean())
    scale = 0.45 / max(float(linear_to_srgb(albedo).mean()), 1e-6)
    for label, image in (("albedo", albedo), ("shaded", shaded)):
        picture = np.clip(linear_to_srgb(image) * scale, 0, 1)
        Image.fromarray((picture * 255 + 0.5).astype(np.uint8)).save(out / f"{name}-reference-{label}.png")
    return {"area": area, "step m": step, "albedo mean (linear)": round(mean, 4),
            "display scale": round(scale, 3), "gloss mean": round(float(result["gloss"].mean()), 3),
            "reflectance mean": round(float(result["reflectance"].mean()), 4), "coverage": result["coverage"], **options}


# The texture arrays of the Unreal material: one slice for each terrain material of the world (materials.json, sorted by
# path), the same size in each array. Albedo: the diffuse map (sRGB RGB). Normal: the detail normal (X and Y signed in
# R and G, Z in B). Masks (data): R the gloss (smoothness), G the material height, B the specular map (its red: the
# shader uses reflectance.x). A missing map is white (1.0). The 1024 maps of two materials and the 512 specular maps are
# resized (bilinear) to ARRAY_SIZE.
ARRAY_SIZE = 2048
ARRAY_ROLES = ("albedo", "normal", "masks")


def array_dir() -> Path:
    return env.content_dir() / "terrain" / "arrays"


def array_index() -> dict[str, int]:
    """@return The slice of each terrain material (materials.json, sorted by path)."""
    table = json.loads((env.content_dir() / "terrain" / "materials.json").read_text())["materials"]
    return {path: index for index, path in enumerate(sorted(table))}


def _fit(relative: str | None, channels: int) -> np.ndarray:
    """@return A texture of the content folder as uint8 (ARRAY_SIZE, ARRAY_SIZE, channels); white when missing."""
    if not relative:
        return np.full((ARRAY_SIZE, ARRAY_SIZE, channels), 255, np.uint8)
    image = Image.open(env.content_dir() / relative)
    image = image.convert("RGB" if channels == 3 else "L")
    if image.size != (ARRAY_SIZE, ARRAY_SIZE):
        image = image.resize((ARRAY_SIZE, ARRAY_SIZE), Image.BILINEAR)
    array = np.asarray(image)
    return array if channels == 3 else array[:, :, None]


def write_slice(item: tuple[str, int]) -> dict:
    """Writes the three slices of one material. @return Its record."""
    path, index = item
    table = json.loads((env.content_dir() / "terrain" / "materials.json").read_text())["materials"]
    textures = {t["role"]: t["file"] for t in table[path]["textures"] if t.get("file")}
    images = {"albedo": _fit(textures.get("diffuse"), 3), "normal": _fit(textures.get("normal"), 3),
              "masks": np.concatenate([_fit(textures.get("gloss"), 1), _fit(textures.get("height"), 1),
                                       _fit(textures.get("specular"), 3)[:, :, :1]], 2)}
    for role, image in images.items():
        out = array_dir() / role / f"{index:03d}.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        temporary = out.with_name(f".{out.name}.tmp.png")
        Image.fromarray(image).save(temporary)
        temporary.replace(out)
    return {"material": path, "index": index, "missing": sorted(r for r in ("diffuse", "normal", "gloss", "height", "specular")
                                                                if r not in textures)}


def write_arrays(workers: int = 3) -> dict:
    """Writes the slices of all terrain materials and terrain/arrays/index.json. @return A report."""
    from concurrent.futures import ProcessPoolExecutor
    items = sorted(array_index().items(), key=lambda kv: kv[1])
    with ProcessPoolExecutor(max_workers=workers) as pool:
        rows = list(pool.map(write_slice, items))
    index = {"version": VERSION, "size": ARRAY_SIZE, "roles": list(ARRAY_ROLES), "slices": {r["material"]: r["index"] for r in rows},
             "missing": {r["material"]: r["missing"] for r in rows if r["missing"]}}
    (array_dir() / "index.json").write_text(json.dumps(index, indent=1))
    return {"slices": len(rows), "missing": index["missing"]}
