-- The mission boards (C-12 of ut567; QG6a: the town board; docs/UI.md "The mission boards"). The static read of the scripts and the native
-- code: ~/Games/soulwarden-client/work/ut567/qgplan/boards/ANSWERS.md (all [S] unless marked). The client (UI/SwcUiBoards.cpp) opens the
-- town screen TownProjects (state 640726528) when the board use of action 51 starts, and gives the data of the screen:
--   Swc.HostBoardTerritory()   the territory of the screen: Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey
--   Swc.HostBoardOffers(text)  the town offers of the C++ generator (Boards/SwcTownOffers, the parity with the backend generator):
--                              "S seed seconds" and "O mission bucket project available" lines
--   Swc.HostBoardStates(text)  the board projects (TerritoryInterface 1746) "P key project secondsLeft progress" and the claims
--                              (LandClaimManager 3086) "C key level" with its "U category level" and "A project type state" lines
--   Swc.HostBoard(what, actor) "open" (the actor of the board, 32 hex digits) or "close"
-- The requests: "board end" (RequestCancelCommittedInteraction of the screen), "board accept MISSION PROJECT" (3819), "board complete
-- INSTANCE" (2461).
-- The mission data: gamedata/missions.json (tools/convert-missions.sh: the mission rows of both tables with their cells, the projects).

local B = {offers = {}, seed = 0, seconds = 0, secondsAt = 0, projects = {}, claims = {}, open = false}
Swc.boards = B
local Q = Swc.quests

local TOWN_STATE = 640726528
local TOWN_CANVAS = "LyShineUI/Territory/TerritoryPlanning/TownProjects.uicanvas"
-- The EntityId of the board in the data layer (Hud.TerritoryGovernance.EntityId: the bus target of TerritoryInterfaceComponentRequestBus).
B.entity = EntityId("6005173470066573600")

local function crc(text)
  return swcui.crc32(string.lower(text or ""))
end

