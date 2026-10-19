-- The game buses that the first HUD reads, fed by the host (swc/host.lua: the decoded states of the backend). Each
-- other game bus is a stub; the stub log is the list of the next work (research open point O9).

local Swc = Swc

Swc.Handled("ConfigSystemEventBus", "LocalPlayerEventsBus", "LocalPlayerComponentNotificationBus")

-- ---------------------------------------------------------------------------------------------------- the config

-- The remote configuration and the feature flags of the client. The backend gives one document of 12 (docs/NETWORK.md,
-- step 6 of the login path); the host can set values (Swc.SetConfig). A flag that is not set is false.
Swc.config = {
  -- The default of the client (research inventory.md 6: the setting has the default 500, the vector a maximum of 500).
  ["javelin.max-container-size"] = 500,
  -- The defaults of the settings that the item screens read (the registration function of the client at image RVA
  -- 0x616C670; the address of the name of each setting). Open point: the defaults of the other settings.
  ["javelin.enable-dyeing"] = true,                             -- 0x616EBEA
  ["javelin.enable-equipment-set-bonuses"] = true,              -- 0x618783F
  ["javelin.enable-instanced-drops"] = true,                    -- 0x6181182
  ["javelin.enable-inventory-while-running"] = true,            -- 0x6176FE7
  ["javelin.enable-item-skinning"] = true,                      -- 0x616CB57
  ["javelin.enable-shared-cooldown-reduction"] = true,          -- 0x616FF80
  ["javelin.enable-storage-transfer-rfi"] = true,               -- 0x6178160
  ["javelin.gear-set-storage-enabled"] = true,                  -- 0x6174241
  ["javelin.gear-set-storage-enable-store-attributes"] = true,  -- 0x6174607
  ["javelin.gear-set-storage-hotkeys-enabled"] = true,          -- 0x617426A
  ["javelin.umbral-upgrade-required-tier"] = 5,                 -- 0x617F3F9
  -- The gates of the two screen loaders (stage P2.3; docs/UI.md "The census"): the defaults of the registration of
  -- the settings (UIFeatures: the records at image RVA 0x4A3A1F7 to 0x4A3B1FA; javelin: the setters of 0x616C670 and
  -- the others). The backend gives no other document (a 404 leaves the defaults; docs/NETWORK.md step 6).
  ["UIFeatures.enable-on-demand-canvas-loading"] = true,       -- 0x4A3B08E
  ["UIFeatures.enableBoxOpeningPopup"] = true,                 -- 0x4A3A500
  ["UIFeatures.enable-control-hints-screen"] = true,           -- 0x4A3B1FA
  ["UIFeatures.enable-enhanced-dialogue"] = true,              -- 0x4A3AF4C
  ["UIFeatures.enable-hud-paperdoll-status"] = false,          -- 0x4A3B1B4
  ["UIFeatures.enableQueueHud"] = true,                        -- 0x4A3A84B
  ["UIFeatures.enable-target-lock-ui"] = true,                 -- 0x4A3B0D4
  ["UIFeatures.enable-territory-incentives-screen"] = true,    -- 0x4A3AA0A
  ["UIFeatures.g_uiEnableStyleGuide"] = false,                 -- 0x4A3A805
  ["UIFeatures.housingImprovements"] = false,                  -- 0x4A3ABCD
  ["javelin.enable-all-dungeons"] = true,                      -- 0x617B13D
  ["javelin.enable-barbershop"] = true,                        -- 0x61850C4
  ["javelin.enable-charms-feature"] = true,                    -- 0x618247E
  ["javelin.enable-faction-changing"] = true,                  -- 0x617669E
  ["javelin.enable-ftue-v2"] = true,                           -- 0x616DFE1
  ["javelin.enable-game-mode-arena-3v3"] = true,               -- 0x617B3F6
  ["javelin.enable-game-mode-capture-the-flag"] = true,        -- 0x617B93F
  ["javelin.enable-game-mode-duels"] = true,                   -- 0x617B099
  ["javelin.enable-game-mode-outpost-rush"] = true,            -- 0x617B0C2
  ["javelin.enable-group-finder"] = true,                      -- 0x617E014
  ["javelin.enable-in-game-survey"] = false,                   -- 0x1083315
  ["javelin.enable-journal"] = true,                           -- 0x616EB98
  ["javelin.enable-make-good-notification"] = true,            -- 0x616F183
  ["javelin.enable-mtx-storeV2"] = true,                       -- 0x6184C56
  ["javelin.enable-p2p-trading"] = true,                       -- 0x617E9F4
  ["javelin.enable-pug-finder"] = true,                        -- 0x617E0BB
  ["javelin.enable-ready-up-feature"] = true,                  -- 0x61862F5
  ["javelin.enable-transmog"] = true,                          -- 0x6183860
  ["javelin.faction-control.enabled"] = true,                  -- 0x61726E2
  ["javelin.faction-influence-v2-enabled"] = true,             -- 0x616FFD7
  ["javelin.news-modal.enable-in-game-news-screen"] = true,    -- 0x6187488
  ["javelin.player-tutorials.enabled"] = true,                 -- 0x6184A3B
  ["javelin.seasons-consumable-tokens-enabled"] = true,        -- 0x61770E2
  ["javelin.seasons-rewards.enabled"] = true,                  -- 0x617341A
  ["javelin.siege.enable-warboard"] = true,                    -- 0x6172569
  ["javelin.social.enable-player-inspect"] = true,             -- 0x61755FF
  -- The flags of the options screen (stage P2.3): the categories, the key page.
  ["UIFeatures.g_uiEnableAccessibilitySettings"] = true,       -- 0x4A39C78
  ["UIFeatures.g_allowUnboundKeys"] = true,                    -- 0x4A3A332
  ["UIFeatures.ignoreKeybindConflicts"] = false,               -- 0x4A3A523
  ["javelin.social.enable-streamermode-blocking"] = true,      -- 0x616D904
  ["javelin.allow-controller-remapping"] = true,               -- 0x616C9E6
  ["javelin.enable-hotkey-targeted-healing"] = true,           -- 0x10831ED
  ["javelin.mount-character-relative-hotkeys-enabled"] = true, -- 0x1083771
  ["javelin.pvp-spectator-cam-enabled"] = true,                -- 0x6180DF1
  -- The flags of the rows of the options screen (the rows with a flag that is false do not show; javelin.enable-exit-survey
  -- has no reference in the code: not set).
  ["UIFeatures.enable-gena"] = true,                           -- 0x4A3A40B
  ["UIFeatures.enable-hud-2024-customization"] = true,         -- 0x4A3B06B
  ["UIFeatures.enable-hud-2024-layouts"] = true,               -- 0x4A3B002
  ["UIFeatures.enable-hud-2024-margin"] = true,                -- 0x4A3B025
  ["UIFeatures.enable-hud-2024-scale"] = true,                 -- 0x4A3B048
  ["UIFeatures.enable-subtitles-v2"] = true,                   -- 0x4A3AF29
  ["UIFeatures.enable-tts"] = true,                            -- 0x4A3A3E8
  ["UIFeatures.enableNameplateSlider"] = true,                 -- 0x4A3A8B4
  ["UIFeatures.enableReticleColorPicker"] = true,              -- 0x4A3A954
  ["UIFeatures.g_enableCrossplayFeature"] = true,              -- 0x4A3B21D
  ["UIFeatures.g_enableTwitchSpotlight"] = true,               -- 0x4A39FEA
  ["UIFeatures.g_enableTwitchSubArmy"] = true,                 -- 0x4A3A00D
  ["UIFeatures.g_uiBandwidthModeSetting"] = true,              -- 0x4A3A63B
  ["UIFeatures.g_uiEnableAdvancedVideoSettings"] = true,       -- 0x4A3A16B
  ["UIFeatures.g_uiEnableBrightnessSettings"] = true,          -- 0x4A39BC9
  ["UIFeatures.g_uiEnableContrastSettings"] = true,            -- 0x4A39BEC
  ["UIFeatures.g_uiEnableFPSSettings"] = true,                 -- 0x4A3A1B1
  ["UIFeatures.g_uiEnableGammaSettings"] = false,              -- 0x4A39BA6
  ["UIFeatures.g_uiEnableHeadphoneSpatialization"] = true,     -- 0x4A3A1D4
  ["UIFeatures.g_uiEnableLanguageSettings"] = true,            -- 0x4A39B60
  ["UIFeatures.g_uiEnableMotionBlurSettings"] = false,         -- 0x4A39B83
  ["UIFeatures.g_uiEnableReticleAlwaysShowing"] = true,        -- 0x4A39B1A
  ["javelin.enable-menu-vibration"] = false,                   -- 0x617EB19
  ["javelin.enable-premium-emotes"] = true,                    -- 0x616CBA9
  ["javelin.group-marker-priority-enabled"] = true,            -- 0x61769A1
  ["javelin.pvp-reward-track-enabled"] = true,                 -- 0x6173DEB
}

