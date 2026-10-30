-- The entrance window of a story instance (C-13 step 1b part B of ut567; the static read work/ut567/c13read/NOTES.md): the original
-- DungeonEnterScreen (dungeonenterscreen.luac, state 4119896358) for the entrance of QuestMedea (the member entrance of the object: the
-- mode 95B632E9). The use of OpenDungeonMenu (action 71) opens it (Swc.HostDungeon); its Enter sends "dungeon enter" (72 with the base
-- action 71); the result 3094 (OnDungeonActivateResult): EntranceBusy (17) tries again, Success (0) closes with no notice, a failure (10)
-- closes with its notice; the close ends the use ("dungeon end").
local HUD, ENTRANCE = 2702338936, 4119896358
local QUEST_MEDEA = 0x95B632E9
allow("LyShineUI/Dungeons/DungeonEnterScreen.dynamicuicanvas")
screen("LyShineUI/Notifications/Notification.uicanvas", 50)
screen("LyShineUI/Popup/Popup.uicanvas", 52)
screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(1830, 1830, 100, 100, 100, 100, false)
Swc.HostProgression(19, 0)
Swc.HostView(-45, 8513, 2660, 56)
frames(0.3)

local GM = Swc.gamemodes
local errorsBefore = swcui.error_count()
local function requests()
  local out = {}
  for _ = 1, 16 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local function has(list, text)
  for _, each in ipairs(list) do
    if each == text then return true end
  end
  return false
end
local function state()
  return LyShineManagerBus.Broadcast.GetCurrentState()
end

-- The data of the mode (gamedata/gamemodes.json: the row QuestMedea of javelindata_gamemodes).
local static = GameModeDataManagerBus.Broadcast.GetGameModeStaticData(QUEST_MEDEA)
equal(static.displayName, "@Quest_Medea_title", "the static data of QuestMedea: its name")
equal(static.isSoloTrial, true, "the static data of QuestMedea: a solo trial")
equal(static.recommendedLevel, 17, "the static data of QuestMedea: the recommended level 17")
local drops = static:GetPossibleItemDropIds(19)
equal(#drops, 7, "the possible rewards of QuestMedea: 7 items")
check(Swc.ItemStatic(drops[1]) ~= nil, "the first reward is an item of the data")

-- 1. The open: the state of the window, the entrance and its mode.
equal(Swc.HostDungeon("open", tostring(QUEST_MEDEA)), "ok", "the use of 71 opens the entrance window")
frames(2.0)
equal(state(), ENTRANCE, "the state of the entrance window")
local canvas
for _, each in pairs(Swc.screens) do
  if each.name == "DungeonEnterScreen" then canvas = each.canvas end
end
check(canvas ~= nil and swcui.canvas_get(canvas, "enabled"), "the entrance window shows")
equal(ArenaRequestBus.Event.GetDungeonGameModeId(GM.entity), QUEST_MEDEA, "the mode of the entrance")
local texts = {}
local function walk(id)
  for _, child in ipairs(UiElementBus.Event.GetChildren(id) or {}) do
    if swcui.enabled_in_hierarchy(child) and swcui.has(child, "text") then
      local t = swcui.get(child, "text.text") or ""
      if t ~= "" then texts[#texts + 1] = swcui.name(child) .. "=" .. t end
    end
    walk(child)
  end
end
if canvas then walk(swcui.canvas_get(canvas, "root")) end
note("the texts: " .. table.concat(texts, " | "))
picture("dungeonenter-open")
equal(swcui.error_count() - errorsBefore, 0, "no Lua error at the open")

-- 2. Enter: one request "dungeon enter 0" (the difficulty of a solo trial).
requests()
-- The Enter button of the script (Properties.EnterButton; the canvas has a second one in the hidden SeasonTrialsContent).
local main = canvas and script_of(find(swcui.canvas_get(canvas, "root"), "DungeonEnterScreen"), "dungeonenterscreen.luac")
local enter = main and main.Properties.EnterButton
check(enter ~= nil and swcui.enabled_in_hierarchy(enter), "the Enter button shows")
check(main ~= nil and main.EnterButton:IsEnabled() == true, "the Enter button is enabled (no group, no cooldown, the level 19)")
if enter then
  move_to(enter)
  frames(0.3)
  click(enter)
end
frames(0.5)
local r = requests()
check(has(r, "dungeon enter 0"), "Enter sends the entry request (72 with the base 71): " .. table.concat(r, " | "))

-- 3. EntranceBusy (17): the window stays and tries again after its delay.
Swc.HostDungeonResult(17)
frames(0.3)
equal(state(), ENTRANCE, "EntranceBusy: the window stays")
frames(3.0)
r = requests()
check(has(r, "dungeon enter 0"), "EntranceBusy: the window sends the entry request again: " .. table.concat(r, " | "))

-- 4. Success (0): the window closes with no notice, and the use ends.
Swc.HostDungeonResult(0)
frames(1.0)
r = requests()
check(state() ~= ENTRANCE, "Success: the window closes")
check(has(r, "dungeon end"), "Success: the close ends the use of 71: " .. table.concat(r, " | "))
check(not GM.open, "the window is not open")

-- 5. A failure (EntranceActivationFailure 10): the window closes with its notice.
equal(Swc.HostDungeon("open", tostring(QUEST_MEDEA)), "ok", "the second open")
frames(2.0)
requests()
local noticed
local handler = Swc.Bus("UiNotificationsBus").Broadcast.EnqueueNotification
Swc.Define("UiNotificationsBus", "Broadcast", {EnqueueNotification = function(data, ...)
  noticed = data and data.text
  if handler then return handler(data, ...) end
end})
Swc.HostDungeonResult(10)
frames(1.0)
check(state() ~= ENTRANCE, "EntranceActivationFailure: the window closes")
equal(noticed, "@ui_dungeon_entrance_activation_failure", "EntranceActivationFailure: the notice of the script")
check(has(requests(), "dungeon end"), "the close ends the use")

-- 6. Escape: the window closes, the use ends, and the HUD comes back.
equal(Swc.HostDungeon("open", tostring(QUEST_MEDEA)), "ok", "the third open")
frames(2.0)
requests()
key("escape")
frames(1.0)
r = requests()
check(state() ~= ENTRANCE, "Escape closes the entrance window")
check(has(r, "dungeon end"), "Escape ends the use of 71: " .. table.concat(r, " | "))
-- The client ends the use (the backend closed it): no second "dungeon end".
equal(Swc.HostDungeon("open", tostring(QUEST_MEDEA)), "ok", "the fourth open")
frames(2.0)
requests()
Swc.HostDungeon("close")
frames(1.0)
check(state() ~= ENTRANCE, "the close of the client closes the window")
check(not has(requests(), "dungeon end"), "the close of the client sends no dungeon end")
equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
