"""The map of space and scale: engine space of the game data -> glTF of the converter -> Unreal.

engine   meters, X east, Y north, Z up, right-handed (the game data, the placements, the backend)
glTF     meters, (x, z, -y): Y up, right-handed (gltf.z_up_to_y_up, a rotation: no mirror)
Unreal   centimeters, (x, -y, z) x 100: X east, Y south, Z up, left-handed

The glTF -> Unreal step is the Interchange glTF importer of the engine: (X, Y, Z) -> (X, Z, Y) (GLTFCore
ConversionUtilities.h, ConvertVec3) and x 100 (InterchangeGltfPrivate.h, GltfUnitConversionMultiplier). Thus a point
of the engine space maps to Unreal by C = diag(100, -100, 100). C has a negative determinant: the coordinates of a
right-handed space become the coordinates of a left-handed space, and the world itself is not mirrored.

A placement (a 4 x 4 matrix M, column vectors, engine space) maps to the Unreal matrix C M C^-1:
- the linear part becomes S L S with S = diag(1, -1, 1) (the 100 cancels);
- the translation becomes 100 S t;
- a rotation quaternion (x, y, z, w) becomes (-x, y, -z, w); a yaw of a degrees about Z becomes -a degrees;
- a scale stays the same; a placement with a negative determinant stays a mirror (a negative X scale in Unreal).
Stage 1.1 proves the map in Unreal (space proof: tools/convert.sh space-proof, then Scripts/space_proof.py).
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

from . import env, gltf, manifest

C = np.diag([100.0, -100.0, 100.0])   # engine meters -> Unreal centimeters
S = np.diag([1.0, -1.0, 1.0])


def unreal_point(p) -> np.ndarray:
    """@return The Unreal position (cm) of an engine position (m)."""
    return C @ np.asarray(p, dtype=np.float64)


def unreal_matrix(m) -> np.ndarray:
    """@return The Unreal 4 x 4 matrix (column vectors, cm) of an engine placement matrix (column vectors, m)."""
    m = np.asarray(m, dtype=np.float64)
    out = np.eye(4)
    out[:3, :3] = S @ m[:3, :3] @ S
    out[:3, 3] = C @ m[:3, 3]
    return out


def quaternion(r: np.ndarray) -> tuple[float, float, float, float]:
    """@return The unit quaternion (x, y, z, w) of a rotation matrix (column vectors). Unreal FQuat uses the same
    algebra: the quaternion of the Unreal matrix is an Unreal FQuat."""
    t = np.trace(r)
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = ((r[2, 1] - r[1, 2]) / s, (r[0, 2] - r[2, 0]) / s, (r[1, 0] - r[0, 1]) / s, 0.25 * s)
    elif r[0, 0] > r[1, 1] and r[0, 0] > r[2, 2]:
        s = math.sqrt(1.0 + r[0, 0] - r[1, 1] - r[2, 2]) * 2
        q = (0.25 * s, (r[0, 1] + r[1, 0]) / s, (r[0, 2] + r[2, 0]) / s, (r[2, 1] - r[1, 2]) / s)
    elif r[1, 1] > r[2, 2]:
        s = math.sqrt(1.0 + r[1, 1] - r[0, 0] - r[2, 2]) * 2
        q = ((r[0, 1] + r[1, 0]) / s, 0.25 * s, (r[1, 2] + r[2, 1]) / s, (r[0, 2] - r[2, 0]) / s)
    else:
        s = math.sqrt(1.0 + r[2, 2] - r[0, 0] - r[1, 1]) * 2
        q = ((r[0, 2] + r[2, 0]) / s, (r[1, 2] + r[2, 1]) / s, 0.25 * s, (r[1, 0] - r[0, 1]) / s)
    n = math.sqrt(sum(v * v for v in q))
    q = tuple(v / n for v in q)
    return q if q[3] >= 0 else tuple(-v for v in q)


def unreal_transform(m) -> dict:
    """@return The Unreal transform (an FTransform: location cm, rotation quaternion x y z w, scale) of an engine
    placement matrix, and the shear that a transform cannot keep (0 for rotation and scale only)."""
    u = unreal_matrix(m)
    linear = u[:3, :3]
    scale = np.linalg.norm(linear, axis=0)
    if np.linalg.det(linear) < 0:
        scale[0] = -scale[0]
    unscaled = linear / np.where(scale == 0, 1.0, scale)
    shear = float(np.abs(unscaled.T @ unscaled - np.eye(3)).max())
    left, _values, right = np.linalg.svd(unscaled)   # the nearest rotation (a shear has no place in a transform)
    rotation = left @ right
    return {"location": [float(v) for v in u[:3, 3]], "rotation": [float(v) for v in quaternion(rotation)],
            "scale": [float(v) for v in scale], "shear": round(shear, 6)}


# The proof: an asymmetric marker (three arms of different length and color) and real placements of one area.

MARKER = "test/swc_space_marker"
ARMS = (((0.0, -0.1, -0.1), (3.0, 0.1, 0.1), (0.9, 0.1, 0.1)),    # +X: 3 m, red
        ((-0.1, 0.0, -0.1), (0.1, 2.0, 0.1), (0.1, 0.8, 0.1)),    # +Y: 2 m, green
        ((-0.1, -0.1, 0.0), (0.1, 0.1, 1.0), (0.1, 0.2, 0.9)),    # +Z: 1 m, blue
        ((-0.2, -0.2, -0.2), (0.2, 0.2, 0.2), (0.9, 0.9, 0.9)))   # the origin: a white cube
TIPS = {"x tip": (3.0, 0.0, 0.0), "y tip": (0.0, 2.0, 0.0), "z tip": (0.0, 0.0, 1.0)}


def box(lo, hi) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """@return (positions, normals, indices) of a box with flat faces, counter-clockwise outside (engine space)."""
    lo, hi = np.asarray(lo, dtype=np.float64), np.asarray(hi, dtype=np.float64)
    p, n, idx = [], [], []
    for axis in range(3):
        for sign in (-1.0, 1.0):
            u, v = (axis + 1) % 3, (axis + 2) % 3
            normal = np.zeros(3); normal[axis] = sign
            corners = []
            for a, b in ((0, 0), (1, 0), (1, 1), (0, 1)):
                c = np.zeros(3)
                c[axis] = hi[axis] if sign > 0 else lo[axis]
                c[u] = (lo[u], hi[u])[a]
                c[v] = (lo[v], hi[v])[b]
                corners.append(c)
            base = len(p)
            p += corners; n += [normal] * 4
            quad = [(0, 1, 2), (0, 2, 3)] if sign > 0 else [(0, 2, 1), (0, 3, 2)]
            idx += [base + i for t in quad for i in t]
    return np.array(p), np.array(n), np.array(idx, dtype=np.uint32)


def marker_parts() -> list[tuple[np.ndarray, np.ndarray, np.ndarray, tuple]]:
    return [(*box(lo, hi), color) for lo, hi, color in ARMS]


def write_marker() -> Path:
    """Writes the marker as glTF through the same map as the game meshes (gltf.z_up_to_y_up)."""
    out = env.content_dir() / (MARKER + ".gltf")
    out.parent.mkdir(parents=True, exist_ok=True)
    w = gltf.Writer()
    prims = []
    for number, (p, n, idx, color) in enumerate(marker_parts()):
        mat = w.material(f"marker_{'xyzo'[number]}", factor=(*color, 1.0))
        prims.append(w.primitive(gltf.z_up_to_y_up(p), idx, gltf.z_up_to_y_up(n), material=mat))
    w.doc["scenes"][0]["nodes"] = [w.node(name="swc_space_marker", mesh=w.mesh("swc_space_marker", prims))]
    w.save(out)
    manifest.add("test", MARKER, out)
    return out


def rotation(yaw: float = 0.0, pitch: float = 0.0, roll: float = 0.0) -> np.ndarray:
    """@return Rz(yaw) Ry(roll) Rx(pitch) in engine space (degrees, counter-clockwise seen from the positive axis)."""
    a, b, c = (math.radians(v) for v in (yaw, pitch, roll))
    rz = np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])
    rx = np.array([[1, 0, 0], [0, math.cos(b), -math.sin(b)], [0, math.sin(b), math.cos(b)]])
    ry = np.array([[math.cos(c), 0, math.sin(c)], [0, 1, 0], [-math.sin(c), 0, math.cos(c)]])
    return rz @ ry @ rx


def placement(position, r: np.ndarray, scale=(1.0, 1.0, 1.0)) -> np.ndarray:
    m = np.eye(4)
    m[:3, :3] = r @ np.diag(scale)
    m[:3, 3] = position
    return m


def marker_placements(origin) -> list[tuple[str, np.ndarray]]:
    """Four known placements in a row along engine +X, 7 m apart."""
    x, y, z = origin
    return [("identity", placement((x, y, z), np.eye(3))),
            ("yaw 30", placement((x + 7, y, z), rotation(30))),
            ("yaw 30 pitch 20 roll 10 scale 1.5", placement((x + 14, y, z), rotation(30, 20, 10), (1.5, 1.5, 1.5))),
            ("mirror x", placement((x + 21, y, z), np.eye(3), (-1.0, 1.0, 1.0)))]


def camera(eye, target, fov_v: float, size) -> dict:
    """@return An Unreal camera of a camera of the software renderer (vertical field of view -> horizontal)."""
    w, h = size
    fov_h = math.degrees(2 * math.atan(math.tan(math.radians(fov_v) / 2) * w / h))
    return {"eye engine": [float(v) for v in eye], "target engine": [float(v) for v in target],
            "location": [float(v) for v in unreal_point(eye)], "target": [float(v) for v in unreal_point(target)],
            "fov horizontal": fov_h, "fov vertical": fov_v, "size": [w, h]}


def clear_spot(placements: list[dict], area, keep: float = 14.0) -> tuple[float, float]:
    """@return A point of the area with no placement origin within `keep` meters, near the center."""
    x0, y0, size = area
    pts = np.array([p["matrix"][0][3] for p in placements]), np.array([p["matrix"][1][3] for p in placements])
    best, best_score = (x0 + size / 2, y0 + size / 2), -1e9
    for gx in np.arange(x0 + 40, x0 + size - 60, 4.0):
        for gy in np.arange(y0 + 40, y0 + size - 40, 4.0):
            near = np.hypot(pts[0] - (gx + 10.5), pts[1] - gy).min()   # the middle of the row of markers
            score = min(near, keep * 2) - 0.02 * math.hypot(gx - x0 - size / 2, gy - y0 - size / 2)
            if score > best_score:
                best, best_score = (float(gx), float(gy)), score
    return best


def check_placements(placements: list[dict], wanted: int = 24) -> list[int]:
    """@return Indexes of placements for the read-back check: each source, rotated ones first, one per mesh."""
    from . import mesh
    chosen, seen = [], set()
    for kind in ("chunk", "capital", "capital variant", "distribution"):
        rows = [i for i, p in enumerate(placements) if p["source"] == kind and mesh.out_path(p["mesh"]).is_file()
                and not p["mesh"].rsplit("/", 1)[-1].startswith("fx_")]
        rows.sort(key=lambda i: -abs(math.atan2(placements[i]["matrix"][1][0], placements[i]["matrix"][0][0])))
        for i in rows:
            if placements[i]["mesh"] not in seen and len([c for c in chosen if placements[c]["source"] == kind]) < wanted // 4:
                chosen.append(i)
                seen.add(placements[i]["mesh"])
    sheared = max(range(len(placements)), key=lambda i: unreal_transform(placements[i]["matrix"])["shear"])
    return chosen + ([sheared] if sheared not in chosen and mesh.out_path(placements[sheared]["mesh"]).is_file() else [])


def proof(region_name: str, area, step: int = 2) -> dict:
    """Writes the space proof: the marker glTF, a terrain patch, the plan for Scripts/space_proof.py (actors, checks,
    cameras) in content/test/space-proof.json, and two software pictures of the same views."""
    from . import mesh, preview, region
    reg = region.Region(region_name)
    pfile = region.placement_file(region_name, area)
    if not pfile.is_file():
        reg.write_placements(area)
    placements = json.loads(pfile.read_text())["placements"]
    write_marker()
    patch = reg.write_patch(area, step)
    manifest.add("test", f"test/{region_name}_patch_{int(area[0])}_{int(area[1])}_{int(area[2])}", patch)
    patch_asset = manifest.unreal_folder("test", f"test/{region_name}_patch_{int(area[0])}_{int(area[1])}_{int(area[2])}")
    mx, my = clear_spot(placements, area)
    ox, oy = reg.origin
    mz = reg.height_at(mx - ox, my - oy) + 1.5
    actors = [{"label": "terrain patch", "unreal": patch_asset,
               "engine": placement((area[0], area[1], 0.0), np.eye(3)).tolist()}]
    checks = []
    for label, m in marker_placements((mx, my, mz)):
        actors.append({"label": "marker " + label, "unreal": manifest.unreal_folder("test", MARKER), "engine": m.tolist()})
        points = np.concatenate([p for p, _n, _i, _c in marker_parts()])
        world = points @ m[:3, :3].T + m[:3, 3]
        checks.append({"label": "marker " + label, "kind": "vertices",
                       "expected": [[round(float(v), 4) for v in unreal_point(w)] for w in world],
                       "tips": {k: [float(v) for v in unreal_point(m[:3, :3] @ np.array(t) + m[:3, 3])] for k, t in TIPS.items()}})
    for i, p in enumerate(placements):
        if p["mesh"].rsplit("/", 1)[-1].startswith("fx_") or not mesh.out_path(p["mesh"]).is_file():
            continue
        actors.append({"label": f"{p['source']} {i}", "unreal": manifest.unreal_folder("static", p["mesh"]), "engine": p["matrix"]})
    for i in check_placements(placements):
        p = placements[i]
        m = np.array(p["matrix"])
        v = mesh.drawn_positions(p["mesh"]).astype(np.float64) @ m[:3, :3].T + m[:3, 3]
        u = v @ C.T
        shear = unreal_transform(m)["shear"]
        checks.append({"label": f"{p['source']} {i}", "kind": "bounds", "mesh": p["mesh"], "shear": shear,
                       "tolerance cm": round(1.0 + shear * float(np.ptp(u, axis=0).max()), 3),
                       "expected min": [float(x) for x in u.min(0)], "expected max": [float(x) for x in u.max(0)]})
    for a in actors:
        a["transform"] = unreal_transform(a["engine"])
    eye, target = preview.region_camera(region_name, area)
    close_eye, close_target = (mx - 9.0, my - 16.0, mz + 7.0), (mx + 10.5, my + 1.0, mz)
    views = {"overview": (eye, target, 50.0, (1280, 800)), "markers": (close_eye, close_target, 50.0, (1280, 800))}
    folder = env.content_dir() / "test"
    extra = []
    for _label, m in marker_placements((mx, my, mz)):
        for p, n, idx, color in marker_parts():
            if np.linalg.det(m[:3, :3]) < 0:
                idx = idx.reshape(-1, 3)[:, ::-1].reshape(-1)
            extra.append((p @ m[:3, :3].T + m[:3, 3], n @ np.linalg.inv(m[:3, :3]), idx, color))
    pictures = {name: str(preview.region_preview(region_name, area, extra, view, folder / f"space-proof-{name}-software.png"))
                for name, view in views.items()}
    doc = {"region": region_name, "area": list(area), "map": "Unreal = diag(100, -100, 100) x engine",
           "marker origin engine": [mx, my, mz], "actors": actors, "checks": checks,
           "cameras": {name: camera(*view) for name, view in views.items()}, "software pictures": pictures,
           "light engine": [0.3, -0.4, 0.85]}
    (folder / "space-proof.json").write_text(json.dumps(doc))
    return {"plan": str(folder / "space-proof.json"), "actors": len(actors), "checks": len(checks),
            "marker origin": [mx, my, mz], "pictures": pictures,
            "max shear": max(a["transform"]["shear"] for a in actors)}


def sheet(evidence: Path | None = None) -> dict:
    """Puts the software pictures (engine space) and the Unreal pictures of the same cameras side by side, with the
    result of the read-back. @return The paths."""
    from PIL import Image, ImageDraw, ImageFont
    evidence = evidence or env.work_dir() / "evidence" / "1.1"
    report = json.loads((evidence / "space-proof-unreal.json").read_text())
    plan = json.loads((env.content_dir() / "test" / "space-proof.json").read_text())
    w, h, top = 960, 600, 34
    font = ImageFont.load_default(size=22)
    rows = list(plan["cameras"])
    img = Image.new("RGB", (2 * w, len(rows) * (h + top) + 2 * top), (255, 255, 255))
    draw = ImageDraw.Draw(img)
    for r, name in enumerate(rows):
        y = r * (h + top)
        for c, (path, label) in enumerate(((plan["software pictures"][name], "software render, engine space"),
                                           (report["pictures"][f"{name} unreal"], "Unreal 5.8, the same camera through the map"))):
            draw.text((c * w + 10, y + 6), f"{name}: {label}", fill=(0, 0, 0), font=font)
            img.paste(Image.open(path).convert("RGB").resize((w, h)), (c * w, y + top))
    markers = [x for x in report["checks"] if x["label"].startswith("marker")]
    bounds = [x for x in report["checks"] if not x["label"].startswith("marker")]
    vertex_error = max(max(x["max distance expected -> unreal cm"], x["max distance unreal -> expected cm"]) for x in markers)
    bound_error = max(x["bounds error cm"] for x in bounds if x.get("shear", 0) < 1e-3)
    text = (f"read back in Unreal: {len(markers)} markers, largest vertex error {vertex_error:.3f} cm; "
            f"{len(bounds)} game placements, largest bounds error {bound_error:.3f} cm (no shear); "
            f"all checks ok: {report['all ok']}")
    draw.text((10, len(rows) * (h + top) + 8), text, fill=(0, 0, 0), font=font)
    out = evidence / "space-proof-sheet.png"
    img.save(out)
    return {"sheet": str(out), "summary": text}
