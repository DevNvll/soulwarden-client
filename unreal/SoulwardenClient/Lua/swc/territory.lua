-- The territory standing (QG1, step 3 of ut567; Soulwarden protocol notes 4.59, docs/systems/progression-and-skills.md "The territory pools
-- and the tokens", "The card choice", "The card values"; static read ut567/qgplan/t3points/ANSWERS.md): the native part of the
-- ProgressionPointRequestBus methods of the territory pools that territorydatahandler.luac (the map standing list, the card popup
-- territorybonuspopup.luac) and bannertriggers.luac read, the claim rows of the map, the claim keys, and the card choice 4040.
--
-- The data: gamedata/territory.json (tools/convert-territory.sh: the standings with their pools, the cards of each rank row of
-- Territory_Standing, the cards with their values and UI columns). The state: the ranks (slot 65, Swc.owner.ranks; the standing of a
-- territory is the rank of the progression CRC-32 of its id text) and the pools (slot 67, Swc.HostPointPools: the free count of each pool,
-- its category, the levels of the cards of the territory pools).
--
-- The rules [S, the static read]:
--   GetUnspentTokens(progression)        the territory CRC to its pool; the received free count of the pool (not rank minus spent).
--   GetAvailableProgressionPointsData    the cards of the rank row whose index is the spent levels of the pool (the levels of the cards
--     (progression, filter)              with no DoNotSpendPoint), in the row order, no empty id; with the filter, only with a free token
--                                        and room below MaxLevel. The record: id, upgradeValue = card_bonus(level + 1), canUpgrade.
--   GetCurrentProgressionPointsData      each stored card entry of the pool (the filter is not read): upgradeValue = card_bonus(level).
--   canUpgrade                           a free token, the rank of the standing >= RequiredCategoricalProgressionLevel, level + 1 <=
--                                        MaxLevel (not the rank row).
--   The events of a new pools state      for an existing pool: OnUnallocatedProgressionPointsChanged(pool, old, new) when its free count
--                                        changes, then OnProgressionPointsChanged(card, oldLevel, newLevel) for each card whose level
--                                        changes; a new pool and a removed pool send none; the first state sends none.
--   RequestSpendTerritoryStandingPoint   "territory spend PROGRESSION CARD" (4040); no local change: the next points state is the answer.

local Swc = Swc
local T = {}
Swc.territory = T

local D
local function data()
  if D then
    return D
  end
  D = swcui.load_json("gamedata/territory.json") or {standings = {}, rankCards = {}, cards = {}}
  D.byStanding, D.byPool, D.cardsByCrc = {}, {}, {}
  for _, s in ipairs(D.standings or {}) do
    D.byStanding[s.crc] = s
    D.byPool[s.poolCrc] = s
  end
  for _, card in pairs(D.cards or {}) do
    D.cardsByCrc[card.crc] = card
  end
  return D
end
T.data = data

local function standing_of(progression)
  if type(progression) == "string" then
    progression = swcui.crc32(string.lower(progression))
  end
  return data().byStanding[tonumber(progression) or -1]
end

--- The nearest f32 of a number (the client computes the card values in f32).
local function f32(x)
  if x == 0 or x ~= x then
    return x
  end
  local m, e = math.frexp(x)
  local scaled = m * 2 ^ 24
  local whole = math.floor(scaled)
  local rest = scaled - whole
  if rest > 0.5 or (rest == 0.5 and whole % 2 == 1) then
    whole = whole + 1
  end
  return math.ldexp(whole / 2 ^ 24, e)
end
T.f32 = f32

--- The bonus of a card at a level (client 0x5C51100; backend gamedata::card_bonus): r = f64(1.0f - reduction), for i = 1 to level: the
--- step max(f32(r^(i - 1) * initial), minValue); the sum is f32.
local function card_bonus(card, level)
  local ratio = f32(1 - card.reduction)
  local total = 0
  for step = 1, math.min(tonumber(level) or 0, 0xFFFF) do
    local increment = f32(ratio ^ (step - 1) * card.initialBonus)
    total = f32(total + (increment > card.minValue and increment or card.minValue))
  end
  return total
end
T.card_bonus = card_bonus

local function owner()
  return Swc.owner or {}
end

local function free_of(pool)
  return (owner().pools or {})[pool] or 0
end

local function levels_of(pool)
  return (owner().cardLevels or {})[pool] or {}
end

--- The spent levels of a pool (the count helper 0x3CF0600): the levels of its cards with no DoNotSpendPoint.
local function spent_of(pool)
  local n = 0
  for card, level in pairs(levels_of(pool)) do
    local static = data().cardsByCrc[card]
    if not (static and static.doNotSpend) then
      n = n + level
    end
  end
  return n
end

local function standing_rank(standing)
  local ranks = owner().ranks or {}
  local entry = ranks[standing.crc]
  return entry and entry.rank or 0
end

local function record(standing, card, level, next)
  local static = data().cardsByCrc[card]
  local free = free_of(standing.poolCrc)
  local can = static ~= nil and free > 0 and standing_rank(standing) >= (static.requiredRank or 0) and level + 1 <= static.maxLevel
  return {id = card, upgradeValue = static and card_bonus(static, next and level + 1 or level) or 0, canUpgrade = can}
end

function T.available(progression, filter)
  local standing = standing_of(progression)
  if not standing then
    return {}
  end
  local row = data().rankCards[spent_of(standing.poolCrc) % 0x10000 + 1] or {}
  local levels, free, out = levels_of(standing.poolCrc), free_of(standing.poolCrc), {}
  for _, card in ipairs(row) do
    local static = data().cardsByCrc[card]
    local level = levels[card] or 0
    if card ~= 0 and (not filter or (free > 0 and static and level + 1 <= static.maxLevel)) then
      out[#out + 1] = record(standing, card, level, true)
    end
  end
  return out
end

function T.current(progression)
  local standing = standing_of(progression)
  if not standing then
    return {}
  end
  -- The native order is the order of a hash container [I: the client uses the order of the card CRCs].
  local cards = {}
  for card in pairs(levels_of(standing.poolCrc)) do
    cards[#cards + 1] = card
  end
  table.sort(cards)
  local out = {}
  for _, card in ipairs(cards) do
    out[#out + 1] = record(standing, card, levels_of(standing.poolCrc)[card], false)
  end
  return out
end

function T.tokens(progression)
  local standing = standing_of(progression)
  return standing and free_of(standing.poolCrc) or 0
end

local function player()
  return LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.HudComponent.PlayerEntityId")
end

--- The events of a new pools state (swc/owner.lua HostPointPools calls it after it stores the state): only for the territory pools that
--- the earlier state had [I: the client keeps the weapon pools as before: their screens read the state again at OnProgressionPointsChanged
--- of swc/skills.lua].
function T.PoolsChanged(oldPools)
  local o = owner()
  local oldLevels = T.lastLevels or {}
  T.lastLevels = o.cardLevels
  if not oldPools then
    return
  end
  for pool, free in pairs(o.pools or {}) do
    if (o.poolCategories or {})[pool] == (ePoolCategory_Territory or 2) and oldPools[pool] ~= nil then
      if oldPools[pool] ~= free then
        Swc.Notify("ProgressionPointsNotificationBus", player(), "OnUnallocatedProgressionPointsChanged", pool, oldPools[pool], free)
      end
      local before, after = oldLevels[pool] or {}, (o.cardLevels or {})[pool] or {}
      local cards = {}
      for card in pairs(before) do cards[card] = true end
      for card in pairs(after) do cards[card] = true end
      local sorted = {}
      for card in pairs(cards) do sorted[#sorted + 1] = card end
      table.sort(sorted)
      for _, card in ipairs(sorted) do
        if (before[card] or 0) ~= (after[card] or 0) then
          Swc.Notify("ProgressionPointsNotificationBus", player(), "OnProgressionPointsChanged", card, before[card] or 0, after[card] or 0)
        end
      end
    end
  end
end

-- The static data of a point: a territory card (the columns of ProgressionPointData; eTerritoryBonus_* by its category name, the pool
-- category 2); another id keeps the earlier method (swc/skills.lua: the weapon nodes).
local earlierStatic = rawget(Swc.Bus("ProgressionPointRequestBus").Event, "GetStaticProgressionPointData")
Swc.Define("ProgressionPointRequestBus", "Event", {
  GetStaticProgressionPointData = function(owner_, id)
    local card = data().cardsByCrc[tonumber(id) or -1]
    if not card then
      return earlierStatic and earlierStatic(owner_, id) or {id = id or 0, poolCategory = 0}
    end
    return {id = card.crc, poolCategory = ePoolCategory_Territory or 2, territoryBonusCategory = _G["eTerritoryBonus_" .. card.category] or 0,
            requiredPointId = 0, requiredProgressionLevel = card.requiredRank or 0, upgradeCardDescription = card.description,
            upgradeCardSprite = card.sprite, upgradeCardIcon = card.icon, upgradeCardCategory = card.cardCategory,
            upgradeCardStat = card.stat}
  end,
  GetUnspentTokens = function(_, progression) return T.tokens(progression) end,
  GetAvailableProgressionPointsData = function(_, progression, filter) return T.available(progression, filter) end,
  GetCurrentProgressionPointsData = function(_, progression) return T.current(progression) end,
  RequestSpendTerritoryStandingPoint = function(_, progression, card)
    local text = string.format("territory spend %d %d", tonumber(progression) or 0, tonumber(card) or 0)
    Swc.hostRequests[#Swc.hostRequests + 1] = text
    Swc.Note("territory: request " .. text)
  end,
})

-- The claim rows of the map (MapComponentBus GetClaims; the POICapital layout [I]: id, monikerId, settlementId, the name key): the native
-- map builder walks the records of the world map (the static read t3points, question 6), so a row is a standing territory with a territory
-- definition and a shape in the world map data (gamedata/worldmap.json). The DevWorld territories 74 and 75 have standings and
-- definitions but no shape of the world map: no row (the retail standing list has no DevWorld row: Soulwarden qg1 shot 22; the live check
-- t3c showed them before this rule). The offline world has no land claim manager: no owner, no war, no capital icon (compass.luac
-- AddCapitals skips a row with no compassIconPath). The standing list, the NavBar token count, and the token banner read settlementId (the
-- territory id). The id of a row is the actor of the Claim landmark (type 4) of its territory, 32 uppercase hex digits, and worldPosition
-- its place (C-12; static read ut567/qgplan/boards, ANSWERS.md 186 and 188: the town board resolves the destinationOverride of an
-- accepted mission, the Claim landmark actor of the board, against these rows and needs their settlementId).
local CLAIM_LANDMARK = 4
local function map_shapes()
  local ids, marks = {}, {}
  local map = swcui.load_json("gamedata/worldmap.json") or {}
  for _, shape in ipairs(map.shapes or {}) do
    ids[tonumber(shape.id) or -1] = true
  end
  for _, mark in ipairs(map.landmarks or {}) do
    if mark.type == CLAIM_LANDMARK and mark.ids and mark.ids[1] and not marks[mark.ids[1]] then
      marks[mark.ids[1]] = mark
    end
  end
  return ids, marks
end

local function claims()
  if T.claims then
    return T.claims
  end
  local rows = {}
  local shapes, marks = map_shapes()
  for _, s in ipairs(data().standings or {}) do
    local id = tonumber(s.territory)
    local d = id and shapes[id] and TerritoryDefinitionsDataManagerBus.Broadcast.GetTerritoryDefinition(id)
    if d and (d.nameLocalizationKey or "") ~= "" then
      local mark = marks[id]
      local at = mark and mark.position or {0, 0, 0}
      rows[#rows + 1] = {id = mark and string.upper(mark.actor or "") or "", monikerId = id, settlementId = id,
                         nameLocalizationKey = d.nameLocalizationKey, compassIconPath = "", mapIconPath = "", sliceName = "",
                         worldPosition = Vector3(at[1], at[2], at[3])}
    end
  end
  T.claims = rows
  return rows
end
T.claims_rows = claims
Swc.Define("MapComponentBus", "Broadcast", {GetClaims = function() return claims() end})
-- The claim keys of the land claim manager (bannertriggers.luac OnCategoricalProgressionRankChanged matches the progression CRC with the
-- CRC of each key: the territory id): the territories of the claim rows [I: the offline world has no replicated key vector].
Swc.Define("LandClaimRequestBus", "Broadcast", {
  GetClaimKeys = function()
    local keys = {}
    for _, row in ipairs(claims()) do
      keys[#keys + 1] = row.settlementId
    end
    return keys
  end,
})

-- The card popup: the retail dynamic canvas loader loads it when the state Map (2477632187) starts (dynamiccanvasloader.luac canvasInfos:
-- TerritoryBonusPopup, LyShineUI/Territory/TerritoryStanding/TerritoryBonusPopup, triggerStates Map, autohide, POPUP_DRAW_ORDER of
-- CanvasCommon); that loader does not run in the client, so this file loads it in the same way. Its script connects
-- DynamicBus.TerritoryBonusPopupBus (territorystandingspane.luac OnTerritoryClick opens it).
local MAP_STATE = 2477632187
local earlierMapCanvases = Swc.stateCanvases[MAP_STATE]
Swc.stateCanvases[MAP_STATE] = function()
  if earlierMapCanvases then
    earlierMapCanvases()
  end
  if #DynamicBus.TerritoryBonusPopupBus.handlers > 0 then
    return
  end
  local common = RequireScript("LyShineUI._Common.CanvasCommon")
  Swc.LoadScreen("LyShineUI/Territory/TerritoryStanding/TerritoryBonusPopup.uicanvas", true, common and common.POPUP_DRAW_ORDER or 0)
  Swc.Note("territory: the screen TerritoryBonusPopup loads")
end

return T
