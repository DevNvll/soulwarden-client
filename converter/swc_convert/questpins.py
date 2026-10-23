"""The marker sources of the quest tasks (stage Q2, Linear AST-237; research/quest-plan.md, the Q2 row and the converter output row).

One record for each task that has a marker field, one record for each objective with a hand-in NPC, the positions of those NPCs, and
the objective areas. The client builds ONE pin list from these records and the quest state (Lua/swc/questpins.lua): the map icons,
the journal Map action, the compass, and the world pins read that list. The trigger geometry stays apart from the marker positions:
`areas` holds the POI shapes for the objective-area display; the place cues (gamedata/poi-cues.json) keep their own shapes.

The source order of a position (the native rule is not proved yet [I]; the GPT-6.1-Sol study research/quest-pins.md is asked): the
task OverrideLocation, then the POI of the first tag of POITag (its centre), then the TerritoryID; the hand-in marker of an objective is the NPC of
NpcDestinationId (the persons data of stage MVP-H). This module only reads and checks the data; it chooses no order: the client does.

The POI of a tag: the points of interest of quests.json (the backend copy: a centre, a radius, a shape, a territory); else the map
definition with the tag (worldmap.json: the same ids; the centre of the bounds of its shape, the height of the shape position, the shape
polygon; no territory).

Input: SWC_CONTENT/ui/gamedata/quests.json (tools/convert-ui.sh quests), SWC_CONTENT/ui/gamedata/worldmap.json (tools/convert-ui.sh
worldmap), and SWC_CONTENT/persons/persons.json (tools/convert-persons.sh).
Output: SWC_CONTENT/ui/gamedata/quest-pins.json. Usage: python3 -B -m swc_convert.questpins write
"""
from __future__ import annotations

import json
import sys

from . import env

FORMAT = 1
# The task cells of a marker (the names of the task table; research quest-engine.md "Task markers").
TASK_FIELDS = ("OverrideLocation", "POITag", "TerritoryID", "ShowDiegeticTaskPin", "DiegeticPinZOverride",
               "HideDiegeticGatherablePins", "TP_HideOnCompassOnEnterPOI", "TriggerAreaType", "TP_TargetTag")
OBJECTIVE_FIELDS = ("NpcDestinationId", "HideAvailableMapIcon", "HideIncompleteTurninMarker")


def parse_location(cell: str | None) -> list[float] | None:
    """@return The world position (m) of an OverrideLocation cell "x y z", or None: an empty cell, or a cell that is not three numbers
    (the task table has shifted cells, for example "1" in this column)."""
    parts = (cell or "").replace(",", " ").split()
    if len(parts) != 3:
        return None
    try:
        values = [float(v) for v in parts]
    except ValueError:
        return None
    return values


def flag(cell: str | None) -> bool:
    return (cell or "").strip() in ("1", "true", "True")


def number(cell: str | None) -> float | None:
    try:
        return float(cell) if (cell or "").strip() else None
    except ValueError:
        return None


def cells_of(record: dict) -> dict:
    cells = record.get("cells", record)
    return cells if isinstance(cells, dict) else {}


def map_pois(worldmap: dict | None) -> dict[str, dict]:
    """@return The points of interest of the map definitions by lower-case tag, in the form of the quests.json points."""
    if not worldmap:
        return {}
    shapes = {int(s["id"]): s for s in worldmap.get("shapes", []) if "id" in s}
    out: dict[str, dict] = {}
    for key, definition in worldmap.get("definitions", {}).items():
        shape = shapes.get(int(key))
        if shape is None or not shape.get("min") or not shape.get("max"):
            continue
        polygon = shape.get("polygon")
        if isinstance(polygon, str):
            polygon = json.loads(polygon) if polygon.strip() else None
        z = (shape.get("position") or [0, 0, 0])[2]
        poi = {"id": int(key), "center": [round((shape["min"][0] + shape["max"][0]) / 2, 3),
                                          round((shape["min"][1] + shape["max"][1]) / 2, 3), z],
               "shape": polygon, "source": "map"}
        for tag in str(definition.get("pOITag") or "").split(","):
            if tag.strip():
                out.setdefault(tag.strip().lower(), poi)
    return out


