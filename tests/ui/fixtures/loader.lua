-- The loader fixture: the screen loader of the game (uiloader.luac; the globals canvas starts it) at a world entry,
-- against the lists of the converter (tools/convert-ui.sh loader: gamedata/loader.json, the canvas paths in the string
-- constants of uiloader.luac and dynamiccanvasloader.luac). The fixture allows two screens of the loader: a HUD screen
-- (Vitals2024) and a menu (InfoModal); the other ones are load failures, as for a screen of a later stage. Checks: the
-- loader asks only for canvases of its list, one time each, with the ending .uicanvas; it counts each load and each
-- failure and sets the state HUD; a HUD screen loads with no autohide and shows in the HUD state, a menu loads with
-- autohide and stays hidden; each canvas that the game has for the two lists is converted.
local HUD = 2702338936
local lists, err = swcui.load_json("gamedata/loader.json")
check(lists ~= nil, "the converter wrote the loader lists (tools/convert-ui.sh loader): " .. tostring(err))
lists = lists or {screens = {}, screensNotInGame = {}, screensNotConverted = {}, dynamic = {}, dynamicNotConverted = {}}

local listed = {}
for _, path in ipairs(lists.screens) do
  listed[path] = true
end
local function key_of(path)
  return (string.lower(tostring(path)):gsub("\\", "/"):gsub("%.dynamicuicanvas$", ""):gsub("%.uicanvas$", ""))
end

-- Each request of the loader (LyShineScriptBindRequestBus ScriptLoadCanvas -> Swc.LoadScreen).
local requests, seen, twice, endings = {}, {}, 0, 0
local loadScreen = Swc.LoadScreen
Swc.LoadScreen = function(path, autohide, drawOrder)
  local key = key_of(path)
  requests[#requests + 1] = {key = key, autohide = autohide, drawOrder = drawOrder}
  if seen[key] then
    twice = twice + 1
  end
  seen[key] = true
  if string.find(string.lower(tostring(path)), "%.uicanvas$") then
    endings = endings + 1
  end
  return loadScreen(path, autohide, drawOrder)
end

local hudScreen = "lyshineui/hud2024/vitals/vitals2024"
local menuScreen = "lyshineui/infomodal/infomodal"
allow(hudScreen)
allow(menuScreen)
local loader = g_cachedScripts and g_cachedScripts["lyshineui.uiloader"]
check(loader ~= nil, "the globals script has the loader module")

Swc.EnterWorld("newworld_vitaeeterna")
local waited = 0
while LyShineManagerBus.Broadcast.GetCurrentState() ~= HUD and waited < 30 do
  frames(0.5)
  waited = waited + 0.5
end
equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "the loader sets the state HUD after its screens")
note(string.format("the loader asked for %d canvases in %.1f s (%d skipped by this fixture)", #requests, waited,
  Swc.SkippedScreenCount()))
if loader then
  check(loader.numScreensToPreload ~= nil and loader.numScreensLoaded == loader.numScreensToPreload,
    "the loader counted each screen: " .. tostring(loader.numScreensLoaded) .. " of " .. tostring(loader.numScreensToPreload))
  check(Swc.Call(loader.IsLoadingComplete, loader) ~= false, "the loader reports the end of the load")
end

local unknown = {}
for _, r in ipairs(requests) do
  if not listed[r.key] then
    unknown[#unknown + 1] = r.key
  end
end
check(#requests > 50, "the loader asks for the screens of the world (" .. #requests .. ")")
equal(#unknown, 0, "canvases that are not in the list of the loader" .. (unknown[1] and (" (" .. unknown[1] .. ")") or ""))
equal(twice, 0, "canvases that the loader asks for two times")
equal(endings, #requests, "requests with the ending .uicanvas")

-- The allowed screens: the flags of the loader and the state.
local function screen_of(key)
  for _, s in pairs(Swc.screens) do
    if key_of(s.path) == key then
      return s
    end
  end
end
local hud, menu = screen_of(hudScreen), screen_of(menuScreen)
check(hud ~= nil and menu ~= nil, "the two allowed screens loaded")
if hud then
  equal(hud.autohide, false, "the HUD screen has no autohide")
  check(swcui.canvas_get(hud.canvas, "enabled"), "the HUD screen shows in the HUD state")
end
if menu then
  equal(menu.autohide, true, "the menu has autohide")
  check(not swcui.canvas_get(menu.canvas, "enabled"), "the menu is hidden in the HUD state")
  LyShineManagerBus.Broadcast.SetState(Math.CreateCrc32("InfoModal"))
  frames(0.2)
  check(swcui.canvas_get(menu.canvas, "enabled"), "the menu shows in its state")
  LyShineManagerBus.Broadcast.ExitState(Math.CreateCrc32("InfoModal"))
  frames(0.2)
  check(not swcui.canvas_get(menu.canvas, "enabled"), "the menu hides when its state ends")
end

-- The lists against the game data: each canvas that the game has is converted (the paths with no canvas in the game
-- fail in the retail client too).
equal(#lists.screensNotConverted, 0, "screens of the loader that are not converted")
equal(#lists.dynamic > 0 and #lists.dynamicNotConverted or -1, 0, "canvases of the dynamic canvas loader that are not converted")
note(string.format("lists: %d screens (%d with no canvas in the game: %s), %d dynamic canvases", #lists.screens,
  #lists.screensNotInGame, table.concat(lists.screensNotInGame, ", "), #lists.dynamic))