local function D()
  if B.data then
    return B.data
  end
  local doc = swcui.load_json("gamedata/missions.json") or {}
  local d = {missions = {}, projects = {}, projectList = {}}
  for key, row in pairs(doc.missions or {}) do
    d.missions[tonumber(key)] = row
  end
  for _, p in ipairs(doc.projects or {}) do
    d.projects[p.crc] = p
    d.projectList[#d.projectList + 1] = p.crc
  end
  B.doc_events = doc.events or {}
  B.doc_modifiers = doc.rewardModifiers or {}
  B.data = d
  return d
end
B.D = D

local function number(text)
  return tonumber(text or "") or 0
end

-- ------------------------------------------------------------------------------------------------------------- the mission records

-- StaticMissionData (ANSWERS.md section 1; GetMissionData 0x4468E00): the cells of the mission row. The strings and the vectors are
-- usable values also when empty (the scripts call string.len, #, ToSeconds without a nil guard).
local missionRecords = {}
local function mission_record(missionCrc)
  local cached = missionRecords[missionCrc]
  if cached then
    return cached
  end
  local row = D().missions[missionCrc]
  if not row then
    return nil
  end
  local c = row.cells or {}
  local territories, exclude = {}, false
  for id in string.gmatch(c.AvailableTerritoryID or c.AvailableTerritoryId or "", "[^,%s]+") do
    if id:sub(1, 1) == "!" then
      exclude = true
      id = id:sub(2)
    end
    territories[#territories + 1] = tonumber(id)
  end
  local poi = {}
  for tag in string.gmatch(c.POITagOverride or "", "[^,%s]+") do
    poi[#poi + 1] = crc(tag)
  end
  local goal = c.MissionGoalType or ""
  local rec = {
    id = missionCrc, objectiveId = row.objectiveCrc or crc(row.objective), projectId = c.TerritoryProjectId and c.TerritoryProjectId ~= "" and crc(c.TerritoryProjectId) or 0,
    categoricalProgressionId = c.CategoricalProgressionId and c.CategoricalProgressionId ~= "" and crc(c.CategoricalProgressionId) or 0,
    missionTier = number(c.MissionTier),
    taskKillContributionOverride = c.TaskKillContributionOverride or "", taskKillContributionQtyOverride = number(c.TaskKillContributionQtyOverride),
    taskGiveItemOverride = c.TaskGiveAndTakeItemOverride or "", taskHaveItemsOverride = c.TaskHaveAndReturnItemsOverride or "",
    taskHaveItemsQtyOverride = number(c.TaskHaveAndReturnItemsQtyOverride),
    taskTimerOverride = Swc.Duration.new({seconds = number(c.TaskTimerOverride)}),
    territoryIdOverride = number(c.TerritoryIDOverride), poiTagsOverride = poi,
    successGameEventIdOverride = c.SuccessGameEventIdOverride and c.SuccessGameEventIdOverride ~= "" and crc(c.SuccessGameEventIdOverride) or 0,
    failureGameEventIdOverride = c.FailureGameEventIdOverride and c.FailureGameEventIdOverride ~= "" and crc(c.FailureGameEventIdOverride) or 0,
    titleOverride = c.TitleOverride or "", descriptionOverride = c.DescriptionOverride or "", imagePath = c.ImagePath or "",
    detailImagePath = c.DetailImagePath or "", communityGoalProgressAmount = number(c.CommunityGoalAmount),
    missionGoalType = goal ~= "" and _G["eMissionGoalType_" .. goal] or 0,
    estimatedTimeToCompleteMinutes = number(c.EstimatedCompletionTimeMinutes), recommendedGroupSize = number(c.RecommendedGroupSize),
    vcLevel = number(c.VCLevel), isValid = true, isCustomObjective = false, isPvpMission = c.IsPvpMission == "1" or c.IsPvpMission == "true",
    forceReturnToGiver = c.ReturnToGiverOverride == "1" or c.ReturnToGiverOverride == "true",
  }
  rec.IsAvailableInTerritory = function(_, territory)
    if #territories == 0 then
      return true
    end
    local listed = false
    for _, id in ipairs(territories) do
      listed = listed or id == territory
    end
    return listed ~= exclude
  end
  missionRecords[missionCrc] = rec
  return rec
end
B.mission_record = mission_record

local noMissionData = Swc.Bus("ObjectivesDataManagerBus").Broadcast.GetMissionData
Swc.Define("ObjectivesDataManagerBus", "Broadcast", {
  GetMissionData = function(missionCrc)
    return (missionCrc and missionCrc ~= 0 and mission_record(missionCrc)) or (noMissionData and noMissionData(missionCrc)) or Q.noMission
  end,
})
Swc.Define("MissionDataManagerBus", "Broadcast", {
  GetMissionDifficultyLevel = function() return 0 end,
})

-- ---------------------------------------------------------------------------------------------------------------- the project data

-- The static project record (TerritoryGovernanceRequestBus GetTerritoryProgressionData; ANSWERS.md "Project records"): the cells of the
-- project row of the backend copy. The enums: eTerritoryUpgradeType_<ProjectType>, eSettlementProgressionCategory_<ProgressionCategory>.
local projectRecords = {}
local function project_record(projectCrc)
  local cached = projectRecords[projectCrc]
  if cached then
    return cached
  end
  local p = D().projects[projectCrc]
  if not p then
    return nil
  end
  local c = p.cells or {}
  local rec = {
    id = projectCrc, icon = c.Icon or "", image = c.Image or "", projectType = _G["eTerritoryUpgradeType_" .. (c.ProjectType or p.type or "")],
    currentTier = number(c.CurrentTier), title = c.Title or "", buttonLabel = c.ButtonLabel or "", description = c.Description or "",
    totalCompletionTime = number(c.CompletionTimeMinutes), cost = number(c.Cost),
    nextLevelProjectId = c.NextLevelProjectId and c.NextLevelProjectId ~= "" and crc(c.NextLevelProjectId) or 0,
    progressionCategory = _G["eSettlementProgressionCategory_" .. (c.ProgressionCategory or "None")],
    categoryDisplayName = c.ProgressionCategoryName or "", progressionLevel = number(c.ProgressionLevel),
    lifestyleBuffEffectId = c.LifestyleBuffEffectId and c.LifestyleBuffEffectId ~= "" and crc(c.LifestyleBuffEffectId) or 0,
    lifestyleBuffEffectDuration = number(c.LifestyleBuffEffectDuration), azothDiscountPercent = number(c.CraftingAzothDiscount),
    progressionNeeded = math.max(1, number(c.TerritoryProgressionNeeded)),
  }
  projectRecords[projectCrc] = rec
  return rec
end

local function claim_of(key)
  return B.claims[tonumber(key) or -1] or {level = 0, upgrades = {}, active = {}}
end

Swc.Define("TerritoryGovernanceRequestBus", "Broadcast", {
  GetTerritoryProgressionProjectIds = function() return D().projectList end,
  GetTerritoryProgressionData = function(projectCrc) return project_record(projectCrc) end,
  GetTerritoryLevel = function(territory) return claim_of(territory).level end,
})

-- The claim progression summary (LandClaimRequestBus GetTerritoryProgressionData(ClaimKey)): activeProjects {activeProjectId, activeState,
-- progressionType} and territoryUpgrades {category} of the LandClaimManager state (field 15). The state 0 of the backend is Active.
Swc.Define("LandClaimRequestBus", "Broadcast", {
  GetTerritoryProgressionData = function(key)
    local claim = claim_of(key)
    local active, upgrades = {}, {}
    for _, a in ipairs(claim.active) do
      active[#active + 1] = {activeProjectId = a.project, activeState = a.state == 0 and eSettlementProgressionState_Active or a.state,
                             progressionType = a.type}
    end
    for _, u in ipairs(claim.upgrades) do
      upgrades[#upgrades + 1] = {category = u.category, level = u.level}
    end
    return {activeProjects = active, territoryUpgrades = upgrades}
  end,
})

-- The board progression record (TerritoryInterfaceComponentRequestBus GetTerritoryProgressionData(B)): the active projects of the board
-- (TerritoryInterface state 1746, group 1 field 0); no completed upgrade, no status effect (the backend has no town project state).
Swc.Define("TerritoryInterfaceComponentRequestBus", "Event", {
  GetTerritoryProgressionData = function()
    return {completedTerritoryUpgrades = {}, territoryStatusEffects = {},
      GetActiveProjectById = function(_, projectCrc)
        for _, p in ipairs(B.projects) do
          if p.project == projectCrc then
            local now = WallClockTimePoint:Now():GetTimeSinceEpoc():ToSeconds()
            return {currentProgress = p.progress, timeProjectEnds = Swc.WallClockTimePoint.new({seconds = now + p.secondsLeft})}
          end
        end
        return {currentProgress = 0, timeProjectEnds = WallClockTimePoint:Now()}
      end}
  end,
})

-- ------------------------------------------------------------------------------------------------------------------ the offers

--- @return The provider identity of the screen: the Claim landmark actor of the territory (the claim row id; native open 0x495941E).
local function provider_of(territory)
  for _, row in ipairs(MapComponentBus.Broadcast.GetClaims()) do
    if row.settlementId == territory then
      return row.id
    end
  end
  return ""
end

local function territory_now()
  return tonumber(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey")) or 0
end

local function creation_params(fields)
  local p = {instanceId = fields.instanceId or Q.objective_id("0"), missionId = fields.missionId or 0,
             destinationOverride = fields.destinationOverride or "", originTerritoryId = fields.originTerritoryId or 0,
             available = fields.available ~= false, isCommunityGoal = fields.isCommunityGoal == true,
             isDynamicPoiObjective = false, objectiveId = fields.objectiveId or 0}
  p.Clone = function(self)
    local copy = {}
    for k, v in pairs(self) do
      copy[k] = v
    end
    return copy
  end
  return p
end
B.creation_params = creation_params

function Swc.HostBoardTerritory()
  return tostring(territory_now())
end

function Swc.HostBoardOffers(text)
  local offers, seed, seconds = {}, B.seed, B.seconds
  local provider = provider_of(territory_now())
  for line in string.gmatch(text or "", "[^\n]+") do
    local s, left = line:match("^S (%d+) (%d+)$")
    if s then
      seed, seconds = tonumber(s), tonumber(left)
    end
    local mission, bucket, project, available = line:match("^O (%d+) (%d+) (%d+) ([01])$")
    if mission then
      local m = tonumber(mission)
      offers[#offers + 1] = creation_params({missionId = m, destinationOverride = provider, available = available == "1",
        objectiveId = (D().missions[m] or {}).objectiveCrc or 0, instanceId = Q.objective_id("offer" .. #offers)})
      offers[#offers].bucket, offers[#offers].project = tonumber(bucket), tonumber(project)
    end
  end
  -- A new list: a new seed, other missions, or another available flag (a hand-in makes its mission unavailable in the seed: retail qg6a
  -- I5-board-after-hand-in, "A new mission will be available soon"; window 2b of ut567 showed the old card before this rule).
  local function key_of(list)
    local parts = {}
    for _, o in ipairs(list) do
      parts[#parts + 1] = tostring(o.missionId) .. (o.available and "+" or "-")
    end
    return table.concat(parts, " ")
  end
  local changed = seed ~= B.seed or key_of(offers) ~= key_of(B.offers)
  B.offers, B.seed, B.seconds, B.secondsAt = offers, seed, seconds, swcui.clock()
  if changed then
    -- The native sets the node on a new list (ANSWERS.md section 1); the screen reads it at its transition-in. An open screen refreshes at
    -- the signal of the objectives (townprojects.luac <0.2.2>: a data callback, each set).
    Swc.SetData("Hud.TerritoryGovernance.ReloadProjectMissions", true)
    if B.open then
      Swc.SetData("Hud.Objective.OnActiveObjectiveUpdate", true)
    end
  end
  Swc.Note("boards: " .. #offers .. " town offer(s), seed " .. tostring(seed) .. ", " .. tostring(seconds) .. " s to the next seed")
end

function Swc.HostBoardStates(text)
  local projects, claims, claim = {}, {}, nil
  for line in string.gmatch(text or "", "[^\n]+") do
    local kind, rest = line:match("^(%a) (.*)$")
    if kind == "P" then
      local key, project, left, progress = rest:match("^(%d+) (%d+) (%d+) (%d+)$")
      projects[#projects + 1] = {key = tonumber(key), project = tonumber(project), secondsLeft = tonumber(left), progress = tonumber(progress)}
    elseif kind == "C" then
      local key, level = rest:match("^(%d+) (%d+)$")
      claim = {level = tonumber(level), upgrades = {}, active = {}}
      claims[tonumber(key)] = claim
    elseif kind == "U" and claim then
      local category, level = rest:match("^(%d+) (%d+)$")
      claim.upgrades[#claim.upgrades + 1] = {category = tonumber(category), level = tonumber(level)}
    elseif kind == "A" and claim then
      local project, type, state = rest:match("^(%d+) (%d+) (%d+)$")
      claim.active[#claim.active + 1] = {project = tonumber(project), type = tonumber(type), state = tonumber(state)}
    end
  end
  B.projects, B.claims = projects, claims
  if B.open then
    Swc.Notify("TerritoryInterfaceComponentNotificationsBus", B.entity, "OnReceivedTerritoryProgressionData", B.entity)
  end
end

--- @return The quest record of an accepted mission instance (an ObjectiveInstanceId or its key).
local function accepted(instance)
  local key = (type(instance) == "table" or type(instance) == "userdata") and instance.key or tostring(instance or "")
  local rec = Q.byKey[key]
  return rec and rec.mission and rec.mission ~= 0 and rec or nil
end

--- CanCompleteMission (0x4954180; ANSWERS.md section 4): the root container has the progress of its child count minus 1, and its last
--- child (the return task) is still pending with the destination of the current provider.
local function can_complete(instance)
  local rec = accepted(instance)
  if not rec or not rec.root then
    return false
  end
  local root = rec.nodes[rec.root]
  local children = root and root.children or {}
  local state = rec.states[rec.root]
  if #children == 0 or not state or state.progress ~= #children - 1 then
    return false
  end
  local last = rec.states[children[#children]]
  local pending = not last or (last.state ~= (eObjectiveTaskState_Complete or 2) and last.state ~= (eObjectiveTaskState_CompleteActive or 3))
  return pending and (rec.destination or "") == provider_of(territory_now())
end
B.can_complete = can_complete

Swc.Define("ObjectiveInteractorRequestBus", "Broadcast", {
  GetCurrentCommunityGoals = function() return B.offers end,
  GetSecondsToNextSeed = function()
    return math.max(0, math.floor(B.seconds - (swcui.clock() - B.secondsAt)))
  end,
  CanCompleteMission = function(instance) return can_complete(instance) end,
  RequestCommunityGoalSelection = function(missionCrc, projectCrc)
    if missionCrc and missionCrc ~= 0 then
      Swc.hostRequests[#Swc.hostRequests + 1] = "board accept " .. tostring(missionCrc) .. " " .. tostring(projectCrc or 0)
      Swc.Note("boards: accept the mission " .. tostring(missionCrc) .. " (project " .. tostring(projectCrc or 0) .. ")")
    end
  end,
  RequestMissionCompletion = function(instance)
    local rec = accepted(instance)
    if rec then
      Swc.hostRequests[#Swc.hostRequests + 1] = "board complete " .. rec.key
      Swc.Note("boards: hand in the mission instance " .. rec.key)
    end
  end,
})

-- The current mission of the objectives component (the detail popup of the screen): the first accepted mission that is not a town card.
local function current_mission()
  for _, rec in ipairs(Q.list or {}) do
    if rec.mission and rec.mission ~= 0 and not rec.community then
      return rec
    end
  end
  return nil
end
Swc.Define("ObjectivesComponentRequestBus", "Event", {
  GetCurrentMissionId = function()
    local rec = current_mission()
    return rec and rec.mission or 0
  end,
  GetCurrentMissionObjectiveId = function()
    local rec = current_mission()
    return Q.objective_id(rec and rec.key or "0")
  end,
  AbandonCurrentMission = function()
    local rec = current_mission()
    if rec then
      Swc.hostRequests[#Swc.hostRequests + 1] = "quest abandon " .. rec.key
    end
  end,
  -- [I] The capacity of a type: the backend refuses an accept over its limit (the scripts send the selection after the notice too).
  HasRoomForObjectiveType = function() return true end,
})

-- The creation parameters of an accepted objective with the mission fields of the Objectives state (C-12): missionId, the
-- destinationOverride (the provider identity), the community flag.
local creationParams = Swc.Bus("ObjectiveRequestBus").Event.GetCreationParams
Swc.Define("ObjectiveRequestBus", "Event", {
  GetCreationParams = function(id)
    local base = creationParams and creationParams(id) or {}
    local rec = id and Q.byKey[id.key]
    if not rec then
      return base
    end
    return creation_params({instanceId = id, missionId = rec.mission or 0, destinationOverride = rec.destination or "",
      originTerritoryId = rec.territory or 0, isCommunityGoal = rec.community == true, objectiveId = rec.crc})
  end,
  GetClosestPOIPosition = function() return Vector3(0, 0, 0) end,
})

-- The mission events (missions.json "events": the success and failure events of the mission rows) after the quest events.
local otherEvents = Swc.Bus("GameEventRequestBus").Broadcast.GetGameSystemData
Swc.Define("GameEventRequestBus", "Broadcast", {
  GetGameSystemData = function(eventId)
    local d = D()
    if not d.eventsByCrc then
      d.eventsByCrc = {}
      for key, row in pairs(B.doc_events or {}) do
        d.eventsByCrc[crc(row.id or key)] = row
      end
    end
    local row = type(eventId) == "number" and d.eventsByCrc[eventId] or nil
    if row then
      return Q.event_record(row)
    end
    return otherEvents and otherEvents(eventId)
  end,
})

Swc.Define("CategoricalProgressionRequestBus", "Event", {GetStatMultiplier = function() return 1 end})
Swc.Define("GameEventRequestBus", "Broadcast", {GetFactionControlModifiers = function() return {xpModifier = 0} end})
Swc.Define("PlayerHousingClientRequestBus", "Broadcast", {GetHasPurchasedHouse = function() return false end})
Swc.Define("PlayerTutorialsRequestBus", "Broadcast", {SendUiEvent = function() end})

-- ---------------------------------------------------------------------------------------------------- the faction board (phase 2)

-- The faction board (C-12 phase 2; static read boards/ANSWERS.md section 5, qg6b FACTION.md, the backend docs/systems/mission-boards.md
-- "The faction boards"): the option 8 of a representative opens a board use of the person; its screen is OWMissionBoard (state
-- 2609973752). The offers are the explicit offers of the provider state (3829 field 2, in the order of 3816), the PvE ones (the native
-- apply 0x3A43320 splits by isPvpMission); the accepted offer stays in its place with available 0 and its instance (0x495A166).
local FACTION_STATE = 2609973752
local FACTION_CANVAS = "LyShineUI/OldWorldGuilds/OWMissionBoard.uicanvas"
local FACTION_MISSION_BONUS = 0x85D30F59
local MAX_DAILY_BONUSES = 3   -- javelin.daily-bonus-mission-count (0x616E608)
local REPUTATION = {[1] = "syndicate", [2] = "marauder", [3] = "covenant"}
B.provider = {offers = {}, seconds = 0, at = 0}
B.daily = {}

local function daily_available(modifier)
  -- GetAvailableDailyBonuses 0x31F1140: the maximum minus the used count (no lower clamp).
  return MAX_DAILY_BONUSES - (B.daily[modifier] or 0)
end

function Swc.HostBoardProvider(text)
  local offers, daily, seconds = {}, {}, 0
  for line in string.gmatch(text or "", "[^\n]+") do
    local left = line:match("^X (%d+)$")
    if left then
      seconds = tonumber(left)
    end
    local instance, mission, available, origin, destination = line:match("^F (%d+) (%d+) ([01]) (%d+) (%x+)$")
    if instance then
      local m = tonumber(mission)
      local record = mission_record(m)
      if not (record and record.isPvpMission) then
        local p = creation_params({instanceId = Q.objective_id(instance ~= "0" and instance or ("faction" .. #offers)), missionId = m,
          destinationOverride = string.upper(destination), originTerritoryId = tonumber(origin), available = available == "1",
          objectiveId = (D().missions[m] or {}).objectiveCrc or 0})
        p.accepted = instance ~= "0"
        offers[#offers + 1] = p
      end
    end
    local modifier, used = line:match("^B (%d+) (%d+)$")
    if modifier then
      daily[tonumber(modifier)] = tonumber(used)
    end
  end
  local dailyBefore = daily_available(FACTION_MISSION_BONUS)
  B.provider = {offers = offers, seconds = seconds, at = swcui.clock()}
  B.daily = daily
  Swc.Note("boards: the provider state: " .. #offers .. " faction offer(s), " .. seconds .. " s to the update, daily bonuses " ..
    daily_available(FACTION_MISSION_BONUS) .. "/" .. MAX_DAILY_BONUSES)
  if B.open and B.kind == "faction" then
    -- The native store 0x4959E60 emits ObjectiveDataReady (owmissionboard.luac connects ObjectiveInteractorNotificationBus).
    Swc.Notify("ObjectiveInteractorNotificationBus", nil, "ObjectiveDataReady")
  end
  if daily_available(FACTION_MISSION_BONUS) ~= dailyBefore then
    Swc.Notify("GameEventNotificationBus", nil, "OnDailyMissionBonusChanged")
  end
end

local function pve_index(missionCrc)
  for i, p in ipairs(B.provider.offers) do
    if p.missionId == missionCrc then
      return i - 1
    end
  end
  return nil
end

Swc.Define("ObjectiveInteractorRequestBus", "Broadcast", {
  GetCurrentMissions = function() return B.provider.offers end,
  GetCurrentPvpMissions = function() return {} end,
  ArePvpMissionsDisabledForThisProvider = function() return true end,
  CanSelectPvpMission = function() return false end,
  RequestPvpMissionSelection = function() end,
  -- The category of the provider: the reputation progression of the faction of the representative (the faction of the character: the
  -- service admits only that faction) [I: progressionIdToFactionId of FactionCommon maps it to the faction].
  GetCurrentMissionCategories = function()
    local faction = tonumber(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Faction")) or 0
    return REPUTATION[faction] and {crc(REPUTATION[faction])} or {}
  end,
  GetTimeTillNextProviderUpdate = function()
    return Swc.Duration.new({seconds = math.max(0, B.provider.seconds - (swcui.clock() - B.provider.at))})
  end,
  GetCurrentProviderPosition = function()
    return LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Position") or Vector3(0, 0, 0)
  end,
  -- RequestMissionSelectionById 0x495CE70: the zero-based index of the mission in the PvE cache (3816), not the card place.
  RequestMissionSelectionById = function(missionCrc)
    local index = pve_index(missionCrc)
    if index then
      Swc.hostRequests[#Swc.hostRequests + 1] = "board select " .. index
      Swc.Note("boards: select the faction mission " .. tostring(missionCrc) .. " (index " .. index .. ", 3816)")
    end
  end,
})

Swc.Define("ObjectivesComponentRequestBus", "Event", {
  HasMission = function(_, missionCrc)
    for _, rec in ipairs(Q.list or {}) do
      if rec.mission == missionCrc then
        return true
      end
    end
    return false
  end,
  HasObjectiveInstanceId = function(_, id)
    return id ~= nil and Q.byKey[id.key] ~= nil
  end,
})

Swc.Define("TerritoryDetectorServiceRequestBus", "Event", {
  GetDetectedTerritoryId = function() return tonumber(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey")) or 0 end,
})

-- The daily bonus (GameEvent 497; GetMaxDailyBonuses 0x31F2230, GetAvailableDailyBonuses 0x31F1140) and the reward preview
-- (GetUIMissionRewardsAndBonuses 0x31F4630): the values of the success event, with the factors of FactionMissionBonus (missions.json
-- rewardModifiers: experience 2, currency 9, tokens 4, added to the multiplier 1) while a use is left.
local function bonus_factors()
  for _, row in ipairs(B.doc_modifiers or {}) do
    if row.id == "FactionMissionBonus" then
      local c = row.cells or {}
      return 1 + number(c.Experience), 1 + number(c.Currency), 1 + number(c.FactionTokenModifier)
    end
  end
  return 1, 1, 1
end
Swc.Define("GameEventRequestBus", "Broadcast", {
  GetMaxDailyBonuses = function() return MAX_DAILY_BONUSES end,
  GetAvailableDailyBonuses = function(modifier) return daily_available(tonumber(modifier) or FACTION_MISSION_BONUS) end,
  GetUIMissionRewardsAndBonuses = function(eventCrc, origin)
    D()
    local d = B.data
    local event = GameEventRequestBus.Broadcast.GetGameSystemData(eventCrc)
    local xp, coin, standing = event and event.progressionReward or 0, tonumber(event and event.currencyRewardRange or "0") or 0, event and event.territoryStanding or 0
    local reputation, tokens = event and event.factionReputation or 0, event and event.factionTokens or 0
    local bonuses = {}
    if daily_available(FACTION_MISSION_BONUS) > 0 then
      local fx, fc, ft = bonus_factors()
      xp, coin, tokens = math.floor(xp * fx), coin * fc, math.floor(tokens * ft)
      bonuses[#bonuses + 1] = FACTION_MISSION_BONUS
    end
    local territory = tonumber(origin) or 0
    if territory == 0 then
      territory = tonumber(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey")) or 0
    end
    return {finalXp = xp, finalCurrency = coin, finalStanding = standing, finalReputation = reputation, finalTokens = tokens,
            activeBonuses = bonuses, rewardsTerritoryId = territory}
  end,
})
-- The faction shop of the screen (owguildshop.luac PopulateGuildShops): the conversion list of the faction stores. The shops are a later
-- stage (5.1): an empty list (no item of a store; with nil the script returned early and GetAvailableItemCountForCrc indexed no table).
Swc.Define("CurrencyConversionRequestBus", "Event", {GetConversionList = function() return {} end})
Swc.Define("MeshComponentRequestBus", "Event", {SetVisibility = function() end})
Swc.Define("PaperdollRequestBus", "Event", {SetWeaponVisible = function() end})

-- -------------------------------------------------------------------------------------------------------------- the screen

local function ensure_canvas(path)
  for _, screen in pairs(Swc.screens) do
    if string.lower(screen.path or "") == string.lower(path) then
      return   -- the screen is loaded
    end
  end
  Swc.LoadScreen(path, true)
  Swc.Note("boards: the screen " .. path .. " loads")
end

function Swc.HostBoard(what, actor, kind)
  if what == "open" then
    local faction = kind == "faction"
    local state = faction and FACTION_STATE or TOWN_STATE
    ensure_canvas(faction and FACTION_CANVAS or TOWN_CANVAS)
    B.open, B.actor, B.kind = true, actor or "", faction and "faction" or "town"
    Swc.SetData("Hud.TerritoryGovernance.EntityId", B.entity)
    Swc.SetData("Hud.TerritoryGovernance.ReloadProjectMissions", true)
    LyShineManagerBus.Broadcast.SetState(state)
    if LyShineManagerBus.Broadcast.GetCurrentState() ~= state then
      B.open = false
      return "the state of the " .. B.kind .. " screen did not start"
    end
    Swc.Note("boards: the " .. B.kind .. " screen opens (board " .. B.actor .. ", territory " .. territory_now() .. ")")
    return "ok"
  elseif B.open then
    B.open = false
    Swc.Note("boards: the client ended the board use: the screen closes")
    for _, state in ipairs({TOWN_STATE, FACTION_STATE}) do
      if LyShineManagerBus.Broadcast.GetCurrentState() == state then
        LyShineManagerBus.Broadcast.ExitState(state)
      end
    end
  end
end

-- The close of the screen (townprojects.luac OnTransitionOut: RequestCancelCommittedInteraction at the interactor): one "board end".
local cancelInteraction = Swc.Bus("UiInteractorComponentRequestsBus").Event.RequestCancelCommittedInteraction
Swc.Define("UiInteractorComponentRequestsBus", "Event", {
  RequestCancelCommittedInteraction = function(...)
    if B.open then
      B.open = false
      Swc.hostRequests[#Swc.hostRequests + 1] = "board end"
      Swc.Note("boards: the screen closes: the board use ends")
      return
    end
    if cancelInteraction then
      return cancelInteraction(...)
    end
  end,
})

return B
