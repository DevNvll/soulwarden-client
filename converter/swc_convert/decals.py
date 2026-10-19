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
VERSION = 5   # the rule of the road and decal records (2: road U along the road, V across; decals of type 2 project down;
              # 3: the ribbon vertices at most STEP_M apart, along and across; 4: the projected decals use dp_ instances;
              # 5: the specs have the reflectance: the specular map and SpecularColor)


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
ACROSS = 5            # the least count of vertices across the road (0, 1/4, 1/2, 3/4, 1 of the width)
LIFT_M = 0.04         # the ribbon over the terrain (the source draws the road as a decal on the terrain)
# The vertices of the ribbon at most 1 m apart, along and across: the grid of the Landscape. The source road builder
# clips its mesh to the terrain units, so the road follows the ground. With rows every SegmentLength (2 m) and 5
# vertices across, 22 % of the ribbon triangles of 400 Oxboro roads were under the ground (p99 0.23 m: the mesh decal
# fails the depth test there, and the ground shows through in straight-edged patches); with 1 m, 1.0 % (p99 below 0).
STEP_M = 1.0


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
    """@return The ribbon of a road in engine space: positions (rows x across, 3), uvs, and the distances along the
    road. Rows every SegmentLength or STEP_M (the smaller; and the end), the vertices across at most STEP_M apart;
    u = distance / TileLength, v goes across (0..1)."""
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
    rows = np.unique(np.concatenate([np.arange(0.0, length, min(segment, STEP_M)), [length]]))
    cx = np.interp(rows, distance, points[:, 0])
    cy = np.interp(rows, distance, points[:, 1])
    w = np.interp(rows, distance, widths)
    tangent = np.gradient(np.stack([cx, cy], 1), axis=0)
    tangent /= np.maximum(np.linalg.norm(tangent, axis=1, keepdims=True), 1e-9)
    right = np.stack([tangent[:, 1], -tangent[:, 0]], 1)   # the right of the direction of travel, horizontal
    u = np.linspace(0.0, 1.0, max(ACROSS, int(np.ceil(float(w.max()) / STEP_M)) + 1))
    px = cx[:, None] + right[:, 0:1] * (u[None, :] - 0.5) * w[:, None]
    py = cy[:, None] + right[:, 1:2] * (u[None, :] - 0.5) * w[:, None]
    pz = heights(px, py) + LIFT_M
    tile = float(road["tile length"] or 1.0)
    # U along the road (distance / TileLength), V across (0..1): the road maps of the source fade their alpha along V
    # (the cobblestone diffa: the first and last eighth of the rows have the mean alpha 0.75, the columns 0.94 all).
    uv = np.stack([np.broadcast_to((rows / tile)[:, None], px.shape), np.broadcast_to(u[None, :], px.shape)], 2)
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


# The export (S1): the road meshes of each cell and the decal list of each region, and the layer file of the client
# (Source/SoulwardenClient/World/SwcSurfaceLayer: it streams them near the view).
CELL_M = 256.0        # the cells of the road meshes (the cells of the level, cells.py)
ROOT = "/SwcGameContent"


def material_key(mtl: str | None) -> tuple[str | None, dict | None]:
    """@return (the key of the decal material instance, its render rules) of a road or decal material file: "d_" and the
    10 hex digits of the render key of its first drawn sub-material (material_records.render_rules)."""
    from . import material_records as mr
    if not mtl or not source.has(mtl):
        return None, None
    subs = mr.sub_materials(mtl)
    element = subs[0] if subs else None
    if element is None:
        return None, None
    rules = mr.render_rules(mr.source_record(mtl, 0, element))
    return "d_" + mr.render_key(rules)[2:], rules


