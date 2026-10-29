-- The order of canvases with the same draw order (docs/UI.md "The order of the canvases"): the draw order, then the load order (the later
-- load on top), and [I] the Popup canvas goes to the top of its group at each show (decided by the orchestrator on 2026-10-29: two static
-- reads did not find the native cause; one retail picture, Soulwarden qg7 shot 53). The tie of the Popup and RewardCelebrationPopup (both
-- POPUP_DRAW_ORDER 52; the Popup loads at the start, the reward popup later at its state 1000163614). The tie with FactionSelection: the
-- fixture factions.
local HUD = 2702338936
local popup = screen("LyShineUI/Popup/Popup.uicanvas", 52)
local reward = screen("LyShineUI/Inventory/RewardCelebrationPopup.uicanvas", 52)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
frames(0.3)

local function index_of(canvas)
  for index, id in ipairs(swcui.canvases()) do
    if tostring(id) == tostring(canvas) then return index end
  end
  return 0
end

-- Before a show: the load order (the reward popup loaded later is on top).
swcui.canvas_set(reward, "enabled", true)
check(index_of(reward) > index_of(popup), "before a popup shows: the later load (RewardCelebrationPopup) is on top")

-- A popup through the original wrapper (as factionselection.luac OnChangePressed): the Popup goes to the top of its group.
local wrapper = RequireScript("LyShineUI.Popup.PopupRequestWrapper")
local caller = {}
wrapper:RequestPopupWithParams({title = "@ui_change_faction", message = "@ui_yes", eventId = "Swc_CanvasOrder", callerSelf = caller,
  callback = function() end, buttonsYesNo = true, yesButtonText = "@ui_yes", noButtonText = "@ui_no"})
frames(0.5)
check(swcui.canvas_get(popup, "enabled"), "the popup shows")
check(index_of(popup) > index_of(reward), "the popup is drawn over RewardCelebrationPopup (the same draw order 52)")

-- A canvas with no mark keeps its place at a show: the reward popup hides and shows again, the Popup stays on top.
swcui.canvas_set(reward, "enabled", false)
swcui.canvas_set(reward, "enabled", true)
check(index_of(popup) > index_of(reward), "a show of a canvas with no mark does not raise it")
equal(Swc.errorCount or 0, 0, "no Lua error")
