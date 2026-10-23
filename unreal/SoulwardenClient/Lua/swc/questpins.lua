-- The quest pins (stage Q2, Linear AST-237; docs/QUESTS.md "The pins (Q2, AST-237)"; research/quest-pins.md, the study of the native
-- producer): ONE list of the published task positions of the objectives of the character, and the native part of the Objectives
-- component that publishes it. The map layer (lyshineui/worldmap/objectiveslayer.luac) and the compass (compass/compass.luac) get the
-- list through the events of ObjectivesComponentNotificationsBus; the journal Map action and the Map button of the tracker read the map
-- layer (DynamicBus.ObjectivesLayer.GetObjectivePosition); the world pins (swc/markers.lua) read Swc.questPins.list.
--
-- The events (study 1.1): OnObjectiveTaskPositionAdded(taskId, position Vector3 (m), isTracked, poiId u16) and
-- OnObjectiveTaskPositionsRemoved(taskId, isTracked). The task id has objectiveInstanceId and taskIndex.
--
-- The publisher (study 1.2, 2.1, 2.2; [R] unless marked):
--   - each objective of the character (not only the tracked ones; isTracked is an argument: IsObjectiveTracked);
--   - each task that is enabled and unfinished: state Active (1) or CompleteActive (3) and progress < target. No IsHidden or HideChildren
--     test: an active hidden container with a source publishes (the beach root 16) next to its active child (the trigger 9);
--   - the source of a task: the nearest point (full XYZ to the player) of its OverrideLocation, poiId 0; else, with POITag and no
--     GameModeID, the nearest POI point of the tags (inside TerritoryID when the task has one; else the first POI point of the tags),
--     poiId the POI; else no position (removal). TerritoryID is not a position. The POI point is the POI placement of the backend copy:
--     the native provider point is not proved [U];
--   - a conversation task (type code 16): the nearest point of its override, else the nearest place of the NPC of ItemName, poiId 0;
--   - an objective ready for the hand-in (the root Complete or CompleteActive): one more position, on the root task id [I] (the native
--     handle comes from the last task record of the root; the root is the last record of the tree), at the place of the
--     NpcDestinationId NPC nearest to the player, poiId 0. The native completion resolver is not traced [U]; before the readiness no
--     hand-in position (HideIncompleteTurninMarker);
--   - not done [U]: the suppression byte +0x302 of a task, the game mode and mutation test (+0xC0), the dynamic mission locations.
-- The changes: after each change of the state, the pinned list, or the pin readiness (swc/quests.lua Q.Changed), and each 2 s with the
-- player position (the nearest choices) [I], a new or changed position gives OnObjectiveTaskPositionAdded, a task with no position now
-- gives OnObjectiveTaskPositionsRemoved; RequestPublishPoiLocations sends each position again.
--
-- The data: gamedata/quest-pins.json (converter swc_convert/questpins.py).

local Swc = Swc
local Q = Swc.quests

local P = {list = {}, byKey = {}, revision = 0}
Swc.questPins = P

local ACTIVE, COMPLETE, COMPLETE_ACTIVE = eObjectiveTaskState_Active or 1, eObjectiveTaskState_Complete or 2,
  eObjectiveTaskState_CompleteActive or 3
local CONVERSATION_CODE = 16

local data
local function D()
  if data == nil then
    data = swcui.load_json("gamedata/quest-pins.json") or false
    if not data then
      Swc.Note("no quest pin data (python3 -B -m swc_convert.questpins write): the quests have no positions")
    elseif data.format ~= 2 then
      Swc.Note("quest pin data of format " .. tostring(data.format) .. " (the client reads format 2): the quests have no positions")
      data = false
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

--- @return The marker record of a task node (quest-pins.json "tasks"), or nil.
function P.TaskRecord(node)
  return D().tasks[name_of(node)]
end

--- @return The OverrideLocation of a task node as the native UI data: a list of Vector3 (empty with no cell).
function P.OverrideVector(node)
  local out = {}
  local t = P.TaskRecord(node)
  for i, point in ipairs(t and t.override or {}) do
    out[i] = V3(point)
  end
  return out
