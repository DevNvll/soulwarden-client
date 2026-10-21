-- The world map fixture (stage MVP-C, Linear AST-213): the original MagicMap screen (magicmap.luac and its layers) with the
-- map data of swc/worldmap.lua (gamedata/worldmap.json). The player stands in Oxboro; the key M (toggleMapComponent) opens
-- the state Map; the map shows the tiles, the territories, the points of interest, and the shrines; the player marker is at
-- the place of the player. A shrine icon gives the flyout with "Fast Travel"; the button and Yes in the popup give one
-- travel request to the client. M closes the map.
-- The start of a travel closes the open screens (the inventory); a death with the map open gives the respawn screen (the state
-- Respawn closes the others), and its map has the shrines.
-- Pictures: worldmap-open, worldmap-region, worldmap-flyout, worldmap-popup, worldmap-world, worldmap-respawn.
local HUD, MAP = 2702338936, 2477632187
local map = screen("LyShineUI/WorldMap/MagicMap.uicanvas", 20)
local respawn = screen("LyShineUI/Respawn/Respawn.uicanvas", 20)
local flyout = screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 52)
local tooltip = screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
local popup = screen("LyShineUI/Popup/Popup.uicanvas", 60)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)

-- The place of the player: Oxboro (the first test place of the client), heading north-east.
local X, Y, Z = 8880, 4250, 137
Swc.HostView(-45, X, Y, Z)
frames(0.3)
local placeId, placeKey, territoryId, territoryKey = Swc.MapPlaceAt(X, Y)
note(string.format("the place of the player: %d %s in %d %s", placeId, swcui.localize(placeKey), territoryId,
  swcui.localize(territoryKey)))
check(territoryId > 0, "the position of the player is in a territory")
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey"), territoryId,
  "the data layer has the territory of the player")
check(#MapComponentBus.Broadcast.GetFastTravelPointPOIIds() >= 130, "the map has the shrines of the world ("
  .. #MapComponentBus.Broadcast.GetFastTravelPointPOIIds() .. ")")
check(#MapComponentBus.Broadcast.GetTerritories() == 16, "the map has the 16 territories ("
  .. #MapComponentBus.Broadcast.GetTerritories() .. ")")

-- Open: the key M (toggleMapComponent: an original script toggles the state Map).
key("m")
frames(1.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), MAP, "M opens the state Map")
check(swcui.canvas_get(map, "enabled"), "the map screen shows")
check(Swc.HostCursor(), "the map has the cursor")
picture("worldmap-open")

-- The wheel zooms out (magicmap.luac: ui_scroll_down, one zoom level for each step of 120); Space (centerMap) centers
-- the map on the player.
local mapRoot = swcui.canvas_get(map, "root")
local magic = find(mapRoot, "MagicMap")
local function zoom(steps)
  for i = 1, math.abs(steps) do
    move_to(magic, 0.5, 0.5)
    wheel(steps > 0 and 120 or -120)
    frames(0.5)
  end
  key("space")
  frames(1.2)
end
zoom(-3)
picture("worldmap-region")

-- The territory overlays: one for each of the 16 territories.
local territoryText = find(mapRoot, "TerritoryTextLayer")
equal(territoryText and #swcui.children(territoryText) or 0, 16, "the map has one territory overlay for each territory")

-- A shrine on the screen: its icon gives the flyout of the shrine (fasttravelicon.luac).
local layer = find(mapRoot, "FastTravelLayer")
local target, best = nil, nil
local cx, cy = 960, 540
for _, icon in ipairs(layer and swcui.children(layer) or {}) do
  if swcui.enabled_in_hierarchy(icon) then
    local x, y = point_of(icon, 0.5, 0.5)
    local d = math.abs(x - cx) + math.abs(y - cy)
    if x > 300 and x < 1600 and y > 150 and y < 950 and (best == nil or d < best) then
      target, best = icon, d
    end
  end
end
check(target ~= nil, "a shrine icon shows near the player")
if target then
  move_to(target, 0.5, 0.5)
  frames(0.5)
  click(target)
  frames(1.0)
  check(swcui.canvas_get(flyout, "enabled"), "a click on the shrine opens its flyout")
  picture("worldmap-flyout")
  -- The flyout button "Fast Travel" (flyoutrow_button.luac), then Yes in the popup.
  local button
  local function search(id)
    for _, c in ipairs(swcui.children(id)) do
      if button == nil and swcui.enabled_in_hierarchy(c) and swcui.has(c, "text") then
        local layout = swcui.text_layout(c)
        -- The row "Fast Travel" (not the title "Fast Travel Point"); a click on its text goes to the button under it.
        if layout.lines[1] and layout.lines[1].text:upper() == "FAST TRAVEL" then
          button = c
        end
      end
      search(c)
    end
  end
  search(swcui.canvas_get(flyout, "root"))
  check(button ~= nil, "the flyout has the button Fast Travel")
  if button then
    click(button)
    frames(1.0)
    check(swcui.canvas_get(popup, "enabled"), "Fast Travel opens the confirmation popup")
    picture("worldmap-popup")
    equal(#Swc.travelLog, 0, "no travel request before Yes")
    local yes = find(swcui.canvas_get(popup, "root"), "ButtonYes")
    check(yes ~= nil, "the popup has its Yes button")
    if yes then
      click(yes)
      frames(1.0)
    end
    equal(#Swc.travelLog, 1, "Yes: one travel request")
    note("travel request: " .. tostring(Swc.travelLog[1]))
    equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "the travel request closes the map (fasttravelicon.luac)")
    LyShineManagerBus.Broadcast.SetState(MAP)
    frames(1.0)
  end
end

zoom(-6)
picture("worldmap-world")

-- Close: M again.
key("m")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "M closes the map")

-- The start of a travel: the open screens close (the client calls Swc.HostTravelStarts at the loading screen of a travel).
local INVENTORY, RESPAWN = 2972535350, 3901667439
LyShineManagerBus.Broadcast.SetState(INVENTORY)
frames(0.3)
Swc.HostTravelStarts()
frames(0.3)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "the start of a travel closes the inventory")

-- A death with the map open: the respawn screen (the state Respawn closes the map), with the shrines on its map.
key("m")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), MAP, "M opens the map again")
Swc.HostVitals(0, 5330, 100, 100, 100, 100, true)
Swc.HostDeathNotice(5)
frames(6.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), RESPAWN, "the death closes the map: the respawn screen")
check(not swcui.canvas_get(map, "enabled"), "the map screen hides at the death")
-- The shown shrine icons (fasttravelicon.luac) of the respawn screen.
local shownShrines = 0
local function countShrines(id)
  for _, c in ipairs(swcui.children(id)) do
    if swcui.enabled(c) then
      if script_of(c, "fasttravelicon.luac") then
        shownShrines = shownShrines + 1
      end
      countShrines(c)
    end
  end
end
countShrines(swcui.canvas_get(respawn, "root"))
note("shrine icons of the respawn map: " .. shownShrines .. " shown")
check(shownShrines > 0, "the map of the respawn screen shows shrines")
picture("worldmap-respawn")
