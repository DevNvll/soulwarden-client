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
  local data = {objectives = doc.objectives or {}, tasks = doc.tasks or {}, events = doc.events or {}, byCrc = {}, eventsByCrc = {}}
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
    id = newTaskId({key = key, instance = instance, index = index, value = tonumber(instance) or 0})
    Q.taskIds[key] = id
  end
  return id
end

-- The native ObjectiveInstanceId of the scripts (a constructor with no value: an invalid id).
rawset(_G, "ObjectiveInstanceId", function() return objective_id("0") end)

local function record_of(id)
  if type(id) == "userdata" and idFields[id] then
    return Q.byKey[id.instance or id.key]
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
  "originTerritoryId", "exclusiveTerritory", "difficulty", "repeatable", "isValid"}, methods = {IsValid = function(self) return self.isValid == true end}})

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
    repeatable = cell(row, "Repeatable") == "1",
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

local function send_pins()
  local keys = {}
  for _, key in ipairs(Q.tracked) do
    keys[#keys + 1] = key
  end
  Swc.hostRequests[#Swc.hostRequests + 1] = "quest pins " .. table.concat(keys, ",")
end

--- The quest state of the owner (UI/SwcUiQuests): "A instance objective territory parent", "T instance index task progress state",
--- "K instance" (tracked), "S instance index startNs", "C instance" (completed notice).
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
  Q.list, Q.byKey, Q.tracked = list, byKey, tracked
  Q.revision = Q.revision + 1
  Swc.SetData("ObjectivesComponentRequestBus.IsConnected", true)
  -- The events: the removed instances, the added instances, the task changes, the tracked changes, the order.
  for key, old in pairs(before) do
    if not byKey[key] then
      notify_component("OnObjectiveRemoved", old.id)
      if completed[key] then
        notify_component("OnObjectiveCompleted", old.id)
      else
        notify_component("OnObjectiveFailed", old.id)
      end
    end
  end
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
          Swc.Notify("ObjectiveTaskNotificationBus", tid, "OnTaskUpdated", tid)
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
end

--- The pin readiness (slot 73): the tracked objectives show from now on.
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

function Swc.HostQuestPinReady(on)
  local was = Q.ready
  Q.ready = on and true or false
  if Q.ready and not was then
    Swc.Note("quests: the pin readiness: " .. #Q.tracked .. " pinned objective(s)")
    for _, id in ipairs(tracked_ids()) do
      notify_component("OnTrackedObjectiveAdded", id)
    end
    notify_component("OnObjectiveSortingChanged")
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
  end,
  IsObjectiveReadyForCompletion = function() return false end,
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
  -- The start of a task timer (field 0): the timers are stage Q8; until then the time now.
  GetTaskStartTime = function() return WallClockTimePoint:Now() end,
  GetActiveTimedQuickCourseId = function() return 0 end,
  GetCurrentMissionId = function() return 0 end,
  GetCurrentMissionObjectiveId = function() return objective_id("0") end,
  GetAvailableQuestsByTerritoryId = function() return {} end,
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
  GetNpcDestinationGenericName = function() return "" end,
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
    return LyShineScriptBindRequestBus.Broadcast.LocalizeText(key)
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
  GetUIData = function(taskId, name)
    local node = node_of(taskId)
    local value = cell(node and node.row, name)
    return value and (tonumber(value) or value) or nil
  end,
  GetObjectiveInstanceId = function(taskId)
    return taskId and objective_id(taskId.instance) or nil
  end,
  GetRefreshDescriptionInterval = function() return 0 end,
  GetTaskTerritoryId = function() return 0 end,
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
                          ["javelin.objectives-max-epicequipment"] = 20}) do
  Swc.config[name] = value
end

-- The banners (bannertriggers.luac CanvasLoaded): the land claims of the territory (the observer of
-- Hud.LocalPlayer.IsLandClaimManagerAvailable takes the length of the list): this backend has no claims.
Swc.Define("LandClaimRequestBus", "Broadcast", {GetClaimKeys = function() return {} end})
