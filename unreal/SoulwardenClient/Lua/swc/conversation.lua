-- The conversation screen (stage MVP-H, Linear AST-223; docs/PERSONS.md "The conversation screen"): the original canvas
-- ConversationScreenV2 with its script (conversationscreenv2.luac) and the native buses that it reads. The client owns the
-- Talk (the request, the camera, the lock, the close messages: Interact/SwcInteractor, Interact/SwcConversation); this file
-- gives the screen its state and sends the end of the player to the client.
--
-- The rules (the use in conversationscreenv2.luac and dynamiccanvasloader.luac; the retail references of set 5 of the Talk):
--   the open   Swc.HostConversation("open", KEY, SPEAKER, LINE, NAME, TITLE): one conversation state with the line of the person
--              (dialogue "@LINE", its speaker attribute) and one option, Continue ("@ui_continue", ContinueDialogue): the
--              screen shows the subtitle "SPEAKER: TEXT" and the keys "E Next" and "ESC Back" (the retail screen of a person
--              with one line). The backend has no conversation content and no service of a person (the shops and the
--              transmog are later stages): no quest option, no shop option, no next line.
--   the state  the copy of dynamiccanvasloader.luac OnConversationStateChange (title, dialogue, responseType, dialogueId, the
--              options with text, optionType, dataId) to DynamicBus.ConversationScreenV2 (the native loader does not run
--              in the client: the canvas is in the screen list).
--   the next   E (ui_interact) at the last line: RequestConversationOption of the option; the native gives a state with no
--              option, and the script closes the screen (CloseScreen, TryCloseInteraction).
--   the back   Escape (OnEscapeKeyPressed): CloseScreen, TryCloseInteraction.
--   the end    TryCloseInteraction: PlayerCutsceneComponentRequestBus HandleOnCloseConversation and UiInteractorComponentRequestsBus
--              RequestCancelCommittedInteraction: one host request "conversation end" (the client sends 6705, then 2473).
--   the close  Swc.HostConversation("close"): the end of the Talk by the client (a refusal 3743 result 2, the end by the
--              server 3746, a death, a loading screen, E of the client): a state with no option closes the screen at once.

local C = {open = false, ended = false, key = "", speaker = "", line = "", name = "", title = "", log = {}}
Swc.conversation = C

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
  if withOption then
    options[1] = {text = "@ui_continue", optionType = eConversationOptionType_ContinueDialogue or 7, dataId = 0}
  end
  return {
    title = C.title, dialogue = "@" .. C.line, responseType = eConversationResponseType_Invalid or 0,
    dialogueId = swcui.crc32(C.line), options = options,
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
    Swc.hostRequests[#Swc.hostRequests + 1] = "conversation end"
    C.log[#C.log + 1] = "end: " .. why
    Swc.Note("conversation: the screen ends the conversation (" .. why .. ")")
  end
end

--- The client opens or closes the conversation screen (UI/SwcUiConversation.cpp).
function Swc.HostConversation(what, key, speaker, line, name, title)
  if what == "open" then
    C.open, C.ended = true, false
    C.key, C.speaker, C.line, C.name, C.title = key or "", speaker or "", line or "", name or "", title or ""
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
})

-- The attributes of the line: the speaker (the localized name of the person); no VO, no cinematic, no timeline, no camera
-- override (the client plays the camera and the clips of the Talk itself).
Swc.Define("LyShineScriptBindRequestBus", "Broadcast", {
  GetAttributeValueForKeyDefaultEmpty = function(text, attribute)
    if C.open and attribute == "speaker" then
      return C.speaker
    end
    return ""
  end,
})

-- The next option: the line is the last one; the native state after it has no option (the screen closes).
Swc.Define("ConversationRequestBus", "Broadcast", {
  RequestConversationOption = function()
    C.log[#C.log + 1] = "option"
    send_state(false)
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
