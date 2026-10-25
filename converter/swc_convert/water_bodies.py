"""The water bodies of the Unreal Water plugin (docs/WATER.md "The Unreal water"; decision of the user, 2026-10-25): the specs of the
river, lake, and custom water bodies that the client spawns from the source water shapes (-SwcWaterMode=Unreal).

The content stays from the source; only the look changes. The specs are made from the same environment records and in the same
order as the surfaces of water.py (the duplicates removed, the shapes that make no surface skipped): body k of a region spec is
body k of REGION.water, and the client builds a custom body from that mesh.

- A river: one spline point for each vertex V[i] of the source spline, in the world (engine m). Segment i of the source is the cubic
  Bezier V[i], Forward[i], Back[i + 1], V[i + 1]. A cubic Hermite segment with the tangents 3 (Forward[i] - V[i]) (the leave tangent
  of point i) and 3 (V[i + 1] - Back[i + 1]) (the arrive tangent of point i + 1) is the same curve, and the Unreal spline is a cubic
  Hermite curve by the key (the key i + t is the parameter t of segment i). A matrix is affine: the curve of the world control
  points is the world curve. A linear spline has the chords as tangents (a Hermite segment with both tangents P1 - P0 is the
  straight line, linear in t). The width: DefaultWidth + WidthAttribute[i] of the source, linear by t between two vertices (the
  water spline metadata of the plugin is linear by the key: FInterpCurvePoint(InVal, OutVal) has CIM_Linear); the world width
  is the width across the local tangent through the matrix (as the cross-sections of water.river_surface). The depth: the
  surface minus the floor of the water quadtree at the vertex (None where the quadtree has no water).
- A water volume with a flat top (all points of its surface within FLAT_TOLERANCE m) and a simple outline: a lake, the outline of
  its top (the polygon of a prism, the four corners of a box, the rim of a cylinder) at its surface z. Else (a tilted volume, a
  self-crossing outline): a custom body (the mesh of REGION.water).

Output (never in git): unreal-bodies.json in the folder of --out (default SWC_CONTENT/water).
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

from . import census, env
from . import water as w

FORMAT = 1
# The largest height range of a lake top (m); a volume with more is a custom body (the tilted volumes, docs/WATER.md).
FLAT_TOLERANCE = 0.01
# The dense samples of one spline segment in the check.
CHECK_SAMPLES = 512


def hermite(p0: np.ndarray, t0: np.ndarray, p1: np.ndarray, t1: np.ndarray, a: np.ndarray) -> np.ndarray:
    """@return The points of the cubic Hermite segment at the parameters a (FMath::CubicInterp of Unreal)."""
    a = np.asarray(a, dtype=np.float64)[:, None]
    a2, a3 = a * a, a * a * a
    return (2 * a3 - 3 * a2 + 1) * p0 + (a3 - 2 * a2 + a) * t0 + (a3 - a2) * t1 + (-2 * a3 + 3 * a2) * p1


def river_tangents(points: np.ndarray, controls: list, linear: bool) -> tuple[np.ndarray, np.ndarray]:
    """@return (the arrive tangents, the leave tangents) of the spline points (the frame of the points and the controls)."""
    n = len(points)
    arrive = np.zeros((n, 3))
    leave = np.zeros((n, 3))
    for i in range(n - 1):
        if linear:
            leave[i] = points[i + 1] - points[i]
            arrive[i + 1] = points[i + 1] - points[i]
        else:
            leave[i] = 3 * (controls[i][0] - points[i])
            arrive[i + 1] = 3 * (points[i + 1] - controls[i + 1][1])
    arrive[0] = leave[0]
    leave[n - 1] = arrive[n - 1]
    return arrive, leave


def local_left(derivative: np.ndarray, before: np.ndarray | None) -> np.ndarray:
    """@return The horizontal unit normal (to the left) of a local tangent (the rule of water.river_surface: a zero tangent takes the
    direction before it)."""
    flat = derivative[:2]
    norm = float(np.linalg.norm(flat))
    if norm < 1e-9:
        return before if before is not None else np.array([0.0, 1.0, 0.0])
    flat = flat / norm
    return np.array([-flat[1], flat[0], 0.0])


class Trees:
    """The water quadtrees of the regions (water.read_tree), loaded on use."""

    def __init__(self):
        self.nodes: dict[str, list] = {}

    def depth(self, x: float, y: float, z: float) -> float | None:
        rx, ry = math.floor(x / w.REGION_SIZE), math.floor(y / w.REGION_SIZE)
        if rx < 0 or ry < 0:
            return None
        name = f"r_+{rx:02d}_+{ry:02d}"
        if name not in self.nodes:
            self.nodes[name] = w.read_tree(name)
        nodes = self.nodes[name]
        if not nodes:
            return None
        found = w.tree_surface(nodes, x - rx * w.REGION_SIZE, y - ry * w.REGION_SIZE)
        return None if found is None else round(max(float(found[0]) - float(found[1]), 0.0), 3)


def river_spec(record: dict, trees: Trees | None) -> dict | None:
    spline_type, points, controls, river, attributes = w.river_parts(record)
    if len(points) < 2:
        return None
    linear = spline_type == w.LINEAR or len(controls) < len(points)
    matrix = w.matrix_of(record)
    base = river["BaseClass1"]
    default = float(base["Width"]["DefaultWidth"])
    widths = [default + (attributes[i] if i < len(attributes) else 0.0) for i in range(len(points))]
    arrive, leave = river_tangents(points, controls, linear)
    rotation = matrix[:3, :3]
    world = w.apply(matrix, points)
    world_widths, left = [], None
    for i in range(len(points)):
        # The local tangent at the vertex: the start of segment i, or the end of the last segment.
        _, derivative = w.bezier(points, controls, min(i, len(points) - 2), np.array([0.0 if i < len(points) - 1 else 1.0]), linear)
        left = local_left(derivative[0], left)
        world_widths.append(float(np.linalg.norm(rotation @ (left * widths[i]))))
    spec = {"type": "river", "linear": linear, "points": np.round(world, 4).tolist(),
            "arrive": np.round(arrive @ rotation.T, 4).tolist(), "leave": np.round(leave @ rotation.T, 4).tolist(),
            "widths": [round(v, 4) for v in world_widths], "sourceWidths": [round(v, 4) for v in widths],
            "segmentLength": float(base.get("SegmentLength", 10.0))}
    if trees is not None:
        spec["depths"] = [trees.depth(*p) for p in world]
    return spec


def segments_cross(a: np.ndarray, b: np.ndarray, c: np.ndarray, d: np.ndarray) -> bool:
    def side(p, q, r) -> float:
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    d1, d2, d3, d4 = side(c, d, a), side(c, d, b), side(a, b, c), side(a, b, d)
    return ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)) and min(abs(d1), abs(d2), abs(d3), abs(d4)) > 1e-12


def is_simple(outline: np.ndarray) -> bool:
    """@return True when no two edges of the closed outline (n, 2) that do not share a point cross."""
    n = len(outline)
    if n < 3:
        return False
    for i in range(n):
        a, b = outline[i], outline[(i + 1) % n]
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue   # the edges that share point 0
            if segments_cross(a, b, outline[j], outline[(j + 1) % n]):
                return False
    return True


def volume_spec(world: np.ndarray, shape: int) -> dict:
    heights = world[:, 2]
    outline = world[1:, :2] if shape == w.SHAPE_CYLINDER else world[:, :2]
    if float(heights.max() - heights.min()) > FLAT_TOLERANCE:
        return {"type": "custom", "reason": "tilted", "tilt": round(float(heights.max() - heights.min()), 4)}
    if not is_simple(outline):
        return {"type": "custom", "reason": "outline crosses itself"}
    return {"type": "lake", "z": round(float(heights.mean()), 4), "outline": np.round(outline, 4).tolist()}


def specs(names: list[str], with_depths: bool = True) -> dict:
    """@return The specs of the regions: the bodies of each region in the order of REGION.water (the loop of water.write)."""
    content = env.content_dir()
    trees = Trees() if with_depths else None
    seen: set[str] = set()
    regions, counts = {}, {"river": 0, "lake": 0, "custom": 0}
    for name in names:
        path = content / "environment" / "regions" / f"{name}.json"
        records = json.loads(path.read_text())["records"] if path.is_file() else []
        bodies = []
        for record in records:
            if record["kind"] not in ("waterVolume", "river"):
                continue
            key = w.duplicate_key(record)
            if key in seen:
                continue
            seen.add(key)
            if record["kind"] == "waterVolume":
                surface = w.volume_surface(record)
                if surface is None:
                    continue
                spec = volume_spec(surface[0], surface[2])
            else:
                if w.river_surface(record) is None:
                    continue
                spec = river_spec(record, trees)
            spec = {"index": len(bodies), "name": record["entity"], **spec}
            counts[spec["type"]] += 1
            bodies.append(spec)
        if bodies:
            regions[name] = bodies
    return {"regions": regions, "counts": counts}


def zone(names: list[str]) -> dict:
    """@return The extent of the world regions (engine m): the water zone of the client."""
    cells = [tuple(int(v) for v in n[2:].split("_")) for n in names]
    low = [min(c[0] for c in cells) * w.REGION_SIZE, min(c[1] for c in cells) * w.REGION_SIZE]
    high = [(max(c[0] for c in cells) + 1) * w.REGION_SIZE, (max(c[1] for c in cells) + 1) * w.REGION_SIZE]
    return {"min": low, "max": high}


def write(names: list[str], out: Path | None = None) -> dict:
    content = env.content_dir()
    out = Path(out) if out else content / "water"
    out.mkdir(parents=True, exist_ok=True)
    index = json.loads((content / "water" / "water.json").read_text())
    made = specs(names)
    doc = {"format": FORMAT, "space": "engine m, Z up", "zone": zone(names), "ocean": {"level": index["ocean"]["level"]},
           "counts": made["counts"], "regions": made["regions"]}
    (out / "unreal-bodies.json").write_text(json.dumps(doc, indent=0))
    return {"file": str(out / "unreal-bodies.json"), **made["counts"], "regions": len(made["regions"]), "zone": doc["zone"]}


def polyline_distance(points: np.ndarray, line: np.ndarray, chunk: int = 512) -> tuple[np.ndarray, np.ndarray]:
    """@return (the distance of each point to the polyline, the position of the nearest point as a fractional index of the line)."""
    a, b = line[:-1], line[1:]
    ab = b - a
    length2 = np.maximum((ab * ab).sum(axis=1), 1e-18)
    best = np.full(len(points), np.inf)
    where = np.zeros(len(points))
    for start in range(0, len(points), chunk):
        p = points[start:start + chunk]
        t = np.clip(((p[:, None, :] - a[None]) * ab[None]).sum(axis=2) / length2[None], 0.0, 1.0)
        d = np.linalg.norm(a[None] + t[..., None] * ab[None] - p[:, None, :], axis=2)
        k = d.argmin(axis=1)
        rows = np.arange(len(p))
        best[start:start + chunk] = d[rows, k]
        where[start:start + chunk] = k + t[rows, k]
    return best, where


def check_river(spec: dict, body: dict) -> dict:
    """The spline of a spec against the surface of the converter: the distance of the centers of the cross-sections to the
    Hermite curve, and their width against the linear width at the key of the nearest curve point."""
    points = np.array(spec["points"])
    arrive, leave = np.array(spec["arrive"]), np.array(spec["leave"])
    a = np.linspace(0.0, 1.0, CHECK_SAMPLES + 1)
    dense = [hermite(points[i], leave[i], points[i + 1], arrive[i + 1], a if i == len(points) - 2 else a[:-1]) for i in range(len(points) - 1)]
    line = np.vstack(dense)
    mesh = np.asarray(body["points"], dtype=np.float64)
    centers = (mesh[0::2] + mesh[1::2]) / 2
    widths = np.linalg.norm(mesh[0::2] - mesh[1::2], axis=1)
    distance, where = polyline_distance(centers, line)
    key = np.minimum(where / CHECK_SAMPLES, len(points) - 1 - 1e-9)
    i = np.floor(key).astype(int)
    expected = np.array(spec["widths"])[i] + (np.array(spec["widths"])[i + 1] - np.array(spec["widths"])[i]) * (key - i)
    return {"center": float(distance.max()), "width": float(np.abs(widths - expected).max())}


def check(names: list[str], out: Path | None = None) -> dict:
    """The specs (unreal-bodies.json of --out) against the surfaces of REGION.water: the names in order, the rivers (check_river),
    the lake outlines (each outline point is a vertex of the surface at the lake z)."""
    content = env.content_dir()
    path = (Path(out) if out else content / "water") / "unreal-bodies.json"
    doc = json.loads(path.read_text())
    faults, rivers, lakes = [], [], []
    for name, bodies in doc["regions"].items():
        mesh = w.read_mesh(content / "water" / f"{name}.water")
        if len(mesh) != len(bodies):
            faults.append(f"{name}: {len(bodies)} specs, {len(mesh)} surfaces")
            continue
        for spec, body in zip(bodies, mesh):
            if spec["name"] != body["name"]:
                faults.append(f"{name} {spec['index']}: the spec {spec['name']}, the surface {body['name']}")
                continue
            if spec["type"] == "river":
                if body["kind"] != w.KIND_RIVER:
                    faults.append(f"{name} {spec['index']}: a river spec on a volume")
                    continue
                result = check_river(spec, body)
                rivers.append((result["center"], result["width"], f"{name} {spec['name']}"))
            elif spec["type"] == "lake":
                vertices = np.asarray(body["points"], dtype=np.float64)
                outline = np.array(spec["outline"])
                gap = max(float(np.min(np.linalg.norm(vertices[:, :2] - p, axis=1))) for p in outline)
                height = float(np.abs(vertices[:, 2] - spec["z"]).max())
                lakes.append((gap, height, f"{name} {spec['name']}"))
    report = {"file": str(path), "counts": doc["counts"], "faults": faults[:20], "fault count": len(faults)}
    if rivers:
        report["rivers"] = {"count": len(rivers), "largest center distance m": round(max(r[0] for r in rivers), 5),
                            "largest width difference m": round(max(r[1] for r in rivers), 5),
                            "worst center": sorted(rivers, reverse=True)[:5],
                            "worst width": sorted(rivers, key=lambda r: -r[1])[:5]}
    if lakes:
        report["lakes"] = {"count": len(lakes), "largest outline gap m": round(max(r[0] for r in lakes), 5),
                           "largest height difference m": round(max(r[1] for r in lakes), 5)}
    return report


def main(argv=None) -> int:
    import argparse
    parser = argparse.ArgumentParser(prog="python3 -B -m swc_convert.water_bodies")
    parser.add_argument("command", choices=("write", "check"))
    parser.add_argument("names", nargs="*")
    parser.add_argument("--out", help="the folder of unreal-bodies.json (default SWC_CONTENT/water)")
    args = parser.parse_args(argv)
    names = args.names or census.world_regions()
    run = write if args.command == "write" else check
    print(json.dumps(run(names, args.out), indent=1, default=str))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
