-- The quest pin fixture (stage Q2, Linear AST-237; docs/QUESTS.md "The pins (Q2, AST-237)"; research/quest-pins.md): the one pin list of
-- swc/questpins.lua for the start quest of a new character (06_getoffbeach, the state of the quests fixture), and the original readers
-- of the list: the compass (compass.luac), the map layer (objectiveslayer.luac of MagicMap), the journal Map action and the Map button of
-- the tracker (both through DynamicBus.ObjectivesLayer), and the world pin (the marker type DiegeticObjectivePin of the static pool w:
-- staticworldmarker.luac).
-- The steps (study section 7): the start state publishes two handles, the hidden root 16 (9613.6416 2329.4722 43.0) and the trigger task
-- 9 (9621.61 2309.48 40.24), both with POI 0, not tracked before the pin readiness and tracked after it; the compass and the map layer
-- keep both; the world pin is only on task 9 (ShowDiegeticTaskPin), at Z 40.24 + 1.75 = 41.99; the trigger complete removes task 9 and
-- its world pin, the root stays; the root complete gives the hand-in position at the NPC 01_AelstromWW_01; an empty state removes it.
-- Needs gamedata/quest-pins.json of format 2 (python3 -B -m swc_convert.questpins write). Pictures: questpins-compass, questpins-world,
-- questpins-map.
local HUD, MAP = 2702338936, 2477632187
local hud = screen("LyShineUI/Objectives/Hud/ObjectivesHud.uicanvas", 12, true)
local compass = screen("LyShineUI/Compass/Compass.uicanvas", 14, true)
local map = screen("LyShineUI/WorldMap/MagicMap.uicanvas", 20)
local markers = screen("LyShineUI/Markers/MarkerManager.uicanvas", 10, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(1, 0)
-- The start place of the beach (the backend start of a new character is near; the pin is 40 m to the north-east), heading north-east.
Swc.HostView(-45, 9590, 2280, 40)
frames(0.5)

check(next(Swc.questPins.D().tasks) ~= nil, "quest-pins.json has the task records")

-- The events of the producer, as the scripts get them.
local events = {}
ObjectivesComponentNotificationsBus.Connect({
  OnObjectiveTaskPositionAdded = function(_, task, position, tracked, territory)
    events[#events + 1] = {kind = "added", task = task, x = position.x, y = position.y, z = position.z, tracked = tracked, territory = territory}
  end,
  OnObjectiveTaskPositionsRemoved = function(_, task, tracked)
    events[#events + 1] = {kind = "removed", task = task, tracked = tracked}
  end,
}, Swc.ids.objectives)
local function take()
  local out = events
  events = {}
  return out
end

-- The state (the quests fixture): {state, progress} of each index of the tree of 06_getoffbeach.
local INSTANCE = "1311768467294899695"
local START = {{2, 1}, {3, 1}, {2, 1}, {2, 1}, {3, 4}, {2, 1}, {2, 1}, {1, 0}, {1, 0}, {1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 0}, {1, 0}, {1, 3}}
local function state_text(states)
  local row = Swc.quests.D().objectives["06_getoffbeach"]
  local lines = {"A " .. INSTANCE .. " " .. row.crc .. " 6 0"}
  for _, entry in ipairs(row.tree) do
    local index = entry[1]
    local task = Swc.quests.D().tasks[entry[2]]
    lines[#lines + 1] = string.format("T %s %d %d %d %d", INSTANCE, index, task.crc, states[index + 1][2], states[index + 1][1])
  end
  lines[#lines + 1] = "K " .. INSTANCE
  return table.concat(lines, "\n")
end
local function with(changes)
  local out = {}
  for i, s in ipairs(START) do
    out[i] = changes[i - 1] or s
  end
  return out
end

-- 1. The start state: two handles, the root 16 and the trigger task 9 (study 7, steps 1 to 3).
Swc.HostQuests(state_text(START))
frames(1.0)
local e = take()
equal(#Swc.questPins.list, 2, "the start state has two quest positions")
local byIndex = {}
for _, each in ipairs(Swc.questPins.list) do
  byIndex[each.index] = each
end
local trigger, root = byIndex[9], byIndex[16]
check(trigger ~= nil and root ~= nil, "the positions are the trigger task 9 and the hidden root 16")
if trigger and root then
  near(trigger.position[1], 9621.61, 0.001, "the trigger position x (its OverrideLocation)")
  near(trigger.position[3], 40.24, 0.001, "the trigger position z")
  near(root.position[1], 9613.6416, 0.001, "the root position x (its OverrideLocation)")
  near(root.position[2], 2329.4722, 0.001, "the root position y")
  equal(trigger.territory + root.territory, 0, "an override position has POI 0")
  near(math.sqrt((trigger.position[1] - root.position[1]) ^ 2 + (trigger.position[2] - root.position[2]) ^ 2
    + (trigger.position[3] - root.position[3]) ^ 2), 21.70, 0.01, "the two handles are 21.70 m apart")
end
equal(#e, 2, "the start state sends two OnObjectiveTaskPositionAdded")
check(e[1] and e[1].kind == "added" and e[1].tracked == false and e[2] and e[2].tracked == false,
  "before the pin readiness the positions are not tracked")
equal(e[1] and e[1].task.objectiveInstanceId.key, INSTANCE, "the task id has the field objectiveInstanceId")
check(e[1] and (e[1].task.taskIndex == 9 or e[1].task.taskIndex == 16), "the task id has the field taskIndex")
local taskId = trigger and trigger.taskId
local override = taskId and ObjectiveTaskRequestBus.Event.GetUIData(taskId, "OverrideLocation")
check(type(override) == "table" and #override == 1 and math.abs(override[1].x - 9621.61) < 0.001,
  "GetUIData OverrideLocation is a vector of one Vector3")
local container = Swc.quests.task_id(INSTANCE, 14)
equal(#ObjectiveTaskRequestBus.Event.GetUIData(container, "OverrideLocation"), 0, "a task with no override has an empty vector")
equal(ObjectiveTaskRequestBus.Event.GetUIData(container, "StayActive"), false, "GetUIData StayActive is a boolean")
equal(TerritoryDefinitionsDataManagerBus.Broadcast.GetTerritoryIdByPoiTag("Drowned_Shipwreck_2_40142"), 40142,
  "GetTerritoryIdByPoiTag gives the definition of the tag (any case)")

-- 2. The pin readiness: the position again, tracked; the compass shows the task.
Swc.HostQuestPinReady(true)
frames(1.5)
e = take()
check(#e == 2 and e[1].kind == "added" and e[1].tracked == true and e[2].tracked == true, "after the pin readiness the positions are tracked")
local compassScript = script_of(find(swcui.canvas_get(compass, "root"), "Compass"), "compass.luac")
check(compassScript ~= nil, "the compass script is loaded")
local positions = compassScript and compassScript.objectivePositions
local stored = positions and positions[tonumber(INSTANCE)] or {}
check(stored[9] ~= nil and stored[16] ~= nil, "the compass keeps the positions of task 9 and the root 16")
check(stored[9] and stored[9].isTracked == true, "the compass tracks the task")
picture("questpins-compass")

-- 2b. The world pin: the camera 40 m to the west of the trigger, at the height of the pin (41.99 m), along +X of Unreal (the east; the
-- plates fixture matrix). The pin of task 9 (ShowDiegeticTaskPin) takes the slot w0 of the static pool and shows in the middle of the
-- view; the root 16 has no pin.
local TX, TY, TZ = 9621.61, 2309.48, 40.24
Swc.HostView(-90, TX - 40, TY, TZ)
local p00, p11, lx, ly, lz = 1.0, 1920 / 1080, (TX - 40) * 100, -TY * 100, (TZ + 1.75) * 100
Swc.HostViewProjection(0, 0, 0, 1, p00, 0, 0, 0, 0, p11, 0, 0, -ly * p00, -lz * p11, 10, -lx, 1920, 1080, lx, ly, lz)
Swc.HostPlates("")
frames(0.5)
Swc.HostPlates("")
frames(0.5)
local P = Swc.markers.params.DiegeticObjectivePin
check(P ~= nil and P.fadeStart == 100 and P.fadeDistance == 150, "the marker params of DiegeticObjectivePin (the fade 100 m to 150 m)")
local slot = Swc.markers.questSlots[0]
check(slot ~= nil and slot.quest ~= nil and slot.quest.pin.index == 9, "the world pin of task 9 has the slot w0")
near(slot and slot.quest and slot.quest.z or 0, 41.99, 0.001, "the world pin height: 40.24 + 1.75 m (a blank DiegeticPinZOverride)")
check(Swc.markers.questSlots[1] == nil or Swc.markers.questSlots[1].quest == nil, "the root 16 has no world pin (no ShowDiegeticTaskPin)")
local object = slot and slot.markerId and Swc.markers.objects[tostring(slot.markerId)]
local pinElement = object and object.element
check(pinElement ~= nil, "the slot w0 has a marker element")
if pinElement then
  check(swcui.enabled_in_hierarchy(pinElement), "the world pin shows")
  local px, py = swcui.viewport_position(pinElement)
  near(px, 960, 2, "the world pin is in the middle of the view (the trigger is ahead)")
  near(py, 540, 2, "the world pin is at the height of the camera (41.99 m)")
  near(swcui.get(pinElement, "fader.fade"), 1, 0.01, "the world pin at 40 m has the full alpha")
end
note("the world pin point: " .. Swc.QuestPinPoint(INSTANCE .. ":9"))
-- The compass places its icons in OnCrySystemPostViewSystemUpdate (each frame, swc/host.lua) after a change of the player position.
local compassIcon
for _, m in ipairs(compassScript and compassScript.uiMarkers or {}) do
  if m.iconData and m.iconData.position and math.abs(m.iconData.position.x - TX) < 0.01 then
    compassIcon = m
  end
end
check(compassIcon ~= nil, "the compass has an icon at the trigger position")
if compassIcon then
  local x = swcui.viewport_position(compassIcon.entityId)
  near(x, 960, 4, "the compass icon is in the middle of the strip (the pin is ahead, to the east)")
end
picture("questpins-world")

-- 3. The map layer has the icon: the journal Map action and the tracker Map button get the position from it.
ObjectivesComponentRequestBus.Event.RequestPublishPoiLocations(Swc.ids.player)
frames(0.5)
e = take()
equal(#e, 2, "RequestPublishPoiLocations sends the two positions again")
local objectiveId = ObjectivesComponentRequestBus.Event.GetObjectives(Swc.ids.player)[1]
local onMap = DynamicBus.ObjectivesLayer.Broadcast.GetObjectivePosition(objectiveId)
check(onMap ~= nil, "the map layer has a position for the objective (the journal Map action)")
if onMap then
  check(math.abs(onMap.x - 9621.61) < 0.01 or math.abs(onMap.x - 9613.6416) < 0.01,
    "the map position is one of the two published positions (" .. onMap.x .. " " .. onMap.y .. ")")
end
key("m")
frames(1.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), MAP, "M opens the map")
picture("questpins-map")
key("m")
frames(1.0)

-- 4. The trigger complete: task 9 and its world pin go; the root 16 stays (study 7, step 6).
-- (The map of step 3 asked RequestPublishPoiLocations again when it opened: objectiveslayer.luac UpdatePublishedLocations.)
check(#take() >= 2, "the map asks for the positions again when it opens")
Swc.HostQuests(state_text(with({[9] = {2, 1}, [10] = {1, 0}})))
frames(1.0)
e = take()
equal(#Swc.questPins.list, 1, "after the trigger one position")
pin = Swc.questPins.list[1]
equal(pin and pin.index, 16, "the position is the root task (index 16)")
local removed, added = 0, 0
for _, each in ipairs(e) do
  removed = removed + (each.kind == "removed" and each.task.taskIndex == 9 and each.tracked == true and 1 or 0)
  added = added + (each.kind == "added" and 1 or 0)
end
equal(removed, 1, "the trigger position is removed (with isTracked)")
equal(added, 0, "the root position does not change")
Swc.HostPlates("")
frames(0.5)
check(Swc.markers.questSlots[0] and Swc.markers.questSlots[0].quest == nil, "no world pin after the trigger")
if pinElement then
  check(not swcui.enabled_in_hierarchy(pinElement), "the world pin of the trigger hides")
end

-- 5. The root complete: the hand-in position at the NPC of NpcDestinationId.
local done = {}
for i = 0, 16 do
  done[i] = {2, START[i + 1][2]}
end
Swc.HostQuests(state_text(with(done)))
frames(1.0)
take()
pin = Swc.questPins.list[1]
check(pin and pin.kind == "handin", "the root complete gives the hand-in position")
near(pin and pin.position[1] or 0, 9613.356, 0.01, "the hand-in position is the place of 01_AelstromWW_01 (x)")
check(ObjectivesComponentRequestBus.Event.IsObjectiveReadyForCompletion(Swc.ids.player, objectiveId), "the objective is ready for the hand-in")

-- 6. An empty state: the position goes away.
Swc.HostQuests("")
frames(1.0)
e = take()
equal(#Swc.questPins.list, 0, "an empty state has no position")
check(#e == 1 and e[1].kind == "removed", "an empty state sends OnObjectiveTaskPositionsRemoved")
