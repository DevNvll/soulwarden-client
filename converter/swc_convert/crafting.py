"""The data of the crafting screens (stage MVP-J, Linear AST-226): the recipes, the crafting categories, the station rows, the
ingredient categories, the reward events, and the rank tables of the trade skills. The native crafting components of the game
(RecipeDataManagerBus, CraftingRequestBus, CraftingCategoryDataManagerBus) read the same tables; the UI runtime
(swc/crafting.lua) gives this data to the original scripts (craftingscreenv4.luac and its panels).

Sources:
- The backend copy: gamedata/crafting-data.json (the data of the server: Soulwarden tools/gamedata/crafting.py). Each recipe has
  its rules and its causes (`unsupported`: a rule that the backend does not have; the server refuses a request for such a recipe,
  docs/systems/crafting.md). The client shows such a recipe as unavailable with its cause (decision C3 of the orchestrator).
- The game copy (read only): the recipe tables (type CraftingRecipeData) for the columns of the UI that the backend file does
  not have (RecipeNameOverride, AdditionalFilterText, RecipeTags, DisplayIngredients, GearScoreBonus, GearScoreReduction,
  UnlockedAchievementBlocksRecrafting); javelindata_craftingcategories (the labels and icons of the categories);
  javelindata_tradeskill*.datasheet (the rank tables of the trade skills: RecipeLevel, MaximumInfluence, the gear score
  bonus columns); javelindata_categoricalprogression (the rank table and the display name of each progression).

The recipe list: all the recipes (AST-234, research recipe-known.md). The retail list has no default-known filter: the station
script (craftingtreeview.luac) hides a recipe whose RequiredAchievementID is not unlocked, or whose DisplayIngredients have no item
with a positive count in the inventory cache; IsRecipeKnown puts a shown recipe under Learned or Unlearned. The backend still
refuses a recipe with no bKnownByDefault (the cause "recipe unlock or achievement rule"): the screen shows that cause.

The achievement indexes (gamedata/achievements.json, AST-234): the AchievementIndex of each AchievementID of
javelindata_achievements (the bit of the Achievement state 3210 of the owner: bit n & 7 of byte n >> 3).

Output: SWC_CONTENT/ui/gamedata/crafting.json
  {"format": 1, "source", "recipes": {ID: {...}}, "categories": {ID: [image, text, description]}, "ingredientCategories":
   {ID: [ITEM...]}, "stations": [{name, types, discount, mannequin}], "events": {ID: {...}}, "progressions": {ID: {...}},
   "ranks": {RANK TABLE: [[RecipeLevel, MaximumInfluence, MinGearScoreBonus, MaxGearScoreBonus, ...T2..T5]...]},
   "currencies", "rankGearScore", "materials", "resources", "perkGeneration", "achievements" (the backend values)}
"""
from __future__ import annotations

import json
from pathlib import Path

from . import datasheet, source
from .actions import gamedata_dir

RECIPE_TYPE = "CraftingRecipeData"
CATEGORY_TABLE = "javelindata_craftingcategories.datasheet"
PROGRESSION_TABLE = "javelindata_categoricalprogression.datasheet"
RANK_TYPE = "TradeskillRankData"
# The columns of a rank row that the screens read (CategoricalProgressionRequestBus.GetStaticTradeskillRankData).
RANK_COLUMNS = ("Level", "RecipeLevel", "MaximumInfluence", "RollBonus", "MinGearScoreBonus", "MaxGearScoreBonus", "MinGearScoreBonusT2",
                "MaxGearScoreBonusT2", "MinGearScoreBonusT3", "MaxGearScoreBonusT3", "MinGearScoreBonusT4", "MaxGearScoreBonusT4",
                "MinGearScoreBonusT5", "MaxGearScoreBonusT5", "XpReward", "ResourceTierUnlocks", "InfluenceCost")
