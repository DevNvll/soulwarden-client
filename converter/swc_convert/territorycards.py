"""The data of the territory standing cards (QG1, step 3 of ut567; protocol notes 4.59; Soulwarden docs/systems/progression-and-skills.md
"The territory pools and the tokens", "The card choice", "The card values"), for swc/territory.lua (the native ProgressionPointRequestBus
methods of the territory pools that territorydatahandler.luac, territorybonuspopup.luac, and the map standing list read).

Sources:
- the backend copy (SWC_GAMEDATA_DIR or SWC_WORK/backend/current/gamedata/progression-tables.json, format 4): `standings` (the territory,
  the largest rank, the rank table, the pool, AutoRankUp, the experience modifier), `territoryCards` (TerritoryBonusCategory, the Territory
  Bonus Id, MaxLevel, DoNotSpendPoint, RequiredCategoricalProgressionLevel, Initial Bonus, Reduction Modifier, Min Value), and the cards of
  the rank table Territory_Standing (AvailableProgressionPoint1 to 4 of each rank row): the same rules as the backend;
- javelindata_progressionpointdata.datasheet: the UI columns of each card (UpgradeCardDescription, UpgradeCardSprite, UpgradeCardIcon,
  UpgradeCardCategory, UpgradeCardStat) and a check of the backend columns.

Output: SWC_CONTENT/ui/gamedata/territory.json (outside git).
"""
from __future__ import annotations

import json
import zlib

from . import creature, datasheet, env, source

POINTS = "sharedassets/springboardentitites/datatables/javelindata_progressionpointdata.datasheet"
RANK_TABLE = "Territory_Standing"


def crc(text: str) -> int:
    return zlib.crc32(str(text).lower().encode()) & 0xFFFFFFFF


def flag(value) -> bool:
    return value is True or str(value).strip().lower() in ("true", "1")


def build(backend: dict, rows: list[dict]) -> tuple[dict, list[str]]:
    """@return The document and the problems (a card with no row of the point table, a column that differs from the backend)."""
    problems = []
    by_id = {str(r.get("ProgressionPointId") or "").lower(): r for r in rows if r.get("ProgressionPointId")}
    cards = {}
    for ident, category, bonus, max_level, no_spend, required, initial, reduction, minimum in backend.get("territoryCards", []):
        row = by_id.get(str(ident).lower())
        if row is None:
            problems.append(f"{ident}: no row of ProgressionPointData")
            row = {}
        elif str(row.get("TerritoryBonusCategory") or "") != category or int(float(row.get("MaxLevel") or 0)) != max_level \
                or flag(row.get("DoNotSpendPoint")) != bool(no_spend) \
                or int(float(row.get("RequiredCategoricalProgressionLevel") or 0)) != required:
            problems.append(f"{ident}: a column differs from the backend copy")
        cards[str(crc(ident))] = {
            "id": ident, "crc": crc(ident), "category": category, "bonusId": bonus, "maxLevel": max_level, "doNotSpend": bool(no_spend),
            "requiredRank": required, "initialBonus": initial, "reduction": reduction, "minValue": minimum,
            "description": row.get("UpgradeCardDescription") or "", "sprite": row.get("UpgradeCardSprite") or "",
            "icon": row.get("UpgradeCardIcon") or "", "cardCategory": row.get("UpgradeCardCategory") or "",
            "stat": row.get("UpgradeCardStat") or ""}
    table = backend.get("rankTables", {}).get(RANK_TABLE, {})
    rank_cards = []
    for index, offers in enumerate(table.get("cards", [])):
        ids = [crc(card) for card in offers if card]
        for card in offers:
            if card and str(crc(card)) not in cards:
                problems.append(f"the rank row {index} names {card}, not a territory card")
        rank_cards.append(ids)
    standings = [{"territory": str(t), "crc": crc(str(t)), "maxRank": max_rank, "rankTable": rank_table, "pool": pool, "poolCrc": crc(pool),
                  "autoRankUp": bool(auto)} for t, max_rank, rank_table, pool, auto, *_ in backend.get("standings", [])]
    if not standings:
        problems.append("the backend copy has no standings (a file before format 3)")
    if not rank_cards:
        problems.append(f"the rank table {RANK_TABLE} has no cards (a file before QG1)")
    return {"format": 1, "standings": standings, "rankCards": rank_cards, "cards": cards}, problems


def write() -> dict:
    backend = json.loads((creature.gamedata_dir() / "progression-tables.json").read_text())
    rows = list(datasheet.Sheet(source.read(POINTS)).rows())
    doc, problems = build(backend, rows)
    target = env.content_dir() / "ui" / "gamedata" / "territory.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(doc, separators=(",", ":")))
    return {"standings": len(doc["standings"]), "rankRows": len(doc["rankCards"]), "cards": len(doc["cards"]), "problems": problems,
            "file": str(target)}


def main(argv=None) -> int:
    print(json.dumps(write(), indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
