-- User test 2026-10-26 (8d): E in the frame of the open of a Talk. The original ConversationScreenV2 sets up its options one tick after
-- OnConversationStateChange; before that, option 1 has the callback of conversationoptionv2.luac OnInit (OnPress on itself), and E
-- (ui_interact goes to dialogOption1 while the center hint is off) gave a stack overflow, and the Talk ended. The rule of
-- swc/conversation.lua: such a press does nothing. The fixture opens the first Talk of the runtime (a new canvas, as in the client).
local HUD, CONVERSATION = 2702338936, 4203137545
screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
frames(0.3)

local function requests()
  local out = {}
  for _ = 1, 8 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local function body_text()
  local scr = DynamicBus.ConversationScreenV2.handlers[1]
  scr = scr and (scr.table or scr)
  return scr and swcui.get(scr.Properties.BodyText, "text.text") or ""
end
local function option1()
  local scr = DynamicBus.ConversationScreenV2.handlers[1]
  scr = scr and (scr.table or scr)
  return scr and scr.ConversationOptions and scr.ConversationOptions[0]
end

-- Cecilia Polly (0410a_BarkeepPolly): one Leave option (the screen drops it: E Next ends the Talk).
local errors = swcui.error_count()
local result = Swc.HostConversation("open", "a04fab3b-965a-0c32-c854-dcb1be629131", "Cecilia Polly", "0410_00_barkeep_defaultdialog",
  "Cecilia Polly", "", "", "0410a_BarkeepPolly")
equal(result, "ok", "the Talk with Cecilia Polly opens")
local o = option1()
check(o ~= nil and o.callback == o.OnPress, "option 1 before its setup has the callback of OnInit (OnPress on itself)")
requests()
key("e")
equal(swcui.error_count() - errors, 0, "E in the frame of the open: no Lua error (before: a stack overflow)")
equal(#requests(), 0, "E in the frame of the open: no request")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CONVERSATION, "the Talk stays open")
key("e")
frames(1.0)
local r = requests()
equal(r[1], "conversation end", "a later E ends the Talk (the center hint Next)")
Swc.HostConversation("close")
frames(0.5)

-- Saira Yangandul (two lines): E in the frame of the open, then E shows the second line, and E at the last line ends.
errors = swcui.error_count()
result = Swc.HostConversation("open", "a04fab3b-965a-0c32-c854-dcb1be629132", "Saira Yangandul",
  "npc_defaultdialogue_0441a_00_sairayangandul_a", "Saira Yangandul", "",
  "npc_defaultdialogue_0441a_00_sairayangandul_a\tSaira Yangandul\nnpc_defaultdialogue_0441a_00_sairayangandul_b\tSaira Yangandul",
  "0441a_SairaYangandul")
equal(result, "ok", "the Talk with Saira Yangandul opens")
requests()
key("e")
equal(swcui.error_count() - errors, 0, "Saira: E in the frame of the open, no Lua error")
frames(1.5)
local first = body_text()
note("Saira, line a: " .. first)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CONVERSATION, "Saira: the Talk stays open")
key("e")
frames(1.5)
note("Saira, line b: " .. body_text())
check(body_text() ~= first, "Saira: E shows the second line")
equal(#requests(), 0, "Saira: E at the first line gives no request")
key("e")
frames(1.0)
r = requests()
equal(r[1], "conversation end", "Saira: E at the last line ends the Talk")
Swc.HostConversation("close")
frames(0.5)
equal(swcui.error_count(), 0, "no Lua error")
