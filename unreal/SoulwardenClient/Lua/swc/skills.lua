-- The character window (stage MVP-G, Linear AST-222; docs/UI.md "The character window"): the original canvas Skills
-- (skills.luac: the attributes, the weapon mastery with its trees, the trade skills) and the native parts that it reads.
--
-- The settings: skills.luac observes the data nodes javelin.pvp-reward-track-enabled and javelin.meta-achievements.enabled at
-- its OnInit, and it makes its tabs (SetupTabs) when both have a value; its other observers need the tabs. The retail client has
-- its registered settings as data nodes: the javelin.* settings of Swc.config go into the data layer here. The meta
-- achievements need data that the backend does not have: their flag is off (the tab does not show).

local S = {}
Swc.skills = S

for name, value in pairs(Swc.config) do
  if string.sub(name, 1, 8) == "javelin." and LyShineDataLayerBus.Broadcast.GetDataFromNode(name) == nil then
    Swc.SetData(name, value)
  end
end
-- The three screens of the window: the retail window has the tabs Attributes, Weapon Mastery, and Trade Skills (Soulwarden
-- docs/systems/progression-and-skills.md, the positions of the live checks): their UIFeatures flags are on.
for name, value in pairs({["javelin.meta-achievements.enabled"] = false, ["javelin.meta-achievements.init-fix-logging.enabled"] = false,
                          ["javelin.enable-in-game-survey"] = false, ["UIFeatures.g_uiEnableAttributes"] = true,
                          ["UIFeatures.g_uiEnableTradeskills"] = true, ["UIFeatures.g_uiEnableWeaponMastery"] = true}) do
  if Swc.config[name] == nil then
    Swc.SetConfig(name, value)
    Swc.SetData(name, value)
  end
end

-- With the meta achievements off, the screen has no tab for them, but its observer of MetaAchievements.UnclaimedRewardCount
-- calls UpdateTabPoints for that screen (TabbedList:GetIndex(nil) gives nil, and the rest of OnInit stops: no SkillsBus). A
-- screen with no tab gets no tab points.
Swc.scriptHooks["lyshineui.skills.skills"] = function(base)
  local update = base.UpdateTabPoints
  if type(update) ~= "function" then
    return
  end
  base.UpdateTabPoints = function(self, name, ...)
    if not (self.screenToIndex and self.screenToIndex[name]) then
      return
    end
    return update(self, name, ...)
  end
end

-- --------------------------------------------------------------------------------------------------------- the weapon trees
-- The data: gamedata/skilltrees.json (tools/convert-skills.sh: the weapon ability tables, the progression point links, the index
-- of each table in the client, the rank tables and the progressions of the backend copy). The state: the ranks (slot 65,
-- Swc.owner.ranks), the free points of each pool (slot 67, Swc.owner.pools), the learned nodes (slot 67, Swc.HostLearnedNodes),
-- and the bars (slot 46, Swc.player.bars). The rules: masterytree.luac SetTableId, masterytreewindow.luac (research extracts).
--
-- The edits (protocol notes 4.13; the backend accepts what world/skills.h accepts):
--   Assign  SpendPointsOnAbility marks a node (the client changes its own tree at once), ApplyAbilityChanges sends the requests:
--           "assign POOL NODE" for each node (1112, then 2701 for an active node to the first empty place of the bar), then
--           "apply" (2857); the next points state of the server is the answer.
--   Bar     RequestChangeMappedAbility: "bar TABLEINDEX SLOT ABILITY" (2701); the client changes its own bar; the next ability
--           state of the server is the answer.
--   Reset   RequestMasteryTreeRespec: "respec TABLEINDEX" (186); the answer 2858. The backend resets a tree only when the reset
--           costs nothing (a rank below 10; it has no Azoth payment): with a cost the button is unavailable (no false success).
local D
local function data()
  if D then
    return D
  end
  D = swcui.load_json("gamedata/skilltrees.json") or {tables = {}, rankTables = {}, progressions = {}}
  D.byCrc, D.nodes, D.progressionByCrc = {}, {}, {}
  for name, t in pairs(D.tables) do
    t.name = name
    D.byCrc[t.crc] = t
    t.byCrc = {}
    for _, node in ipairs(t.nodes) do
      t.byCrc[node.crc] = node
      D.nodes[node.crc] = node
      node.table = t
    end
  end
  for _, p in ipairs(D.progressions) do
    D.progressionByCrc[p.crc] = p
  end
  return D