end

--- @return The position of the player {x, y, z} (m).
local function player_position()
  local p = Swc.GetData and Swc.GetData("Hud.LocalPlayer.Position")
  return {p and p.x or 0, p and p.y or 0, p and p.z or 0}
end
P.PlayerPosition = player_position

local function distance2(a, b)
  return (a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2
end

--- @return The nearest point of a list (full XYZ; the first on an equal distance), or nil.
local function nearest(points, own)
  local best, bestD
  for _, point in ipairs(points or {}) do
    local d = distance2(point, own)
    if not bestD or d < bestD then
      best, bestD = point, d
    end
  end
  return best
end

local function inside_territory(point, territory)
  for _, id in ipairs(MapComponentBus.Broadcast.GetContainingTerritories(V3(point)) or {}) do
    if id == territory then
      return true
    end
  end
  return false
end

--- The POI choice of a task (study 2.1, 0x3AD3CE0): the nearest nonzero POI point of the tags (inside TerritoryID when the task has
--- one); else the first POI point of the tags. @return The point {x, y, z} and the POI id, or nil.
local function poi_of(t, own)
  local best, bestId, bestD
  for _, entry in ipairs(t.pois or {}) do
    local point = {entry[2], entry[3], entry[4]}
    local nonzero = point[1] ~= 0 or point[2] ~= 0 or point[3] ~= 0
    if nonzero and (not t.territory or inside_territory(point, t.territory)) then
      local d = distance2(point, own)
      if not bestD or d < bestD then
        best, bestId, bestD = point, entry[1], d
      end
    end
  end
  if not best and t.pois and t.pois[1] then
    local entry = t.pois[1]
    best, bestId = {entry[2], entry[3], entry[4]}, entry[1]
  end
  return best, bestId
end

--- The position of a task (the resolver): @return the point {x, y, z} and the POI id (0: none), or nil (no position).
function P.Resolve(node, own)
  local t = P.TaskRecord(node)
  if not t then
    return nil
  end
  own = own or player_position()
  if t.override and #t.override > 0 then
    return nearest(t.override, own), 0
  end
  if node.row and node.row.code == CONVERSATION_CODE then
    local place = t.npc and nearest(D().npcs[string.lower(t.npc)], own)
    if place then
      return place, 0
    end
    return nil
  end
  if t.pois and not t.gameMode then
    local point, id = poi_of(t, own)
    if point then
      return point, id
    end
  end
  return nil
end

local function enabled_unfinished(rec, index)
  local state, progress = Q.state_of(rec, index)
  return (state == ACTIVE or state == COMPLETE_ACTIVE) and progress < Q.target_of(rec.nodes[index])
end
P.EnabledUnfinished = enabled_unfinished

--- @return The pin list of the quest state now, and the pins by task key.
local function build()
  local list = {}
  local tracked = {}
  for _, id in ipairs(Q.tracked_ids()) do
    tracked[id.key] = true
  end
  local own = player_position()
  local seen = {}
  for _, rec in ipairs(Q.list) do
    local indexes = {}
    for index in pairs(rec.nodes) do
      indexes[#indexes + 1] = index
    end
    table.sort(indexes)
    for _, index in ipairs(indexes) do
      if enabled_unfinished(rec, index) then
        local point, poi = P.Resolve(rec.nodes[index], own)
        if point then
          seen[rec.key .. ":" .. index] = true
          list[#list + 1] = {key = rec.key .. ":" .. index, taskId = Q.task_id(rec.key, index), objective = rec.key, index = index,
                             kind = "task", position = point, territory = poi or 0, record = P.TaskRecord(rec.nodes[index])}
        end
      end
    end
    -- The completion position of a ready objective (study 1.2 and 2.2).
    if rec.root then
      local rootState = Q.state_of(rec, rec.root)
      if rootState == COMPLETE or rootState == COMPLETE_ACTIVE then
        local o = D().objectives[string.lower(rec.row and rec.row.id or "")]
        local place = o and o.npc and nearest(D().npcs[string.lower(o.npc)], own)
        local key = rec.key .. ":" .. rec.root
        if place and not seen[key] then
          list[#list + 1] = {key = key, taskId = Q.task_id(rec.key, rec.root), objective = rec.key, index = rec.root, kind = "handin",
                             position = place, territory = 0, record = o}
        end
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
  return a.territory == b.territory and a.tracked == b.tracked and a.kind == b.kind and a.position[1] == b.position[1]
    and a.position[2] == b.position[2] and a.position[3] == b.position[3]
end

--- Builds the list again and sends the changes (swc/quests.lua calls it after each change; swc/markers.lua each 2 s).
function P.Update()
  local list, byKey = build()
  local before = P.byKey
  P.list, P.byKey = list, byKey
  local removed, added = 0, 0
  for key, old in pairs(before) do
    if not byKey[key] then
      Q.notify_component("OnObjectiveTaskPositionsRemoved", old.taskId, old.tracked)
      removed = removed + 1
    end
  end
  for _, pin in ipairs(list) do
    local old = before[pin.key]
    if not old or not same(old, pin) then
      send_added(pin)
      added = added + 1
    end
  end
  if added + removed > 0 then
    P.revision = P.revision + 1
    local keys = {}
    for _, pin in ipairs(list) do
      keys[#keys + 1] = string.format("%s %s %.2f %.2f %.2f poi %d%s", pin.key, pin.kind, pin.position[1], pin.position[2],
        pin.position[3], pin.territory, pin.tracked and " tracked" or "")
    end
    Swc.Note(string.format("quest pins: %d position(s), %d new or changed, %d removed: %s", #list, added, removed,
      table.concat(keys, "; ")))
  end
end
Q.Changed = P.Update

-- The refresh with the player position (the nearest choices of a task with several points): each 2 s [I] (the native schedule of the
-- nearest choice is not recovered, study 1.3).
local lastRefresh = -math.huge
function P.Refresh()
  if (Swc.time or 0) - lastRefresh >= 2 then
    lastRefresh = Swc.time or 0
    if #Q.list > 0 or next(P.byKey) then
      P.Update()
    end
  end
end

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
  -- The world pin shows only for an objective of the character (staticworldmarker.luac: the observer of DiegeticPinObjectiveInstanceId).
  HasObjectiveInstanceId = function(_, id)
    return id ~= nil and type(id) == "userdata" and Q.byKey[id.key] ~= nil
  end,
  -- The map layer asks for all positions when its icons are ready (objectiveslayer.luac UpdatePublishedLocations).
  RequestPublishPoiLocations = function()
    for _, pin in ipairs(P.list) do
      send_added(pin)
    end
  end,
  -- The objectives by the squared XYZ distance of their nearest published position (study 3.2, 0x3A3E680: groups of objective ids; a
  -- group of the objectives with no usable point). compass.luac reads result[1].second. The type-specific distance resolvers and the tie
  -- rule are not recovered [U].
  GetObjectivesOrdered = function(_, order)
    local own = player_position()
    local best = {}
    for _, pin in ipairs(P.list) do
      local d = distance2(pin.position, own)
      if not best[pin.objective] or d < best[pin.objective] then
        best[pin.objective] = d
      end
    end
    local near, none = {}, {}
    for _, rec in ipairs(Q.list) do
      if best[rec.key] then
        near[#near + 1] = rec.key
      else
        none[#none + 1] = Q.objective_id(rec.key)
      end
    end
    table.sort(near, function(a, b) return best[a] < best[b] end)
    for i, key in ipairs(near) do
      near[i] = Q.objective_id(key)
    end
    return {{first = order, second = near}, {first = order, second = none}}
  end,
})

Swc.Define("ObjectiveTaskRequestBus", "Event", {
  -- The territory of a task (study 3.2, 0x3AD55E0): the authored TerritoryID when not 0; else the territory of the matching POIs (the
  -- territory of the backend POI point).
  GetTaskTerritoryId = function(taskId)
    local t = P.TaskRecord((Q.node_of(taskId)))
    return t and (t.territory or t.poiTerritory) or 0
  end,
})
