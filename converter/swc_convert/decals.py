"""Roads and projected decals of the world (stage M5 of the materials track, with S1 of the settlements research).

docs/MATERIALS.md, research/settlements.md (sections 4, 6, 7) and research/world-materials.md (section 4).

The converter (region.py) reads the static meshes of the slices. The slices also have two kinds of surface that it
skips:
- A road: an entity with a spline component (a Bezier spline: vertices, the forward and back handles and the roll
  angle of each vertex, closed, granularity) and a road component (the default width and a width value for each vertex,
  the segment length, the tile length, the sort priority, the view distance, the minimum spec, the material). For
  example the cobblestones of the Oxboro spawn lane: road entity 000025057818b9f9, 5 m wide.
- A projected decal: an entity with a decal component (DecalConfiguration: Visible, ProjectionType, Material,
  SortPriority, Depth, Offset, Color, Opacity, Deferred, Max View Distance, View Distance Multiplier, Min Spec). The
  scale of the entity matrix is the size of the projection box.

The walk: the chunk slices of a region (the chunk position and the entity matrix), the capital slices (the capital
matrix) and the slices that they spawn at the start (the spawner matrix, as region.Region.placements), and the
distribution slices are not walked (no road or decal component in the census of the research).

Usage (tools/convert-materials.sh, through material_records.main): decals scan NAME... | decals lane
"""
from __future__ import annotations

import json
import struct
import time

import numpy as np

from . import env, region, source

F = source.field_id
SPLINE_TYPE = "f09052971e244044bfdabde3583f1e57"
ROAD_TYPE = "3700da70407d47a6b7884654edda32a1"
DECAL_TYPE = "1c2ceaa8786f46848202ca7d940d627b"
FLOAT_TYPE = "ea2c3e90afbe44d4a90dfaaf79baf93d"
COMPONENTS, WORLD_TM, NAME, ID = F("Components"), F("m_worldTM"), F("Name"), F("Id")
VERSION = 1   # the rule of the road and decal records


def out_dir(name: str):
    path = env.content_dir() / "decals" / name
    path.mkdir(parents=True, exist_ok=True)
    return path


def find(node, name: str):
    """@return The first node with a field name under `node` (depth first), or None."""
    field = F(name)
    for n in source.walk(node):
        if n.name == field:
            return n
    return None


def find_all(node, name: str) -> list:
    field = F(name)
    return [n for n in source.walk(node) if n.name == field]


def vec3(node) -> list[float] | None:
    v = getattr(node, "value", None)
    return [float(x) for x in struct.unpack(">3f", v)] if v is not None and len(v) == 12 else None


def scalar(node, default=None):
    """@return A float (4 bytes of the float type), an int (other values of at most 8 bytes), or the default."""
    v = getattr(node, "value", None)
    if v is None:
        return default
    if node.type_id.hex() == FLOAT_TYPE and len(v) == 4:
        return float(struct.unpack(">f", v)[0])
    if len(v) <= 8:
        return int.from_bytes(v, "big")
    return default


def material_path(node) -> str | None:
    """@return The .mtl path of a material field (the asset id in the catalog, else the path text)."""
    if node is None:
        return None
    fallback = None
    for n in source.walk(node):
        if n.value is None:
            continue
        try:
            text = source.text(n)
        except Exception:
            text = None
        if text and text.lower().endswith(".mtl"):
            return text.lower().replace("\\", "/")   # the path text of the field (AssetPath)
        path = region.asset_path(n)
        if path and path.endswith(".mtl") and source.has(path):
            fallback = fallback or path
    return fallback


def entity_matrix(comps) -> np.ndarray:
    for comp in comps.children:
        for f in comp.children:
            if f.name == WORLD_TM and f.value is not None and len(f.value) == 48:
                return region.az_matrix(f.value)
    return np.eye(4)


