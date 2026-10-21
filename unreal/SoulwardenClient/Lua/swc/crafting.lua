-- The crafting (stage MVP-J, Linear AST-226; docs/CRAFTING.md): the native part of the crafting screens of the game. The original
-- scripts (lyshineui/crafting/craftingscreenv4.luac and its panels, lyshineui/stationproperties/stationproperties.luac) read the
-- recipes and the station of the use through RecipeDataManagerBus, CraftingRequestBus, and CraftingCategoryDataManagerBus.
--
-- The data: gamedata/crafting.json (tools/convert-ui.sh crafting; converter swc_convert/crafting.py): the recipes with
-- bKnownByDefault from crafting-data.json of the backend copy (the data of the server, with the causes of the backend) and the UI
-- columns of the recipe tables, the crafting categories, the station rows, the ingredient categories, the reward events, and the
-- rank tables of the trade skills.
--
-- The host (USwcUiSubsystem) gives the station of an accepted use (Swc.HostStation) and takes the requests of the screen
-- (Swc.TakeHostRequest: "craft ...", "craft stop", "station end").

Swc.crafting = Swc.crafting or {}
local C = Swc.crafting

-- The objectives of the client are on in retail: the crafting screen shows its button "Pin Recipe" (craftingstatspanel.luac
-- CheckPinButton reads javelin.enable-objectives; the Soulwarden evidence crafting/37a I3-02, 37b I1-01, 37c I2-01). The backend has no
-- objectives: a pin adds nothing (ObjectivesComponentRequestBus is a stub).
Swc.config["javelin.enable-objectives"] = true
-- The crafting settings of the client (their registration with the defaults: research scratch/melee-mode/raw-all-defaults.txt, the
-- addresses): the whole count in one request (crafting-enable-craft-all-override true, 0x617139E; Soulwarden live check L10b), 4 perk item
-- controls (0x6171372), the first-craft bonus x3 (0x6171316, 0x617133F), 10,000 jobs (max-crafting-jobs 0x2710, 0x6171240), no skip of the
-- craft roll (0x616F6B2), the post-cap trade skills (0x616F593).
Swc.config["javelin.crafting-enable-craft-all-override"] = true
Swc.config["javelin.crafting-max-num-perk-items"] = 4
Swc.config["javelin.crafting-enable-first-craft-bonus"] = true
Swc.config["javelin.crafting-first-craft-bonus-exp-multiplier"] = 3.0
Swc.config["javelin.max-crafting-jobs"] = 10000
Swc.config["javelin.always-skip-craft-roll"] = false
Swc.config["javelin.enable-post-cap-trade-skills"] = true

