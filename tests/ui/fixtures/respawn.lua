-- The respawn fixture: the original Respawn screen (respawn.luac, its world map) through the host calls of the client.
-- The vitals give the dead mark (Hud.LocalPlayer.Vitals.IsDead), a death notice gives the time to the respawn screen
-- (Hud.LocalPlayer.OnPlayerDeath, message 3950): the script queues the state "respawn" and shows the list with the one
-- row "Random Location" (no home point). A drag moves the world map. Escape opens the escape menu of the respawn state
-- and Escape closes it. A click on RESPAWN asks the client for a respawn (LocalPlayerUIRequestsBus RequestRespawn:
-- random, no home id). The loading screen of the death (eLoadingScreenIds_DEATH) shows from the request to message
-- 3954; the end of the death (the revive) and the end of the loading screen give the HUD again.
-- Pictures: respawn-list, respawn-drag, respawn-loading.
local RESPAWN, HUD, RESPAWN_ESC = 3901667439, 2702338936, 921475099
local respawn = screen("LyShineUI/Respawn/Respawn.uicanvas", 20)
local menu = screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 41)
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.3)
-- The UI loader ends (respawn.luac registers its death observers at OnUiLoadingComplete).
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
frames(0.2)

-- The death: the dead mark, then the death notice with 5 s to the respawn screen.
Swc.HostVitals(0, 5330, 100, 100, 100, 100, true)
Swc.HostDeathNotice(5)
frames(0.5)
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Vitals.IsDead"), true, "the data layer has the dead mark")
frames(5.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), RESPAWN, "the death queues the state of the respawn screen")
check(swcui.canvas_get(respawn, "enabled"), "the respawn screen shows")
check(Swc.HostCursor(), "the respawn screen has the cursor")
local root = swcui.canvas_get(respawn, "root")
local list = find(root, "RespawnMapListContent")
local rows = {}
local function texts(id)
  for _, c in ipairs(swcui.children(id)) do
    if swcui.enabled_in_hierarchy(c) and swcui.has(c, "text") then
      local layout = swcui.text_layout(c)
      if layout.lines[1] and layout.lines[1].text ~= "" then
        rows[#rows + 1] = layout.lines[1].text
      end
    end
    texts(c)
  end
end
if list then
  texts(list)
end
check(table.concat(rows, "|"):find(swcui.localize("@ui_respawn_random_location") ~= "@ui_respawn_random_location"
  and swcui.localize("@ui_respawn_random_location") or "Random Location", 1, true) ~= nil,
  "the list has the row of a random location (" .. table.concat(rows, "|") .. ")")
picture("respawn-list")
-- The screen at the window size of the retail picture of the death (the Soulwarden evidence, fight L2-death-screen-time-0: 1256 x
-- 660; stage P3.H).
swcui.set_viewport(1256, 660)
frames(0.3)
picture("respawn-1256")
swcui.set_viewport(1920, 1080)
frames(0.3)

-- A drag of the world map (magicmap.luac on the UiScrollBox MagicMap): the content follows the cursor, the script sees
-- OnScrollOffsetChanging (isDragging) and, at the release, OnScrollOffsetChanged (the end of the drag).
local map = find(root, "MagicMap")
check(map ~= nil and Swc.IState(map).scrollBox ~= nil, "the respawn screen has the scroll box of the world map")
if map then
  local mapScript = script_of(map, "magicmap.luac")
  local before = UiScrollBoxBus.Event.GetScrollOffset(map)
  local dragging = false
  move_to(map, 0.5, 0.5)
  Swc.HostInput("button", "left", true)
  for i = 1, 12 do
    move_to(map, 0.5 - 0.15 * i / 12, 0.5 - 0.1 * i / 12)
    frames(1 / 60)
    dragging = dragging or (mapScript and mapScript.isDragging == true)
  end
  local during = UiScrollBoxBus.Event.GetScrollOffset(map)
  Swc.HostInput("button", "left", false)
  frames(0.1)
  local after = UiScrollBoxBus.Event.GetScrollOffset(map)
  local x0, y0 = point_of(map, 0.5, 0.5)
  local x1, y1 = point_of(map, 0.35, 0.4)
  near(during.x - before.x, x1 - x0, 2, "the map content follows the drag in x")
  near(during.y - before.y, y1 - y0, 2, "the map content follows the drag in y")
  check(dragging, "the map script sees the drag (OnScrollOffsetChanging)")
  check(mapScript and mapScript.isDragging == false, "the release ends the drag of the map (OnScrollOffsetChanged)")
  note(string.format("map offset %.0f %.0f, after the release %.0f %.0f", during.x, during.y, after.x, after.y))
  picture("respawn-drag")
end

-- Escape on the respawn screen: respawn.luac ui_cancel toggles the state of the escape menu of the respawn state
-- (921475099); Escape again toggles it off, back to the respawn screen (not to the HUD).
key("escape")
frames(0.6)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), RESPAWN_ESC, "Escape on the respawn screen: the escape menu state")
check(swcui.canvas_get(menu, "enabled"), "the escape menu shows")
check(swcui.canvas_get(respawn, "enabled"), "the respawn screen stays under the escape menu")
check(Swc.HostCursor(), "the escape menu has the cursor")
key("escape")
frames(0.6)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), RESPAWN, "Escape again: back to the respawn screen")
check(swcui.canvas_get(respawn, "enabled") and not swcui.canvas_get(menu, "enabled"), "the respawn screen shows, the menu hides")

-- The respawn request: the script shows the respawn button 2 s after the map (TimingUtils Delay).
frames(3)
local button = find(root, "RespawnMapButtonAccept")
check(button ~= nil and swcui.enabled_in_hierarchy(button), "the screen shows its respawn button")
if button then
  check(script_of(button).mIsEnabled ~= false, "the respawn button is on")
  click(button)
  frames(0.3)
end
local request = Swc.TakeHostRequest()
equal(request, "respawn 1 ", "RESPAWN asks the client for a respawn at a random location")

-- The client: the loading screen of the death from the request to message 3954, then the revive.
Swc.HostLoading(true, "newworld_vitaeeterna", eLoadingScreenIds_DEATH)
frames(0.5)
check(Swc.loading.shown, "the loading screen of the death shows")
picture("respawn-loading")
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostLoading(false)
frames(1.0)
check(not swcui.canvas_get(respawn, "enabled"), "the respawn screen hides after the respawn")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "the HUD after the respawn")
check(not Swc.HostCursor(), "no cursor in the HUD")

-- The respawn screen of this fixture sends no camera request.
camera_clean("the respawn screen")
