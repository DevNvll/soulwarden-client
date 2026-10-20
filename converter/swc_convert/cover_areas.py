"""The vegetation blockers of the regions for the ground cover (research/ground-cover.md, "The rule for the client").

Retail lets the vegetation areas of the level claim the sample slots of a sector before it uses the baked records of
region.vegetation: a blocker claims a slot and makes no mesh, and a baked record draws only if its slot is still free. The
shipped roads, houses, and plots have such blockers. A slot is a point of the sector lattice: 32 x 32 points of 0.5 m in
each 16 m tile, ordinal X-major (sampleX = tile origin + floor(ordinal / 32) x 0.5, sampleY = ... + (ordinal % 32) x 0.5);
the slot of a record is the tile and the u16 ordinal of its bytes 12 to 13.

The inputs are those of the placements (region.Region.placements): the static chunks of region.chunks (not the collision
chunks), the capitals, and the startup child slices of their prefab spawners. A blocker is an entity with the blocker
component and a shape (box, cylinder, sphere, polygon prism; compound shapes are counted, not used). The test point of a
slot is the lattice point at the height of the terrain (bilinear), as the conditional replay of the study (Oxboro: 7,821
records within 30 m of 8944.4, 4185.2, 4,674 after the blockers).

Not yet (open points of the study): the filter chain and the priorities of the areas, the inheritance of an enclosing area,
the areas that place their own meshes, the surface providers other than the terrain, and the variant replacement of a shape.

Output: regions/NAME/cover-blockers.json (the shapes in world meters, with their flags) and the claimed slots (claimed()),
which cover.py writes into cover.bin.
"""
from __future__ import annotations

import json
import struct
import uuid

import numpy as np

from . import env, region, source

F = source.field_id
ENTITY = uuid.UUID("75651658-8663-478d-9090-2432dfcafa44").bytes
BLOCKER = uuid.UUID("954683f7-7965-4686-bcdb-0f65767730d3").bytes
AREA = uuid.UUID("adca8ab2-924d-463f-a0ea-27d254f9682a").bytes
TRANSFORM = uuid.UUID("484ae67d-abd0-4d9c-b2c8-9bb0eec900e0").bytes
SHAPES = {uuid.UUID("5edf4b9e-0d3d-40b8-8c91-5142bcfc30a6").bytes: "box",
          uuid.UUID("b0c6aa97-e754-4e33-8d32-33e267db622f").bytes: "cylinder",
          uuid.UUID("e24cbff0-2531-4f8d-a8ab-47af4d54bcd2").bytes: "sphere",
          uuid.UUID("ad882674-1d5d-4e40-b079-449b47d2492c").bytes: "polygon",
          uuid.UUID("c0c817de-843f-44c8-9fc1-989cde66b662").bytes: "compound"}
IDENTITY_TM = struct.pack(">12f", 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)
TILES, LATTICE, STEP = 128, 32, 0.5   # tiles of a region, lattice points of a tile side, the lattice step (m)


def value(node, name: str, default=None):
    """@return The value of the first field NAME below NODE (depth first), or DEFAULT."""
    if node is None:
        return default
    key = F(name)
    for element in source.walk(node):
        if element.name == key:
            return element.value
    return default


def shapes_of_slice(data: bytes) -> list[dict]:
    """@return The blockers of one slice in the space of the slice: kind, matrix (4 x 4), size, flags."""
    out = []
    for root in source.parse(data):
        for node in source.walk(root):
            if node.type_id != ENTITY or node.child(F("Components")) is None:
                continue
            components = node.child(F("Components")).children
            blocker = next((c for c in components if c.type_id == BLOCKER), None)
            if blocker is None:
                continue
            transform = next((c for c in components if c.type_id == TRANSFORM), None)
            shape = next((c for c in components if c.type_id in SHAPES), None)
            area = next((c for c in components if c.type_id == AREA), None)
            row = {"entity": source.text(node.child(F("Name"))), "kind": SHAPES.get(shape.type_id) if shape else None,
                   "matrix": region.az_matrix(value(transform, "m_worldTM", IDENTITY_TM)),
                   "flags": {k: (int.from_bytes(value(area, k, b"\x00"), "big") if area is not None else None)
                             for k in ("Priority", "AreaType", "PlacementModeTypeFlag")}}
            row["flags"].update({k: int.from_bytes(value(blocker, k, b"\x00"), "big") for k in ("InheritBehavior", "UseRelativeUVW")})
            kind = row["kind"]
            if kind == "box":
                row["dimensions"] = list(struct.unpack(">3f", value(shape, "Dimensions")))
            elif kind in ("sphere", "cylinder"):
                row["radius"] = struct.unpack(">f", value(shape, "Radius"))[0]
                if kind == "cylinder":
                    row["height"] = struct.unpack(">f", value(shape, "Height"))[0]
            elif kind == "polygon":
                vertices = next((c for c in source.walk(shape) if c.name == F("Vertices")), None)
                row["vertices"] = [list(struct.unpack(">2f", v.value)) for v in vertices.children] if vertices else []
                row["height"] = struct.unpack(">f", value(shape, "Height"))[0]
            out.append(row)
    return out


