-- The faction board (C-12 phase 2; static read ut567/qgplan/boards/ANSWERS.md section 5, qg6b FACTION.md section 1, the backend
-- docs/systems/mission-boards.md "The faction boards" and "The daily bonus"): the conversation option 8 (Open Faction Board) of Astrid
-- (9826_Astrid, the Syndicate representative) for a Syndicate character and not for a Marauder; its request "conversation board"; then the
-- original OWMissionBoard screen (state 2609973752) with the explicit offers of the provider state (3829 field 2), the select by the index
-- of the PvE list (3816), the daily bonus (GameEvent 497: 3/3, then 2/3), the reward preview with the daily factors, and the close.
local HUD, FACTION = 2702338936, 2609973752
local conversation = screen("LyShineUI/Conversation/ConversationScreenV2.uicanvas", 20)
local board = screen("LyShineUI/OldWorldGuilds/OWMissionBoard.uicanvas", 21)
screen("LyShineUI/Popup/Popup.uicanvas", 52)
screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 55, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(59, 0)   -- the level 60 of the screen
Swc.HostSocial()
Swc.HostFaction(1, "0", 0)   -- the Syndicate
Swc.HostView(-45, 8928, 3289, 70)
frames(0.3)

local C, B = Swc.conversation, Swc.boards
local FACTION_BOARD = eConversationOptionType_OpenFactionBoard or 8
local errorsBefore = swcui.error_count()
local function crc(text) return swcui.crc32(string.lower(text)) end
local function requests()
  local out = {}
  for _ = 1, 16 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local function has_type(kind)
  for _, each in ipairs(C.result and C.result.options or {}) do
    if each.optionType == kind then return true end
  end
  return false
end
local function last_page()
  for _ = 1, 10 do
    if C.index >= #C.lines then break end
    ConversationRequestBus.Broadcast.RequestConversationOption(eConversationOptionType_ContinueDialogue, 0)
    frames(1.5)
  end
end
local function open_astrid()
  Swc.HostConversation("open", "person:astrid", "", "", "Astrid", "Syndicate Representative", "", "9826_Astrid")
  frames(5.0)
  last_page()
  frames(1.0)
end

-- 1. The option 8 for a Syndicate character (the service Faction_Board of the state, RequiredFaction Syndicate).
open_astrid()
note("the state " .. tostring(C.stateId))
check(has_type(FACTION_BOARD), "a Syndicate character: the option Open Faction Board (8) of the service Faction_Board")
picture("factionboard-options")
-- 2. Its selection: ConversationServiceOpen, the end of the conversation, the board use of the person ("conversation board").
requests()
ConversationRequestBus.Broadcast.RequestConversationOption(FACTION_BOARD, 0)
frames(0.5)
local r = requests()
check(r[1] ~= nil and r[1]:find("^conversation board") ~= nil, "the option 8 sends the end of the conversation and the board use: " .. tostring(r[1]))
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.ConversationServiceOpen"), true, "ConversationServiceOpen is set")
Swc.HostConversation("close")
frames(1.0)
-- 3. A Marauder character: no option 8 (the faction of the state is not the faction of the player).
Swc.HostFaction(2, "0", 0)
frames(0.3)
open_astrid()
check(not has_type(FACTION_BOARD), "a Marauder character: no option Open Faction Board at the Syndicate representative")
requests()
Swc.HostConversation("close")
frames(1.0)
Swc.HostFaction(1, "0", 0)
frames(0.3)

-- 4. The faction board: three PvE world missions of the Syndicate (RequiredFaction, not PvP) as the explicit offers of the provider state.
local picks = {}
local ids = {}
for missionCrc in pairs(B.D().missions) do ids[#ids + 1] = missionCrc end
table.sort(ids)
for _, missionCrc in ipairs(ids) do
  local row = B.D().missions[missionCrc]
  local c = row.cells or {}
  if #picks < 3 and row.table == "world" and string.lower(c.RequiredFaction or "") == "syndicate" and c.IsPvpMission ~= "1" then
    picks[#picks + 1] = missionCrc
  end
end
equal(#picks, 3, "three Syndicate PvE world missions of the data")
local CLAIM = "4B657E1A68E06C9F0000000000000000"
local function provider(acceptedIndex, used)
  local lines = {"X 1200"}
  for i, m in ipairs(picks) do
    local accepted = i == acceptedIndex
    lines[#lines + 1] = string.format("F %s %d %d 6 %s", accepted and "1234" or "0", m, accepted and 0 or 1, CLAIM)
  end
  if used then
    lines[#lines + 1] = string.format("B %d %d", 0x85D30F59, used)
  end
  return table.concat(lines, "\n") .. "\n"
end
Swc.HostBoardProvider(provider(nil, nil))
local offers = ObjectiveInteractorRequestBus.Broadcast.GetCurrentMissions()
equal(#offers, 3, "GetCurrentMissions: the three explicit offers")
equal(offers[2].missionId, picks[2], "the offers keep the order of the provider state")
equal(#ObjectiveInteractorRequestBus.Broadcast.GetCurrentPvpMissions(), 0, "no PvP offer")
equal(GameEventRequestBus.Broadcast.GetMaxDailyBonuses(0x85D30F59), 3, "the daily bonuses: 3 at most")
equal(GameEventRequestBus.Broadcast.GetAvailableDailyBonuses(0x85D30F59), 3, "the daily bonuses: 3 available with no use")
local categories = ObjectiveInteractorRequestBus.Broadcast.GetCurrentMissionCategories()
equal(categories[1], crc("syndicate"), "the category of the provider: the reputation progression of the Syndicate")
near(ObjectiveInteractorRequestBus.Broadcast.GetTimeTillNextProviderUpdate():ToSeconds(), 1200, 2, "the time to the next provider update")

equal(Swc.HostBoard("open", "772189A58AA5E03F0000000000000000", "faction"), "ok", "the open of the faction screen")
frames(2.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), FACTION, "the open sets the state of the faction board")
check(swcui.canvas_get(board, "enabled"), "the faction board shows")
picture("factionboard-open")

-- 5. The select of the second offer: the index of the PvE list (3816).
requests()
ObjectiveInteractorRequestBus.Broadcast.RequestMissionSelectionById(picks[2])
r = requests()
equal(r[1], "board select 1", "RequestMissionSelectionById: the index 1 of the PvE list (3816)")
-- The answer: the offer stays in its place with available 0 and its instance; one daily bonus is used after a hand-in.
Swc.HostBoardProvider(provider(2, 1))
frames(1.0)
equal(GameEventRequestBus.Broadcast.GetAvailableDailyBonuses(0x85D30F59), 2, "the daily bonuses: 2 available after one use")
offers = ObjectiveInteractorRequestBus.Broadcast.GetCurrentMissions()
check(offers[2] and not offers[2].available and offers[2].instanceId.key == "1234", "the accepted offer stays in its place with its instance")
picture("factionboard-accepted")
equal(swcui.error_count() - errorsBefore, 0, "no Lua error")

-- 6. Escape: the screen closes and the board use ends.
requests()
key("escape")
frames(1.0)
r = requests()
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= FACTION, "Escape closes the faction board")
local ended = false
for _, each in ipairs(r) do ended = ended or each == "board end" end
check(ended, "the close ends the board use (board end): " .. table.concat(r, " | "))
Swc.HostBoard("close")
frames(0.3)
