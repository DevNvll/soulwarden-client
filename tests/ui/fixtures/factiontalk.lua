-- The Talk service of a recruiter (2b of ut567, part 2: C5 to C8; Soulwarden docs/systems/factions.md "The join and the change"; retail
-- Soulwarden qg7 shots 13-options-3, 14-choice-poster, 15-confirm-popup, 16-after-join): Astrid (9826_Astrid, the Syndicate) with the
-- recruitment of the Syndicate active (FactionIntro_Syndicate_Recruitment, its task JoinFaction Active) and a character with no faction:
-- the state 9826_RepAstrid_01 with its services Faction_Selection (the option ChooseFaction 11, "@Topic_Prompt_Choose_Faction") and
-- PvP_Guide (OpenFactionGuide 20, "@98_PvP_prompt"); the order of the builder: the objectives, the topics, the services, Leave (static
-- read ut567/qgplan/f4service); the choice opens the poster of the screen (ShowFactionSelection: a local event, no
-- request); the poster's Join, the popup, Yes: "faction set 1" (3161); the Faction state of the answer closes the popup and the poster,
-- and the Talk selects its state again (RefreshConversation 6: the state 02 of a Syndicate character). A Marauder at the state 01_b gets
-- no ChooseFaction option (the service rule: the faction of the state, None, is not the faction of the character).
-- Pictures: factiontalk-options, factiontalk-poster, factiontalk-after.
local HUD = 2702338936
local conversation = screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
local popup = screen("LyShineUI/Popup/Popup.uicanvas", 52)
screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(17, 0)   -- the level of the recruitment
Swc.HostSocial()             -- the poster reads GetFriends (the crash of the retail client with no Social state)
Swc.HostFaction(0, "0", 0)
frames(0.3)

local C = Swc.conversation
local errorsBefore = swcui.error_count()
local CHOOSE_FACTION, FACTION_GUIDE = eConversationOptionType_ChooseFaction or 11, eConversationOptionType_OpenFactionGuide or 20

local function requests()
  local out = {}
  for _ = 1, 16 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local function walk_texts(root, all)
  local out = {}
  local function walk(id, depth)
    if depth > 30 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and (all or swcui.enabled_in_hierarchy(id)) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(root, 0)
  return table.concat(out, " | ")
end
local function texts(canvas)
  return walk_texts(swcui.canvas_get(canvas or conversation, "root"))
end
local function find_text(text, canvas)
  local found
  local function walk(id, depth)
    if found or depth > 30 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t:find(text, 1, true) and swcui.enabled_in_hierarchy(id) then found = id return end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(canvas or conversation, "root"), 0)
  return found
