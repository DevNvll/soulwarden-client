-- The leave of a story instance in the escape menu (C-13 step 1b part B of ut567; escapemenu.luac UpdateScreen and OnLeaveGameMode): with
-- the GameModeParticipant state of the backend (3312: the mode QuestMedea 95B632E9 and the flag PlayerCanQuit, bit 22), the menu shows
-- ButtonGameExtra with the quit text of the mode (@ui_solo_arena_leave of javelindata_gamemodes); its popup has the leave texts of the mode;
-- Yes sends the client event 9BB080D0 (1980: "gamemode event 2612035792") and the state goes to the HUD. With no mode, no button.
local HUD, ESC = 2702338936, 1643432462
local QUEST_MEDEA = 0x95B632E9
local menu = screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 41)
local popup = screen("LyShineUI/Popup/Popup.uicanvas", 52)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
frames(0.3)
local errorsBefore = swcui.error_count()
local function state()
  return LyShineManagerBus.Broadcast.GetCurrentState()
end
local function leave_requests()
  local out = {}
  for _, r in ipairs(Swc.hostRequests) do
    if r:find("^gamemode event") then out[#out + 1] = r end
  end
  return out
end
local menuRoot = swcui.canvas_get(menu, "root")
local function extra()
  local id = find(menuRoot, "ButtonGameExtra")
  return id, id ~= nil and swcui.enabled_in_hierarchy(id)
end
local function text_below(id)
  local found
  local function walk(e)
    for _, child in ipairs(UiElementBus.Event.GetChildren(e) or {}) do
      if not found and swcui.has(child, "text") and (swcui.get(child, "text.text") or "") ~= "" then
        found = swcui.get(child, "text.text")
      end
      walk(child)
    end
  end
  walk(id)
  return found
end

-- 1. No game mode: no leave button.
Swc.HostGameModes("Q 0\n")
key("escape")
frames(0.6)
equal(state(), ESC, "Escape: the escape menu")
local _, shown = extra()
check(not shown, "no game mode: no leave button")
key("escape")
frames(0.6)
equal(state(), HUD, "Escape again: the HUD")

-- 2. In QuestMedea with PlayerCanQuit: the leave button with the text of the mode.
Swc.HostGameModes(string.format("M %d 0 120\nQ 1\n", QUEST_MEDEA))
check(GameModeParticipantComponentRequestBus.Event.IsInGameMode(EntityId(), QUEST_MEDEA), "IsInGameMode QuestMedea")
equal(GameModeParticipantComponentRequestBus.Event.GetCurrentDungeonGameModeId(EntityId()), QUEST_MEDEA, "the current dungeon mode")
key("escape")
frames(0.6)
equal(state(), ESC, "Escape in the trial: the escape menu")
local button
button, shown = extra()
check(shown, "PlayerCanQuit: the leave button shows")
local label = button and text_below(button)
equal(label, swcui.localize("@ui_solo_arena_leave"), "the leave button has the quit text of QuestMedea (@ui_solo_arena_leave)")
picture("escapeleave-menu")
equal(#leave_requests(), 0, "no leave event before Yes")
if button then
  click(button)
  frames(0.6)
end
check(swcui.canvas_get(popup, "enabled"), "the leave button opens the popup")
local popupTexts = {}
local function walk(e)
  for _, child in ipairs(UiElementBus.Event.GetChildren(e) or {}) do
    if swcui.enabled_in_hierarchy(child) and swcui.has(child, "text") then
      popupTexts[#popupTexts + 1] = swcui.get(child, "text.text") or ""
    end
    walk(child)
  end
end
walk(swcui.canvas_get(popup, "root"))
local joined = table.concat(popupTexts, " | ")
note("the popup texts: " .. joined)
check(joined:find(swcui.localize("@ui_solo_arena_leave_title"), 1, true) ~= nil, "the popup has the leave title of the mode")
check(joined:find(swcui.localize("@ui_solo_arena_leave_desc"), 1, true) ~= nil, "the popup has the leave text of the mode (no rejoin penalty)")
picture("escapeleave-popup")
local yes = find(swcui.canvas_get(popup, "root"), "ButtonYes")
check(yes ~= nil, "the popup has its Yes button")
if yes then
  click(yes)
  frames(0.6)
end
local leaves = leave_requests()
equal(#leaves, 1, "Yes: one leave event")
equal(leaves[1], "gamemode event 2612035792", "the leave event 9BB080D0 (1980)")
equal(state(), HUD, "Yes: the state goes to the HUD")
equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
