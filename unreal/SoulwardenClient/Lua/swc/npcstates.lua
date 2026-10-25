-- The NPC quest states (stage Q4, Linear AST-239; research/quest-conversation.md sections 7 and 8; docs/QUESTS.md "The persons, the
-- offers, and the hand-in"): the native NPC state query (0x37AC8C0) for each person that shows for the character, and its three users:
-- the overhead Conversation marker (swc/markers.lua, NPCState), the compass (OnNpcHasAvailableQuestChanged), and the map
-- (ObjectivesComponentRequestBus GetNPCMarkers, GetAvailableQuestsByTerritoryId).
--
-- The persons: Swc.HostPersons (UI/SwcUiSubsystem.cpp, at each change of the presence): the persons with a Talk and an NPC id that show
-- for the character, "KEY<TAB>NPCID<TAB>X<TAB>Y<TAB>Z" (engine m) each.
--
-- The query (0x37AC8C0) is an ordered decision tree [R]:
--   1. the first ready hand-in of the NPC (swc/conversation.lua ready_handin): TurnIn 4 when DestinationCompletionAvailableResponse has
--      text (else NoObjective 0); nothing else is tested;
--   2. else the LinearObjectiveId of the selected state through the classifier in marker mode (HideIncompleteTurninMarker hides an
--      incomplete result): Proposal -> HasObjective 1, InProgress -> InProgress 5, OnCooldown 8 -> OnCooldown 16 (stage Q8); the linear
--      setting stops at the first nonzero result; an available state is not replaced by a later in-progress one;
--   3. with no state yet: an active talk-to task for the NPC (a conversation task, type code 16 or 17, Active, ItemName this NPC [I: the
--      native request +0x78 is not traced further]) -> HasTalkToTask 13 with its objective instance;
--   4. else an active objective with NpcDestinationId this NPC and no HideIncompleteTurninMarker -> InProgress 5;
--   5. else the first service of the state with a state (the jump table 0x37ACEC4): Inn 7, Event_Store 8, Horse_Store 9, Wolf_Store 10,
--      Lion_Store 11, Expansion_Modal 12, Transmog 14, Barbershop 15, Open_Mission_Board 18. Faction_Board 3 and Open_Shop 17 have gates
--      (a faction, the shop data) that this client does not have: not used [I]. Community_Board 6 has no row in the data.
--   The objective type: the Type of the selected objective.
-- The compass event OnNpcHasAvailableQuestChanged(npcCRC, state, worldPosition, objectiveType) goes for a person at a change of its
-- state, for the persons within 200 m of the player [I: the native publisher runs for the NPC entities of the stream]; a person that goes
-- out of that range gets the clear form (NoObjective, type 0). RequestPublishNpcStates sends each state again. The states are computed
-- again each second and after each change of the quests and the achievements [I].

local Swc = Swc
local C = Swc.conversation

local N = {persons = {}, states = {}, published = {}, dirty = true, next = 0, revision = 0}
Swc.npcStates = N

local STATE = {none = eConversationState_NoObjective or 0, available = eConversationState_HasObjective or 1,
               turnIn = eConversationState_TurnIn or 4, inProgress = eConversationState_InProgress or 5,
               talk = eConversationState_HasTalkToTask or 13, cooldown = eConversationState_OnCooldown or 16}
N.STATE = STATE
local SERVICES = {Inn = 7, Event_Store = 8, Horse_Store = 9, Wolf_Store = 10, Lion_Store = 11, Expansion_Modal = 12, Transmog = 14,
                  Barbershop = 15, Open_Mission_Board = 18}
local RANGE = 200
local LINEAR = Swc.config["javelin.enable-linear-conversations"] ~= false

local function cell(row, name)
  local value = row and row.cells and row.cells[name]
  return value ~= nil and value ~= "" and value or nil
end

local function objective_type(row)
  local name = cell(row, "Type") or "Objective"
  return _G["eObjectiveType_" .. name] or (eObjectiveType_Objective or 0)
end

--- The persons of the presence (UI/SwcUiSubsystem.cpp).
function Swc.HostPersons(text)
  local out = {}
  for line in string.gmatch(text or "", "[^\n]+") do
    local key, npc, x, y, z = line:match("^([^\t]*)\t([^\t]*)\t([^\t]*)\t([^\t]*)\t([^\t]*)$")
    if key and npc ~= "" then
      out[#out + 1] = {key = key, npcId = npc, crc = swcui.crc32(string.lower(npc)), x = tonumber(x) or 0, y = tonumber(y) or 0,
                       z = tonumber(z) or 0}
    end
  end
  N.persons = out
  N.dirty = true
  Swc.Note("npc states: " .. #out .. " person(s) of the presence")
end

--- The NPC state query (0x37AC8C0) for the character. @return {State, ObjectiveCrc, ObjectiveType, ConversationObjectiveInstanceId}.
function N.Query(npcId)
  local result = {State = STATE.none, ObjectiveCrc = 0, ObjectiveType = 0, ConversationObjectiveInstanceId = nil}
  local npc = string.lower(npcId or "")
  local _, state = C.choose_state(npcId)
  local selected
  local ready = C.ready_handin(npcId)
  if ready then
    selected = ready.row
    if cell(ready.row, "DestinationCompletionAvailableResponse") then
      result.State = STATE.turnIn
    end
  else
    for _, id in ipairs(state and state.objectives or {}) do
      local row = Swc.quests.D().objectives[string.lower(id)]
      local kind = C.classify(row, npcId, true)
      if kind ~= C.RESPONSE.invalid then
        selected = row
        if kind == C.RESPONSE.proposal then
          result.State = STATE.available
        elseif kind == C.RESPONSE.inprogress and result.State ~= STATE.available then
          result.State = STATE.inProgress
        elseif kind == C.RESPONSE.cooldown then
          result.State = STATE.cooldown
        end
        if LINEAR then
          break
        end
      end
    end
    if result.State == STATE.none then
      -- The active talk-to task of the NPC [I], else an active objective with this destination (no HideIncompleteTurninMarker).
      for _, rec in ipairs(Swc.quests.list) do
        for index, node in pairs(rec.nodes) do
          local code = node.row and node.row.code
          if (code == 16 or code == 17) and string.lower(cell(node.row, "ItemName") or "") == npc and (Swc.quests.state_of(rec, index)) == 1 then
            result.State, result.ConversationObjectiveInstanceId, selected = STATE.talk, rec.id, rec.row
            break
          end
        end
        if result.State ~= STATE.none then
          break
        end
      end
    end
    if result.State == STATE.none then
      for _, rec in ipairs(Swc.quests.list) do
        local destination = cell(rec.row, "NpcDestinationId")
        if destination and string.lower(destination) == npc and cell(rec.row, "HideIncompleteTurninMarker") ~= "1" then
          result.State, selected = STATE.inProgress, rec.row
          break
        end
      end
    end
    if result.State == STATE.none then
      for _, service in ipairs(state and state.services or {}) do
        if SERVICES[service] then
          result.State = SERVICES[service]
          break
        end
      end
    end
  end
  if selected then
    result.ObjectiveCrc, result.ObjectiveType = selected.crc, objective_type(selected)
  end
  return result
end

local function changed(a, b)
  return not a or a.State ~= b.State or a.ObjectiveType ~= b.ObjectiveType or a.ObjectiveCrc ~= b.ObjectiveCrc
end

local function publish(person, result)
  Swc.Notify("NpcComponentNotificationsBus", nil, "OnNpcHasAvailableQuestChanged", person.crc, result.State,
    Swc.Vector3.new({x = person.x, y = person.y, z = person.z}), result.ObjectiveType)
end

--- The states of the persons near the player (each second, and after a change of the quests or the achievements); the compass events of
--- the changes; the overhead markers read N.near.
function N.Update()
  local own = Swc.GetData("Hud.LocalPlayer.Position")
  local px, py = own and own.x or 0, own and own.y or 0
  local near = {}
  local seen = {}
  for _, person in ipairs(N.persons) do
    local dx, dy = person.x - px, person.y - py
    if dx * dx + dy * dy <= RANGE * RANGE then
      local result = N.Query(person.npcId)
      person.state = result
      near[#near + 1] = person
      seen[person.key] = true
      local before = N.published[person.key]
      if changed(before, result) or (N.republish and result.State ~= STATE.none) then
        result.person = person
        N.published[person.key] = result
        publish(person, result)
        if result.State ~= STATE.none or (before and before.State ~= STATE.none) then
          Swc.Note(string.format("npc states: %s (%s): state %d, objective %08x, type %d", person.npcId, person.key, result.State,
            result.ObjectiveCrc, result.ObjectiveType))
        end
      end
    end
  end
  -- The persons out of the range, or out of the presence (a swap: the walkaway Aelstrom after the knife offer): the clear form.
  for keyName, before in pairs(N.published) do
    if not seen[keyName] then
      N.published[keyName] = nil
      if before.State ~= STATE.none and before.person then
        publish(before.person, {State = STATE.none, ObjectiveType = 0})
        Swc.Note(string.format("npc states: %s (%s) gone: the clear form", before.person.npcId, keyName))
      end
    end
  end
  N.republish = false
  N.near = near
  N.revision = N.revision + 1
end

function N.MarkChanged()
  N.dirty = true
end

Swc.Connect("TickBus", {OnTick = function()
  local now = Swc.time or 0
  if N.dirty or now >= N.next then
    N.dirty, N.next = false, now + 1
    if #N.persons > 0 and Swc.quests then
      N.Update()
    end
  end
end}, nil)
if Swc.quests and Swc.quests.listeners then
  Swc.quests.listeners[#Swc.quests.listeners + 1] = N.MarkChanged
end

Swc.Define("NpcComponentRequestBus", "Broadcast", {
  -- The map and the compass ask for each state again (objectiveslayer.luac UpdatePublishedLocations, compass.luac).
  -- The published list stays (a person that leaves later still gets its clear form): the compass asks at each added or removed
  -- objective (compass.luac), so a reset here lost the clear of the walkaway Aelstrom (live ast239-exit3).
  RequestPublishNpcStates = function()
    N.republish = true
    N.dirty = true
  end,
})

Swc.Define("ObjectivesComponentRequestBus", "Event", {
  -- The incomplete destination markers of the map (0x3A3CEB0): the active objectives with a destination NPC and no
  -- HideIncompleteTurninMarker, not ready, one entry for each NPC (its place of the presence).
  GetNPCMarkers = function()
    local out, done = {}, {}
    for _, rec in ipairs(Swc.quests.list) do
      local destination = cell(rec.row, "NpcDestinationId")
      local npc = destination and string.lower(destination)
      local state = rec.root and Swc.quests.state_of(rec, rec.root)
      if npc and not done[npc] and cell(rec.row, "HideIncompleteTurninMarker") ~= "1" and state ~= 2 and state ~= 3 then
        for _, person in ipairs(N.persons) do
          if string.lower(person.npcId) == npc then
            done[npc] = true
            out[#out + 1] = {npcId = person.crc, worldPosition = Swc.Vector3.new({x = person.x, y = person.y, z = person.z}),
                             display = eNPCUIDisplayType_InProgress or 1}
            break
          end
        end
      end
    end
    return out
  end,
  -- The available quests of a territory [I: the native list is not traced; the offers (HasObjective) of the persons of the presence in
  -- the territory]. The map helper filters HideAvailableMapIcon (objectivesdatahandler.luac).
  GetAvailableQuestsByTerritoryId = function(_, territory)
    -- One pass over the persons for each revision of the states (the map asks for each territory in turn).
    if N.availableRevision ~= N.revision or not N.available then
      N.availableRevision, N.available = N.revision, {}
      for _, person in ipairs(N.persons) do
        local result = N.Query(person.npcId)
        if result.State == STATE.available then
          person.territories = person.territories
            or MapComponentBus.Broadcast.GetContainingTerritories(Swc.Vector3.new({x = person.x, y = person.y, z = person.z})) or {}
          for _, id in ipairs(person.territories) do
            N.available[id] = N.available[id] or {}
            table.insert(N.available[id], {npcId = person.crc, objectiveId = result.ObjectiveCrc,
              worldPosition = Swc.Vector3.new({x = person.x, y = person.y, z = person.z}), display = 0})
          end
        end
      end
    end
    return N.available[territory] or {}
  end,
})
