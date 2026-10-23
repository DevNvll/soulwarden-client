-- The conversation screen (stage MVP-H, Linear AST-223; docs/PERSONS.md "The conversation screen"): the original canvas
-- ConversationScreenV2 with its script (conversationscreenv2.luac) and the native buses that it reads. The client owns the
-- Talk (the request, the camera, the lock, the close messages: Interact/SwcInteractor, Interact/SwcConversation); this file
-- gives the screen its state and sends the end of the player to the client.
--
-- The rules (the use in conversationscreenv2.luac and dynamiccanvasloader.luac; the retail references of set 5 of the Talk):
--   the open   Swc.HostConversation("open", KEY, SPEAKER, LINE, NAME, TITLE, LINES): one conversation state for each line of the
--              person (LINES: "KEY\tSPEAKER" each, the chain of dialogue-next; dialogue "@LINE", its speaker attribute) with one
--              option, Continue ("@ui_continue", ContinueDialogue): the screen shows the subtitle "SPEAKER: TEXT". The backend
--              has no conversation content and no service of a person (the shops and the transmog are later stages): no quest
--              option, no shop option.
--   the state  the copy of dynamiccanvasloader.luac OnConversationStateChange (title, dialogue, responseType, dialogueId, the
--              options with text, optionType, dataId) to DynamicBus.ConversationScreenV2 (the native loader does not run
--              in the client: the canvas is in the screen list).
--   the next   E (ui_interact): RequestConversationOption of the option: the next line, or at the last line a state with no
--              option, and the script closes the screen (CloseScreen, TryCloseInteraction).
--   the back   Escape (OnEscapeKeyPressed): CloseScreen, TryCloseInteraction.
--   the end    TryCloseInteraction: PlayerCutsceneComponentRequestBus HandleOnCloseConversation and UiInteractorComponentRequestsBus
--              RequestCancelCommittedInteraction: one host request "conversation end" (the client sends 6705, then 2473).
--   the close  Swc.HostConversation("close"): the end of the Talk by the client (a refusal 3743 result 2, the end by the
--              server 3746, a death, a loading screen, E of the client): a state with no option closes the screen at once.

local C = {open = false, ended = false, key = "", speaker = "", line = "", name = "", title = "", lines = {}, index = 1, log = {}, topics = {},
           finished = {}}
Swc.conversation = C

