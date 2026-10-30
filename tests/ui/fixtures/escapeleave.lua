-- The dungeon leave of the game menu (backend release 2026-10-30-340fbc7, stage QG9e; protocol notes 4.69; escapemenu.luac UpdateScreen,
-- OnRespawnPressed, OnPopupResult): in a mode with IsDungeon (QuestMedea 95B632E9, the GameModeParticipant state with no flag) and with the
-- PlayerArena state isInArena (2406), the button of Recall to Inn (ButtonRespawn, hidden in this client outside a dungeon mode) is "Leave"
-- (@ui_dungeon_leave_solo); its popup Popup_ExitDungeon ("Exit Soul Trial?") calls ForfeitArena(root, true): "arena forfeit 1" (926), and
-- the state goes to the HUD. No flag: no extra mode button (ButtonGameExtra). With no mode: no leave button.
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
local function requests(prefix)
  local out = {}
  for _, r in ipairs(Swc.hostRequests) do
    if r:find(prefix, 1, true) == 1 then out[#out + 1] = r end
  end
  return out
end
local menuRoot = swcui.canvas_get(menu, "root")
local function button(name)
  local id = find(menuRoot, name)
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

-- 1. No game mode: no leave button (Recall to Inn stays hidden), no extra mode button.
Swc.HostGameModes("Q 0\nA 0\n")
key("escape")
frames(0.6)
equal(state(), ESC, "Escape: the escape menu")
local _, shown = button("ButtonRespawn")
check(not shown, "no game mode: no leave button (Recall to Inn is hidden)")
_, shown = button("ButtonGameExtra")
check(not shown, "no game mode: no extra mode button")
key("escape")
frames(0.6)
equal(state(), HUD, "Escape again: the HUD")

-- 2. In QuestMedea (IsDungeon) with isInArena and no flag: "Leave" at the place of Recall to Inn, no extra mode button.
Swc.HostGameModes(string.format("M %d 0 120\nQ 0\nA 1\n", QUEST_MEDEA))
equal(GameModeParticipantComponentRequestBus.Event.GetCurrentDungeonGameModeId(EntityId()), QUEST_MEDEA, "the current dungeon mode")
check(PlayerArenaRequestBus.Event.IsInArena(EntityId()), "IsInArena: the PlayerArena state")
key("escape")
frames(0.6)
equal(state(), ESC, "Escape in the trial: the escape menu")
local leave
leave, shown = button("ButtonRespawn")
check(shown, "a dungeon mode: the button of Recall to Inn shows")
equal(leave and text_below(leave), swcui.localize("@ui_dungeon_leave_solo"), "its text is Leave (@ui_dungeon_leave_solo)")
_, shown = button("ButtonGameExtra")
check(not shown, "no flag: no extra mode button")
local settings = find(menuRoot, "ButtonSettings")
if leave and settings then
  local _, settingsTop = swcui.rect(settings)
  local _, leaveTop = swcui.rect(leave)
  check(leaveTop > settingsTop, "Leave is below Settings in the group Game")
end
picture("escapeleave-menu")
equal(#requests("arena forfeit"), 0, "no 926 before Yes")
if leave then
  click(leave)
  frames(0.6)
end
check(swcui.canvas_get(popup, "enabled"), "Leave opens the popup")
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
check(joined:find(swcui.localize("@ui_exit_dungeon_title_solo"), 1, true) ~= nil, "the popup title: Exit Soul Trial? (@ui_exit_dungeon_title_solo)")
check(joined:find(swcui.localize("@ui_exit_dungeon_will_close_desc_solo"), 1, true) ~= nil, "the popup text of a solo trial")
picture("escapeleave-popup")
local yes = find(swcui.canvas_get(popup, "root"), "ButtonYes")
check(yes ~= nil, "the popup has its Yes button")
if yes then
  click(yes)
  frames(0.6)
end
local forfeits = requests("arena forfeit")
equal(#forfeits, 1, "Yes: one ForfeitArena")
equal(forfeits[1], "arena forfeit 1", "Yes: ForfeitArena with leaveDungeon 1 (926)")
equal(#requests("gamemode event"), 0, "no 1980 (the extra mode button is off)")
equal(state(), HUD, "Yes: the state goes to the HUD")

-- 3. After the leave (no mode, isInArena false): no leave button again.
Swc.HostGameModes("Q 0\nA 0\n")
key("escape")
frames(0.6)
_, shown = button("ButtonRespawn")
check(not shown, "after the leave: no leave button")
key("escape")
frames(0.6)
equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
