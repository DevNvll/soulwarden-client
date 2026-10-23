-- The quest persons fixture (stage Q4, Linear AST-239; docs/PERSONS.md "The conversation state"; backend persons.md, stage 1.8f): the
-- state selection of swc/conversation.lua with the validity of a record (IsInitialState XOR a requirement) and the offers of a state
-- (gamedata/conversations.json "objectives"). Marcel Dupont: with no bit the initial state 06_00; with S_introtosettlement the state
-- 06_01 (the offer 06A_MSQ_01_Quartermaster), not 06_01b (no requirement and not initial: never selected). Aelstrom of the beach: with no
-- bit 01_Aelstrom_Default; with S_getoffbeach_ww 01_AelstromWW_p1_01 (the offer 06_skinningknife).
-- Needs gamedata/conversations.json with "initial" and "objectives" (tools/convert-persons.sh conversations, AST-239).
local HUD = 2702338936
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(20, 0)
frames(0.3)

local C = Swc.conversation
local function bits(...)
  local out = {}
  for _, id in ipairs({...}) do
    local index = Swc.AchievementIndex(id)
    check(index ~= nil, "the achievement " .. id .. " has an index")
    out[#out + 1] = tostring(index or "")
  end
  Swc.HostOwnership(table.concat(out, ","), "", true)
end
local function select(npc)
  C.stateId = nil
  C.select_state(npc)
  return C.stateId
end

check(C.record_valid ~= nil, "conversation.lua has the validity rule of a state record")
local data = swcui.load_json("gamedata/conversations.json")
local p1 = data and data.states["01_AelstromWW_p1_01"]
check(p1 ~= nil and p1.initial == false and p1.objectives and p1.objectives[1] == "06_skinningknife",
  "conversations.json has initial and objectives (01_AelstromWW_p1_01 offers 06_skinningknife)")

-- Marcel Dupont.
bits()
equal(select("06_MarcelDupont01"), "06_00_MarcelDupont", "Marcel Dupont with no bit: the initial state 06_00")
bits("S_introtosettlement")
equal(select("06_MarcelDupont01"), "06_01_MarcelDupont", "with S_introtosettlement: 06_01, not 06_01b (no requirement, not initial)")
check(data and not C.record_valid(data.states["06_01b_MarcelDupont"]), "06_01b_MarcelDupont is not a valid record")
check(data and C.record_valid(data.states["06_00_MarcelDupont"]), "06_00_MarcelDupont (initial, no requirement) is valid")

-- Aelstrom.
bits()
equal(select("01_AelstromWW_01"), "01_Aelstrom_Default", "Aelstrom with no bit: 01_Aelstrom_Default")
bits("S_getoffbeach_ww")
equal(select("01_AelstromWW_01"), "01_AelstromWW_p1_01", "Aelstrom with S_getoffbeach_ww: 01_AelstromWW_p1_01 (the knife offer)")

-- ------------------------------------------------------------------------------------------------- the hand-in and the offer [I]
-- The flow of the retail references of the backend stage 1.8b (P1, F1): 06_getoffbeach ready (its root Complete) opens the conversation
-- of Aelstrom on its hand-in chain; its last line shows the details and "Complete the Quest" (794); the reply of the server (the
-- completion notice and the bit S_getoffbeach_ww) gives the new state 01_AelstromWW_p1_01 with the offer "Do you have food and
-- supplies?" (06_skinningknife); its proposal chain ends with "Accept Quest" (3772) and the Acceptance state.
local conversation = screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
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
-- The state that the screen got (conversationscreenv2.luac OnConversationStateChange keeps it as stateData).
local function screen_state()
  for _, list in pairs(Swc.components) do
    for _, instance in ipairs(list) do
      if string.find(instance.__swcScript or "", "conversationscreenv2.luac", 1, true) then
        return instance.stateData, instance
      end
    end
  end
  return nil
end
local function watch() end

local INSTANCE = "777"
local row = Swc.quests.D().objectives["06_getoffbeach"]
local function ready_state()
  local lines = {"A " .. INSTANCE .. " " .. row.crc .. " 6 0"}
  for _, entry in ipairs(row.tree) do
    local task = Swc.quests.D().tasks[entry[2]]
    lines[#lines + 1] = string.format("T %s %d %d %d %d", INSTANCE, entry[1], task.crc, entry[1] == row.root and 4 or 1, 2)
  end
  lines[#lines + 1] = "K " .. INSTANCE
  return table.concat(lines, "\n")
end
bits()
Swc.HostQuests(ready_state())
Swc.HostQuestPinReady(true)
frames(0.5)
check(ObjectivesComponentRequestBus.Event.IsObjectiveReadyForCompletion(Swc.ids.player, Swc.quests.byKey[INSTANCE].id),
  "06_getoffbeach is ready for the hand-in (its root Complete)")
Swc.HostConversation("open", "person:aelstrom", "Aelstrom", "01_aelstrom_default_00", "Aelstrom", "", "", "01_AelstromWW_01")
watch()
frames(2.0)
local C = Swc.conversation
equal(C.result and C.result.responseType, 6, "a ready hand-in: CompletionAvailable 6 from the first page (research quest-conversation.md 4.1)")
equal(C.result and C.result.dialogueId, row.crc, "a ready hand-in: the objective CRC as dialogueId")
equal(C.result and C.result.title, "@01_GetOffBeach_title", "a ready hand-in: the objective Title")
check(C.result and C.result.options[1].optionType == 5 and C.result.options[#C.result.options].optionType == 1,
  "the result: CompleteObjective 5 first, Leave 1 last")
equal(#C.lines, 5, "the page chain of the ready response: 5 pages")
note("the first line: " .. texts())
check(texts():find("born again", 1, true) ~= nil, "between the lines the option is the player reply (dialogue-prompt: \"Wait... born again?\")")
for _ = 1, 4 do
  ConversationRequestBus.Broadcast.RequestConversationOption(eConversationOptionType_ContinueDialogue, 0)
  frames(1.5)
end
local last = screen_state()
equal(last and last.responseType, eConversationResponseType_CompletionAvailable, "the last line: the response type CompletionAvailable")
equal(last and last.dialogueId, row.crc, "the last line: the objective CRC as dialogueId (the details panel)")
frames(3.0)
note("the last line: " .. texts())
check(texts():find("Complete the Quest", 1, true) ~= nil, "the last line shows \"Complete the Quest\"")
check(texts():find("Soul Survivor", 1, true) ~= nil or texts():find("SOUL SURVIVOR", 1, true) ~= nil, "the details panel shows the quest")
picture("questpersons-complete")
requests()
local complete = find_text("Complete the Quest")
if complete then click(complete) end
frames(1.0)
local r = requests()
equal(r[1], "quest complete " .. row.crc, "Complete the Quest sends 794 with the objective CRC")

-- The reply of the server: the completion notice removes the instance, the bit S_getoffbeach_ww.
check(C.result and C.result.responseType == 6, "no answer yet: the page stays (no timeout, no retry)")
Swc.HostQuests("C " .. INSTANCE)
bits("S_getoffbeach_ww")
frames(4.0)
equal(C.stateId, "01_AelstromWW_p1_01", "after the hand-in: the new state of Aelstrom")
check(C.result and C.result.responseType == 1 and C.result.options[1].optionType == 3 and C.result.options[1].dataId == 3510093317,
  "the new menu: Default 1 with ObjectiveDetails 3 for 06_skinningknife (d137c605)")
note("after the hand-in: " .. texts())
check(texts():find("food and supplies", 1, true) ~= nil, "the offer option \"Do you have food and supplies?\" (PlayerPrompt of 06_skinningknife)")
picture("questpersons-offer")
local knife = Swc.quests.D().objectives["06_skinningknife"]
local offer = find_text("food and supplies")
if offer then click(offer) end
frames(1.5)
equal(C.result and C.result.responseType, 3, "the offer opens the proposal (Proposal 3)")
equal(C.result and C.result.title, "@01_skinningknife_title", "the proposal: the objective Title")
while C.index < #C.lines do
  ConversationRequestBus.Broadcast.RequestConversationOption(eConversationOptionType_ContinueDialogue, 0)
  frames(1.5)
end
last = screen_state()
equal(last and last.responseType, eConversationResponseType_Proposal, "the last proposal line: the response type Proposal")
frames(3.0)
note("the proposal: " .. texts())
check(texts():find("Accept Quest", 1, true) ~= nil, "the last proposal line shows \"Accept Quest\"")
picture("questpersons-proposal")
requests()
local accept = find_text("Accept Quest")
if accept then click(accept) end
frames(1.5)
r = requests()
equal(r[1], "quest accept " .. knife.crc, "Accept Quest sends 3772 with the objective CRC (d137c605)")
equal((screen_state() or {}).responseType, eConversationResponseType_Acceptance, "then the Acceptance state at once")
