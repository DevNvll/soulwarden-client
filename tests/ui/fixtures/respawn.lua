-- The respawn fixture: the original Respawn screen (respawn.luac, its world map) through the host calls of the client.
-- The vitals give the dead mark (Hud.LocalPlayer.Vitals.IsDead), a death notice gives the time to the respawn screen
-- (Hud.LocalPlayer.OnPlayerDeath, message 3950): the script queues the state "respawn" and shows the list with the one
-- row "Random Location" (no home point). A click on RESPAWN asks the client for a respawn (LocalPlayerUIRequestsBus
-- RequestRespawn: random, no home id). The loading screen of the death (eLoadingScreenIds_DEATH) shows from the request
-- to message 3954; the end of the death (the revive) and the end of the loading screen give the HUD again.
-- Pictures: respawn-list, respawn-loading.
local RESPAWN, HUD = 3901667439, 2702338936
local respawn = screen("LyShineUI/Respawn/Respawn.uicanvas", 41)
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
