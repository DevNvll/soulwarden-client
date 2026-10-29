-- The faction rules of the Talk (2b of ut567, part 1: C1 to C4; docs/PERSONS.md "The conversation state"; Soulwarden
-- apps/backend/src/world/persons.cpp Persons::passes and progression_passes, world/quests.cpp Quests::available): the state selection of
-- swc/conversation.lua with the faction of the character (swc/factions.lua, the Faction state of slot 49), the wait of a faction change,
-- and the categorical progressions (swc/owner.lua HostRanks).
--   RequiredFaction        a state: None no rule, Any no rule, a named faction = the faction of the character; a topic: Any needs a
--                          faction (client 0x37AB440, 0x37AB7A0).
--   RequiredFactionCooldown  a state: NotInCooldown no rule; InCooldownMultipleDays more than 24 whole hours of the wait left;
--                          InCooldownSingleDay 1 to 24 whole hours (0x37AB5F9 to 0x37AB6B5).
--   RequiredProgression    "ID RANK POINTS": the rank of the categorical progression ID is RANK or more, and with POINTS above 0 its
--                          points are POINTS or more (0x37AF340, 0x3CB82D0).
--   The offers             RequiredFaction of an objective row: the faction mask with the faction of the character (0x3A4C166).
-- The persons of the data: the Brightwood alchemist 0208_Alchemist (the faction states 01 to 03, the progression states 04pre and 04) and
-- the Syndicate recruiter Astrid 9826_Astrid (the cooldown states _b).
local HUD = 2702338936
screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(17, 0)   -- the screen level 17 (the level of the recruitment)
Swc.HostSocial()
frames(0.3)

local C = Swc.conversation
local errorsBefore = swcui.error_count()
local SYNDICATE_REPUTATION = 0x56FBDE12   -- the CRC-32 of "syndicate" (the progression id of RequiredProgression "Syndicate 0 3000")
local HOUR = 3600
local WAIT = 2592000                      -- Faction Change Cooldown Seconds (30 days)

local function state_of(npc)
  return (C.choose_state(npc))
end
--- The Faction state: the faction and the time of the last change (seconds before now; nil: a join, the time 0).
local function faction(value, changedAgo)
  local ns = "0"
  if changedAgo then
    ns = string.format("%d000000000", math.floor(WallClockTimePoint:Now().seconds) - changedAgo)
  end
  Swc.HostFaction(value, ns, changedAgo and 1 or 0)
  frames(0.1)