def write_roads(name: str, roads: list[dict], heights: Heights, materials: dict) -> list[dict]:
    """Writes the road meshes of a region (one glTF for each cell and sort priority) and @return their records."""
    from . import gltf
    rx, ry = (int(v) for v in name[2:].split("_"))
    groups: dict[tuple, list] = {}
    for road in roads:
        key, rules = material_key(road["material"])
        if key is None:
            continue
        materials.setdefault(key, {"mtl": road["material"], "rules": rules, "uses": 0})["uses"] += 1
        rb = ribbon(road, heights)
        if rb is None or np.isnan(rb["positions"]).any():
            continue
        mid = rb["positions"][len(rb["rows"]) // 2, rb["positions"].shape[1] // 2]
        cell = (int(np.floor(mid[0] / CELL_M)), int(np.floor(mid[1] / CELL_M)))
        groups.setdefault((cell, int(road["sort priority"] or 0)), []).append((key, rb, road))
    records = []
    for ((cx, cy), priority), parts in sorted(groups.items()):
        origin = np.array([cx * CELL_M, cy * CELL_M, 0.0])
        w = gltf.Writer()
        slots: dict[str, int] = {}
        prims = []
        for key, rb, road in parts:
            rows, across = rb["positions"].shape[:2]
            pos = (rb["positions"].reshape(-1, 3) - origin).astype(np.float32)
            uv = rb["uv"].reshape(-1, 2).astype(np.float32)
            quads = [(r * across + c, (r + 1) * across + c, r * across + c + 1, (r + 1) * across + c + 1)
                     for r in range(rows - 1) for c in range(across - 1)]
            # Two triangles of each quad, with the face toward +Z (engine space, right handed).
            idx = np.array([[a, c, b, c, d, b] for a, b, c, d in quads], dtype=np.uint32).ravel()
            normals = np.tile(np.array([0.0, 0.0, 1.0], np.float32), (len(pos), 1))
            if key not in slots:
                slots[key] = w.material(key)
            prims.append(w.primitive(gltf.z_up_to_y_up(pos), idx, gltf.z_up_to_y_up(normals), uv, material=slots[key]))
        out = out_dir(name) / f"roads_{cx}_{cy}_{priority}.gltf"
        stem = out.stem
        w.doc["scenes"][0]["nodes"] = [w.node(name=stem, mesh=w.mesh(stem, prims))]
        w.save(out)
        from . import mesh as mesh_module
        records.append({"key": f"road:{name}:{cx}_{cy}_{priority}", "file": str(out.relative_to(env.content_dir())),
                        "hash": mesh_module.file_hash(out), "unreal": f"{ROOT}/Roads/R{rx}_{ry}_{cx}_{cy}_{priority}",
                        "origin m": origin.tolist(), "sort priority": priority, "roads": len(parts),
                        "materials": sorted(slots), "cell": [cx, cy]})
    return records


def decal_transforms(decals: list[dict], materials: dict) -> list[dict]:
    """@return The decals in Unreal space: the projection box (half sizes) from the columns of the entity matrix
    (right, up, front: the decal of the source projects along front, with the depth factor on front) and the Offset; an
    Unreal decal projects along its local X: X = front, Y = right, Z = up (a cyclic order keeps the hand)."""
    from . import space
    out = []
    for d in decals:
        if not d["visible"]:
            continue
        key, rules = material_key(d["material"])
        if key is None:
            continue
        materials.setdefault(key, {"mtl": d["material"], "rules": rules, "uses": 0})["uses"] += 1
        m = np.asarray(d["matrix"], dtype=np.float64)
        right, up, front = m[:3, 0], m[:3, 1], m[:3, 2]
        if d["projection"] == 2:
            # On the terrain (2): the decal projects straight down whatever the tilt of the entity (14,013 decals of
            # r_+04_+02: 9,417 have their Z vertical, 2,390 their Y, 2,206 their X; in each group X and Y have the
            # same median scale: the footprint is |X| x |Y|, the depth |Z| x Depth). The heading: the horizontal part
            # of X (of Y turned by -90 degrees when X is vertical).
            sx, sy, sz = (float(np.linalg.norm(c)) for c in (right, up, front))
            heading = np.array([right[0], right[1], 0.0])
            if np.linalg.norm(heading) < 1e-3 * max(sx, 1e-9):
                heading = np.array([up[1], -up[0], 0.0])
            heading /= max(float(np.linalg.norm(heading)), 1e-9)
            down = np.array([0.0, 0.0, 1.0])
            right = heading * sx
            up = np.cross(down, heading) * sy
            front = down * sz
        box = np.eye(4)
        box[:3, 0] = front * float(d["depth"] or 1.0)
        box[:3, 1] = right
        box[:3, 2] = up
        box[:3, 3] = m[:3, :3] @ np.asarray(d["offset"] or [0.0, 0.0, 0.0]) + m[:3, 3]
        t = space.unreal_transform(box)
        distance = d["max view distance"]
        # A projected decal has its own instance dp_ (the same spec, the static switch Projected: the angle fade of the
        # source); the road ribbons keep d_.
        out.append({"entity": d["entity"], "material": "dp_" + key[2:], "location": t["location"], "rotation": t["rotation"],
                    "size": [abs(v) * 100.0 for v in t["scale"]], "sort priority": int(d["sort priority"] or 0),
                    "fade m": float(distance) * float(d["view distance multiplier"] or 1.0) if distance else None,
                    "opacity": d["opacity"], "color": d["color"], "projection": d["projection"],
                    "deferred": d["deferred"]})
    return out


def export(names: list[str]) -> dict:
    """Writes for each region SWC_CONTENT/decals/NAME/: items.json (the source records), the road meshes
    (roads_CX_CY_P.gltf), and layer.json (the road meshes with their Unreal folders and origins, the decals in Unreal
    space). Writes SWC_CONTENT/decals/materials.json (each decal material key with its source and render rules).
    @return The counts."""
    started = time.time()
    heights = Heights()
    path = env.content_dir() / "decals" / "materials.json"
    materials = json.loads(path.read_text()) if path.is_file() else {}
    report = {}
    for name in names:
        items = region_items(name)
        (out_dir(name) / "items.json").write_text(json.dumps(items))
        roads = write_roads(name, items["roads"], heights, materials)
        decals = decal_transforms(items["decals"], materials)
        layer = {"version": VERSION, "region": name, "roads": roads, "decals": decals}
        (out_dir(name) / "layer.json").write_text(json.dumps(layer))
        report[name] = {"roads": len(items["roads"]), "road meshes": len(roads), "decals": len(items["decals"]),
                        "decals placed": len(decals), **items["counts"]}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(materials, indent=1, sort_keys=True))
    return {"regions": report, "materials": len(materials), "seconds": round(time.time() - started, 1)}