end
local function option_types()
  local out = {}
  for _, each in ipairs(C.result and C.result.options or {}) do out[#out + 1] = each.optionType end
  return out
end
--- The pages of the dialogue to the last one (Continue 7): the screen shows the options of the result at the last page.
local function last_page()
  for _ = 1, 10 do
    if C.index >= #C.lines then break end
    ConversationRequestBus.Broadcast.RequestConversationOption(eConversationOptionType_ContinueDialogue, 0)
    frames(1.5)
  end
end
local function has_type(kind)
  for _, each in ipairs(option_types()) do
    if each == kind then return true end
  end
  return false
end

-- The recruitment of the Syndicate with its task JoinFaction Active (the root and the containers Active).
local INSTANCE = "4343"
local function recruitment_state(joinState)
  local row = Swc.quests.D().objectives["factionintro_syndicate_recruitment"]
  local lines = {"A " .. INSTANCE .. " " .. row.crc .. " 4 0"}
  for _, entry in ipairs(row.tree) do
    local task = Swc.quests.D().tasks[entry[2]]
    local state = 0
    if entry[2] == "factionintro_syndicate_recruitment_joinfaction" then
      state = joinState
    elseif entry[2] == row.root or entry[2] == "factionintro_syndicate_recruitment_container2" then
      state = 1
    end
    lines[#lines + 1] = string.format("T %s %d %d 0 %d", INSTANCE, entry[1], task.crc, state)
  end
  return table.concat(lines, "\n")
end
Swc.HostOwnership(tostring(Swc.AchievementIndex("FactionIntro_Syndicate_03")), "", true)
Swc.HostQuests(recruitment_state(1))
frames(0.5)

-- 1. The options of the state 01 (retail qg7 13: "I've decided. I want to join the Synd..." and "Why join a faction?").
Swc.HostConversation("open", "person:astrid", "", "", "Astrid", "Syndicate Representative", "", "9826_Astrid")
frames(5.0)
last_page()
frames(2.0)
equal(C.stateId, "9826_RepAstrid_01", "no faction, the task JoinFaction active: the state 01 of Astrid")
check(has_type(CHOOSE_FACTION), "the service Faction_Selection: the option ChooseFaction (11)")
check(has_type(FACTION_GUIDE), "the service PvP_Guide: the option OpenFactionGuide (20, not OpenPvPGuide 19)")
equal(table.concat(option_types(), " "), "2 11 20 1", "the order: the topic Decline, the two services in the state order, Leave")
equal(C.npc_faction("9826_Astrid"), 1, "the faction of Astrid: the Syndicate (the last Faction_Board state, 03)")
equal(C.npc_faction("0441a_SairaYangandul"), 0, "a person with no Faction_Board state: None")
note("the option types: " .. table.concat(option_types(), " "))
local shown = texts()
note("the state 01: " .. shown)
check(shown:find("I've decided. I want to join the Syndicate.", 1, true) ~= nil,
  "the choice text names the faction of Astrid ({npcFaction}: GetNpcFactionAlignment)")
check(shown:find("Why join a faction?", 1, true) ~= nil, "the PvP guide option shows")
picture("factiontalk-options")
requests()

-- 2. The choice: the poster of the screen (ShowFactionSelection), no request to the server, the Talk stays open.
local choice = find_text("I want to join the Syndicate")
check(choice ~= nil, "the choice option is a shown element")
if choice then click(choice) end
frames(1.5)
local poster = find(swcui.canvas_get(conversation, "root"), "ChangeFactionPoster")
check(poster ~= nil and swcui.enabled_in_hierarchy(poster), "the poster shows")
local posterText = poster and walk_texts(poster) or ""
note("the poster: " .. posterText)
check(posterText:lower():find("join the syndicate", 1, true) ~= nil, "the poster: Join the Syndicate")
local sent = requests()
equal(#sent, 0, "the choice sends nothing (" .. table.concat(sent, "; ") .. ")")
check(C.open and not C.ended, "the Talk stays open under the poster")
picture("factiontalk-poster")

-- 3. Join, the popup (drawn over the Talk), Yes: the request 3161 with the Syndicate.
local join = poster and find(poster, "ConfirmButton")
check(join ~= nil, "the Join button of the poster")
if join then click(join, 0.5, 0.5) end
frames(1.0)
check(swcui.canvas_get(popup, "enabled"), "the confirmation popup shows")
note("the popup: " .. texts(popup))
local yes = find(swcui.canvas_get(popup, "root"), "ButtonYes")
check(yes ~= nil, "the Yes button of the popup")
if yes then click(yes, 0.5, 0.5) end
frames(0.5)
local joined = false
for _, r in ipairs(requests()) do joined = joined or r == "faction set 1" end
check(joined, "Yes asks the host for 3161 with the Syndicate")

-- 4. The answer: the Faction state of the Syndicate (a join: the time 0) and the task JoinFaction complete. The popup and the poster
-- close, and the Talk selects its state again (retail qg7 16: the dialogue of the new state).
Swc.HostFaction(1, "0", 0)
Swc.HostQuests(recruitment_state(2))
frames(2.0)
check(not swcui.canvas_get(popup, "enabled"), "the popup closes")
check(not (poster and swcui.enabled_in_hierarchy(poster)), "the poster closes")
equal(C.stateId, "9826_RepAstrid_02", "the Talk of a Syndicate character: the state 02")
check(not has_type(CHOOSE_FACTION), "the state 02: no ChooseFaction option")
check(C.lines[1] and C.lines[1].line == "9826_repastrid_02_a", "the screen shows the dialogue of the state 02 (retail qg7 16)")
note("after the join: " .. texts())
picture("factiontalk-after")
Swc.HostConversation("close")
frames(1.0)
requests()

-- 5. A Marauder with the recruitment achievement of the Syndicate and no wait: the state 01_b, no ChooseFaction option (the state has
-- no faction; the service needs the faction of the state or Any), the PvP guide stays.
Swc.HostQuests("")
Swc.HostOwnership(tostring(Swc.AchievementIndex("FactionIntro_Syndicate_Recruitment")), "", true)
Swc.HostFaction(2, "0", 0)
frames(0.5)
Swc.HostConversation("open", "person:astrid", "", "", "Astrid", "Syndicate Representative", "", "9826_Astrid")
frames(3.0)
last_page()
equal(C.stateId, "9826_RepAstrid_01_b", "a Marauder with the recruitment achievement: the state 01_b")
check(not has_type(CHOOSE_FACTION), "a Marauder at 01_b: no ChooseFaction option")
check(has_type(FACTION_GUIDE), "a Marauder at 01_b: the PvP guide option")
Swc.HostConversation("close")
frames(1.0)
requests()

equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
