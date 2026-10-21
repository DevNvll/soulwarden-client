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

The recipe list: only the recipes with bKnownByDefault. The retail client with this backend lists only "Learned" recipes (the
Soulwarden evidence crafting/37a to 37c: the tree has the one root "Learned"); the backend has no learned recipe and refuses a
recipe with no bKnownByDefault (the cause "recipe unlock or achievement rule").

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


def rank_tables(progressions: dict[str, dict]) -> dict[str, list]:
    """@return The rows of the rank table of each trade skill, by the RankTableId of its progression."""
    wanted = {p["rankTable"].lower(): p["rankTable"] for p in progressions.values() if p["rankTable"]}
    out: dict[str, list] = {}
    for path in sorted(source.paks().paths(datasheet.TABLES_FOLDER, ".datasheet")):
        name = path.rsplit("/", 1)[-1]
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


def convert(out_dir: Path) -> dict:
    source_path = gamedata_dir() / "crafting-data.json"
    data = json.loads(source_path.read_text())
    ui_rows = recipe_rows()
    recipes: dict[str, dict] = {}
    for recipe in data["recipes"]:
        if not recipe.get("known"):
            continue
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
    target = out_dir / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "crafting.json").write_text(json.dumps(doc, separators=(",", ":"), sort_keys=True))
    supported = sum(1 for r in recipes.values() if not r["unsupported"])
    return {"crafting": str(target / "crafting.json"), "recipes": len(recipes), "supported": supported,
            "with a cause": len(recipes) - supported, "recipes with no UI row": sum(1 for k in recipes if k not in ui_rows),
            "categories": len(categories), "ingredient categories": len(doc["ingredientCategories"]), "rank tables": len(ranks)}