end
local function bits(...)
  local out = {}
  for _, name in ipairs({...}) do
    out[#out + 1] = tostring(Swc.AchievementIndex(name))
  end
  Swc.HostOwnership(table.concat(out, " "), "", true)
  frames(0.1)
end

-- 1. RequiredFaction of a state: the faction states of the alchemist.
check(Swc.conversation.choose_state ~= nil and swcui.load_json("gamedata/conversations.json").npcs["0208_alchemist"] ~= nil,
  "conversations.json has the alchemist 0208_Alchemist")
equal(state_of("0208_Alchemist"), "0208_00_Alchemist", "no faction: the initial state of the alchemist")
faction(1)
equal(state_of("0208_Alchemist"), "0208_01_Alchemist", "the Syndicate: the state 01 (RequiredFaction Syndicate)")
faction(2)
equal(state_of("0208_Alchemist"), "0208_02_Alchemist", "the Marauders: the state 02 (RequiredFaction Marauder)")
faction(3)
equal(state_of("0208_Alchemist"), "0208_03_Alchemist", "the Covenant: the state 03 (RequiredFaction Covenant)")

-- 2. RequiredProgression: "Syndicate 0 3000" (04pre) and with RequiredLevel 19 (04).
faction(1)
Swc.HostRanks(string.format("%d 0 2999", SYNDICATE_REPUTATION))
frames(0.1)
equal(state_of("0208_Alchemist"), "0208_01_Alchemist", "2999 Syndicate points: not the state 04pre (3000 points)")
Swc.HostRanks(string.format("%d 0 3000", SYNDICATE_REPUTATION))
frames(0.1)
equal(state_of("0208_Alchemist"), "0208_04pre_Alchemist", "3000 Syndicate points: the state 04pre")
Swc.HostProgression(19, 0)
frames(0.1)
equal(state_of("0208_Alchemist"), "0208_04_Alchemist", "3000 points and the level 19: the state 04")
faction(2)
equal(state_of("0208_Alchemist"), "0208_02_Alchemist", "the Marauders with the Syndicate points: the state 02 (the faction gate first)")
Swc.HostRanks("")
Swc.HostProgression(17, 0)
frames(0.1)

-- 3. RequiredFaction of a topic (no topic of the data names a faction: the rule with the requirement tables).
faction(0)
check(C.requirements_pass({RequiredFaction = "Any"}, false), "a state with Any and no faction passes (no rule)")
check(not C.requirements_pass({RequiredFaction = "Any"}, true), "a topic with Any and no faction does not pass")
check(not C.requirements_pass({RequiredFaction = "Syndicate"}, true), "a topic with the Syndicate and no faction does not pass")
check(C.requirements_pass({RequiredFaction = "None"}, true), "a topic with None passes")
faction(1)
check(C.requirements_pass({RequiredFaction = "Any"}, true), "a topic with Any and a faction passes")
check(C.requirements_pass({RequiredFaction = "Syndicate"}, true), "a topic with the faction of the character passes")
check(not C.requirements_pass({RequiredFaction = "Covenant"}, true), "a topic with another faction does not pass")
check(C.requirements_pass({RequiredFaction = "!Covenant"}, false), "a negated name is no faction (None): no rule")
check(C.requirements_pass({RequiredProgression = "Syndicate 0"}, false), "a progression text with no points (rank 0, points 0): no rule")
check(not C.requirements_pass({RequiredProgression = "Syndicate 1"}, true), "a topic with a progression rank that the character does not have")
check(not C.requirements_pass({RequiredProgression = "Syndicate x 5"}, false), "a progression text that does not parse does not pass")

-- 4. The offers (the faction mask with the faction of the character): a row of the Syndicate (mask 0x2) and the recruitment of the
-- Syndicate (RequiredFaction !Syndicate: the complement 0xD has bit 0, which bypasses the test: offered to each faction, as in the backend
-- faction_mask; RequiredAchievementId FactionIntro_Syndicate_03, RequiredLevel 17).
local syndicateRow = {id = "factionrules_syndicate", cells = {RequiredFaction = "Syndicate"}}
faction(0)
equal(C.unavailable(syndicateRow), "wrongFaction", "a row of the Syndicate: not offered with no faction")
faction(1)
equal(C.unavailable(syndicateRow), nil, "a row of the Syndicate: offered to the Syndicate")
faction(3)
equal(C.unavailable(syndicateRow), "wrongFaction", "a row of the Syndicate: not offered to the Covenant")
local recruitment = Swc.quests.D().objectives["factionintro_syndicate_recruitment"]
check(recruitment ~= nil, "quests.json has FactionIntro_Syndicate_Recruitment")
equal(C.faction_mask("!Syndicate"), 0xD, "!Syndicate: the mask 0xD (bit 0 bypasses the test)")
bits("FactionIntro_Syndicate_03")
faction(0)
equal(C.unavailable(recruitment), nil, "the recruitment of the Syndicate: offered with no faction")
faction(1)
equal(C.unavailable(recruitment), nil, "the recruitment of the Syndicate: offered to the Syndicate too (the mask bypass)")
faction(2)
equal(C.unavailable(recruitment), nil, "the recruitment of the Syndicate: offered to the Marauders")

-- 5. RequiredFactionCooldown: Astrid with the recruitment achievement of the Syndicate and a Marauder character.
bits("FactionIntro_Syndicate_03", "FactionIntro_Syndicate_Recruitment")
faction(2)
equal(state_of("9826_Astrid"), "9826_RepAstrid_01_b", "no wait (a join, the time 0): the state 01_b")
faction(2, 0)
equal(state_of("9826_Astrid"), "9826_RepAstrid_CooldownMultiple_b", "a change now (30 days left): CooldownMultiple_b")
faction(2, WAIT - 25 * HOUR - 60)
equal(state_of("9826_Astrid"), "9826_RepAstrid_CooldownMultiple_b", "25 h 1 min left (25 whole hours): CooldownMultiple_b")
faction(2, WAIT - 25 * HOUR + 60)
equal(state_of("9826_Astrid"), "9826_RepAstrid_CooldownSingle_b", "24 h 59 min left (24 whole hours): CooldownSingle_b")
faction(2, WAIT - 10 * HOUR - 60)
equal(state_of("9826_Astrid"), "9826_RepAstrid_CooldownSingle_b", "10 h left: CooldownSingle_b")
faction(2, WAIT - 1800)
equal(state_of("9826_Astrid"), "9826_RepAstrid_01_b", "30 min left (0 whole hours): no cooldown state, 01_b")
faction(1)
equal(state_of("9826_Astrid"), "9826_RepAstrid_02", "the Syndicate: the state 02 (RequiredFaction Syndicate; 03 needs the level 60)")

-- 6. A change of the faction or of the ranks marks the open Talk and the NPC states for a new selection.
Swc.HostConversation("open", "person:astrid", "", "", "Astrid", "", "", "9826_Astrid")
frames(1.0)
equal(C.stateId, "9826_RepAstrid_02", "the open Talk of a Syndicate character: the state 02")
faction(2)
frames(0.5)
equal(C.stateId, "9826_RepAstrid_01_b", "a faction change selects the state of the open Talk again")
Swc.HostConversation("close")
frames(0.5)
while Swc.TakeHostRequest() ~= "" do end

equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