def specs() -> dict:
    """Writes SWC_CONTENT/decals/instances.json (the spec of each decal material key: the parent M_SwcDecal of
    Scripts/swc_materials.py, "create": the instance has no manifest record) and textures.json: the d_ materials of the
    roads and projected decals (materials.json), and the dm_ materials of the mesh decal companions of mesh rule 7
    (mesh-materials.json: the sub-material index; the vertex alpha with %VERTCOLORS). @return The counts."""
    from . import material_records as mr
    folder = env.content_dir() / "decals"
    materials = json.loads((folder / "materials.json").read_text()) if (folder / "materials.json").is_file() else {}
    mesh_file = folder / "mesh-materials.json"
    mesh_materials = json.loads(mesh_file.read_text())["materials"] if mesh_file.is_file() else {}
    textures: dict = {}
    out = {}

    def use(record: dict) -> str:
        textures[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
        return record["asset"]

    rows = [(key, m, 0, m["rules"], False) for key, m in materials.items()]
    for key, m in mesh_materials.items():
        record = mr.source_record(m["mtl"], m["index"], mr.sub_materials(m["mtl"])[m["index"]])
        rows.append((key, m, m["index"], mr.render_rules(record), True))
    projected = set()
    for path in folder.glob("r_*/layer.json"):
        projected |= {d["material"] for d in json.loads(path.read_text()).get("decals", [])}
    for key, m, index, rules, on_mesh in sorted(rows, key=lambda row: row[0]):
        maps = rules["textures"]
        record = mr.source_record(m["mtl"], index, mr.sub_materials(m["mtl"])[index])
        diffuse = record["lighting"].get("Diffuse") or [1.0, 1.0, 1.0, 1.0]
        alpha = float(diffuse[3]) if isinstance(diffuse, list) and len(diffuse) > 3 else 1.0
        spec = {"parent": "decal", "create": True, "textures": {}, "vectors": {}, "scalars": {}, "source": m["mtl"],
                "uses": m["uses"], "flags": rules.get("flags", [])}
        if on_mesh:
            spec["source"] = f"{m['mtl']}#{index}"
            if "%VERTCOLORS" in rules.get("flags", []):   # the source decal alpha x the vertex alpha
                spec["switches"] = {"UseVertexAlpha": True}
        if "diffuse" in maps:
            spec["textures"]["BaseColor"] = use(mr.color_texture(maps["diffuse"]["product"], "base"))
        if "normal" in maps:
            spec["textures"]["Normal"] = use(mr.color_texture(maps["normal"]["product"], "normal"))
        if "gloss" in maps:
            product = maps["gloss"]["product"]
            spec["textures"]["Gloss"] = use(mr.linear_texture(product, alpha=source.has(product + ".a")))
        if "decal opacity" in maps:
            spec["textures"]["DecalOpacity"] = use(mr.linear_texture(maps["decal opacity"]["product"]))
        if "specular" in maps:   # the reflectance of the decal (version 5; M_SwcDecal version 7), as on a surface
            spec["textures"]["Specular"] = use(mr.color_texture(maps["specular"]["product"], "base"))
        params = rules["params"]
        spec["vectors"]["DiffuseColor"] = list(rules["diffuse"]) + [alpha]
        spec["vectors"]["SpecularColor"] = list(rules["specular"]) + [1.0]
        spec["vectors"]["UVTransform"] = mr.uv_transform(maps.get("diffuse", {}).get("uv"))
        spec["scalars"]["Smoothness"] = max(1 / 127, float(rules["shininess"]) / 255)
        spec["scalars"]["DecalAlphaMult"] = float(params.get("DecalAlphaMult", 1.0))
        spec["scalars"]["DecalFalloff"] = float(params.get("DecalFalloff", 1.0))
        out[key] = spec
        if "dp_" + key[2:] in projected:   # the projected decals of the layer: the same spec, the angle fade on
            out["dp_" + key[2:]] = {**spec, "switches": {**spec.get("switches", {}), "Projected": True}}
    from . import manifest
    for key, record in textures.items():
        record["unreal"] = manifest.unreal_folder("texture", key)
    (folder / "instances.json").write_text(json.dumps({"version": VERSION, "instances": out}, indent=1, sort_keys=True))
    (folder / "textures.json").write_text(json.dumps({"version": VERSION, "textures": textures}, indent=1, sort_keys=True))
    return {"instances": len(out), "mesh decals": len(mesh_materials), "textures": len(textures),
            "without %DECAL": sum(1 for s in out.values() if "%DECAL" not in s["flags"])}


def main(argv: list[str]) -> dict:
    """decals scan NAME... | decals export NAME... | decals specs"""
    if argv and argv[0] == "scan":
        return scan(argv[1:])
    if argv and argv[0] == "export":
        result = export(argv[1:])
        result["specs"] = specs()
        return result
    if argv and argv[0] == "specs":
        return specs()
    raise SystemExit("usage: decals scan NAME... | export NAME... | specs")
