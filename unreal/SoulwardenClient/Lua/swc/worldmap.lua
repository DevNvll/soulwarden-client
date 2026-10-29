-- The world map data (stage MVP-C, Linear AST-213; docs/UI.md "The world map"): the native part of the map component
-- (MapComponentBus), of the territory definitions (TerritoryDefinitionsDataManagerBus), of the territory of a position
-- (LandClaimRequestBus), and of the shrine travel (PlayerHousingClientRequestBus). The original scripts of the map
-- (lyshineui/worldmap: magicmap.luac and its layers), of the respawn screen, and of the compass read them.
--
-- The data: gamedata/worldmap.json (tools/convert-ui.sh worldmap; converter swc_convert/worldmap.py): the definitions of
-- the territories, areas, and points of interest (the data tables), their shapes (the polygon prisms of the slices of the
-- territory master slice), and the landmarks of the world (TerritoryLandmarks of the region metadata).
--
-- The rules of the fixed backend (no change of the backend):
-- - The backend has no discovery: each point of interest shows as discovered and charted.
-- - The travel of a shrine is the travel command of the backend (a local travel tool: the admin command "travel" with the
--   place of the backend place list). The backend has no fee, no cooldown, and no condition: the costs are 0 and the check
--   gives Success, except for a dead character (the original result IsDead). The request goes to the client as the host
--   request "travel shrine POI X Y Z" (the landmark position: the client takes the backend place nearest to it).

local data = swcui.load_json("gamedata/worldmap.json")
if not data then
  Swc.Note("no world map data (tools/convert-ui.sh worldmap): the map has no territories and no points of interest")
  data = {definitions = {}, shapes = {}, landmarks = {}}
end

local V3 = function(x, y, z) return Swc.Vector3.new({x = x or 0, y = y or 0, z = z or 0}) end
local LANDMARK_SETTLEMENT, LANDMARK_SHRINE = 1, 27

-- The definitions by id: the columns of the data tables in the names of the native class, and an icon column also with
-- "Path" after it (mapIcon -> mapIconPath), as the scripts read them.
-- The methods of the native class (TerritoryDefinition): HasPoiTag (the POITag column: tags with "," between them).
local Definition = {}
Definition.__index = Definition
function Definition:HasPoiTag(tag)
  local wanted = type(tag) == "number" and tag or swcui.crc32(string.lower(tostring(tag)))
  for part in string.gmatch(tostring(self.pOITag or self.poiTag or ""), "[^,]+") do
    if swcui.crc32(string.lower(part)) == wanted then
      return true
    end
  end
  return false
end

local definitions = {}
for key, row in pairs(data.definitions or {}) do
  local entry = setmetatable({id = tonumber(key), territoryId = tonumber(key)}, Definition)
  for column, value in pairs(row) do
    entry[column] = value
    if column:sub(-4) == "Icon" then
      entry[column .. "Path"] = value
    end
  end
  definitions[entry.id] = entry
end

local emptyDefinition = setmetatable({id = 0, territoryId = 0, nameLocalizationKey = "", isPOI = false, isTerritory = false,
  isArea = false, mapIconPath = "", compassIconPath = "", unchartedIconPath = "", tooltipBackground = "",
  recommendedLevel = 0, maximumLevel = 0, groupSize = 0, gameMode = "", outpostId = "", eventId = ""}, Definition)

local function definition(id)
  return definitions[tonumber(id) or -1]
end