def road_record(spline, road) -> dict:
    """@return The fields of a road: the Bezier spline and the road part (all values as in the source)."""
    vertices = [vec3(n) for n in (find(spline, "Vertices").children[0].children if find(spline, "Vertices") else [])]
    if vertices and vertices[0] is None:   # the vertex list is one level deeper (Vertices / Vertices / ELEMENT)
        vertices = []
    if not vertices:
        outer = find(spline, "Vertices")
        inner = [c for c in outer.children if c.children] if outer is not None else []
        vertices = [vec3(n) for n in (inner[0].children if inner else [])]
    bezier = find(spline, "Bezier Data")
    handles = []
    for element in (bezier.children if bezier is not None else []):
        handles.append({"forward": vec3(find(element, "forward")), "back": vec3(find(element, "Back")),
                        "angle": scalar(find(element, "Angle"), 0.0)})
    width = find(road, "Width")
    attributes = find(width, "elements") if width is not None else None
    return {"vertices": vertices, "bezier": handles, "closed": bool(scalar(find(spline, "Closed"), 0)),
            "granularity": scalar(find(spline, "granularity"), 8), "spline type": scalar(find(spline, "Spline Type")),
            "default width": scalar(find(width, "DefaultWidth"), 0.0) if width is not None else None,
            "widths": [scalar(n, 0.0) for n in attributes.children] if attributes is not None else [],
            "segment length": scalar(find(road, "SegmentLength")), "tile length": scalar(find(road, "TileLength")),
            "sort priority": scalar(find(road, "SortPriority"), 0),
            "view distance multiplier": scalar(find(road, "ViewDistanceMultiplier"), 1.0),
            "min spec": scalar(find(road, "MinSpec")), "material": material_path(find(road, "material")),
            "ignore terrain holes": bool(scalar(find(road, "IgnoreTerrainHoles"), 0)),
            "allow road follow": bool(scalar(find(road, "AllowRoadFollow"), 0))}


def decal_record(decal) -> dict:
    """@return The fields of a projected decal (DecalConfiguration)."""
    config = find(decal, "DecalConfiguration") or decal
    color = find(config, "Color")
    color_value = None
    if color is not None and color.value is not None and len(color.value) == 16:
        color_value = [float(x) for x in struct.unpack(">4f", color.value)]
    return {"visible": bool(scalar(find(config, "Visible"), 1)), "projection": scalar(find(config, "ProjectionType")),
            "material": material_path(find(config, "Material")), "sort priority": scalar(find(config, "SortPriority"), 0),
            "depth": scalar(find(config, "Depth"), 1.0), "offset": vec3(find(config, "Offset")),
            "color": color_value, "opacity": scalar(find(config, "Opacity"), 1.0),
            "deferred": bool(scalar(find(config, "Deferred"), 0)),
            "max view distance": scalar(find(config, "Max View Distance")),
            "view distance multiplier": scalar(find(config, "View Distance Multiplier"), 1.0),
            "min spec": scalar(find(config, "Min Spec"))}


def slice_items(data: bytes) -> tuple[list[dict], list[dict], list]:
    """@return (roads, decals, spawners) of one slice: each road and decal with its entity id, name, and matrix in the
    space of the slice."""
    from nav_slices import entity_id, read_slice
    roads, decals = [], []
    for e in source.walk(source.parse(data)[0]):
        comps = e.child(COMPONENTS)
        if comps is None:
            continue
        kinds = {c.type_id.hex(): c for c in comps.children}
        if SPLINE_TYPE not in kinds and DECAL_TYPE not in kinds:
            continue
        ident = entity_id(e.child(ID))
        entity = {"entity": f"{ident:016x}" if ident is not None else None,
                  "name": source.text(e.child(NAME)) if e.child(NAME) is not None else "",
                  "matrix": entity_matrix(comps)}
        if SPLINE_TYPE in kinds and ROAD_TYPE in kinds:
            roads.append({**entity, **road_record(kinds[SPLINE_TYPE], kinds[ROAD_TYPE])})
        if DECAL_TYPE in kinds:
            decals.append({**entity, **decal_record(kinds[DECAL_TYPE])})
    return roads, decals, read_slice(data).spawners