-- The UIFeatures settings are also data nodes of the data layer (the scripts read some as data: the key page,
-- UIFeatures.ignoreKeybindConflicts and g_allowUnboundKeys; the UI loader, housingImprovements).
for name, value in pairs(Swc.config) do
  if string.sub(name, 1, 11) == "UIFeatures." then
    Swc.SetData(name, value)
  end
end

function Swc.SetConfig(name, value)
  Swc.config[name] = value
end

Swc.Define("ConfigProviderEventBus", "Broadcast", {
  GetBool = function(name) return Swc.config[name] == true end,
  GetInt = function(name) return tonumber(Swc.config[name]) or 0 end,
  GetUInt = function(name) return tonumber(Swc.config[name]) or 0 end,
  GetUInt64 = function(name) return tonumber(Swc.config[name]) or 0 end,
  GetFloat = function(name) return tonumber(Swc.config[name]) or 0 end,
  GetString = function(name) local v = Swc.config[name] if type(v) == "string" then return v end return "" end,
})

-- ------------------------------------------------------------------------------------------------ the reflection

-- The client registers groups of classes and buses on demand (g_Reflect:RequireReflectionCategory, 27 categories;
-- research modding-native-ui.md 2.1). In the emulation each bus exists at once: the call only records the category.
Swc.reflectionCategories = {}
ScriptReflection = function()
  return {
    RequireReflectionCategory = function(_, category)
      Swc.reflectionCategories[tostring(category)] = true
    end,
  }
