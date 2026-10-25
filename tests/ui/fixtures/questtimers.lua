-- The quest timer fixture (stage Q8, Linear AST-243; docs/QUESTS.md "The timer row", a client decision): the node
-- Hud.LocalPlayer.MissionTimers.<instance>.TargetTime and the countdown row of the tracker (pinnedobjective.luac) and of the journal.
-- 06_raisealarm with its visible timer 06_raisealarm_01_03_TaskTimer1 (6 s) Active, its start (field 0) 1 s ago: the node is the start
-- plus 6 s, the row counts down ("00:00:05"), and GetTaskStartTime gives the start of field 0. The knife quest with its hidden timer
-- (06_skinningknife_03_swapDelay, 25.4 s) Active: no node, no row. The end of the timer task (Complete): the node and the row go. A relog
-- with a start 10 s ago (the server ends the timer; the time away counts): the row shows 00:00:00 until the server state comes.
-- Picture: questtimers-hud.
local HUD = 2702338936
local hud = screen("LyShineUI/Objectives/Hud/ObjectivesHud.uicanvas", 12, true)
local bannerCanvas = screen("LyShineUI/Banner/Banner.uicanvas", 30)
local journal = screen("LyShineUI/Objectives/Journal/JournalScreen.uicanvas", 40)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(5, 0)
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
local function now()
  return WallClockTimePoint:Now():GetTimeSinceEpoc():ToSeconds()
end
-- A state of one objective: the tasks before the index `at` Complete (2), the task `at` Active (1) with its start, the rest New (0),
-- the containers of `at` Active.
local function state(instance, row, at, startSeconds, extra)
  local lines = {"A " .. instance .. " " .. row.crc .. " 6 0"}
  for _, entry in ipairs(row.tree) do
    local task = D.tasks[entry[2]]
    local s = entry[1] < at and 2 or (entry[1] == at and 1 or 0)
    if extra and extra[entry[1]] then s = extra[entry[1]] end
    lines[#lines + 1] = string.format("T %s %d %d %d %d", instance, entry[1], task.crc, 0, s)
  end
  if startSeconds then
    lines[#lines + 1] = string.format("S %s %d %.0f", instance, at, startSeconds * 1e9)
  end
  lines[#lines + 1] = "K " .. instance
  return table.concat(lines, "\n")
end
local function row_timer(canvas)
  local text = texts_of(canvas)
  return text:match("(%d%d:%d%d:%d%d)")
end

local alarm = D.objectives["06_raisealarm"]
local INSTANCE = "4660"
local path = string.format("Hud.LocalPlayer.MissionTimers.%08x.TargetTime", tonumber(INSTANCE))
-- The timer task 16 Active in its container 17 and the root 37; the whisper tasks 13 to 15 Complete.
local active = {[17] = 1, [37] = 1}
local start = now() - 1
Swc.HostQuests(state(INSTANCE, alarm, 16, start, active))
Swc.HostQuestPinReady(true)
frames(2.0)
local node = Swc.GetData(path)
check(node ~= nil, "the visible Active timer: the node exists")
near(node and node.seconds or 0, start + 6, 0.01, "the node: the start of field 0 plus the Duration 6 s")
local taskStart = ObjectivesComponentRequestBus.Event.GetTaskStartTime(Swc.ids.player, Swc.quests.task_id(INSTANCE, 16))
near(taskStart and taskStart.seconds or 0, start, 0.01, "GetTaskStartTime: the start of field 0")
frames(1.0)
note("the tracker: " .. texts_of(hud))
local shown = row_timer(hud)
-- The frames of the fixture do not move the wall clock: about 5 s remain.
check(shown == "00:00:05" or shown == "00:00:04", "the row shows the remainder of the server deadline (" .. tostring(shown) .. ")")
picture("questtimers-hud")

-- The end of the timer task (the server state: task 16 Complete, the next container Active): the node and the row go.
Swc.HostQuests(state(INSTANCE, alarm, 18, nil, {[25] = 1, [37] = 1}))
frames(1.5)
equal(Swc.GetData(path), nil, "the timer task Complete: no node")
check(row_timer(hud) == nil, "the timer task Complete: no row (" .. texts_of(hud) .. ")")

-- The knife quest with its hidden timer Active: no node, no row.
local knife = D.objectives["06_skinningknife"]
local KNIFE = "4661"
local knifePath = string.format("Hud.LocalPlayer.MissionTimers.%08x.TargetTime", tonumber(KNIFE))
local swap
for _, entry in ipairs(knife.tree) do
  if D.tasks[entry[2]].id == "06_skinningknife_03_swapDelay" then swap = entry[1] end
end
Swc.HostQuests(state(KNIFE, knife, swap, now(), {[17] = 1, [18] = 1, [19] = 1}))
frames(1.5)
equal(Swc.GetData(knifePath), nil, "a hidden timer: no node (the retail red 00:00:00 row does not show)")
check(row_timer(hud) == nil, "a hidden timer: no row")

-- A relog with the timer of 06_raisealarm started 10 s ago (the time away counts): the deadline is past; the row shows 00:00:00 until the
-- server state ends the task.
Swc.HostQuests("")
frames(0.5)
Swc.HostQuests(state(INSTANCE, alarm, 16, now() - 10, active))
frames(1.5)
check(Swc.GetData(path) ~= nil, "the relog: the node from the start of field 0")
equal(row_timer(hud), "00:00:00", "the relog after the end: the row shows 00:00:00 until the server ends the task")

-- The journal row of the same objective.
key("j")
frames(1.5)
note("the journal: " .. texts_of(journal))
check(row_timer(journal) ~= nil, "the journal shows the timer row")
key("escape")
frames(0.5)

-- The failure (backend quests.md "The end of an instance": a failure timer, TaskDie): the server removes the instance with no entry in the
-- completed list. bannertriggers.luac OnObjectiveFailed: the banner "@objective_failed" with the title of the quest; the tracker row goes.
local failed = {}
ObjectivesComponentNotificationsBus.Connect({OnObjectiveFailed = function(_, id) failed[#failed + 1] = id end}, Swc.ids.player)
Swc.HostQuests("")
frames(2.5)
equal(#failed, 1, "the removal with no completed entry: OnObjectiveFailed")
check(not texts_of(hud):find("Bearer of Bad Tidings", 1, true), "the failed quest leaves the tracker")
-- The banner queue shows one card at a time (the cards of the earlier states first): wait for the failure card.
local bannerText = ""
for _ = 1, 30 do
  bannerText = texts_of(bannerCanvas)
  if bannerText:lower():find("failed", 1, true) then break end
  frames(1.0)
end
note("the banner after the failure: " .. bannerText)
check(bannerText:lower():find("failed", 1, true) ~= nil and bannerText:find("Bearer of Bad Tidings", 1, true) ~= nil,
  "the failure banner: Objective Failed with the title of the quest")
picture("questtimers-failed")
-- A hand-in (the completed entry "C instance"): OnObjectiveCompleted, no failure.
Swc.HostQuests(state(INSTANCE, alarm, 16, now() - 1, active))
frames(1.0)
failed = {}
Swc.HostQuests("C " .. INSTANCE)
frames(1.5)
equal(#failed, 0, "a removal with the completed entry: no OnObjectiveFailed")
equal(Swc.GetData(path), nil, "the end of the objective: no timer node")
