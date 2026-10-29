-- The map settlements fixture (the map settlements item of ut567; static read ut567/qgplan/mapsettle; retail Soulwarden
-- quests/qg1/implement/live/22-map-standing-after.png): ObjectiveInteractorRequestBus GetOutpostDestinations of swc/worldmap.lua (the
-- landmarks of type 1 Settlement and 22 Outpost with a prism; the record MissionDestinationData: id = the actor id, territoryId, worldPosition,
-- nameLocalizationKey), the empty claim destinations, and the original map layers: settlementiconslayer.luac makes the Windsward
-- settlement icon, and its collapsible settlement icon (collapsiblesettlementicon.luac FillLandmarks) makes the icons of the landmarks of
-- Windsward (the crafting stations by their landmark data "Smelting5" and others, the inn, the trading post, the storage).
-- Needs gamedata/worldmap.json with the landmark actor ids and data texts (tools/convert-ui.sh worldmap after the change of this item).
-- Picture: mapsettlements-windsward.
local HUD, MAP = 2702338936, 2477632187
local map = screen("LyShineUI/WorldMap/MagicMap.uicanvas", 20)
screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 52)
screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
-- The player in Windsward, near Saatvik Agrawal.
Swc.HostView(0, 9348.5, 2756, 87.6)
frames(0.3)
local errorsBefore = swcui.error_count()

local destinations = ObjectiveInteractorRequestBus.Broadcast.GetOutpostDestinations()
local windsward
for _, d in ipairs(destinations) do
  if d.territoryId == 6 then windsward = d end
end
if not (windsward and windsward.id ~= "" and windsward.id ~= nil) then
  note("worldmap.json has no landmark actor ids (the converter of the map settlements item): the settlement checks are skipped")
  equal(swcui.error_count() - errorsBefore, 0, "no Lua error with the old map data")
  return
end

-- 1. The records (the expected native record of the static read: id 2BB560AA..., territory 6, 9349.5556 2702.2458, @Windsward).
equal(windsward.id, "2BB560AA58EB88810000000000000000", "Windsward: the actor id of the settlement landmark (32 hex digits)")
equal(windsward.nameLocalizationKey, "@Windsward", "Windsward: the name key of territory 6")
near(windsward.worldPosition.x, 9349.556, 0.01, "Windsward: x")
near(windsward.worldPosition.y, 2702.246, 0.01, "Windsward: y")
local settlements, outposts = 0, 0
for _, d in ipairs(destinations) do
  if TerritoryDefinitionsDataManagerBus.Broadcast.GetTerritoryDefinition(d.territoryId).isTerritory then
    settlements = settlements + 1
  else
    outposts = outposts + 1
  end
end
note(string.format("the destinations: %d settlement(s), %d outpost(s)", settlements, outposts))
equal(settlements, 13, "13 settlements (the landmarks of type 1)")
check(outposts <= 9, "at most 9 outposts (the landmarks of type 22 with a prism)")
equal(#ObjectiveInteractorRequestBus.Broadcast.GetClaimDestinations(), 0, "no claim destination (no land claim manager)")

-- 2. The map: the settlement icons and the landmark icons of Windsward.
key("m")
frames(2.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), MAP, "M opens the state Map")
local layer
for _, list in pairs(Swc.components) do
  for _, instance in ipairs(list) do
    if string.find(instance.__swcScript or "", "settlementiconslayer.luac", 1, true) and instance.settlementIcons then
      layer = instance
    end
  end
end
check(layer ~= nil, "the settlement icons layer runs")
local icon
if layer then
  note(string.format("the settlement icons: %d", layer.iconCount or 0))
  equal(layer.iconCount, #destinations, "one settlement icon for each destination")
  for i = 1, layer.iconCount or 0 do
    local each = layer.settlementIcons[i]
    if each and each.iconData and each.iconData.territoryId == 6 then icon = each end
  end
end
check(icon ~= nil, "the Windsward settlement icon")
if icon then
  equal(icon.iconType, layer.iconTypes.Settlement, "the Windsward icon is a Settlement icon (territory 6 isTerritory)")
  -- The landmarks of the icon (FillLandmarks: self.landmarks, the landmarks near the settlement with a static record).
  local count, names = 0, {}
  for _, entry in ipairs(icon.landmarks or {}) do
    count = count + 1
    local data = entry.staticData or {}
    names[#names + 1] = tostring(data.name or data.title or data.text or "?")
  end
  note(string.format("the Windsward landmark icons: %d (%s)", count, table.concat(names, ", ")))
  check(count > 0, "the Windsward settlement icon has landmark icons (stations, inn, trading post)")
  -- The drawn state (the live check msa had icons that were never drawn): the filters exist before the settlement icons (the landmark
  -- data is ready after PlayerMapFiltersHandler InitFilters), the Stations filter is on, and at this zoom the icon shows its station
  -- layer with its station icons.
  local filters = RequireScript("LyShineUI.WorldMap.PlayerMapFiltersHandler")
  check(filters:GetFilterEnabled("Stations") == true and filters:GetFilterEnabled("Settlements") == true, "the Stations and Settlements filters are on")
  check(swcui.enabled_in_hierarchy(icon.entityId), "the Windsward icon is shown")
  note(string.format("the zoom %s, at the station zoom %s", tostring(icon.zoomLevel), tostring(icon.atStationZoom)))
  check(icon.atStationZoom == true and UiElementBus.Event.IsEnabled(icon.Properties.StationsLayer), "the station layer of Windsward is shown")
  local shown = 0
  for _, child in ipairs(UiElementBus.Event.GetChildren(icon.Properties.StationsLayer) or {}) do
    if swcui.enabled_in_hierarchy(child) then shown = shown + 1 end
  end
  note("the shown station icons of Windsward: " .. shown)
  check(shown >= 10, "the station icons of Windsward are shown (" .. shown .. ")")
end
picture("mapsettlements-windsward")
key("m")
frames(1.0)
equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
