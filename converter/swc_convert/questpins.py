"""The marker sources of the quest tasks (stage Q2, Linear AST-237; research/quest-pins.md, the study of the native producer).

One record for each task that has a marker cell, one record for each objective with a hand-in NPC, and the positions of the NPCs. The
client builds ONE pin list from these records and the quest state (Lua/swc/questpins.lua): the map icons, the journal Map action, the
compass, and the world pins read that list. This module only reads and checks the data; the client applies the rules of the study.

The cells (research/quest-pins.md sections 2 and 4):
  - OverrideLocation: the native parser (0x178CF90): entries split by commas; each entry starts at (0, 0, 0) and takes up to three
    numbers split by spaces (strtof; a missing number stays 0). "1" is one point (1, 0, 0); "1295,1424,930" is three points. [R]
  - POITag: the tags (commas). The POI ids of the tags: the map definitions (worldmap.json, the POITag column) and the backend points
    (quests.json "pois"). The point of a POI: the native point comes from the territory data provider, whose load path is not proved
    [U]; the only point here is the POI placement of the backend copy (quests.json "pois" center: the Territory component of the POI
    placement). A map definition with no backend point gives an id and no point (no guessed centre).
  - TerritoryID: a filter of the POI choice and the result of GetTaskTerritoryId, not a position. [R]
  - GameModeID: the POI path needs it 0 or blank. [R]
  - A conversation task (type code 16, taskconversationstate): the override, else the NPC of ItemName (the persons data). [R]
  - DiegeticPinZOverride: blank or 0 is the sentinel -101 (no override: the client adds 1.75 m); a value of at least -100 is an absolute
    height. [R]
  - ShowDiegeticTaskPin, HideDiegeticGatherablePins, TP_HideOnCompassOnEnterPOI, TriggerAreaType, TP_TargetTag: copied.
The objective areas: no record. The study found no search-area display rule [U]; the trigger geometry stays with the backend.

Input: SWC_CONTENT/ui/gamedata/quests.json (tools/convert-ui.sh quests), SWC_CONTENT/ui/gamedata/worldmap.json (tools/convert-ui.sh
worldmap), and SWC_CONTENT/persons/persons.json (tools/convert-persons.sh).
Output: SWC_CONTENT/ui/gamedata/quest-pins.json. Usage: python3 -B -m swc_convert.questpins write
"""
from __future__ import annotations

import json
import re
import sys

from . import env

FORMAT = 2
# The task cells of a marker (the names of the task table; research quest-engine.md "Task markers").
TASK_FIELDS = ("OverrideLocation", "POITag", "TerritoryID", "ShowDiegeticTaskPin", "DiegeticPinZOverride",
               "HideDiegeticGatherablePins", "TP_HideOnCompassOnEnterPOI", "TriggerAreaType", "TP_TargetTag")
OBJECTIVE_FIELDS = ("NpcDestinationId", "HideAvailableMapIcon", "HideIncompleteTurninMarker")
CONVERSATION_CODE = 16   # taskconversationstate (the factory 0x4461150; research/quest-pins.md 2.2)
NUMBER = re.compile(r"^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?")


def strtof(text: str) -> float:
    """@return The number at the start of a text as strtof reads it (0 with no number)."""
    match = NUMBER.match(text.strip())
    return float(match.group(0)) if match else 0.0


def parse_locations(cell: str | None) -> list[list[float]]:
    """@return The points of an OverrideLocation cell by the native parser: comma entries, up to three space numbers each, 0 for a
    missing number; an empty entry is skipped."""
    out = []
    for entry in (cell or "").split(","):
        numbers = entry.split()
        if not numbers:
            continue
        point = [0.0, 0.0, 0.0]
        for i, text in enumerate(numbers[:3]):
            point[i] = strtof(text)
        out.append(point)
    return out


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


def tags_of(cell: str | None) -> list[str]:
    return [t.strip().lower() for t in (cell or "").split(",") if t.strip()]


