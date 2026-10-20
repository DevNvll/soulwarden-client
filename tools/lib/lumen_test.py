"""The list of the Lumen mesh data test (stage E2, docs/ENVIRONMENT.md, "The Lumen mesh data test"): the meshes of the inn of
Oxboro and of two houses at the north side of the spawn lane, from the placements of the converter (regions/r_+04_+02) and the
asset folders of the manifest. tools/lumen-test.sh copies them with mesh distance fields and Lumen cards.

Usage: lumen_test.py OUT.json [--views VIEW.json... [--radius M]]
  --views   also the meshes of matched views (stage E5: the black of the shadows): the placements within RADIUS m (default 80) of the
            camera of each view JSON (a retail reference of tools/match-view.sh), in front of it or within 15 m, with the trees, the
            rocks, and the plants (the regions of 2,048 m about the camera)
"""
import json
import os
import sys
from pathlib import Path

import numpy as np

CONTENT = Path(os.environ.get("SWC_CONTENT", Path.home() / "Games/soulwarden-client/content"))
# The places (engine m) and the horizontal radius of each building (the inn of the brief; the two houses of the lane view night-n).
BUILDINGS = {"inn": ((8908.0, 4210.0), 14.0), "house 9x12": ((8877.3, 4267.0), 7.0), "house 9x15": ((8891.1, 4273.3), 8.0)}
# Not buildings: plants and fog shapes.
SKIP = ("objects/nature/", "objects/volumetrics/")
# The views: no fog shapes (the plants and the rocks stop the sky light too).
VIEW_SKIP = ("objects/volumetrics/",)
REGION_SIZE = 2048.0


def add(items: dict, manifest: dict, mesh: str, group: str, skip: tuple) -> None:
    entry = manifest.get(mesh)
    if mesh.startswith(skip) or entry is None or entry.get("kind") != "static":
        return
    item = items.setdefault(mesh, {"mesh": mesh, "folder": entry["unreal"], "file": str(CONTENT / entry["file"]),
                                   "nanite": bool(entry.get("nanite")), "buildings": [], "placements": 0})
    item["placements"] += 1
    if group not in item["buildings"]:
        item["buildings"].append(group)


def region_placements(name: str, cache: dict):
    if name not in cache:
        path = CONTENT / "regions" / name / "placements.npz"
        cache[name] = np.load(path, allow_pickle=True) if path.exists() else None
    return cache[name]


def add_views(items: dict, manifest: dict, views: list[str], radius: float) -> dict:
    """Adds the placements near the camera of each view (in front of it, or within 15 m)."""
    cache, counts = {}, {}
    for path in views:
        doc = json.loads(Path(path).read_text())
        camera = doc["camera"]
        eye = np.array([camera["x"], camera["y"], camera["z"]])
        heading = np.radians(camera.get("heading_deg_cw_from_north", 0.0))
        forward = np.array([np.sin(heading), np.cos(heading)])
        name = Path(path).stem
        counts[name] = 0
        regions = {f"r_{int(np.floor(x / REGION_SIZE)):+03d}_{int(np.floor(y / REGION_SIZE)):+03d}"
                   for x in (eye[0] - radius, eye[0] + radius) for y in (eye[1] - radius, eye[1] + radius)}
        for region in sorted(regions):
            placements = region_placements(region, cache)
            if placements is None:
                continue
            offset = placements["matrix"][:, :2, 3] - eye[:2]
            distance = np.linalg.norm(offset, axis=1)
            ahead = offset @ forward > -0.25 * distance
            for index in np.where((distance <= radius) & (ahead | (distance <= 15.0)))[0]:
                before = len(items)
                add(items, manifest, str(placements["meshes"][placements["mesh"][index]]), name, VIEW_SKIP)
                counts[name] += len(items) - before
    return counts


def main() -> int:
    out = Path(sys.argv[1])
    views, view_radius = [], 80.0
    args = sys.argv[2:]
    while args:
        word = args.pop(0)
        if word == "--radius":
            view_radius = float(args.pop(0))
        elif word == "--views":
            while args and not args[0].startswith("--"):
                views.append(args.pop(0))
        else:
            raise SystemExit(f"lumen_test.py: unknown argument {word}")
    placements = np.load(CONTENT / "regions/r_+04_+02/placements.npz", allow_pickle=True)
    manifest = json.loads((CONTENT / "manifest.json").read_text())["assets"]
    places = placements["matrix"][:, :2, 3]
    items = {}
    for building, (center, radius) in BUILDINGS.items():
        near = np.where(np.linalg.norm(places - np.array(center), axis=1) <= radius)[0]
        for index in near:
            add(items, manifest, str(placements["meshes"][placements["mesh"][index]]), building, SKIP)
    view_counts = add_views(items, manifest, views, view_radius) if views else {}
    result = sorted(items.values(), key=lambda item: item["mesh"])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=1))
    print(json.dumps({"meshes": len(result), "by building": {b: sum(b in i["buildings"] for i in result) for b in BUILDINGS},
                      "new meshes by view": view_counts}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
