"""The gather fragments of the player (stage P5.2, docs/OBJECTS.md): the action Gather of playeractions_gather.actionlist plays
the fragment "Gather" with the tag of the GatheringAction of the gatherable row (Chopping, Chopping_Vertical, Mining, Cutting,
Dressing, Handcrafting; none for the rows with no action) and the tags of the tool in the hand (its weapon tags of weapons.json,
for example 2H_Melee,Axe,Tool). The actions converter of the player track (actions.py) adds these fragments to the "use" fragments
of the unarmed set: the key "gather|ACTION|TOOLTAGS" (ACTION: the GatheringAction of the row; TOOLTAGS: the tag text of the tool
item, empty with no tool). Its clip pack then has their clips.
"""
from __future__ import annotations

import json

from . import env
from .creature import gamedata_dir

GATHER_ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_gather.actionlist"
# The GatheringAction of a row -> the Mannequin tag of the fragment (InteractingGatherable). The rows of the AzothStaff, fishing,
# emotes, and the music are not in this stage.
ACTION_TAGS = {"Chopping": "Chopping", "ChoppingVertical": "Chopping_Vertical", "Mining": "Mining", "Cutting": "Cutting",
               "Dressing": "Dressing", "Handcrafting": "Handcrafting", "None": ""}
# The gathering type of a tool -> the actions of its rows.
TYPE_ACTIONS = {"Chopping": ("Chopping", "ChoppingVertical"), "Mining": ("Mining",), "Cutting": ("Cutting",), "Dressing": ("Dressing",)}


def tool_tag_sets() -> dict[str, list[str]]:
    """@return The tag text of each gathering tool of weapons.json -> its gathering types (gathering-data.json "tools")."""
    gathering = json.loads((gamedata_dir() / "gathering-data.json").read_text())
    weapons_path = env.content_dir() / "player" / "weapons.json"
    weapon_items = json.loads(weapons_path.read_text())["items"] if weapons_path.is_file() else {}
    out: dict[str, set[str]] = {}
    for item, tool in gathering["tools"].items():
        tags = (weapon_items.get(item) or {}).get("tag", "")
        if tags:
            out.setdefault(tags, set()).update(tool["types"])
    return {tags: sorted(types) for tags, types in out.items()}


def fragments(record, find_fragment, tag_set) -> dict[str, str]:
    """@return The use key of each gather -> the fragment key of record (the functions of actions.py: record(adb, element, name),
    find_fragment(name, tags, extra, offhand), tag_set(text))."""
    out: dict[str, str] = {}
    pairs = [(action, "") for action in ("None", "Handcrafting")]
    for tags, types in tool_tag_sets().items():
        for gathering_type in types:
            pairs += [(action, tags) for action in TYPE_ACTIONS.get(gathering_type, ())]
    for action, tool in sorted(set(pairs)):
        found = find_fragment("Gather", tag_set(tool), tag_set(ACTION_TAGS[action]), False)
        if found is not None:
            out[f"gather|{action}|{tool}"] = record(found[0], found[1], "Gather")
    return out
