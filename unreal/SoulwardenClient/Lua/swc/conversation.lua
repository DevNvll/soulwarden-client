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

local TOPIC_OPTION_TYPE = 2
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
local function select_state(npcId)
  local data = cdata()
  local states = data and data.npcs[string.lower(npcId or "")]
  if not states then
    return false
  end
  for i = #states, 1, -1 do
    local state = data.states[states[i]]
    if state and record_valid(state) and requirements_pass(state.requires, false) then
      C.stateId = states[i]
      C.lines, C.index = chain_of(state.dialogue), 1
      C.topics = passing_topics(state.topics)
      local r = state.requires or {}
      C.taskObjective = r.RequiredActiveObjectiveId and r.RequiredActiveTaskId and Swc.quests.ActiveTask(r.RequiredActiveObjectiveId, r.RequiredActiveTaskId) or nil
      return true
    end
  end
  return false
end
C.select_state = select_state
C.record_valid = record_valid

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

local function state_of(withOption)
  local options = {}
  if withOption and C.index >= #C.lines and #(C.topics or {}) > 0 then
    -- The topics at the last line. The option type of a topic is not in the enum table of the client: 2, the gap between Leave (1) and
    -- ObjectiveDetails (3) [inference; the screen drops the types Invalid and Leave: conversationscreenv2.luac OnConversationStateChange].
    -- The icon and the color come from the conversation state of the person (conversationoptionv2.luac).
    for _, topic in ipairs(C.topics) do
      options[#options + 1] = {text = topic.prompt, optionType = TOPIC_OPTION_TYPE, dataId = topic.crc}
    end
  elseif withOption then
    options[1] = {text = "@ui_continue", optionType = eConversationOptionType_ContinueDialogue or 7, dataId = 0}
  end
  local line = C.lines[C.index] and C.lines[C.index].line or C.line
  return {
    title = C.title, dialogue = "@" .. line, responseType = eConversationResponseType_Invalid or 0,
    dialogueId = swcui.crc32(line), options = options,
  }
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
    if select_state(C.npcId) then
      C.log[#C.log + 1] = "state: " .. C.stateId .. " (" .. #C.topics .. " topic(s))"
      Swc.Note("conversation: the state " .. C.stateId .. " of " .. C.npcId .. ", " .. #C.topics .. " topic(s)")
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
  RequestConversationOption = function(_, dataId)
    C.log[#C.log + 1] = "option"
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
        send_state(true)
        return
      end
    end
    if C.index < #C.lines then
      C.index = C.index + 1
      send_state(true)
    else
      send_state(false)
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