local data
--- @return The crafting data (loaded at the first use: about 4 MB).
local function D()
  if not data then
    data = swcui.load_json("gamedata/crafting.json") or {recipes = {}, categories = {}, ingredientCategories = {}, stations = {},
                                                         events = {}, progressions = {}, ranks = {}, rankColumns = {}}
    data.byCrc = {}
    data.order = {}
    for id, recipe in pairs(data.recipes) do
      data.byCrc[recipe.crc] = recipe
      data.order[#data.order + 1] = id
    end
    table.sort(data.order)
    data.stationRows = {}
    for _, row in ipairs(data.stations or {}) do
      data.stationRows[string.lower(row.name)] = row
    end
    data.progressionsByCrc = {}
    for id, row in pairs(data.progressions or {}) do
      row.id = id
      data.progressionsByCrc[swcui.crc32(string.lower(id))] = row
    end
    data.eventsByCrc = {}
    for id, row in pairs(data.events or {}) do
      row.id = id
      data.eventsByCrc[swcui.crc32(string.lower(id))] = row
    end
  end
  return data
end
C.Data = D

--- @return The recipe of an id: a recipe id text, its CRC-32 (of the lower-case id), or a RecipeData object.
local function recipe_of(id)
  if id == nil then
    return nil
  end
  local d = D()
  if type(id) == "number" then
    return d.byCrc[id]
  end
  if type(id) == "string" then
    return d.recipes[id] or d.byCrc[swcui.crc32(string.lower(id))]
  end
  local ok, key = pcall(function() return id.id end)
  if ok and key ~= nil and key ~= id then
    return recipe_of(key)
  end
  return nil
end
C.Recipe = recipe_of

-- ----------------------------------------------------------------------------------------------------------- the station

--- The station of the use: its Assembly key (m_craftingStationEntry: "Blacksmith5") and the fee flag (m_chargeCraftingFee); nil
--- with no use.
C.station = C.station

local CRAFTING_STATE = 3024636726   -- the UI state CraftingStation (uistates.json: CraftingScreenV4, StationProperties)

--- The host (UI/SwcUiCrafting): the station use whose screen shows ("open", KEY, FEE, NAME: the Assembly key, the fee flag, the name
--- key "@BlacksmithT5_MasterName") or its end ("close": a refusal, 3746, a death, a loading screen). The open sets the state
--- CraftingStation and Hud.LocalPlayer.InteractName (stationproperties.luac: the header "FORGE TIER 5" and the trade skills of the
--- station); the close leaves the state.
-- The canvas: the retail dynamic canvas loader loads it at the first station (dynamiccanvasloader.luac canvasInfos: CraftingScreenV4,
-- LyShineUI/Crafting/CraftingScreenV4); that loader does not run in the client, so this file loads it in the same way (the draw order
-- of the canvas file). StationProperties is a screen of the UI loader (it loads at the start).
local PATH = "LyShineUI/Crafting/CraftingScreenV4.uicanvas"
local function ensure_canvas()
  if #DynamicBus.CraftingScreenBus.handlers > 0 then
    return   -- the screen is loaded (its OnInit connects DynamicBus.CraftingScreenBus)
  end
  Swc.LoadScreen(PATH, true)
  Swc.Note("crafting: the screen CraftingScreenV4 loads")
end

function Swc.HostStation(kind, key, fee, name)
  if kind == "open" then
    ensure_canvas()
    C.station = {key = key or "", fee = fee and true or false, name = name or ""}
    Swc.Note("crafting: the station " .. tostring(key) .. " " .. tostring(name) .. (fee and " (fee)" or ""))
    Swc.SetData("Hud.LocalPlayer.InteractName", name or "")
    LyShineManagerBus.Broadcast.SetState(CRAFTING_STATE)
  elseif C.station then
    C.station = nil
    Swc.Note("crafting: the client ended the station use: the screen closes")
    if LyShineManagerBus.Broadcast.GetCurrentState() == CRAFTING_STATE then
      LyShineManagerBus.Broadcast.ExitState(CRAFTING_STATE)
    end
  end
end

--- The end of the screen by the player (Escape, the back button: craftingscreenv4.luac OnExit leaves the state; its OnTransitionOut
--- calls OnCraftingScreenHidden): one host request "station end" (the client sends 2473).
local function screen_hidden()
  if C.station then
    C.station = nil
    Swc.hostRequests[#Swc.hostRequests + 1] = "station end"
    Swc.Note("crafting: the screen closes: the station use ends")
  end
end

--- @return The station row of an Assembly key or a station type ("Blacksmith5", "blacksmith2"), or nil.
local function station_row(key)
  return key and D().stationRows[string.lower(key)]
end
C.StationRow = station_row

--- @return True when the station of the use permits a station type of the recipe (gamedata::CraftingData::permits of the backend:
--- the CraftingTypes of the station row has a station key of the recipe).
local function station_permits(recipe)
  local row = C.station and station_row(C.station.key)
  if not row or not recipe then
    return false
  end
  for _, type in ipairs(row.types or {}) do
    for _, wanted in ipairs(recipe.stations or {}) do
      if string.lower(type) == string.lower(wanted) then
        return true
      end
    end
  end
  return false
end
C.StationPermits = station_permits

-- --------------------------------------------------------------------------------------------------------- the recipes

--- @return The CRC-32 of the lower-case text of an id (the item, category, and progression ids of the native data).
local function crc(text)
  return swcui.crc32(string.lower(text or ""))
end
C.Crc = crc

-- The types of an ingredient (enums.json: eIngredientType_Item 1, eIngredientType_Currency 2, eIngredientType_CategoryOnly 4).
local INGREDIENT_TYPES = {Item = eIngredientType_Item or 1, Currency = eIngredientType_Currency or 2,
                          Category_Only = eIngredientType_CategoryOnly or 4}

--- @return The ingredient entries of a recipe (IngredientEntry of the client: ingredientId, the CRC-32 of the item, category, or
--- currency id; quantity; type, an eIngredientType value). craftinginventorycache.luac compares the ids with the item ids of the
--- bag slots, the keys of GetIngredientCategoryIds, and the progression ids of CategoricalProgressionRequestBus.
local ENTRY = {
  __index = {
    -- The descriptor of an item entry (its item and quantity); a category or currency entry has no item (IsValid false).
    CreateItemDescriptor = function(self)
      local item = self.type == INGREDIENT_TYPES.Item and self.ingredientId or 0
      return Swc.ItemDescriptor.new({itemId = item, quantity = self.quantity or 0, gearScore = 0, perks = {}})
    end,
  },
}
local function entry(values)
  return setmetatable(values, ENTRY)
end

local function ingredients_of(recipe)
  local out = {}
  for _, ingredient in ipairs(recipe.ingredients or {}) do
    out[#out + 1] = entry({ingredientId = crc(ingredient[2]), quantity = ingredient[3], type = INGREDIENT_TYPES[ingredient[1]] or 0,
                           key = ingredient[2]})
  end
  return out
end

-- The native classes that the crafting scripts make: an ingredient entry (craftinginventorycache.luac sets ingredientId and type,
-- then counts it with ContainerRequestBus GetIngredientCount) and an item descriptor (ingredientrowv4.luac).
IngredientEntry = setmetatable({}, {__call = function() return entry({ingredientId = 0, quantity = 0, type = INGREDIENT_TYPES.Item}) end})
ItemDescriptor = setmetatable({}, {
  __call = function() return Swc.ItemDescriptor.new({itemId = 0, quantity = 0, gearScore = 0, perks = {}}) end,
})

--- The recipe data of the client (CraftingRecipeData): the fields that the scripts read, and a stub line for each other field.
C.RecipeData = Swc.ValueClass({
  name = "CraftingRecipeData",
  fields = {"id", "name", "listedByDefault", "knownByDefault", "numStationTypes", "resultItemId", "gameEventId", "baseGearScore",
            "baseTier", "recipeLevel", "tradeskill", "requiredAchievement", "craftAll", "isRefining", "isProcedural", "outputQty",
            "craftingFee", "useCraftingTax", "perkCost", "maxPerkItemsAllowed", "attributeOnlyPerkItemSlotCount",
            "perkItemsBucketPush", "cooldownSeconds", "cooldownQuantity", "requiredExpansion", "craftingCategory", "craftingGroup",
            "firstCraftAchievementId", "disallowBonusesToGS", "isTemporary", "ingredients"},
  methods = {
    IsValid = function(self) return self.id ~= nil and self.id ~= "" end,
    -- The units of the ingredients that are not a currency (client 0x5C47AC0; the backend Crafting::experience_of: the base of the
    -- trade skill points of one job is the reward of the event times these units).
    GetIngredientCount = function(self)
      local units = 0
      for _, entry in ipairs(self.ingredients or {}) do
        units = units + (entry.type ~= INGREDIENT_TYPES.Currency and (entry.quantity or 0) or 0)
      end
      return units
    end,
    GetIngredients = function(self) return self.ingredients or {} end,
    GetHiddenByAchievementId = function(self)
      local recipe = recipe_of(self.id)
      return recipe and recipe.hiddenByAchievement ~= "" and swcui.crc32(string.lower(recipe.hiddenByAchievement)) or 0
    end,
    -- The expansion of the recipe (RequiredExpansion: an eExpansionId value; none: eExpansionId_None).
    GetExpansionRequirement = function(self)
      local recipe = recipe_of(self.id)
      local name = recipe and recipe.expansion or ""
      return name ~= "" and (_G["eExpansionId_" .. name] or 1) or (eExpansionId_None or 0)
    end,
    -- UnlockedAchievementBlocksRecrafting and UnlockedAchievementID (the backend has no unlock rule: such a recipe has a cause).
    GetUnlockedAchievementBlocksRecrafting = function(self)
      local recipe = recipe_of(self.id)
      local value = recipe and tostring(recipe.unlockBlocksRecrafting or "") or ""
      return value == "true" or value == "TRUE" or value == "1"
    end,
    GetUnlockedAchievementId = function(self)
      local recipe = recipe_of(self.id)
      return recipe and recipe.unlockedAchievement ~= "" and swcui.crc32(string.lower(recipe.unlockedAchievement)) or 0
    end,
    GetSkipGrantItemsDescription = function(self)
      local recipe = recipe_of(self.id)
      return recipe and recipe.skipGrantDesc or ""
    end,
    GetSkipGrantItemsTitle = function(self)
      local recipe = recipe_of(self.id)
      return recipe and recipe.skipGrantTitle or ""
    end,
    GetSkipGrantItemsBody = function(self)
      local recipe = recipe_of(self.id)
      return recipe and recipe.skipGrantBody or ""
    end,
    GetSkipGrantItems = function(self)
      local recipe = recipe_of(self.id)
      return recipe ~= nil and recipe.skipGrant == true
    end,
  },
})

local cache = {}
--- @return The RecipeData object of a recipe (one object for each recipe).
local function recipe_data(recipe)
  if not recipe then
    return C.RecipeData.new({id = "", ingredients = {}})
  end
  local object = cache[recipe.id]
  if object then
    return object
  end
  -- The name: RecipeNameOverride, else the name of the output item (the rows of the tree and the search read recipeData.name; an empty
  -- name gave rows with no text and no search match).
  local output = Swc.ItemStatic(crc(recipe.item or ""))
  local name = recipe.nameOverride ~= "" and recipe.nameOverride or (output and output.name) or recipe.id
  object = C.RecipeData.new({
    id = recipe.id, name = name, listedByDefault = recipe.listed, knownByDefault = recipe.known,
    numStationTypes = #(recipe.stations or {}), resultItemId = recipe.item or "", gameEventId = recipe.event or "",
    baseGearScore = recipe.baseGearScore or 0, baseTier = recipe.baseTier or 0, recipeLevel = recipe.level or 0,
    tradeskill = recipe.skill or "", requiredAchievement = recipe.requiredAchievement ~= "" and swcui.crc32(string.lower(recipe.requiredAchievement)) or 0,
    craftAll = recipe.craftAll, isRefining = recipe.refining, isProcedural = recipe.procedural, outputQty = recipe.outputQty or 0,
    craftingFee = recipe.fee or 0, useCraftingTax = recipe.tax, perkCost = recipe.perkCost or 0, maxPerkItemsAllowed = recipe.perkItems or 0,
    attributeOnlyPerkItemSlotCount = recipe.attributeSlots or 0, perkItemsBucketPush = recipe.push,
    cooldownSeconds = recipe.cooldownSeconds or 0, cooldownQuantity = recipe.cooldownQuantity or 0,
    requiredExpansion = recipe.expansion or "", craftingCategory = recipe.category or "", craftingGroup = recipe.group or "",
    firstCraftAchievementId = recipe.firstCraftAchievement ~= "" and swcui.crc32(string.lower(recipe.firstCraftAchievement)) or 0,
    disallowBonusesToGS = recipe.disallowBonuses, isTemporary = recipe.temporary, ingredients = ingredients_of(recipe),
  })
  cache[recipe.id] = object
  return object
end
C.RecipeDataOf = recipe_data

Swc.Define("RecipeDataManagerBus", "Broadcast", {
  -- The recipes of the client: the recipes with bKnownByDefault (the converter keeps only these: the backend has no learned recipe; the
  -- retail lists of MVP-J showed no pattern recipe either). The native enumeration (research crafting-ui.md 1, code 0x4488B00): a recipe
  -- with RequiredWorldTags is in the list only when one of its tags is a tag of the world (javelin.world-tags, code 0x5C66960). This
  -- world has no tag: the retail woodshop with this backend lists no "Ethereal Dust Refining" group (Soulwarden evidence crafting/37a
  -- D1-02). No recipe has a ScheduleId in this build (the schedule test).
  GetRecipes = function()
    local out = {}
    local d = D()
    local world = {}
    for tag in string.gmatch(string.lower(tostring(Swc.config["javelin.world-tags"] or "")), "[^%s,+]+") do
      world[tag] = true
    end
    for _, id in ipairs(d.order) do
      local tags = d.recipes[id].worldTags or ""
      local listed = tags == ""
      for tag in string.gmatch(string.lower(tags), "[^%s,+]+") do
        listed = listed or world[tag] == true
      end
      if listed then
        out[#out + 1] = id
      end
    end
    return out
  end,
  GetCraftingRecipeData = function(id) return recipe_data(recipe_of(id)) end,
  GetStationsFromCraftingRecipe = function(id)
    local recipe = recipe_of(id)
    local out = {}
    for _, station in ipairs(recipe and recipe.stations or {}) do
      out[#out + 1] = station
    end
    return out
  end,
  GetCraftingCategoryData = function(id)
    local recipe = recipe_of(id)
    return {craftingCategory = recipe and recipe.category or "", craftingGroup = recipe and recipe.group or ""}
  end,
  IsRecipeProcedural = function(id)
    local recipe = recipe_of(id)
    return recipe ~= nil and recipe.procedural == true
  end,
  GetRecipeDisplayIngredients = function(id)
    local recipe = recipe_of(id)
    local out = {}
    for item in string.gmatch(recipe and recipe.displayIngredients or "", "[^,%s]+") do
      out[#out + 1] = item
    end
    return out
  end,
  GetAdditionalFilterText = function(id)
    local recipe = recipe_of(id)
    return recipe and recipe.filterText or ""
  end,
  GetRecipeFirstCraftAchievementId = function(id)
    local recipe = recipe_of(id)
    return recipe and recipe.firstCraftAchievement ~= "" and swcui.crc32(string.lower(recipe.firstCraftAchievement)) or 0
  end,
  GetMaxPerkItemsAllowed = function(id)
    local recipe = recipe_of(id)
    return recipe and recipe.perkItems or 0
  end,
  GetHasAttributeOnlyPerkItem = function(id)
    local recipe = recipe_of(id)
    return recipe ~= nil and (recipe.attributeSlots or 0) > 0
  end,
  -- The ingredient categories of the recipes (the IngredientCategories of the master items; crafting-data.json `categories`): the
  -- CRC-32 of each category id, and the item id CRCs of its members.
  GetIngredientCategoryIds = function()
    local out = {}
    for id in pairs(D().ingredientCategories or {}) do
      out[#out + 1] = crc(id)
    end
    table.sort(out)
    return out
  end,
  GetIngredientCategoryItemIds = function(category)
    local d = D()
    if not d.categoryItems then
      d.categoryItems = {}
      for id, items in pairs(d.ingredientCategories or {}) do
        local list = {}
        for _, item in ipairs(items) do
          list[#list + 1] = crc(item)
        end
        d.categoryItems[crc(id)] = list
      end
    end
    local key = type(category) == "number" and category or crc(tostring(category or ""))
    local out = {}
    for _, item in ipairs(d.categoryItems[key] or {}) do
      out[#out + 1] = item
    end
    return out
  end,
})

-- The labels and icons of the crafting categories (javelindata_craftingcategories: CategoryID, ImagePath, DisplayText, Description).
local function category_data(id)
  local row = D().categories[tostring(id or "")]
  return {categoryId = id or "", imagePath = row and row[1] or "", displayText = row and row[2] or "", description = row and row[3] or "",
          IsValid = function() return row ~= nil end}
end
Swc.Define("CraftingCategoryDataManagerBus", "Broadcast", {
  GetCategoryData = category_data,
  GetCategoryDataById = function(id)
    if type(id) == "number" then
      for key in pairs(D().categories) do
        if swcui.crc32(string.lower(key)) == id then
          return category_data(key)
        end
      end
    end
    return category_data(id)
  end,
})

-- ------------------------------------------------------------------------------------------- the trade skills and events

--- @return The rank table rows of a trade skill (the columns of crafting.json `rankColumns`).
local function rank_rows(skill)
  local d = D()
  local progression = d.progressions[skill or ""]
  return progression and d.ranks[progression.rankTable] or {}
end
local function rank_column(name)
  for index, column in ipairs(D().rankColumns or {}) do
    if column == name then
      return index
    end
  end
  return nil
end

--- @return The lowest rank of a trade skill whose RecipeLevel is the recipe level or more (the rank that the recipe needs).
function C.RankForRecipeLevel(skill, level)
  local column = rank_column("RecipeLevel")
  for _, row in ipairs(rank_rows(skill)) do
    if column and (row[column] or 0) >= (level or 0) then
      return row[1] or 0
    end
  end
  return level or 0
end

--- @return The rank row of a trade skill (CategoricalProgressionRequestBus GetStaticTradeskillRankData: the columns of the rank table in
--- the lower camel case of the native class, with resourceTierUnlock).
local function rank_data(skillCrc, rank)
  local d = D()
  local progression = d.progressionsByCrc[skillCrc or 0]
  local rows = progression and d.ranks[progression.rankTable] or {}
  local row = rows[(rank or 0) + 1]
  local out = {isValid = row ~= nil}
  for index, column in ipairs(d.rankColumns or {}) do
    local name = string.lower(column:sub(1, 1)) .. column:sub(2)
    out[name] = row and row[index] or 0
  end
  out.resourceTierUnlock = out.resourceTierUnlocks
  return out
end
C.RankData = rank_data

--- @return The data of a progression (javelindata_categoricalprogression): displayName, the rank table, the levels, the icon.
local function progression_data(id)
  local d = D()
  local row = type(id) == "number" and d.progressionsByCrc[id] or d.progressions[tostring(id or "")]
  -- maxRank: Expansion01MaxLevel with the owned expansion, else MaxLevel; postSkillCapSkill: the progression id of the post-cap skill.
  local maxRank = row and row.maxLevel or 0
  if row and (row.expansion01MaxLevel or 0) > 0 and Swc.OwnsExpansion and Swc.OwnsExpansion() then
    maxRank = row.expansion01MaxLevel
  end
  return {isValid = row ~= nil, categoricalProgressionId = row and crc(row.id) or 0, displayName = row and row.displayName or "",
          maxRank = maxRank, postSkillCapSkill = row and row.postCap ~= "" and crc(row.postCap) or 0,
          maxLevel = row and row.maxLevel or 0, rankTableId = row and crc(row.rankTable) or 0, iconPath = row and row.icon or "",
          expansion01MaxLevel = row and row.expansion01MaxLevel or 0}
end
C.ProgressionData = progression_data

Swc.Define("CategoricalProgressionRequestBus", "Event", {
  GetStaticTradeskillRankData = function(_, skillCrc, rank) return rank_data(skillCrc, rank) end,
  GetCategoricalProgressionData = function(_, id) return progression_data(id) end,
  -- The points of a rank (the MaximumInfluence of its row: the points to the next rank).
  GetMaxPointsForRank = function(_, id, rank) return rank_data(type(id) == "number" and id or crc(tostring(id)), rank).maximumInfluence end,
  GetMaxPointsForCurrentRank = function(_, id)
    local key = type(id) == "number" and id or crc(tostring(id))
    return rank_data(key, CategoricalProgressionRequestBus.Event.GetRank(nil, key)).maximumInfluence
  end,
  -- No boost of the trade skill points (the backend has no boost: crafting.md, the experience of a craft).
  GetTotalProgressionBoost = function() return 0 end,
})

-- The faction control buffs of a territory: the backend has no territory (0).
Swc.Define("FactionControlBuffRequestBus", "Event", {GetFactionControlBuffValue = function() return 0 end})

Swc.Define("GameEventRequestBus", "Broadcast", {
  -- The reward event of a recipe (GameEventData): the trade skill points (CategoricalProgressionReward).
  GetGameSystemData = function(crc)
    local event = D().eventsByCrc[crc or 0]
    return {isValid = event ~= nil, categoricalProgressionReward = event and event.reward or 0,
            categoricalProgressionId = event and swcui.crc32(string.lower(event.progression or "")) or 0,
            universalExpAmount = event and event.xp or 0, territoryStanding = event and event.standing or 0}
  end,
})

Swc.Define("CraftingRequestBus", "Broadcast", {
  -- The Assembly key of the station of the use ("Blacksmith5"); "" with no use.
  GetCraftingStationName = function() return C.station and C.station.key or "" end,
  -- The trade skill of a recipe; "None" with no trade skill (craftingstatspanel.luac UpdateRequirements tests the value "None").
  GetRecipeTradeskill = function(id)
    local recipe = recipe_of(id)
    return recipe and recipe.skill ~= "" and recipe.skill or "None"
  end,
  GetRequiredRecipeLevel = function(id)
    local recipe = recipe_of(id)
    return recipe and recipe.level or 0
  end,
  GetRecipeTradeskillLevel = function(id)
    local recipe = recipe_of(id)
    return recipe and C.RankForRecipeLevel(recipe.skill, recipe.level) or 0
  end,
  GetTradeskillLevelRequiredForRecipeLevel = function(skill, level) return C.RankForRecipeLevel(skill, level) end,
  -- A recipe with a RequiredExpansion: unlocked when the player owns the expansion (the backend gives each player Expansion2023; the
  -- tree lists only unlocked recipes). The backend refuses it (CraftVerdict::expansion): the refusal row of decision C3 shows that.
  IsRecipeExpansionUnlocked = function(id)
    local recipe = recipe_of(id)
    if not recipe then
      return false
    end
    if (recipe.expansion or "") == "" then
      return true
    end
    return PlayerComponentRequestsBus.Event.IsExpansionOwnerScript(nil, recipe.expansion) == true
  end,
  HasCooldown = function(id)
    local recipe = recipe_of(id)
    return recipe ~= nil and (recipe.cooldownQuantity or 0) > 0
  end,
  -- The native rule (research crafting-ui.md 1, code 0x2E5E720): a recipe with RequiredAchievementID needs that achievement; with the
  -- second argument true the call returns the achievement result at once (the tree of the screen asks it so for Learned/Unlearned).
  -- Then a recipe of the list javelin.red-button-disable-crafting-recipe-list is not known; a recipe with no trade skill is known; else
  -- the recipe level of the rank of the trade skill must reach the RecipeLevel (the retail woodshop at Woodworking 0 lists Lumber,
  -- level 50, under "Unlearned": Soulwarden evidence crafting/37a D1-02). bKnownByDefault is not read.
  IsRecipeKnown = function(id, achievementOnly)
    local recipe = recipe_of(id)
    if not recipe then
      return false
    end
    if (recipe.requiredAchievement or "") ~= "" then
      local unlocked = Swc.HasAchievement((D().achievements or {})[recipe.requiredAchievement])
      if achievementOnly then
        return unlocked
      end
      if not unlocked then
        return false
      end
    end
    for disabled in string.gmatch(tostring(Swc.config["javelin.red-button-disable-crafting-recipe-list"] or ""), "[^%s,]+") do
      if string.lower(disabled) == string.lower(recipe.id) then
        return false
      end
    end
    if (recipe.skill or "") == "" then
      return true
    end
    return CategoricalProgressionRequestBus.Event.GetRank(nil, crc(recipe.skill)) >= C.RankForRecipeLevel(recipe.skill, recipe.level or 0)
  end,
  -- UnlockedAchievementBlocksRecrafting: the backend has no unlock rule (a recipe with an achievement has a cause of the data).
  IsRecipeBlockedByAchievement = function() return false end,
  -- The discounts of the fee (the taxes and the buffs of a territory, a company): the backend has none.
  GetCraftingDiscountList = function() return {} end,
  IsValidRecipe = function(id) return recipe_of(id) ~= nil end,
  IsRecipeRefining = function(id)
    local recipe = recipe_of(id)
    return recipe ~= nil and recipe.refining == true
  end,
  IsRecipeCraftAll = function(id)
    local recipe = recipe_of(id)
    return recipe ~= nil and recipe.craftAll == true
  end,
  GetRecipeResultId = function(id)
    local recipe = recipe_of(id)
    return recipe and swcui.crc32(string.lower(recipe.item or "")) or 0
  end,
  GetRecipeOutputQuantity = function(id)
    local recipe = recipe_of(id)
    return recipe and recipe.outputQty or 0
  end,
  -- The Azoth of one job of a perk upgrade level G (docs/systems/crafting.md, stage 3.7c; client 0x2E56070, 0x2E40EE0): trunc(PerkCost x
  -- G) (no faction control reduction: no territory), then x (1 - AzothDiscountPercent of the station row), with floor.
  GetPerkCostForLocalPlayer = function(id, level)
    local recipe = recipe_of(id)
    if not recipe then
      return 0
    end
    local cost = math.floor((recipe.perkCost or 0) * (level or 0))
    local row = C.station and station_row(C.station.key)
    return math.floor(cost * (1 - (row and row.discount or 0)))
  end,
  OnCraftingScreenHidden = screen_hidden,
  -- The storage search of the crafting screen: the backend has no storage (docs/systems/crafting.md: an empty result).
  IsCraftingSearchEnabled = function() return false end,
})

-- The ingredient categories of an item (the column IngredientCategories of the item tables: gamedata/items.tsv), as CRC-32 values.
Swc.Define("ItemDataManagerBus", "Broadcast", {
  GetIngredientCategories = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    local out = {}
    for id in string.gmatch(static and static.ingredientCategories or "", "[^,%s]+") do
      out[#out + 1] = crc(id)
    end
    return out
  end,
})

-- The count of an ingredient in a container (the bag): an item entry counts the units of its item id. The craft of the backend takes
-- the bag and the quick slots (docs/systems/crafting.md); craftinginventorycache.luac counts the paperdoll slots itself.
Swc.Define("ContainerRequestBus", "Event", {
  GetIngredientCount = function(id, entry)
    if not entry then
      return 0
    end
    return ContainerRequestBus.Event.GetItemCount(id, entry.ingredientId)
  end,
})

Swc.Define("LocalPlayerUIRequestsBus", "Broadcast", {
  -- The key of the storage of the territory: the backend has no storage before its stage 5.2 (an empty key: no storage panel).
  GetStorageKeyForGlobalStorage = function() return "" end,
})

-- ------------------------------------------------------------------------------------------------------ the gear score

-- The craft bonus of the statuses (state 205 field 1: trade skill code -> bonus; Swc.HostCraftBonus) and the cap of the client
-- setting (50; docs/systems/crafting.md, stage 3.7c).
C.statusBonus = C.statusBonus or {}
local STATUS_CAP = 50
-- The trade skill codes of state 205 (the TradeskillType of the client, Weaponsmithing 0; backend world/crafting.cpp tradeskill_code).
local TRADESKILL_CODES = {weaponsmithing = 0, armoring = 1, engineering = 6}

--- The host: the gear score bonus map of state 205 ("code bonus" lines).
function Swc.HostCraftBonus(text)
  local map = {}
  for code, bonus in string.gmatch(text or "", "(%d+) (%d+)") do
    map[tonumber(code)] = tonumber(bonus)
  end
  C.statusBonus = map
end

--- @return The material values of an item (crafting-data.json `materials`: ExtraBonusItemChance, IngredientBonusPrimary,
--- IngredientBonusSecondary, IngredientGearScoreBaseBonus, IngredientGearScoreMaxBonus), or nil.
local function material(itemId)
  local static = type(itemId) == "number" and Swc.ItemStatic(itemId) or nil
  local key = static and static.id or itemId
  return key and D().materials[key] or nil
end

--- The craft gear score (a copy of the backend rule Crafting::gear_score_of, stage 3.7c; client 0x2E57A70): only a recipe with a
--- BaseGearScore. The high value: BaseGearScore + the MaxGearScoreBonus of the rank for the tier of the output (the columns T2 to T5,
--- else the first; not with DisallowBonusesToGS or refining) + the IngredientGearScoreMaxBonus of each selected category member (a
--- procedural recipe; a sum below 0 takes it to 0) + the status bonus of the trade skill (cap 50). The low value of the client takes
--- the Min columns and IngredientGearScoreBaseBonus (no effect on a result). The GearScoreOverride of the output replaces both; the cap
--- is the MaxCraftGS of the output, else of the owned expansion (backend ProgressionTables::max_craft_gear_score).
--- @param selected The item ids (CRC-32 or text) of the selected member of each ingredient, in the order of the recipe (or nil).
--- @return low, high (0, 0 with no rule).
function C.CraftGearScore(recipe, selected)
  if not recipe or (recipe.baseGearScore or 0) == 0 then
    return 0, 0
  end
  local output = Swc.ItemStatic(crc(recipe.item))
  local tier = output and output.tier or 0
  local low, high = recipe.baseGearScore, recipe.baseGearScore
  local bonus = not recipe.disallowBonuses and not recipe.refining and recipe.skill ~= ""
  if bonus then
    local rows = D().rankGearScore[recipe.skill] or {}
    local rank = CategoricalProgressionRequestBus.Event.GetRank(nil, crc(recipe.skill))
    local row = rows[math.min(rank, #rows - 1) + 1]
    if row then
      high = high + ((tier >= 2 and tier <= 5) and row[tier] or row[1])
    end
    local rankRow = rank_data(crc(recipe.skill), rank)
    local suffix = (tier >= 2 and tier <= 5) and ("T" .. tier) or ""
    low = low + (rankRow["minGearScoreBonus" .. suffix] or 0)
  end
  if recipe.procedural then
    local lowSum, highSum = 0, 0
    for index, ingredient in ipairs(recipe.ingredients or {}) do
      local item = selected and selected[index]
      if ingredient[1] == "Category_Only" and item and item ~= 0 and item ~= "" then
        local values = material(item)
        lowSum = lowSum + (values and values[4] or 0)
        highSum = highSum + (values and values[5] or 0)
      end
    end
    low = math.max(0, low + lowSum)
    high = math.max(0, high + highSum)
  end
  if bonus then
    local code = TRADESKILL_CODES[string.lower(recipe.skill)]
    high = high + math.min(code and C.statusBonus[code] or 0, STATUS_CAP)
  end
  if output and (tonumber(output.gearScoreOverride) or 0) ~= 0 then
    low, high = output.gearScoreOverride, output.gearScoreOverride
  end
  local cap = recipe.maxCraftGS or 0
  if cap == 0 then
    local expansion = PlayerComponentRequestsBus.Event.GetOwnedExpansionData(nil)
    cap = expansion and expansion.maxCraftGS or 0
  end
  if cap ~= 0 then
    low, high = math.min(low, cap), math.min(high, cap)
  end
  return low, high
end

--- @return The item ids of a selection list of the scripts (numbers, item descriptors, or texts).
local function selected_ids(list)
  if type(list) ~= "table" then
    return nil
  end
  local out = {}
  for index, value in pairs(list) do
    if type(value) == "number" or type(value) == "string" then
      out[index] = value
    elseif value ~= nil then
      local ok, id = pcall(function() return value.itemId end)
      out[index] = ok and id or nil
    end
  end
  return out
end

GearScoreRange = setmetatable({}, {__call = function() return {minValue = 0, maxValue = 0} end})

Swc.Define("CraftingRequestBus", "Broadcast", {
  -- qualitybar.luac SetRecipeData: the gear score range of the craft (the bar subtracts BaseGearScore for its bonus parts).
  GetGearScoreBonus = function(recipeData, _, _, selected)
    local recipe = recipe_of(recipeData)
    local low, high = C.CraftGearScore(recipe, selected_ids(selected))
    return {minValue = low, maxValue = high}
  end,
})

-- The rarity data of a rarity level (qualitybar.luac: the thresholds of its bar) and the perk tiers of the crafting list (itemcommon.luac).
C.RarityData = Swc.ValueClass({name = "ItemRarityData", fields = {"percents", "barColor"}, methods = {}})
ItemRarityData = setmetatable({}, {__call = function() return C.RarityData.new({percents = {}}) end})

local PerkTierData = Swc.ValueClass({
  name = "PerkTierData",
  fields = {"tier"},
  methods = {
    -- The largest perk count of a crafted item of the tier at a gear score (backend CraftingData::craft_max_perks; client 0x5C4D730
    -- with the crafting flag): the largest count with a range that has the gear score, else the count of the nearest range; 0 at 0.
    GetMaxPerksForGearScore = function(self, gearScore)
      local tiers = (D().perkGeneration or {}).tiers or {}
      local ranges = tiers[(self.tier or 0) + 1]
      if not ranges or (gearScore or 0) == 0 then
        return 0
      end
      local best, nearest, distance = nil, 0, math.huge
      for _, entry in ipairs(ranges) do
        for _, range in ipairs(entry[2]) do
          if gearScore >= range[1] and gearScore <= range[2] then
            best = math.max(best or 0, entry[1])
          end
          local d = gearScore < range[1] and range[1] - gearScore or gearScore - range[2]
          if d < distance then
            distance, nearest = d, entry[1]
          end
        end
      end
      return best or nearest
    end,
  },
})

Swc.Define("LocalPlayerUIRequestsBus", "Broadcast", {
  GetItemRarityData = function() return C.RarityData.new({percents = {}}) end,
  GetPerkTierData = function(tier) return PerkTierData.new({tier = tier or 0}) end,
})

-- The crafts whose output fits in the bag (craftingrecipepanel.luac: the largest quantity). The rule of the client: the free units of
-- the stacks of the output item and the free slots of the bag (500 slots, kMaxContainerSlots of the backend) times its stack size.
-- The backend plans each job (Inventory::plan_craft; the slots that the costs free count there too): it can accept more.
Swc.Define("ContainerRequestBus", "Event", {
  GetSpaceToCraft = function(id, resultItemId, perCraft)
    local static = Swc.ItemStatic(resultItemId)
    local stack = math.max(1, static and static.maxStack or 1)
    local units, used = 0, 0
    local bag = id == Swc.ids.inventory and Swc.player.bag or nil
    for i = 0, (bag and bag.count or 0) - 1 do
      local slot = bag.slots[i]
      if slot and (slot.static or slot.occupied) then
        used = used + 1
        if slot.static and slot.static.crc == resultItemId then
          units = units + math.max(0, stack - (slot.quantity or 0))
        end
      end
    end
    units = units + math.max(0, Swc.bagCapacity - used) * stack
    return math.floor(units / math.max(1, perCraft or 1))
  end,
})

-- ---------------------------------------------------------------------------------------------- the fee and the cooldown

--- The fee of a craft in hundredths of a coin (a copy of the backend rule Crafting::fee_of; client 0x2E513A0, 0x2E57580): no fee when
--- the Assembly of the station does not charge one (m_chargeCraftingFee; a camp); else count x CraftingFee (the territory factor 1: the
--- backend has no territory); with UseCraftingTax at least 1 (no tax). The screen shows "Everfall Fee: 0.01" for 1.
function C.Fee(recipe, count)
  if not recipe or not C.station or not C.station.fee or (count or 0) <= 0 then
    return 0
  end
  local base = count * (recipe.fee or 0)
  return recipe.tax and math.max(1, base) or base
end

-- The cooldown windows of the recipes (state 205 field 0: recipe CRC -> crafts used and the end of the window, wall clock seconds;
-- Swc.HostCraftCooldowns). The backend sends the full map at each change.
C.cooldowns = C.cooldowns or {}

--- The host: the cooldown map of state 205 ("crc used endSeconds" lines).
function Swc.HostCraftCooldowns(text)
  local map = {}
  for id, used, finish in string.gmatch(text or "", "(%d+) (%d+) (%d+)") do
    map[tonumber(id)] = {used = tonumber(used), finish = tonumber(finish)}
  end
  C.cooldowns = map
  Swc.SetData("Hud.LocalPlayer.Crafting.UpdateCooldowns", true)
end

--- @return The crafts that are left in the cooldown window of a recipe (backend Crafting::cooldown_left): no cooldown: a large count; no
--- window or its end passed: CooldownQuantity.
function C.CooldownLeft(recipe)
  if not recipe or (recipe.cooldownQuantity or 0) == 0 then
    return 1000000
  end
  local window = C.cooldowns[recipe.crc]
  local now = WallClockTimePoint:Now():GetTimeSinceEpoc():ToSeconds()
  if not window or now >= window.finish then
    return recipe.cooldownQuantity
  end
  return math.max(0, recipe.cooldownQuantity - window.used)
end

Swc.Define("CraftingRequestBus", "Broadcast", {
  GetCraftingFeeOnCurrentTerritory = function(id, count) return C.Fee(recipe_of(id), count or 1) end,
  IsRecipeOnCooldown = function(id, count)
    local recipe = recipe_of(id)
    return recipe ~= nil and C.CooldownLeft(recipe) < math.max(1, count or 1)
  end,
  GetRemainingCooldownCount = function(id)
    local recipe = recipe_of(id)
    return recipe and math.min(C.CooldownLeft(recipe), recipe.cooldownQuantity or 0) or 0
  end,
  GetCooldownDuration = function(id)
    local recipe = recipe_of(id)
    return recipe and recipe.cooldownSeconds or 0
  end,
  GetCooldownTimeRemainingInSeconds = function(id)
    local recipe = recipe_of(id)
    local window = recipe and C.cooldowns[recipe.crc]
    if not window then
      return 0
    end
    return math.max(0, window.finish - WallClockTimePoint:Now():GetTimeSinceEpoc():ToSeconds())
  end,
})

Swc.Define("LocalPlayerUIRequestsBus", "Broadcast", {
  GetMaximumPerksByTierAndGearscore = function(tier, gearScore)
    return PerkTierData.new({tier = tier or 0}):GetMaxPerksForGearScore(gearScore or 0)
  end,
})

-- ------------------------------------------------------------------------------- the bonus items and the craft check

--- @return The item id text of a selection (a CRC-32 or a text), or "".
local function item_key(value)
  if type(value) == "number" then
    local static = Swc.ItemStatic(value)
    return static and static.id or ""
  end
  return value or ""
end

--- @return The item ids of a vector of the scripts (vector_Crc32 of craftingrecipepanel.luac GetIngredients: the selected item of each
--- ingredient in the order of the recipe; an item ingredient has its item).
local function selection(recipe, list)
  local out = {}
  for index, ingredient in ipairs(recipe.ingredients or {}) do
    local value = type(list) == "table" and list[index] or nil
    if (value == nil or value == 0) and ingredient[1] == "Item" then
      value = ingredient[2]
    end
    out[index] = item_key(value)
  end
  return out
end
C.Selection = selection

--- The chance of bonus items of one job (a copy of the backend rule Crafting::yield_of, stage 3.7b; client 0x5C52A40, 0x2E57A70):
--- BonusItemChance of the recipe; the base tier is the item of the first ingredient (an item ingredient adds its ExtraBonusItemChance);
--- each later category ingredient adds BonusItemChanceIncrease (a higher tier than the base) or BonusItemChanceDecrease (a lower tier)
--- and its ExtraBonusItemChance; then the roll points x 0.0001 (IngredientBonusPrimary of the first entry, IngredientBonusSecondary of
--- the others, the RollBonus of the rank with no DisallowBonusesToGS; the ROL effects of the statuses: none in the client), at least 0.
function C.BonusChance(recipe, selected)
  if not recipe or #(recipe.ingredients or {}) == 0 then
    return 0
  end
  local tier = function(id)
    local static = Swc.ItemStatic(crc(id))
    return static and static.tier or 0
  end
  local values = function(id) return D().materials[id] or {0, 0, 0, 0, 0} end
  local chance = recipe.bonusChance or 0
  local base = tier(selected[1])
  if recipe.ingredients[1][1] ~= "Category_Only" then
    chance = chance + values(selected[1])[1]
  end
  for index = 2, #recipe.ingredients do
    if recipe.ingredients[index][1] == "Category_Only" then
      local difference = tier(selected[index]) - base
      local increase, decrease = recipe.bonusIncrease or {}, recipe.bonusDecrease or {}
      local part = 0
      if difference > 0 and #increase > 0 then
        part = increase[math.min(difference, #increase)]
      elseif difference < 0 and #increase > 0 and #decrease > 0 then
        part = decrease[math.min(-difference, #decrease)]
      end
      chance = chance + part + values(selected[index])[1]
    end
  end
  local points = 0
  for index = 1, #recipe.ingredients do
    points = points + (index == 1 and values(selected[index])[2] or values(selected[index])[3])
  end
  if recipe.skill ~= "" and not recipe.disallowBonuses then
    local rank = CategoricalProgressionRequestBus.Event.GetRank(nil, crc(recipe.skill))
    local row = rank_data(crc(recipe.skill), rank)
    points = points + math.floor((row.rollBonus or 0) + 0.5)
  end
  return math.max(0, math.floor((chance + points * 0.0001) * 1e6 + 0.5) / 1e6)
end

--- @return The cause of the backend for a recipe at the station of the use (the copy of the request checks of Crafting::check that the
--- client knows before a request; decision C3 of the orchestrator), or nil: a cause of the data (`unsupported`, the first one), an
--- expansion (the backend does not check it yet: CraftVerdict::expansion), no station row of the Assembly key (unknownStation).
function C.Refusal(recipe)
  if not recipe then
    return "no data"
  end
  if C.station and not station_row(C.station.key) then
    return "unknown station " .. tostring(C.station.key)
  end
  if recipe.unsupported and recipe.unsupported[1] then
    return recipe.unsupported[1]
  end
  if (recipe.expansion or "") ~= "" then
    return "expansion " .. recipe.expansion
  end
  return nil
end

--- @return True when the client would send the request of a craft of one job (craftingrecipepanel.luac CheckCanCraft: the native check
--- 0x2E49FE0: the access, the materials, the cooldown, the Azoth, the capacity): the station permits the recipe, the rank, no cause of
--- the backend, the materials of the selection in the bag, the cooldown, the coin of the fee, a place in the bag.
function C.CanCraft(recipe, selected, count)
  count = math.max(1, count or 1)
  if not recipe or C.Refusal(recipe) or not station_permits(recipe) then
    return false
  end
  if recipe.skill ~= "" and (recipe.level or 0) > 0 then
    local rank = CategoricalProgressionRequestBus.Event.GetRank(nil, crc(recipe.skill))
    if rank < recipe.level then
      return false
    end
  end
  if C.CooldownLeft(recipe) < count then
    return false
  end
  local need = {}
  for index, ingredient in ipairs(recipe.ingredients or {}) do
    if ingredient[1] == "Currency" then
      local balance = CategoricalProgressionRequestBus.Event.GetProgression(nil, crc(ingredient[2]))
      if balance < ingredient[3] * count then
        return false
      end
    else
      local item = selected[index]
      if item == "" then
        return false
      end
      need[item] = (need[item] or 0) + ingredient[3] * count
    end
  end
  for item, quantity in pairs(need) do
    if ContainerRequestBus.Event.GetItemCount(Swc.ids.inventory, crc(item)) < quantity then
      return false
    end
  end
  if (Swc.player.coin or 0) < C.Fee(recipe, count) then
    return false
  end
  return ContainerRequestBus.Event.GetSpaceToCraft(Swc.ids.inventory, crc(recipe.item), recipe.outputQty or 1) >= count
end

Swc.Define("CraftingRequestBus", "Broadcast", {
  GetChanceForBonusItems = function(recipeData, selected)
    local recipe = recipe_of(recipeData)
    return recipe and C.BonusChance(recipe, selection(recipe, selected)) or 0
  end,
  CanCraftRecipe = function(id, selected, count)
    local recipe = recipe_of(id)
    return recipe ~= nil and C.CanCraft(recipe, selection(recipe, selected), count)
  end,
})

Swc.Define("CategoricalProgressionRequestBus", "Event", {
  -- The largest rank of a trade skill: Expansion01MaxLevel with the owned expansion (the backend gives each player Expansion2023), else
  -- MaxLevel (javelindata_categoricalprogression).
  GetMaxRank = function(_, id)
    local row = progression_data(id)
    local expansion = PlayerComponentRequestsBus.Event.GetOwnedExpansionData(nil)
    if expansion and expansion.maxCraftGS ~= nil and (row.expansion01MaxLevel or 0) > 0 and Swc.OwnsExpansion and Swc.OwnsExpansion() then
      return row.expansion01MaxLevel
    end
    return row.maxLevel or 0
  end,
})

-- ---------------------------------------------------------------------------------------------------------- the request

--- @return The values of a vector of the scripts (vector_Crc32: a Lua table) as a comma list ("-" when empty).
local function crc_list(list)
  local parts = {}
  for _, value in ipairs(type(list) == "table" and list or {}) do
    parts[#parts + 1] = string.format("%d", tonumber(value) or 0)
  end
  return #parts > 0 and table.concat(parts, ",") or "-"
end

--- craftingscreenv4.luac OnCraftClicked: CraftRecipe(recipe id, count, perk items, upgrade level, the selected items, the gear score
--- roll). The client writes request 2997 (protocol notes 4.50): the fee claim is the rule of the client (C.Fee: the native adds it), the
--- gear score claim is the roll of the quality bar, each ingredient is one entry with the CRC-32 of its selected item (0 for a currency
--- ingredient: live check L5c). The host request: "craft RECIPE COUNT FEE GEARSCORE UPGRADE PERKS ITEMS".
local function craft(id, count, perks, upgrade, selected, gearScore)
  local recipe = recipe_of(id)
  if not recipe or not C.station then
    Swc.Note("crafting: CraftRecipe with no recipe or no station (" .. tostring(id) .. ")")
    return
  end
  count = math.max(1, math.floor(tonumber(count) or 1))
  local items = {}
  local chosen = selection(recipe, selected)
  for index, ingredient in ipairs(recipe.ingredients or {}) do
    items[index] = ingredient[1] == "Currency" and 0 or crc(chosen[index])
  end
  local fee = C.Fee(recipe, count)
  local score = math.max(0, math.min(10000, math.floor((tonumber(gearScore) or 0) + 0.5)))
  local line = string.format("craft %d %d %d %d %d %s %s", recipe.crc, count, fee, score, math.floor(tonumber(upgrade) or 0), crc_list(perks),
    crc_list(items))
  C.lastRequest = line
  Swc.hostRequests[#Swc.hostRequests + 1] = line
  Swc.Note("crafting: " .. line .. " (" .. recipe.id .. ")")
end

--- The host: a craft progress 2993 (the recipe id, the completed jobs, the jobs of the batch; the stop form: no recipe, 0 of 0). The handler
--- of the client gives the counts to CraftingEventBus OnCraftingCountUpdate (craftingscreenv4.luac: the expected item updates of a batch).
function Swc.HostCraftProgress(recipe, completed, total)
  C.progress = {recipe = recipe or "", completed = completed or 0, total = total or 0}
  Swc.Notify("CraftingEventBus", Swc.ids.crafting, "OnCraftingCountUpdate", completed or 0, total or 0)
end
Swc.Handled("CraftingEventBus")

Swc.Define("CraftingRequestBus", "Broadcast", {CraftRecipe = craft})

-- The drive target "recipe:ID" (the UI drives of the checks): the middle of the shown row of the recipe tree (craftingtreeviewitem.luac
-- with the recipe in its entry data).
Swc.driveTargets = Swc.driveTargets or {}
Swc.driveTargets.recipe = function(id)
  for key, instances in pairs(Swc.components or {}) do
    for _, instance in ipairs(instances) do
      local entry = type(instance) == "table" and rawget(instance, "entryData")
      local data = type(entry) == "table" and entry.recipeData
      if data and data.id == id and instance.entityId and swcui.enabled_in_hierarchy(instance.entityId) then
        local x0, y0, _, _, x2, y2 = swcui.viewport_points(instance.entityId)
        return string.format("%.1f %.1f", (x0 + x2) / 2, (y0 + y2) / 2)
      end
    end
  end
  return ""
end

-- The perk buckets of an item (the columns PerkBucket1 to PerkBucket5 of the item tables; crafting.json `perkBuckets`: the type and the
-- chance of each bucket). The upgrade of a perk place (the Azoth upgrade G covers it) adds the chance modifier of its type, 1.0 for the
-- Perk, Attribute, and Gem types of the player base attributes (docs/systems/crafting.md, stage 3.7c: a guarantee).
local PERK_TYPES = {Generated = ePerkType_Generated or 0, Gem = ePerkType_Gem or 1, Inherent = ePerkType_Inherent or 2,
                    OffensiveCharm = ePerkType_OffensiveCharm or 3, DefensiveCharm = ePerkType_DefensiveCharm or 4,
                    SkillCharm = ePerkType_SkillCharm or 5}
local function bucket_of(itemCrc, index)
  local static = Swc.ItemStatic(itemCrc)
  local name = static and static["perkBucket" .. tostring(index)]
  return name and name ~= "" and D().perkBuckets and D().perkBuckets[name] or nil
end
--- The craft mod data (crafting.json craftMods): the resources by the CRC-32 of the lower-case id, each with its display perk and its leaf
--- perks as tag sets.
function C.Mods()
  if C.mods then
    return C.mods
  end
  local function tags(text)
    local out = {}
    for tag in string.gmatch(string.lower(text or ""), "[^+%s]+") do
      out[tag] = true
    end
    return out
  end
  local data = D().craftMods or {}
  local mods = {list = {}, byCrc = {}, labels = {}}
  for key, labels in pairs(data.perkLabels or {}) do
    mods.labels[key] = tags(labels)
  end
  for _, row in ipairs(data.resources or {}) do
    local mod = {id = row[1], crc = crc(row[1]), display = row[2], leaves = {}}
    for _, leaf in ipairs(row[3] or {}) do
      mod.leaves[#mod.leaves + 1] = {id = leaf[1], include = tags(leaf[2]), exclude = tags(leaf[3]), labels = tags(leaf[4])}
    end
    mods.list[#mods.list + 1] = mod
    mods.byCrc[mod.crc] = mod
  end
  C.mods = mods
  return mods
end

local function meets(a, b)
  for tag in pairs(a) do
    if b[tag] then
      return true
    end
  end
  return false
end

--- @return The CRC-32 values of the compatible craft mods of an output item (GetCompatiblePerkResourceIds).
function C.CompatibleMods(output, others)
  local static = Swc.ItemStatic(output)
  if not static or (static.canHavePerks or 0) ~= 1 then
    return {}
  end
  local mods = C.Mods()
  local classes = {}
  for tag in string.gmatch(string.lower(static.itemClass or ""), "[^+%s]+") do
    classes[tag] = true
  end
  local function fits(leaf)
    return (next(leaf.include) == nil or meets(leaf.include, classes)) and not meets(leaf.exclude, classes)
  end
  local reserved = {}
  for n = 1, 5 do
    local perk = static["perk" .. n]
    for tag in pairs(perk and mods.labels[string.lower(tostring(perk))] or {}) do
      reserved[tag] = true
    end
  end
  for _, other in ipairs(others or {}) do
    local mod = mods.byCrc[type(other) == "number" and other or crc(tostring(other))]
    for _, leaf in ipairs(mod and mod.leaves or {}) do
      if fits(leaf) and not meets(leaf.labels, reserved) then
        for tag in pairs(leaf.labels) do
          reserved[tag] = true
        end
      end
    end
  end
  local out = {}
  for _, mod in ipairs(mods.list) do
    for _, leaf in ipairs(mod.leaves) do
      if fits(leaf) and not meets(leaf.labels, reserved) then
        out[#out + 1] = mod.crc
        break
      end
    end
  end
  return out
end

Swc.Define("ItemDataManagerBus", "Broadcast", {
  -- The craft mods (perk items) of an output item (AST-231; research crafting-ui.md 2, client code 0x4436640): the resources with
  -- SocketType None and a perk bucket that has one leaf perk for the classes of the output (ItemClass empty or shared, no shared
  -- ExcludeItemClass) whose ExclusiveLabels do not meet the labels of the fixed perks of the output and of the other selected resources.
  -- No gear score or tier test; the screen adds the counts of the bag, the type filter, the None row, and the order.
  GetCompatiblePerkResourceIds = function(output, others)
    return C.CompatibleMods(output, others)
  end,
  -- The display perk of a resource (code 0x4436C60): the first perk of its bucket.
  GetDisplayPerkIdFromResource = function(resource)
    local mod = C.Mods().byCrc[type(resource) == "number" and resource or crc(tostring(resource))]
    return mod and crc(mod.display) or 0
  end,
  GetPerkBucketType = function(itemCrc, index)
    local bucket = bucket_of(itemCrc, index)
    return bucket and PERK_TYPES[bucket[1]] or (ePerkType_Invalid or 6)
  end,
  GetPerkChance = function(itemCrc, index, upgraded)
    local bucket = bucket_of(itemCrc, index)
    if not bucket then
      return 0
    end
    return math.min(1, (bucket[2] or 0) + (upgraded and 1.0 or 0))
  end,
})

-- ------------------------------------------------------------------------------------------------------------ the refusals

-- Decision C3 of the orchestrator: a recipe that the backend refuses for a known reason shows as unavailable with its reason before the
-- request. The cause (C.Refusal: a cause of the data, an expansion, an Assembly key with no row) goes into the original expansion row of
-- the requirements (VersionRequirement) with the X icon and the cause in red; CanCraftRecipe is false, so CRAFT stays disabled. The rows of
-- craftingstatspanel.luac UpdateRequirements: from y 81, 28 for each shown row; the panel under them moves down with them.
local ROW_TOP, ROW_STEP = 81, 28
C.refusalPrefix = "Not available on this server: "
Swc.scriptHooks["lyshineui.crafting.craftingstatspanel"] = function(base)
  local update = base.UpdateRequirements
  if type(update) ~= "function" then
    return
  end
  base.UpdateRequirements = function(self, ...)
    local result = update(self, ...)
    local recipe = self.recipeData and recipe_of(self.recipeData.id)
    local reason = recipe and C.Refusal(recipe)
    local rows = self.Properties and self.Properties.CraftingRequirements
    if not reason or not rows then
      return result
    end
    local shown = 0
    for _, name in ipairs({"StationRequirement", "SkillRequirement"}) do
      if rows[name] and UiElementBus.Event.IsEnabled(rows[name]) then
        shown = shown + 1
      end
    end
    UiElementBus.Event.SetIsEnabled(rows.VersionRequirement, true)
    UiTransformBus.Event.SetLocalPositionY(rows.VersionRequirement, ROW_TOP + ROW_STEP * shown)
    UiImageBus.Event.SetSpritePathname(rows.VersionRequirementIcon, "lyshineui/images/icons/misc/icon_requirement_x.dds")
    UiElementBus.Event.SetIsEnabled(rows.VersionIcon, false)
    UiElementBus.Event.SetIsEnabled(rows.VersionDescText, true)
    UiTransformBus.Event.SetLocalPositionX(rows.VersionDescText, 23)
    UiTextBus.Event.SetText(rows.VersionDescText, C.refusalPrefix .. reason)
    UiTextBus.Event.SetColor(rows.VersionDescText, self.UIStyle and self.UIStyle.COLOR_RED_MEDIUM or Color(0.8, 0.2, 0.2, 1))
    UiTransformBus.Event.SetLocalPositionY(rows.ParentContainer, -123 - ROW_STEP * (shown + 1))
    if self.Properties.QuantityWidget then
      UiTransformBus.Event.SetLocalPositionY(self.Properties.QuantityWidget, -261 - ROW_STEP * (shown + 1))
    end
    return result
  end
end
