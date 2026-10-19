"""The water of the world (stage WF5, docs/WATER.md): the surfaces of the water volumes and the rivers of each region, the ocean
and its local overrides, the water materials, and the water quadtree of each region for the depth queries.

The sources are the environment records of the regions (environment.py: SWC_CONTENT/environment/regions/REGION.json, the kinds
waterVolume, river, ocean), the base ocean of the mission (environment/mission.json), the .mtl files of the water materials, and
the water quadtree of each region (region.waterqt). The rules come from the research of WF5
(~/Games/soulwarden-client/research/wf5/; each rule was checked with the quadtree, the surface of the client data):

- A water volume: its surface is the top of its shape in the frame of the entity, through the full matrix (rotation and scale):
  a polygon prism M (x, y, Height), a box M (x, y, +Dz / 2) (a box is centered on its entity), a cylinder M (x, y, +Height / 2).
  452 volumes agree with the quadtree surface (the largest difference 0.028 m). The polygon is counterclockwise (568 of 569)
  and can be concave: ear clipping.
- A river: segment i is the cubic Bezier V[i], Forward[i], Back[i + 1], V[i + 1] (the field crc:6dcec137 is "Back"); a linear
  spline has straight segments. The full width is DefaultWidth + WidthAttribute[i] (one value for each vertex), centered, linear
  between two vertices by the parameter of the segment [inference: the rule of Lumberyard]. The mesh has a cross-section at each
  SegmentLength of the length of a segment. The surface is the matrix applied to the curve point (200 rivers agree with the
  quadtree, the median difference 0 m). The texture coordinates: u across (W / TileWidth), v along (the length / TileLength).
- The quadtree: the nodes in level order (Height, FloorHeight in m as float32, Flags); Height 65535 splits a node into four
  (children (x, y), (x + h, y), (x, y + h), (x + h, y + h)); a leaf with Height 0 and Floor 0 has no water. Flags: bit 0
  gatherable, 1 fishable, 2 fresh water, 3 salt water.

Outputs (SWC_CONTENT/water, never in git):
  water.json          the format, the ocean (the level, the base settings, the overrides with their shapes), the materials, the
                      regions with their counts and files, the duplicates that were removed
  REGION.water        the surfaces of the region (binary, below)
  REGION.waterq       the quadtree nodes of the region (binary, below)

REGION.water (little-endian): b"SWCW", u32 version, u32 body count; each body: u8 kind (0 volume, 1 river), u8 shape (0 prism, 1
box, 2 cylinder, 3 Bezier river, 4 linear river), u16 material (an index of water.json "materials"), f32 x 3 fog color, f32 fog
density, f32 fog color scale, f32 stream speed, f32 x 6 bounds (engine m: min xyz, max xyz), u16 name length, the name (UTF-8),
u32 vertex count, u32 index count, the vertices (f32 x, y, z in engine m, f32 u, v), the indices (u32, counterclockwise from
above).
REGION.waterq: b"SWQT", u32 version, u32 region size (m), u32 node count, then each node f32 height, f32 floor, u32 flags.
"""
from __future__ import annotations

import json
import math
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

from . import census, env, source

from nav_assets import number, parse, real  # noqa: E402
from nav_terrain import FLAGS_FIELD, FLOOR_FIELD, HEIGHT_FIELD, NODES_FIELD, SIZE_FIELD, SUBDIVIDED  # noqa: E402

WORLD = "sharedassets/coatlicue/newworld_vitaeeterna/"
REGION_SIZE = 2048
MESH_VERSION = 1
TREE_VERSION = 1
# The ids of the spline classes: the first 8 bytes of the class UUID (little-endian).
BEZIER = 2612576363626472641
LINEAR = 7588504976706601181
KIND_VOLUME, KIND_RIVER = 0, 1
SHAPE_PRISM, SHAPE_BOX, SHAPE_CYLINDER, SHAPE_BEZIER, SHAPE_LINEAR = 0, 1, 2, 3, 4
# The points on the circle of a cylinder volume (a decision of the converter: the source has a real circle).
CYLINDER_POINTS = 48
# The samples of the parameter of one Bezier segment for its length table.
ARC_SAMPLES = 64