# The UI columns of a recipe row (the rules come from the backend file).
UI_COLUMNS = {"RecipeNameOverride": "nameOverride", "AdditionalFilterText": "filterText", "RecipeTags": "tags",
              "DisplayIngredients": "displayIngredients", "GearScoreBonus": "gearScoreBonus", "GearScoreReduction": "gearScoreReduction",
              "UnlockedAchievementBlocksRecrafting": "unlockBlocksRecrafting", "SkipGrantItemsDesc": "skipGrantDesc",
              "SkipGrantItemsTitle": "skipGrantTitle", "SkipGrantItemsBody": "skipGrantBody"}


def plain(value):
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


def recipe_rows() -> dict[str, dict]:
    """@return The UI columns of each recipe row of the game (by its id)."""
    rows: dict[str, dict] = {}
    for path in sorted(source.paks().paths(datasheet.TABLES_FOLDER, ".datasheet")):
        sheet = datasheet.Sheet(source.read(path))
        if sheet.type != RECIPE_TYPE:
            continue
        for row in sheet.rows():
            identifier = str(row.get("RecipeID", "")).strip()
            if identifier:
                rows[identifier] = {key: plain(row.get(column, "")) for column, key in UI_COLUMNS.items()}
    return rows


# The rank tables of other categorical progressions in the same columns (a column that the table does not have is 0): the territory
# standing (AST-242, Q7: javelindata_territory_standing, type CategoricalProgressionRankData; the map shows the points of the rank against
# its MaximumInfluence, 150 for rank 0).
OTHER_RANK_TABLES = {"Territory_Standing": "javelindata_territory_standing.datasheet",
                     # The factions (QG7, 2a of ut567; Soulwarden docs/systems/factions.md): the reputation (Syndicate MerchantGuild, Marauder
                     # ProcurerGuild, Covenant ExplorerGuild) and the tokens of each faction. The Bio tab and the poster show the points
                     # against MaximumInfluence of the rank (bio.luac UpdateWallet: GetMaxPointsForRank); with no table it showed 2000 / 2000.
                     "MerchantGuild": "javelindata_owg_merchant.datasheet",
                     "ProcurerGuild": "javelindata_owg_procurer.datasheet",
                     "ExplorerGuild": "javelindata_owg_explorer.datasheet",
                     "SyndicateTokens": "javelindata_owg_progression_syndicatetokens.datasheet",
                     "MarauderTokens": "javelindata_owg_progression_marauderstokens.datasheet",
                     "CovenantTokens": "javelindata_owg_progression_covenanttokens.datasheet"}


def rank_tables(progressions: dict[str, dict]) -> dict[str, list]:
    """@return The rows of the rank table of each trade skill, by the RankTableId of its progression, and the rows of OTHER_RANK_TABLES."""
    wanted = {p["rankTable"].lower(): p["rankTable"] for p in progressions.values() if p["rankTable"]}
    out: dict[str, list] = {}
    for path in sorted(source.paks().paths(datasheet.TABLES_FOLDER, ".datasheet")):
        name = path.rsplit("/", 1)[-1]
        other = next((table for table, file in OTHER_RANK_TABLES.items() if file == name), None)
        if other is not None and other.lower() in wanted:
            sheet = datasheet.Sheet(source.read(path))
            out[other] = [[plain(row.get(column, 0)) for column in RANK_COLUMNS] for row in sheet.rows()]
            continue
        if not name.startswith("javelindata_tradeskill"):
            continue
        sheet = datasheet.Sheet(source.read(path))
        if sheet.type != RANK_TYPE:
            continue
        key = name[len("javelindata_tradeskill"):-len(".datasheet")]
        table = next((v for k, v in wanted.items() if k == key), None)
        if table is None:
            continue
        out[table] = [[plain(row.get(column, 0)) for column in RANK_COLUMNS] for row in sheet.rows()]
    return out


