"""The mission data of the boards (C-12 of ut567; QG6a and QG6b; Soulwarden docs/systems/mission-boards.md), for the town offers of the client
(Boards/SwcTownOffers: the C++ generator) and the board screens (swc/boards.lua: townprojects.luac, owmissionboard, the mission records of
ObjectivesDataManagerBus GetMissionData).

Source: the backend copy (SWC_GAMEDATA_DIR or SWC_WORK/backend/current/gamedata/mission-data.json, format 1: the mission rows of the town
and the world tables with their cells, the projects, the success events, the weights, the reward modifiers, the town buckets). The values
of the filter follow the loader of the backend (gamedata/mission_data.cpp): a whole number cell is a decimal number (an empty cell is 0);
Enabled is true when the cell is absent or "1"/"true"; the project is the CRC-32 of the lower-case TerritoryProjectId.

Output: SWC_CONTENT/ui/gamedata/missions.json (outside git):
  "town":     {"rows": [[crc, projectCrc, enabled, territories, exclude, minLevel, maxLevel, tradeskill, tradeskillLevel, conditional], ...],
               "projects": [[crc, type, level], ...] (the order of the data), "buckets": [{"key", "goal", "missions"}, ...]}
  "missions": {"CRC": {"id", "table", "objective", "objectiveCrc", "cells"}}  (each row of both tables)
  "projects": [{"id", "crc", "type", "level", "cells"}], "events", "rewardModifiers", "weights" (as the backend copy has them)
"""
from __future__ import annotations

import json
import math
import zlib

from . import creature, env


def crc(text: str) -> int:
    return zlib.crc32(str(text).lower().encode()) & 0xFFFFFFFF


def whole(text) -> int | None:
    """A whole number cell of the backend loader: empty is 0; a finite decimal 0 to 2^32-1 is truncated; else None."""
    text = "" if text is None else str(text)
    if text == "":
        return 0
    try:
        value = float(text)
    except ValueError:
        return None
    if not math.isfinite(value) or value < 0 or value > 4294967295.0:
        return None
    return int(value)


def truth(text) -> bool:
    return str(text) in ("1", "true", "TRUE", "True")


def build(source: dict) -> tuple[dict, list[str]]:
    problems = []
    rows, missions = [], {}
    for entry in source.get("missions", []):
        cells = entry.get("cells", {})
        missions[str(entry["crc"])] = {"id": entry["id"], "table": entry["table"], "objective": entry.get("objective", ""),
                                       "objectiveCrc": entry.get("objectiveCrc", 0), "cells": cells}
        if entry["table"] != "town":
            continue
        numbers = [whole(cells.get(name)) for name in ("MinLevel", "MaxLevel", "TradeskillLevel")]
        if None in numbers:
            problems.append(f"{entry['id']}: a level cell that is not a whole number")
            continue
        rule = entry.get("territories", {"ids": [], "exclude": False})
        project = cells.get("TerritoryProjectId", "")
        rows.append([entry["crc"], crc(project) if project else 0, "Enabled" not in cells or truth(cells.get("Enabled")),
                     list(rule.get("ids", [])), bool(rule.get("exclude", False)), numbers[0], numbers[1], cells.get("RequiredTradeskill", ""),
                     numbers[2], cells.get("RequiredAchievementConditional", "")])
    projects = [[p["crc"], p["type"], p["level"]] for p in source.get("projects", [])]
    for p in source.get("projects", []):
        if p["crc"] != crc(p["id"]):
            problems.append(f"the project {p['id']}: its CRC is not the CRC-32 of its lower-case id")
    buckets = [{"key": b["key"], "goal": b["goal"], "missions": b["missions"]} for b in source.get("townBuckets", [])]
    known = {r[0] for r in rows}
    for bucket in buckets:
        missing = [m for m in bucket["missions"] if m not in known]
        if missing:
            problems.append(f"the bucket {bucket['goal']} names {len(missing)} mission(s) that are not town rows")
    doc = {"format": 1, "town": {"rows": rows, "projects": projects, "buckets": buckets}, "missions": missions,
           "projects": source.get("projects", []), "events": source.get("events", {}), "rewardModifiers": source.get("rewardModifiers", []),
           "weights": source.get("weights", [])}
    return doc, problems


def write() -> dict:
    source = json.loads((creature.gamedata_dir() / "mission-data.json").read_text())
    doc, problems = build(source)
    target = env.content_dir() / "ui" / "gamedata" / "missions.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(doc, separators=(",", ":")))
    return {"missions": len(doc["missions"]), "townRows": len(doc["town"]["rows"]), "projects": len(doc["town"]["projects"]),
            "buckets": [[b["goal"], len(b["missions"])] for b in doc["town"]["buckets"]], "problems": problems[:20], "file": str(target)}


def main(argv=None) -> int:
    print(json.dumps(write(), indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