def elements(value) -> list:
    """@return The list of an object stream container: {"element": [...]} or {"element": x} (one element)."""
    if isinstance(value, dict):
        value = value.get("element", [])
    if isinstance(value, list):
        return value
    return [value]


def matrix_of(record: dict) -> np.ndarray:
    return np.array(record["matrix"], dtype=np.float64)


def apply(matrix: np.ndarray, points: np.ndarray) -> np.ndarray:
    """@return The points (n, 3) of the frame of an entity in the world (engine m)."""
    return points @ matrix[:3, :3].T + matrix[:3, 3]


def material_path(path: str) -> str:
    """@return The pak path of a material reference: lower case, with the extension .mtl."""
    path = (path or "").lower().replace("\\", "/")
    if path and not path.endswith(".mtl"):
        path += ".mtl"
    return path


def triangulate(polygon: np.ndarray) -> list[tuple[int, int, int]]:
    """@return The triangles (counterclockwise) of a simple polygon (n, 2) by ear clipping. The order of the points is
    counterclockwise or clockwise; a degenerate rest stops the clipping."""
    n = len(polygon)
    if n < 3:
        return []
    area = 0.5 * sum(polygon[i, 0] * polygon[(i + 1) % n, 1] - polygon[(i + 1) % n, 0] * polygon[i, 1] for i in range(n))
    order = list(range(n)) if area > 0 else list(range(n - 1, -1, -1))

    def cross(a, b, c) -> float:
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def inside(p, a, b, c) -> bool:
        return cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0

    triangles = []
    guard = 0
    while len(order) > 3 and guard < 4 * n * n:
        guard += 1
        clipped = False
        for k in range(len(order)):
            i0, i1, i2 = order[k - 1], order[k], order[(k + 1) % len(order)]
            a, b, c = polygon[i0], polygon[i1], polygon[i2]
            if cross(a, b, c) <= 1e-12:
                continue   # a reflex or flat corner
            if any(inside(polygon[j], a, b, c) for j in order if j not in (i0, i1, i2)):
                continue
            triangles.append((i0, i1, i2))
            del order[k]
            clipped = True
            break
        if not clipped:
            break
    if len(order) == 3:
        triangles.append(tuple(order))
    return triangles


def volume_surface(record: dict) -> tuple[np.ndarray, list, int] | None:
    """@return (the world points of the top of the shape, the triangles, the shape) of a water volume, or None."""
    shape = record["shape"]
    config = shape["fields"].get("Configuration", {})
    if shape["type"] == "polygonPrism":
        prism = elements(config.get("PolygonPrism"))[0]
        height = float(prism.get("Height", 0.0))
        vertices = elements(prism["VertexContainer"]["Vertices"])
        polygon = np.array(vertices, dtype=np.float64).reshape(-1, 2)
        local = np.column_stack([polygon, np.full(len(polygon), height)])
        kind = SHAPE_PRISM
        triangles = triangulate(polygon)
    elif shape["type"] == "box":
        dx, dy, dz = (float(v) for v in config["Dimensions"])
        polygon = np.array([[-dx / 2, -dy / 2], [dx / 2, -dy / 2], [dx / 2, dy / 2], [-dx / 2, dy / 2]])
        local = np.column_stack([polygon, np.full(4, dz / 2)])
        kind = SHAPE_BOX
        triangles = [(0, 1, 2), (0, 2, 3)]
    elif shape["type"] == "cylinder":
        radius, height = float(config["Radius"]), float(config["Height"])
        angles = np.arange(CYLINDER_POINTS) * (2 * math.pi / CYLINDER_POINTS)
        rim = np.column_stack([radius * np.cos(angles), radius * np.sin(angles)])
        polygon = np.vstack([[0.0, 0.0], rim])
        local = np.column_stack([polygon, np.full(len(polygon), height / 2)])
        kind = SHAPE_CYLINDER
        triangles = [(0, 1 + i, 1 + (i + 1) % CYLINDER_POINTS) for i in range(CYLINDER_POINTS)]
    else:
        return None
    if not triangles:
        return None
    world = apply(matrix_of(record), local)
    # A matrix with a mirror (a negative determinant) turns the order of the points: keep the triangles counterclockwise from above.
    if np.linalg.det(matrix_of(record)[:3, :3]) < 0:
        triangles = [(a, c, b) for a, b, c in triangles]
    return world, triangles, kind


