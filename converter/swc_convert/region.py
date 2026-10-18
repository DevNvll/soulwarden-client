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
MAP, SLICE_NAME, VARIANT_NAME = F("sliceMetaDataMap"), F("sliceName"), F("variantName")
MESHES, MESH_ID, ROOT_TM, INSTANCE_TMS = F("meshes"), F("meshAssetId"), F("rootRelativeTransform"), F("rootRelativeInstanceTransforms")


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


def slice_meshes(data: bytes) -> list[tuple[str, np.ndarray]]:
    """@return (mesh path, entity world matrix) of each entity of a slice with a static mesh component."""
    out = []
    for e in source.walk(source.parse(data)[0]):
        comps = e.child(COMPONENTS)
        if comps is None:
            continue
        tm, mesh = None, None
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
        if mesh and mesh.endswith(".cgf"):
            out.append((mesh, tm if tm is not None else np.eye(4)))
    return out


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
            for mesh, tm in slice_meshes(source.read(path)):
                m = base @ tm
                if inside(m[0, 3], m[1, 3]):
                    out.append({"source": "chunk", "mesh": mesh, "matrix": m.tolist()})
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
                    for im in (mats or [np.eye(4)]):
                        out.append({"source": "capital variant", "mesh": mp, "matrix": (matrix @ root_m @ im).tolist()})
                        counts["capital variant"] += 1
            else:
                for mp, tm in meshes:
                    out.append({"source": "capital", "mesh": mp, "matrix": (matrix @ tm).tolist()})
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
        names, _variants, idx, pos, rot, scale = read_distribution(source.read(self.folder + "region.distribution"))
        lx = (pos >> 16) * 2048 / 65535.0
        ly = (pos & 0xFFFF) * 2048 / 65535.0
        slices: dict[str, list | None] = {}
        wx, wy = lx + ox, ly + oy
        for r in np.nonzero((wx >= x0) & (wx <= x0 + size) & (wy >= y0) & (wy <= y0 + size))[0]:
            name = names[idx[r]].lower()
            if name not in slices:
                p = name if name.endswith(".dynamicslice") else name + ".dynamicslice"
                p = p if p.startswith("slices/") else "slices/" + p
                try:
                    slices[name] = slice_meshes(source.read(p))
                except Exception:
                    slices[name] = None
            meshes = slices[name]
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
            for mesh, tm in meshes:
                out.append({"source": "distribution", "mesh": mesh, "matrix": (m @ tm).tolist()})
                counts["distribution"] += 1
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
