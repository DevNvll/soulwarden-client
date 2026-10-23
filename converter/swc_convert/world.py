"""The world content (stage 1.2): the meshes of regions with shared textures and materials, converted in parallel.

The rule of the meshes has a version (mesh.VERSION): a mesh converts again when its manifest entry has an older
version or its glTF is missing (the game copy does not change). The import (Scripts/import_content.py) imports again
the assets whose hash changed. The census (census.py) gives the placements of each region.
"""
from __future__ import annotations

import time
from concurrent.futures import ProcessPoolExecutor

from . import census, manifest, mesh


def convert_one(path: str) -> dict:
    try:
        return mesh.convert(path, force=True, write_manifest=False)
    except Exception as error:
        return {"mesh": path, "error": str(error)[:160]}


def stale(paths: list[str], force: bool) -> list[str]:
    """@return The meshes that need a conversion (missing, an older rule, or forced). An "empty" mesh has no glTF of its own
    (only its decal companion, or nothing): its record of the current rule is up to date."""
    assets = manifest.load()["assets"]
    return [p for p in paths if force or assets.get(p, {}).get("version") != mesh.rule_for(p)
            or (assets.get(p, {}).get("kind") != "empty" and not mesh.out_path(p).is_file())]


def convert_meshes(paths: list[str], workers: int = 4, force: bool = False) -> dict:
    """Converts the meshes that need it and writes their manifest records one time. @return A report."""
    started = time.time()
    paths = sorted({p.lower() for p in paths})
    todo = stale(paths, force)
    if workers > 1 and len(todo) > 1:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            reports = list(pool.map(convert_one, todo, chunksize=8))
    else:
        reports = [convert_one(p) for p in todo]
    records = mesh.records_of(reports)
    # A mesh with no drawn triangles (only collision, decals, or light beams) is "empty": no asset, no placement.
    empty = [r["mesh"] for r in reports if "no drawn triangles" in r.get("error", "")]
    records.update({path: {"kind": "empty", "version": mesh.rule_for(path)} for path in empty})
    if records:
        manifest.add_many(records)
    failures = {r["mesh"]: r["error"] for r in reports if "error" in r and r["mesh"] not in empty}
    kinds: dict[str, int] = {}
    for entry in records.values():
        kinds[entry["kind"]] = kinds.get(entry["kind"], 0) + 1
    return {"meshes": len(paths), "up to date": len(paths) - len(todo), "converted": len(todo) - len(failures) - len(empty),
            "empty": len(empty), "failed": len(failures), "failures": dict(list(failures.items())[:10]), "records": kinds,
            "triangles": sum(r.get("triangles", 0) for r in reports), "seconds": round(time.time() - started, 1)}


def convert_regions(names: list[str], workers: int = 4, force: bool = False) -> dict:
    """Converts the meshes of the placements of regions (the census writes the placements of each region)."""
    paths: set[str] = set()
    for name in names:
        if not census.placements_file(name).is_file():
            census.run([name], workers=1)
        paths.update(str(m) for m in census.load_placements(name)["meshes"])
    return {"regions": names, **convert_meshes(sorted(paths), workers, force)}