def region_items(name: str) -> dict:
    """@return The roads and decals of a region in engine space (each with its source slice, the chain of the capital
    or chunk, and its world matrix)."""
    folder = region.WORLD + "regions/" + name + "/"
    cache: dict[str, tuple | None] = {}
    roads, decals = [], []
    counts = {"chunk slices": 0, "capitals": 0, "spawned slices": 0, "read errors": 0}

    def items(path: str):
        if path not in cache:
            try:
                cache[path] = slice_items(source.read(path))
            except Exception:
                counts["read errors"] += 1
                cache[path] = None
        return cache[path]

    def add(path: str, matrix: np.ndarray, origin: str, depth: int = 0) -> None:
        if depth > 12:
            return
        found = items(path)
        if found is None:
            return
        slice_roads, slice_decals, spawners = found
        for target, rows in ((roads, slice_roads), (decals, slice_decals)):
            for r in rows:
                target.append({**r, "matrix": (matrix @ r["matrix"]).tolist(), "slice": path, "origin": origin})
        for sp in spawners:
            if sp.slice is None or not sp.on_start:
                continue
            child = source.product(sp.slice[0], sp.slice[1], ".dynamicslice")
            if child:
                counts["spawned slices"] += 1
                add(child, matrix @ region.rows4(sp.matrix), origin, depth + 1)

    if source.has(folder + "region.chunks"):
        root = source.parse(source.read(folder + "region.chunks"))[0]
        for ch in root.child(F("chunks")).children:
            if source.number(ch.child(F("chunkType"))) == 2:
                continue   # collision chunks
            x, y, z = struct.unpack(">3f", ch.child(F("WorldPosition")).value)
            a = source.asset_id(ch.child(F("AssetId")))
            path = source.product(a[0], a[1], ".dynamicslice") if a else None
            if path is None:
                continue
            counts["chunk slices"] += 1
            base = np.eye(4)
            base[:3, 3] = (x, y, z)
            add(path, base, "chunk")
    for cpath in sorted(p for p in source.paks().entries
                        if p.startswith(folder + "capitals/") and p.endswith(".capitals.json")):
        for c in json.loads(source.read(cpath)).get("Capitals", []):
            if c.get("sliceName") == "<PLOT>":
                continue
            aid = str(c.get("sliceAssetId", ""))
            path = source.product_of_text(aid, ".dynamicslice") if aid.startswith("{") and not aid.startswith("{00000000") else None
            if path is None:
                key = region.slice_key(c.get("sliceName", ""))
                path = next((p for p in ("slices/" + key + ".dynamicslice", key + ".dynamicslice") if source.has(p)), None)
            if path is None:
                continue
            counts["capitals"] += 1
            q, wp = c.get("rotation", {}), c.get("worldPosition", {})
            m = np.eye(4)
            m[:3, :3] = region.quat_matrix(q.get("x", 0.0), q.get("y", 0.0), q.get("z", 0.0), q.get("w", 1.0)) * float(c.get("scale", 1.0))
            m[:3, 3] = (wp.get("x", 0.0), wp.get("y", 0.0), wp.get("z", 0.0))
            add(path, m, "capital " + str(c.get("id", "")))
    return {"region": name, "version": VERSION, "roads": roads, "decals": decals, "counts": counts}


def scan(names: list[str]) -> dict:
    """Writes SWC_CONTENT/decals/NAME/items.json for each region. @return The counts."""
    started = time.time()
    out = {}
    for name in names:
        result = region_items(name)
        (out_dir(name) / "items.json").write_text(json.dumps(result))
        out[name] = {"roads": len(result["roads"]), "decals": len(result["decals"]), **result["counts"]}
    return {"regions": out, "seconds": round(time.time() - started, 1)}


# The road ribbon (the geometry of the road builder of the source, rebuilt): research/settlements.md section 8 states the
# assumptions (the standard cubic Bezier of each segment, the width interpolated between the vertices).
SAMPLES = 64          # Bezier samples of each spline segment before the resampling by distance
ACROSS = 5            # vertices across the road (0, 1/4, 1/2, 3/4, 1 of the width)
LIFT_M = 0.04         # the ribbon over the terrain (the source draws the road as a decal on the terrain)


