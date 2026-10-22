"""One region of a world: the height map (16-bit PNG), the macro color map (PNG), and the placements (JSON).

Placement rules (proved in the research AST-135 for region r_+04_+02, and in the navigation research of Soulwarden):
- chunk entity:   T(chunk WorldPosition) x entity m_worldTM
- capital:        T(position) R(rotation) S(scale) x spawner transforms x entity m_worldTM; a variant uses the meshes
                  of the slice metadata (rootRelativeTransform x instance transforms)
- distribution:   packed position, rotation, scale; the height is the terrain height
Matrices are 4 x 4, row major, in engine space (meters, X right, Y forward, Z up, right-handed).
Not done yet: the terrain cookies of the capitals, the grass (.vegetation), water, hidden world variants.
"""
from __future__ import annotations

import functools
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image

from . import dds, env, source
from nav_slices import matrix_of, read_slice
from nav_terrain import HEIGHT_SCALE, read_heightmap

WORLD = "sharedassets/coatlicue/newworld_vitaeeterna/"
F = source.field_id
SMRN, STATIC_MESH, WORLD_TM, COMPONENTS = F("Static Mesh Render Node"), F("Static Mesh"), F("m_worldTM"), F("Components")
VISIBLE = F("Visible")
# An entity with IsRuntimeActive 0 is not activated when its slice starts. Most such entities with a mesh (1,922 meshes in 233
# slices of the world, tools/lib/inactive_report.py) are parts that a script, a quest, an event, or an interaction activates later
# (POIs, world events, faction control fills, doors, quest portals): they stay. Only the clear helpers go: an inactive entity named
# "MeshVisualizer" (the cubes of the Great Cleave weather boxes, slices/ambience/snow_weatherbox_*, scaled to 1,400 m, with no
# material: white boxes in the client before; AST-261).
IS_RUNTIME_ACTIVE = F("IsRuntimeActive")
INACTIVE_HELPERS = (b"MeshVisualizer",)
SKIP_INACTIVE = True             # False: the meshes of the inactive helpers stay (the report of tools/lib/inactive_report.py)
INACTIVE_LOG: list | None = None  # a list: slice_meshes appends (entity name, mesh, instances) of each inactive entity with a mesh


def runtime_active(entity) -> bool:
    """@return False when the entity has IsRuntimeActive 0 (a missing field is active)."""
    node = entity.child(IS_RUNTIME_ACTIVE)
    return node is None or node.value != b"\x00"


def inactive_helper(entity) -> bool:
    """@return True for an inactive helper entity whose meshes retail never draws (INACTIVE_HELPERS by its name)."""
    name = entity.child(NAME)
    return not runtime_active(entity) and name is not None and name.value in INACTIVE_HELPERS
NAME = F("Name")
# Gathering nodes (P4.1): the level shows only the healthy mesh of a node; the client shows the depleted mesh at run time
# when the server says that the node is depleted (the creature track). Two rules of the game data:
# 1. The master slices of slices/gatherables name the entity of the depleted mesh DepletedMesh or Depleted (both
#    entities of a node are Visible).
# 2. The variation tables (VariationData: StartMesh, DepletedMesh of each variant): a DepletedMesh is not placed where
#    the StartMesh of its row stands (within DEPLETED_PLACE_M; for example the BaseMesh of the alchemy nodes).
DEPLETED_ENTITIES = (b"DepletedMesh", b"Depleted")
DEPLETED_PLACE_M = 0.1
MAP, SLICE_NAME, VARIANT_NAME = F("sliceMetaDataMap"), F("sliceName"), F("variantName")
MESHES, MESH_ID, ROOT_TM, INSTANCE_TMS = F("meshes"), F("meshAssetId"), F("rootRelativeTransform"), F("rootRelativeInstanceTransforms")
# The material override of a placement (P4.1): the field of the mesh render node of a slice entity, and the field of a
# mesh of the slice metadata (a variant: for example the fall material of a poplar of Oxboro). It replaces the material
# of the mesh (all its sub-materials, by index).
MATERIAL_OVERRIDE_ASSET, MATERIAL_OVERRIDE_ID = F("Material Override Asset"), F("materialOverrideAssetId")
# The instanced mesh component of a slice entity (S0 of the settlement research): its render node has the fields of the
# static node in its BaseClass1, and its Instance Transforms (a child of the node) are the transforms of the instances in
# the entity space.
IMRN, BASE, INSTANCE_TRANSFORMS, ID = F("Instanced Mesh Render Node"), F("BaseClass1"), F("Instance Transforms"), F("Id")
# The wind of a placement: the bool receiveWind of the Render Options of the mesh render node (of the BaseClass1 of an
# instanced node). Oxboro chunk slices: 40 of 2,088 render nodes. The mesh of a slice metadata variant and a distribution
# row have no field of this name (a variant mesh has two fields with no known name, 0x755c7d72 bool and 0x9ae4b28c u32):
# no wind. The level gives a component with wind the custom primitive data 0 "SwcWind" = 1 (cells.py, E1).
RENDER_OPTIONS, RECEIVE_WIND = F("Render Options"), F("receiveWind")