end

local function table_of(id)
  if type(id) == "string" then
    id = swcui.crc32(string.lower(id))
  end
  return data().byCrc[tonumber(id) or -1]
end

S.learned = {}   -- node crc -> true (the points state of the server, and the spends of the client before its answer)
S.requests = {}  -- the log of the edit requests (the fixture reads it)

--- The learned nodes of the weapon trees (slot 67): "NODE" lines (a state has each node: it replaces the set).
function Swc.HostLearnedNodes(text)
  local learned = {}
  for node in string.gmatch(text or "", "(%d+)") do
    learned[tonumber(node)] = true
  end
  S.learned = learned
  local player = LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.HudComponent.PlayerEntityId")
  Swc.Notify("ProgressionPointsNotificationBus", player, "OnProgressionPointsChanged")
end

local function request(text)
  Swc.hostRequests[#Swc.hostRequests + 1] = text
  S.requests[#S.requests + 1] = text
  Swc.Note("skills: request " .. text)
end

local function ability_of(node)
  if not node then
    return nil
  end
  return {id = node.crc, key = node.id, abilityId = node.id, displayName = node.displayName, displayDescription = node.description,
          sound = node.sound, displayIcon = node.icon, uiCategory = swcui.crc32(string.lower(node.uiCategory or "")),
          treeId = node.tree, treeRowPos = node.row, treeColPos = node.column, cooldownTimer = 0,
          tableNameId = node.table.crc, isActiveAbility = node.active}
end

local function free_points(t)
  local pools = Swc.owner.pools or {}
  return pools[t.crc] or 0
end

local function spent(t)
  local n = 0
  for _, node in ipairs(t.nodes) do
    if S.learned[node.crc] then
      n = n + 1
    end
  end
  return n
end

local pendingAssign = {}

Swc.Define("CharacterAbilityRequestBus", "Event", {
  GetAvailableAbilityDataByAbilityTableId = function(_, id)
    local t, out = table_of(id), {}
    for _, node in ipairs(t and t.nodes or {}) do
      if node.active then
        out[#out + 1] = ability_of(node)
      end
    end
    return out
  end,
  GetAbilityData = function(_, _, id)
    return ability_of(data().nodes[tonumber(id) or -1])
  end,
  IsDefaultUnlocked = function(_, _, id)
    local node = data().nodes[tonumber(id) or -1]
    return node and node.unlockDefault or false
  end,
  GetSpentPointsOnAbility = function(_, id)
    return S.learned[tonumber(id) or -1] and 1 or 0
  end,
  SpendPointsOnAbility = function(_, tableId, id, count)
    local t, node = table_of(tableId), data().nodes[tonumber(id) or -1]
    if t and node and not S.learned[node.crc] and (count or 1) > 0 then
      S.learned[node.crc] = true
      pendingAssign[#pendingAssign + 1] = {t = t, node = node}
    end
  end,
  ApplyAbilityChanges = function()
    if #pendingAssign == 0 then
      return
    end
    for _, entry in ipairs(pendingAssign) do
      request(string.format("assign %d %d %d", entry.t.index, entry.t.crc, entry.node.crc))
      -- An active ability goes to the first empty place of the bar (protocol notes 4.13: the button Assign of the retail client).
      if entry.node.active then
        local bar = Swc.player.bars[entry.t.crc] or {}
        for place = 1, 3 do
          if not bar[place] then
            request(string.format("bar %d %d %d", entry.t.index, place - 1, entry.node.crc))
            bar[place] = entry.node.crc
            Swc.player.bars[entry.t.crc] = bar
            Swc.dataBarsChanged = true
            break
          end
        end
      end
    end
    request("apply")
    pendingAssign = {}
  end,
  GetSelectedTableIndexFromTableName = function(_, id)
    local t = table_of(id)
    return t and t.index or -1
  end,
  GetActiveAbilityMoveDataByAbilityTableId = function(_, id)
    local t = table_of(id)
    local bar = t and Swc.player.bars[t.crc] or {}
    -- weaponmasteryrow.luac and masterytreewindow.luac lock a place whose number is not below the length of the list; the three
    -- places of a bar are open (the HUD uses three abilities), thus the list has a fourth element [inference: the native list].
    local out = {}
    for slot = 1, 3 do
      out[slot] = bar[slot] and ability_of(data().nodes[bar[slot]]) or false
    end
    out[4] = false
    return out
  end,
  RequestChangeMappedAbility = function(_, index, slot, abilityId)
    local t
    for _, each in pairs(data().tables) do
      if each.index == index then
        t = each
      end
    end
    if not t then
      return
    end
    request(string.format("bar %d %d %d", index, slot, tonumber(abilityId) or 0))
    -- The client changes its own bar at once (the ability leaves the place that it had); the server state is the answer.
    local bar = Swc.player.bars[t.crc] or {}
    for i = 1, 3 do
      if bar[i] == abilityId then
        bar[i] = nil
      end
    end
    bar[slot + 1] = abilityId
    Swc.player.bars[t.crc] = bar
    Swc.dataBarsChanged = true
  end,
  RequestMasteryTreeRespec = function(_, index)
    request(string.format("respec %d", index))
  end,
  DoesAbilitySupportSmartcast = function(_, id)
    local node = data().nodes[tonumber(id) or -1]
    return node and node.smartcast or false
  end,
  GetMinCooldownTime = function() return 0 end,
})

Swc.Define("ProgressionPointRequestBus", "Event", {
  -- The static data of a point pool (bannertriggers.luac OnProgressionPointsChanged reads its poolCategory; a nil record was a Lua error
  -- at each change of the learned nodes): the weapon nodes have the category 0 here; the territory cards (ePoolCategory_Territory 2) have
  -- their own record in swc/territory.lua.
  GetStaticProgressionPointData = function(_, id) return {id = id or 0, poolCategory = 0} end,
  GetUnallocatedPoolPoints = function(_, id)
    local t = table_of(id)
    if not t then
      return 0
    end
    return math.max(0, free_points(t) - #pendingAssign)
  end,
  GetNumAllocatedPoolPoints = function(_, id)
    local t = table_of(id)
    return t and spent(t) or 0
  end,
  GetAllProgressionPointDataFromPoolId = function(_, id)
    local t, out = table_of(id), {}
    for _, node in ipairs(t and t.nodes or {}) do
      out[#out + 1] = {id = node.crc, requiredPointId = node.required}
    end
    return out
  end,
  GetLevel = function(_, id)
    return S.learned[tonumber(id) or -1] and 1 or 0
  end,
})

-- The rank data of a weapon mastery: the reset cost of the rank (javelindata_weaponmastery AzothRespecCost: the backend copy
-- rankTables), the experience of the rank, the largest points of a rank (one point for each rank: the backend rule).
local function rank_table(id)
  local p = data().progressionByCrc[tonumber(id) or swcui.crc32(string.lower(tostring(id)))]
  return p and data().rankTables[p.rankTable], p
end
-- A progression that is not a weapon mastery (a trade skill, a territory standing: AST-242) keeps the method of swc/crafting.lua.
local earlier = Swc.Bus("CategoricalProgressionRequestBus").Event
local earlierMaxPoints, earlierMaxRank = rawget(earlier, "GetMaxPointsForRank"), rawget(earlier, "GetMaxRank")
Swc.Define("CategoricalProgressionRequestBus", "Event", {
  GetRankData = function(_, id, rank)
    local rt = rank_table(id)
    if not rt and Swc.crafting and Swc.crafting.RankData then
      local key = tonumber(id) or swcui.crc32(string.lower(tostring(id)))
      local other = Swc.crafting.RankData(key, tonumber(rank) or 0)
      return {azothRespecCost = 0, maximumInfluence = other.maximumInfluence, displayName = other.displayName, rank = rank}
    end
    local i = (tonumber(rank) or 0) + 1
    return {azothRespecCost = rt and rt.resetCost and rt.resetCost[i] or 0, maximumInfluence = rt and rt.experience[i] or 0,
            rank = rank}
  end,
  GetMaxRank = function(owner, id)
    local _, p = rank_table(id)
    if not p and earlierMaxRank then
      return earlierMaxRank(owner, id)
    end
    return p and p.maxRank or 0
  end,
  GetAbsoluteMaxRank = function(_, id)
    local _, p = rank_table(id)
    if not p and Swc.crafting and Swc.crafting.ProgressionData then
      -- Another progression (a faction reputation of the faction board, C-12 phase 2: owguild.luac UpdateReputationSummary): the largest
      -- rank of the data, with the expansion (before, 0: the reputation bar divided by 0 and drew its milestones at NaN).
      local row = Swc.crafting.ProgressionData(id)
      return math.max(row.maxLevel or 0, row.expansion01MaxLevel or 0)
    end
    return p and p.maxRank or 0
  end,
  GetMaxPointsForRank = function(owner, id, rank)
    local rt = rank_table(id)
    if not rt and earlierMaxPoints then
      return earlierMaxPoints(owner, id, rank)
    end
    return rt and rt.experience[(tonumber(rank) or 0) + 1] or 0
  end,
})

-- The reset of a tree with a cost: the original rule enables the button when the character has the Azoth of the cost; the backend
-- resets only a tree with no cost (it has no Azoth payment) and refuses the others (2858 success 0). The button is unavailable
-- then: no request that the backend refuses as a false offer.
Swc.scriptHooks["lyshineui.skills.masterytreewindow"] = function(base)
  local update = base.UpdateRespecButton
  if type(update) ~= "function" then
    return
  end
  base.UpdateRespecButton = function(self, ...)
    local result = update(self, ...)
    if (self.azothCost or 0) > 0 and self.RespecButton and self.RespecButton.SetEnabled then
      self.RespecButton:SetEnabled(false)
    end
    return result
  end
end

--- The answer of the server to a reset (2858): the success and the table index. A refused reset changes nothing (the client did
--- not change its tree for the request; the points state is the answer of a success).
function Swc.HostRespecResult(success, index)
  Swc.Note(string.format("skills: the reset of the table %d: %s", index or -1, success and "done" or "refused"))
  S.requests[#S.requests + 1] = string.format("respec-result %d %s", index or -1, success and "1" or "0")
end

-- The points of a tree that its last row needs: 10 (protocol notes 4.13, the condition of a node of the client 0x6849F10 with the
-- number of playerbaseattributes.pbadb; masterynode.luac has the same default).
Swc.Define("PlayerDataManagerBus", "Broadcast", {GetAbilityTreeFinalRowPointRequirement = function() return 10 end})

-- The weapon reference data of the weapons of the mastery screen (weaponmasterydata.luac weaponId: the line "scales with" of a tree,
-- abilitiescommon.luac GetAttributeScalingText): the attribute scaling of weaponstats.tsv, by the CRC-32 of the weapon id.
local WEAPON_IDS = {"SwordT5", "RapierT5", "HatchetT5", "FlailT5", "DaggerT5", "SpearT5", "2HAxeT5", "2HhammerT5", "2hGreatSwordT5", "BowT5", "MusketT5", "BlunderbussT5", "FireStaffT5", "LifeStaffT5", "1hElementalGauntlet_IceT5", "VoidGauntletT5"}
local weaponRefs
Swc.Define("WeaponRefDataManagerBus", "Broadcast", {
  GetWeaponRefData = function(id)
    if not weaponRefs then
      weaponRefs = {}
      for _, name in ipairs(WEAPON_IDS) do
        local row = swcui.table_row("weaponstats", string.lower(name))
        if row then
          local ref = {strengthScaling = row.scalingStrength or 0, dexterityScaling = row.scalingDexterity or 0,
                       intelligenceScaling = row.scalingIntelligence or 0, focusScaling = row.scalingFocus or 0,
                       constitutionScaling = 0, weaponId = name}
          weaponRefs[Math.CreateCrc32(name)] = ref
          weaponRefs[swcui.crc32(string.lower(name))] = ref
        end
      end
    end
    return weaponRefs[id]
  end,
})

-- The drive targets of the checks of the window: "node:ABILITY" is the shown node of a weapon tree (masterynode.luac) of an ability
-- id (the text id of the table, as Ability_Bow_PenetratingShot). @return "x y" in viewport pixels, or "".
-- "ability:ABILITY": the shown button of that ability in the list of a place of the bar (abilitydisplaybutton.luac: its icon).
local pointOf = Swc.HostPointOf
function Swc.HostPointOf(name, fx, fy)
  local kind, ability = nil, nil
  if type(name) == "string" then
    kind, ability = name:match("^(node):(.+)$")
    if not kind then
      kind, ability = name:match("^(ability):(.+)$")
    end
  end
  if not ability then
    return pointOf(name, fx, fy)
  end
  local id = swcui.crc32(string.lower(ability))
  local node = data().nodes[id]
  for element, instances in pairs(Swc.components) do
    for _, instance in ipairs(instances) do
      local script = instance.__swcScript or ""
      local match = (kind == "node" and string.find(script, "masterynode", 1, true) and instance.data and instance.data.id == id)
        or (kind == "ability" and node and string.find(script, "abilitydisplaybutton", 1, true)
            and string.lower(instance.displayIcon or "") == string.lower(node.icon or ""))
      if match and instance.entityId and swcui.enabled_in_hierarchy(instance.entityId) then
        local x0, y0, _, _, x2, y2 = swcui.viewport_points(instance.entityId)
        return string.format("%.1f %.1f", (x0 + x2) / 2, (y0 + y2) / 2)
      end
    end
  end
  return ""
end

-- The attributes screen (the first screen of the window): the largest value of an attribute (the backend copy
-- progression-tables.json attributes.maximum 500; attributemodifier.luac UpdateArrows), and no pending point (the client asks for
-- no attribute change in this stage: the pending values are 0).
local ATTRIBUTE_MAXIMUM = 500
Swc.Define("AttributeRequestBus", "Event", {
  GetMaxLevelForAttribute = function() return ATTRIBUTE_MAXIMUM end,
  GetMaxPointsForAttribute = function() return ATTRIBUTE_MAXIMUM end,
  -- The bonus of the equipment (the Attribute state: the bonus of the gear; attributemodifier.luac SetSpentValueData).
  GetEquipmentAttributeBonusLevel = function(_, type)
    local a = Swc.owner.attributes and Swc.owner.attributes[tonumber(type) or -1]
    return a and a.bonus or 0
  end,
})
for _, name in ipairs({"Strength", "Dexterity", "Intelligence", "Focus", "Constitution"}) do
  local path = "Hud.LocalPlayer.Attributes." .. name .. ".Pending"
  if LyShineDataLayerBus.Broadcast.GetDataFromNode(path) == nil then
    Swc.SetData(path, 0)
  end
end
if LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Attributes.PendingPoints") == nil then
  Swc.SetData("Hud.LocalPlayer.Attributes.PendingPoints", 0)
end

-- The lists of a dynamic scroll box that a script fills in SetVisible or TransitionIn: the box refreshes at its post activation (O3DE
-- UiDynamicScrollBoxComponent, autoRefreshOnPostActivate), before the script has the list, and the count of a nil list was a Lua error
-- at each start (tradeskills_crafting.luac GetNumElements #self.skillsData; changetitlewindow.luac #self.categoryOrderList), then one
-- error for each element of the default count. An empty list at OnInit gives the box no element until the script fills the list and
-- refreshes it (no visible change).
local function empty_lists_at_init(fields)
  return function(base)
    local init = rawget(base, "OnInit")
    if type(init) ~= "function" then
      return
    end
    base.OnInit = function(self, ...)
      for _, field in ipairs(fields) do
        if self[field] == nil then
          self[field] = {}
        end
      end
      return init(self, ...)
    end
  end
end
Swc.scriptHooks["lyshineui.skills.tradeskills.tradeskills_crafting"] = empty_lists_at_init({"skillsData"})
Swc.scriptHooks["lyshineui.skills.changetitlewindow"] = empty_lists_at_init({"categoryOrderList"})
