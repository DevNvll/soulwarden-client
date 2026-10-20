-- The front end of the client (stage F2 of docs/FRONTEND.md): the native part of the original landing screen
-- (lyshineui/main menu/landingscreenv2.luac, LandingScreenV2). The scripts and the canvas are the ones of the game; this
-- module gives their native requests and completion events.
--
--   * The account (open point FE9): Swc.HostMenuAccount(authorized, entitlements) sends UiMainMenuBus OnAuthComplete and
--     EntitlementNotificationBus OnEntitlementsReady. The end of the UI load (swc/options.lua) is not an account event in the
--     front end: it goes only to the options screen.
--   * The list: UiLoginScreenRequestBus GetLoginInfoLists gives the host request "menu list"; the host sends the worlds,
--     the characters, and the reserved names (Swc.HostMenuWorld, HostMenuCharacter, HostMenuName) and then
--     Swc.HostMenuListEnd: UiLoginScreenNotificationBus OnLoginInfoListResult(worlds, characters, names, maxCharacters,
--     pendingMerges). The values are the native classes WorldMetadata, CharacterMetadata, CharacterNameData (the fields of
--     worldlistcommon.luac).
--   * The regions: GetRegionList gives OnClusterListResult(regions) in the next frame, from the regions of the channel document
--     (Swc.HostMenuRegion). The backend is on loopback: latency 0 ms.
--   * The world CMS data: DynamicContentBus RetrieveCMSData(eCMSDataType_Worlds) gives a local empty result of the shape that
--     the script reads (globalMotd.title, globalMotd.description, worldDescriptions) in the next frame, with
--     UiMainMenuBus OnWorldCMSDataSet(data) and no entity address (research frontend-native.md FE2).
--   * The menu level (FE1): javelin.frontend-level-name (native default NW_Frontend_Settlement); the schedule row
--     EventNightHaven_Frontend (2025-10-01 to 2027-10-01 UTC) gives NW_Frontend_NightHaven. The adapter keeps that choice as
--     an explicit override (data, not a proved live value). The host sends the end of the menu level load
--     (Swc.HostMenuLevelLoaded): landingscreenv2.luac queues IntroToCharacter until then.
--   * The selection: SetVisibleCharacter(id) gives the host request "menu preview ID"; the preview entity
--     (RequestCustomizableCharacterEntityId) shows or hides with TransformBus SetWorldZ: "menu show 1" or "menu show 0".
--     A refresh keeps the selection by its id (open point FE10): before the list result of a refresh, the module writes the
--     selected id to MainMenu.NewCharacterId, which SelectDefaultCharacter reads first.
--   * Play: GameRequestsBus RequestLogin(id, worldType) gives "menu login ID TYPE" (stage F5); GetConnectionStatus gives the
--     numbers of the table of landingscreenv2.luac (Swc.HostMenuConnection). RequestDisconnect gives "menu quit".
--   * The camera sequences: CinematicRequestBus PlaySequenceByName ends with OnCinematicStateChanged(name, Stopped) after the
--     serialized time range of the sequence of the NightHaven scene (docs/FRONTEND.md "Animation and sequences"; the start
--     policy is open point FE7).
-- No public service: the store, invites, Twitch, rewards, news, and surveys give explicit local results (none).

local Swc = Swc

local F = {
  menu = false,
  authorized = false,
  entitlements = false,
  -- The values of the list that the host sends (the next list result).
  building = nil,
  worlds = {},
  characters = {},
  names = {},
  regions = {},
  maxCharacters = 0,
  regionId = "",
  -- The name of the menu level (GameRequestsBus GetMainMenuLevelName): the explicit override of the schedule row
  -- EventNightHaven_Frontend (FE1).
  levelName = "NW_Frontend_NightHaven",
  connection = 0,
  selectedId = "",
  listRequests = 0,
  previewEntity = EntityId("6005173470066574337"),
  previewShown = false,
  -- The local results of the next frame: {kind, value}.
  queue = {},
  -- A test holds the local results of a kind ("cms", "regions": the completion order of the three ready flags).
  hold = {},
  held = {},
  sequences = {},
}
Swc.frontend = F

