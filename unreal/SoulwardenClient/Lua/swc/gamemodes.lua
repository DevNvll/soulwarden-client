-- The game modes of the character and the entrance window of a story instance (C-13 step 1b part B of ut567; the static read
-- ~/Games/soulwarden-client/work/ut567/c13read/NOTES.md; the codec part A: docs/NETWORK.md "The membership of a story instance").
-- The client (UI/SwcUiGameModes.cpp) gives:
--   Swc.HostGameModes(text)          the GameModeParticipant state (3312): "M mode map enteredSeconds" for each entry, "Q 0|1" PlayerCanQuit
--                                    (bit 22 of the flags)
--   Swc.HostDungeon(what, mode)      "open" with the mode of the entrance (the member entrance of the object: the CRC of the lower-case
--                                    GameModeId) when the use of OpenDungeonMenu (action 71) runs; "close" when it ends
--   Swc.HostDungeonResult(result)    each 3094 (OnDungeonActivateResult: eDungeonActivateResponse, 0 Success, 10, 11, 17 EntranceBusy)
-- The requests: "dungeon enter DIFFICULTY" (the Enter of the window: 72 with the base action 71), "dungeon end" (the window closes: the use
-- of 71 ends), "gamemode event EVENT" (the leave of the escape menu: 1980 with 9BB080D0).
-- The game mode data: gamedata/gamemodes.json (tools/convert-ui.sh gamemodes: javelindata_gamemodes by the CRC of the GameModeId).

local GM = {modes = {}, canQuit = false, open = false, mode = 0}
Swc.gamemodes = GM

local ENTRANCE_STATE = 4119896358
local ENTRANCE_CANVAS = "LyShineUI/Dungeons/DungeonEnterScreen.dynamicuicanvas"
local LEAVE_EVENT = 0x9BB080D0
-- The entity of the entrance of the open window (the interactable of the interactor, its root: dungeonenterscreen.luac OnTransitionIn).
GM.entity = EntityId("6005173470066573700")

local function data()
  if not GM.data then
    local doc = swcui.load_json("gamedata/gamemodes.json") or {}
    GM.data = {}
    for key, row in pairs(doc.modes or {}) do
      GM.data[tonumber(key)] = row
    end
  end
  return GM.data
end
GM.D = data

local function row_of(mode)
  return data()[tonumber(mode) or -1]
end