def river_parts(record: dict) -> tuple[int, np.ndarray, list, dict, list]:
    config = record["shape"]["fields"]["Configuration"]
    spline = elements(config["Spline"])[0]
    vertices = elements(spline["BaseClass1"]["Vertices"]["Vertices"])
    if vertices and isinstance(vertices[0], (int, float)):
        vertices = [vertices]
    points = np.array(vertices, dtype=np.float64).reshape(-1, 3)
    controls = []
    for item in elements(spline.get("Bezier Data", {}))[: len(points)]:
        controls.append((np.array(item["Forward"], dtype=np.float64), np.array(item["crc:6dcec137"], dtype=np.float64)))
    river = record["fields"]["River"]
    widths = [float(v) for v in elements(river["BaseClass1"]["Width"]["WidthAttribute"]["Elements"])]
    return int(config["Spline Type"]), points, controls, river, widths


def bezier(points: np.ndarray, controls: list, i: int, t: np.ndarray, linear: bool) -> tuple[np.ndarray, np.ndarray]:
    """@return (the points, the derivatives) of segment i at the parameters t (local frame)."""
    p0, p3 = points[i], points[i + 1]
    t = t[:, None]
    if linear or len(controls) < len(points):
        return p0 + (p3 - p0) * t, np.repeat((p3 - p0)[None, :], len(t), axis=0)
    p1, p2 = controls[i][0], controls[i + 1][1]
    u = 1 - t
    position = u ** 3 * p0 + 3 * t * u * u * p1 + 3 * t * t * u * p2 + t ** 3 * p3
    derivative = 3 * u * u * (p1 - p0) + 6 * u * t * (p2 - p1) + 3 * t * t * (p3 - p2)
    return position, derivative