def build(quests: dict, persons: list[dict], worldmap: dict | None = None) -> dict:
    """@return The quest-pins document for the quest data, the persons, and the map data."""
    poi_by_tag: dict[str, dict] = map_pois(worldmap)
    for poi in quests.get("pois", []):
        for tag in poi.get("tags", []):
            poi_by_tag[tag.lower()] = poi
    tasks: dict[str, dict] = {}
    areas: dict[str, dict] = {}
    report = {"tasks": 0, "override": 0, "override not a position": 0, "poi": 0, "poi of the map": 0, "poi unknown": 0, "territory": 0,
              "pin": 0}
    for name, record in quests.get("tasks", {}).items():
        cells = cells_of(record)
        if not any((cells.get(f) or "").strip() for f in TASK_FIELDS):
            continue
        out: dict = {}
        raw = (cells.get("OverrideLocation") or "").strip()
        if raw:
            location = parse_location(raw)
            if location is None:
                out["overrideRaw"] = raw
                report["override not a position"] += 1
            else:
                out["override"] = location
                report["override"] += 1
        tag = (cells.get("POITag") or "").strip()
        if tag:
            out["poiTag"] = tag
            poi = poi_by_tag.get(tag.split(",")[0].strip().lower())
            if poi is None:
                report["poi unknown"] += 1
            else:
                out["poi"] = poi["id"]
                out["poiCenter"] = poi.get("center")
                if poi.get("territory") is not None:
                    out["poiTerritory"] = poi["territory"]
                report["poi of the map" if poi.get("source") == "map" else "poi"] += 1
                if poi.get("shape") or poi.get("radius"):
                    areas[name] = {"poi": poi["id"], "center": poi.get("center"), "radius": poi.get("radius"), "shape": poi.get("shape")}
        territory = number(cells.get("TerritoryID"))
        if territory is not None:
            out["territory"] = int(territory)
            report["territory"] += 1
        if flag(cells.get("ShowDiegeticTaskPin")):
            out["showPin"] = True
            report["pin"] += 1
        z = number(cells.get("DiegeticPinZOverride"))
        if z is not None:
            out["pinZ"] = z
        if flag(cells.get("HideDiegeticGatherablePins")):
            out["hideGatherablePins"] = True
        if flag(cells.get("TP_HideOnCompassOnEnterPOI")):
            out["hideOnEnterPoi"] = True
        for field, key in (("TriggerAreaType", "triggerArea"), ("TP_TargetTag", "targetTag")):
            if (cells.get(field) or "").strip():
                out[key] = cells[field].strip()
        tasks[name] = out
        report["tasks"] += 1
    npc_positions: dict[str, list] = {}
    for person in persons:
        npc = (person.get("npcId") or "").lower()
        if npc and person.get("position"):
            npc_positions.setdefault(npc, []).append([round(v, 3) for v in person["position"][:3]])
    objectives: dict[str, dict] = {}
    npcs: dict[str, list] = {}
    for key, record in quests.get("objectives", {}).items():
        cells = cells_of(record)
        npc = (cells.get("NpcDestinationId") or "").strip()
        out = {}
        if npc:
            out["npc"] = npc
            if npc.lower() in npc_positions:
                npcs[npc.lower()] = npc_positions[npc.lower()]
        if flag(cells.get("HideAvailableMapIcon")):
            out["hideAvailableIcon"] = True
        if flag(cells.get("HideIncompleteTurninMarker")):
            out["hideTurninMarker"] = True
        if out:
            objectives[key] = out
    report.update({"objectives": len(objectives), "areas": len(areas), "npcs with a position": len(npcs),
                   "hand-in npcs with no position": len({o["npc"].lower() for o in objectives.values() if o.get("npc")} - set(npcs))})
    return {"format": FORMAT,
            "rule": "the marker sources of the data; the source order is a client rule [I] until research/quest-pins.md",
            "tasks": tasks, "objectives": objectives, "npcs": npcs, "areas": areas, "report": report}


def write() -> dict:
    content = env.content_dir()
    quests = json.loads((content / "ui" / "gamedata" / "quests.json").read_text())
    worldmap_file = content / "ui" / "gamedata" / "worldmap.json"
    worldmap = json.loads(worldmap_file.read_text()) if worldmap_file.exists() else None
    persons_doc = json.loads((content / "persons" / "persons.json").read_text())
    persons = persons_doc.get("persons", persons_doc) if isinstance(persons_doc, dict) else persons_doc
    doc = build(quests, persons if isinstance(persons, list) else [], worldmap)
    out = content / "ui" / "gamedata" / "quest-pins.json"
    out.write_text(json.dumps(doc, indent=0, sort_keys=True))
    return {"file": str(out), **doc["report"]}


def main(argv: list[str]) -> int:
    if argv[:1] == ["write"]:
        print(json.dumps(write(), indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
