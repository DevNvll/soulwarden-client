-- The town board fixture (C-12 of ut567; QG6a): the original TownProjects screen (townprojects.luac, townproject_activeproject.luac,
-- townprojects_availabletask.luac, territorydatahandler.luac) with the data of swc/boards.lua: the board states of the backend encoders
-- (the TerritoryInterface project CommunityProjectId, the LandClaimManager summary: AlwaysAvailable type 6, state 0 Active), the town
-- offers of the C++ generator, the accept 3819, the tracker of the accepted mission, CanCompleteMission, and the hand-in 2461.
local HUD, TOWN = 2702338936, 640726528
local town = screen("LyShineUI/Territory/TerritoryPlanning/TownProjects.uicanvas", 20)
screen("LyShineUI/Notifications/Notification.uicanvas", 50)
screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 55, true)   -- the card hover calls the flyout menu (a screen of the client)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(20, 0)
-- The place: the Claim landmark of Windsward (swc/worldmap.lua sets the territory of the position: ClaimKey 6).
Swc.HostView(-45, 8928, 3289, 70)
frames(0.3)
local B = Swc.boards
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey"), 6, "the place is in Windsward")
equal(Swc.HostBoardTerritory(), "6", "the territory of the board screen")

local function crc(text) return swcui.crc32(string.lower(text)) end
local COMMUNITY = crc("CommunityProjectId")
local function requests()
  local out = {}
  for _ = 1, 8 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end

-- The offers: one town mission of each goal of the always available project (the C++ generator gives one of each bucket).
local picks, byGoal = {}, {}
for missionCrc, row in pairs(B.D().missions) do
  local c = row.cells or {}
  if row.table == "town" and c.TerritoryProjectId == "CommunityProjectId" and (c.RequiredTradeskill or "") == "" and not byGoal[c.MissionGoalType]
     and (c.AvailableTerritoryID or "") == "" then
    byGoal[c.MissionGoalType] = missionCrc
  end
