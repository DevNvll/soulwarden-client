-- The quest progress fixture (Linear AST-241, stage Q6; docs/QUESTS.md): the counter of a kill task on the tracker of the HUD
-- (ObjectivesHud) and the Reset of a main story quest in the journal (JournalScreen and its confirm popup). The state is 06_crewmates
-- "Crewmates No More" with its kill task (index 8, TaskKillContribution Corrupted, TargetQty 5) at 2, then 3 (the retail picture
-- K1-2of5.png of the backend stage 1.8c: "2 / 5 - Defeat Corrupted Mariners"). Reset: the button of the journal row, the popup, Yes:
-- the request "quest reset INSTANCE" (5600).
-- Pictures: questprogress-hud, questprogress-reset.
local HUD = 2702338936
local hud = screen("LyShineUI/Objectives/Hud/ObjectivesHud.uicanvas", 12, true)
local journal = screen("LyShineUI/Objectives/Journal/JournalScreen.uicanvas", 40)
local popup = screen("LyShineUI/Popup/Popup.uicanvas", 52)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(5, 0)
Swc.HostLoading(true, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.3)
Swc.HostLoading(false, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.5)

local function requests()
  local out = {}
  for _ = 1, 8 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local function texts_of(canvas)
  local out = {}
  local function walk(id, depth)
    if depth > 24 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(canvas, "root"), 0)
  return table.concat(out, " | ")
end
local function find_text(canvas, text)
  local found
  local function walk(id, depth)
    if found or depth > 24 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t == text and swcui.enabled_in_hierarchy(id) then found = id return end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(canvas, "root"), 0)
  return found
end