end

-- The engine classes that the scripts make and that the emulation does not have yet.
Swc.NativeClass("VirtualCursorConfiguration")

-- ------------------------------------------------------------------------------------------------------ the game

Swc.levelName = ""

-- The platform of the client (a native table; the scripts only compare its values): this client is a 64-bit PC.
Platform = {Windows64 = 1, Windows32 = 2, Scarlett = 3, Prospero = 4}
Platform.Current = Platform.Windows64

Swc.Define("GameRequestsBus", "Broadcast", {
  IsInMainMenu = function() return false end,
  GetMainMenuLevelName = function() return "frontend" end,
  IsFastLoadEnabled = function() return false end,
})

Swc.Define("FtueSystemRequestBus", "Broadcast", {
  IsFtue = function() return false end,
})

-- The store of the retail client: the backend has no store, thus no entitlement and a balance of 0 (the escape menu
-- and its store button read them).
Swc.Define("EntitlementRequestBus", "Broadcast", {
  GetEntitlementBalance = function() return 0 end,
  GetEntitlementsForRewardType = function() return {} end,
})

-- The input profiles of a game pad (the options screen lists them): none (keyboard and mouse only).
Swc.Define("InputProfileDataManagerRequestBus", "Broadcast", {
  GetAllInputProfileData = function() return {} end,
})

-- The build of the client (the options screen shows it): this client, no changelist of the retail build.
Swc.Define("GameRequestsBus", "Broadcast", {
  GetVersion = function() return "Soulwarden client" end,
  GetChangelist = function() return "-" end,
  ResetActionMaps = function() end,
})

-- The world map data of the client (MapComponentBus): none in this stage. The respawn screen holds the world map
-- (magicmap.luac and its layers): the lists are empty, the territory ids 0, the tests false.
Swc.Define("MapComponentBus", "Broadcast", {
  GetSimplePOIs = function() return {} end,
  GetTerritories = function() return {} end,
  GetAllLandmarksByType = function() return {} end,
  GetOutposts = function() return {} end,
  GetFastTravelPointPOIIds = function() return {} end,
  GetContainingTerritories = function() return {} end,
  GetSettlements = function() return {} end,
  GetLandmarksInTerritory = function() return {} end,
  GetClaims = function() return {} end,
  GetSmallestContainingTerritory = function() return 0 end,
  GetContainingTerritory = function() return 0 end,
  GetTractAtPosition = function() return "" end,
  WorldPosToTerritoryLoRes = function() return 0 end,
  IsDynamicPoiObjectiveComplete = function() return false end,
  HasDynamicPoiObjective = function() return false end,
  GetMedianPoiLevel = function() return 0 end,
})

-- The group and the raid sizes of the game (5 members in a group, 10 groups in a raid); the map layers make their
-- icons from them (markerslayer.luac). No position of the raid members.
Swc.SetData("Hud.LocalPlayer.Group.MaxMembers", 5)
Swc.SetData("Hud.LocalPlayer.Raid.MaxGroups", 10)
Swc.SetData("javelin.enable-replicate-all-raid-member-positions", false)