def rank_names() -> dict[str, list]:
    """@return The DisplayName of each rank row of the tables of OTHER_RANK_TABLES (the title of a standing rank: territorydatahandler.luac
    GetTerritoryStanding reads GetRankData(...).displayName; rank 0 of Territory_Standing is @territory_standing_title_1)."""
    out: dict[str, list] = {}
    for path in sorted(source.paks().paths(datasheet.TABLES_FOLDER, ".datasheet")):
        name = path.rsplit("/", 1)[-1]
        table = next((t for t, file in OTHER_RANK_TABLES.items() if file == name), None)
        if table is not None:
            out[table] = [str(row.get("DisplayName") or "") for row in datasheet.Sheet(source.read(path)).rows()]
    return out


def craft_mods() -> dict:
    """The data of the craft mods (AST-231; research crafting-ui.md 2, client code 0x4436640): each resource with SocketType None and a
    PerkBucket (javelindata_itemdefinitions_resources) as [ResourceID, the first perk of its bucket (the display perk, code 0x4436C60),
    its leaf perks [PerkID, ItemClass, ExcludeItemClass, ExclusiveLabels]] (javelindata_perkbuckets: Perk1 ..., "[PBID]NAME" a nested
    bucket); and the ExclusiveLabels of each perk (the labels of the fixed perks of the output)."""
    import re
    perks: dict[str, dict] = {}
    buckets: dict[str, dict] = {}
    resources: list[dict] = []
    for path in sorted(source.paks().paths("", ".datasheet")):
        name = path.rsplit("/", 1)[1]
        if re.match(r"javelindata_perks.*\.datasheet$", name):
            for row in datasheet.Sheet(source.read(path)).rows():
                if row.get("PerkID"):
                    perks.setdefault(str(row["PerkID"]).lower(), row)
        elif re.match(r"javelindata_perkbuckets.*\.datasheet$", name):
            for row in datasheet.Sheet(source.read(path)).rows():
                if row.get("PerkBucketID"):
                    buckets.setdefault(str(row["PerkBucketID"]).lower(), row)
        elif re.match(r"javelindata_itemdefinitions_resources.*\.datasheet$", name):
            resources.extend(datasheet.Sheet(source.read(path)).rows())

    def leaves(bucket: str, chain: tuple = ()) -> list[dict]:
        bucket = bucket.lower()
        if bucket in chain:
            return []
        out = []
        for column, value in buckets.get(bucket, {}).items():
            if not column.startswith("Perk") or not column[4:].isdigit() or not isinstance(value, str) or not value:
                continue
            if value.startswith("[PBID]"):
                out += leaves(value[6:], chain + (bucket,))
            elif value.lower() in perks:
                out.append(perks[value.lower()])
        return out

    rows = []
    for row in resources:
        if row.get("SocketType") or not row.get("PerkBucket") or not row.get("ResourceID"):
            continue
        found = leaves(str(row["PerkBucket"]))
        if not found:
            continue
        rows.append([row["ResourceID"], found[0]["PerkID"],
                     [[p["PerkID"], p.get("ItemClass") or "", p.get("ExcludeItemClass") or "", p.get("ExclusiveLabels") or ""] for p in found]])
    labels = {key: row["ExclusiveLabels"] for key, row in perks.items() if row.get("ExclusiveLabels")}
    return {"resources": rows, "perkLabels": labels}


def trading_group_spellings(groups: set[str]) -> dict[str, str]:
    """@return The first spelling of each item TradingGroup text (javelindata_itemdefinitions_master_*, the order of their paths and
    rows) whose lower case is the lower case of a recipe group: lower case -> spelling (research forge-order.md 1, AST-234)."""
    wanted = {group.lower() for group in groups if group}
    out: dict[str, str] = {}
    for path in sorted(source.paks().paths("", ".datasheet")):
        if not path.rsplit("/", 1)[1].startswith("javelindata_itemdefinitions_master_"):
            continue
        for row in datasheet.Sheet(source.read(path)).rows():
            group = str(row.get("TradingGroup") or "").strip()
            if group and group.lower() in wanted and group.lower() not in out:
                out[group.lower()] = group
    return out


