-- The quest pins (stage Q2, Linear AST-237; docs/QUESTS.md "The pins (Q2)"; research quest-plan.md Q2): ONE list of the task positions of
-- the objectives of the character, and the native part of the Objectives component that publishes it. The map layer
-- (lyshineui/worldmap/objectiveslayer.luac) and the compass (compass/compass.luac) get the list through the events of
-- ObjectivesComponentNotificationsBus; the journal Map action and the Map button of the tracker read the map layer
-- (DynamicBus.ObjectivesLayer.GetObjectivePosition, RequestObjectivePositionForObjectiveId); the world pins read Swc.questPins.list.
-- The trigger geometry of a task is not in this list: the backend checks the areas, and the area display reads its own records
-- (quest-pins.json "areas").
--
-- The events (the handlers of the two scripts):
--   OnObjectiveTaskPositionAdded(taskId, position, isTracked, territoryId): position a Vector3 (m); the map layer puts the icon on the POI
--     of territoryId when it is not 0; the compass drops territoryId when the task has an OverrideLocation, takes the height of the
--     territory when position.z is 0, and shows the task when isTracked.
--   OnObjectiveTaskPositionsRemoved(taskId, territoryId).
--
-- The rules [I] (the native producer is not proved; the study research/quest-pins.md is asked, the brief briefs/q2-pin-publisher.md):
--   - Each objective of the character gives positions (the map shows each active objective; isTracked: the pinned list after the pin
--     readiness, as IsObjectiveTracked).
--   - The tasks: from the root, the Active (1) tasks; a task gives a position when no Active task under it gives one (the deepest; a
--     hidden task too). In the start quest 06_getoffbeach that is the trigger task of the beach, not the hidden root.
--   - The position of a task: its OverrideLocation, else the centre of the POI of its first POITag, else the centre of its TerritoryID
--     (height 0). territoryId: the POI, else the TerritoryID, else 0.
--   - An objective that is ready for the hand-in (its root Complete or CompleteActive) gives one position on its root task: the place of
--     the NPC of NpcDestinationId (the persons data) that is nearest to the player. Before that, no hand-in position.
--   - The changes: after each change of the state, the pinned list, or the pin readiness (swc/quests.lua Q.Changed), a new task position
--     or a changed one gives OnObjectiveTaskPositionAdded, a task with no position now gives OnObjectiveTaskPositionsRemoved;
--     RequestPublishPoiLocations gives OnObjectiveTaskPositionAdded again for each position.
--
-- The data: gamedata/quest-pins.json (converter swc_convert/questpins.py: the marker cells of the tasks, the POI centres, the hand-in NPC
-- places).

local Swc = Swc
local Q = Swc.quests

local P = {list = {}, byKey = {}}
Swc.questPins = P

local ACTIVE = eObjectiveTaskState_Active or 1

local data
local function D()
  if data == nil then
    data = swcui.load_json("gamedata/quest-pins.json") or false
    if not data then
      Swc.Note("no quest pin data (python3 -B -m swc_convert.questpins write): the quests have no positions")
    end
  end
  return data or {tasks = {}, objectives = {}, npcs = {}}
end
P.D = D

local function V3(p)
  return Swc.Vector3.new({x = p[1] or 0, y = p[2] or 0, z = p[3] or 0})
end

local function name_of(node)
  return node and node.row and string.lower(node.row.id or "") or ""
end

--- @return The marker record of a task (quest-pins.json "tasks"), or nil.
function P.TaskRecord(node)
  return D().tasks[name_of(node)]
end

--- @return The position {x, y, z} of a task record and its territory id, or nil.
local function source_of(t)
  if not t then
    return nil
  end
  local territory = t.poi or t.territory or 0
  if t.override then
    return t.override, territory
  elseif t.poiCenter then
    return t.poiCenter, territory
  elseif t.territory then
    local shape = MapComponentBus.Broadcast.GetTerritoryPosition(t.territory)
    if shape and (shape.x ~= 0 or shape.y ~= 0) then
      return {shape.x, shape.y, 0}, territory
    end
  end
  return nil
end

local function player_position()
  local p = Swc.GetData and Swc.GetData("Hud.LocalPlayer.Position")
  return p and p.x or 0, p and p.y or 0
end

local function nearest(places)
  local px, py = player_position()
  local best, bestD
  for _, place in ipairs(places or {}) do
    local d = (place[1] - px) ^ 2 + (place[2] - py) ^ 2
    if not bestD or d < bestD then
      best, bestD = place, d
    end
  end
  return best
end