def centerline(road: dict) -> tuple[np.ndarray, np.ndarray] | None:
    """@return (points in engine space (n, 3), widths (n,)) of the road spline, dense: segment i goes from vertex i
    (its forward handle) to vertex i + 1 (its back handle); the width is DefaultWidth + the width value of each vertex,
    linear in the segment parameter."""
    vertices = road["vertices"]
    handles = road["bezier"]
    n = len(vertices)
    if n < 2 or len(handles) != n or any(v is None for v in vertices):
        return None
    m = np.asarray(road["matrix"], dtype=np.float64)
    widths = road["widths"] if len(road["widths"]) == n else [0.0] * n
    base = float(road["default width"] or 0.0)
    segments = n if road["closed"] else n - 1
    t = np.linspace(0.0, 1.0, SAMPLES, endpoint=False)[:, None]
    points, ws = [], []
    for i in range(segments):
        j = (i + 1) % n
        p0, p3 = np.asarray(vertices[i]), np.asarray(vertices[j])
        p1 = np.asarray(handles[i]["forward"] or vertices[i])
        p2 = np.asarray(handles[j]["back"] or vertices[j])
        b = (1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t ** 2 * p2 + t ** 3 * p3
        points.append(b)
        ws.append((base + widths[i]) * (1 - t[:, 0]) + (base + widths[j]) * t[:, 0])
    last = 0 if road["closed"] else n - 1
    points.append(np.asarray(vertices[last])[None, :])
    ws.append(np.array([base + widths[last]]))
    local = np.concatenate(points)
    world = local @ m[:3, :3].T + m[:3, 3]
    return world, np.concatenate(ws)


class Heights:
    """The terrain height of the Landscape (regions/NAME/landscape.r16, terrain.landscape), bilinear."""

    def __init__(self) -> None:
        self.grids: dict = {}

    def __call__(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        shape = np.shape(x)
        return self.flat(np.ravel(x), np.ravel(y)).reshape(shape)

    def flat(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        from . import terrain
        out = np.full(x.shape, np.nan)
        rx, ry = np.floor(x / 2048).astype(int), np.floor(y / 2048).astype(int)
        for key in set(zip(rx.tolist(), ry.tolist())):
            name = f"r_+{key[0]:02d}_+{key[1]:02d}"
            if name not in self.grids:
                path = env.content_dir() / "regions" / name / "landscape.r16"
                self.grids[name] = (np.frombuffer(path.read_bytes(), dtype="<u2").reshape(terrain.VERTICES, terrain.VERTICES)
                                    .astype(np.float64) * region.HEIGHT_SCALE if path.is_file() else None)
            grid = self.grids[name]
            sel = (rx == key[0]) & (ry == key[1])
            if grid is None:
                continue
            last = terrain.VERTICES - 1
            col = (x[sel] - key[0] * 2048) / 2048 * last
            row = last - (y[sel] - key[1] * 2048) / 2048 * last   # row 0 = north
            c0 = np.clip(np.floor(col).astype(int), 0, last - 1)
            r0 = np.clip(np.floor(row).astype(int), 0, last - 1)
            fc, fr = col - c0, row - r0
            top = grid[r0, c0] * (1 - fc) + grid[r0, c0 + 1] * fc
            bottom = grid[r0 + 1, c0] * (1 - fc) + grid[r0 + 1, c0 + 1] * fc
            out[sel] = top * (1 - fr) + bottom * fr
        return out


def ribbon(road: dict, heights: Heights) -> dict | None:
    """@return The ribbon of a road in engine space: positions (rows x ACROSS, 3), uvs, and the distances along the
    road. Rows every SegmentLength (and the end); u goes across (0..1), v = distance / TileLength."""
    line = centerline(road)
    if line is None:
        return None
    points, widths = line
    step = np.linalg.norm(np.diff(points[:, :2], axis=0), axis=1)
    distance = np.concatenate([[0.0], np.cumsum(step)])
    length = float(distance[-1])
    segment = float(road["segment length"] or 2.0)
    if length < 1e-3:
        return None
    rows = np.unique(np.concatenate([np.arange(0.0, length, segment), [length]]))
    cx = np.interp(rows, distance, points[:, 0])
    cy = np.interp(rows, distance, points[:, 1])
    w = np.interp(rows, distance, widths)
    tangent = np.gradient(np.stack([cx, cy], 1), axis=0)
    tangent /= np.maximum(np.linalg.norm(tangent, axis=1, keepdims=True), 1e-9)
    right = np.stack([tangent[:, 1], -tangent[:, 0]], 1)   # the right of the direction of travel, horizontal
    u = np.linspace(0.0, 1.0, ACROSS)
    px = cx[:, None] + right[:, 0:1] * (u[None, :] - 0.5) * w[:, None]
    py = cy[:, None] + right[:, 1:2] * (u[None, :] - 0.5) * w[:, None]
    pz = heights(px, py) + LIFT_M
    tile = float(road["tile length"] or 1.0)
    uv = np.stack([np.broadcast_to(u[None, :], px.shape), np.broadcast_to((rows / tile)[:, None], px.shape)], 2)
    return {"positions": np.stack([px, py, pz], 2), "uv": uv, "rows": rows, "length": length}


def centerline_distance(road: dict, x: float, y: float) -> float | None:
    """@return The horizontal distance of a point to the centerline of a road (the check of the research: 2.116 m)."""
    line = centerline(road)
    if line is None:
        return None
    p = line[0]
    a, b = p[:-1, :2], p[1:, :2]
    d = b - a
    t = np.clip(((np.array([x, y]) - a) * d).sum(1) / np.maximum((d * d).sum(1), 1e-12), 0, 1)
    return float(np.min(np.linalg.norm(a + d * t[:, None] - np.array([x, y]), axis=1)))
