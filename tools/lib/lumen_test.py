"""The list of the Lumen mesh data test (stage E2, docs/ENVIRONMENT.md, "The Lumen mesh data test"): the meshes of the inn of
Oxboro and of two houses at the north side of the spawn lane, from the placements of the converter (regions/r_+04_+02) and the
asset folders of the manifest. tools/lumen-test.sh copies them with mesh distance fields and Lumen cards.

Usage: lumen_test.py OUT.json
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


def main() -> int:
    out = Path(sys.argv[1])
    placements = np.load(CONTENT / "regions/r_+04_+02/placements.npz", allow_pickle=True)
    manifest = json.loads((CONTENT / "manifest.json").read_text())["assets"]
    places = placements["matrix"][:, :2, 3]
    items = {}
    for building, (center, radius) in BUILDINGS.items():
        near = np.where(np.linalg.norm(places - np.array(center), axis=1) <= radius)[0]
        for index in near:
            mesh = str(placements["meshes"][placements["mesh"][index]])
            entry = manifest.get(mesh)
            if mesh.startswith(SKIP) or entry is None or entry.get("kind") != "static":
                continue
            item = items.setdefault(mesh, {"mesh": mesh, "folder": entry["unreal"], "file": str(CONTENT / entry["file"]),
                                           "nanite": bool(entry.get("nanite")), "buildings": [], "placements": 0})
            item["placements"] += 1
            if building not in item["buildings"]:
                item["buildings"].append(building)
    result = sorted(items.values(), key=lambda item: item["mesh"])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=1))
    print(json.dumps({"meshes": len(result), "by building": {b: sum(b in i["buildings"] for i in result) for b in BUILDINGS}}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
