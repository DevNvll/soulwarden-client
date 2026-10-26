-- The Lua error fixture (the cleanup of 2026-10-25; docs/UI.md "The Lua errors"): each path that gave a Lua error in the client runs here
-- and the error count of the runtime (Swc.errorCount) stays 0. The start of the client is the run tools/test-ui.sh --client-screens (the
-- screens of SwcUi/HudScreens.h with the loader of the game: 34 errors before, 0 now).
--   1. bannertriggers.luac OnProgressionPointsChanged (a change of the learned nodes): GetStaticProgressionPointData has a record.
--   2. bannertriggers.luac TrySeasonsRewardsSeasonPassBanner at the spawn: the level node is set before the spawn, the config value
--      javelin.seasons-rewards.min-level-for-seasonpass is 25.
--   3. The journal: documentstab.luac (the document lists of JournalDataManagerBus, the bus method GetCurrentBusId).
--   4. A travel (AST-280): loadingscreenmanager.luac UpdateTipPoolPriorityLevels (the native calls of the tip show counts).
--   5. The skills screens: the lists of tradeskills_crafting.luac and changetitlewindow.luac at the refresh of their scroll boxes.
--   AST-228: hudcanvasdefault.luac and lootticker2024canvas.luac SetIsVisible at the UI state changes (the hook of swc/elements.lua).
local HUD = 2702338936
screen("LyShineUI/Banner/Banner.uicanvas", 30)
local journal = screen("LyShineUI/Objectives/Journal/JournalScreen.uicanvas", 40)
local skills = screen("LyShineUI/Skills/Skills.uicanvas", 20)
-- AST-228: two HUD canvases with hudcanvasdefault.luac and lootticker2024canvas.luac (SetIsVisible at each UI state change).
screen("LyShineUI/HUD2024/WeaponQuickSlots/WeaponQuickSlots.uicanvas", 6, true)
screen("LyShineUI/HUD2024/LootTicker/LootTicker2024.uicanvas", 20, true)
local function errors()
  return Swc.errorCount or 0
end
local start = errors()
equal(start, 0, "the screens load with no Lua error (the banner, the journal, the skills window)")

-- 2. The level before the spawn (UI/SwcUiSubsystem.cpp FeedLocalPlayer), then the spawn: the season pass banner reads the level.
Swc.HostProgression(5, 1234)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
frames(0.5)
equal(errors(), 0, "the spawn with the level node: no Lua error (TrySeasonsRewardsSeasonPassBanner)")
equal(ConfigProviderEventBus.Broadcast.GetUInt("javelin.seasons-rewards.min-level-for-seasonpass"), 25, "the season pass level of the config")

-- 1. A change of the learned nodes of the weapon trees: OnProgressionPointsChanged.
Swc.HostLearnedNodes("")
frames(0.3)
equal(errors(), 0, "a change of the learned nodes: no Lua error (OnProgressionPointsChanged)")
local data = ProgressionPointRequestBus.Event.GetStaticProgressionPointData(Swc.ids.player, 0)
check(data ~= nil and data.poolCategory ~= ePoolCategory_Territory, "the static data of a pool: not a territory pool")