def slice_key(name: str) -> str:
    name = name.lower().replace("\\", "/")
    while "//" in name:
        name = name.replace("//", "/")
    for ending in (".dynamicslice", ".slice"):
        if name.endswith(ending):
            name = name[:-len(ending)]
    return name[7:] if name.startswith("slices/") else name


def quat_matrix(x, y, z, w) -> np.ndarray:
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def rows4(m12) -> np.ndarray:
    m = np.eye(4)
    m[:3, :4] = np.array(m12, dtype=np.float64).reshape(3, 4)
    return m


def az_matrix(value: bytes) -> np.ndarray:
    v = struct.unpack(">12f", value)
    m = np.eye(4)
    m[:3, 0], m[:3, 1], m[:3, 2], m[:3, 3] = v[0:3], v[3:6], v[6:9], v[9:12]
    return m


def asset_path(element) -> str | None:
    """@return The pak path of an asset field: its asset id in the catalog, else its path text."""
    v = element.value
    if v is None or len(v) < 56:
        return None
    p = source.product(v[:16], int.from_bytes(v[16:20], "big"))
    if p:
        return p
    size = int.from_bytes(v[48:56], "big")
    return v[56:56 + size].decode("latin-1").lower() or None


def material_path(element) -> str | None:
    """@return The .mtl of a material asset field, or None (no override). Two forms: a value with the asset id (the mesh
    render node of a slice), or the children guid and subId (the meshes of the slice metadata)."""
    if element is None:
        return None
    if element.children:
        a = source.asset_id(element)
        path = source.product(a[0], a[1], ".mtl") if a and any(a[0]) else None
    elif element.value and any(element.value[:16]):
        path = asset_path(element)
    else:
        return None
    return path if path and path.endswith(".mtl") else None


def entity_id(entity) -> int:
    """@return The id of a slice entity (u64), or 0."""
    node = entity.child(ID)
    child = node.children[0] if node is not None and node.children else node
    return int.from_bytes(child.value, "big") if child is not None and child.value and len(child.value) == 8 else 0


def receives_wind(node) -> bool:
    """@return The receiveWind flag of the Render Options of a mesh render node (False when it has none)."""
    options = node.child(RENDER_OPTIONS) if node is not None else None
    flag = options.child(RECEIVE_WIND) if options is not None else None
    return flag is not None and flag.value == b"\x01"


def slice_meshes(data: bytes) -> list[tuple[str, np.ndarray, str | None, int, int, bool]]:
    """@return (mesh path, world matrix, material override or None, entity id, instance index or -1, wind) of each static
    mesh of a slice: the static mesh component of an entity, and each instance of an instanced mesh component (its
    matrix: the entity world matrix times the instance transform; research/settlements.md 5)."""
    out = []
    for e in source.walk(source.parse(data)[0]):
        comps = e.child(COMPONENTS)
        if comps is None:
            continue
        name = e.child(NAME)
        if name is not None and name.value in DEPLETED_ENTITIES:
            continue   # the depleted mesh of a gathering node (rule 1)
        active = runtime_active(e)
        helper = not active and inactive_helper(e)
        tm, mesh, material, wind = None, None, None, False
        instanced = []
        for comp in comps.children:
            for f in comp.children:
                if f.name == WORLD_TM and f.value is not None and len(f.value) == 48:
                    tm = az_matrix(f.value)
                if f.name == SMRN:
                    visible = f.child(VISIBLE)
                    if visible is not None and visible.value == b"\x00":
                        continue   # a hidden mesh (for example a helper volume of the level design)
                    a = f.child(STATIC_MESH)
                    if a is not None:
                        mesh = asset_path(a)
                    material = material_path(f.child(MATERIAL_OVERRIDE_ASSET))
                    wind = receives_wind(f)
                if f.name == IMRN:
                    node = f.child(BASE) or f
                    visible = node.child(VISIBLE)
                    if visible is not None and visible.value == b"\x00":
                        continue
                    a = node.child(STATIC_MESH)
                    path = asset_path(a) if a is not None else None
                    transforms = f.child(INSTANCE_TRANSFORMS)   # a child of the render node
                    if path and path.endswith(".cgf") and transforms is not None:
                        instanced.append((path, material_path(node.child(MATERIAL_OVERRIDE_ASSET)),
                                          [az_matrix(t.value) for t in transforms.children if t.value and len(t.value) == 48],
                                          receives_wind(node)))
        world = tm if tm is not None else np.eye(4)
        ident = entity_id(e) if (mesh or instanced) else 0
        if not active and ((mesh and mesh.endswith(".cgf")) or instanced):
            if INACTIVE_LOG is not None:
                label = source.text(name) if name is not None else ""
                if mesh and mesh.endswith(".cgf"):
                    INACTIVE_LOG.append((label, mesh, 1))
                for path, _override, matrices, _bends in instanced:
                    INACTIVE_LOG.append((label, path, len(matrices)))
            if SKIP_INACTIVE and helper:
                continue   # a helper that is never activated
        if mesh and mesh.endswith(".cgf"):
            out.append((mesh, world, material, ident, -1, wind))
        for path, override, matrices, bends in instanced:
            for index, m in enumerate(matrices):
                out.append((path, world @ m, override, ident, index, bends))
    return out


