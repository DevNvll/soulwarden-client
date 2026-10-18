"""The movement rules of the player (stage P3.3): the motion lists and the facing lists of the movement actions, their named
conditions, and the movement columns of the status rows.

Writes SWC_CONTENT/player/movement.json (tools/convert-player.sh movement):

- motions: for each movement action (Nav_Start, Nav, Nav_Stop, Sprint_Start, Sprint) the first CameraRelativeMotion of the
  action (the strafe movement of the default keyboard setting, research feel-values.md R2.3.2; Nav_Stop: its
  CharacterRelativeMovement, the stop): its Default and its
  Conditionals in the order of the data (the first one whose condition holds wins): the name, the speed (x side, y forward,
  z: m/s), the acceleration and the deceleration (m/s2 on each axis), BackwardsMultiplier, and MinSpeed.
- facings: for each movement action the first CameraRelativeFacing (TurnRate of the Default and of each Conditional).
- conditions: each named condition of the movement action lists (playeractions_nav, sprint, global, idle, traversal,
  idleposetransition, postureinput) as a tree [type, attributes, children]. A Conditional name that is no named condition
  is a Mannequin tag state ("Crouch", "Wade+Crouch": each tag of the "+" list is set).
- statuses: the status rows with a movement column (ability-data.json of the backend copy, format 7: the cells NoNav,
  NoRun, NoSprint, NoDodge, Stunned, Silenced, MoveSpeedMod, SprintSpeedMod, EffectCategories), by their index on the wire.

Research: ~/Games/soulwarden-client/research/p33-movement.md, feel-values.md R2.
"""
from __future__ import annotations

import json
import xml.etree.ElementTree as ET

from . import source
from . import player
from .actions import gamedata_dir

CAGE = "sharedassets/springboardentitites/playerdata/cage/"
MOTION_LISTS = {"Nav_Start": "playeractions_nav", "Nav": "playeractions_nav", "Nav_Stop": "playeractions_nav",
                "Sprint_Start": "playeractions_sprint", "Sprint": "playeractions_sprint"}
CONDITION_LISTS = ("playeractions_nav", "playeractions_sprint", "playeractions_global", "playeractions_idle", "playeractions_traversal",
                   "playeractions_idleposetransition", "playeractions_postureinput", "playeractions_dodge")
STATUS_COLUMNS = ("NoNav", "NoRun", "NoSprint", "NoDodge", "Silenced", "MoveSpeedMod", "SprintSpeedMod")


def vector(text: str | None) -> list[float]:
    parts = [float(v) for v in (text or "").split(",") if v.strip()]
    return (parts + [parts[-1] if parts else 0.0] * 3)[:3]


def number(text: str | None, default: float) -> float:
    try:
        return float(text) if text not in (None, "") else default
    except ValueError:
        return default


def motion_entry(element: ET.Element) -> dict:
    out = {"speed": vector(element.get("Speed")), "accel": vector(element.get("Accel")), "decel": vector(element.get("Decel")),
           "backwards": number(element.get("BackwardsMultiplier"), 1.0)}
    if element.get("Name"):
        out["name"] = element.get("Name")
    if element.get("MinSpeed"):
        out["minSpeed"] = vector(element.get("MinSpeed"))
    return out


def tree(element: ET.Element) -> list:
    return [element.tag, {k: v for k, v in element.attrib.items() if k not in ("Name", "Comment")}, [tree(child) for child in element]]


def read_list(name: str) -> ET.Element:
    return ET.fromstring(source.read(CAGE + name + ".actionlist").decode("utf-8", "replace"))


def movement() -> dict:
    """Writes movement.json (see the module text). @return Counts."""
    lists = {name: read_list(name) for name in set(MOTION_LISTS.values()) | set(CONDITION_LISTS)}
    motions, facings = {}, {}
    for action, list_name in MOTION_LISTS.items():
        node = next((a for a in lists[list_name].find("Actions") if a.get("Name") == action), None)
        if node is None:
            continue
        # Nav_Stop has only a CharacterRelativeMovement (its speed is 0; the decelerations of the stop).
        motion = next(node.iter("CameraRelativeMotion"), None) or next(node.iter("CharacterRelativeMovement"), None)
        if motion is not None:
            default = motion.find("Default")
            motions[action] = {"default": motion_entry(default) if default is not None else None,
                               "conditionals": [motion_entry(c) for c in motion.findall("Conditional")]}
        facing = next(node.iter("CameraRelativeFacing"), None)
        if facing is not None:
            default = facing.find("Default")
            facings[action] = {"default": number(default.get("TurnRate"), 0.0) if default is not None else 0.0,
                               "conditionals": [[c.get("Name"), number(c.get("TurnRate"), 0.0)] for c in facing.findall("Conditional")]}
    conditions = {}
    for list_name in CONDITION_LISTS:
        section = lists[list_name].find("Conditions")
        for element in section if section is not None else []:
            if element.get("Name") and element.get("Name") not in conditions:
                conditions[element.get("Name")] = tree(element)
    statuses = {}
    ability = json.loads((gamedata_dir() / "ability-data.json").read_text())
    for row in ability.get("statusEffects", []):
        name, index = row[0], row[1]
        cells = row[9] if len(row) > 9 and isinstance(row[9], dict) else {}
        values = {column: cells[column] for column in STATUS_COLUMNS if cells.get(column)}
        stunned, no_nav = bool(row[4]), bool(row[5])
        if values or stunned or no_nav:
            statuses[str(index)] = {"id": name, "noNav": no_nav or bool(cells.get("NoNav")), "noRun": bool(cells.get("NoRun")),
                                    "noSprint": bool(cells.get("NoSprint")), "noDodge": bool(cells.get("NoDodge")), "stunned": stunned,
                                    "silenced": bool(cells.get("Silenced")), "moveSpeedMod": float(cells.get("MoveSpeedMod") or 0.0),
                                    "sprintSpeedMod": float(cells.get("SprintSpeedMod") or 0.0),
                                    "categories": str(cells.get("EffectCategories") or "")}
    doc = {"format": 1, "sources": [CAGE + name + ".actionlist" for name in sorted(lists)] + [str(gamedata_dir() / "ability-data.json")],
           "motions": motions, "facings": facings, "conditions": conditions, "statuses": statuses}
    out = player.player_dir() / "movement.json"
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"movement": str(out), "motions": {k: len(v["conditionals"]) for k, v in motions.items()}, "facings": sorted(facings),
            "conditions": len(conditions), "statuses": len(statuses)}
