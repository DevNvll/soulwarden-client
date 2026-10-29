#!/usr/bin/env python3
"""The converted creature content against the creature data of the backend copy.

Usage: creature_check.py BACKEND_DIR CONTENT_DIR
Prints one line: "ok ..." or "warn N slices and M names not converted ...". A warning, not a failure: the client draws a creature
with no converted slice as a placeholder cylinder and shows the id of a kind with no name (C-8, 2026-10-29: the backend copy
2026-10-29-a5ceec7 had 47 slices and 282 names (ids) that the content of 2026-10-18 and 2026-10-22 did not have; the two enforcers of an
encounter were white cylinders named "creature 112d3e50"). The fix: tools/convert-creatures.sh census, markers, wanted
(docs/CREATURES.md), then tools/import.sh creature-.
A slice is converted when slices.json has its key (ASSET:SUB); a slice that census.json knows with no body cannot convert and is
not counted. A name is missing when a kind of the backend has a vitals id or a category id that names.json does not have.
"""
import json
import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    backend, content = Path(argv[1]), Path(argv[2])
    try:
        data = json.loads((backend / "gamedata" / "creatures.json").read_text())
    except (OSError, ValueError) as error:
        print(f"warn the creature check: no gamedata/creatures.json in {backend} ({error})")
        return 0
    creatures = content / "creatures"

    def load(name: str) -> dict:
        try:
            return json.loads((creatures / name).read_text())
        except (OSError, ValueError):
            return {}

    converted = set(load("slices.json").get("slices", {}))
    no_body = {f"{s['asset'].upper()}:{s['sub']}" for s in load("census.json").get("slices", []) if not s.get("body")}
    keys = {f"{s['asset'].upper()}:{s['sub']}" for s in data.get("slices", [])}
    slices = sorted(keys - converted - no_body)
    names = load("names.json")
    vitals = {str(k["vitalsId"]) for k in data.get("kinds", []) if k.get("vitalsId")} - set(names.get("vitals", {}))
    categories = {str(k["categoryId"]) for k in data.get("kinds", []) if k.get("categoryId")} - set(names.get("categories", {}))
    version = "?"
    try:
        version = json.loads((backend / "release.json").read_text()).get("version", "?")
    except (OSError, ValueError):
        pass
    if slices or vitals or categories:
        print(f"warn {len(slices)} slices and {len(vitals) + len(categories)} names not converted ({len(vitals)} vitals, "
              f"{len(categories)} category) for the backend release {version}: the client shows placeholders; run "
              f"tools/convert-creatures.sh census, markers, wanted, then tools/import.sh creature- (docs/CREATURES.md)")
    else:
        print(f"ok the creature content has each slice and name of the backend release {version} ({len(keys)} slices, "
              f"{len(no_body)} with no body)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