def blockers(name: str) -> tuple[list[dict], dict]:
    """@return (the blockers of a region in world meters, counts). The inputs of region.Region.placements."""
    reg = region.Region(name)
    ox, oy = reg.origin
    cache: dict[str, tuple | None] = {}
    out: list[dict] = []
    counts = {"chunks": 0, "capitals": 0, "spawned slices": 0, "unread slices": 0}

    def place(path: str, matrix: np.ndarray, depth: int) -> None:
        if depth > 12:
            return
        if path not in cache:
            try:
                data = source.read(path)
                cache[path] = (shapes_of_slice(data), region.read_slice(data).spawners)
            except Exception:
                cache[path] = None
                counts["unread slices"] += 1
        item = cache[path]
        if item is None:
            return
        shapes, spawners = item
        for row in shapes:
            out.append(dict(row, matrix=(matrix @ row["matrix"]).tolist(), path=path))
        for sp in spawners:
            if sp.slice is None or not sp.on_start:
                continue
            child = source.product(sp.slice[0], sp.slice[1], ".dynamicslice")
            if child:
                counts["spawned slices"] += 1
                place(child, matrix @ region.rows4(sp.matrix), depth + 1)

    root = source.parse(source.read(reg.folder + "region.chunks"))[0]
    for ch in root.child(F("chunks")).children:
        if source.number(ch.child(F("chunkType"))) == 2:
            continue   # collision chunks
        x, y, z = struct.unpack(">3f", ch.child(F("WorldPosition")).value)
        a = source.asset_id(ch.child(F("AssetId")))
        path = source.product(a[0], a[1], ".dynamicslice") if a else None
        if path is None:
            continue
        counts["chunks"] += 1
        base = np.eye(4)
        base[:3, 3] = (x, y, z)
        place(path, base, 0)
    for cpath in sorted(p for p in source.paks().entries
                        if p.startswith(reg.folder + "capitals/") and p.endswith(".capitals.json")):
        for c in json.loads(source.read(cpath)).get("Capitals", []):
            if c.get("sliceName") == "<PLOT>":
                continue
            wp = c.get("worldPosition", {})
            if not (ox - 64 <= wp.get("x", -1e9) <= ox + 2048 + 64 and oy - 64 <= wp.get("y", -1e9) <= oy + 2048 + 64):
                continue
            path = None
            aid = str(c.get("sliceAssetId", ""))
            if aid.startswith("{") and not aid.startswith("{00000000"):
                path = source.product_of_text(aid, ".dynamicslice")
            if path is None:
                key = region.slice_key(c.get("sliceName", ""))
                path = next((p for p in ("slices/" + key + ".dynamicslice", key + ".dynamicslice") if source.has(p)), None)
            if path is None:
                continue
            counts["capitals"] += 1
            q = c.get("rotation", {})
            m = np.eye(4)
            m[:3, :3] = region.quat_matrix(q.get("x", 0.0), q.get("y", 0.0), q.get("z", 0.0), q.get("w", 1.0)) * float(c.get("scale", 1.0))
            m[:3, 3] = (wp["x"], wp["y"], wp["z"])
            place(path, m, 0)
    return out, counts


def inside(row: dict, points: np.ndarray) -> np.ndarray:
    """@return For each world point (N x 3, m) whether it is in the shape of a blocker (the tests of the study)."""
    m = np.linalg.inv(np.array(row["matrix"]))
    loc = points @ m[:3, :3].T + m[:3, 3]
    kind = row["kind"]
    if kind == "box":
        a = np.array(row["dimensions"]) / 2
        return np.all(np.abs(loc) <= a, axis=1)
    if kind == "sphere":
        return np.sum(loc ** 2, axis=1) <= row["radius"] ** 2
    if kind == "cylinder":
        return (loc[:, 0] ** 2 + loc[:, 1] ** 2 <= row["radius"] ** 2) & (np.abs(loc[:, 2]) <= row["height"] / 2)
    if kind == "polygon" and len(row["vertices"]) >= 3:
        v = np.array(row["vertices"])
        mask = np.zeros(len(points), bool)
        for a, b in zip(v, np.roll(v, -1, axis=0)):
            if b[1] != a[1]:
                mask ^= ((a[1] > loc[:, 1]) != (b[1] > loc[:, 1])) & \
                        (loc[:, 0] < (b[0] - a[0]) * (loc[:, 1] - a[1]) / (b[1] - a[1]) + a[0])
        return mask & (loc[:, 2] >= 0) & (loc[:, 2] <= row["height"])
    return np.zeros(len(points), bool)