-- The conversation state of the client (Linear AST-248; backend docs/systems/persons.md "The conversation state of a character" and "The
-- topics of a selection", the static read of stage 2.5c: client 0x37AC710, 0x37AB440, 0x37AB7A0): gamedata/conversations.json
-- (tools/convert-persons.sh conversations: the conversation data of the backend copy). The states of the NPC row in reverse, the first
-- whose requirements pass; its topics whose requirements pass; after a topic, its passing ProvidedTopics.
local CD
local function cdata()
  if CD == nil then
    CD = swcui.load_json("gamedata/conversations.json") or false
  end
  return CD or nil
end

-- The conditional of RequiredAchievement: ids with "!", "&&", "||", and parentheses (the rule of TaskAchievement, backend quests.md).
-- An id is true when the character has its bit; an id that the data does not know is false; a text that does not parse is false.
local function achievements_pass(text)
  local tokens, i = {}, 1
  while i <= #text do
    local two, c = text:sub(i, i + 1), text:sub(i, i)
    if c:match("%s") then
      i = i + 1
    elseif two == "&&" or two == "||" then
      tokens[#tokens + 1] = two
      i = i + 2
    elseif c == "!" or c == "(" or c == ")" then
      tokens[#tokens + 1] = c
      i = i + 1
    else
      local id = text:match("^[^%s!&|()]+", i) or c
      tokens[#tokens + 1] = {id = id}
      i = i + #id
    end
  end
  local pos, bad = 1, false
  local expr
  local function unary()
    local t = tokens[pos]
    pos = pos + 1
    if t == "!" then
      return not unary()
    elseif t == "(" then
      local v = expr()
      if tokens[pos] ~= ")" then bad = true end
      pos = pos + 1
      return v
    elseif type(t) == "table" then
      return Swc.HasAchievement(Swc.AchievementIndex(t.id))
    end
    bad = true
    return false
  end
  local function conj()
    local v = unary()
    while tokens[pos] == "&&" do
      pos = pos + 1
      local w = unary()
      v = v and w
    end
    return v
  end
  expr = function()
    local v = conj()
    while tokens[pos] == "||" do
      pos = pos + 1
      local w = conj()
      v = v or w
    end
    return v
  end
  local v = expr()
  return v and not bad and pos > #tokens
end

-- The faction of the player: the backend has none (roadmap 4.5); conversationoptionv2.luac reads the node for each option text.
if Swc.GetData("Hud.LocalPlayer.Faction") == nil then
  Swc.SetData("Hud.LocalPlayer.Faction", eFactionType_None or 0)
end
local FACTIONS = {syndicate = 1, marauder = 2, covenant = 3, any = 4, faction1 = 1, faction2 = 2, faction3 = 3}

-- @return True when the requirements of a state (forTopic false) or a topic pass for the character. The character has no faction, no
-- faction cooldown, and no categorical progression (backend roadmap 4.5).
local function requirements_pass(requires, forTopic)
  for name, value in pairs(requires or {}) do
    local text = tostring(value)
    if name == "RequiredLevel" then
      local level = tonumber(text)
      if level == nil or (tonumber(Swc.GetData("Hud.LocalPlayer.Progression.Level")) or 1) < level then return false end
    elseif name == "RequiredAchievement" then
      if text ~= "" and not achievements_pass(text) then return false end
    elseif name == "RequiredFaction" then
      local faction = FACTIONS[string.lower(text)] or 0
      if forTopic and faction ~= 0 then return false end
      if not forTopic and faction ~= 0 and faction ~= 4 then return false end
    elseif name == "RequiredFactionCooldown" then
      if not forTopic and text ~= "" and text ~= "NotInCooldown" then return false end
    elseif name == "RequiredActiveObjectiveId" then
      local task = requires.RequiredActiveTaskId
      if not forTopic and text ~= "" and task and task ~= "" and not Swc.quests.ActiveTask(text, task) then return false end
    elseif name == "RequiredCompletedObjective" then
      if not forTopic then
        local row = Swc.quests.D().objectives[string.lower(text)]
        local achievement = row and row.cells and row.cells.AchievementId
        if not achievement or achievement == "" or not Swc.HasAchievement(Swc.AchievementIndex(achievement)) then return false end
      end
    elseif name == "RequiredProgression" then
      local id, rank, points = text:match("^(%S+)%s+(%-?%d+)%s+(%-?%d+)$")
      if id and ((tonumber(rank) or 0) > 0 or (tonumber(points) or 0) > 0) then return false end
    end
  end
  return true
end

-- The validity of a state record (backend stage 1.8f; client loader 0x379F2F0 to 0x379F377, selector 0x37AC846 to 0x37AC870; backend
-- persons.md "The conversation state of a character", world/persons.cpp record_valid): IsInitialState XOR "the row has a requirement".
-- A requirement is a loaded value that is not 0: RequiredLevel not 0, a named RequiredFaction (Any too), a cooldown value of
-- RequiredFactionCooldown, RequiredCompletedObjective, RequiredAchievement, RequiredProgression with a rank or points not 0,
-- RequiredActiveObjectiveId (an id) with RequiredActiveTaskId. A record that is not valid is never selected (06_01b_MarcelDupont).
-- Data written before AST-239 has no "initial": then each record counts as valid (the rule of AST-248).
local function record_valid(state)
  if state.initial == nil then
    return true
  end
  local r = state.requires or {}
  local function text(name) return r[name] ~= nil and tostring(r[name]) or "" end
  local progression = false
  local _, rank, points = text("RequiredProgression"):match("^%s*(%S+)%s+(%-?%d+)%s*(%-?%d*)")
  if rank then
    progression = tonumber(rank) ~= 0 or (tonumber(points) or 0) ~= 0
  end
  local cooldown = string.lower(text("RequiredFactionCooldown"))
  local requirement = ((tonumber(text("RequiredLevel")) or 0) ~= 0)
    or (FACTIONS[string.lower(text("RequiredFaction"))] or 0) ~= 0
    or cooldown == "incooldownmultipledays" or cooldown == "incooldownsingleday"
    or text("RequiredCompletedObjective") ~= "" or text("RequiredAchievement") ~= "" or progression
    or (text("RequiredActiveObjectiveId"):find("[^,%s]") ~= nil and text("RequiredActiveTaskId") ~= "")
  return (state.initial == true) ~= requirement
end

local function chain_of(key)
  local data, lines, seen = cdata(), {}, {}
  while key and key ~= "" and not seen[key] and #lines < 10 do
    seen[key] = true
    local entry = data and data.lines[key]
    lines[#lines + 1] = {line = key, speaker = entry and entry.speaker or C.speaker}
    key = entry and entry.next or nil
  end
  return lines
end

local function passing_topics(ids)
  local data, out = cdata(), {}
  for _, id in ipairs(ids or {}) do
    local topic = data.topics[id]
    if topic and requirements_pass(topic.requires, true) then
      out[#out + 1] = {id = id, crc = topic.crc, prompt = topic.prompt, response = topic.response, provided = topic.provided}
    end
  end
  return out
end

--- The selection of the client for a person (its NPC row): sets C.stateId, C.lines, C.topics, and C.taskObjective (the objective of a
--- state with an active task rule: the conversation state HasTalkToTask of the option icons). @return True with a state.
--- The state of an NPC row for the character (the reverse scan 0x37AC710 with the validity of a record), with no change of the open
--- conversation (the NPC quest states of swc/npcstates.lua use it too). @return The state id and its record, or nil.
local function choose_state(npcId)
  local data = cdata()
  local states = data and data.npcs[string.lower(npcId or "")]
  for i = #(states or {}), 1, -1 do
    local state = data.states[states[i]]
    if state and record_valid(state) and requirements_pass(state.requires, false) then
      return states[i], state
    end
  end
  return nil
end
C.choose_state = choose_state

local function select_state(npcId)
  local id, state = choose_state(npcId)
  if not id then
    return false
  end
  C.stateId = id
  C.lines, C.index = chain_of(state.dialogue), 1
  C.topics = passing_topics(state.topics)
  local r = state.requires or {}
  C.taskObjective = r.RequiredActiveObjectiveId and r.RequiredActiveTaskId and Swc.quests.ActiveTask(r.RequiredActiveObjectiveId, r.RequiredActiveTaskId) or nil
  return true
end
C.select_state = select_state
C.record_valid = record_valid

-- ------------------------------------------------------------------------------------------------- the objectives (AST-239, stage Q4)

-- The conversation result of the native component (research/quest-conversation.md sections 2 to 6: the builder 0x37ACF10, the refresh
-- 0x376C720, the objective details 0x3773540, the accept caller 0x376DFF0, the complete caller 0x376E2C0):
--   C.result = {title, dialogue (a text key), responseType, dialogueId, options}. The page chain of the dialogue (dialogue-next: C.lines,
--   C.index) adds ContinueDialogue 7 with the dialogue-prompt of the next page (else @ui_continue) while a next page exists (0x3779AE0);
--   the screen then shows only Continue (with the look of Accept or Complete in those responses; the real type stays 7).
--   The build: DefaultDialogue of the selected state, Default 1, no title, dialogueId 0; then
--     1. the first ready hand-in among the active objectives in their stored order (0x3A369E0, predicate 0x3A48E60: NpcDestinationId this
--        NPC, the root Complete or CompleteActive, no completion bit): its Title, its CRC, DestinationCompletionAvailableResponse with
--        CompletionAvailable 6 (empty: InProgressResponse with InProgress 7), and CompleteObjective 5 with
--        DestinationCompletionAvailablePrompt; the linear scan is skipped (HideIncompleteTurninMarker does not hide a ready hand-in);
--     2. else the LinearObjectiveId of the state in column order through the classifier (0x37AE910): Proposal 3 appends ObjectiveDetails
--        3 (PlayerPrompt; Title for @prompt_DEFAULT), InProgress 7 sets InProgressResponse; javelin.enable-linear-conversations (true, the
--        fallback of the builder) stops at the first nonzero result. OnCooldown 8 needs repeatable quests (not in this backend);
--     3. the passing topics (ConversationTopic 2, the prompt, the topic CRC). The services are not shown (an open point of the client);
--     4. Leave 1 (@NPC_EndConversation) last: the screen hides it; with no other option E closes the screen (its center hint).
--   ObjectiveDetails (0x3773540): the classifier again; Proposal: its Title, ObjectiveProposalResponse, Proposal 3, its CRC,
--   AcceptObjective 4 (@NPC_AcceptObjective), Leave.
--   AcceptObjective (the screen first asks HasRoomForObjective): the Acceptance 4 (ObjectiveAcceptanceResponse; the protocol notes 3772:
--   the window shows it at once), the Accept option removed, 3772. CompleteObjective: the prepared Completion 5
--   (ObjectiveCompletionResponse), the Complete option removed, 794. Neither waits for an answer and neither is sent twice.
--   The refresh after an owner update (0x3765580, 0x37645B0, predicate 0x3759CE0): the selection again; with objective content (a ready
--   hand-in or a nonzero classification) the result is built again and published; else the prepared result is published (an empty
--   Acceptance or Completion dialogue closes the screen). The client runs it once in the frame after the updates of the quests and the
--   achievements [I: the scheduling edge is not traced]. A refused request gets no answer: the page stays (no timeout, no retry).
local OPTION = {leave = eConversationOptionType_Leave or 1, topic = 2, details = eConversationOptionType_ObjectiveDetails or 3,
                accept = eConversationOptionType_AcceptObjective or 4, complete = eConversationOptionType_CompleteObjective or 5,
                continue = eConversationOptionType_ContinueDialogue or 7}
local RESPONSE = {invalid = 0, default = 1, topic = 2, proposal = 3, acceptance = 4, completion = 5, available = 6, inprogress = 7,
                  cooldown = 8}
C.RESPONSE = RESPONSE
local LINEAR = Swc.config["javelin.enable-linear-conversations"] ~= false

local function key_of(text)
  return string.lower((tostring(text or ""):gsub("^@", "")))
end

local function objective_cell(row, name)
  local value = row and row.cells and row.cells[name]
  return value ~= nil and value ~= "" and value or nil
end

local function root_ready(rec)
  local state = rec.root and Swc.quests.state_of(rec, rec.root)
  return state == 2 or state == 3
end

local function objective_done(row)
  local achievement = objective_cell(row, "AchievementId")
  return achievement ~= nil and Swc.HasAchievement(Swc.AchievementIndex(achievement))
end

--- @return The first ready hand-in of an NPC among the active objectives in their stored order (0x3A369E0), or nil.
local function ready_handin(npcId)
  local npc = string.lower(npcId or "")
  for _, rec in ipairs(Swc.quests and Swc.quests.list or {}) do
    local destination = objective_cell(rec.row, "NpcDestinationId")
    if destination and string.lower(destination) == npc and root_ready(rec) and not objective_done(rec.row) then
      return rec
    end
  end
  return nil
end
C.ready_handin = ready_handin

--- The classifier of an objective of a state (0x37AE910; markerMode: the NPC marker query). @return A response value (0 for none).
local function classify(row, npcId, markerMode)
  if not row then
    return RESPONSE.invalid
  end
  local achievement = objective_cell(row, "AchievementId")
  for _, rec in ipairs(Swc.quests and Swc.quests.list or {}) do
    if rec.row == row then
      local destination = objective_cell(row, "NpcDestinationId")
      if destination and string.lower(destination) == string.lower(npcId or "") and root_ready(rec) then
        return RESPONSE.invalid   -- the ready hand-in path owns it
      end
      if markerMode and objective_cell(row, "HideIncompleteTurninMarker") == "1" then
        return RESPONSE.invalid
      end
      return RESPONSE.inprogress
    end
  end
  if achievement then
    for _, rec in ipairs(Swc.quests and Swc.quests.list or {}) do
      if objective_cell(rec.row, "AchievementId") == achievement then
        return RESPONSE.invalid   -- another active variant with the same completion achievement
      end
    end
  end
  if objective_done(row) then
    return RESPONSE.invalid
  end
  -- The general availability (0x3A4BFE0): the required level and the RequiredAchievementId expression (this backend has no faction, no
  -- progression, and no territory rule for an offer).
  local level = tonumber(objective_cell(row, "RequiredLevel") or "0") or 0
  if level > (tonumber(Swc.GetData("Hud.LocalPlayer.Progression.Level")) or 1) then
    return RESPONSE.invalid
  end
  local required = objective_cell(row, "RequiredAchievementId")
  if required and not achievements_pass(required) then
    return RESPONSE.invalid
  end
  return objective_cell(row, "ObjectiveProposalResponse") and RESPONSE.proposal or RESPONSE.invalid
end
C.classify = classify

local function option(text, optionType, dataId)
  return {text = text, optionType = optionType, dataId = dataId or 0}
end

local function leave_option()
  return option("@NPC_EndConversation", OPTION.leave, 0)
end

--- Sets the page chain of a dialogue key.
local function set_dialogue(key)
  C.lines, C.index = chain_of(key_of(key)), 1
end

--- The build of the result (0x37ACF10) for the selected state. @return True when the result has objective content.
local function build_result()
  local data = cdata()
  local state = data and C.stateId and data.states[C.stateId]
  local result = {title = "", dialogue = state and state.dialogue or C.line, responseType = RESPONSE.default, dialogueId = 0, options = {}}
  local content = false
  local ready = C.npcId ~= "" and ready_handin(C.npcId)
  if ready then
    local row = ready.row
    result.title, result.dialogueId = objective_cell(row, "Title") or "", row.crc
    local response = objective_cell(row, "DestinationCompletionAvailableResponse")
    if response then
      result.dialogue, result.responseType = response, RESPONSE.available
    else
      result.dialogue, result.responseType = objective_cell(row, "InProgressResponse") or result.dialogue, RESPONSE.inprogress
    end
    result.options[#result.options + 1] = option(objective_cell(row, "DestinationCompletionAvailablePrompt") or "", OPTION.complete, row.crc)
    content = true
  else
    for _, id in ipairs(state and state.objectives or {}) do
      local row = Swc.quests.D().objectives[string.lower(id)]
      local kind = classify(row, C.npcId, false)
      if kind == RESPONSE.proposal then
        local prompt = objective_cell(row, "PlayerPrompt")
        if prompt == "@prompt_DEFAULT" then
          prompt = objective_cell(row, "Title")
        end
        result.options[#result.options + 1] = option(prompt or "", OPTION.details, row.crc)
      elseif kind == RESPONSE.inprogress then
        result.dialogue, result.responseType = objective_cell(row, "InProgressResponse") or result.dialogue, RESPONSE.inprogress
      end
      content = content or kind ~= RESPONSE.invalid
      if kind ~= RESPONSE.invalid and LINEAR then
        break
      end
    end
  end
  for _, topic in ipairs(C.topics or {}) do
    result.options[#result.options + 1] = option(topic.prompt, OPTION.topic, topic.crc)
  end
  result.options[#result.options + 1] = leave_option()
  C.result = result
  set_dialogue(result.dialogue)
  return content
end
C.build_result = build_result

local function state_of(withOption)
  local result = C.result or {title = "", responseType = RESPONSE.default, dialogueId = 0, options = {}}
  local options = {}
  if withOption then
    if C.index < #C.lines then
      local nextLine = C.lines[C.index + 1].line
      local entry = cdata() and cdata().lines[nextLine]
      options[1] = option(entry and entry.prompt or "@ui_continue", OPTION.continue, 0)
    end
    for _, each in ipairs(result.options) do
      options[#options + 1] = each
    end
  end
  local line = C.lines[C.index] and C.lines[C.index].line or C.line
  return {title = result.title, dialogue = "@" .. line, responseType = result.responseType, dialogueId = result.dialogueId,
          options = options}
end

local function person_entity(key)
  return EntityId(string.format("8%010d", swcui.crc32(key or "")))
end

-- The canvas: the retail dynamic canvas loader loads it at the first conversation (dynamiccanvasloader.luac canvasInfos: the path
-- LyShineUI/Conversation/ConversationScreenV2, autohide, the draw order FULLSCREEN_DRAW_ORDER of CanvasCommon); that loader does not
-- run in the client, so this file loads it in the same way.
local PATH = "LyShineUI/Conversation/ConversationScreenV2.uicanvas"
local function ensure_canvas()
  if #DynamicBus.ConversationScreenV2.handlers > 0 then
    return   -- the screen is loaded (its OnInit connects DynamicBus.ConversationScreenV2)
  end
  local common = RequireScript("LyShineUI._Common.CanvasCommon")
  Swc.LoadScreen(PATH, true, common and common.FULLSCREEN_DRAW_ORDER or 0)
  Swc.Note("conversation: the screen ConversationScreenV2 loads")
end

local function send_state(withOption)
  local bus = DynamicBus.ConversationScreenV2
  if bus == nil or bus.Broadcast == nil then
    Swc.Note("conversation: no ConversationScreenV2 screen")
    return
  end
  bus.Broadcast.OnConversationStateChange(state_of(withOption))
end

local function request_end(why)
  if C.open and not C.ended then
    C.ended = true
    -- The finished topics go with the end: the client sends them (5397) after the 2473 of the close (AST-248).
    Swc.hostRequests[#Swc.hostRequests + 1] = "conversation end" .. (#C.finished > 0 and (" " .. table.concat(C.finished, ",")) or "")
    C.log[#C.log + 1] = "end: " .. why
    Swc.Note("conversation: the screen ends the conversation (" .. why .. ")")
  end
end

--- The client opens or closes the conversation screen (UI/SwcUiConversation.cpp).
function Swc.HostConversation(what, key, speaker, line, name, title, lines, npcId)
  if what == "open" then
    C.open, C.ended = true, false
    C.key, C.speaker, C.line, C.name, C.title = key or "", speaker or "", line or "", name or "", title or ""
    -- The lines: "KEY\tSPEAKER" each (the first line and its chain of dialogue-next), else the one line.
    C.lines, C.index = {}, 1
    for entry in string.gmatch(lines or "", "[^\n]+") do
      local lineKey, lineSpeaker = entry:match("^([^\t]*)\t?(.*)$")
      C.lines[#C.lines + 1] = {line = lineKey, speaker = lineSpeaker}
    end
    if #C.lines == 0 then
      C.lines[1] = {line = C.line, speaker = C.speaker}
    end
    -- The state of the client for the NPC row of the person (AST-248); with no row the line of the person (persons.json).
    C.npcId, C.stateId, C.topics, C.taskObjective, C.finished = npcId or "", nil, {}, nil, {}
    C.result, C.prepared, C.dirty = nil, nil, false
    -- The refresh listens to the quest changes (swc/quests.lua; registered at the first open: this file loads before it).
    if not C.listening and Swc.quests and Swc.quests.listeners then
      C.listening = true
      Swc.quests.listeners[#Swc.quests.listeners + 1] = function() C.MarkChanged() end
    end
    if select_state(C.npcId) then
      build_result()
      C.log[#C.log + 1] = "state: " .. C.stateId .. " (" .. #C.topics .. " topic(s)), response " .. C.result.responseType
      Swc.Note(string.format("conversation: the state %s of %s, %d topic(s), the response %d, %d option(s)", C.stateId, C.npcId, #C.topics,
        C.result.responseType, #C.result.options))
    elseif C.npcId ~= "" and ready_handin(C.npcId) then
      build_result()
    else
      -- No NPC row (a person of persons.json with its line chain): the default result with Leave (MVP-H, AST-248).
      C.result = {title = "", responseType = RESPONSE.default, dialogueId = 0, options = {leave_option()}}
    end
    C.entity = person_entity(C.key)
    C.log[#C.log + 1] = "open: " .. C.key
    Swc.SetData("Hud.LocalPlayer.InteractEntityId", C.entity)   -- conversationoptionv2.luac reads the npc data through it
    ensure_canvas()
    send_state(true)
  elseif what == "close" and C.open then
    C.ended = true   -- the client ended the Talk: no request
    C.log[#C.log + 1] = "close"
    send_state(false)
    C.open = false
    Swc.SetData("Hud.LocalPlayer.InteractEntityId", EntityId())
  end
end

-- The refresh after an owner update (research/quest-conversation.md 5.4): swc/quests.lua (the quest state, the completion notice) and
-- swc/owner.lua (the Achievement state) mark it; the next frame selects the state again and builds the result again when it has objective
-- content, else it publishes the prepared result (an empty Acceptance or Completion closes the screen).
local function refresh()
  if not (C.open and not C.ended and C.dirty) then
    return
  end
  C.dirty = false
  local before = C.stateId
  C.stateId = nil
  local selected = select_state(C.npcId)
  local ready = C.npcId ~= "" and ready_handin(C.npcId)
  local content = false
  if selected or ready then
    local keep = C.result
    content = build_result()
    if not content then
      C.result = keep
    end
  end
  C.log[#C.log + 1] = "refresh: " .. tostring(C.stateId) .. (content and " (built again)" or " (the prepared result)")
  Swc.Note(string.format("conversation: the refresh: the state %s (before %s), %s", tostring(C.stateId), tostring(before),
    content and "the result built again" or "the prepared result"))
  if content then
    send_state(true)
  elseif C.prepared then
    local bus = DynamicBus.ConversationScreenV2
    if bus and bus.Broadcast then
      bus.Broadcast.OnConversationStateChange(C.prepared)
    end
  end
  C.prepared = nil
end
function C.MarkChanged()
  if C.open and not C.ended then
    C.dirty = true
  end
end
Swc.Connect("TickBus", {OnTick = function() refresh() end}, nil)

-- The room for an objective (conversationscreenv2.luac RequestConversationOption: AcceptObjective asks it first; "@ui_objective_type_full"
-- with no room) [I]: the active objectives of the type of the offer below the largest count of the type (GetMaxObjectivesForType; 0: no
-- cap known in this client).
Swc.Define("ObjectivesComponentRequestBus", "Event", {
  HasRoomForObjective = function(id, objectiveCrc)
    local data = ObjectivesDataManagerBus.Broadcast.GetObjectiveData(objectiveCrc)
    if not data then
      return true
    end
    local max = ObjectivesComponentRequestBus.Event.GetMaxObjectivesForType(id, data.type) or 0
    return max <= 0 or (ObjectivesComponentRequestBus.Event.GetObjectivesCountForType(id, data.type) or 0) < max
  end,
})

-- The recipe of an achievement (objectivedatahelper.luac GetRewardDataFromAchievementId: an objective whose AchievementId unlocks a
-- recipe has the recipe as a reward; the script tests the result against 0, so a missing method made a recipe reward of each objective
-- and stopped the details panel of the conversation): the recipe whose RequiredAchievementID is that achievement, else 0 [I: the native
-- index is not traced].
local recipeByAchievement
Swc.Define("RecipeDataManagerBus", "Broadcast", {
  GetRecipeIdByAchievementId = function(achievement)
    if recipeByAchievement == nil then
      recipeByAchievement = {}
      for _, id in ipairs(RecipeDataManagerBus.Broadcast.GetRecipes() or {}) do
        local data = RecipeDataManagerBus.Broadcast.GetCraftingRecipeDataById(id)
        local required = data and data.requiredAchievement or 0
        if required ~= 0 and recipeByAchievement[required] == nil then
          recipeByAchievement[required] = id
        end
      end
    end
    return recipeByAchievement[achievement] or 0
  end,
})

-- The interactable of the conversation: the person (the screen reads its npc data and its root).
Swc.Define("UiInteractorComponentRequestsBus", "Event", {
  GetInteractableEntityIdThroughInteractionDetails = function()
    return C.open and C.entity or EntityId()
  end,
  RequestCancelCommittedInteraction = function() request_end("RequestCancelCommittedInteraction") end,
})

-- The npc data of the person: its name and title (the header of the screen); no faction alignment.
local npcData = Swc.Bus("NpcComponentRequestBus").Event.GetNpcData
Swc.Define("NpcComponentRequestBus", "Event", {
  GetNpcData = function(id, ...)
    if C.open and id == C.entity then
      return {id = swcui.crc32(C.key), genericName = C.name, title = C.title, GetNpcFactionAlignment = function() return eFactionType_None or 0 end}
    end
    return npcData and npcData(id, ...) or nil
  end,
  -- The conversation state of the person for the player (conversationoptionv2.luac: HasTalkToTask gives the options the icon and the
  -- color of the objective of the task).
  GetCurrentStateWithPlayer = function(id)
    if C.open and id == C.entity and C.taskObjective then
      return {State = eConversationState_HasTalkToTask or 13, ConversationObjectiveInstanceId = C.taskObjective}
    end
    return {State = eConversationState_NoObjective or 0}
  end,
})

-- The attributes of the line: the speaker (the localized name of the person); no VO, no cinematic, no timeline, no camera
-- override (the client plays the camera and the clips of the Talk itself).
Swc.Define("LyShineScriptBindRequestBus", "Broadcast", {
  GetAttributeValueForKeyDefaultEmpty = function(text, attribute)
    if C.open and attribute == "speaker" then
      local key = string.lower(tostring(text or "")):gsub("^@", "")
      for _, entry in ipairs(C.lines) do
        if entry.line == key then
          return entry.speaker
        end
      end
      -- A topic prompt (AST-248): its own speaker ("UI - Player Prompt": the screen fades the options in after its playerPromptDelay).
      local data = cdata()
      local entry = data and data.lines[key]
      if entry then
        return entry.speaker
      end
      return C.speaker
    end
    return ""
  end,
})

-- The next option (Continue): the next line of the chain, a state with the same option; after the last line the state has no
-- option, and the screen closes (the backend has no message for the lines: the client shows them; backend docs travel-and-
-- conversations.md "E goes to the next text, and the conversation ends at the last one").
Swc.Define("ConversationRequestBus", "Broadcast", {
  RequestConversationOption = function(optionType, dataId)
    C.log[#C.log + 1] = "option"
        local result = C.result or {options = {}}
    local function has_option(kind, id)
      for index, each in ipairs(result.options) do
        if each.optionType == kind and each.dataId == id then
          return index
        end
      end
      return nil
    end
    if optionType == OPTION.accept and has_option(OPTION.accept, dataId) then
      local row = Swc.quests.D().byCrc[dataId]
      table.remove(result.options, has_option(OPTION.accept, dataId))
      Swc.hostRequests[#Swc.hostRequests + 1] = "quest accept " .. tostring(dataId)
      C.log[#C.log + 1] = "accept: " .. tostring(row and row.id)
      Swc.Note("conversation: accept " .. tostring(row and row.id) .. " (3772)")
      C.result = {title = objective_cell(row, "Title") or "", dialogue = objective_cell(row, "ObjectiveAcceptanceResponse") or "",
                  responseType = RESPONSE.acceptance, dialogueId = dataId, options = {leave_option()}}
      local dialogue = C.result.dialogue
      local bus = DynamicBus.ConversationScreenV2
      if dialogue == "" then
        if bus and bus.Broadcast then
          bus.Broadcast.OnConversationStateChange({title = C.result.title, dialogue = "", responseType = RESPONSE.acceptance, dialogueId = dataId,
            options = {}})
        end
      else
        set_dialogue(dialogue)
        send_state(true)
      end
      return
    end
    if optionType == OPTION.complete and has_option(OPTION.complete, dataId) then
      local row = Swc.quests.D().byCrc[dataId]
      table.remove(result.options, has_option(OPTION.complete, dataId))
      Swc.hostRequests[#Swc.hostRequests + 1] = "quest complete " .. tostring(dataId)
      C.log[#C.log + 1] = "complete: " .. tostring(row and row.id)
      Swc.Note("conversation: complete " .. tostring(row and row.id) .. " (794)")
      local dialogue = objective_cell(row, "ObjectiveCompletionResponse")
      C.prepared = {title = objective_cell(row, "Title") or "", dialogue = dialogue and dialogue or "", responseType = RESPONSE.completion,
                    dialogueId = dataId, options = dialogue and {option("@ui_back", 6, 0), leave_option()} or {}}
      return
    end
    if optionType == OPTION.details and has_option(OPTION.details, dataId) then
      local row = Swc.quests.D().byCrc[dataId]
      if classify(row, C.npcId, false) == RESPONSE.proposal then
        C.result = {title = objective_cell(row, "Title") or "", dialogue = objective_cell(row, "ObjectiveProposalResponse"),
                    responseType = RESPONSE.proposal, dialogueId = dataId,
                    options = {option("@NPC_AcceptObjective", OPTION.accept, dataId), leave_option()}}
        set_dialogue(C.result.dialogue)
        C.log[#C.log + 1] = "proposal: " .. row.id
        Swc.Note("conversation: the proposal of " .. row.id)
        send_state(true)
        return
      end
    end
    for _, topic in ipairs(C.topics or {}) do
      if dataId ~= nil and dataId ~= 0 and topic.crc == dataId then
        -- A topic (3158): its response, then its passing ProvidedTopics (backend persons.md "The topics of a selection").
        Swc.hostRequests[#Swc.hostRequests + 1] = "conversation topic " .. tostring(topic.crc)
        local finished = false
        for _, crc in ipairs(C.finished) do finished = finished or crc == topic.crc end
        if not finished then C.finished[#C.finished + 1] = topic.crc end
        C.log[#C.log + 1] = "topic: " .. topic.id
        Swc.Note("conversation: the topic " .. topic.id)
        C.lines, C.index = chain_of(topic.response), 1
        C.topics = passing_topics(topic.provided)
        -- The topic response [I: Topic 2 with the topic CRC]: the passing ProvidedTopics, Leave.
        local topicOptions = {}
        for _, provided in ipairs(C.topics) do
          topicOptions[#topicOptions + 1] = option(provided.prompt, OPTION.topic, provided.crc)
        end
        topicOptions[#topicOptions + 1] = leave_option()
        C.result = {title = "", responseType = RESPONSE.topic, dialogueId = topic.crc, options = topicOptions}
        send_state(true)
        return
      end
    end
    -- Continue (a local page action, 0x37622D0): the next page of the dialogue. Other options (Leave from the exit control) change
    -- nothing here.
    if optionType == OPTION.continue and C.index < #C.lines then
      C.index = C.index + 1
      send_state(true)
    elseif optionType == OPTION.continue or optionType == nil then
      send_state(true)
    end
  end,
})

Swc.Define("PlayerCutsceneComponentRequestBus", "Broadcast", {
  HandleOnCloseConversation = function() request_end("HandleOnCloseConversation") end,
  RequestStartSceneForLocTag = function() end,
  RequestStopSceneForLocTag = function() end,
  GetMultiNpcEntityId = function() return EntityId() end,
})

-- The camera of the screen: the client sets the Talk camera (a cut) and gives it back at the end.
Swc.Define("JavCameraControllerRequestBus", "Broadcast", {
  OverrideCameraState = function() end,
  RestoreCameraState = function() end,
  SetCameraLookAt = function() end,
  ClearCameraLookAt = function() end,
  SetCameraEnableMouseInput = function() end,
  MakeActiveView = function() end,
})
Swc.Define("JavelinCameraRequestBus", "Broadcast", {
  EnableDepthOfField = function() end,
  ResetDepthOfField = function() end,
  GetCurrentDepthOfFieldDistance = function() return 0 end,
  GetCurrentDepthOfFieldBlur = function() return 0 end,
})
Swc.Define("TimelineControllerComponentRequestBus", "Event", {UnTriggerTimelineOverrideForCrc = function() end})
-- The top progression of the objective of the line: none (the backend has no objectives): an empty record.
Swc.Define("ObjectivesComponentRequestBus", "Event", {GetTopProgressionForObjective = function() return {displayName = ""} end})
-- The faction cooldown of the person (the text of a faction line): none (the time now).
Swc.Define("FactionRequestBus", "Event", {GetCooldownEndTime = function() return WallClockTimePoint:Now() end})

-- The subtitle options that the options data does not decode (gamedata/options.json "unknown"): the values of the saved
-- options of the retail client (usersettings javsave of the Soulwarden development prefix: m_subtitlesSpeakerNameColorOption
-- 0, m_subtitlesTextColorOption 0, m_subtitlesTextSizeOption 1, m_subtitlesTextBorderEffectOption 0). The screen reads them
-- for the colors of the subtitle.
for node, value in pairs({SubtitlesSpeakerNameColorOption = 0, SubtitlesTextColorOption = 0, SubtitlesTextSizeOption = 1,
                          SubtitlesTextBorderEffectOption = 0}) do
  local path = "Hud.LocalPlayer.Options.Accessibility." .. node
  if LyShineDataLayerBus.Broadcast.GetDataFromNode(path) == nil then
    Swc.SetData(path, value)
  end
end