def river_surface(record: dict) -> tuple[np.ndarray, np.ndarray, list, int] | None:
    """@return (the world points, the texture coordinates, the triangles, the shape) of the surface of a river, or None."""
    spline_type, points, controls, river, attributes = river_parts(record)
    if len(points) < 2:
        return None
    linear = spline_type == LINEAR
    base = river["BaseClass1"]
    default = float(base["Width"]["DefaultWidth"])
    widths = [default + (attributes[i] if i < len(attributes) else 0.0) for i in range(len(points))]
    step = max(0.5, float(base.get("SegmentLength", 10.0)))
    tile_length = max(0.01, float(base.get("TileLength", 1.0)))
    tile_width = max(0.01, float(river.get("TileWidth", 1.0)))
    centers, tangents, width_of = [], [], []
    for i in range(len(points) - 1):
        dense_t = np.linspace(0.0, 1.0, ARC_SAMPLES + 1)
        dense, _ = bezier(points, controls, i, dense_t, linear)
        lengths = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(dense, axis=0), axis=1))])
        count = max(1, math.ceil(lengths[-1] / step))
        # Equal steps of the length of the segment (the last segment keeps its end point).
        targets = np.linspace(0.0, lengths[-1], count + 1)
        if i < len(points) - 2:
            targets = targets[:-1]
        t = np.interp(targets, lengths, dense_t)
        position, derivative = bezier(points, controls, i, t, linear)
        centers.append(position)
        tangents.append(derivative)
        width_of.append(widths[i] + (widths[i + 1] - widths[i]) * t)
    center = np.vstack(centers)
    tangent = np.vstack(tangents)
    width = np.concatenate(width_of)
    # The direction across: the horizontal normal of the tangent (a zero tangent takes the one before it).
    flat = tangent[:, :2].copy()
    norm = np.linalg.norm(flat, axis=1)
    for k in range(len(flat)):
        if norm[k] < 1e-9:
            flat[k] = flat[k - 1] if k > 0 else np.array([1.0, 0.0])
            norm[k] = max(np.linalg.norm(flat[k]), 1e-9)
    flat /= norm[:, None]
    left = np.column_stack([-flat[:, 1], flat[:, 0], np.zeros(len(flat))])
    half = (width / 2)[:, None]
    local = np.empty((2 * len(center), 3))
    local[0::2] = center + left * half
    local[1::2] = center - left * half
    world = apply(matrix_of(record), local)
    along = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(center, axis=0), axis=1))])
    uv = np.empty((2 * len(center), 2))
    uv[0::2, 0] = 0.0
    uv[1::2, 0] = width / tile_width
    uv[0::2, 1] = along / tile_length
    uv[1::2, 1] = along / tile_length
    triangles = []
    for k in range(len(center) - 1):
        a, b, c, d = 2 * k, 2 * k + 1, 2 * k + 2, 2 * k + 3
        # From above: left (a), right (b) at k; the next pair (c, d) along the flow. Counterclockwise: a, b, d and a, d, c.
        triangles.append((a, b, d))
        triangles.append((a, d, c))
    if np.linalg.det(matrix_of(record)[:3, :3]) < 0:
        triangles = [(x, z, y) for x, y, z in triangles]
    return world, uv, triangles, SHAPE_LINEAR if linear else SHAPE_BEZIER


def duplicate_key(record: dict) -> str:
    rounded = [round(v, 2) for row in record["matrix"] for v in row]
    return json.dumps([record["kind"], rounded, record["shape"], record["fields"]], sort_keys=True)


def material_record(path: str) -> dict:
    """@return The shader, the public parameters, and the textures of a water material (an empty record when the pak has no
    such file)."""
    record = {"path": path, "shader": "", "params": {}, "textures": {}}
    if not path or not source.has(path):
        record["missing"] = True
        return record
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    material = root if root.find("SubMaterials") is None else list(root.find("SubMaterials"))[0]
    record["shader"] = material.get("Shader", "")
    record["genMask"] = material.get("StringGenMask", "")
    params = material.find("PublicParams")
    for key, value in (params.attrib.items() if params is not None else []):
        try:
            record["params"][key] = float(value)
        except ValueError:
            record["params"][key] = value
    for texture in material.iter("Texture"):
        record["textures"][texture.get("Map", "")] = (texture.get("File") or "").lower()
    for key in ("Diffuse", "Specular", "Shininess", "Opacity"):
        if material.get(key) is not None:
            record[key.lower()] = material.get(key)
    return record


def read_tree(name: str) -> list[tuple[float, float, int]]:
    path = WORLD + f"regions/{name}/region.waterqt"
    if not source.has(path):
        return []
    root = parse(source.read(path))[0]
    if number(root.child(SIZE_FIELD)) != REGION_SIZE:
        raise ValueError(f"{path}: a region size that is not {REGION_SIZE}")
    out = []
    for node in root.child(NODES_FIELD).children:
        flags = node.child(FLAGS_FIELD)
        out.append((real(node.child(HEIGHT_FIELD)), real(node.child(FLOOR_FIELD)),
                    int.from_bytes(flags.value, "little") if flags is not None and flags.value is not None else 0))
    return out