local function add_pin(out, rec, index, position, territory, kind, record)
  local key = rec.key .. ":" .. index
  out[#out + 1] = {key = key, taskId = Q.task_id(rec.key, index), objective = rec.key, index = index, kind = kind,
                   position = position, territory = territory or 0, record = record}
end

--- The deepest Active tasks with a source under an index. @return true when the index or a task under it gave a position.
local function collect(rec, index, out)
  local node = rec.nodes[index]
  if not node or (Q.state_of(rec, index)) ~= ACTIVE then
    return false
  end
  local found = false
  for _, child in ipairs(node.children) do
    found = collect(rec, child, out) or found
  end
  if not found then
    local t = P.TaskRecord(node)
    local position, territory = source_of(t)
    if position then
      add_pin(out, rec, index, position, territory, "task", t)
      found = true
    end
  end
  return found
end

--- @return The pin list of the quest state now, and the pins by task key.
local function build()
  local list = {}
  local tracked = {}
  for _, id in ipairs(Q.tracked_ids()) do
    tracked[id.key] = true
  end
  for _, rec in ipairs(Q.list) do
    if rec.root then
      local rootState = Q.state_of(rec, rec.root)
      if rootState == 2 or rootState == 3 then
        local o = D().objectives[string.lower(rec.row and rec.row.id or "")]
        local place = o and o.npc and nearest(D().npcs[string.lower(o.npc)])
        if place then
          add_pin(list, rec, rec.root, place, 0, "handin", o)
        end
      else
        collect(rec, rec.root, list)
      end
    end
  end
  local byKey = {}
  for _, pin in ipairs(list) do
    pin.tracked = tracked[pin.objective] == true
    byKey[pin.key] = pin
  end
  return list, byKey
end

local function send_added(pin)
  Q.notify_component("OnObjectiveTaskPositionAdded", pin.taskId, V3(pin.position), pin.tracked, pin.territory)
end

local function same(a, b)
  return a.territory == b.territory and a.tracked == b.tracked and a.position[1] == b.position[1] and a.position[2] == b.position[2]
    and a.position[3] == b.position[3]
end

--- Builds the list again and sends the changes (swc/quests.lua calls it after each change).
function P.Update()
  local list, byKey = build()
  local before = P.byKey
  P.list, P.byKey = list, byKey
  for key, old in pairs(before) do
    if not byKey[key] then
      Q.notify_component("OnObjectiveTaskPositionsRemoved", old.taskId, old.territory)
    end
  end
  local added = 0
  for _, pin in ipairs(list) do
    local old = before[pin.key]
    if not old or not same(old, pin) then
      send_added(pin)
      added = added + 1
    end
  end
  if added > 0 then
    Swc.Note(string.format("quest pins: %d position(s), %d new or changed", #list, added))
  end
end
Q.Changed = P.Update

--- @return The pins of an objective (an instance key).
function P.OfObjective(key)
  local out = {}
  for _, pin in ipairs(P.list) do
    if pin.objective == key then
      out[#out + 1] = pin
    end
  end
  return out
end

-- ------------------------------------------------------------------------------------------------------------- the buses

Swc.Define("ObjectivesComponentRequestBus", "Event", {
  -- The map layer asks for all positions when its icons are ready (objectiveslayer.luac UpdatePublishedLocations).
  RequestPublishPoiLocations = function()
    for _, pin in ipairs(P.list) do
      send_added(pin)
    end
  end,
  -- The objectives by the distance of their nearest position to the player (compass.luac: result[1].second with
  -- eObjectiveOrder_Distance; the type of the native result is not proved [I]: a list of {first = order, second = ids}).
  GetObjectivesOrdered = function(_, order)
    local px, py = player_position()
    local bestByKey = {}
    for _, pin in ipairs(P.list) do
      local d = (pin.position[1] - px) ^ 2 + (pin.position[2] - py) ^ 2
      if not bestByKey[pin.objective] or d < bestByKey[pin.objective] then
        bestByKey[pin.objective] = d
      end
    end
    local keys = {}
    for key in pairs(bestByKey) do
      keys[#keys + 1] = key
    end
    table.sort(keys, function(a, b) return bestByKey[a] < bestByKey[b] end)
    local ids = {}
    for i, key in ipairs(keys) do
      ids[i] = Q.objective_id(key)
    end
    return {{first = order, second = ids}}
  end,
})

Swc.Define("ObjectiveTaskRequestBus", "Event", {
  -- The territory of a task (objectivesdatahandler.luac GetTerritoryIdForObjectiveTask): the POI of its POITag, else its TerritoryID [I].
  GetTaskTerritoryId = function(taskId)
    local t = P.TaskRecord((Q.node_of(taskId)))
    return t and (t.poi or t.territory) or 0
  end,
})