def convert(out_dir: Path) -> dict:
    source_path = gamedata_dir() / "crafting-data.json"
    data = json.loads(source_path.read_text())
    ui_rows = recipe_rows()
    recipes: dict[str, dict] = {}
    for recipe in data["recipes"]:
        entry = dict(recipe)
        entry.update(ui_rows.get(recipe["id"], {}))
        recipes[recipe["id"]] = entry
    categories = {}
    for row in datasheet.load(CATEGORY_TABLE).rows():
        identifier = str(row.get("CategoryID", "")).strip()
        if identifier:
            categories[identifier] = [row.get("ImagePath", ""), row.get("DisplayText", ""), row.get("Description", "")]
    progressions = {}
    for row in datasheet.load(PROGRESSION_TABLE).rows():
        identifier = str(row.get("CategoricalProgressionId", "")).strip()
        if identifier:
            progressions[identifier] = {"displayName": row.get("DisplayName", ""), "rankTable": row.get("RankTableId", ""),
                                        "maxLevel": plain(row.get("MaxLevel", 0)), "expansion01MaxLevel": plain(row.get("Expansion01MaxLevel", 0)),
                                        "type": row.get("ProgressionType", ""), "icon": row.get("IconPath", ""),
                                        "postCap": row.get("PostSkillCapSkill", ""), "preCap": row.get("PreSkillCapSkill", "")}
    ranks = rank_tables(progressions)
    doc = {"format": 1, "source": str(source_path), "rankColumns": list(RANK_COLUMNS), "recipes": recipes, "categories": categories,
           # All categories: craftinginventorycache.luac caches the categories of each bag item (ItemDataManagerBus
           # GetIngredientCategories) in the lists of GetIngredientCategoryIds and expects each of them there.
           "ingredientCategories": data["categories"],
           "stations": data["stations"], "missingStations": data["missingStations"], "events": data["events"],
           "progressions": progressions, "ranks": ranks}
    for key in ("currencies", "rankGearScore", "materials", "resources", "perkGeneration", "achievements"):
        doc[key] = data.get(key, {})
    # The perk buckets of the items (the backend copy perk-data.json `buckets`: the type and the chance of each bucket): the rows of the
    # perks of a procedural result (ItemDataManagerBus GetPerkBucketType, GetPerkChance).
    perks = json.loads((gamedata_dir() / "perk-data.json").read_text())
    doc["perkBuckets"] = {name: [bucket[0], bucket[1]] for name, bucket in perks.get("buckets", {}).items()}
    target = out_dir / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    doc["craftMods"] = craft_mods()
    doc["rankNames"] = rank_names()
    # GetCraftingCategoryData returns the group text from the shared string table of the client: a table of the CRC of the lower case
    # that keeps the first spelling (research forge-order.md 1). The TradingGroup texts of the items are in it before the recipe
    # groups [inference: the order of the first registrations of a retail start is not proven; it explains the retail forge order
    # Greatswords before GreatAxes].
    doc["groupSpellings"] = trading_group_spellings({str(r.get("group") or "") for r in recipes.values()})
    (target / "crafting.json").write_text(json.dumps(doc, separators=(",", ":"), sort_keys=True))
    achievements = {}
    for row in datasheet.load("javelindata_achievements.datasheet").rows():
        identifier = str(row.get("AchievementID") or "").strip()
        if identifier and isinstance(row.get("AchievementIndex"), (int, float)):
            achievements[identifier.lower()] = int(row["AchievementIndex"])
    (target / "achievements.json").write_text(json.dumps(achievements, separators=(",", ":"), sort_keys=True))
    supported = sum(1 for r in recipes.values() if not r["unsupported"])
    return {"crafting": str(target / "crafting.json"), "recipes": len(recipes), "supported": supported,
            "with a cause": len(recipes) - supported, "recipes with no UI row": sum(1 for k in recipes if k not in ui_rows),
            "achievement indexes": len(achievements),
            "group spellings of the items that differ": sorted(v for k, v in doc["groupSpellings"].items()
                                                              if v not in {str(r.get("group") or "") for r in recipes.values()}), "categories": len(categories), "ingredient categories": len(doc["ingredientCategories"]), "rank tables": len(ranks)}