-- 3. The journal and its documents tab.
key("j")
frames(1.5)
check(swcui.canvas_get(journal, "enabled"), "J opens the journal")
equal(errors(), 0, "the journal: no Lua error (documentstab.luac)")
equal(#(JournalDataManagerBus.Broadcast.GetTopicEntries() or {}), 0, "no document topics in this client")
-- GetCurrentBusId: the address of the handler of the event that runs.
local seen
local h = UiDynamicScrollBoxDataBus.Connect({GetNumElements = function() seen = UiDynamicScrollBoxDataBus.GetCurrentBusId() return 0 end}, EntityId(4242))
Swc.Notify("UiDynamicScrollBoxDataBus", EntityId(4242), "GetNumElements")
check(seen ~= nil and seen == EntityId(4242), "GetCurrentBusId: the address of the handler of the event")
equal(type(UiDynamicScrollBoxDataBus.GetCurrentBusId) == "function" and UiDynamicScrollBoxDataBus.GetCurrentBusId() or nil, nil,
  "GetCurrentBusId after the event: none")
h:Disconnect()
key("escape")
frames(0.5)

-- 3b. A trade skill rank change (AST-244 o3b: the first Skinning rank at the gather of a boar body): bannertriggers.luac
-- OnCategoricalProgressionRankChanged takes the length of GetTradeskillLockedGatherableData (a nil stopped it).
do
  local function crc(text) return swcui.crc32(string.lower(text)) end
  Swc.HostRanks(string.format("%d 0 0", crc("Skinning")))
  frames(0.2)
  Swc.HostRanks(string.format("%d 1 95", crc("Skinning")))
  frames(0.5)
  equal(errors(), 0, "a trade skill rank change (Skinning 0 to 1): no Lua error (bannertriggers.luac)")
end

-- 4. A travel: the tip pools of the loading screen manager (the show count of each tip).
Swc.HostLoading(true, "newworld_vitaeeterna")
frames(0.5)
Swc.HostLoading(false)
frames(0.3)
equal(errors(), 0, "the loading screen of a travel: no Lua error")
local manager
for _, list in pairs(Swc.components) do
  for _, inst in ipairs(list) do
    if (inst.__swcScript or ""):find("loadingscreenmanager.luac", 1, true) then manager = inst end
  end
end
check(manager ~= nil and type(manager.UpdateTipPoolPriorityLevels) == "function", "the loading screen manager is loaded")
if manager then
  local tips = {{saveSettingsId = swcui.crc32("tip_a"), timesToShowPriority = 3}, {saveSettingsId = swcui.crc32("tip_b"), timesToShowPriority = 2}}
  manager:UpdateTipPoolPriorityLevels(tips)
  equal(errors(), 0, "a travel: the tip pool update has no Lua error (AST-280)")
  equal(tips[1].timesToShowPriority, 3, "a tip with no saved count keeps its priority (-1: none)")
  manager.dataLayer:Call(1214891646, swcui.crc32("tip_b"), 7)
  manager:UpdateTipPoolPriorityLevels(tips)
  equal(tips[2].timesToShowPriority, 7, "a saved count (1214891646) is read back (355238560)")
end

-- AST-228: UI state changes (the HUD canvases hide and show: SetIsVisible over ElementsWithActionListeners).
for _, state in ipairs({2972535350, HUD, 1823500652, HUD}) do
  LyShineManagerBus.Broadcast.SetState(state)
  frames(0.5)
end
equal(errors(), 0, "UI state changes with the HUD canvases: no Lua error (AST-228, fixed by 54c0c77)")

-- 5. The skills window: its screens and scroll boxes (the lists at the refresh).
equal(errors(), 0, "the skills window and its scroll boxes: no Lua error")
-- The BIO tab of the window: the label of the world line (canvas text "@ui_world_select_world:"; the user test of 2026-10-26).
do
  local label = find(swcui.canvas_get(skills, "root"), "WorldLabel")
  check(label ~= nil, "the BIO tab has its world label")
  if label then
    equal(swcui.get(label, "text.text"), "World:", "the BIO tab: the world label is localized (not \"@ui_world_select_world:\")")
  end
end
note("the Lua errors of the run: " .. tostring(errors()))

-- The reward popup (stage Q7): only the dynamic loader names its canvas; PrepareState(1000163614) loads it once.
allow("LyShineUI/Inventory/RewardCelebrationPopup")
equal(#DynamicBus.RewardCelebrationPopup.handlers, 0, "no reward popup before its state")
check(DynamicBus.DynamicCanvasLoaderBus.Broadcast.PrepareState(1000163614), "PrepareState of the popup state: true")
frames(0.3)
check(#DynamicBus.RewardCelebrationPopup.handlers > 0, "PrepareState loads the reward popup canvas")
equal(errors(), 0, "the load of the reward popup: no Lua error")