end
for _, goal in ipairs({"Kill", "Gather", "Explore"}) do
  check(byGoal[goal] ~= nil, "a town mission of the goal " .. goal .. " of CommunityProjectId")
  picks[#picks + 1] = byGoal[goal]
end
local KILL = byGoal.Kill
local claimActor = B.creation_params and MapComponentBus.Broadcast.GetClaims and (function()
  for _, row in ipairs(MapComponentBus.Broadcast.GetClaims()) do if row.settlementId == 6 then return row.id end end
end)()
equal(claimActor, "4B657E1A68E06C9F0000000000000000", "the provider identity: the Claim landmark of Windsward")

-- The states of the board (UI/SwcUiBoards.cpp lines) and the offers.
Swc.HostBoardStates(string.format("P 0 %d 0 0\nC 6 0\nA %d 6 0\n", COMMUNITY, COMMUNITY))
Swc.HostBoardOffers(string.format("S 123456 1500\nO %d 4 %d 1\nO %d 1 %d 1\nO %d 5 %d 0\n", picks[1], COMMUNITY, picks[2], COMMUNITY,
  picks[3], COMMUNITY))
local goals = ObjectiveInteractorRequestBus.Broadcast.GetCurrentCommunityGoals()
equal(#goals, 3, "three offers")
equal(goals[1].missionId, KILL, "the first offer is the Kill mission")
equal(goals[1].destinationOverride, claimActor, "the destinationOverride of an offer is the provider identity")
check(goals[1].available and not goals[3].available, "the available flag of the offers (the third is unavailable)")
check(ObjectiveInteractorRequestBus.Broadcast.GetSecondsToNextSeed() <= 1500 and ObjectiveInteractorRequestBus.Broadcast.GetSecondsToNextSeed() >= 1498,
  "the seconds to the next seed")
local data = ObjectivesDataManagerBus.Broadcast.GetMissionData(KILL)
equal(data.id, KILL, "GetMissionData: the mission record")
check(data.objectiveId ~= 0 and data.projectId == COMMUNITY, "GetMissionData: the objective and the project")
check(data:IsAvailableInTerritory(6), "GetMissionData: available in Windsward")
local project = TerritoryGovernanceRequestBus.Broadcast.GetTerritoryProgressionData(COMMUNITY)
equal(project.projectType, eTerritoryUpgradeType_AlwaysAvailable, "the project type AlwaysAvailable")
equal(#LandClaimRequestBus.Broadcast.GetTerritoryProgressionData(6).activeProjects, 1, "the claim summary has one active project")

-- The open of the client (UI/SwcUiBoards: the board use of action 51).
local errorsBefore = swcui.error_count()
equal(Swc.HostBoard("open", "07000100076400850000000000000000"), "ok", "the open of the town screen")
frames(2.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), TOWN, "the open sets the state TownProjects")
check(swcui.canvas_get(town, "enabled"), "the town screen shows")
picture("town-open")
-- The project grid (simplegriditemlist.luac, 744 px items in a 1534 px grid: 2 for each row). The dynamic scroll box makes its first copies
-- at its own InGamePostActivate, before the OnInit of the grid (static read ut567/qgplan/gridclone): each shown row has 2 project columns.
do
  local content = find(find(swcui.canvas_get(town, "root"), "SimpleDynamicGrid"), "Content")
  local shown, columns = 0, {}
  for _, row in ipairs(UiElementBus.Event.GetChildren(content) or {}) do
    if UiElementBus.Event.IsEnabled(row) then
      shown = shown + 1
      local n = 0
      for _, child in ipairs(UiElementBus.Event.GetChildren(row) or {}) do
        n = n + ((swcui.name(child) == "ActiveProjectPrototype") and 1 or 0)
      end
      columns[#columns + 1] = n
    end
  end
  equal(shown, 2, "two rows of projects are shown (4 projects: 3 project types with no project and the always available project)")
  equal(table.concat(columns, " "), "2 2", "each shown row has 2 project columns")
end
equal(swcui.error_count() - errorsBefore, 0, "no Lua error at the open")

-- The cards of the always available project (the grid of the screen; the enabled TownProject_AvailableTask elements of its last row).
local function town_cards()
  local cards = {}
  local function walk(id)
    for _, child in ipairs(UiElementBus.Event.GetChildren(id) or {}) do
      if (swcui.name(child) or ""):find("^TownProject_AvailableTask") and UiElementBus.Event.IsEnabled(child) then
        local x, y = point_of(child)
        cards[#cards + 1] = {id = child, x = x, y = y}
      end
      walk(child)
    end
  end
  walk(swcui.canvas_get(town, "root"))
  table.sort(cards, function(a, b) return a.y > b.y or (a.y == b.y and a.x < b.x) end)
  return cards
end
local cards = town_cards()
check(#cards >= 3, "the cards of the offers: " .. #cards)
-- The hover of a card shows its rewards and "Start Mission" (townprojects_availabletask.luac); the click accepts it (OnStartMission ->
-- townproject_activeproject.luac OnTaskStart: RequestCommunityGoalSelection(missionId, the displayed project)): 3819.
requests()
move_to(cards[1].id)
frames(1.0)
picture("town-hover")
click(cards[1].id, 0.5, 0.9)
frames(0.5)
local r = requests()
local accepted = r[1] and tonumber(r[1]:match("^board accept (%d+) " .. COMMUNITY .. "$"))
check(accepted == picks[1] or accepted == picks[2] or accepted == picks[3], "the click of Start Mission sends the accept of an offer (3819): "
  .. tostring(r[1]))
note("the first card accepts the mission " .. tostring(accepted) .. " (Kill " .. KILL .. ")")
-- The answer of the backend: the Objectives state with the accepted mission (CM_bounty_kill_X_enemies: the kill task 0 and the return
-- task 1 in the container 2), the mission fields of 2a: the mission, the destinationOverride (the provider identity), the community flag.
local OBJECTIVE = crc("CM_bounty_kill_X_enemies")
local function accepted_state(killed, returned)
  Swc.HostQuests(string.format("A 777 %d 6 0 %d %s 1\nT 777 0 0 %d %d\nT 777 1 0 0 %d\nT 777 2 0 %d 1\nK 777\n", OBJECTIVE, KILL,
    claimActor, killed and 1 or 0, killed and 2 or 1, returned and 2 or (killed and 1 or 0), killed and 1 or 0))
end
accepted_state(false, false)
frames(0.5)
local ids = ObjectivesComponentRequestBus.Event.GetObjectives(Swc.ids.player)
local mine
for _, id in ipairs(ids) do if id.key == "777" then mine = id end end
check(mine ~= nil, "the accepted mission is an objective of the character")
local params = mine and ObjectiveRequestBus.Event.GetCreationParams(mine) or {}
equal(params.missionId, KILL, "GetCreationParams: the mission")
equal(params.destinationOverride, claimActor, "GetCreationParams: the destinationOverride")
check(params.isCommunityGoal == true, "GetCreationParams: a community goal")
check(not ObjectiveInteractorRequestBus.Broadcast.CanCompleteMission(mine), "CanCompleteMission: false before the kill")
-- The kill task complete: the container has the progress 1 (its child count minus 1), the return task is pending: ready to hand in.
accepted_state(true, false)
frames(0.5)
check(ObjectiveInteractorRequestBus.Broadcast.CanCompleteMission(mine), "CanCompleteMission: true after the kill, at the provider")
-- The hand-in of retail (qg6a shots I5): the banner "You have 1 completed mission(s)" with Complete All Missions, then its popup with Accept
-- (townprojects.luac OnUpdateGlobalStorage -> ContractsDataHandler RequestStorageData -> OnCompleteAllMissionsButton; Accept:
-- OnCompleteAllMissionsConfirm -> RequestMissionCompletion(objectiveInstanceId)): 2461.
do
  local root = swcui.canvas_get(town, "root")
  Swc.SetData("Hud.Objective.OnActiveObjectiveUpdate", true)   -- the refresh signal of the objectives (townprojects.luac <0.2.2>)
  frames(1.0)
  local banner = find(root, "CompleteAllContainer")
  check(banner ~= nil and UiElementBus.Event.IsEnabled(banner), "the banner of the completed missions shows")
  local button = banner and find(banner, "CompleteAllButton")
  if button then note(string.format("Complete All Missions at %.4f %.4f", point_of(button) / 1920, select(2, point_of(button)) / 1080)) end
  requests()
  if button then
    click(button)
    frames(1.5)
  end
  local popup = find(root, "CompleteAllMissionsPopup")
  check(popup ~= nil and UiElementBus.Event.IsEnabled(popup), "Complete All Missions opens its popup")
  picture("town-complete-all")
  local accept = popup and find(popup, "CompleteAllButton")
  if accept then note(string.format("Accept at %.4f %.4f", point_of(accept) / 1920, select(2, point_of(accept)) / 1080)) end
  if accept then
    click(accept)
    frames(1.0)
  end
  r = requests()
  equal(r[1], "board complete 777", "Accept of the popup sends the hand-in of the ready mission (2461: the instance)")
end
requests()
ObjectiveInteractorRequestBus.Broadcast.RequestMissionCompletion(mine)
r = requests()
equal(r[1], "board complete 777", "the hand-in request (2461: the instance)")
-- After the hand-in the backend removes the mission and its town entry makes it unavailable in this seed: the client sends the offers with
-- the available flag 0, and the open screen shows "A new mission will be available soon" in its place (retail qg6a I5-board-after-hand-in).
Swc.HostQuests("")
frames(1.0)   -- the live order of window 2b: the objective goes first (the screen refreshes), the new provider state after it
Swc.HostBoardOffers(string.format("S 123456 1400\nO %d 4 %d 0\nO %d 1 %d 1\nO %d 5 %d 0\n", picks[1], COMMUNITY, picks[2], COMMUNITY,
  picks[3], COMMUNITY))
frames(1.0)
do
  local unavailable = 0
  for _, c in ipairs(town_cards()) do
    local root = c.id
    local function walk(id)
      for _, child in ipairs(UiElementBus.Event.GetChildren(id) or {}) do
        if swcui.enabled_in_hierarchy(child) and (UiTextBus.Event.GetText(child) or ""):find("new mission will be", 1, true) then
          unavailable = unavailable + 1
        end
        walk(child)
      end
    end
    walk(root)
  end
  equal(unavailable, 2, "after the hand-in the open screen shows two unavailable cards (the handed-in mission and the third offer)")
end
picture("town-after-hand-in")
accepted_state(true, false)
frames(0.5)
-- At another provider (the Claim of another territory) the mission is not ready to hand in.
Swc.HostView(-45, 8461, 4877, 180)
frames(0.3)
check(not ObjectiveInteractorRequestBus.Broadcast.CanCompleteMission(mine), "CanCompleteMission: false at the board of another territory")
Swc.HostView(-45, 8928, 3289, 70)
frames(0.3)
picture("town-accepted")
equal(swcui.error_count() - errorsBefore, 0, "no Lua error after the accept")

-- Escape: the screen closes and the board use ends one time.
requests()
key("escape")
frames(1.0)
r = requests()
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= TOWN, "Escape closes the screen")
equal(r[1], "board end", "the request is the end of the board use")
Swc.HostBoard("close")
frames(0.3)
