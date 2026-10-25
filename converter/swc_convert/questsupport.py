"""The client coverage report of the quests (stage Q9, Linear AST-244; research/quest-plan.md "Converter output" Q9).

Usage (tools/convert-ui.sh questsupport MANIFEST [--out FILE]; output outside git, default SWC_CONTENT/quests/quest-client-support.json):
  MANIFEST  the support manifest of the backend (stage 1.8e; Soulwarden `tools/world quest support FILE`): each row of the quest data with
            its kind, grants, gates, tasks and their suppliers, rewards, the reasons of its block, and the histories that play it.

For each row of the manifest the report adds the client side:
  data     the row, its tree, and each task of the manifest in gamedata/quests.json; the text key of the title in the English texts;
  grant    each supported grant of an offer: the NPC row and the state with the offer in gamedata/conversations.json, and a placed person
           with the NPC id in persons/persons.json;
  gates    the gate columns of the row and the client rule of each (swc/conversation.lua unavailable);
  tasks    for each task the client part of its type (TASK_PARTS) and the client data of its supplier (a person, a point of interest, a
           quest object, a scene, a cinematic, a game event, a recipe, an item);
  rewards  each reward kind and its client display; each reward item in gamedata/items.tsv (a loot container: the open popup);
  tests    the fixtures and the live checks of the row (TESTS).
The class of a row: "unavailable" (the backend does not support it: its first reason), "missing data" (a client data check fails),
"client stage" (a task type whose client part is a later stage), "untested", "tested". The counts give each class for all rows, for the
supported rows, and for the rows that each history of the manifest plays.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path

from . import env
from .actions import gamedata_dir

# The client part of each task type: (the part, the status). The status: "client" (a client part that the player uses or sees: the
# display of the task and the action that the server needs), "server" (the server decides with no client action: the client shows the
# state), "stage Q8" (a later client stage), "none" (no client part; the backend does not run these types either).
TASK_PARTS = {
    "consecutivetaskcontainer": ("the tree (swc/quests.lua)", "server"),
    "simpletaskcontainer": ("the tree (swc/quests.lua)", "server"),
    "partialtaskcontainer": ("the tree (swc/quests.lua)", "server"),
    "taskobjective": ("a child objective (swc/quests.lua)", "server"),
    "taskunlockachievement": ("the achievement state (slot 66, swc/owner.lua)", "server"),
    "tasklockachievement": ("the achievement state (slot 66, swc/owner.lua)", "server"),
    "taskachievement": ("the achievement state (slot 66, swc/owner.lua)", "server"),
    "tasktriggerarea": ("the position of the character; the pins (swc/questpins.lua)", "server"),
    "taskgotopoitag": ("the position; the pins and the place cues (swc/questpins.lua)", "server"),
    "taskexitpoitag": ("the position; the pins and the place cues (swc/questpins.lua)", "server"),
    "tasktriggergameevent": ("the whisper of the event (swc/scenes.lua)", "client"),
    "taskgameevent": ("the interaction with a quest object (Interact, docs/OBJECTS.md)", "client"),
    "taskinteract": ("the interaction with an object of the tag (Interact; the quest tags of Q6)", "client"),
    "taskcutscenecomplete": ("the in-game scene (swc/scenes.lua, UI/SwcUiScenes: 6703, 6706, 6705)", "client"),
    "taskcinematiccomplete": ("the cinematic video stand-in (swc/scenes.lua: 6606, 6542)", "client"),
    "taskkillcontribution": ("the fight (Combat) and the creatures", "client"),
    "taskhaveitems": ("the bag (swc/items.lua)", "client"),
    "taskhaveandreturnitems": ("the bag; the hand-in (swc/conversation.lua)", "client"),
    "taskgatherandreturnitems": ("the gathering and the loot; the hand-in", "client"),
    "taskgatheritems": ("the gathering and the loot (docs/OBJECTS.md)", "client"),
    "taskrequiregatheritems": ("the gathering (docs/OBJECTS.md)", "client"),
    "taskequipitem": ("the equipment (swc/items.lua)", "client"),
    "taskcraftrecipe": ("the crafting screen (swc/crafting.lua)", "client"),
    "taskconversationstate": ("the conversation (swc/conversation.lua)", "client"),
    "taskconversationtopic": ("the conversation topics (swc/conversation.lua, 3158)", "client"),
    "taskgiveitem": ("the bag state", "server"),
    "taskgiveandtakeitem": ("the bag state", "server"),
    "taskremoveitem": ("the bag state", "server"),
    "taskdie": ("the health state", "server"),
    "tasktimer": ("the timer row of the tracker (stage Q8, AST-243)", "stage Q8"),
}

# The tests of a row: the offline fixtures and the live checks (the quest stages Q4 to Q7). A row that is not here is untested.
TESTS = {
    "06_getoffbeach": ["fixture questpersons", "fixture questrewards", "live AST-239 (Q4: the hand-in, 794)", "live AST-240 (Q5: the scene)"],
    "06_skinningknife": ["fixture questpersons (the offer, 3772)", "fixture questrewards (the full bag, the pouch)",
                         "live AST-239 (the accept)"],
    "06_boarfeast": ["fixture questrewards (the availability)"],
    "06_raisealarm": ["fixture questrewards (the reward data, the standing)"],
}
for _variant in ("destroyer", "musketeer", "mystic", "occultist", "ranger", "soldier", "swordsman"):
    TESTS["06_boarfeast_" + _variant] = ["fixture questrewards (the archetype variant)"]

# The known gaps (the decision of the orchestrator, 2026-10-25: written in the report, not a part of stage Q9): the row and its note.
KNOWN_GAPS = {
    "9932_0104_RelicReclamation": "the offer is a note (9932_VK_RomacNote) with no placed person in persons.json",
    "rewards: House_HousingItem_QuestRewardGC_Lighting_MemoryOfFallen": "a housing reward item that is not in items.tsv",
    "rewards: House_HousingItem_Bed_Settler_Bed02_A": "a housing reward item that is not in items.tsv",
}

# The gate columns of a row and the client rule of each (swc/conversation.lua unavailable, the order of Quests::available).
GATE_RULES = {
    "RequiredLevel": "the level",
    "RequiredAchievementId": "the conditional with the NPA rows",
    "RequiredFaction": "the faction mask (an unaligned character)",
    "RequiredGameModeId": "a gate with no rule: no offer",
    "QuestArcTag": "a gate with no rule: no offer",
    "RequiredWorldTags": "a gate with no rule: no offer",
    "RequiredProgression": "a gate with no rule: no offer",
}

REWARD_PARTS = {
    "experience": "the reward panel and the XP state (slot 70)",
    "coin": "the reward panel and the coin state (slot 60)",
    "items": "the reward panel and the bag state (slot 61); the full-bag rule",
    "standing": "the standing of the territory (slot 65; the map)",
    "azoth": "the Azoth balance (the currency node AzothAmount)",
    "next": "the next objective (ObjectiveReward)",
}


def load(path: Path) -> dict:
    return json.loads(path.read_text())


class Client:
    """The client data that the report checks."""

    def __init__(self, content: Path):
        ui = content / "ui" / "gamedata"
        quests = load(ui / "quests.json")
        self.objectives = quests.get("objectives", {})
        self.tasks = quests.get("tasks", {})
        self.events = {k.lower(): v for k, v in quests.get("events", {}).items()}
        self.poi_tags = {tag.lower() for poi in quests.get("pois", []) for tag in poi.get("tags", [])}
        conversations = load(ui / "conversations.json")
        self.npcs = {k.lower(): [s.lower() for s in v] for k, v in conversations.get("npcs", {}).items()}
        self.states = {k.lower(): v for k, v in conversations.get("states", {}).items()}
        persons = load(content / "persons" / "persons.json").get("persons", [])
        self.placed = {p["npcId"].lower() for p in persons if p.get("npcId")}
        self.scenes = {k.lower() for k in load(content / "quests" / "scenes.json").get("scenes", {})}
        self.cinematics = {k.lower() for k in load(ui / "scenes.json").get("cinematics", {})}
        self.recipes = {k.lower() for k in load(ui / "crafting.json").get("recipes", {})}
        self.items = {}
        with (ui / "items.tsv").open() as handle:
            header = handle.readline().rstrip("\n").split("\t")
            column = header.index("itemClass")
            for line in handle:
                cells = line.rstrip("\n").split("\t")
                self.items[cells[0].lower()] = cells[column].lower()
        self.texts = set(load(content / "ui" / "localization" / "en-us.json"))
        self.quest_objects = set()
        for region in sorted((content / "world-objects" / "regions").glob("*.json")):
            for match in re.finditer(r'"quest":\{"tag":"([^"]+)"', region.read_text()):
                self.quest_objects.add(match.group(1).lower())
        # The producers of a game event (the backend copy): a gatherable row (its event; its objects have the tag IO_ROW) and a
        # destructible quest object row (quest-objects.json: its objects have their own tags). A client file of the destructible objects
        # (world-objects/quest-objects.json, stage Q9) adds its tags.
        gamedata = gamedata_dir()
        self.event_tags: dict[str, set[str]] = collections.defaultdict(set)
        for row in load(gamedata / "gathering-data.json").get("rows", []):
            if row.get("event"):
                self.event_tags[row["event"].lower()].add("io_" + row["id"].lower())
        quest_objects = load(gamedata / "quest-objects.json")
        variants = collections.defaultdict(set)
        for obj in quest_objects.get("objects", []):
            variants[obj["variant"].lower()].add(obj["tag"].lower())
        for row in quest_objects.get("rows", []):
            if row.get("event"):
                self.event_tags[row["event"].lower()] |= variants.get(row["id"].lower(), set())
        self.destructible_tags = {tag for tags in variants.values() for tag in tags}
        client_destructibles = content / "world-objects" / "quest-objects.json"
        if client_destructibles.is_file():
            # An object counts only with drawn parts (a slice with meshes in the plugin), not for its entry alone.
            document = load(client_destructibles)
            drawn = {path for path, parts in document.get("slices", {}).items() if parts}
            self.quest_objects |= {o["tag"].lower() for o in document.get("objects", [])
                                   if o.get("tag") and any(path in drawn for path in o.get("slices", {}).values())}
        census = load(content / "creatures" / "census.json")
        self.creature_slices = len(census.get("slices", []))
        self.creature_failures = len(census.get("failures", []))


def supplier_check(client: Client, task: dict) -> tuple[str, bool]:
    """@return The client data of the supplier of a task and True when it is there (or when the supplier needs no client data)."""
    supplier = task.get("supplier") or ""
    match = re.match(r"the person (\S+)", supplier)
    if match:
        npc = match.group(1).lower()
        ok = npc in client.npcs and npc in client.placed
        return f"the NPC row and a placed person {match.group(1)}", ok
    match = re.match(r"the point of interest (\S+)", supplier)
    if match:
        return f"the POI tag {match.group(1)} (quests.json pois)", match.group(1).lower() in client.poi_tags
    match = re.match(r"(a destructible quest object|a quest object) \(([^)\s]+)\)", supplier)
    if match:
        tags = client.event_tags.get(match.group(2).lower(), set())
        have = sorted(tags & client.quest_objects)
        kind = "a destructible quest object" if tags & client.destructible_tags else "a quest object"
        return (f"{kind} of the event {match.group(2)} (tags {', '.join(sorted(tags)) or 'none'}; in the client: "
                f"{', '.join(have) or 'none'})", bool(have))
    match = re.match(r"the objects of the tag (\S+)", supplier)
    if match:
        return f"a quest object of the tag {match.group(1)} (world-objects)", match.group(1).lower() in client.quest_objects
    match = re.search(r"the client asks for (\S+?)\)", supplier)
    if match:
        return f"the scene {match.group(1)} (quests/scenes.json)", match.group(1).lower() in client.scenes
    if supplier.startswith("messages 6606"):
        return f"the cinematics ({len(client.cinematics)} in scenes.json)", bool(client.cinematics)
    match = re.match(r"the activation \(the game event (\S+?)\)", supplier)
    if match:
        return f"the game event {match.group(1)} (quests.json events)", match.group(1).lower() in client.events
    match = re.match(r"the recipe (\S+)", supplier)
    if match:
        return f"the recipe {match.group(1)} (crafting.json)", match.group(1).lower() in client.recipes
    if supplier.startswith("the kills of the category"):
        return (f"the creatures ({client.creature_slices} slices, {client.creature_failures} failures)",
                client.creature_slices > 0 and client.creature_failures == 0)
    return "no client data", True


def row_report(client: Client, row: dict) -> dict:
    oid = row["id"]
    key = oid.lower()
    out: dict = {"id": oid, "kind": row.get("kind"), "backend": {"supported": row.get("supported", False),
                                                              "blocked": row.get("blocked", [])[:3]}}
    missing: list[str] = []
    objective = client.objectives.get(key)
    task_ids = [t["id"] for t in row.get("tasks", [])]
    lost_tasks = [t for t in task_ids if t.lower() not in client.tasks]
    title = (objective or {}).get("cells", {}).get("Title", "")
    data = {"row": objective is not None, "tree": bool(objective and objective.get("tree")), "tasks": len(task_ids),
            "tasksMissing": lost_tasks[:5], "title": title, "titleText": title.lstrip("@").lower() in client.texts if title else None}
    out["data"] = data
    if objective is None:
        missing.append("data: no row in quests.json")
    if lost_tasks:
        missing.append(f"data: {len(lost_tasks)} tasks not in quests.json")
    if title and not data["titleText"]:
        missing.append("data: no English text of the title")
    grants = []
    for grant in row.get("grants", []):
        entry = {"source": grant.get("source"), "supported": grant.get("supported"), "detail": grant.get("detail", "")}
        if grant.get("source") == "offer" and grant.get("supported"):
            parts = entry["detail"].split()
            npc, state = (parts + ["", ""])[:2]
            npc, state = npc.lower(), state.lower()
            entry["npcRow"] = npc in client.npcs and (not state or state in client.npcs[npc])
            entry["offer"] = key in [o.lower() for o in client.states.get(state, {}).get("objectives", [])] if state else None
            entry["placed"] = npc in client.placed
            if not (entry["npcRow"] and entry["placed"] and entry["offer"] is not False):
                missing.append(f"grant: the offer of {parts[0] if parts else '?'} {parts[1] if len(parts) > 1 else ''}".strip())
        grants.append(entry)
    out["grants"] = grants
    cells = (objective or {}).get("cells", {})
    out["gates"] = {name: rule for name, rule in GATE_RULES.items() if cells.get(name) not in (None, "", "0", "None")}
    tasks, stages = [], set()
    for task in row.get("tasks", []):
        part, status = TASK_PARTS.get(task.get("type", ""), ("no client part", "none"))
        data_text, ok = supplier_check(client, task) if task.get("supported") else ("", True)
        entry = {"id": task["id"], "type": task.get("type"), "part": part, "status": status, "supplier": task.get("supplier", ""),
                 "clientData": data_text, "clientDataOk": ok}
        if task.get("supported") and status in ("stage Q8", "none"):
            stages.add(f"{task.get('type')} ({status})")
        if task.get("supported") and not ok:
            missing.append(f"task {task['id']}: {data_text}")
        tasks.append(entry)
    out["tasks"] = tasks
    rewards = row.get("rewards", {})
    shown = {}
    for kind in ("experience", "coin", "standing", "azoth"):
        if rewards.get(kind):
            shown[kind] = {"value": rewards[kind], "display": REWARD_PARTS[kind]}
    items = []
    for text in rewards.get("items", []):
        item = text.rsplit(" x", 1)[0]
        item_class = client.items.get(item.lower())
        items.append({"item": text, "inItemData": item_class is not None,
                      "lootContainer": bool(item_class and "lootcontainer" in item_class)})
        if item_class is None:
            missing.append(f"rewards: the item {item} is not in items.tsv")
    if items:
        shown["items"] = {"value": items, "display": REWARD_PARTS["items"]}
    if rewards.get("next"):
        shown["next"] = {"value": rewards["next"], "display": REWARD_PARTS["next"]}
    out["rewards"] = shown
    out["tests"] = TESTS.get(oid, TESTS.get(key, []))
    if not row.get("supported"):
        reasons = row.get("blocked", [])
        out["class"], out["reason"] = "unavailable", reasons[0] if reasons else "the backend does not support it"
    elif missing:
        out["class"], out["reason"] = "missing data", missing[0]
    elif stages:
        out["class"], out["reason"] = "client stage", sorted(stages)[0]
    elif not out["tests"]:
        out["class"], out["reason"] = "untested", ""
    else:
        out["class"], out["reason"] = "tested", ""
    out["missing"] = missing
    return out


def report(manifest_path: Path, content: Path) -> dict:
    manifest = load(manifest_path)
    client = Client(content)
    rows = manifest.get("objectives", []) + manifest.get("missionTemplates", [])
    reports = [row_report(client, row) for row in rows]
    by_id = {r["id"]: r for r in reports}
    counts = {"all": dict(collections.Counter(r["class"] for r in reports)),
              "supported": dict(collections.Counter(r["class"] for r in reports if r["backend"]["supported"]))}
    reasons = collections.Counter(r["reason"] for r in reports if r["class"] in ("missing data", "client stage"))
    histories = []
    for profile in manifest.get("profiles", []):
        played = profile.get("played", [])
        histories.append({"id": profile.get("id"), "group": profile.get("group"), "level": profile.get("level"),
                          "played": len(played),
                          "classes": dict(collections.Counter(by_id[p]["class"] for p in played if p in by_id)),
                          "rows": {p: by_id[p]["class"] for p in played if p in by_id} if profile.get("group") == "new character" else {}})
    gaps = []
    for r in reports:
        for key, note in KNOWN_GAPS.items():
            item = key.split(": ", 1)[1] if key.startswith("rewards: ") else None
            if r["id"] == key or (item and any(item in m for m in r["missing"])):
                gaps.append({"row": r["id"], "class": r["class"], "note": note})
    return {"format": 1, "manifest": manifest_path.name, "knownGaps": gaps, "manifestCounts": manifest.get("counts", {}).get("rows"),
            "rule": __doc__.split("\n\n")[0], "content": str(content),
            "creatures": {"slices": client.creature_slices, "failures": client.creature_failures},
            "counts": counts, "clientReasons": dict(reasons.most_common(40)), "histories": histories, "rows": reports}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-quest-support", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--content", type=Path, help="the client content (default SWC_CONTENT)")
    args = parser.parse_args(argv)
    content = args.content or env.content_dir()
    result = report(args.manifest, content)
    out = args.out or content / "quests" / "quest-client-support.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=1, sort_keys=True))
    summary = {"out": str(out), "counts": result["counts"], "clientReasons": dict(list(result["clientReasons"].items())[:12]),
               "histories": [h for h in result["histories"] if h["group"] in ("new character", "no template (decision D2 option A)")]}
    for history in summary["histories"]:
        history.pop("rows", None)
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
