-- The conversation fixture (stage MVP-H, Linear AST-223): the original ConversationScreenV2 (conversationscreenv2.luac) with
-- the state of swc/conversation.lua. The client opens a Talk with a person of one line: the screen state, the subtitle
-- "SPEAKER: TEXT", the key hints. E (ui_interact) at the last line closes the screen and gives one request "conversation end";
-- Escape does the same; the close of the client (a refusal) closes the screen at once with no request.
-- Pictures: conversation-open.
local HUD, CONVERSATION = 2702338936, 4203137545
-- The canvas: in the game swc/conversation.lua loads it at the first conversation (the screen list of the client allows it; the
-- runtime of a fixture allows no loader screen, so the fixture loads it first, and the module must not load a second copy).
local conversation = screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
local helpbar = screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
-- The escape menu (AST-234): Escape at the Talk screen only goes back; the game menu must not open.
local escapemenu = screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 40)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
frames(0.3)
local root

local function requests()
  local out = {}
  for _ = 1, 8 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end

local function open(lines)
  Swc.HostConversation("open", "r_+04_+02:abc:1:8836.19,4246.09", "Zeng Lingyun", "08_sq_zeng_greet_00", "Zeng Lingyun", "", lines)
  frames(1.5)
end
local function body_text()
  return swcui.get(find(root, "BodyText"), "text.text") or ""
end

-- The open.
open()
equal(#DynamicBus.ConversationScreenV2.handlers, 1, "one conversation screen after the open (no second load)")
root = swcui.canvas_get(conversation, "root")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CONVERSATION, "the open sets the state ConversationScreenV2")
check(swcui.canvas_get(conversation, "enabled"), "the conversation screen shows")
local body = swcui.get(find(root, "BodyText"), "text.text") or ""
note("the body text: " .. body)
check(body:find("Zeng Lingyun", 1, true) ~= nil and body:find("The Empress needs all the allies", 1, true) ~= nil,
  "the subtitle has the speaker and the line")
frames(2.0)
local function shown(name)
  local id = find(root, name)
  return id ~= nil and swcui.enabled_in_hierarchy(id)
end
note("hints: center " .. tostring(shown("CenterHintContainer")) .. " exit " .. tostring(shown("ExitHintContainer"))
  .. " " .. tostring(shown("ExitButtonContainer")) .. " options " .. tostring(shown("ConversationOptionsHolder")))
check(shown("CenterHintContainer"), "the hint E Next shows")
do
  local hb = swcui.canvas_get(helpbar, "root")
  local texts = {}
  local function walk(id, depth)
    if depth > 12 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then texts[#texts + 1] = swcui.name(id) .. "=" .. t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(hb, 0)
  note("helpbar enabled " .. tostring(swcui.canvas_get(helpbar, "enabled")) .. ": " .. table.concat(texts, "; "))
end
picture("conversation-open")

-- E at the last line: the screen closes and asks for the end of the conversation one time.
requests()
key("e")
frames(1.0)
local r = requests()
equal(#r, 1, "E gives one request")
equal(r[1], "conversation end", "the request is the end of the conversation")
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= CONVERSATION, "E closes the screen state")
Swc.HostConversation("close")
frames(0.5)

-- Two lines (the chain of dialogue-next of Commander Purcell): E shows the second line with no request; E at the last line ends.
open("npc_defaultdialogue_12_00_commander_new_a\tCommander Purcell\nnpc_defaultdialogue_12_00_commander_new_b\tCommander Purcell")
note("line a: " .. body_text())
check(body_text():find("We are forged in the fire", 1, true) ~= nil, "the first line of the chain shows")
requests()
key("e")
frames(1.5)
note("line b: " .. body_text())
equal(#requests(), 0, "E at the first line gives no request")
check(body_text():find("Go on and look into the other factions", 1, true) ~= nil, "E shows the second line")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CONVERSATION, "the screen stays at the second line")
key("e")
frames(1.0)
r = requests()
equal(r[1], "conversation end", "E at the last line ends the conversation")
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= CONVERSATION, "E at the last line closes the screen")
Swc.HostConversation("close")
frames(0.5)

-- Escape (Back): the same end.
open()
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CONVERSATION, "the second open sets the state again")
requests()
key("escape")
frames(1.0)
r = requests()
equal(#r, 1, "Escape gives one request")
equal(r[1], "conversation end", "the request of Escape is the end of the conversation")
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= CONVERSATION, "Escape closes the screen state")
frames(0.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "Escape at the Talk screen goes back to the HUD (no game menu)")
Swc.HostConversation("close")
frames(0.5)

-- The close of the client (a refusal 3743 result 2): the screen closes at once, no request.
open()
requests()
Swc.HostConversation("close")
frames(0.5)
r = requests()
equal(#r, 0, "the close of the client gives no request")
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= CONVERSATION, "the close of the client closes the screen state")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "the state is HUD again")

-- AST-287 (the user test 2026-10-24): the Tour Board of Everfall is an npc object with no person of the person layer: its NPC row
-- EF_Tour_2 and no line. The character of the fixture has no achievement 9401_TutorialComplete: the state 9401_00_EF_Tour_2 shows its
-- text "You see a posting board. ...". An NPC row with no passing state and no line gives no screen: the client ends the Talk.
local result = Swc.HostConversation("open", "a04fab3b-965a-0c32-c854-dcb1be629131", "", "", "Tour Board", "", "", "EF_Tour_2")
frames(1.5)
equal(result, "ok", "the board opens the screen with its NPC row")
equal(Swc.conversation.stateId, "9401_00_EF_Tour_2", "the board: the initial state of its row")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CONVERSATION, "the board sets the state ConversationScreenV2")
note("the board text: " .. body_text())
check(body_text():find("posting board", 1, true) ~= nil, "the board shows the text of its state")
requests()
key("e")
frames(1.0)
r = requests()
equal(r[1], "conversation end", "E at the board text ends the conversation")
Swc.HostConversation("close")
frames(0.5)
result = Swc.HostConversation("open", "a04fab3b-965a-0c32-c854-dcb1be629131", "", "", "Nobody", "", "", "NoSuchRow")
frames(0.5)
check(result ~= "ok", "an NPC row with no state and no line: no screen (" .. tostring(result) .. ")")
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= CONVERSATION, "no screen: the state is not ConversationScreenV2")
equal(#requests(), 0, "no screen: the script gives no request (the client ends the Talk)")

-- The camera of the conversation (conversationscreenv2.luac SetScreenVisible): UI_Conversation 0.4 s and the look-at point of
-- GetNpcLookAtPosition; the depth of field to 5 0.8; the close removes all of them (TryCloseInteraction).
local camera = camera_log()
camera_sent(camera, "camera state UI_Conversation 0.400", "the conversation")
camera_sent(camera, "camera lookat ", "the conversation")
camera_sent(camera, "camera dof 5.000 0.800 0.000 0.000 10.000", "the conversation")
camera_sent(camera, "camera lookat -", "the conversation closes")
camera_clean("the conversation")
