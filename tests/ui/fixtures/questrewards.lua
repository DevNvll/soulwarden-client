-- The quest rewards fixture (stage Q7, Linear AST-242; docs/QUESTS.md "Q7: the rewards and the offers"; backend quests.md stage 1.8d):
-- the availability of an offer (swc/conversation.lua unavailable: the order of Quests::available; the faction masks, the gates with no
-- rule, the NPA rows, the archetype variants of 06_boarfeast), the reward data of the previews, and the full bag (a client decision of this
-- project, research/quest-full-bag.md 5.3): the knife hand-in with a full bag and no compatible stack is refused locally (the notice, no
-- 794, no celebration, the option stays); with a compatible stack of the Meat Pouch it is sent; the first quest (no item reward) is sent
-- with a full bag.
-- Needs gamedata/quests.json with "npa" (tools/convert-ui.sh quests of stage Q7).
local HUD = 2702338936
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(20, 0)
frames(0.3)

local C = Swc.conversation
local D = Swc.quests.D()
local function bits(...)
  local out = {}
  for _, id in ipairs({...}) do
    local index = Swc.AchievementIndex(id)
    check(index ~= nil, "the achievement " .. id .. " has an index")
    out[#out + 1] = tostring(index or "")
  end
  Swc.HostOwnership(table.concat(out, ","), "", true)
end
local function requests()
  local out = {}
  for _ = 1, 8 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end

-- ------------------------------------------------------------------------------------------------------ the availability
-- The faction masks (backend gamedata/quest_data.cpp faction_mask; client 0x445CA30).
for text, mask in pairs({[""] = 0xF, None = 0xF, Any = 0xE, Syndicate = 0x2, Marauder = 0x4, Covenant = 0x8, ["!Marauder"] = 0xB,
                         ["!Covenant"] = 0x7, ["!Syndicate"] = 0xD, ["!"] = 0xE, ["!None"] = 0xE}) do
  equal(C.faction_mask(text), mask, "the faction mask of \"" .. text .. "\"")
end
-- One gate at a time (a row with only that cell; the rows of the data have more gates, and the order of the checks gives one reason).
local function gate(cells) return {id = "test", cells = cells} end
bits()
equal(C.unavailable(gate({RequiredFaction = "Marauder"})), "wrongFaction", "an unaligned character (faction 0): no Marauder offer")
equal(C.unavailable(gate({RequiredFaction = "Covenant"})), "wrongFaction", "no Covenant offer")
equal(C.unavailable(gate({RequiredFaction = "!Marauder"})), nil, "!Marauder has bit 0 (the bypass): offered")
equal(C.unavailable(gate({RequiredFaction = "None"})), nil, "None: offered")
equal(C.unavailable(gate({})), nil, "no gate: offered")
for _, name in ipairs({"RequiredGameModeId", "QuestArcTag", "RequiredWorldTags", "RequiredProgression"}) do
  equal(C.unavailable(gate({[name] = "X"})), "unsupportedGate", "a gate with no rule (" .. name .. "): no offer")
end
equal(C.unavailable(gate({RequiredLevel = "21"})), "levelTooLow", "RequiredLevel 21 at the level 20: no offer")
equal(C.unavailable(gate({RequiredLevel = "20"})), nil, "RequiredLevel 20 at the level 20: offered")
equal(C.unavailable(gate({RequiredAchievementId = "S_skinningknife"})), "notAvailable", "a false RequiredAchievementId: no offer")
equal(C.unavailable(gate({AchievementId = "S_getoffbeach_ww", RequiredLevel = "99"})), "levelTooLow", "the completion bit not set: the next check")
bits("S_getoffbeach_ww")
equal(C.unavailable(gate({AchievementId = "S_getoffbeach_ww", RequiredLevel = "99", RequiredFaction = "Marauder"})), "completedBefore",
  "the completion bit comes before the level and the faction")
equal(C.unavailable(gate({RequiredGameModeId = "X", RequiredLevel = "99"})), "unsupportedGate", "a gate with no rule comes before the level")
equal(C.unavailable(gate({RequiredLevel = "99", RequiredFaction = "Marauder"})), "levelTooLow", "the level comes before the faction")
bits()
equal(C.unavailable(nil), "noData", "no row: noData")
local counts = {}
for _, row in pairs(D.objectives) do
  local reason = C.unavailable(row) or "offered"
  counts[reason] = (counts[reason] or 0) + 1
end
local parts = {}
for reason, count in pairs(counts) do parts[#parts + 1] = reason .. " " .. count end
table.sort(parts)
note("the objectives of the data for a new character at the level 20: " .. table.concat(parts, ", "))
check((counts.wrongFaction or 0) > 0 and (counts.unsupportedGate or 0) > 0, "the data has offers that the faction and the gates hide")

-- The archetype variants of 06_boarfeast: with S_skinningknife and one archetype bit, only the variant of that archetype.
local VARIANTS = {Warmonger = "06_boarfeast_destroyer", Musketeer = "06_boarfeast_musketeer", Theurgist = "06_boarfeast_mystic",
                  Occultist = "06_boarfeast_occultist", Tracker = "06_boarfeast_ranger", Mercenary = "06_boarfeast_soldier",
                  Wanderer = "06_boarfeast_swordsman"}
local function offered()
  local out = {}
  for id, row in pairs(D.objectives) do
    if id:find("^06_boarfeast") and C.unavailable(row) == nil then out[#out + 1] = id end
  end
  table.sort(out)
  return table.concat(out, ",")
end
bits("S_skinningknife")
equal(offered(), "06_boarfeast", "S_skinningknife with no archetype: only 06_boarfeast")
for archetype, id in pairs(VARIANTS) do
  bits("S_skinningknife", "Archetype_" .. archetype)
  equal(offered(), id, "S_skinningknife and Archetype_" .. archetype .. ": only " .. id)
  local event = D.events[string.lower(D.objectives[id].cells.SuccessGameEventId or "")]
  check(event ~= nil and (event.ItemReward or "") ~= "", id .. ": its success event has its own item (" .. tostring(event and event.ItemReward) .. ")")
end
bits()
equal(offered(), "", "no S_skinningknife: no boarfeast offer")
bits("S_skinningknife", "S_boarfeast")
local done = D.objectives["06_boarfeast"].cells.AchievementId
note("the completion achievement of 06_boarfeast: " .. tostring(done))

-- The NPA rows (non-persisted achievements; backend id_value, kNpaDepth 8): a row is its own conditional; a status requirement, an empty
-- conditional, or the depth 8 is false; an id that the data does not know is false.
check(D.npa ~= nil and D.npa["*npa_redbutton_1"] ~= nil, "quests.json has the NPA rows (*npa_redbutton_1)")
local debugBit = Swc.AchievementIndex("debug_61")
if debugBit then
  bits()
  check(not C.achievements_pass("*NPA_RedButton_1"), "*NPA_RedButton_1 (debug_61) is false with no bit")
  Swc.HostOwnership(tostring(debugBit), "", true)
  check(C.achievements_pass("*NPA_RedButton_1"), "*NPA_RedButton_1 is true with debug_61")
else
  note("debug_61 has no index: the NPA value test uses the rows below")
end
bits()
check(not C.achievements_pass("*17DW_JumpPad_ReqStatusEffect_1"), "an NPA with a status requirement is false")
check(not C.achievements_pass("*17_MSQ_BatteryOn1"), "an NPA with an empty conditional is false")
D.npa["*swc_test_loop"] = {conditional = "*swc_test_loop", status = ""}
check(not C.achievements_pass("*swc_test_loop"), "an NPA chain ends at the depth 8 (false), with no endless loop")
D.npa["*swc_test_loop"] = nil
D.npa["*swc_test_true"] = {conditional = "!S_skinningknife", status = ""}
check(C.achievements_pass("*swc_test_true"), "an NPA whose conditional is true is true")
D.npa["*swc_test_true"] = nil
check(not C.achievements_pass("swc_unknown_id"), "an unknown id is false")

-- ------------------------------------------------------------------------------------------------------ the reward data
local beach, knife, alarm = D.objectives["06_getoffbeach"], D.objectives["06_skinningknife"], D.objectives["06_raisealarm"]
local knifeEvent = GameEventRequestBus.Broadcast.GetGameSystemData(knife.cells.SuccessGameEventId)
equal(knifeEvent.itemReward, "QuestReward_MB_Meat_Pouch", "the knife: the success event gives the Meat Pouch")
note(string.format("the knife event: xp %s coin %s", tostring(knifeEvent.progressionReward), tostring(knifeEvent.currencyRewardRange)))
local alarmEvent = GameEventRequestBus.Broadcast.GetGameSystemData(alarm.cells.SuccessGameEventId)
equal(alarmEvent.territoryStanding, 120, "06_raisealarm: 120 territory standing")
local beachEvent = GameEventRequestBus.Broadcast.GetGameSystemData(beach.cells.SuccessGameEventId)
equal(beachEvent.itemReward, "", "06_getoffbeach: no item reward")
equal(#C.reward_items(beach), 0, "the reward plan of 06_getoffbeach: no item")
local knifeItems = C.reward_items(knife)
check(#knifeItems == 1 and knifeItems[1].id == "QuestReward_MB_Meat_Pouch" and knifeItems[1].quantity == 1,
  "the reward plan of the knife: one Meat Pouch (a blank ItemRewardQty of the event is 1)")
equal(#C.reward_items({cells = {ItemRewardName = "WoodT1", ItemRewardQty = "0"}}), 0, "an explicit quantity 0: no item")
local blank = C.reward_items({cells = {ItemRewardName = "WoodT1"}})
equal(blank[1] and blank[1].quantity, 1, "a blank ItemRewardQty of the row: 1")

-- ------------------------------------------------------------------------------------------------------ the full bag
-- A bag of 3 slots (Swc.bagCapacity of a test). The Meat Pouch: bind on pickup (flags bit 0), stack 10000, no perks.
local notices = screen("LyShineUI/Notifications/Notification.uicanvas", 55, true)
local function notice_text()
  local found = ""
  local function walk(id, depth)
    if depth > 14 then return end
    if swcui.name(id) == "MinorNotifcationText" and swcui.enabled_in_hierarchy(id) then found = swcui.get(id, "text.text") or "" end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(notices, "root"), 0)
  return found
end
Swc.bagCapacity = 3
Swc.HostBagSize(3)
local function bag(third, quantity, flags)
  Swc.HostBagSlot(0, "WoodT1", 5, 0, 0, 0, "{00000000-0000-0000-0000-000000000001}", 0, 1, 0, "")
  Swc.HostBagSlot(1, "FlintT1", 5, 0, 0, 0, "{00000000-0000-0000-0000-000000000002}", 0, 2, 0, "")
  Swc.HostBagSlot(2, third or "", quantity or 0, 0, 0, 0, third and "{00000000-0000-0000-0000-000000000003}" or "", flags or 0, 3, 0, "")
end
bag()
check(C.rewards_fit(knife), "two used slots of three: the pouch fits")
bag("1hSkinningKnifeT1", 1)
check(not C.rewards_fit(knife), "a full bag with no pouch stack: no place")
bag("QuestReward_MB_Meat_Pouch", 5, 1)
check(C.rewards_fit(knife), "a full bag with a bound pouch stack of 5: the stack takes it")
bag("QuestReward_MB_Meat_Pouch", 5, 0)
check(not C.rewards_fit(knife), "a pouch stack that is not bound: a different stack (the bind flag of the backend put)")
bag("QuestReward_MB_Meat_Pouch", 10000, 1)
check(not C.rewards_fit(knife), "a full pouch stack (10000): no place")
check(C.rewards_fit(beach), "06_getoffbeach (no item reward) with a full bag: no item to place")
local destroyer = D.objectives["06_boarfeast_destroyer"]
check(not C.rewards_fit(destroyer), "the destroyer variant with a full bag: no place for its item")

-- The hand-in at Urda: the ready knife (its root Complete). The screen script method RequestConversationOption with the hook.
local conversation = screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
local function screen_instance()
  for _, list in pairs(Swc.components) do
    for _, instance in ipairs(list) do
      if string.find(instance.__swcScript or "", "conversationscreenv2.luac", 1, true) then
        return instance
      end
    end
  end
  return nil
end
local function ready(instance, row)
  local lines = {"A " .. instance .. " " .. row.crc .. " 6 0"}
  for _, entry in ipairs(row.tree) do
    local task = D.tasks[entry[2]]
    lines[#lines + 1] = string.format("T %s %d %d %d %d", instance, entry[1], task.crc, entry[1] == row.root and 4 or 1, 2)
  end
  lines[#lines + 1] = "K " .. instance
  return table.concat(lines, "\n")
end
local function has_complete(crc)
  for _, each in ipairs(C.result and C.result.options or {}) do
    if each.optionType == 5 and each.dataId == crc then return true end
  end
  return false
end
bits("S_getoffbeach_ww")
Swc.HostQuests(ready("778", knife))
Swc.HostPersons("person:urda\t01_UrdaWW_01\t9601.71\t2367.60\t44.64\n")
frames(0.5)
Swc.HostConversation("open", "person:urda", "Urda", "", "Urda", "", "", "01_UrdaWW_01")
frames(2.0)
equal(C.result and C.result.responseType, 6, "Urda with the ready knife: CompletionAvailable 6")
check(has_complete(knife.crc), "the result has Complete for the knife")
local script = screen_instance()
check(script ~= nil, "the screen script runs")
bag("1hSkinningKnifeT1", 1)
requests()
script:RequestConversationOption(5, knife.crc)
frames(1.0)
local r = requests()
equal(#r, 0, "a full bag with no pouch stack: no 794")
check(has_complete(knife.crc), "the Complete option stays")
check(not script.rewardCelebrationPlayed, "no reward celebration")
equal(C.prepared, nil, "no prepared Completion state")
equal(notice_text(), "Too many items in inventory to receive rewards.", "the retail notice @ui_inventory_warning_toofullforrewards")
picture("questrewards-fullbag")
-- The second guard: a direct bus call keeps the option too.
ConversationRequestBus.Broadcast.RequestConversationOption(5, knife.crc)
equal(#requests(), 0, "the bus guard: no 794")
check(has_complete(knife.crc), "the bus guard: the option stays")
frames(7.0)
equal(notice_text(), "Too many items in inventory to receive rewards.", "the notice shows for 10 s (8 s now)")
frames(6.0)
equal(notice_text(), "", "the notice ends after 10 s")

bag("QuestReward_MB_Meat_Pouch", 5, 1)
script:RequestConversationOption(5, knife.crc)
frames(1.0)
r = requests()
equal(r[1], "quest complete " .. knife.crc, "a full bag with a compatible pouch stack: 794 is sent")
check(not has_complete(knife.crc), "the Complete option goes (no second send)")
script:RequestConversationOption(5, knife.crc)
frames(0.5)
equal(#requests(), 0, "a second press: no second 794")
Swc.HostConversation("close")
frames(1.0)

-- The first quest at Aelstrom with a full bag (no item reward): sent.
bits()
Swc.HostQuests("C 778")
Swc.HostQuests(ready("777", beach))
Swc.HostPersons("person:aelstrom\t01_AelstromWW_01\t9613.356\t2329.267\t42.432\n")
bag("1hSkinningKnifeT1", 1)
frames(0.5)
Swc.HostConversation("open", "person:aelstrom", "Aelstrom", "01_aelstrom_default_00", "Aelstrom", "", "", "01_AelstromWW_01")
frames(2.0)
check(has_complete(beach.crc), "Aelstrom with the ready first quest: Complete")
requests()
screen_instance():RequestConversationOption(5, beach.crc)
frames(1.0)
r = requests()
equal(r[1], "quest complete " .. beach.crc, "06_getoffbeach with a full bag: 794 is sent (no item reward)")
Swc.HostConversation("close")
frames(1.0)
Swc.bagCapacity = 500

-- ------------------------------------------------------------------------------------------------------ the open of the Meat Pouch
-- The bag action Open of a loot container (dynamictooltip_commands.luac OnOpen, BoxOpenHelper: PrepareState of the popup state, then
-- SetRewardBoxGameEventId and SalvageItem): the host request "open SLOT 1 INSTANCE" (1733). The answer of the backend (live F of stage
-- 1.8d): one record, the pouch one unit less (cause 2) and the items of the roll with the cause OpenRewardChest 26 (10 Travel Rations,
-- 5 Weak Health Potions): rewardcelebrationpopup.luac OnRewardChestOpened, then its popup with the items.
local INVENTORY = 2972535350
local inventory = screen("LyShineUI/Inventory/NewInventory.uicanvas", 15)
screen("LyShineUI/Equipment/EquipmentV3.uicanvas", 35)
screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
local flyout = screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 52)
local popup = screen("LyShineUI/Inventory/RewardCelebrationPopup.uicanvas", 60)
allow("LyShineUI/Popup/Popup")
local function instance(n)
  return string.format("{%08X-0000-0000-0000-%012X}", n, n)
end
Swc.HostBagSize(4)
Swc.HostBagSlot(0, "WoodT1", 5, 0, 0, 0, instance(1), 0, 1, 17, "")
Swc.HostBagSlot(1, "FlintT1", 5, 0, 0, 0, instance(2), 0, 2, 17, "")
Swc.HostBagSlot(2, "QuestReward_MB_Meat_Pouch", 1, 0, 0, 0, instance(3), 1, 3, 1, "")
Swc.HostBagSlot(3, "", 0, 0, 0, 0, "", 0, 0, 17, "")
frames(0.3)
key("tab")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), INVENTORY, "Tab opens the state Inventory")
local root = swcui.canvas_get(inventory, "root")
local froot = swcui.canvas_get(flyout, "root")
local function each_script(id, fn)
  for _, c in ipairs(swcui.children(id)) do
    for _, inst in ipairs(Swc.components[tostring(c)] or {}) do fn(c, inst) end
    each_script(c, fn)
  end
end
local function cell_of(key)
  local found
  each_script(root, function(c, inst)
    if (inst.__swcScript or ""):find("itemdraggable.luac", 1, true) and swcui.enabled_in_hierarchy(c) and inst.ItemLayout
      and inst.ItemLayout.itemId == Swc.ItemStatic(key).crc then
      found = c
    end
  end)
  return found
end
local function command(label)
  local found
  local function scan(id)
    for _, c in ipairs(swcui.children(id)) do
      if swcui.enabled_in_hierarchy(c) and swcui.name(c) == "buttonText" and swcui.get(c, "text.text") == label then
        found = found or swcui.parent(c)
      end
      scan(c)
    end
  end
  scan(froot)
  return found
end
local pouch = cell_of("QuestReward_MB_Meat_Pouch")
check(pouch ~= nil, "the bag shows the Meat Pouch")
move_to(pouch, 0.5, 0.5)
frames(1.5)
check(swcui.canvas_get(flyout, "enabled"), "hover: the tooltip of the pouch shows")
picture("questrewards-pouch-tooltip")
click(pouch, 0.5, 0.5)
frames(0.8)
check(command("Open") ~= nil, "the command list of the pouch has Open")
picture("questrewards-pouch-commands")
Swc.hostRequests = {}
if command("Open") then click(command("Open"), 0.3, 0.5) end
frames(1.0)
equal(Swc.hostRequests[1], "open 2 1 " .. instance(3), "Open: the request of the bag slot 2, quantity 1, the instance (1733)")
check(not swcui.canvas_get(popup, "enabled"), "no popup before the answer")
Swc.HostBagSlot(2, "", 0, 0, 0, 0, "", 0, 0, 2, "")
Swc.HostBagSlot(3, "FoodHealthRecoveryT2", 10, 0, 0, 0, instance(4), 0, 4, 26, "")
Swc.HostBagSize(5)
Swc.HostBagSlot(4, "PotionHealthT1", 5, 0, 0, 0, instance(5), 0, 5, 26, "")
frames(2.0)
check(swcui.canvas_get(popup, "enabled"), "the answer with the cause 26: the reward popup shows")
local shown = {}
local function walk(id, depth)
  if depth > 30 then return end
  local t = swcui.has(id, "text") and swcui.get(id, "text.text")
  if t and t ~= "" and swcui.enabled_in_hierarchy(id) then shown[#shown + 1] = t end
  for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
end
walk(swcui.canvas_get(popup, "root"), 0)
local text = table.concat(shown, " | ")
note("the popup: " .. text)
local items = 0
each_script(swcui.canvas_get(popup, "root"), function(c, inst)
  if (inst.__swcScript or ""):find("rewardcelebrationitem.luac", 1, true) and swcui.enabled_in_hierarchy(c) then items = items + 1 end
end)
equal(items, 2, "the popup shows two reward items (the rations and the potions)")
picture("questrewards-pouch-popup")


-- ------------------------------------------------------------------------------------------------------ the territory standing
-- The standing of Windsward after 06_raisealarm (slot 65: the id CRC-32 of "6", 1DB87A14, rank 0, 120 points; protocol notes 4.57,
-- retail I0-11 "120 / 150"). territorydatahandler.luac reads GetRank, GetMaxRank, and GetRankData of that id; the map shows the points
-- of GetProgression against GetMaxPointsForRank (the rank table Territory_Standing: rank 0 MaximumInfluence 150).
local WINDSWARD = swcui.crc32("6")
equal(WINDSWARD, 0x1DB87A14, "the standing id of Windsward: the CRC-32 of \"6\"")
Swc.HostRanks(string.format("%d 0 120", WINDSWARD))
local player = LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.HudComponent.PlayerEntityId")
equal(CategoricalProgressionRequestBus.Event.GetRank(player, WINDSWARD), 0, "the standing rank: 0")
equal(CategoricalProgressionRequestBus.Event.GetProgression(player, WINDSWARD), 120, "the standing points: 120")
equal(CategoricalProgressionRequestBus.Event.GetMaxPointsForRank(player, WINDSWARD, 0), 150, "the points of rank 0: 150 (\"120 / 150\")")
check((CategoricalProgressionRequestBus.Event.GetMaxRank(player, WINDSWARD) or 0) > 0, "the standing has its rank table (GetMaxRank > 0)")
local rank0 = CategoricalProgressionRequestBus.Event.GetRankData(player, WINDSWARD, 0)
equal(rank0 and rank0.displayName, "@territory_standing_title_1", "the title of rank 0 (Territory_Standing DisplayName)")
equal(rank0 and rank0.maximumInfluence, 150, "the rank data of rank 0: MaximumInfluence 150")
Swc.HostRanks("")