def reach(row: dict) -> float:
    """@return The radius (m) of a sphere about the origin of the blocker that holds its shape (with the scale)."""
    scale = float(np.max(np.linalg.norm(np.array(row["matrix"])[:3, :3], axis=0)))
    kind = row["kind"]
    if kind == "box":
        size = float(np.linalg.norm(row["dimensions"])) / 2
    elif kind == "sphere":
        size = row["radius"]
    elif kind == "cylinder":
        size = float(np.hypot(row["radius"], row["height"] / 2))
    elif kind == "polygon" and row["vertices"]:
        size = float(np.max(np.linalg.norm(np.array(row["vertices"]), axis=1))) + row["height"]
    else:
        return 0.0
    return size * scale


def claimed(name: str, shapes: list[dict]) -> np.ndarray:
    """@return The claimed slots of a region: a bool array (4096 x 4096) by (lattice x, lattice y); lattice x = tileX x 32 +
    ordinal // 32, lattice y = tileY x 32 + ordinal % 32. The test point is the lattice point at the terrain height."""
    reg = region.Region(name)
    ox, oy = reg.origin
    heights = reg.heights().astype(np.float64) * region.HEIGHT_SCALE
    side = TILES * LATTICE
    out = np.zeros((side, side), bool)
    for row in shapes:
        r = reach(row)
        if r <= 0.0:
            continue
        cx, cy = row["matrix"][0][3], row["matrix"][1][3]
        x0, x1 = int(np.floor((cx - r - ox) / STEP)), int(np.ceil((cx + r - ox) / STEP))
        y0, y1 = int(np.floor((cy - r - oy) / STEP)), int(np.ceil((cy + r - oy) / STEP))
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, side - 1), min(y1, side - 1)
        if x0 > x1 or y0 > y1:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1), indexing="ij")
        lx, ly = gx.ravel() * STEP, gy.ravel() * STEP
        xi, yi = np.minimum(lx.astype(int), 2046), np.minimum(ly.astype(int), 2046)
        fx, fy = lx - xi, ly - yi
        z = (heights[yi, xi] * (1 - fx) + heights[yi, xi + 1] * fx) * (1 - fy) + \
            (heights[yi + 1, xi] * (1 - fx) + heights[yi + 1, xi + 1] * fx) * fy
        points = np.stack([lx + ox, ly + oy, z], axis=1)
        hit = inside(row, points)
        out[gx.ravel()[hit], gy.ravel()[hit]] = True
    return out


def tile_bits(slots: np.ndarray) -> dict[int, bytes]:
    """@return The claimed ordinals of each tile with a claim: tile index (tileY x 128 + tileX) -> 128 bytes (bit ordinal)."""
    out = {}
    blocks = slots.reshape(TILES, LATTICE, TILES, LATTICE)   # (tileX, ordinal // 32, tileY, ordinal % 32)
    for tx, ty in zip(*np.nonzero(blocks.any(axis=(1, 3)))):
        ordinal_bits = blocks[tx, :, ty, :].reshape(-1)   # ordinal = (ordinal // 32) x 32 + ordinal % 32
        out[int(ty) * TILES + int(tx)] = np.packbits(ordinal_bits, bitorder="little").tobytes()
    return out


def write(name: str) -> tuple[dict, np.ndarray]:
    """Writes regions/NAME/cover-blockers.json. @return (a report, the claimed slots of claimed())."""
    shapes, counts = blockers(name)
    slots = claimed(name, shapes)
    folder = env.content_dir() / "regions" / name
    folder.mkdir(parents=True, exist_ok=True)
    kinds: dict = {}
    for row in shapes:
        kinds[str(row["kind"])] = kinds.get(str(row["kind"]), 0) + 1
    (folder / "cover-blockers.json").write_text(json.dumps({"version": 1, "region": name, "counts": counts, "kinds": kinds,
                                                            "blockers": shapes}, indent=None))
    return {"region": name, **counts, "blockers": len(shapes), "kinds": kinds, "claimed slots": int(slots.sum())}, slots