-- 06_crewmates: the swaps, the tutorial container, and the place task complete (2), the kill task active (1) with its progress.
local INSTANCE = "9001"
local function state(kills)
  local row = Swc.quests.D().objectives["06_crewmates"]
  local lines = {"A " .. INSTANCE .. " " .. row.crc .. " 6 0"}
  for _, entry in ipairs(row.tree) do
    local index = entry[1]
    local task = Swc.quests.D().tasks[entry[2]]
    local s, progress = 2, 1
    if index == 8 then s, progress = 1, kills end
    if index == 9 then s, progress = 1, 0 end
    lines[#lines + 1] = string.format("T %s %d %d %d %d", INSTANCE, index, task.crc, progress, s)
  end
  lines[#lines + 1] = "K " .. INSTANCE
  return table.concat(lines, "\n")
end
Swc.HostQuests(state(2))
Swc.HostQuestPinReady(true)
frames(2.5)
local text = texts_of(hud)
note("the tracker: " .. text)
check(text:find("Crewmates No More", 1, true) ~= nil, "the tracker shows Crewmates No More")
check(text:find("2 / 5", 1, true) ~= nil and text:find("Defeat Corrupted Mariners", 1, true) ~= nil,
  "the counter of the kill task: 2 / 5 - Defeat Corrupted Mariners (retail K1-2of5)")
picture("questprogress-hud")
Swc.HostQuests(state(3))
frames(1.5)
text = texts_of(hud)
note("the tracker after a kill: " .. text)
check(text:find("3 / 5", 1, true) ~= nil, "a kill: the counter 3 / 5")
local placeText = Swc.quests.FillTaskText("Go to {POITags}", Swc.quests.D().tasks["06_crewmates_02_taskgotopoitag_shipwreck1"])
note("the place text: " .. placeText)
check(placeText ~= "Go to " and not placeText:find("{", 1, true), "{POITags}: the name of the point of interest of the tag")

-- The journal: Reset (CanBeAbandoned 0: a main story quest), the confirm popup, Yes.
requests()
key("j")
frames(1.5)
note("the journal: " .. texts_of(journal))
local screens = {}
for _ = 1, 16 do
  local each = Swc.TakeQuestScreen()
  if each == 0 then break end
  screens[#screens + 1] = each
end
note("the screens: " .. table.concat(screens, ", "))
check(screens[#screens] == 1823500652, "J: the journal state goes to the queue of the screen reports (2617)")
local reset = find_text(journal, "Reset")
check(reset ~= nil, "the journal row has the button Reset")
if reset then click(reset) end
frames(1.0)
note("the popup: " .. texts_of(popup))
picture("questprogress-reset")
local yes = find_text(popup, "Yes")
check(yes ~= nil, "the reset asks for a confirmation (the popup has Yes)")
if yes then click(yes) end
frames(1.0)
local r = requests()
note("the requests: " .. table.concat(r, "; "))
local sent = false
for _, each in ipairs(r) do sent = sent or each == "quest reset " .. INSTANCE end
check(sent, "Yes: the request quest reset INSTANCE (5600)")

-- The quest objects (06_pillageship: the three treasures, TaskGatherAndReturnItems with InteractTag IO_MSQ_Treasure_A to C): the tags of
-- the Active tasks go to the focus of the objects; a complete task gives no tag.
local PILLAGE = "9002"
local function pillage(treasureState)
  local row = Swc.quests.D().objectives["06_pillageship"]
  local lines = {"A " .. PILLAGE .. " " .. row.crc .. " 6 0"}
  for _, entry in ipairs(row.tree) do
    local task = Swc.quests.D().tasks[entry[2]]
    local s = 2
    if entry[2]:find("gatherreturn_treasure", 1, true) then s = treasureState end
    if entry[2] == "06_pillageship_04_container2" or entry[2] == row.root then s = 1 end
    lines[#lines + 1] = string.format("T %s %d %d 0 %d", PILLAGE, entry[1], task.crc, s)
  end
  return table.concat(lines, "\n")
end
Swc.HostQuests(pillage(1))
frames(0.5)
local tags = Swc.QuestActiveTags()
note("the active tags: " .. tags)
check(tags:find("io_msq_treasure_a", 1, true) ~= nil and tags:find("io_msq_treasure_c", 1, true) ~= nil,
  "the treasure tasks active: the tags of the quest objects")
Swc.HostQuests(pillage(2))
frames(0.5)
equal(Swc.QuestActiveTags(), "", "the treasure tasks complete: no tag (no prompt)")

-- The hand-in (06_skinningknife, its root complete: the hand-in is stage 1.8b): the tracker shows the hand-in row "Talk to Urda"
-- (the retail picture K1-2of5: "Basic Needs / Talk to Urda").
LyShineManagerBus.Broadcast.SetState(HUD)
frames(1.0)
local KNIFE = "9003"
local knife = Swc.quests.D().objectives["06_skinningknife"]
local knifeLines = {"A " .. KNIFE .. " " .. knife.crc .. " 6 0"}
for _, entry in ipairs(knife.tree) do
  knifeLines[#knifeLines + 1] = string.format("T %s %d %d 1 2", KNIFE, entry[1], Swc.quests.D().tasks[entry[2]].crc)
end
knifeLines[#knifeLines + 1] = "K " .. KNIFE
Swc.HostQuests(table.concat(knifeLines, "\n"))
frames(2.0)
local handIn = texts_of(hud)
note("the tracker with the hand-in: " .. handIn)
check(handIn:find("Basic Needs", 1, true) ~= nil and handIn:find("Talk to Urda", 1, true) ~= nil,
  "the root complete: the hand-in row Talk to Urda (retail K1-2of5)")

-- A side story that the player can abandon (02A_Side07_02_Death: Journey, CanBeAbandoned 1, a row that backend 1.8f supports; before the
-- cleanup of 2026-10-25 a faction mission, whose journal list is hidden now): the journal row has Abandon; its popup and Yes give the
-- request "quest abandon INSTANCE" (3836).
local MISSION = "9004"
local mission = Swc.quests.D().objectives["02a_side07_02_death"]
local missionLines = {"A " .. MISSION .. " " .. mission.crc .. " 6 0"}
for _, entry in ipairs(mission.tree) do
  missionLines[#missionLines + 1] = string.format("T %s %d %d 0 1", MISSION, entry[1], Swc.quests.D().tasks[entry[2]].crc)
end
Swc.HostQuests(table.concat(missionLines, "\n"))
frames(1.0)
requests()
key("j")
frames(1.5)
note("the journal with the mission: " .. texts_of(journal))
local function has_journal_text(text) return find_text(journal, text) ~= nil end
local section = find_text(journal, "Side Stories")
if section then click(section) end
frames(1.5)
check(has_journal_text("Abandon"), "the side story row has Abandon")
local abandon = find_text(journal, "Abandon")
if abandon then click(abandon) end
frames(1.0)
note("the abandon popup: " .. texts_of(popup))
local yes2 = find_text(popup, "Yes")
if yes2 then click(yes2) end
frames(1.0)
local sentAbandon = false
for _, each in ipairs(requests()) do sentAbandon = sentAbandon or each == "quest abandon " .. MISSION end
check(sentAbandon, "Abandon, Yes: the request quest abandon INSTANCE (3836)")