-- The place of the player before the first value of the host (swc/host.lua writes them each frame).
Swc.SetData("Hud.LocalPlayer.PlayerHeading", 0)
Swc.SetData("Hud.LocalPlayer.Position", Swc.Vector3.new({x = 0, y = 0, z = 0}))

-- The home points of the character (the respawn list): the backend has none, thus the one row "Random Location".
Swc.SetData("Hud.LocalPlayer.HomePoints.Count", 0)

-- The level cap of the character (progression-tables.json levelCap of the backend copy; the host gives it:
-- Swc.HostLevelCap). The loading screen tips use it (loadingscreenmanager.luac).
Swc.levelCap = 60
function Swc.HostLevelCap(cap)
  Swc.levelCap = tonumber(cap) or Swc.levelCap
end
Swc.Define("ProgressionRequestBus", "Event", {
  GetMaxLevelForPlayer = function() return Swc.levelCap end,
  -- The highest level of the wire (the level of the screen minus 1: xpdetails.luac adds 1).
  GetMaxLevel = function() return Swc.levelCap - 1 end,
})

-- The NPC markers of the objectives of the character (the map layers read them): none in this stage.
Swc.Define("ObjectivesComponentRequestBus", "Event", {
  GetNPCMarkers = function() return {} end,
})

-- The end of the last combat (a time point in the past: not in combat; quitgamepopup.luac).
Swc.SetData("Hud.LocalPlayer.CombatStatus.InCombatTimePoint", Swc.TimePoint.new({seconds = 0}))

-- ------------------------------------------------------------------------------------------------- the local player

-- The entities of the local player that the HUD addresses (the client gives their ids in the data layer paths
-- Hud.LocalPlayer.HudComponent.*). Here they are ids of the runtime; the game buses below answer for them.
Swc.ids = {
  player = EntityId("6005173470066573569"),
  root = EntityId("6005173470066573570"),
  paperdoll = EntityId("6005173470066573571"),
  objectives = EntityId("6005173470066573572"),
}

-- The paperdoll slots of the client (Javelin::PaperdollSlotTypes; Soulwarden protocol notes 4.11, registration
-- 0x5D19E40). The scripts compare these values with the slots of the paperdoll state of the backend.
local slots = {Head = 0, Chest = 1, Hands = 2, Legs = 3, Feet = 4, Amulet = 5, Token = 6, Ring = 7, GatherableHand = 9,
  Arrow = 12, Cartridge = 13, QuickSlot1 = 14, QuickSlot2 = 15, QuickSlot3 = 16, QuickSlot4 = 17, QuickSlot5 = 18,
  QuickSlot6 = 19, MainHandOption1 = 24, MainHandOption2 = 25, MainHandOption3 = 26, OffHandOption1 = 27,
  OffHandOption2 = 28, OffHandOption3 = 29, BagSlot1 = 30, BagSlot2 = 31, BagSlot3 = 32, Chopping = 33, Cutting = 34,
  Dressing = 35, Mining = 36, Fishing = 37, AzothStaff = 38, InstrumentFlute = 39, InstrumentGuitar = 40,
  InstrumentMandolin = 41, InstrumentUprightBass = 42, InstrumentDrums = 43, HeartGem = 45, Mount = 48, Num = 50,
  Invalid = 51}
for name, value in pairs(slots) do
  Swc.DefineEnum("ePaperDollSlotTypes_" .. name, value)
end
-- The aliases of GetActiveSlot (the client values: ActiveWeapon 50, ActiveOffHandWeapon 55).
Swc.DefineEnum("ePaperdollSlotAlias_ActiveWeapon", 50)
Swc.DefineEnum("ePaperdollSlotAlias_ActiveOffHandWeapon", 55)

Swc.player = Swc.player or {}
Swc.player.equipment = {}      -- slot -> item id (text)
Swc.player.sheathed = {}       -- slot -> true
Swc.player.activeWeaponSlot = nil
Swc.player.bars = {}           -- ability table CRC -> {ability key, ability key, ability key}
Swc.player.cooldowns = {}      -- ability key -> {ends = time of Swc.time, total = seconds}

-- The game data of the HUD (tools/convert-ui.sh gamedata).
local gamedata
function Swc.GameData()
  if gamedata == nil then
    gamedata = {abilities = {}, tables = {}, weapons = {}}
    local abilities, err = swcui.load_json("gamedata/abilities.json")
    if abilities then
      gamedata.abilities = abilities.abilities or {}
      gamedata.tables = abilities.tables or {}
    else
      Swc.Note("no ability data: " .. tostring(err) .. " (tools/convert-ui.sh gamedata)")
    end
    local weapons, err2 = swcui.load_json("gamedata/weapons.json")
    if weapons then
      gamedata.weapons = weapons
    else
      Swc.Note("no weapon data: " .. tostring(err2) .. " (tools/convert-ui.sh gamedata)")
    end
  end
  return gamedata