def build(quests: dict, persons: list[dict], worldmap: dict | None = None) -> dict:
    """@return The quest-pins document for the quest data, the persons, and the map data."""
    # The POI ids of each tag: the map definitions, then the backend points (same ids).
    ids_by_tag: dict[str, list[int]] = {}
    for key, definition in (worldmap or {}).get("definitions", {}).items():
        for tag in tags_of(definition.get("pOITag")):
            ids_by_tag.setdefault(tag, [])
            if int(key) not in ids_by_tag[tag]:
                ids_by_tag[tag].append(int(key))
    points: dict[int, dict] = {}
    for poi in quests.get("pois", []):
        points[int(poi["id"])] = poi
        for tag in tags_of(",".join(poi.get("tags", []))):
            ids_by_tag.setdefault(tag, [])
            if int(poi["id"]) not in ids_by_tag[tag]:
                ids_by_tag[tag].append(int(poi["id"]))
    npc_positions: dict[str, list] = {}
    for person in persons:
        npc = (person.get("npcId") or "").lower()
        if npc and person.get("position"):
            npc_positions.setdefault(npc, []).append([round(v, 3) for v in person["position"][:3]])
    tasks: dict[str, dict] = {}
    npcs: dict[str, list] = {}
    report = {"tasks": 0, "override": 0, "override entries": 0, "override short entries": 0, "poi ids": 0, "poi with a point": 0,
              "poi with no point": 0, "poi unknown": 0, "territory": 0, "pin": 0, "pinZ": 0, "conversation npc": 0,
              "conversation npc with no place": 0}
    unusual: list[str] = []
    for name, record in quests.get("tasks", {}).items():
        cells = cells_of(record)
        conversation = record.get("code") == CONVERSATION_CODE
        npc = (cells.get("ItemName") or "").strip() if conversation else ""
        if not npc and not any((cells.get(f) or "").strip() for f in TASK_FIELDS):
            continue
        out: dict = {}
        raw = (cells.get("OverrideLocation") or "").strip()
        if raw:
            locations = parse_locations(raw)
            if locations:
                out["override"] = locations
                report["override"] += 1
                report["override entries"] += len(locations)
            short = [e for e in raw.split(",") if e.strip() and len(e.split()) != 3]
            if short:
                report["override short entries"] += 1
                unusual.append(f"{name}: {raw}")
        tags = tags_of(cells.get("POITag"))
        if tags:
            out["poiTags"] = tags
            ids: list[int] = []
            for tag in tags:
                for ident in ids_by_tag.get(tag, []):
                    if ident not in ids:
                        ids.append(ident)
            if not ids:
                report["poi unknown"] += 1
            else:
                report["poi ids"] += 1
                with_point = [[i] + [round(v, 4) for v in points[i]["center"][:3]] for i in ids if i in points and points[i].get("center")]
                if with_point:
                    out["pois"] = with_point
                    report["poi with a point"] += 1
                else:
                    report["poi with no point"] += 1
                out["poiIds"] = ids
                territories = [points[i]["territory"] for i in ids if i in points and points[i].get("territory")]
                if territories:
                    out["poiTerritory"] = territories[0]
        territory = number(cells.get("TerritoryID"))
        if territory:
            out["territory"] = int(territory)
            report["territory"] += 1
        game_mode = (cells.get("GameModeID") or "").strip()
        if game_mode and game_mode != "0":
            out["gameMode"] = game_mode
        if flag(cells.get("ShowDiegeticTaskPin")):
            out["showPin"] = True
            report["pin"] += 1
        z = number(cells.get("DiegeticPinZOverride"))
        if z is not None and z != 0 and z >= -100:
            out["pinZ"] = z
            report["pinZ"] += 1
        if flag(cells.get("HideDiegeticGatherablePins")):
            out["hideGatherablePins"] = True
        if flag(cells.get("TP_HideOnCompassOnEnterPOI")):
            out["hideOnEnterPoi"] = True
        for field, key in (("TriggerAreaType", "triggerArea"), ("TP_TargetTag", "targetTag")):
            if (cells.get(field) or "").strip():
                out[key] = cells[field].strip()
        if npc:
            out["npc"] = npc
            report["conversation npc"] += 1
            if npc.lower() in npc_positions:
                npcs[npc.lower()] = npc_positions[npc.lower()]
            else:
                report["conversation npc with no place"] += 1
        tasks[name] = out
        report["tasks"] += 1
    objectives: dict[str, dict] = {}
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
    report.update({"objectives": len(objectives), "npcs with a position": len(npcs),
                   "hand-in npcs with no position": len({o["npc"].lower() for o in objectives.values() if o.get("npc")} - set(npcs))})
    return {"format": FORMAT,
            "rule": "the marker sources of the data (research/quest-pins.md); the POI point is the backend POI placement [U]",
            "tasks": tasks, "objectives": objectives, "npcs": npcs, "report": report, "unusual": unusual}


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
    return {"file": str(out), **doc["report"], "unusual": doc["unusual"][:20]}


def main(argv: list[str]) -> int:
    if argv[:1] == ["write"]:
        print(json.dumps(write(), indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