def tree_counts(nodes: list) -> dict:
    """@return The leaves with water, their area, and the counts of the flags of the nodes of a quadtree."""
    level = [REGION_SIZE]
    position, leaves, area, flags = 0, 0, 0, {}
    while level and position < len(nodes):
        following = []
        for edge in level:
            height, floor_height, flag = nodes[position]
            position += 1
            if height == SUBDIVIDED:
                following.extend([edge // 2] * 4)
            elif height != 0.0 or floor_height != 0.0:
                leaves += 1
                area += edge * edge
                flags[str(flag)] = flags.get(str(flag), 0) + 1
        level = following
    return {"nodes": len(nodes), "waterLeaves": leaves, "waterArea": area, "flags": flags}


def ocean_override(record: dict) -> dict:
    settings = record["fields"].get("crc:adf3f363", {})
    shape = record["shape"]
    config = shape["fields"].get("Configuration", {})
    out = {"entity": record["entity"], "chain": record["chain"][0], "matrix": record["matrix"], "place": record["place"],
           "shape": shape["type"], "transition": float(record["fields"].get("oceanTransitionTime", 0.0)),
           "material": material_path(settings.get("General", {}).get("oceanMaterial", {}).get("BaseClass1", {}).get("AssetPath", "")),
           "animation": settings.get("Animation", {}), "fog": settings.get("crc:82a8ed13", {}), "caustics": settings.get("Caustics", {})}
    if shape["type"] == "cylinder":
        out["radius"], out["height"] = float(config["Radius"]), float(config["Height"])
    elif shape["type"] == "box":
        out["dimensions"] = [float(v) for v in config["Dimensions"]]
    elif shape["type"] == "sphere":
        out["radius"] = float(config.get("Radius", 0.0))
    return out


# The engine textures of the water shaders of the game [inference of the research: the .mtl files have no normal map slot; the
# shaders bind these engine textures]. role: (pak path, the color space of the import: normal or srgb).
TEXTURES = {"waves": ("engineassets/textures/oceanwaves_ddn.dds", "normal"),
            "foamNormal": ("engineassets/shading/waterfoam2_ddn.dds", "normal"),
            "foam": ("engineassets/shading/waterfoam.dds", "srgb")}


def write_textures(out) -> dict:
    """Writes the water textures as PNG into water/textures (a normal map keeps the green of the source: Unreal has the same
    direction). @return Their records for water.json."""
    from . import dds
    from PIL import Image
    folder = out / "textures"
    folder.mkdir(parents=True, exist_ok=True)
    records = {}
    for role, (path, color) in TEXTURES.items():
        data, w, h, fmt = dds.join(source.read, path, 1024)
        image = dds.to_image(data, w, h, fmt)
        image = image.convert("RGB") if color == "normal" else image.convert("RGBA")
        name = "T_SwcWater_" + role[0].upper() + role[1:]
        image.save(folder / f"{name}.png")
        records[role] = {"source": path, "file": f"water/textures/{name}.png", "name": name, "color": color, "size": [w, h]}
    return records


def write(names: list[str]) -> dict:
    content = env.content_dir()
    out = content / "water"
    out.mkdir(parents=True, exist_ok=True)
    mission = json.loads((content / "environment" / "mission.json").read_text())
    terrain = json.loads(source.read(WORLD + "terrain.json").decode("utf-8"))
    environment = mission["environment"]
    fog = {item["name"]: item["saved"] for item in mission["timeOfDay"]["variables"] if item["name"].startswith("Ocean fog")}
    materials: list[dict] = []
    material_index: dict[str, int] = {}

    def material_of(path: str) -> int:
        path = material_path(path)
        if path not in material_index:
            material_index[path] = len(materials)
            materials.append(material_record(path))
        return material_index[path]

    ocean = {"level": float(terrain["oceanLevel"]), "material": material_of(environment["Ocean"]["Material"]),
             "animation": {k: float(v) for k, v in environment.get("OceanAnimation", {}).items()},
             "caustics": {k: float(v) for k, v in environment["Ocean"].items() if k != "Material"},
             "fog": fog, "overrides": []}
    regions, duplicates = {}, []
    totals = {"volumes": 0, "rivers": 0, "vertices": 0, "triangles": 0, "skipped": []}
    seen: set[str] = set()
    for name in names:
        path = content / "environment" / "regions" / f"{name}.json"
        records = json.loads(path.read_text())["records"] if path.is_file() else []
        bodies = []
        for record in records:
            if record["kind"] == "ocean":
                override = ocean_override(record)
                if override["material"]:
                    override["materialIndex"] = material_of(override["material"])
                ocean["overrides"].append(override)
                continue
            if record["kind"] not in ("waterVolume", "river"):
                continue
            key = duplicate_key(record)
            if key in seen:
                duplicates.append({"region": name, "entity": record["entity"], "place": record["place"]})
                continue
            seen.add(key)
            if record["kind"] == "waterVolume":
                surface = volume_surface(record)
                if surface is None:
                    totals["skipped"].append({"region": name, "entity": record["entity"], "shape": record["shape"]["type"]})
                    continue
                world, triangles, shape = surface
                common = record["fields"].get("Common", {})
                uv = world[:, :2] * np.array([float(common.get("SurfaceUScale", 1.0)), float(common.get("SurfaceVScale", 1.0))])
                body = {"kind": KIND_VOLUME, "shape": shape, "name": record["entity"],
                        "material": material_of(common.get("MaterialAsset", {}).get("BaseClass1", {}).get("AssetPath", "")),
                        "fog": [float(v) for v in common.get("FogColor", [0, 0, 0])[:3]], "density": float(common.get("fogDensity", 0.0)),
                        "scale": float(common.get("fogColorMultiplier", 1.0)), "stream": 0.0}
                totals["volumes"] += 1
            else:
                surface = river_surface(record)
                if surface is None:
                    totals["skipped"].append({"region": name, "entity": record["entity"], "shape": "spline"})
                    continue
                world, uv, triangles, shape = surface
                river = record["fields"]["River"]
                body = {"kind": KIND_RIVER, "shape": shape, "name": record["entity"],
                        "material": material_of(river.get("Material", {}).get("BaseClass1", {}).get("AssetPath", "")),
                        "fog": [float(v) for v in river.get("FogColor", [0, 0, 0])[:3]], "density": float(river.get("WaterFogDensity", 0.0)),
                        "scale": float(river.get("FogColorScale", 1.0)), "stream": float(river.get("WaterStreamSpeed", 0.0))}
                totals["rivers"] += 1
            body["points"], body["uv"], body["triangles"] = world, uv, triangles
            bodies.append(body)
        tree = read_tree(name)
        record = {"volumes": sum(1 for b in bodies if b["kind"] == KIND_VOLUME), "rivers": sum(1 for b in bodies if b["kind"] == KIND_RIVER),
                  "vertices": sum(len(b["points"]) for b in bodies), "triangles": sum(len(b["triangles"]) for b in bodies),
                  "tree": tree_counts(tree)}
        if bodies:
            (out / f"{name}.water").write_bytes(mesh_bytes(bodies))
            record["file"] = f"{name}.water"
        if tree:
            (out / f"{name}.waterq").write_bytes(tree_bytes(tree))
            record["treeFile"] = f"{name}.waterq"
        totals["vertices"] += record["vertices"]
        totals["triangles"] += record["triangles"]
        regions[name] = record
    textures = write_textures(out)
    index = {"format": {"mesh": MESH_VERSION, "tree": TREE_VERSION, "space": "engine m, Z up", "regionSize": REGION_SIZE,
                        "flags": {"gatherable": 1, "fishable": 2, "fresh": 4, "salt": 8}},
             "ocean": ocean, "materials": materials, "textures": textures, "regions": regions, "duplicates": duplicates, "skipped": totals["skipped"]}
    (out / "water.json").write_text(json.dumps(index, indent=1))
    return {"regions": len(regions), "volumes": totals["volumes"], "rivers": totals["rivers"], "duplicates": len(duplicates),
            "skipped": len(totals["skipped"]), "ocean overrides": len(ocean["overrides"]), "materials": len(materials),
            "vertices": totals["vertices"], "triangles": totals["triangles"], "folder": str(out)}


def mesh_bytes(bodies: list[dict]) -> bytes:
    parts = [b"SWCW", struct.pack("<II", MESH_VERSION, len(bodies))]
    for body in bodies:
        points = np.asarray(body["points"], dtype=np.float32)
        uv = np.asarray(body["uv"], dtype=np.float32)
        indices = np.asarray(body["triangles"], dtype=np.uint32).reshape(-1)
        name = body["name"].encode("utf-8")[:65535]
        low, high = points.min(axis=0), points.max(axis=0)
        parts.append(struct.pack("<BBH3ffff6fH", body["kind"], body["shape"], body["material"], *body["fog"], body["density"],
                                 body["scale"], body["stream"], *low, *high, len(name)))
        parts.append(name)
        parts.append(struct.pack("<II", len(points), len(indices)))
        parts.append(np.column_stack([points, uv]).astype("<f4").tobytes())
        parts.append(indices.astype("<u4").tobytes())
    return b"".join(parts)


def tree_bytes(nodes: list) -> bytes:
    data = np.array([(h, f, g) for h, f, g in nodes], dtype=[("h", "<f4"), ("f", "<f4"), ("g", "<u4")])
    return b"SWQT" + struct.pack("<III", TREE_VERSION, REGION_SIZE, len(nodes)) + data.tobytes()


def read_mesh(path) -> list[dict]:
    """@return The bodies of a REGION.water file (the reader of the tests and of the check)."""
    data = Path(path).read_bytes()
    if data[:4] != b"SWCW":
        raise ValueError(f"{path}: not a water mesh file")
    version, count = struct.unpack_from("<II", data, 4)
    offset, bodies = 12, []
    head = struct.Struct("<BBH3ffff6fH")
    for _ in range(count):
        kind, shape, material, r, g, b, density, scale, stream, *bounds, length = head.unpack_from(data, offset)
        offset += head.size
        name = data[offset:offset + length].decode("utf-8")
        offset += length
        vertices, indices = struct.unpack_from("<II", data, offset)
        offset += 8
        values = np.frombuffer(data, dtype="<f4", count=vertices * 5, offset=offset).reshape(-1, 5)
        offset += vertices * 20
        triangles = np.frombuffer(data, dtype="<u4", count=indices, offset=offset).reshape(-1, 3)
        offset += indices * 4
        bodies.append({"kind": kind, "shape": shape, "material": material, "fog": (r, g, b), "density": density, "scale": scale,
                       "stream": stream, "bounds": bounds, "name": name, "points": values[:, :3], "uv": values[:, 3:], "triangles": triangles})
    return bodies


def tree_surface(nodes: list, x: float, y: float) -> tuple[float, float, int] | None:
    """@return (the surface, the floor, the flags) of the quadtree of a region at a local point (m), or None (no water)."""
    position, base, edge = 0, (0, 0), REGION_SIZE
    # The children of the k-th split node of the level order are the nodes 1 + 4k to 4 + 4k: count the split nodes before.
    splits = np.cumsum([1 if n[0] == SUBDIVIDED else 0 for n in nodes]) if not hasattr(tree_surface, "cache") or tree_surface.cache[0] is not nodes else tree_surface.cache[1]
    tree_surface.cache = (nodes, splits)
    while True:
        height, floor_height, flags = nodes[position]
        if height != SUBDIVIDED:
            return None if height == 0.0 and floor_height == 0.0 else (height, floor_height, flags)
        edge //= 2
        child = int(x >= base[0] + edge) + 2 * int(y >= base[1] + edge)
        base = (base[0] + edge * (child & 1), base[1] + edge * (child >> 1))
        k = int(splits[position]) - 1
        position = 1 + 4 * k + child


def check(names: list[str], per_body: int = 20) -> dict:
    """The surfaces against the quadtree: at points inside the triangles of each body (the centroids, at most per_body), the
    height of the mesh minus the surface of the quadtree, for the points that the quadtree has as water."""
    out = env.content_dir() / "water"
    diffs, missing, kinds, medians = [], 0, {}, []
    for name in names:
        path = out / f"{name}.water"
        if not path.is_file():
            continue
        nodes = read_tree(name)
        rx, ry = (int(v) for v in name[2:].split("_"))
        for body in read_mesh(path):
            points, triangles = body["points"], body["triangles"]
            step = max(1, len(triangles) // per_body)
            own = []
            for a, b, c in triangles[::step]:
                center = (points[a] + points[b] + points[c]) / 3
                found = tree_surface(nodes, center[0] - rx * REGION_SIZE, center[1] - ry * REGION_SIZE)
                if found is None:
                    missing += 1
                    continue
                diff = float(center[2] - found[0])
                diffs.append(diff)
                own.append(abs(diff))
                key = ("volume" if body["kind"] == KIND_VOLUME else "river")
                kinds.setdefault(key, []).append(abs(diff))
            if own:
                medians.append(float(np.median(own)))
    a = np.abs(np.array(diffs)) if diffs else np.zeros(1)
    return {"points": len(diffs), "not water in the quadtree": missing, "median |dz| m": round(float(np.median(a)), 4),
            "p95 |dz| m": round(float(np.percentile(a, 95)), 4), "within 0.05 m": round(float((a <= 0.05).mean()), 4),
            "by kind (within 0.05 m)": {k: round(float((np.array(v) <= 0.05).mean()), 4) for k, v in kinds.items()},
            "bodies with quadtree water": len(medians), "bodies with a median |dz| within 0.05 m": sum(1 for m in medians if m <= 0.05)}


def samples(names: list[str], per_region: int = 400, seed: int = 20261018) -> dict:
    """Writes water/samples.json for the test of the reader of the client (tools/test-water.sh): random points of each region
    with a quadtree (half of them on the leaves with water), with the answer of the quadtree (tree_surface)."""
    out = env.content_dir() / "water"
    rng = np.random.default_rng(seed)
    regions, total = {}, 0
    for name in names:
        nodes = read_tree(name)
        if not nodes:
            continue
        points = []
        bodies = read_mesh(out / f"{name}.water") if (out / f"{name}.water").is_file() else []
        rx, ry = (int(v) for v in name[2:].split("_"))
        for k in range(per_region):
            if k % 2 == 1 and bodies:
                body = bodies[int(rng.integers(len(bodies)))]
                vertex = body["points"][int(rng.integers(len(body["points"])))]
                x, y = float(vertex[0] - rx * REGION_SIZE) + rng.uniform(-2, 2), float(vertex[1] - ry * REGION_SIZE) + rng.uniform(-2, 2)
                x, y = min(max(x, 0.0), REGION_SIZE - 0.001), min(max(y, 0.0), REGION_SIZE - 0.001)
            else:
                x, y = float(rng.uniform(0, REGION_SIZE)), float(rng.uniform(0, REGION_SIZE))
            found = tree_surface(nodes, x, y)
            points.append([round(x, 3), round(y, 3)] + ([found[0], found[1], found[2]] if found else []))
        regions[name] = points
        total += len(points)
    (out / "samples.json").write_text(json.dumps({"seed": seed, "regions": regions}))
    return {"regions": len(regions), "points": total, "with water": sum(1 for r in regions.values() for p in r if len(p) > 2)}


def main(argv=None) -> int:
    import argparse
    parser = argparse.ArgumentParser(prog="python3 -B -m swc_convert.water")
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("write", "check", "samples"):
        p = sub.add_parser(command)
        p.add_argument("names", nargs="*")
    args = parser.parse_args(argv)
    names = args.names or census.world_regions()
    run = {"write": write, "check": check, "samples": samples}[args.command]
    print(json.dumps(run(names), indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
