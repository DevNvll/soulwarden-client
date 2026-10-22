#!/usr/bin/env python3
"""The report of the meshes of inactive entities (IsRuntimeActive 0; converter/swc_convert/region.py SKIP_INACTIVE; AST-261): for
each region the meshes of the inactive entities that its placements read (the slice, the entity name, the mesh, the instances of
each slice read; one pass with the meshes kept), and for the whole world the same by slice. It reads the game copy only and writes one JSON file.
Usage: python3 -B tools/lib/inactive_report.py OUT.json [REGION...]   (default: all regions)"""
from __future__ import annotations

import collections
import json
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import region, source  # noqa: E402


def main() -> int:
    out = Path(sys.argv[1])
    names = sys.argv[2:] or sorted({p.split("/")[-2] for p in source.paks().entries
                                    if p.startswith(region.WORLD + "regions/") and p.endswith("/region.chunks")})
    # The slice of each logged entity: the last path that the converter read before it parsed the slice.
    last = {"path": ""}
    read = source.read

    def read_and_keep(path, *args, **kwargs):
        last["path"] = path
        return read(path, *args, **kwargs)
    source.read = read_and_keep
    report = {"regions": {}, "slices": {}}
    by_slice: dict = collections.defaultdict(lambda: collections.Counter())
    started = time.time()
    for name in names:
        # One pass with the meshes of the inactive entities kept and logged (the slice of each entry and the region).
        region.SKIP_INACTIVE = False
        region.INACTIVE_LOG = []
        original = region.slice_meshes
        logged_here = collections.Counter()

        def logged(data, _original=original, _here=logged_here):
            before = len(region.INACTIVE_LOG)
            result = _original(data)
            for entry in region.INACTIVE_LOG[before:]:
                by_slice[last["path"]][(entry[0], entry[1])] += entry[2]
                _here[(last["path"], entry[0], entry[1])] += entry[2]
            return result
        region.slice_meshes = logged
        try:
            placements, _ = region.Region(name).placements()
        finally:
            region.slice_meshes = original
        report["regions"][name] = {"placements": len(placements), "inactive entity meshes (slice reads)": sum(logged_here.values()),
                                   "entries": [{"slice": k[0], "entity": k[1], "mesh": k[2], "instances": v} for k, v in sorted(logged_here.items())]}
        print(f"{name}: {sum(logged_here.values())} inactive entity meshes in {len(placements)} placements ({round(time.time() - started)} s)",
              flush=True)
    region.SKIP_INACTIVE = True
    region.INACTIVE_LOG = None
    for path, entities in sorted(by_slice.items()):
        report["slices"][path] = [{"entity": e, "mesh": m, "instances in the slice": n} for (e, m), n in sorted(entities.items())]
    report["inactive entity meshes"] = sum(r["inactive entity meshes (slice reads)"] for r in report["regions"].values())
    out.write_text(json.dumps(report, indent=1))
    print(f"inactive report: {report['inactive entity meshes']} meshes of inactive entities, {len(report['slices'])} slices; {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
