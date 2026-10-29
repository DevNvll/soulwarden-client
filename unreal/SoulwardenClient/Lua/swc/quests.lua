-- The quests (Linear AST-236; docs/QUESTS.md; research quest-plan.md Q1): the native part of the original quest scripts: the tracker of the
-- HUD (lyshineui/objectives/hud/objectiveshud.luac and pinnedobjective*.luac), the journal (lyshineui/objectives/journal/*.luac, the J key
-- of journalscreen.luac), and the banners (lyshineui/banner/bannertriggers.luac). The model is the Objectives state of the owner (slot 22,
-- type 3857) and the pin readiness (slot 73, type 5606) that UI/SwcUiQuests gives through Swc.HostQuests and Swc.HostQuestPinReady; the
-- static data is gamedata/quests.json (tools/convert-ui.sh quests: the quest data of the backend copy).
--
-- The ids: an objective instance is an ObjectiveInstanceId object (its key: the decimal text of the u64 instance; `value` the number);
-- a task is a task id object of the instance and the index of the tree (its key "INSTANCE:INDEX"). One object for each key, so == of
-- the scripts and the bus addresses (by tostring) agree. The tree of an objective is the tree of the data (child-first indexes, the root
-- last); the state of each index comes from the task elements of the wire (an index with no element is New).
--
-- The rules of the native component that the scripts read:
--   - GetTrackedObjectives and IsObjectiveTracked give the pinned list only after the pin readiness (live check L1 of stage 1.8a: with
--     slot 22 alone the client shows the banner and no pin).
--   - SetObjectiveTracked changes the pinned list of the client and sends it (1458: the request "quest pins I,J"); the backend keeps it
--     and sends no echo.
--   - A container has the number of its children as target (a partial container 1); a leaf its TargetQty (1 when blank).
--   - A new instance gives OnObjectiveAdded (the banner of the quest, the journal); a removed instance with no completed notice gives
--     OnObjectiveRemoved and OnObjectiveFailed; each task change gives the task events of ObjectiveTaskNotificationBus.

local Swc = Swc

-- The nil CRC of the native code (AZ::Crc32(): 0; the scripts compare data ids with GetNilCrc()).
if rawget(_G, "GetNilCrc") == nil then
  rawset(_G, "GetNilCrc", function() return 0 end)
end

local Q = {list = {}, byKey = {}, tracked = {}, ready = false, ids = {}, taskIds = {}, revision = 0}
Swc.quests = Q
Swc.Handled("ObjectivesComponentNotificationsBus", "ObjectiveNotificationBus", "ObjectiveTaskNotificationBus")

local function crc(text)
  return swcui.crc32(string.lower(text or ""))
end

-- ------------------------------------------------------------------------------------------------------------- the data

local function D()
  if Q.data then
    return Q.data
  end
  local doc = swcui.load_json("gamedata/quests.json") or {objectives = {}, tasks = {}, events = {}}
  local data = {objectives = doc.objectives or {}, tasks = doc.tasks or {}, events = doc.events or {}, byCrc = {}, eventsByCrc = {},
                pois = doc.pois or {}, npcNames = doc.npcNames or {}, npa = doc.npa or {}, schedules = doc.schedules or {},
                schedulesByCrc = {}}
  for key, schedule in pairs(data.schedules) do
    data.schedulesByCrc[crc(key)] = schedule
  end
  for _, row in pairs(data.objectives) do
    data.byCrc[row.crc] = row
  end
  for key, row in pairs(data.events) do
    data.eventsByCrc[crc(key)] = row
  end
  Q.data = data
  return data
end
Q.D = D

local function cell(row, name)
  local value = row and row.cells and row.cells[name]
  return value ~= nil and value ~= "" and value or nil
end

-- ------------------------------------------------------------------------------------------------------------- the ids

-- The ids are userdata, as the native ids (objectivesdatahandler.luac GetObjectiveDifficulty tests type(id) == "userdata"); each id keeps
-- its fields in a side table. One metatable for each kind: == of two ids of a kind compares their keys.
local idFields = setmetatable({}, {__mode = "k"})

local function id_class(kind)
  local proto = newproxy(true)
  local meta = getmetatable(proto)
  local methods = {
    ToString = function(self) return idFields[self].key end,
    IsValid = function(self) return idFields[self].key ~= "0" end,
  }
  meta.__index = function(self, name)
    local fields = idFields[self]
    if fields[name] ~= nil then
      return fields[name]
    end
    return methods[name]
  end
  meta.__tostring = function(self) return kind .. ":" .. idFields[self].key end
  meta.__eq = function(a, b) return idFields[a].key == idFields[b].key end
  return function(fields)
    local id = newproxy(proto)
    idFields[id] = fields
    return id
  end
end
local newObjectiveId = id_class("objective")
local newTaskId = id_class("task")

local function objective_id(key)
  local id = Q.ids[key]
  if not id then
    id = newObjectiveId({key = key, value = tonumber(key) or 0})
    Q.ids[key] = id
  end
  return id
end

local function task_id(instance, index)
  local key = instance .. ":" .. index
  local id = Q.taskIds[key]
  if not id then
    -- objectiveInstanceId and taskIndex: the fields of the native task id that the map layer and the compass read
    -- (objectiveslayer.luac and compass.luac OnObjectiveTaskPositionAdded).
    id = newTaskId({key = key, instance = instance, index = index, value = tonumber(instance) or 0,
                    objectiveInstanceId = objective_id(instance), taskIndex = index})
    Q.taskIds[key] = id
  end
  return id
end

-- The native ObjectiveInstanceId of the scripts (a constructor with no value: an invalid id).
rawset(_G, "ObjectiveInstanceId", function() return objective_id("0") end)

local function record_of(id)
  if type(id) == "userdata" and idFields[id] then
    local key = id.instance or id.key
    -- During the removal events the removed record stays readable (bannertriggers.luac OnObjectiveFailed reads the type, the data, and
    -- the creation params of the failed objective; the retail banner "Objective Failed" shows: live L1 of backend 1.8a).
    return Q.byKey[key] or (Q.removing and Q.removing[key])
  end
  return nil
end

--- @return The tree node of a task id: {index, task row, children indexes}, and its record.
local function node_of(taskId)
  local rec = type(taskId) == "userdata" and idFields[taskId] and Q.byKey[taskId.instance] or nil
  local node = rec and rec.nodes[taskId.index]
  return node, rec
end

-- ------------------------------------------------------------------------------------------------------------- the tree rules

local CONTAINERS = {simpletaskcontainer = "simple", consecutivetaskcontainer = "consecutive", partialtaskcontainer = "partial",
                    taskcontainer = "simple"}

local function kind_of(node)
  return node and node.row and CONTAINERS[node.row.type] or nil
end

local function target_of(node)
  local kind = kind_of(node)
  if kind == "partial" then
    return 1
  elseif kind then
    return #node.children
  end
  return tonumber(cell(node and node.row, "TargetQty") or "1") or 1
end

local function state_of(rec, index)
  local s = rec and rec.states[index]
  return s and s.state or (eObjectiveTaskState_New or 0), s and s.progress or 0
end

-- ------------------------------------------------------------------------------------------------------------- the data objects

local ObjectiveData = Swc.ValueClass({name = "ObjectiveData", fields = {"id", "objectiveId", "type", "title", "description",
  "npcDestinationId", "successGameEventId", "difficultyLevel", "canBeAbandoned", "canBeShared", "requiredLevel", "achievementId",
  "originTerritoryId", "exclusiveTerritory", "difficulty", "repeatable", "hideAvailableMapIcon", "isValid"}, methods = {IsValid = function(self) return self.isValid == true end}})

local function objective_data(row)
  if not row then
    return nil
  end
  local typeName = cell(row, "Type") or "Quest"
  return ObjectiveData.new({
    id = row.crc, objectiveId = row.crc, type = _G["eObjectiveType_" .. typeName] or (eObjectiveType_Quest or 1),
    title = cell(row, "Title") or "", description = cell(row, "Description") or "",
    npcDestinationId = cell(row, "NpcDestinationId") and crc(cell(row, "NpcDestinationId")) or GetNilCrc(),
    successGameEventId = cell(row, "SuccessGameEventId") and crc(cell(row, "SuccessGameEventId")) or GetNilCrc(),
    difficultyLevel = tonumber(cell(row, "DifficultyLevel") or "0") or 0,
    canBeAbandoned = cell(row, "CanBeAbandoned") == "1", canBeShared = cell(row, "CanBeShared") == "1",
    requiredLevel = tonumber(cell(row, "RequiredLevel") or "0") or 0,
    achievementId = cell(row, "AchievementId") and crc(cell(row, "AchievementId")) or GetNilCrc(),
    exclusiveTerritory = tonumber(cell(row, "ExclusiveTerritory") or "0") or 0,
    -- The group difficulty of the row (the column Difficulty; the journal shows "@objective_difficulty_N" above 0).
    difficulty = tonumber(cell(row, "Difficulty") or "0") or 0,
    -- Repeatable: 1, true, TRUE (backend quest_data: the census of the data); ScheduleId: the CRC-32 of the lower-case id (the journal
    -- and the interact card ask ScheduleDataManagerBus with it).
    repeatable = (cell(row, "Repeatable") or ""):lower() == "1" or (cell(row, "Repeatable") or ""):lower() == "true",
    scheduleId = cell(row, "ScheduleId") and crc(cell(row, "ScheduleId")) or GetNilCrc(),
    -- The filter of the available map icons (objectivesdatahandler.luac GetAvailableObjectivesByTerritoryId; AST-239).
    hideAvailableMapIcon = cell(row, "HideAvailableMapIcon") == "1",
    isValid = true,
  })
end

-- ------------------------------------------------------------------------------------------------------------- the model

-- The notices of the Objectives component go to each address that the scripts use: the objective entity (objectiveshud.luac,
-- objectivestab.luac), the GDE root (bannertriggers.luac; the retail Objectives component is on the root entity of the player), the player.
local function notify_component(event, ...)
  for _, id in ipairs({Swc.ids.objectives, Swc.ids.root, Swc.ids.player}) do
    Swc.Notify("ObjectivesComponentNotificationsBus", id, event, ...)
  end
end

local function tracked_ids()
  local out = {}
  if Q.ready then
    for _, key in ipairs(Q.tracked) do
      if Q.byKey[key] then
        out[#out + 1] = objective_id(key)
      end
    end
  end
  return out
end

-- The helpers of swc/questpins.lua (the quest pins of stage Q2) and its hook: Q.Changed after each change of the state, the pinned
-- list, or the pin readiness.
Q.task_id, Q.node_of, Q.state_of, Q.target_of, Q.cell, Q.objective_id, Q.notify_component, Q.tracked_ids =
  task_id, node_of, state_of, target_of, cell, objective_id, notify_component, tracked_ids
-- The listeners of a change (swc/questpins.lua, swc/conversation.lua: AST-237, AST-239).
Q.listeners = {}
Q.Changed = function()
  for _, listener in ipairs(Q.listeners) do
    listener()
  end
end

local function send_pins()
  local keys = {}
  for _, key in ipairs(Q.tracked) do
    -- An encounter root is tracked by its link (field 5), not by a pin of the character.
    if not (Q.byKey[key] and Q.byKey[key].encounter) then
      keys[#keys + 1] = key
    end
  end
  Swc.hostRequests[#Swc.hostRequests + 1] = "quest pins " .. table.concat(keys, ",")
end

-- ------------------------------------------------------------------------------------------------------------- the timer row

-- The timer row of an objective (stage Q8, AST-243; docs/QUESTS.md "The timer row", a client decision, not the retail result).
-- pinnedobjective.luac and journal/objective.luac show a countdown row when Hud.LocalPlayer.MissionTimers.<instance>.TargetTime exists,
-- and read it once (SetObjectiveId). The retail client publishes a copy of a zero deadline at the bind of each TaskTimer, before its
-- activation (0x3B2FD90), and no known server field changes it: the retail row shows a red 00:00:00, also for a hidden timer [R: backend
-- 1.8e timer study; live I3 of backend 1.8d]. This client publishes the real deadline of the server instead: the start of the task
-- (field 0 of the Objectives state, the synced wall clock) plus its Duration, only for an Active TaskTimer that is not hidden. The node
-- goes when that task is no longer Active. Only the server ends the timer and the quest.
Q.timerNodes = {}

local function timer_path(rec)
  return string.format("Hud.LocalPlayer.MissionTimers.%08x.TargetTime", rec.id.value)
end

--- @return The deadline (seconds of the wall clock) of the running TaskTimer of an objective that is not hidden, or nil.
local function timer_deadline(rec)
  for index, node in pairs(rec.nodes) do
    local row, state = node.row, rec.states[index]
    -- A failure timer (FailOnCondition) runs as CompleteActive (3) on the server (backend quests.md TaskTimer); a normal timer as Active.
    local running = state and (state.state == (eObjectiveTaskState_Active or 1)
      or (state.state == (eObjectiveTaskState_CompleteActive or 3) and cell(row, "FailOnCondition") == "1"))
    if row and row.type == "tasktimer" and cell(row, "IsHidden") ~= "1" and running then
      local start, duration = tonumber(rec.starts[index] or ""), tonumber(cell(row, "Duration") or "")
      if start and start > 0 and duration then
        return start / 1e9 + duration
      end
    end
  end
  return nil
end
Q.timer_deadline = timer_deadline

-- A row that read the node before (the HUD pin, the journal entry) gets the new value: the cached end time, the row, and the tick.
local function refresh_timer_rows(key, value)
  for _, list in pairs(Swc.components) do
    for _, inst in ipairs(list) do
      local script = inst.__swcScript or ""
      local hud = script:find("objectives/hud/pinnedobjective.luac", 1, true) ~= nil
      local journal = script:find("objectives/journal/objective.luac", 1, true) ~= nil
      if (hud or journal) and inst.objectiveId and inst.objectiveId.key == key and inst.Properties and inst.Properties.TimeRemaining then
        inst.objectiveEndTime = value
        inst.timeRemainingSeconds = 0
        UiElementBus.Event.SetIsEnabled(inst.Properties.TimeRemaining, value ~= nil)
        if inst.tickBusHandler and value == nil then
          inst:BusDisconnect(inst.tickBusHandler)
          inst.tickBusHandler = nil
        elseif value ~= nil and not inst.tickBusHandler then
          inst.tickBusHandler = inst:BusConnect(DynamicBus.UITickBus)
        end
        if hud and type(inst.UpdateTaskPositions) == "function" then
          inst:UpdateTaskPositions()
        end
      end
    end
  end
end

local function update_timers(list)
  local seen = {}
  for _, rec in ipairs(list) do
    local deadline = timer_deadline(rec)
    if deadline then
      local path = timer_path(rec)
      seen[path] = true
      if Q.timerNodes[path] ~= deadline then
        Q.timerNodes[path] = deadline
        local value = Swc.WallClockTimePoint.new({seconds = deadline})
        Swc.SetData(path, value)
        Swc.Note(string.format("quests: the timer row of %s ends at %.3f (%.1f s from now)", rec.row and rec.row.id or rec.key, deadline,
          deadline - WallClockTimePoint:Now():GetTimeSinceEpoc():ToSeconds()))
        refresh_timer_rows(rec.key, value)
      end
    end
  end
  for path in pairs(Q.timerNodes) do
    if not seen[path] then
      Q.timerNodes[path] = nil
      Swc.SetData(path, nil)
      local key = nil
      for _, rec in ipairs(list) do
        if timer_path(rec) == path then key = rec.key end
      end
      for k, rec in pairs(Q.byKeyBeforeTimers or {}) do
        if timer_path(rec) == path then key = k end
      end
      if key then refresh_timer_rows(key, nil) end
    end
  end
end

--- The quest state of the owner (UI/SwcUiQuests): "A instance objective territory parent", "T instance index task progress state",
--- "K instance" (tracked), "S instance index startNs", "C instance" (completed notice).
--- The record of an encounter root (QG5; protocol notes 4.62): "E INSTANCE ASSET<TAB>TITLE<TAB>DESC|TARGET|TIMELIMIT|HIDE<TAB>...". The
--- generated tree of the native client: task m has the leaf 2m+1 (the text, the target, the TimeLimit, m_countDownProgress) and its
--- consecutive wrapper 2m+2; the outer consecutive container 2T+1 is the root. The type POI: the tracker shows it in its location slot
--- at the top (the teal diamond of retail). Its rows are not rows of quests.json: no pin request and no data of the quest table.
local function encounter_record(rest)
  -- The fields with the empty ones (an encounter with no title has an empty second field).
  local fields, from = {}, 1
  while true do
    local at = string.find(rest, "\t", from, true)
    fields[#fields + 1] = string.sub(rest, from, at and at - 1 or #rest)
    if not at then
      break
    end
    from = at + 1
  end
  local head = fields[1] or ""
  local instance, asset = head:match("^(%d+) (%x+)$")
  if not instance then
    return nil
  end
  local title = fields[2] or ""
  local row = {id = "encounter:" .. asset, cells = {Title = title ~= "" and title or nil, Type = "POI"}}
  local rec = {key = instance, id = objective_id(instance), crc = 0, row = row, territory = 0, parent = "0", states = {}, nodes = {},
               starts = {}, encounter = asset}
  local wrappers = {}
  for m = 0, #fields - 3 do
    local desc, target, limit, hide = (fields[m + 3] or ""):match("^([^|]*)|(%d+)|([%d.]*)|(%d)$")
    if not desc then
      break
    end
    local leaf, wrapper = 2 * m + 1, 2 * m + 2
    rec.nodes[leaf] = {index = leaf, children = {}, row = {type = "task", cells = {TP_DescriptionTag = desc ~= "" and desc or nil,
      TargetQty = target, TimeLimit = (tonumber(limit) or 0) > 0 and limit or nil, HideProgress = hide == "1" and "1" or nil}}}
    rec.nodes[wrapper] = {index = wrapper, children = {leaf}, row = {type = "consecutivetaskcontainer", cells = {}}}
    wrappers[#wrappers + 1] = wrapper
  end
  local root = 2 * #wrappers + 1
  rec.nodes[root] = {index = root, children = wrappers, row = {type = "consecutivetaskcontainer", cells = {}}}
  rec.root = root
  return rec
end

function Swc.HostQuests(text)
  local data = D()
  local list, byKey, tracked, completed = {}, {}, {}, {}
  for line in string.gmatch(text or "", "[^\n]+") do
    local kind, rest = line:match("^(%a) (.*)$")
    if kind == "A" then
      local instance, objective, territory, parent = rest:match("^(%d+) (%d+) (%d+) (%d+)$")
      local row = data.byCrc[tonumber(objective)]
      local old = Q.byKey[instance]
      local rec = {key = instance, id = objective_id(instance), crc = tonumber(objective), row = row, territory = tonumber(territory) or 0,
                   parent = parent, states = {}, nodes = {}, starts = {}}
      for _, entry in ipairs(row and row.tree or {}) do
        rec.nodes[entry[1]] = {index = entry[1], row = data.tasks[entry[2]], children = entry[3] or {}}
        rec.root = entry[1]
      end
      rec.previous = old and old.states or {}
      list[#list + 1] = rec
      byKey[instance] = rec
    elseif kind == "E" then
      local rec = encounter_record(rest)
      if rec and not byKey[rec.key] then
        local old = Q.byKey[rec.key]
        rec.previous = old and old.states or {}
        list[#list + 1] = rec
        byKey[rec.key] = rec
      end
    elseif kind == "T" then
      local instance, index, task, progress, state = rest:match("^(%d+) (%d+) (%d+) (%d+) (%d+)$")
      local rec = byKey[instance]
      if rec then
        rec.states[tonumber(index)] = {task = tonumber(task), progress = tonumber(progress), state = tonumber(state)}
      end
    elseif kind == "K" then
      tracked[#tracked + 1] = rest
    elseif kind == "S" then
      local instance, index, start = rest:match("^(%d+) (%d+) (%d+)$")
      if byKey[instance] then
        byKey[instance].starts[tonumber(index)] = start
      end
    elseif kind == "C" then
      completed[rest] = true
    end
  end
  local before = Q.byKey
  local beforeTracked = {}
  for _, key in ipairs(Q.tracked) do
    beforeTracked[key] = true
  end
  Q.byKeyBeforeTimers = before
  Q.list, Q.byKey, Q.tracked = list, byKey, tracked
  -- The timer rows first: a new pin reads its node in SetObjectiveId (OnObjectiveAdded below).
  update_timers(list)
  Q.byKeyBeforeTimers = nil
  Q.revision = Q.revision + 1
  Swc.SetData("ObjectivesComponentRequestBus.IsConnected", true)
  -- The events: the removed instances, the added instances, the task changes, the tracked changes, the order.
  Q.removing = before
  for key, old in pairs(before) do
    if not byKey[key] then
      notify_component("OnObjectiveRemoved", old.id)
      if old.encounter then
        -- [I] The end of an encounter (the stop of the root, the unlink): no banner of a completed or failed quest.
      elseif completed[key] then
        -- bannertriggers.luac OnObjectiveCompleted(instanceId, objectiveCRC, creationParams) (research/quest-conversation.md 6.3).
        notify_component("OnObjectiveCompleted", old.id, old.crc, {missionId = 0, objectiveId = old.crc, originTerritoryId = old.territory or 0,
          destinationOverride = EntityId(), parentObjectiveId = objective_id(old.parent or "0")})
      else
        notify_component("OnObjectiveFailed", old.id)
      end
    end
  end
  Q.removing = nil
  for _, rec in ipairs(list) do
    if not before[rec.key] then
      Swc.Note("quests: the objective " .. (rec.row and rec.row.id or tostring(rec.crc)) .. " (" .. rec.key .. ")")
      notify_component("OnObjectiveAdded", rec.id)
    else
      local changed = false
      for index, s in pairs(rec.states) do
        local p = rec.previous[index]
        if not p or p.state ~= s.state or p.progress ~= s.progress then
          changed = true
          local tid = task_id(rec.key, index)
          -- No OnTaskUpdated on this bus: no script handles it, and pinnedobjective.luac has a method of that name for its task rows
          -- (OnTaskUpdated(taskTable)); a task id there stopped the tracker (GetPositionY of a nil method; AST-237).
          Swc.Notify("ObjectiveTaskNotificationBus", tid, "OnTaskChanged", tid)
          if s.state == (eObjectiveTaskState_Active or 1) and (not p or p.state ~= s.state) then
            Swc.Notify("ObjectiveTaskNotificationBus", tid, "OnTaskActivated", tid)
          elseif (s.state == (eObjectiveTaskState_Complete or 2) or s.state == (eObjectiveTaskState_CompleteActive or 3))
              and (not p or (p.state ~= 2 and p.state ~= 3)) then
            Swc.Notify("ObjectiveTaskNotificationBus", tid, "OnTaskCompleted", tid)
            notify_component("OnObjectiveTaskCompleted", rec.id, tid)
          end
        end
      end
      if changed then
        Swc.Notify("ObjectiveNotificationBus", rec.id, "OnObjectiveChanged", rec.id)
      end
    end
    rec.previous = nil
  end
  if Q.ready then
    for _, key in ipairs(tracked) do
      if not beforeTracked[key] and byKey[key] then
        notify_component("OnTrackedObjectiveAdded", objective_id(key))
      end
    end
    for key in pairs(beforeTracked) do
      local still = false
      for _, k in ipairs(tracked) do
        still = still or k == key
      end
      if not still then
        notify_component("OnTrackedObjectiveRemoved", objective_id(key))
      end
    end
  end
  notify_component("OnObjectiveSortingChanged")
  Q.Changed()
end

--- The pin readiness (slot 73): the tracked objectives show from now on.
-- The place of a POI tag (gamedata/quests.json "pois": the points of interest of the backend copy).
local poiByTag
function Q.PoiOfTag(tag)
  if poiByTag == nil then
    poiByTag = {}
    for _, poi in ipairs(D().pois or {}) do
      for _, each in ipairs(poi.tags or {}) do
        poiByTag[string.lower(each)] = poi
      end
    end
  end
  return poiByTag[string.lower(tag or "")]
end

--- The values of the task texts (AST-241): the native GetDescription fills them [inference from the texts and the retail picture
--- K1-2of5 of the backend stage 1.8c: "Defeat {targetName} Mariners" is "Defeat Corrupted Mariners"]. {targetName}: the vitals
--- category of KillEnemyType ("@vc_corrupted"), else the item; {itemName}: the item of ItemName; {POITags}: the name of the point of
--- interest of the first POITag; {destinationName}: the NPC name of ItemName (a conversation task); {targetAmount}: TargetQty.
function Q.FillTaskText(text, row)
  if not text:find("{", 1, true) or not row then
    return text
  end
  local function localize(key)
    return key and key ~= "" and LyShineScriptBindRequestBus.Broadcast.LocalizeText(key) or ""
  end
  local item = cell(row, "ItemName")
  local static = item and Swc.ItemStatic(item)
  local itemName = static and localize(static.name) or ""
  local enemy = cell(row, "KillEnemyType")
  local tag = (cell(row, "POITag") or ""):match("^%s*([^,%s]+)")
  local poi = tag and Q.PoiOfTag(tag)
  local npc = item and (D().npcNames or {})[string.lower(item)]
  local values = {
    targetName = enemy and localize("@vc_" .. string.lower(enemy)) or itemName,
    itemName = itemName,
    POITags = poi and localize(poi.name) or "",
    destinationName = npc and localize(npc) or "",
    targetAmount = cell(row, "TargetQty") or "",
  }
  values.POItags = values.POITags
  return (text:gsub("{(%w+)}", function(name) return values[name] end))
end

--- The rule RequiredActiveObjectiveId and RequiredActiveTaskId of a conversation state (AST-248; backend docs/systems/persons.md, client
--- IsTaskActiveById 0x3A49520): one objective of the list (commas) has the task Active (1) or CompleteActive (3).
--- @return The objective id (userdata) of the first match, or nil.
function Q.ActiveTask(objectives, task)
  local wanted = {}
  for name in string.gmatch(objectives or "", "[^,]+") do
    wanted[string.lower((name:gsub("^%s+", ""):gsub("%s+$", "")))] = true
  end
  task = string.lower(task or "")
  for _, rec in ipairs(Q.list) do
    if rec.row and wanted[string.lower(rec.row.id or "")] then
      for index, node in pairs(rec.nodes) do
        local state = rec.states[index] and rec.states[index].state
        if node.row and string.lower(node.row.id or "") == task and (state == 1 or state == 3) then
          return rec.id
        end
      end
    end
  end
  return nil
end

--- The InteractTags of the Active (1) tasks of the character, lower case, with commas (AST-241; backend gathering.md stage 1.8c: a quest
--- object with InteractOnlyOnQuest needs an Active task with its tag). UI/SwcUiQuests gives them to the focus of the objects.
function Swc.QuestActiveTags()
  local tags, seen = {}, {}
  for _, rec in ipairs(Q.list) do
    for index, node in pairs(rec.nodes) do
      local tag = node.row and cell(node.row, "InteractTag")
      if tag and tag ~= "" and rec.states[index] and rec.states[index].state == 1 and not seen[string.lower(tag)] then
        seen[string.lower(tag)] = true
        tags[#tags + 1] = string.lower(tag)
      end
    end
  end
  return table.concat(tags, ",")
end

--- The next UI state that opened (2617; swc/manager.lua), or 0.
function Swc.TakeQuestScreen()
  return table.remove(Swc.questScreens or {}, 1) or 0
end

function Swc.HostQuestPinReady(on)
  local was = Q.ready
  Q.ready = on and true or false
  if Q.ready and not was then
    Swc.Note("quests: the pin readiness: " .. #Q.tracked .. " pinned objective(s)")
    for _, id in ipairs(tracked_ids()) do
      notify_component("OnTrackedObjectiveAdded", id)
    end
    notify_component("OnObjectiveSortingChanged")
    Q.Changed()
  end
end

-- ------------------------------------------------------------------------------------------------------------- the buses

Swc.Define("ObjectivesComponentRequestBus", "Event", {
  GetObjectives = function()
    local out = {}
    for _, rec in ipairs(Q.list) do
      out[#out + 1] = rec.id
    end
    return out
  end,
  GetTrackedObjectives = function() return tracked_ids() end,
  -- The count of the active objectives of a type and its largest count (dynamictooltip_itemstats.luac: the artifact quest of an
  -- artifact item; AST-259: with no definition the compare of the two nil values stopped the tooltip of each artifact).
  GetObjectivesCountForType = function(_, objectiveType)
    local count = 0
    for _, rec in ipairs(Q.list) do
      if (objective_data(rec.row) or {}).type == objectiveType then
        count = count + 1
      end
    end
    return count
  end,
  GetMaxObjectivesForType = function(_, objectiveType)
    local names = {[eObjectiveType_Artifact or 28] = "javelin.objectives-max-artifact-quests",
                   [eObjectiveType_MainStoryQuest or 13] = "javelin.objectives-max-MSQ",
                   [eObjectiveType_EpicEquipment or 19] = "javelin.objectives-max-epicequipment"}
    return tonumber(Swc.config[names[objectiveType] or ""]) or 0
  end,
  IsObjectiveTracked = function(_, id)
    if not Q.ready or id == nil then
      return false
    end
    for _, key in ipairs(Q.tracked) do
      if key == id.key then
        return true
      end
    end
    return false
  end,
  SetObjectiveTracked = function(_, id, on)
    if id == nil or not Q.byKey[id.key] then
      return
    end
    local at
    for i, key in ipairs(Q.tracked) do
      if key == id.key then
        at = i
      end
    end
    if on and not at then
      if #Q.tracked >= 8 then
        return
      end
      Q.tracked[#Q.tracked + 1] = id.key
      send_pins()
      notify_component("OnTrackedObjectiveAdded", id)
    elseif not on and at then
      table.remove(Q.tracked, at)
      send_pins()
      notify_component("OnTrackedObjectiveRemoved", id)
    else
      return
    end
    Swc.Note("quests: " .. (on and "pin " or "unpin ") .. id.key)
    notify_component("OnPinnedObjectiveChanged", id)
    notify_component("OnObjectiveSortingChanged")
    Q.Changed()
  end,
  -- Ready for the hand-in: the root task of the instance is Complete (2) or CompleteActive (3) (backend quests.md: "the root of the
  -- objective is complete: the hand-in is stage 1.8b"; the retail tracker then shows the hand-in row "Talk to Urda", picture K1-2of5).
  IsObjectiveReadyForCompletion = function(_, id)
    local rec = record_of(id)
    local state = rec and rec.root and state_of(rec, rec.root)
    return state == 2 or state == 3
  end,
  GetNearbyObjectives = function() return {} end,
  IsObjectiveNearby = function() return false end,
  -- The place of a pinned objective in the pinned list from 0, -1 for an objective with no pin: the number of its icon is the place
  -- + 1 (objectivetypedata.luac GetObjectiveIconByObjectiveInstanceId: numberedIconPath[pos + 1]; the retail tracker shows "1" for the
  -- one pin of a new character, Soulwarden evidence quests/18a/implement/live/I2-new-character-hud.png).
  GetObjectiveSortingPos = function(_, id)
    for i, tracked in ipairs(tracked_ids()) do
      if id and tracked.key == id.key then
        return i - 1
      end
    end
    return -1
  end,
  GetFirstTrackedObjectiveType = function()
    local first = tracked_ids()[1]
    local rec = first and Q.byKey[first.key]
    return rec and objective_data(rec.row).type or (eObjectiveType_Objective or 0)
  end,
  -- The start of a task (field 0 of the Objectives state: the synced wall clock, Unix ns; stage Q8), else the time now.
  GetTaskStartTime = function(_, taskId)
    local rec = taskId and Q.byKey[taskId.instance]
    local start = rec and tonumber(rec.starts[taskId.index] or "")
    if start and start > 0 then
      return Swc.WallClockTimePoint.new({seconds = start / 1e9})
    end
    return WallClockTimePoint:Now()
  end,
  GetActiveTimedQuickCourseId = function() return 0 end,
  GetCurrentMissionId = function() return 0 end,
  GetCurrentMissionObjectiveId = function() return objective_id("0") end,
  -- GetAvailableQuestsByTerritoryId, GetNPCMarkers: swc/npcstates.lua (AST-239).
  ShouldObjectiveGrantCategoricalProgression = function() return true end,
  GetTerritoryIdForNpc = function() return 0 end,
  RequestShareObjectiveWithGroup = function() end,
  -- Abandon and reset (stage Q6, AST-241): the request of the instance; the backend answers with the new state.
  AbandonObjective = function(_, id)
    if id and Q.byKey[id.key] then
      Swc.hostRequests[#Swc.hostRequests + 1] = "quest abandon " .. id.key
    end
  end,
  ResetObjective = function(_, id)
    if id and Q.byKey[id.key] then
      Swc.hostRequests[#Swc.hostRequests + 1] = "quest reset " .. id.key
    end
  end,
})

Swc.Define("ObjectiveRequestBus", "Event", {
  GetType = function(id)
    local rec = record_of(id)
    return rec and objective_data(rec.row).type or (eObjectiveType_Objective or 0)
  end,
  GetTitle = function(id)
    local rec = record_of(id)
    return rec and cell(rec.row, "Title") or ""
  end,
  -- The localized description: the journal puts it after the subtitle (objectivesdatahandler.luac GetMissionTitleAndDescription), and a
  -- key in the middle of a text is not localized.
  -- The name of the hand-in person (pinnedobjectivetask.luac SetHandInTask: "@objective_hand_in_destination" with npcName): the NPC
  -- name of the NpcDestinationId of the objective (gamedata/quests.json "npcNames": the conversation data of the backend copy).
  GetNpcDestinationGenericName = function(id)
    local rec = record_of(id)
    local npc = rec and cell(rec.row, "NpcDestinationId")
    local key = npc and (D().npcNames or {})[string.lower(npc)]
    return key and LyShineScriptBindRequestBus.Broadcast.LocalizeText(key) or ""
  end,
  GetDescription = function(id)
    local rec = record_of(id)
    local key = rec and cell(rec.row, "Description")
    return key and LyShineScriptBindRequestBus.Broadcast.LocalizeText(key) or ""
  end,
  GetDifficultyLevel = function(id)
    local rec = record_of(id)
    return rec and tonumber(cell(rec.row, "DifficultyLevel") or "0") or 0
  end,
  GetTasks = function(id)
    local rec = record_of(id)
    return rec and rec.root and {task_id(rec.key, rec.root)} or {}
  end,
  GetObjectiveData = function(id)
    local rec = record_of(id)
    return rec and objective_data(rec.row) or nil
  end,
  GetCreationParams = function(id)
    local rec = record_of(id)
    return {missionId = 0, objectiveId = rec and rec.crc or 0, originTerritoryId = rec and rec.territory or 0,
            destinationOverride = EntityId(), parentObjectiveId = objective_id(rec and rec.parent or "0")}
  end,
  GetOwningEntityId = function() return Swc.ids.player end,
  GetRewardCount = function() return 0 end,
  HasAnyProgress = function() return false end,
  IsObjectiveDisabled = function() return false end,
  IsDynamicPoiObjectiveActive = function() return false end,
  CanBeAbandoned = function(id)
    local rec = record_of(id)
    return rec ~= nil and cell(rec.row, "CanBeAbandoned") == "1"
  end,
  -- The reset of the journal: the objectives that cannot be abandoned (the main story; live check L2 of stage 1.8c).
  CanBeReset = function(id)
    local rec = record_of(id)
    return rec ~= nil and cell(rec.row, "CanBeAbandoned") ~= "1"
  end,
})

Swc.Define("ObjectiveTaskRequestBus", "Event", {
  GetTasks = function(taskId)
    local node, rec = node_of(taskId)
    local out = {}
    for _, child in ipairs(node and node.children or {}) do
      out[#out + 1] = task_id(rec.key, child)
    end
    return out
  end,
  GetState = function(taskId)
    local _, rec = node_of(taskId)
    return (state_of(rec, taskId.index))
  end,
  GetProgress = function(taskId)
    local _, rec = node_of(taskId)
    local _, progress = state_of(rec, taskId.index)
    return progress
  end,
  GetTarget = function(taskId)
    return target_of((node_of(taskId)))
  end,
  GetProgressPercent = function(taskId)
    local node, rec = node_of(taskId)
    local _, progress = state_of(rec, taskId.index)
    local target = target_of(node)
    return target > 0 and math.min(progress / target, 1) or 0
  end,
  GetDescription = function(taskId)
    local node = node_of(taskId)
    local key = cell(node and node.row, "TP_DescriptionTag")
    if not key then
      return ""
    end
    return Q.FillTaskText(LyShineScriptBindRequestBus.Broadcast.LocalizeText(key), node.row)
  end,
  GetIsHidden = function(taskId)
    local node = node_of(taskId)
    return cell(node and node.row, "IsHidden") == "1"
  end,
  GetHideChildren = function(taskId)
    local node = node_of(taskId)
    return cell(node and node.row, "HideChildren") == "1"
  end,
  GetHideProgress = function(taskId)
    local node = node_of(taskId)
    return cell(node and node.row, "HideProgress") == "1" or target_of(node) <= 1
  end,
  ContainsConsecutiveTasks = function(taskId)
    return kind_of((node_of(taskId))) == "consecutive"
  end,
  -- The typed values of the native UI data map (research/quest-pins.md 3.1, 0x3AD7BE0): OverrideLocation a vector of Vector3 (the
  -- native parser; empty with no cell: compass.luac takes #value), StayActive a boolean, ID the task id; the other names the cell.
  GetUIData = function(taskId, name)
    local node = node_of(taskId)
    if name == "OverrideLocation" then
      return Swc.questPins and Swc.questPins.OverrideVector(node) or {}
    elseif name == "StayActive" then
      return cell(node and node.row, "StayActive") == "1"
    elseif name == "ID" then
      return node and taskId or nil
    end
    local value = cell(node and node.row, name)
    return value and (tonumber(value) or value) or nil
  end,
  GetObjectiveInstanceId = function(taskId)
    return taskId and objective_id(taskId.instance) or nil
  end,
  GetRefreshDescriptionInterval = function() return 0 end,
  -- GetTaskTerritoryId: swc/questpins.lua.
})

-- The rewards of the data (GameEvent rows and the reward columns of the objective row).
local GameEventData = Swc.ValueClass({name = "GameEventData", fields = {"isValid", "id", "progressionReward", "currencyRewardRange",
  "azothReward", "itemReward", "gearScoreRange", "factionReputation", "factionTokens", "affinityRewards", "territoryStanding",
  "factionInfluenceAmount", "contributionLevel", "pvpXp", "azothSalt", "categoricalProgressionReward", "categoricalProgressionId"},
  methods = {IsValid = function(self) return self.isValid == true end}})

local otherEvents = Swc.Bus("GameEventRequestBus").Broadcast.GetGameSystemData
Swc.Define("GameEventRequestBus", "Broadcast", {
  -- A quest event of quests.json; else the event of swc/crafting.lua (the trade skill events of the recipes).
  GetGameSystemData = function(eventId)
    local data = D()
    local row = type(eventId) == "number" and data.eventsByCrc[eventId] or data.events[string.lower(tostring(eventId or ""))]
    if not row then
      return otherEvents and otherEvents(eventId) or GameEventData.new({isValid = false})
    end
    return GameEventData.new({isValid = true, id = crc(row.id), progressionReward = tonumber(row.UniversalExpAmount or "0") or 0,
      currencyRewardRange = row.CurrencyReward or "0", azothReward = tonumber(row.AzothReward or "0") or 0, itemReward = row.ItemReward or "",
      gearScoreRange = row.GearScoreRange or "", factionReputation = tonumber(row.FactionReputation or "0") or 0,
      factionTokens = tonumber(row.FactionTokens or "0") or 0, affinityRewards = {}, territoryStanding = tonumber(row.TerritoryStanding or "0") or 0,
      factionInfluenceAmount = tonumber(row.FactionInfluenceAmount or "0") or 0, contributionLevel = 0, pvpXp = tonumber(row.PvpXp or "0") or 0,
      azothSalt = 0, categoricalProgressionReward = tonumber(row.CategoricalProgressionReward or "0") or 0,
      categoricalProgressionId = row.CategoricalProgressionId and crc(row.CategoricalProgressionId) or GetNilCrc()})
  end,
})

local MissionData = Swc.ValueClass({name = "MissionData", fields = {"id", "objectiveId", "successGameEventIdOverride", "vcLevel", "titleOverride", "descriptionOverride",
  "territoryIdOverride", "poiTagsOverride", "taskKillContributionOverride", "taskKillContributionQtyOverride", "taskHaveItemsOverride",
  "taskHaveItemsQtyOverride", "taskGiveItemOverride", "taskTimerOverride"}, methods = {}})
Q.noMission = MissionData.new({id = 0, objectiveId = 0, successGameEventIdOverride = 0, vcLevel = 0, titleOverride = "", descriptionOverride = "", territoryIdOverride = 0, poiTagsOverride = {},
  taskKillContributionOverride = "", taskKillContributionQtyOverride = 0, taskHaveItemsOverride = "", taskHaveItemsQtyOverride = 0,
  taskGiveItemOverride = "", taskTimerOverride = 0})

local ObjectiveRewards = Swc.ValueClass({name = "ObjectiveRewards", fields = {"experienceReward", "itemRewardId", "itemRewardIsGive",
  "itemRewardQuantity", "progressionRewardId", "progressionRankRewardId"}, methods = {}})

local otherObjectiveData = Swc.Bus("ObjectivesDataManagerBus").Broadcast.GetObjectiveData
Swc.Define("ObjectivesDataManagerBus", "Broadcast", {
  GetObjectiveData = function(objectiveCrc)
    local row = D().byCrc[objectiveCrc]
    return row and objective_data(row) or (otherObjectiveData and otherObjectiveData(objectiveCrc))
  end,
  GetObjectiveRewards = function(objectiveCrc)
    local row = D().byCrc[objectiveCrc]
    if not row then
      return nil
    end
    return ObjectiveRewards.new({experienceReward = tonumber(cell(row, "ExperienceReward") or "0") or 0, itemRewardId = cell(row, "ItemRewardName") or "",
      itemRewardIsGive = true, itemRewardQuantity = tonumber(cell(row, "ItemRewardQty") or "1") or 1, progressionRewardId = GetNilCrc(),
      progressionRankRewardId = 0})
  end,
  -- No mission (missionId 0): an empty record (objectivesdatahandler.luac GetMissionTitleAndDescription reads its overrides).
  GetMissionData = function() return Q.noMission end,
  -- The difficulty level of an offer (conversationscreenv2.luac SetupObjectiveDetailsVisuals compares it with the level of the player;
  -- AST-239): the DifficultyLevel of the row, 0 for an unknown objective.
  GetObjectiveDifficultyLevel = function(objectiveCrc)
    local row = D().byCrc[objectiveCrc]
    return row and tonumber(cell(row, "DifficultyLevel") or "0") or 0
  end,
})

-- The journal and the objectives of the options.
Swc.config["javelin.enable-journal"] = true
-- The Auto-Pin switches of the journal (objectivestab.luac: shown with javelin.enable-auto-pin-options; the state of each switch is the
-- option node Hud.LocalPlayer.Options.Misc.EnableAutoPinningObjectives.TYPE). The values are the flags of 2709 (UI/SwcUiQuests.cpp):
-- each type on, CommunityGoal (6) off, the retail default. A change of a switch does not go to the backend yet (an open point).
Swc.config["javelin.enable-auto-pin-options"] = true
Swc.SetData("javelin.enable-auto-pin-options", true)   -- objectivestab.luac reads it as a data node (dataLayer:OnChange)
for objectiveType = 0, 35 do
  Swc.SetData("Hud.LocalPlayer.Options.Misc.EnableAutoPinningObjectives." .. objectiveType, objectiveType ~= 6)
end
-- The journal canvas: the retail dynamic canvas loader loads it when the state 1823500652 starts (dynamiccanvasloader.luac canvasInfos:
-- JournalScreen, LyShineUI/Objectives/Journal/JournalScreen, autohide, FULLSCREEN_DRAW_ORDER of CanvasCommon); that loader does not
-- run in the client, so this file loads it in the same way.
Swc.stateCanvases[1823500652] = function()
  if #DynamicBus.JournalScreenProxyBus.handlers > 0 then
    return   -- the screen is loaded (its OnInit connects DynamicBus.JournalScreenProxyBus)
  end
  local common = RequireScript("LyShineUI._Common.CanvasCommon")
  Swc.LoadScreen("LyShineUI/Objectives/Journal/JournalScreen.uicanvas", true, common and common.FULLSCREEN_DRAW_ORDER or 0)
  Swc.Note("quests: the screen JournalScreen loads")
end
Swc.config["javelin.enable-objectives"] = true
-- The largest number of active objectives of each journal section (objectivestab.luac): the values of the retail journal (Soulwarden
-- evidence quests/18a/implement/live/I2-new-character-journal.png: Main Story 1/20, Faction Story 0/10, Favor 0/30, Season Story 0/20,
-- Skill Progression 0/20, Mount Quest 0/10, Epic Equipment 0/20). The other sections are not in that picture (open point).
for name, value in pairs({["javelin.objectives-max-MSQ"] = 20, ["javelin.objectives-max-factionstory"] = 10,
                          ["javelin.objectives-max-affinity"] = 30, ["javelin.objectives-max-season-quests"] = 20,
                          ["javelin.objectives-max-skillprogression"] = 20, ["javelin.objectives-max-mountrace"] = 10,
                          ["javelin.objectives-max-epicequipment"] = 20,
                          -- The default of the registration of the settings (immediate 10 at image RVA 0x616E59A; AST-259).
                          ["javelin.objectives-max-artifact-quests"] = 10}) do
  Swc.config[name] = value
end

-- The banners (bannertriggers.luac CanvasLoaded): the land claims of the territory (the observer of
-- Hud.LocalPlayer.IsLandClaimManagerAvailable takes the length of the list): this backend has no claims.
Swc.Define("LandClaimRequestBus", "Broadcast", {GetClaimKeys = function() return {} end})

-- The region of the server for the dates of the journal (timehelperfunctions.luac GetServerDateTable: a scheduled mission row of the
-- journal shows its next start): the backend has no region data; UTC with no daylight saving time [inference]. The next start of a
-- schedule: the backend has no schedules (now).
Swc.Define("PlayerDataManagerBus", "Broadcast", {
  GetRegionData = function() return {observesDst = false, utcOffset = 0, dstRuleId = 0} end,
})
-- The schedules of the repeats (stage Q8, AST-243; quests.json "schedules": javelindata_schedules). The cycles start at the shipped
-- defaults of the client (configuration 0x616C670, instructions 0x6181D4B to 0x6181F87; research quests-18d rules Q4): Hourly at the full
-- hour, Daily (DailyCooldown) at 05:00 UTC, Weekly (WeeklyCooldown) on Tuesday at 05:00 UTC; no region offset (the backend rule). A
-- window of dates (StartDate and EndDate with a date: a season) starts at its date. A schedule with no rule has no next start.
local DAY = 86400
local function days_from_civil(y, m, d)
  y = m <= 2 and y - 1 or y
  local era = math.floor(y / 400)
  local yoe = y - era * 400
  local doy = math.floor((153 * (m + (m > 2 and -3 or 9)) + 2) / 5) + d - 1
  local doe = yoe * 365 + math.floor(yoe / 4) - math.floor(yoe / 100) + doy
  return era * 146097 + doe - 719468
end
local function date_seconds(text)
  local y, m, d = tostring(text or ""):match("^(%d%d%d%d)%-(%d%d)%-(%d%d)T")
  return y and days_from_civil(tonumber(y), tonumber(m), tonumber(d)) * DAY or nil
end
local function schedule_of(id)
  local data = D()
  if type(id) == "number" then
    return data.schedulesByCrc[id]
  end
  return id and data.schedules[string.lower(tostring(id))] or nil
end
--- @return The next start of a schedule (Unix seconds) after now, or nil.
local function next_start(schedule, now)
  if not schedule then
    return nil
  end
  local start = schedule.startDate or ""
  if start == "*-*-*T*:00:00" then
    return math.floor(now / 3600) * 3600 + 3600
  elseif start == "*-*-*TDailyCooldown" then
    local at = math.floor(now / DAY) * DAY + 5 * 3600
    return at > now and at or at + DAY
  elseif schedule.startWeekday == "WeeklyCooldown" then
    local day = math.floor(now / DAY)
    local weekday = (day + 4) % 7   -- 1970-01-01 was a Thursday (4); Tuesday is 2
    local at = (day - weekday + 2) * DAY + 5 * 3600
    while at <= now do at = at + 7 * DAY end
    return at
  end
  local dated = date_seconds(start)
  return dated and dated > now and dated or nil
end
Q.next_schedule_start = function(id, now) return next_start(schedule_of(id), now or os.time()) end
Swc.Define("ScheduleDataManagerBus", "Broadcast", {
  GetNextScheduleStartTimeSeconds = function(id) return next_start(schedule_of(id), os.time()) or os.time() end,
  GetSecondsRemainingUntilNextScheduleStart = function(id)
    local at = next_start(schedule_of(id), os.time())
    return at and math.max(0, at - os.time()) or 0
  end,
  GetScheduleDescription = function(id)
    local schedule = schedule_of(id)
    return schedule and schedule.description or ""
  end,
  -- A window of dates contains now; a cycle schedule is always in its schedule; an unknown key is not.
  IsKeyInScheduleNow = function(key)
    local schedule = schedule_of(key)
    if not schedule then
      return false
    end
    local first, last = date_seconds(schedule.startDate), date_seconds(schedule.endDate)
    if not first and not last then
      return schedule.startDate ~= nil or schedule.startWeekday ~= nil
    end
    local now = os.time()
    return (not first or first <= now) and (not last or now < last + DAY)
  end,
})

-- The documents of the journal (documentstab.luac OnInit counts the topics and the chapters; a nil list was a Lua error at each open of
-- the journal): the client has no journal document data (the lore pages of the journal are an open point): empty lists.
Swc.Define("JournalDataManagerBus", "Broadcast", {
  GetTopicEntries = function() return {} end,
  GetChapterEntries = function() return {} end,
  GetJournalEntries = function() return {} end,
})
