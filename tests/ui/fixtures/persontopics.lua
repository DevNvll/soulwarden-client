-- The person topics fixture (Linear AST-248; docs/PERSONS.md "The conversation state"): the original ConversationScreenV2 with the
-- conversation state of the client (swc/conversation.lua, gamedata/conversations.json). Saira Yangandul with the quest
-- 04A_Side05_03_EarnAudience and its task TalkToSaira active: the state TaskTopic_EarnAudience_01 and its topic "Can I meet with your
-- employer?" (the backend live check L3); its selection gives "conversation topic 3845277786" (3158) and the response; the end gives
-- "conversation end 3845277786" (5397 after the 2473). With no quest: the default state, no topic. Aelstrom of a new character
-- (01_AelstromWW_01, no bit S_getoffbeach_ww): the default state 01_Aelstrom_Default, no topic, the end with no topic.
-- Pictures: persontopics-saira.
local HUD = 2702338936
local conversation = screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(20, 0)
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
local function texts()
  local out = {}
  local function walk(id, depth)
    if depth > 24 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(conversation, "root"), 0)
  return table.concat(out, " | ")
end
local function find_text(text)
  local found
  local function walk(id, depth)
    if found or depth > 24 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t:find(text, 1, true) and swcui.enabled_in_hierarchy(id) then found = id return end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(conversation, "root"), 0)
  return found
end

-- The quest of the backend live check L3: 04A_Side05_03_EarnAudience with its first task (TalkToSaira) active.
local INSTANCE = "4242"
local function quest_state(taskState)
  local row = Swc.quests.D().objectives["04a_side05_03_earnaudience"]
  local lines = {"A " .. INSTANCE .. " " .. row.crc .. " 4 0"}
  for _, entry in ipairs(row.tree) do
    local task = Swc.quests.D().tasks[entry[2]]
    local state = (entry[2] == "04a_side05_03_01_talktosaira" or entry[2] == row.root) and taskState or 0
    lines[#lines + 1] = string.format("T %s %d %d 0 %d", INSTANCE, entry[1], task.crc, state)
  end
  return table.concat(lines, "\n")
end

local SAIRA = "0441a_SairaYangandul"
local function open_saira()
  Swc.HostConversation("open", "person:saira", "", "", "Saira Yangandul", "", "", SAIRA)
  frames(2.0)
end

-- No quest: the default state, no topic.
check(Swc.quests.D().objectives["04a_side05_03_earnaudience"] ~= nil, "quests.json has 04A_Side05_03_EarnAudience")
open_saira()
equal(Swc.conversation.stateId, "0441a_00_SairaYangandul", "no quest: the default state of Saira")
equal(#Swc.conversation.topics, 0, "no quest: no topic")
note("no quest: " .. texts())
Swc.HostConversation("close")
frames(1.0)
requests()

-- The task active: the task topic state and its topic.
Swc.HostQuests(quest_state(1))
frames(0.5)
open_saira()
equal(Swc.conversation.stateId, "0441a_SairaYangandul_TaskTopic_EarnAudience_01", "the task active: the task topic state (backend live L3)")
equal(#Swc.conversation.topics, 1, "the task active: one topic")
frames(5.0)
local shownText = texts()
note("the topic state: " .. shownText)
check(shownText:find("Can I meet with your employer?", 1, true) ~= nil, "the topic option shows its prompt")
picture("persontopics-saira")
local option = find_text("Can I meet with your employer?")
check(option ~= nil, "the topic option is a shown element")
requests()
if option then click(option) end
frames(1.5)
local r = requests()
equal(r[1], "conversation topic 3845277786", "the topic selection: 3158 with the CRC-32 of the lower-case topic id (e532485a)")
note("the response: " .. texts())
check(Swc.conversation.lines[1] and Swc.conversation.lines[1].line == "0441a_sairayangandul_tasktopic_earnaudience_response_a",
  "the response of the topic shows")
-- The end (Escape): the finished topic goes with the end request.
key("escape")
frames(1.0)
r = requests()
equal(r[#r], "conversation end 3845277786", "the end: the finished topic for the 5397 after the 2473")
Swc.HostConversation("close")
frames(1.0)

-- A completed task: no topic again (the state of the task rule does not pass).
Swc.HostQuests(quest_state(2))
frames(0.5)
open_saira()
equal(Swc.conversation.stateId, "0441a_00_SairaYangandul", "the task complete: the default state")
Swc.HostConversation("close")
frames(1.0)
requests()

-- Aelstrom of a new character: the default state, the end with no topic.
Swc.HostConversation("open", "person:aelstrom", "", "", "Aelstrom", "Fallen King", "", "01_AelstromWW_01")
frames(2.0)
equal(Swc.conversation.stateId, "01_Aelstrom_Default", "Aelstrom of a new character: the default state")
note("Aelstrom: " .. texts())
check(texts():find("Corrupted", 1, true) ~= nil, "Aelstrom: the line of the default state shows")
key("escape")
frames(1.0)
r = requests()
equal(r[#r], "conversation end", "Aelstrom: the end with no topic (no 5397)")