end

local function weapon_of(itemId)
  if not itemId then
    return nil
  end
  return Swc.GameData().weapons[string.lower(itemId)]
end

-- The item slots, the paperdoll, and the item data: swc/items.lua.
local function item_slot_of(value)
  -- A slot object of the scripts, or a slot number.
  if type(value) == "number" then
    return Swc.PaperdollSlot(value)
  end
  return value
end

-- The abilities (CharacterAbilityRequestBus of the entity root). An ability data object has the fields that the HUD
-- reads: id (the ability key), displayIcon, uiCategory, weaponTag (CRC-32), treeId, holdConditionButtonIcon.
local function ability_data(key)
  local row = Swc.GameData().abilities[tostring(key)]
  if not row then
    return nil
  end
  return {id = tonumber(key), key = row.id, abilityId = row.id, displayIcon = row.icon, uiCategory = row.uiCategory,
          weaponTag = swcui.crc32(string.lower(row.weaponTag or "")), treeId = row.treeId, displayName = row.displayName,
          holdConditionButtonIcon = row.holdIcon ~= "" and row.holdIcon or nil}
end

local function table_of_slot(itemSlot)
  local slot = item_slot_of(itemSlot)
  local weapon = slot and slot.static and weapon_of(slot.static.key)
  if not weapon or weapon.masteryTable == "" then
    return nil
  end
  return weapon.masteryTable
end

Swc.Define("CharacterAbilityRequestBus", "Event", {
  GetAbilityTableFromItemSlot = function(_, itemSlot)
    local name = table_of_slot(itemSlot)
    return name and swcui.crc32(string.lower(name)) or 0
  end,
  GetActiveAbilityDataByItemSlot = function(_, itemSlot)
    local name = table_of_slot(itemSlot)
    if not name then
      return {}
    end
    local bar = Swc.player.bars[swcui.crc32(string.lower(name))]
    local out = {}
    for i = 1, 3 do
      out[i] = bar and bar[i] and ability_data(bar[i]) or nil
    end
    return out
  end,
  LookupAbilityDataFromAllAbilityTables = function(_, key) return ability_data(key) end,
  GetAbilityDataFromAbilityId = function(_, key) return ability_data(key) end,
  GetEquippedAbilitySpellManaCost = function(_, key)
    local row = Swc.GameData().abilities[tostring(key)]
    return row and row.manaCost or 0
  end,
  GetTotalCooldownTime = function(_, key)
    local cd = Swc.player.cooldowns[key]
    if cd then
      return cd.total
    end
    local row = Swc.GameData().abilities[tostring(key)]
    return row and row.cooldown or 0
  end,
  GetNumFreeCooldownsRemaining = function() return 0 end,
})
Swc.Handled("CharacterAbilityNotificationBus", "CharacterAbilityUINotificationBus", "AbilityCooldownNotificationsBus")

local function cooldown_left(key)
  local cd = Swc.player.cooldowns[key]
  if not cd then
    return 0
  end
  return math.max(0, cd.ends - Swc.time)
end

Swc.Define("CooldownTimersComponentBus", "Event", {
  GetAbilityCurrentCooldown = function(_, key) return cooldown_left(key) end,
  IsAbilityOnCooldown = function(_, key) return cooldown_left(key) > 0 end,
})

-- The stamina of the player: the client predicts it (UiStaminaRequestsBus GetStaminaState); here the value of the
-- stamina state of the backend.
Swc.StaminaState = Swc.ValueClass({
  name = "StaminaState",
  fields = {"entity"},
  methods = {
    GetStaminaPercent = function()
      local p = Swc.player
      if p.staminaMax and p.staminaMax > 0 then
        return math.max(0, math.min(1, (p.stamina or 0) / p.staminaMax))
      end
      return 1
    end,
    GetStamina = function() return Swc.player.stamina or 0 end,
    GetMaxStamina = function() return Swc.player.staminaMax or 0 end,
  },
})
Swc.Define("UiStaminaRequestsBus", "Event", {
  GetStaminaState = function(entity) return Swc.StaminaState.new({entity = entity}) end,
})

Swc.Define("SpectatorUIRequestBus", "Broadcast", {
  IsInSpectatorMode = function() return false end,
})