@functools.lru_cache(maxsize=1)
def depleted_pairs() -> dict[str, frozenset[str]]:
    """@return For each DepletedMesh of the variation tables of the game: the StartMeshes of its rows (rule 2)."""
    from . import datasheet
    pairs: dict[str, set[str]] = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except (ValueError, struct.error):
            continue
        if sheet.type != "VariationData" or "StartMesh" not in sheet.columns or "DepletedMesh" not in sheet.columns:
            continue
        for row in sheet.rows():
            start = str(row.get("StartMesh", "")).lower().replace("\\", "/")
            depleted = str(row.get("DepletedMesh", "")).lower().replace("\\", "/")
            if start and depleted and start != depleted:
                pairs.setdefault(depleted, set()).add(start)
    return {k: frozenset(v) for k, v in pairs.items()}


def without_depleted(items: list[dict]) -> tuple[list[dict], int]:
    """@return The placements without each DepletedMesh whose StartMesh stands at its place (rule 2), and the count."""
    pairs = depleted_pairs()
    cell = DEPLETED_PLACE_M

    def key(mesh, m):
        return mesh, round(m[0][3] / cell), round(m[1][3] / cell), round(m[2][3] / cell)

    starts = {key(p["mesh"], p["matrix"]) for p in items}
    out, dropped = [], 0
    for p in items:
        mesh = p["mesh"]
        if mesh in pairs:
            _, x, y, z = key(mesh, p["matrix"])
            if any((s, x + dx, y + dy, z + dz) in starts for s in pairs[mesh]
                   for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)):
                dropped += 1
                continue
        out.append(p)
    return out, dropped


def read_distribution(data: bytes):
    position = 0
    if struct.unpack_from("<H", data, 0)[0] == 0xB9D6:
        position = 4
    (count,) = struct.unpack_from("<H", data, position)
    position += 2
    names = []
    for _ in range(count * 2):
        size = data[position]
        names.append(data[position + 1:position + 1 + size].decode("utf-8", "replace"))
        position += 1 + size
    (rows,) = struct.unpack_from("<I", data, position)
    position += 4
    idx = np.frombuffer(data, np.uint16, rows, position); position += rows * 2
    pos = np.frombuffer(data, np.uint32, rows, position); position += rows * 4
    rot = np.frombuffer(data, np.uint32, rows, position); position += rows * 4
    scale = np.frombuffer(data, np.uint8, rows, position)
    return names[:count], names[count:], idx, pos, rot, scale