-- The definition of a POI tag (the POITag column; a tag in any case; the first definition with the tag, by id).
local byPoiTag
local function definitionOfTag(tag)
  if byPoiTag == nil then
    byPoiTag = {}
    local ids = {}
    for id in pairs(definitions) do
      ids[#ids + 1] = id
    end
    table.sort(ids)
    for _, id in ipairs(ids) do
      for part in string.gmatch(tostring(definitions[id].pOITag or ""), "[^,]+") do
        local key = string.lower((part:gsub("^%s+", ""):gsub("%s+$", "")))
        byPoiTag[key] = byPoiTag[key] or definitions[id]
      end
    end
  end
  return byPoiTag[string.lower(tostring(tag or ""))]
end

-- The shapes: the polygon of each territory, area, and point of interest (world meters), with its box and its area.
local shapes, shapeById = {}, {}
for _, s in ipairs(data.shapes or {}) do
  local poly = s.polygon
  local area = 0
  for i = 1, #poly do
    local a, b = poly[i], poly[i % #poly + 1]
    area = area + a[1] * b[2] - b[1] * a[2]
  end
  local shape = {id = s.id, polygon = poly, min = s.min, max = s.max, position = s.position, area = math.abs(area) / 2}
  shapes[#shapes + 1] = shape
  shapeById[s.id] = shapeById[s.id] or shape
end

local function inside(shape, x, y)
  if x < shape.min[1] or x > shape.max[1] or y < shape.min[2] or y > shape.max[2] then
    return false
  end
  local poly, hit = shape.polygon, false
  local j = #poly
  for i = 1, #poly do
    local xi, yi, xj, yj = poly[i][1], poly[i][2], poly[j][1], poly[j][2]
    if (yi > y) ~= (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi then
      hit = not hit
    end
    j = i
  end
  return hit
end

local function xy(position)
  if position == nil then
    return nil
  end
  return tonumber(position.x) or 0, tonumber(position.y) or 0
end

--- @return The shapes that contain a position, the smallest first.
local function containing(position)
  local x, y = xy(position)
  local out = {}
  if x == nil then
    return out
  end
  for _, shape in ipairs(shapes) do
    if inside(shape, x, y) then
      out[#out + 1] = shape
    end
  end
  table.sort(out, function(a, b) return a.area < b.area end)
  return out
end

local function territoryAt(position)
  for _, shape in ipairs(containing(position)) do
    local d = definition(shape.id)
    if d and d.isTerritory == true then
      return shape.id
    end
  end
  return 0
end

-- The landmarks: the type, the ids of the territories that contain it, and the world position.
local landmarks = {}
for _, mark in ipairs(data.landmarks or {}) do
  local poi = 0
  for _, id in ipairs(mark.ids) do
    local d = definition(id)
    if d and d.isPOI == true then
      poi = id
    end
  end
  landmarks[#landmarks + 1] = {type = mark.type, ids = mark.ids, poi = poi,
    position = V3(mark.position[1], mark.position[2], mark.position[3])}
end

local function landmarkObject(mark)
  return {worldPosition = mark.position, position = mark.position, landmarkType = mark.type, type = mark.type,
    territoryId = mark.ids[1] or 0, poiId = mark.poi, landmarkData = 0, index = mark.poi}
end

local function hasId(mark, id)
  for _, other in ipairs(mark.ids) do
    if other == id then
      return true
    end
  end
  return false
end

--- A point of interest of the lists: its definition, its place, and the discovery state (all discovered: no discovery).
local function poiObject(id, position)
  local d = definition(id) or emptyDefinition
  local out = {}
  for k, v in pairs(d) do
    out[k] = v
  end
  out.id = id
  out.position = position
  out.worldPosition = position
  out.isDiscovered = true
  out.isCharted = true
  return out
end

local simplePOIs, settlements, shrineIds = nil, nil, nil
local function lists()
  if simplePOIs then
    return
  end
  simplePOIs, settlements, shrineIds = {}, {}, {}
  local seen = {}
  for _, shape in ipairs(shapes) do
    local d = definition(shape.id)
    if d and d.isPOI == true and not seen[shape.id] then
      seen[shape.id] = true
      simplePOIs[#simplePOIs + 1] = poiObject(shape.id, V3(shape.position[1], shape.position[2], shape.position[3]))
    end
  end
  local shrineSeen, settlementSeen = {}, {}
  for _, mark in ipairs(landmarks) do
    if mark.type == LANDMARK_SHRINE and mark.poi ~= 0 and not shrineSeen[mark.poi] then
      shrineSeen[mark.poi] = true
      shrineIds[#shrineIds + 1] = mark.poi
    elseif mark.type == LANDMARK_SETTLEMENT and mark.poi ~= 0 and not settlementSeen[mark.poi] then
      settlementSeen[mark.poi] = true
      settlements[#settlements + 1] = poiObject(mark.poi, mark.position)
    end
  end
  table.sort(shrineIds)
end

local function territoryList()
  local out = {}
  for _, shape in ipairs(shapes) do
    local d = definition(shape.id)
    if d and d.isTerritory == true then
      local w, h = shape.max[1] - shape.min[1], shape.max[2] - shape.min[2]
      out[#out + 1] = {id = shape.id, territoryName = d.nameLocalizationKey or "", width = w, height = h,
        position = V3((shape.min[1] + shape.max[1]) / 2, (shape.min[2] + shape.max[2]) / 2, 0)}
    end
  end
  table.sort(out, function(a, b) return a.id < b.id end)
  return out
end

local function shrineMark(poi)
  for _, mark in ipairs(landmarks) do
    if mark.type == LANDMARK_SHRINE and mark.poi == poi then
      return mark
    end
  end
  return nil
end

Swc.Define("MapComponentBus", "Broadcast", {
  GetSimplePOIs = function() lists() return simplePOIs end,
  GetTerritories = function() return territoryList() end,
  GetSettlements = function() lists() return settlements end,
  GetFastTravelPointPOIIds = function() lists() return shrineIds end,
  GetAllLandmarksByType = function(id, kind)
    local out = {}
    for _, mark in ipairs(landmarks) do
      if mark.type == kind and hasId(mark, id) then
        out[#out + 1] = landmarkObject(mark)
      end
    end
    return out
  end,
  GetFirstLandmarkByType = function(id, kind)
    for _, mark in ipairs(landmarks) do
      if mark.type == kind and hasId(mark, id) then
        return landmarkObject(mark)
      end
    end
    return {worldPosition = V3(0, 0, 0), position = V3(0, 0, 0), landmarkType = kind, type = kind, territoryId = 0,
      poiId = 0, landmarkData = 0, index = 0}
  end,
  GetLandmarksInTerritory = function(id)
    local out = {}
    for _, mark in ipairs(landmarks) do
      if hasId(mark, id) then
        out[#out + 1] = landmarkObject(mark)
      end
    end
    return out
  end,
  GetContainingTerritories = function(position)
    local out = {}
    for _, shape in ipairs(containing(position)) do
      out[#out + 1] = shape.id
    end
    return out
  end,
  GetContainingTerritory = function(position) return territoryAt(position) end,
  GetSmallestContainingTerritory = function(position)
    local all = containing(position)
    return all[1] and all[1].id or 0
  end,
  WorldPosToTerritoryLoRes = function(position) return territoryAt(position) end,
  GetTerritoryPosition = function(id)
    local shape = shapeById[tonumber(id) or -1]
    if not shape then
      return V3(0, 0, 0)
    end
    return V3((shape.min[1] + shape.max[1]) / 2, (shape.min[2] + shape.max[2]) / 2, shape.position[3])
  end,
  GetPOINameAtPosition = function(position)
    for _, shape in ipairs(containing(position)) do
      local d = definition(shape.id)
      if d and d.isPOI == true and (d.nameLocalizationKey or "") ~= "" then
        return d.nameLocalizationKey
      end
    end
    return ""
  end,
  GetTerritoryZPosFromWorldPos = function(position) return position and tonumber(position.z) or 0 end,
  -- Outposts, claims, NPC locations, and dynamic objectives: the backend has none of these systems.
  GetOutposts = function() return {} end,
  GetClaims = function() return {} end,
  -- The NPC data of the map (compass.luac OnNpcHasAvailableQuestChanged indexes factionInfoTable with its factionType unless it is None
  -- or Any; AST-239): no faction for the persons of this backend [I].
  GetNpcData = function() return {factionType = eFactionType_None or 0} end,
  GetTractAtPosition = function() return "" end,
  IsDynamicPoiObjectiveComplete = function() return false end,
  HasDynamicPoiObjective = function() return false end,
  GetMedianPoiLevel = function(id)
    local d = definition(id)
    return d and tonumber(d.recommendedLevel) or 0
  end,
})

Swc.Define("TerritoryDefinitionsDataManagerBus", "Broadcast", {
  GetTerritoryDefinition = function(id) return definition(id) or emptyDefinition end,
  -- The quest texts and the mission places (objectivesdatahandler.luac, territorydatahandler.luac: poiTagsOverride[1]); the quest pins
  -- (stage Q2) use the same tags.
  GetTerritoryDefinitionByPoiTag = function(tag) return definitionOfTag(tag) or emptyDefinition end,
  GetTerritoryIdByPoiTag = function(tag)
    local d = definitionOfTag(tag)
    return d and d.id or 0
  end,
})

-- Wars, races, raids, outposts, claims, and influence towers: the backend has none of these systems.
-- An influence race of a territory: none (companiesatwarlist.luac GetConflicts calls IsValid on the result for each territory; the
-- stub value nil gave "attempt to index a nil value" at each open of the map: AST-289).
local noRace = {
  IsValid = function() return false end,
  IsActive = function() return false end,
  IsUpcoming = function() return false end,
}
-- The phases of a war: no war in the offline world. mapfortressinfomenu.luac reads the Conquest duration when the Social bus is ready
-- (GetWarPhaseDuration(eWarPhase_Conquest):ToHours(); a nil gave a Lua error at the Social state of QG7, ut567 2a). [I] 0 hours.
Swc.Define("WarRequestBus", "Broadcast", {GetWarPhaseDuration = function() return Swc.Duration.new({seconds = 0}) end})
Swc.Define("WarDataServiceBus", "Broadcast", {
  GetWars = function() return {} end,
  GetWarsForGuild = function() return {} end,
  GetRaceForTerritory = function() return noRace end,
})
Swc.Define("RaidSetupRequestBus", "Broadcast", {
  GetSignedUpTerritories = function() return {} end,
})
Swc.Define("ObjectiveInteractorRequestBus", "Broadcast", {
  GetOutpostDestinations = function() return {} end,
  GetClaimDestinations = function() return {} end,
  GetInfluenceTowerDestinations = function() return {} end,
  UpdateLandmarkDataAvailability = function() end,
})

-- No dungeon and no expedition (the map menu shows "Leave" and "Abandon Activity" only in a dungeon game mode).
Swc.Define("GameModeParticipantComponentRequestBus", "Event", {
  GetCurrentDungeonGameModeId = function() return 0 end,
  GetCurrentDungeonGameModeMutation = function() return 0 end,
})
Swc.Define("PlayerComponentRequestsBus", "Event", {
  IsCrossWorldLogin = function() return false end,
})

-- No company owns a territory (the backend has no companies and no claims): the owner has an invalid guild id.
local noGuild = {IsValid = function() return false end, ToString = function() return "" end}
local noOwner = {guildId = noGuild, guildName = "", faction = eFactionType_None or 0, isClaimed = false}

Swc.Define("LandClaimRequestBus", "Broadcast", {
  GetTerritoryIdForWorldPos = function(position) return territoryAt(position) end,
  GetClaimOwnerData = function() return noOwner end,
  -- No governance (no company, no upkeep, no taxes).
  GetTerritoryGovernanceData = function() return {failedToPayUpkeep = false, taxRate = 0, tradingTax = 0,
    craftingFee = 0, refiningFee = 0, housingTax = 0} end,
  GetTerritoryDarknessThreshold = function() return 0 end,
  GetTerritoryConflictFaction = function() return eFactionType_None or 0 end,
  GetTerritoryFactionInfluencePercentages = function() return {} end,
})

-- The shrine travel: the original flow of fasttravelicon.luac (the flyout, the Yes/No popup, RequestFastTravelToTerritory)
-- with the rules of the backend (no fee, no cooldown, no condition but life).
local function travelCheck()
  if LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Vitals.IsDead") == true then
    return eCanFastTravelToSettlementResults_IsDead or 5
  end
  return eCanFastTravelToSettlementResults_Success or 0
end
local function costs()
  return {baseCost = 0, distanceCost = 0, encumbranceCost = 0, cost = 0, baseCostDiscount = 0, distanceCostDiscount = 0,
    encumbranceCostDiscount = 0, currentTerritoryDiscount = 0}
end
local noCosts = costs()
-- The value class of the costs (fasttravelicon.luac makes one: FastTravelCosts()).
FastTravelCosts = costs

-- No mission and no timed course stops a travel (the backend has no quests).
Swc.Define("ObjectivesComponentRequestBus", "Event", {
  GetNumObjectivesCannotFastTravel = function() return 0 end,
  GetActiveTimedQuickCourseId = function() return 0 end,
})

Swc.travelLog = {}   -- the travel requests of the scripts (the tests read it)

Swc.Define("PlayerHousingClientRequestBus", "Broadcast", {
  CanFastTravelToTerritoryLandmark = function() return travelCheck() end,
  CanFastTravelToTerritory = function() return travelCheck() end,
  HasFastTravelPointInTerritory = function() return true end,
  GetFastTravelToTerritoryLandmarkCost = function() return 0 end,
  GetFastTravelToTerritoryLandmarkCosts = function() return noCosts end,
  GetFastTravelToTerritoryFactionDiscountPct = function() return 0 end,
  GetFastTravelToTerritoryCompanyDiscountPct = function() return 0 end,
  GetFastTravelToTerritoryAttributeDiscountPct = function() return 0 end,
  GetFastTravelFromFieldCostMultiplier = function() return 1 end,
  IsFastTravelFromFieldAllowed = function() return true end,
  IsInFieldForFastTravel = function() return false end,
  GetFastTravelToTerritoryIdByPosition = function(position) return territoryAt(position) end,
  -- The houses of the player: none (the backend has no housing). bio.luac SetupBonuses (the bio tab of the Skills screen) takes the
  -- length of the list; the stub nil gave "attempt to get length of a nil value" (user test 2026-10-26, 8d).
  GetOwnedHouseData = function() return {} end,
  RequestFastTravelToTerritory = function(poi)
    local mark = shrineMark(tonumber(poi) or -1)
    if not mark then
      Swc.Note("travel: no shrine landmark for the point of interest " .. tostring(poi))
      return
    end
    local p = mark.position
    local request = string.format("travel shrine %d %.3f %.3f %.3f", mark.poi, p.x, p.y, p.z)
    Swc.travelLog[#Swc.travelLog + 1] = request
    Swc.hostRequests[#Swc.hostRequests + 1] = request
  end,
})

-- The territory data of the client is ready (the native land claim manager): the map makes its territory overlays.
Swc.SetData("Hud.LocalPlayer.IsLandClaimManagerAvailable", true)

-- The territory of the player: the native client updates these nodes as the player moves (the compass, the map frame,
-- the banners read them). The host calls Swc.MapPosition with each new position (swc/host.lua Swc.HostView).
local current = {smallest = -1, territory = -1}
function Swc.MapPosition(x, y, z)
  local position = V3(x, y, z)
  local all = containing(position)
  local smallest = all[1] and all[1].id or 0
  local territory = territoryAt(position)
  if smallest ~= current.smallest then
    current.smallest = smallest
    Swc.SetData("Hud.LocalPlayer.CurrentAreaTerritory.SmallestContainingId", smallest)
  end
  if territory ~= current.territory then
    if current.territory > 0 then
      Swc.SetData("Hud.LocalPlayer.ExitedTerritoryId", current.territory)
    end
    current.territory = territory
    Swc.SetData("Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey", territory)
    if territory > 0 then
      Swc.SetData("Hud.LocalPlayer.EnteredTerritoryId", territory)
    end
  end
end

--- @return The id and the name key of the smallest area or point of interest at a position, and of its territory (the tests).
function Swc.MapPlaceAt(x, y)
  local all = containing(V3(x, y, 0))
  local d = all[1] and definition(all[1].id)
  local t = definition(territoryAt(V3(x, y, 0)))
  return all[1] and all[1].id or 0, d and d.nameLocalizationKey or "", t and t.id or 0, t and t.nameLocalizationKey or ""
end
