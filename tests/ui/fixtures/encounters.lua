-- The encounter fixture (QG5, ut567; protocol notes 4.62): the record of a linked encounter root ("E" line of the host: the generated
-- tree of the recipe) in the original quest HUD, next to an ordinary pinned quest. The Obelisk encounter: two tasks (the device, then
-- the kill 0/2): the leaves 1 and 3, the wrappers 2 and 4, the root 5. The root is tracked first with the type POI; the kill row shows
-- "1 / 2"; a timed leaf (the Thorpe intermission, 10 s) has its TimeLimit and the start of the S line; the end of the root removes
-- the row with no banner of a failed quest; the pin request has only the ordinary quest.
-- Picture: encounters-hud.
local HUD = 2702338936
local hud = screen("LyShineUI/Objectives/Hud/ObjectivesHud.uicanvas", 12, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(30, 0)
Swc.HostLoading(true, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.3)
Swc.HostLoading(false, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.5)

local D = Swc.quests.D()
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
local function requests()
  local out = {}
  for _ = 1, 16 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local failed = 0
ObjectivesComponentNotificationsBus.Connect({OnObjectiveFailed = function() failed = failed + 1 end}, Swc.ids.player)

-- An ordinary quest (the first state of 06_raisealarm), tracked.
local alarm = D.objectives["06_raisealarm"]
local QUEST = "4660"
local function quest_lines()
  local lines = {"A " .. QUEST .. " " .. alarm.crc .. " 6 0"}
  for _, entry in ipairs(alarm.tree) do
    lines[#lines + 1] = string.format("T %s %d %d 0 %d", QUEST, entry[1], D.tasks[entry[2]].crc, entry[1] == alarm.tree[1][1] and 1 or 0)
  end
  return lines
end

-- The Obelisk root: G 0x4C554F534000002A = 5500585112935202858; the device done (leaf 1 1/1 Complete, wrapper 2 1/1 Complete), the kill
-- leaf 3 at 1/2 Active, its wrapper 4 Active, the root 5 at 1/2 Active.
local ROOT = "5500585112935202858"
local TREE = "@04A_Side02_02_ObeliskDefenders_Title\t@Enc_04A_Side02_02_Interact|1|0|0\t@Enc_04A_Side02_02_KillAI|2|0|0"
local function encounter_lines(kill)
  local lines = {"E " .. ROOT .. " 7B52B1B2898D5D2684BD2DCED94F3321\t" .. TREE,
    "T " .. ROOT .. " 1 0 1 2", "T " .. ROOT .. " 2 0 1 2", "T " .. ROOT .. " 3 0 " .. kill .. " 1", "T " .. ROOT .. " 4 0 0 1",
    "T " .. ROOT .. " 5 0 1 1", "K " .. ROOT}
  return lines
end
local function feed(withEncounter, kill)
  local lines = {}
  if withEncounter then
    for _, l in ipairs(encounter_lines(kill or 1)) do lines[#lines + 1] = l end
  end
  for _, l in ipairs(quest_lines()) do lines[#lines + 1] = l end
  lines[#lines + 1] = "K " .. QUEST
  Swc.HostQuests(table.concat(lines, "\n"))
end

feed(true, 1)
Swc.HostQuestPinReady(true)
frames(2.0)
local tracked = ObjectivesComponentRequestBus.Event.GetTrackedObjectives(Swc.ids.player)
check(#tracked == 2 and tracked[1]:ToString() == ROOT, "the encounter is tracked first (" .. #tracked .. " tracked)")
local rootId = tracked[1]
equal(ObjectiveRequestBus.Event.GetType(rootId), eObjectiveType_POI, "the encounter objective has the type POI")
equal(ObjectiveRequestBus.Event.GetTitle(rootId), "@04A_Side02_02_ObeliskDefenders_Title", "the title of the stages")
local rootTasks = ObjectiveRequestBus.Event.GetTasks(rootId)
check(#rootTasks == 1 and rootTasks[1].taskIndex == 5, "the root task is the outer container 5")
local wrappers = ObjectiveTaskRequestBus.Event.GetTasks(rootTasks[1])
check(#wrappers == 2 and wrappers[1].taskIndex == 2 and wrappers[2].taskIndex == 4, "the wrappers 2 and 4")
check(ObjectiveTaskRequestBus.Event.ContainsConsecutiveTasks(rootTasks[1]), "the outer container is consecutive")
local kill = Swc.quests.task_id(ROOT, 3)
equal(ObjectiveTaskRequestBus.Event.GetTarget(kill), 2, "the kill leaf has the target 2 (m_requiredCount)")
equal(ObjectiveTaskRequestBus.Event.GetProgress(kill), 1, "the kill leaf has the progress 1")
check(not ObjectiveTaskRequestBus.Event.GetHideProgress(kill), "the kill leaf shows N / M")
local text = texts_of(hud)
note("the tracker: " .. text)
check(text:find("1 / 2", 1, true) ~= nil, "the tracker shows the counter 1 / 2")
picture("encounters-hud")

-- The pin request of a change of the pins has only the ordinary quest.
requests()
ObjectivesComponentRequestBus.Event.SetObjectiveTracked(Swc.ids.player, rootId, true)
frames(0.3)
local pins
for _, r in ipairs(requests()) do
  if r:find("^quest pins") then pins = r end
end
check(pins == nil or not pins:find(ROOT, 1, true), "no pin request names the encounter root (" .. tostring(pins) .. ")")

-- A timed leaf (the Thorpe intermission: 10 s): its TimeLimit and the start of the S line.
local TIMED = "5500585112935202859"
local start = WallClockTimePoint:Now():GetTimeSinceEpoc():ToSeconds() - 2
Swc.HostQuests(table.concat({"E " .. TIMED .. " 252DC9178EA98EA7\t@MSQ_0036_Thorpe_Title\t|1|10|0", "T " .. TIMED .. " 1 0 0 1",
  "T " .. TIMED .. " 2 0 0 1", "T " .. TIMED .. " 3 0 0 1", string.format("S %s 1 %.0f", TIMED, start * 1e9), "K " .. TIMED}, "\n"))
frames(0.5)
local leaf = Swc.quests.task_id(TIMED, 1)
equal(ObjectiveTaskRequestBus.Event.GetUIData(leaf, "TimeLimit"), 10, "the timed leaf has the TimeLimit 10")
local taskStart = ObjectivesComponentRequestBus.Event.GetTaskStartTime(Swc.ids.player, leaf)
near(taskStart and taskStart.seconds or 0, start, 0.01, "GetTaskStartTime gives the start of the root")

-- A recipe with no user wave (Legion Attack): an E line with no task gives no record (ut567 2026-10-29: a lone POI diamond).
local LEGION = "5500585112935202860"
local before = #ObjectivesComponentRequestBus.Event.GetTrackedObjectives(Swc.ids.player)
Swc.HostQuests(table.concat({"E " .. LEGION .. " 38C0B7FF392EF3E226390F6D866CD435\t", "T " .. LEGION .. " 1 0 0 1", "K " .. LEGION,
  "E " .. TIMED .. " 252DC9178EA98EA7\t@MSQ_0036_Thorpe_Title\t|1|10|0", "K " .. TIMED}, "\n"))
frames(0.5)
local after = ObjectivesComponentRequestBus.Event.GetTrackedObjectives(Swc.ids.player)
local named = false
for _, id in ipairs(after) do named = named or id:ToString() == LEGION end
check(not named, "an encounter with no task has no tracked row (" .. #after .. " tracked, " .. before .. " before)")

-- The end of the encounter (no E line): the row goes, and no banner of a failed quest.
failed = 0
feed(false)
frames(1.0)
tracked = ObjectivesComponentRequestBus.Event.GetTrackedObjectives(Swc.ids.player)
check(#tracked == 1 and tracked[1]:ToString() == QUEST, "the end of the encounter: only the quest stays tracked")
equal(failed, 0, "the end of the encounter sends no OnObjectiveFailed")
check(not texts_of(hud):find("1 / 2", 1, true), "the counter row goes")
equal(Swc.errorCount or 0, 0, "no Lua error")