class Region:
    def __init__(self, name: str) -> None:
        self.name = name
        rx, ry = (int(v) for v in name[2:].split("_"))
        self.index = (rx, ry)
        self.origin = (rx * 2048, ry * 2048)
        self.folder = WORLD + "regions/" + name + "/"
        self.out = env.content_dir() / "regions" / name
        self._heights = None

    def heights(self) -> np.ndarray:
        """@return The terrain samples (2048 x 2048, row 0 = local y 0), as uint16."""
        if self._heights is None:
            self._heights = np.frombuffer(read_heightmap(source.read(self.folder + "region.heightmap")),
                                          dtype=np.uint16).reshape(2048, 2048)
        return self._heights

    def height_at(self, lx: float, ly: float) -> float:
        h = self.heights()
        return float(h[min(max(int(ly), 0), 2047), min(max(int(lx), 0), 2047)]) * HEIGHT_SCALE

    def write_terrain(self) -> dict:
        self.out.mkdir(parents=True, exist_ok=True)
        h = self.heights()
        Image.fromarray(np.ascontiguousarray(h[::-1])).save(self.out / "height16.png")   # row 0 = north
        rx, ry = self.index
        macro = (f"materials/terrain/newworld_vitaeeterna/jav_newworld_vitaeeterna_{rx}_{ry}/"
                 f"jav_newworld_vitaeeterna_{rx}.{ry}.macro_diff.dds")
        info = {"region": self.name, "origin m": list(self.origin), "size m": 2048, "samples": [2048, 2048],
                "cell m": 1, "height scale m": HEIGHT_SCALE, "height min m": round(float(h.min()) * HEIGHT_SCALE, 3),
                "height max m": round(float(h.max()) * HEIGHT_SCALE, 3), "png row 0": "north (local y 2047)"}
        if source.has(macro):
            data, w, hh, fmt = dds.join(source.read, macro, 2048)
            dds.to_image(data, w, hh, fmt).convert("RGB").save(self.out / "macro_diff.png")
            info["macro"] = {"source": macro, "size": [w, hh]}
        (self.out / "terrain.json").write_text(json.dumps(info, indent=1))
        return info

    def write_patch(self, area: tuple[float, float, float], step: int = 2) -> Path:
        """Writes the terrain of an area as one glTF mesh with the macro color map (a test of the space map; the
        world level uses Landscape actors). The mesh origin is the south-west corner of the area at height 0."""
        from . import gltf
        x0, y0, size = area
        ox, oy = self.origin
        heights = self.heights().astype(np.float64) * HEIGHT_SCALE
        xs = np.arange(int(x0 - ox), int(x0 - ox + size) + 1, step)
        ys = np.arange(int(y0 - oy), int(y0 - oy + size) + 1, step)
        gx, gy = np.meshgrid(xs, ys)
        gz = heights[np.clip(gy, 0, 2047), np.clip(gx, 0, 2047)]
        p = np.stack([gx + ox - x0, gy + oy - y0, gz], -1).reshape(-1, 3)
        dzdx, dzdy = np.gradient(gz, step, axis=1), np.gradient(gz, step, axis=0)
        n = np.stack([-dzdx, -dzdy, np.ones_like(gz)], -1).reshape(-1, 3)
        n /= np.linalg.norm(n, axis=1, keepdims=True)
        uv = np.stack([gx / 2048.0, 1 - gy / 2048.0], -1).reshape(-1, 2)   # the macro map: row 0 = north
        nx, ny = len(xs), len(ys)
        a = (np.arange(ny - 1)[:, None] * nx + np.arange(nx - 1)[None, :]).reshape(-1)
        idx = np.concatenate([np.stack([a, a + 1, a + nx + 1], 1), np.stack([a, a + nx + 1, a + nx], 1)]).reshape(-1)
        out = self.out / ("patch-%d-%d-%d.gltf" % (int(x0), int(y0), int(size)))
        w = gltf.Writer()
        base = "macro_diff.png" if (self.out / "macro_diff.png").is_file() else None
        mat = w.material("terrain_macro", base)
        prim = w.primitive(gltf.z_up_to_y_up(p).astype(np.float32), idx, gltf.z_up_to_y_up(n).astype(np.float32),
                           uv.astype(np.float32), material=mat)
        w.doc["scenes"][0]["nodes"] = [w.node(name="terrain_patch", mesh=w.mesh("terrain_patch", [prim]))]
        w.save(out)
        return out

    def placements(self, area: tuple[float, float, float] | None = None) -> tuple[list[dict], dict]:
        """@return (placements, counts). `area` = (x0, y0, size) in world meters, or all of the region."""
        ox, oy = self.origin
        x0, y0, size = area if area else (ox, oy, 2048.0)

        def inside(x, y):
            return x0 <= x <= x0 + size and y0 <= y <= y0 + size

        out: list[dict] = []
        counts = {"chunk": 0, "capital": 0, "capital variant": 0, "distribution": 0, "capitals": 0,
                  "spawned slices": 0, "distribution rows with no mesh": 0}
        # chunk slices
        root = source.parse(source.read(self.folder + "region.chunks"))[0]
        for ch in root.child(F("chunks")).children:
            if source.number(ch.child(F("chunkType"))) == 2:
                continue   # collision chunks
            x, y, z = struct.unpack(">3f", ch.child(F("WorldPosition")).value)
            if not (x0 - 64 <= x <= x0 + size + 64 and y0 - 64 <= y <= y0 + size + 64):
                continue
            a = source.asset_id(ch.child(F("AssetId")))
            path = source.product(a[0], a[1], ".dynamicslice") if a else None
            if path is None:
                continue
            base = np.eye(4)
            base[:3, 3] = (x, y, z)
            for mesh, tm, material, ident, instance, wind in slice_meshes(source.read(path)):
                m = base @ tm
                if inside(m[0, 3], m[1, 3]):
                    out.append({"source": "chunk", "mesh": mesh, "matrix": m.tolist(), "material": material,
                                "entity": ident, "instance": instance, "wind": wind})
                    counts["chunk"] += 1
        # capitals
        metadata = {}
        mroot = source.parse(source.read(self.folder + "region.slicedata"))[0]
        for entry in mroot.child(MAP).children:
            if len(entry.children) == 2:
                k, v = entry.children
                metadata[(slice_key(source.text(k.child(SLICE_NAME))), source.text(k.child(VARIANT_NAME)).lower())] = v
        cache: dict[str, tuple | None] = {}

        def place(path: str, variant: str, matrix: np.ndarray, depth: int) -> None:
            if depth > 12:
                return
            if path not in cache:
                try:
                    data = source.read(path)
                    cache[path] = (slice_meshes(data), read_slice(data).spawners)
                except Exception:
                    cache[path] = None
            item = cache[path]
            if item is None:
                return
            meshes, spawners = item
            meta = metadata.get((slice_key(path), variant.lower())) if variant else None
            if meta is not None and meta.child(MESHES) is not None:
                for mesh in meta.child(MESHES).children:
                    a = source.asset_id(mesh.child(MESH_ID))
                    mp = source.product(a[0], a[1], ".cgf") if a else None
                    if mp is None:
                        continue
                    root_m = rows4(matrix_of(getattr(mesh.child(ROOT_TM), "value", None)) or (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))
                    inst = mesh.child(INSTANCE_TMS)
                    mats = [rows4(m) for m in (matrix_of(n.value) for n in inst.children) if m is not None] if inst is not None else []
                    material = material_path(mesh.child(MATERIAL_OVERRIDE_ID))
                    for im in (mats or [np.eye(4)]):
                        out.append({"source": "capital variant", "mesh": mp, "matrix": (matrix @ root_m @ im).tolist(),
                                    "material": material})
                        counts["capital variant"] += 1
            else:
                for mp, tm, material, ident, instance, wind in meshes:
                    out.append({"source": "capital", "mesh": mp, "matrix": (matrix @ tm).tolist(), "material": material,
                                "entity": ident, "instance": instance, "wind": wind})
                    counts["capital"] += 1
            for sp in spawners:
                if sp.slice is None or not sp.on_start:
                    continue
                child = source.product(sp.slice[0], sp.slice[1], ".dynamicslice")
                if child:
                    counts["spawned slices"] += 1
                    place(child, sp.variant or "", matrix @ rows4(sp.matrix), depth + 1)

        for cpath in sorted(p for p in source.paks().entries
                            if p.startswith(self.folder + "capitals/") and p.endswith(".capitals.json")):
            for c in json.loads(source.read(cpath)).get("Capitals", []):
                if c.get("sliceName") == "<PLOT>":
                    continue
                wp = c.get("worldPosition", {})
                if not inside(wp.get("x", -1e9), wp.get("y", -1e9)):
                    continue
                path = None
                aid = str(c.get("sliceAssetId", ""))
                if aid.startswith("{") and not aid.startswith("{00000000"):
                    path = source.product_of_text(aid, ".dynamicslice")
                if path is None:
                    key = slice_key(c.get("sliceName", ""))
                    path = next((p for p in ("slices/" + key + ".dynamicslice", key + ".dynamicslice") if source.has(p)), None)
                if path is None:
                    continue
                counts["capitals"] += 1
                q = c.get("rotation", {})
                m = np.eye(4)
                m[:3, :3] = quat_matrix(q.get("x", 0.0), q.get("y", 0.0), q.get("z", 0.0), q.get("w", 1.0)) * float(c.get("scale", 1.0))
                m[:3, 3] = (wp["x"], wp["y"], wp["z"])
                place(path, str(c.get("variantName", "")), m, 0)
        # distribution rows
        names, variants, idx, pos, rot, scale = read_distribution(source.read(self.folder + "region.distribution"))
        lx = (pos >> 16) * 2048 / 65535.0
        ly = (pos & 0xFFFF) * 2048 / 65535.0
        slices: dict = {}
        of_variant: set[tuple[str, str]] = set()
        counts["distribution rows of a variant"] = 0
        wx, wy = lx + ox, ly + oy
        for r in np.nonzero((wx >= x0) & (wx <= x0 + size) & (wy >= y0) & (wy <= y0 + size))[0]:
            name = names[idx[r]].lower()
            variant = variants[idx[r]].lower() if idx[r] < len(variants) else ""
            if (name, variant) not in slices:
                # The variant of the row (for example the ore of a master_minerals node): the meshes of its metadata in
                # region.slicedata, as for a capital; else the meshes of the slice.
                meta = metadata.get((slice_key(name), variant)) if variant else None
                if meta is not None and meta.child(MESHES) is not None:
                    found = []
                    for mesh in meta.child(MESHES).children:
                        a = source.asset_id(mesh.child(MESH_ID))
                        mp = source.product(a[0], a[1], ".cgf") if a else None
                        if mp is None:
                            continue
                        root_m = rows4(matrix_of(getattr(mesh.child(ROOT_TM), "value", None)) or (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))
                        inst = mesh.child(INSTANCE_TMS)
                        mats = [rows4(m) for m in (matrix_of(n.value) for n in inst.children) if m is not None] if inst is not None else []
                        material = material_path(mesh.child(MATERIAL_OVERRIDE_ID))
                        found.extend((mp, root_m @ im, material, 0, -1, False) for im in (mats or [np.eye(4)]))
                    slices[(name, variant)] = found or None
                    of_variant.add((name, variant))
                else:
                    if name not in slices:
                        p = name if name.endswith(".dynamicslice") else name + ".dynamicslice"
                        p = p if p.startswith("slices/") else "slices/" + p
                        try:
                            slices[name] = slice_meshes(source.read(p))
                        except Exception:
                            slices[name] = None
                    slices[(name, variant)] = slices[name]
            meshes = slices[(name, variant)]
            if (name, variant) in of_variant and meshes:
                counts["distribution rows of a variant"] += 1
            if not meshes:
                counts["distribution rows with no mesh"] += 1
                continue
            v = int(rot[r])
            qx = ((v >> 21) & 0x3FF) / 255.0 - 1
            qy = ((v >> 11) & 0x3FF) / 255.0 - 1
            qz = (v & 0x7FF) / 511.0 - 1
            qw = math.sqrt(max(0.0, 1 - min(1.0, qx * qx + qy * qy + qz * qz))) * (-1 if v >> 31 else 1)
            m = np.eye(4)
            m[:3, :3] = quat_matrix(qx, qy, qz, qw) * (scale[r] * 0.01)
            m[:3, 3] = (lx[r] + ox, ly[r] + oy, self.height_at(lx[r], ly[r]))
            for mesh, tm, material, ident, instance, wind in meshes:
                out.append({"source": "distribution", "mesh": mesh, "matrix": (m @ tm).tolist(), "material": material,
                            "entity": ident, "instance": instance, "wind": wind})
                counts["distribution"] += 1
        out, counts["depleted meshes left out (variation rows)"] = without_depleted(out)
        return out, counts

    def write_placements(self, area=None) -> dict:
        self.out.mkdir(parents=True, exist_ok=True)
        items, counts = self.placements(area)
        name = "placements.json" if area is None else "placements-%d-%d-%d.json" % tuple(int(v) for v in area)
        (self.out / name).write_text(json.dumps({"region": self.name, "area": area, "space": "engine (m, Z up, right-handed)",
                                                 "counts": counts, "placements": items}))
        return {"file": str(self.out / name), "placements": len(items), "unique meshes": len({p["mesh"] for p in items}),
                **counts}


def convert(name: str, area=None) -> dict:
    region = Region(name)
    report = {"terrain": region.write_terrain(), "placements": region.write_placements(area)}
    return report


def placement_file(name: str, area=None) -> Path:
    region = Region(name)
    return region.out / ("placements.json" if area is None else "placements-%d-%d-%d.json" % tuple(int(v) for v in area))
