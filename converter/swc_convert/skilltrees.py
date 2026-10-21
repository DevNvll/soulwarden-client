"""The data of the character window (stage MVP-G, Linear AST-222; docs/UI.md "The character window"): the weapon mastery trees, the
rank tables, and the trade skills, for swc/skills.lua (the native part of skills.luac, masterytree.luac, masterytreewindow.luac).

Sources:
- the weapon ability tables of the game (sharedassets/.../datatables/weaponabilities/javelindata_ability_*.datasheet): each node of
  a tree (AbilityID, TreeId, TreeRowPosition, TreeColumnPosition, IsActiveAbility, UnlockDefault, the texts, the icon, the sound);
- javelindata_progressionpointdata.datasheet: the node that a node needs (RequiredProgressionPointId) and its pool;
- the backend copy (SWC_GAMEDATA_DIR or SWC_WORK/backend/current/gamedata/progression-tables.json): the progressions of the
  backend (weapons and trade skills with their largest rank and rank table) and the rank tables (the experience and the reset
  cost of each rank);
- the index of an ability table in the client (protocol notes 4.13 kAbilityTableOrder: the request 2701 and 186 name a table by
  this index, not by its id).

Output: SWC_CONTENT/ui/gamedata/skilltrees.json (outside git).
"""
from __future__ import annotations

import json
import zlib

from . import creature, datasheet, env, source

TABLES = "sharedassets/springboardentitites/datatables/"
POINTS = TABLES + "javelindata_progressionpointdata.datasheet"

# The ability tables of the client in its sequence (protocol notes 4.13, the backend gamedata::kAbilityTableOrder): the index of
# each table of a weapon (the 13 other tables are tables of creatures, perks, and items).
TABLE_INDEX = {"FlailAbilityTable": 0, "BlunderbussAbilityTable": 1, "LifeMagicAbilityTable": 2, "HatchetAbilityTable": 3,
               "FireMagicAbilityTable": 4, "SpearAbilityTable": 5, "GreatSwordAbilityTable": 6, "IceMagicAbilityTable": 9,
               "WarHammerAbilityTable": 13, "VoidGauntletAbilityTable": 15, "DaggerAbilityTable": 16, "MusketAbilityTable": 18,
               "GreatAxeAbilityTable": 19, "RapierAbilityTable": 20, "BowAbilityTable": 25, "SwordAbilityTable": 27}
# The table ids of the protocol notes (a check of the names above).
TABLE_CRC = {"FlailAbilityTable": 0x2915FB5D, "BlunderbussAbilityTable": 0x6020B33F, "LifeMagicAbilityTable": 0x6E4FD87A,
             "HatchetAbilityTable": 0xAF42A7F2, "FireMagicAbilityTable": 0x5553E699, "SpearAbilityTable": 0x7C2BB2C5,
             "GreatSwordAbilityTable": 0x180B9A9F, "IceMagicAbilityTable": 0x4A366E22, "WarHammerAbilityTable": 0xF94C464F,
             "VoidGauntletAbilityTable": 0xAC457988, "DaggerAbilityTable": 0x572562B8, "MusketAbilityTable": 0x1C79C0C7,
             "GreatAxeAbilityTable": 0xE7AAC3FB, "RapierAbilityTable": 0x81575CBB, "BowAbilityTable": 0xC7EA8FF8,
             "SwordAbilityTable": 0xE8EC5716}


def crc(text: str) -> int:
    return zlib.crc32(str(text).lower().encode()) & 0xFFFFFFFF


def flag(value) -> bool:
    return value is True or str(value).strip().lower() in ("true", "1")


def write() -> dict:
    backend = json.loads((creature.gamedata_dir() / "progression-tables.json").read_text())
    abilities_doc = json.loads((env.content_dir() / "ui" / "gamedata" / "abilities.json").read_text())
    files = {name.lower(): info["file"] for name, info in abilities_doc["tables"].items()}   # the game: GreatswordAbilityTable
    by_file = {path.rsplit("/", 1)[-1]: path for path in source.paks().entries if path.startswith(TABLES) and path.endswith(".datasheet")}
    required = {}
    for row in datasheet.Sheet(source.read(POINTS)).rows():
        if row.get("ProgressionPointId"):
            required[str(row["ProgressionPointId"]).lower()] = str(row.get("RequiredProgressionPointId") or "")
    tables, problems = {}, []
    for name, index in TABLE_INDEX.items():
        if crc(name) != TABLE_CRC[name]:
            problems.append(f"{name}: the CRC-32 of the name is not the id of the protocol notes")
        path = by_file.get(files.get(name.lower(), ""))
        if path is None:
            problems.append(f"{name}: no ability table in the game copy")
            continue
        nodes = []
        for row in datasheet.Sheet(source.read(path)).rows():
            key = str(row.get("AbilityID") or "")
            if not key or row.get("TreeRowPosition") in (None, ""):
                continue
            need = required.get(key.lower(), "") or str(row.get("RequiredAbilityId") or "")
            nodes.append({"id": key, "crc": crc(key), "displayName": row.get("DisplayName") or "",
                          "description": row.get("Description") or "", "icon": row.get("Icon") or "",
                          "uiCategory": row.get("UICategory") or "", "sound": row.get("Sound") or "",
                          "tree": int(row.get("TreeId") or 0), "row": int(row.get("TreeRowPosition") or 0),
                          "column": int(row.get("TreeColumnPosition") or 0), "active": flag(row.get("IsActiveAbility")),
                          "unlockDefault": flag(row.get("UnlockDefault")), "required": crc(need) if need else 0,
                          "requiredId": need, "cooldownId": row.get("CooldownId") or "", "smartcast": flag(row.get("SupportsSmartcast"))})
        tables[name] = {"crc": TABLE_CRC[name], "index": index, "nodes": nodes}
    # A check against the backend: each node of the backend tree is a node here, at the same place.
    for table, key, tree, row, column, _active, _need in backend.get("abilities", []):
        here = {n["id"].lower(): n for n in tables.get(table, {}).get("nodes", [])}
        node = here.get(str(key).lower())
        if table in tables and (node is None or (node["tree"], node["row"], node["column"]) != (tree, row, column)):
            problems.append(f"{table} {key}: not at the place of the backend tree")
    # The largest rank: the rank with the expansion (the fifth value of the backend: each player owns the expansion, a decision of
    # the user in the backend; 250 for the trade skills), else the base rank.
    progressions = [{"id": p[0], "crc": crc(p[0]), "kind": p[1], "maxRank": (p[4] if len(p) > 4 and p[4] else p[2]), "baseMaxRank": p[2],
                     "rankTable": p[3]} for p in backend["progressions"]]
    doc = {"format": 1, "tables": tables, "rankTables": backend["rankTables"], "progressions": progressions}
    target = env.content_dir() / "ui" / "gamedata" / "skilltrees.json"
    target.write_text(json.dumps(doc, separators=(",", ":")))
    return {"tables": len(tables), "nodes": sum(len(t["nodes"]) for t in tables.values()), "progressions": len(progressions),
            "problems": problems, "file": str(target)}


def main(argv=None) -> int:
    print(json.dumps(write(), indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
