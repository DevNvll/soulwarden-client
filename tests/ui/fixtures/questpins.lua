-- The quest pin fixture (stage Q2, Linear AST-237; docs/QUESTS.md "The pins (Q2)"): the one pin list of swc/questpins.lua for the start
-- quest of a new character (06_getoffbeach, the state of the quests fixture), and the original readers of the list: the compass
-- (compass.luac), the map layer (objectiveslayer.luac of MagicMap), the journal Map action and the Map button of the tracker (both through
-- DynamicBus.ObjectivesLayer), and the world pin (the marker type DiegeticObjectivePin of the static pool w: staticworldmarker.luac). The rules of the list are [I] until research/quest-pins.md.
-- The steps: the start state gives one position, the trigger task of the beach (OverrideLocation 9621.61 2309.48 40.24), not tracked
-- before the pin readiness and tracked after it; the compass and the map layer have it; the trigger complete moves the pin to the hidden
-- root (9613.64 2329.47 43.0, at Aelstrom); the root complete gives the hand-in position at the NPC 01_AelstromWW_01; an empty state
-- removes it.
-- Needs gamedata/quest-pins.json (python3 -B -m swc_convert.questpins write). Pictures: questpins-compass, questpins-world, questpins-map.
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
  OnObjectiveTaskPositionsRemoved = function(_, task, territory)
    events[#events + 1] = {kind = "removed", task = task, territory = territory}
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

-- 1. The start state: one position, the trigger task (index 9) of the beach.
Swc.HostQuests(state_text(START))
frames(1.0)
local e = take()
equal(#Swc.questPins.list, 1, "the start state has one quest position")
local pin = Swc.questPins.list[1]
equal(pin.index, 9, "the position is the trigger task 06_getoffbeach_01_aelstrom_beach_start_trigger (index 9)")
near(pin.position[1], 9621.61, 0.01, "the position x is the OverrideLocation of the trigger")
near(pin.position[2], 2309.48, 0.01, "the position y is the OverrideLocation of the trigger")
equal(#e, 1, "the start state sends one OnObjectiveTaskPositionAdded")
check(e[1] and e[1].kind == "added" and e[1].tracked == false, "before the pin readiness the position is not tracked")
equal(e[1] and e[1].task.objectiveInstanceId.key, INSTANCE, "the task id has the field objectiveInstanceId")
equal(e[1] and e[1].task.taskIndex, 9, "the task id has the field taskIndex")
local taskId = pin.taskId
equal(type(ObjectiveTaskRequestBus.Event.GetUIData(taskId, "OverrideLocation")), "string", "GetUIData OverrideLocation is the text of the cell")
equal(TerritoryDefinitionsDataManagerBus.Broadcast.GetTerritoryIdByPoiTag("Drowned_Shipwreck_2_40142"), 40142,
  "GetTerritoryIdByPoiTag gives the definition of the tag (any case)")

-- 2. The pin readiness: the position again, tracked; the compass shows the task.
Swc.HostQuestPinReady(true)
frames(1.5)
e = take()
check(#e == 1 and e[1].kind == "added" and e[1].tracked == true, "after the pin readiness the position is tracked")
local compassScript = script_of(find(swcui.canvas_get(compass, "root"), "Compass"), "compass.luac")
check(compassScript ~= nil, "the compass script is loaded")
local positions = compassScript and compassScript.objectivePositions
local stored = positions and positions[tonumber(INSTANCE)] and positions[tonumber(INSTANCE)][9]
check(stored ~= nil, "the compass has the position of task 9")
check(stored and stored.isTracked == true, "the compass tracks the task")
picture("questpins-compass")

-- 2b. The world pin: the camera 40 m to the west of the trigger, 1.7 m above it, along +X of Unreal (the east; the plates fixture
-- matrix). The pin of task 9 (ShowDiegeticTaskPin) takes the slot w0 of the static pool and shows in the middle of the view.
local TX, TY, TZ = 9621.61, 2309.48, 40.24
Swc.HostView(-90, TX - 40, TY, TZ)
local p00, p11, lx, ly, lz = 1.0, 1920 / 1080, (TX - 40) * 100, -TY * 100, TZ * 100 + 170
Swc.HostViewProjection(0, 0, 0, 1, p00, 0, 0, 0, 0, p11, 0, 0, -ly * p00, -lz * p11, 10, -lx, 1920, 1080, lx, ly, lz)
Swc.HostPlates("")
frames(0.5)
Swc.HostPlates("")
frames(0.5)
local P = Swc.markers.params.DiegeticObjectivePin
check(P ~= nil and P.fadeStart == 100 and P.fadeDistance == 150, "the marker params of DiegeticObjectivePin (the fade 100 m to 150 m)")
local slot = Swc.markers.questSlots[0]
check(slot ~= nil and slot.quest ~= nil and slot.quest.pin.index == 9, "the world pin of task 9 has the slot w0")
local object = slot and slot.markerId and Swc.markers.objects[tostring(slot.markerId)]
local pinElement = object and object.element
check(pinElement ~= nil, "the slot w0 has a marker element")
if pinElement then
  check(swcui.enabled_in_hierarchy(pinElement), "the world pin shows")
  local x = swcui.viewport_position(pinElement)
  near(x, 960, 2, "the world pin is in the middle of the view (the trigger is ahead)")
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
equal(#e, 1, "RequestPublishPoiLocations sends the one position again")
local objectiveId = ObjectivesComponentRequestBus.Event.GetObjectives(Swc.ids.player)[1]
local onMap = DynamicBus.ObjectivesLayer.Broadcast.GetObjectivePosition(objectiveId)
check(onMap ~= nil, "the map layer has a position for the objective (the journal Map action)")
if onMap then
  near(onMap.x, 9621.61, 0.01, "the map position is the trigger position (x)")
  near(onMap.y, 2309.48, 0.01, "the map position is the trigger position (y)")
end
key("m")
frames(1.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), MAP, "M opens the map")
picture("questpins-map")
key("m")
frames(1.0)

-- 4. The trigger complete: the pin goes to the hidden root (at Aelstrom), the trigger position goes away.
Swc.HostQuests(state_text(with({[9] = {2, 1}, [10] = {1, 0}})))
frames(1.0)
e = take()
equal(#Swc.questPins.list, 1, "after the trigger one position")
pin = Swc.questPins.list[1]
equal(pin and pin.index, 16, "the position is the root task (index 16) with its OverrideLocation")
near(pin and pin.position[1] or 0, 9613.6416, 0.001, "the root position x")
local removed, added = 0, 0
for _, each in ipairs(e) do
  removed = removed + (each.kind == "removed" and each.task.taskIndex == 9 and 1 or 0)
  added = added + (each.kind == "added" and each.task.taskIndex == 16 and 1 or 0)
end
equal(removed, 1, "the trigger position is removed")
equal(added, 1, "the root position is added")
Swc.HostPlates("")
frames(0.5)
check(Swc.markers.questSlots[0] and Swc.markers.questSlots[0].quest == nil, "the root has no ShowDiegeticTaskPin: no world pin")
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