function Swc.HostGameModes(text)
  local modes, canQuit = {}, false
  for line in string.gmatch(text or "", "[^\n]+") do
    local mode, map, entered = line:match("^M (%d+) (%d+) (%d+)$")
    if mode then
      modes[#modes + 1] = {mode = tonumber(mode), map = tonumber(map), entered = tonumber(entered)}
    end
    local quit = line:match("^Q ([01])$")
    if quit then
      canQuit = quit == "1"
    end
  end
  GM.modes, GM.canQuit = modes, canQuit
  if #modes > 0 then
    GM.last = modes[#modes].mode
  end
  Swc.Note("gamemodes: " .. #modes .. " active mode(s)" .. (modes[1] and string.format(" (%08X)", modes[1].mode) or "") .. ", can quit " .. tostring(canQuit))
end

local function in_mode(mode)
  for _, m in ipairs(GM.modes) do
    if m.mode == mode then
      return true
    end
  end
  return false
end

local function current_dungeon()
  for _, m in ipairs(GM.modes) do
    local row = row_of(m.mode)
    if row and row.isDungeon then
      return m.mode
    end
  end
  return 0
end

local function text_of(mode, field)
  local row = row_of(mode)
  return row and row[field] or ""
end

local earlierStatic = Swc.Bus("GameModeDataManagerBus").Broadcast.GetGameModeStaticData
local function valid() return true end
local function invalid() return false end
-- GetGameModeStaticData: the row of the mode (the camel case columns of javelindata_gamemodes) and the fields of the earlier answer.
-- IsValid of the native class (expeditionscommon.luac IsSoloTrial, IsRaidTrial, GetPostFixForGameMode): true only for a mode of the data.
Swc.Define("GameModeDataManagerBus", "Broadcast", {
  GetGameModeStaticData = function(mode)
    local base = earlierStatic and earlierStatic(mode) or {}
    local out = {}
    for k, v in pairs(base) do
      out[k] = v
    end
    local row = row_of(mode)
    if not row then
      out.IsValid = invalid
      return out
    end
    out.IsValid = valid
    -- The possible rewards of the window (dungeonenterscreen.luac UpdateRewardsList): the item ids (CRC-32 of the lower-case names) of
    -- the column PossibleItemDropIds.
    out.GetPossibleItemDropIds = function()
      local ids = {}
      for name in string.gmatch(row.possibleItemDropIds or "", "[^,%s]+") do
        ids[#ids + 1] = swcui.crc32(string.lower(name))
      end
      return ids
    end
    -- The items to enter (dungeonenterscreen.luac UpdateRequirements with no mutation): javelindata_gamemodes has no column of them.
    out.GetRequiredItems = function() return {} end
    -- The struct of the matchmaking limits (dungeonenterscreen.luac UpdateRequirements: minGearScore, minLevel).
    out.matchmakingRequirements = {minGearScore = tonumber(row.matchmakingMinGS) or 0, minLevel = tonumber(row.matchmakingMinLevel) or 0,
                                   maxLevel = tonumber(row.matchmakingMaxLevel) or 0}
    for k, v in pairs(row) do
      out[k] = v
    end
    out.gameModeId = tonumber(mode)
    out.hideCompass = row.hideCompass == true
    return out
  end,
})

Swc.Define("GameModeParticipantComponentRequestBus", "Event", {
  IsInAnyGameMode = function() return #GM.modes > 0 end,
  IsInGameMode = function(_, mode) return in_mode(tonumber(mode)) end,
  GetCurrentDungeonGameModeId = function() return current_dungeon() end,
  GetLastEnteredGameMode = function() return GM.last or 0 end,
  -- PlayerCanQuit (bit 22 of the flags of the state; the leave button of the escape menu): the other flags are not set by the backend.
  IsFlagSet = function(_, flag) return flag == eGameModeParticipantFlag_PlayerCanQuit and GM.canQuit end,
  GetGameModeEntityId = function() return GM.entity end,
  GetQuitButtonTextIdForGameMode = function(_, mode) return text_of(mode, "quitButtonTextId") end,
  GetLeaveTitleTextIdForGameMode = function(_, mode) return text_of(mode, "leaveTitleTextId") end,
  GetLeaveDescTextIdForGameMode = function(_, mode) return text_of(mode, "leaveDescTextId") end,
  GetLeaveTimeDescTextIdForGameMode = function(_, mode) return text_of(mode, "leaveTimeDescTextId") end,
  GetCannotFastTravelTextIdForGameMode = function(_, mode) return text_of(mode, "cannotFastTravelTextId") end,
  -- No queue, no matchmaking, no schedule (the backend has none).
  IsInQueueForAnyGameMode = function() return false end,
  HasAcceptedMatch = function() return false end,
  IsGameModeInCategory = function() return false end,
  SetActivityScreenState = function() end,
  GetActiveGameModeQueues = function() return {} end,
  IsGameModeGroupTrial = function() return false end,
  GetActivityId = function() return 0 end,
})
-- The rejoin penalty of the leave popup (escapemenu.luac OnLeaveGameMode): none.
Swc.Define("GameModeComponentRequestBus", "Event", {GetGameModeRejoinPenaltyTimeSec = function() return 0 end})
-- The leave of the escape menu (escapemenu.luac: the Yes of the popup Popup_LeaveOutpostRush): the client event 9BB080D0 (1980).
Swc.Define("GameModeParticipantComponentRequestsBus", "Event", {
  SendClientEvent = function(_, event)
    if tonumber(event) == LEAVE_EVENT then
      Swc.hostRequests[#Swc.hostRequests + 1] = "gamemode event " .. tostring(LEAVE_EVENT)
      Swc.Note("gamemodes: the leave event 9BB080D0 (1980)")
    end
  end,
})

-- ------------------------------------------------------------------------------------------------------------- the entrance window

-- The entrance of the open window: the interactable of the interactor and its root; its mode (ArenaRequestBus GetDungeonGameModeId).
local earlierInteractable = Swc.Bus("UiInteractorComponentRequestsBus").Event.GetInteractable
Swc.Define("UiInteractorComponentRequestsBus", "Event", {
  GetInteractable = function(...)
    if GM.open then
      return GM.entity
    end
    return earlierInteractable and earlierInteractable(...) or EntityId()
  end,
})
Swc.Define("ArenaRequestBus", "Event", {
  GetDungeonGameModeId = function() return GM.mode or 0 end,
  -- A character with no group (UpdateEnterButton: allMembersInProximity, allMembersAlive).
  GetAllGroupMembersInProximity = function() return true end,
  GetAllGroupMembersAlive = function() return true end,
})
-- The counts and the times of the dungeon limits: no run counted (the backend has no limit), the refresh now.
Swc.Define("PlayerArenaRequestBus", "Event", {
  GetNumDungeonsEnteredSinceLastRefresh = function() return 0 end,
  GetNextDungeonMaxLimitRefreshTime = function() return WallClockTimePoint:Now() end,
  GetNumGroupTrialsEnteredSinceLastRefresh = function() return 0 end,
  GetNextGroupTrialMaxLimitRefreshTime = function() return WallClockTimePoint:Now() end,
  GetMutationRank = function() return 0 end,
  IsInArena = function() return false end,
})
-- No mutation (the backend has none; the window of a mode with isMutable false): no mutation set, no mutation rewards.
Swc.Define("GameModeMutationSchedulerRequests", "Broadcast", {
  GetMutationSet = function() return {elementalId = 0, promotionId = 0, curseId = 0} end,
  GetPossibleItemDropIdsForDifficulty = function() return {} end,
  GetMutationDifficultyStaticData = function() return {reqItemsToEnter = {}, matchmakingRequirements = {minGearScore = 0, minLevel = 0}} end,
})
-- The dungeon layout of the escape menu (escapemenu.luac UpdateScreen) and the other screens: in a mode of the data with isDungeon.
Swc.Define("GameRequestsBus", "Broadcast", {IsInDungeonGameMode = function() return current_dungeon() ~= 0 end})
Swc.Define("UIArenaAndDungeonRequestBus", "Broadcast", {
  -- -1: no cooldown (dungeonenterscreen.luac UpdateEnterButton: hasDungeonCooldown = time ~= -1).
  GetDungeonCooldownTime = function() return -1 end,
  GetSinglePlayerGameModeId = function() return 0 end,
})
-- The Enter of the window (dungeonenterscreen.luac TryEnterDungeon): 72 with the base action 71 of the open use.
Swc.Define("LocalPlayerUIRequestsBus", "Broadcast", {
  EnterDungeon = function(difficulty)
    if GM.open then
      Swc.hostRequests[#Swc.hostRequests + 1] = "dungeon enter " .. tostring(math.floor(tonumber(difficulty) or 0))
      Swc.Note(string.format("gamemodes: Enter of the entrance %08X (72)", GM.mode or 0))
    end
  end,
})

local function ensure_canvas()
  for _, screen in pairs(Swc.screens) do
    if string.lower(screen.name or "") == "dungeonenterscreen" then
      return
    end
  end
  Swc.LoadScreen(ENTRANCE_CANVAS, true)
  Swc.Note("gamemodes: the screen DungeonEnterScreen loads")
end

function Swc.HostDungeon(what, mode)
  if what == "open" then
    ensure_canvas()
    GM.open, GM.mode = true, tonumber(mode) or 0
    LyShineManagerBus.Broadcast.SetState(ENTRANCE_STATE)
    if LyShineManagerBus.Broadcast.GetCurrentState() ~= ENTRANCE_STATE then
      GM.open = false
      return "the state of the entrance window did not start"
    end
    Swc.Note(string.format("gamemodes: the entrance window opens (mode %08X)", GM.mode))
    return "ok"
  elseif GM.open then
    GM.open = false
    if LyShineManagerBus.Broadcast.GetCurrentState() == ENTRANCE_STATE then
      LyShineManagerBus.Broadcast.ExitState(ENTRANCE_STATE)
    end
    Swc.Note("gamemodes: the client ended the entrance use: the window closes")
  end
end

-- The result of an Enter (3094): ArenaEventBus OnDungeonActivateResult at the entrance (the window retries at EntranceBusy, else closes).
function Swc.HostDungeonResult(result)
  Swc.Note("gamemodes: the result of the entrance " .. tostring(result) .. " (3094)")
  Swc.Notify("ArenaEventBus", GM.entity, "OnDungeonActivateResult", tonumber(result) or 0)
end
Swc.Handled("ArenaEventBus")

-- The close of the window (its Escape, its back button: RequestCancelCommittedInteraction): one "dungeon end".
local cancelInteraction = Swc.Bus("UiInteractorComponentRequestsBus").Event.RequestCancelCommittedInteraction
Swc.Define("UiInteractorComponentRequestsBus", "Event", {
  RequestCancelCommittedInteraction = function(...)
    if GM.open then
      GM.open = false
      Swc.hostRequests[#Swc.hostRequests + 1] = "dungeon end"
      Swc.Note("gamemodes: the entrance window closes: the use ends")
      return
    end
    if cancelInteraction then
      return cancelInteraction(...)
    end
  end,
})

return GM