-- The pending world merges of the account (a native data call of landingscreenv2.luac and worldlistdatahandler.luac by the
-- CRC 225285239): none, as the PendingWorldMerges [] of the login information of the backend.
Swc.callValues[225285239] = function() return {} end

local function request(text)
  Swc.hostRequests[#Swc.hostRequests + 1] = text
end

-- ----------------------------------------------------------------------------------------------- the value classes

local function value_class(name, fields)
  local class = Swc.ValueClass({name = name, fields = fields})
  _G[name] = function() return class.new({}) end
  return class
end

value_class("WorldMetadata", {"status", "type", "publicStatusCode", "publicName", "worldId", "worldSet",
  "maxAccountCharacters", "name", "worldMetrics", "version", "transferToRegion", "isFull", "isRecommended"})
value_class("CharacterMetadata", {"characterId", "personaId", "worldId", "prevWorldId", "name", "currency", "position",
  "currentLevel", "crestData", "guildId", "portraitData", "publishedSource", "publishedElapsedSeconds", "ftueCompleted",
  "mustRename", "mustRenameReason", "needsTransfer", "hasTransfered", "transferReason", "modifiedDate", "nameModifiedDate",
  "needsTransferDate", "nameLatentDate", "isNameLatent", "isFreshStart"})
value_class("CharacterNameData", {"name", "personaId", "worldId", "region", "creationDate", "modifiedDate", "channel"})
value_class("RegionMetadata", {"regionId", "name", "awsRegion", "latencyMs"})

-- The guild id of a character (the script tests guildId:IsValid()): the backend has no guilds.
local GuildId = Swc.ValueClass({
  name = "GuildId",
  fields = {"text"},
  methods = {
    IsValid = function(self) return self.text ~= "" end,
    ToString = function(self) return self.text end,
  },
})

local function zero_time()
  return TimePoint()
end

-- ---------------------------------------------------------------------------------------------- the host calls

--- The account state of the login path: true when the list of the account came (the token and the entitlements).
function Swc.HostMenuAccount(authorized, entitlements)
  F.menu = true
  local wasAuthorized = F.authorized
  F.authorized = authorized == true or authorized == 1
  -- The order of the login path: the account token, then the entitlement sync.
  if F.authorized ~= wasAuthorized or not F.authorized then
    Swc.Notify("UiMainMenuBus", nil, "OnAuthComplete", F.authorized)
  end
  if (entitlements == true or entitlements == 1) and not F.entitlements then
    F.entitlements = true
    Swc.Notify("EntitlementNotificationBus", nil, "OnEntitlementsReady")
  end
end

--- One region of the channel document; `selected` (or the first region) is the region of the client.
function Swc.HostMenuRegion(regionId, name, awsRegion, selected)
  F.regions[#F.regions + 1] = {regionId = regionId, name = name, awsRegion = awsRegion, latencyMs = 0}
  if selected == 1 or selected == true or F.regionId == "" then
    F.regionId = regionId
  end
end

--- The start of a list result.
function Swc.HostMenuListBegin()
  F.building = {worlds = {}, characters = {}, names = {}}
end

local function building()
  F.building = F.building or {worlds = {}, characters = {}, names = {}}
  return F.building
end

local function flag(value)
  return value == 1 or value == true
end

--- One world of the list (the members of the login information; worldMetrics with the names of the scripts).
function Swc.HostMenuWorld(worldId, name, publicName, worldSet, status, kind, version, publicStatusCode,
                           maxAccountCharacters, isFull, isRecommended, transferToRegion, queueSize, queueWaitSeconds,
                           populationStatus)
  local b = building()
  b.worlds[#b.worlds + 1] = {
    worldId = worldId, name = name, publicName = publicName, worldSet = worldSet, status = status, type = kind,
    version = version, publicStatusCode = publicStatusCode or 0, maxAccountCharacters = maxAccountCharacters or 0,
    isFull = flag(isFull), isRecommended = flag(isRecommended), transferToRegion = transferToRegion or "",
    worldMetrics = {queueSize = queueSize or 0, queueWaitTimeSec = queueWaitSeconds or 0,
      worldPopulationStatus = populationStatus or ""},
  }
end

--- One character of the list. `currentLevel` is the member of the menu document (the screen shows it + 1); `elapsed` the
--- seconds since the last change of the character (0: not known; GetLastPlayedCharacterIndex skips 0).
function Swc.HostMenuCharacter(characterId, personaId, worldId, name, currentLevel, currency, x, y, z, elapsed, ftueCompleted,
                               mustRename, needsTransfer, isFreshStart, isNameLatent)
  local b = building()
  b.characters[#b.characters + 1] = {
    characterId = characterId, personaId = personaId, worldId = worldId, prevWorldId = "", name = name,
    currency = currency or 0, position = Vector3(x or 0, y or 0, z or 0), currentLevel = currentLevel or 0,
    guildId = GuildId.new({text = ""}), publishedSource = "", publishedElapsedSeconds = elapsed or 0,
    ftueCompleted = flag(ftueCompleted), mustRename = flag(mustRename), mustRenameReason = "",
    needsTransfer = flag(needsTransfer), hasTransfered = false, transferReason = "",
    modifiedDate = zero_time(), nameModifiedDate = zero_time(), needsTransferDate = zero_time(),
    nameLatentDate = zero_time(), isNameLatent = flag(isNameLatent), isFreshStart = flag(isFreshStart),
  }
end

--- One reserved name of the list.
function Swc.HostMenuName(name, worldId, region)
  local b = building()
  b.names[#b.names + 1] = {name = name, personaId = "", worldId = worldId, region = region or "",
    creationDate = zero_time(), modifiedDate = zero_time(), channel = ""}
end

local function to_values(list, constructor)
  local out = {}
  for _, fields in ipairs(list) do
    local value = constructor()
    for key, v in pairs(fields) do
      value[key] = v
    end
    out[#out + 1] = value
  end
  return out
end

--- The end of a list result: OnLoginInfoListResult to the screens. A refresh keeps the selected character by its id.
function Swc.HostMenuListEnd(maxCharacters)
  local b = building()
  F.building = nil
  F.worlds, F.characters, F.names = b.worlds, b.characters, b.names
  F.maxCharacters = maxCharacters or 0
  if F.selectedId ~= "" then
    for _, c in ipairs(F.characters) do
      if c.characterId == F.selectedId then
        Swc.SetData("MainMenu.NewCharacterId", F.selectedId)
        break
      end
    end
  end
  Swc.Notify("UiLoginScreenNotificationBus", nil, "OnLoginInfoListResult",
    to_values(F.worlds, WorldMetadata), to_values(F.characters, CharacterMetadata), to_values(F.names, CharacterNameData),
    F.maxCharacters, {})
end

--- A failed login of the selected character (no login token): the connection state 0 and the error text in the data node
--- MainMenu.LastError (the data callback of landingscreenv2.luac shows the popup "@mm_connectionfailed" with the text; its OK
--- resets the screen, and PLAY can try again).
function Swc.HostMenuLoginFailed(text)
  F.connection = 0
  Swc.SetData("MainMenu.LastError", text or "@mm_loginservices_UnableToConnectError")
end

--- The connection state of the login of the selected character (the numbers of the table of landingscreenv2.luac:
--- 0 Disconnected ... 14 InGame).
function Swc.HostMenuConnection(status)
  F.connection = status or 0
end

--- The end of the load of the menu level: CrySystemNotificationsBus OnLevelLoad and OnLevelLoadComplete (the landing screen
--- then plays its queued camera sequence).
function Swc.HostMenuLevelLoaded()
  local crc = Math.CreateCrc32(F.levelName)
  local buses = {"LyShineScriptBindNotificationBus", "CrySystemNotificationsBus"}
  Swc.NotifyOnce(buses, "OnLevelLoad", crc)
  Swc.NotifyOnce(buses, "OnLevelLoadComplete", crc)
end

-- The screens of the front end (the native loads of the main menu; the canvas of each and its draw order). The loader of the
-- world loads the options, the escape menu, and the popup again at the world entry.
local LANDING_STATE = 3881446394
local MENU_SCREENS = {
  {"LyShineUI/Main Menu/LandingScreenV2.uicanvas", 5},
  {"LyShineUI/Options/Options.uicanvas", 30},
  {"LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 41},
  {"LyShineUI/Popup/Popup.uicanvas", 60},
}
F.canvases = {}

--- The start of the front end: the menu screens load, the end of the UI load goes to the options screen only (FE9), and the
--- state LandingScreen shows the landing screen. The host then sends the regions and the account state.
function Swc.HostMenuStart()
  F.menu = true
  if #F.canvases == 0 then
    for _, screen in ipairs(MENU_SCREENS) do
      local canvas = Swc.LoadScreen(screen[1], true, screen[2])
      if canvas:IsValid() then
        F.canvases[#F.canvases + 1] = canvas
      end
    end
  end
  Swc.AuthComplete()
  LyShineManagerBus.Broadcast.SetState(LANDING_STATE)
end

--- The end of the front end at the world entry: the menu screens unload (the loader of the world loads its own screens).
function Swc.HostMenuEnd()
  F.menu = false
  for _, canvas in ipairs(F.canvases) do
    Swc.UnloadCanvas(canvas)
  end
  F.canvases = {}
end

-- ------------------------------------------------------------------------------------------------- the requests

local function deliver_cms()
  local data = {
    globalMotd = {title = "", description = ""},
    worldDescriptions = {},
  }
  Swc.Notify("UiMainMenuBus", nil, "OnWorldCMSDataSet", data)
end

local function deliver_regions()
  Swc.Notify("UiLoginScreenNotificationBus", nil, "OnClusterListResult", to_values(F.regions, RegionMetadata))
end

--- Each frame: the local results of the frame before, and the ends of the camera sequences.
function Swc.FrontendTick(dt)
  local queue = F.queue
  F.queue = {}
  for _, item in ipairs(queue) do
    if F.hold[item.kind] then
      F.held[item.kind] = (F.held[item.kind] or 0) + 1
    elseif item.kind == "cms" then
      deliver_cms()
    elseif item.kind == "regions" then
      deliver_regions()
    end
  end
  for name, seq in pairs(F.sequences) do
    seq.left = seq.left - dt
    if seq.left <= 0 then
      F.sequences[name] = nil
      Swc.Notify("CinematicEventBus", nil, "OnCinematicStateChanged", name, eMovieEvent_Stopped or 2)
    end
  end
end

--- A test: end the hold of a kind and give its held results (one result for each kind is enough: the script keeps the last).
function Swc.FrontendRelease(kind)
  F.hold[kind] = false
  if (F.held[kind] or 0) > 0 then
    F.held[kind] = 0
    if kind == "cms" then
      deliver_cms()
    elseif kind == "regions" then
      deliver_regions()
    end
  end
end

Swc.Define("UiLoginScreenRequestBus", "Broadcast", {
  GetLoginInfoLists = function()
    F.listRequests = F.listRequests + 1
    request("menu list")
  end,
  GetRegionList = function()
    F.queue[#F.queue + 1] = {kind = "regions"}
  end,
  SetVisibleCharacter = function(characterId)
    F.selectedId = tostring(characterId or "")
    request("menu preview " .. F.selectedId)
  end,
  SetSelectedWorldId = function(worldId)
    F.worldId = tostring(worldId or "")
  end,
  GetGatewayRegion = function() return F.regionId end,
})

Swc.Define("MainMenuSystemRequestBus", "Broadcast", {
  SetSelectedWorldId = function(worldId)
    F.worldId = tostring(worldId or "")
  end,
})

Swc.Define("DynamicContentBus", "Broadcast", {
  RetrieveCMSData = function(kind)
    if kind == eCMSDataType_Worlds then
      F.queue[#F.queue + 1] = {kind = "cms"}
    end
  end,
  ClearCMSData = function() end,
  RetrieveWorldCMSWarning = function() end,
})

Swc.Define("UiMainMenuRequestBus", "Broadcast", {
  RequestCustomizableCharacterEntityId = function()
    Swc.Notify("UiMainMenuBus", nil, "SetCustomizableCharacterEntityId", F.previewEntity)
  end,
  -- No event multiplier on this backend (the XP boosts of the landing screen show for a value above 1).
  GetProgressionMultiplier = function() return 1 end,
  ShowLoadingScreen = function() request("menu loading") end,
  StartIntro = function() request("menu intro") end,
  StartBenchmark = function() end,
})

Swc.Define("CustomizableCharacterRequestBus", "Event", {
  SetAlwaysUpdate = function() end,
})

-- The preview character shows at the menu height and hides at Z -1000 (landingscreenv2.luac ShowCharacter).
Swc.Define("TransformBus", "Event", {
  SetWorldZ = function(entity, z)
    if tostring(entity) ~= tostring(F.previewEntity) then
      return
    end
    F.previewZ = z
    local shown = z > -100
    if shown ~= F.previewShown then
      F.previewShown = shown
      request("menu show " .. (shown and "1" or "0"))
    end
  end,
})

-- The ends of the camera sequences: the time ranges of the sequences of the NightHaven scene (D5 of docs/FRONTEND.md).
local SEQUENCE_SECONDS = {IntroStart = 3, IntroToCharacter = 1, CharacterToIntro = 1, EstablishingToCharacter = 1,
  Idle = 36, OnPlay = 2}
Swc.Define("CinematicRequestBus", "Broadcast", {
  PlaySequenceByName = function(name)
    F.sequences[name] = {left = SEQUENCE_SECONDS[name] or 0}
  end,
  StopSequence = function(name)
    if F.sequences[name] then
      F.sequences[name] = nil
      Swc.Notify("CinematicEventBus", nil, "OnCinematicStateChanged", name, eMovieEvent_Stopped or 2)
    end
  end,
})

Swc.Define("JavelinCameraRequestBus", "Broadcast", {
  EnableDepthOfField = function() end,
  SetDepthOfField = function() end,
  ResetDepthOfField = function() end,
})

Swc.Define("GameRequestsBus", "Broadcast", {
  IsInMainMenu = function() return F.menu end,
  GetMainMenuLevelName = function() return F.levelName end,
  IsFirstRun = function() return false end,
  IsAuthorized = function() return F.authorized end,
  IsMenuReset = function() return false end,
  ClearFirstRun = function() end,
  -- The account owns the game (the free weekend popup of the landing screen is for a trial account).
  IsPermanentAppOwner = function() return true end,
  GetRegionId = function() return F.regionId end,
  GetConnectionStatus = function() return F.connection end,
  RequestLogin = function(characterId, worldType)
    request("menu login " .. tostring(characterId) .. " " .. tostring(worldType or ""))
  end,
  RequestDisconnect = function() request("menu quit") end,
  OpenSteamStoreOverlay = function() end,
  TextToSpeech = function() end,
  -- The channel of the channel document of the backend ("channelName": "Retail").
  GetChannelName = function() return "Retail" end,
  IsFreeTrialOwner = function() return false end,
  -- No Steam presence of friends: the client reads no Steam friend list (only the Steam id and the persona name).
  GetSteamPresenceFriendsWorldInfo = function() return {} end,
})

Swc.Define("LyShineManagerBus", "Broadcast", {
  -- The message of the day of the world CMS data: the local result is empty.
  SetGlobalAnnouncement = function() end,
})

Swc.Define("EntitlementRequestBus", "Broadcast", {
  EntitlementsReceived = function() return F.entitlements end,
  -- The local world: each world of the list of the backend is a world of the account.
  IsEntitledForWorld = function() return true end,
  GetAllRewardsOfType = function() return {} end,
  GetAllEntitlementIds = function() return {} end,
  AreThereNewEntitlements = function() return false end,
})

-- No invite, no activity launch, no Twitch on this backend.
Swc.Define("GameInviteRequestBus", "Broadcast", {
  GetIsInvitePending = function() return false end,
  GetInvitedWorld = function() return "" end,
  GetInvitedRegion = function() return "" end,
  GetInviterName = function() return "" end,
  ClearPendingWorldSelectFlag = function() end,
  DeclinePendingInvite = function() end,
})
Swc.Define("GameActivityRequestBus", "Broadcast", {
  GetIsLaunchedFromActivity = function() return false end,
  ClearLaunchedFromActivity = function() end,
})
Swc.Define("TwitchSystemRequestBus", "Broadcast", {
  IsLoggedIn = function() return false end,
})

return F
