-- The game buses that the first HUD reads, fed by the host (swc/host.lua: the decoded states of the backend). Each
-- other game bus is a stub; the stub log is the list of the next work (research open point O9).

local Swc = Swc

Swc.Handled("ConfigSystemEventBus", "LocalPlayerEventsBus", "LocalPlayerComponentNotificationBus")

-- ---------------------------------------------------------------------------------------------------- the config

-- The remote configuration and the feature flags of the client. The backend gives one document of 12 (docs/NETWORK.md,
-- step 6 of the login path); the host can set values (Swc.SetConfig). A flag that is not set is false.
Swc.config = {}

function Swc.SetConfig(name, value)
  Swc.config[name] = value
end

Swc.Define("ConfigProviderEventBus", "Broadcast", {
  GetBool = function(name) return Swc.config[name] == true end,
  GetInt = function(name) return tonumber(Swc.config[name]) or 0 end,
  GetUInt = function(name) return tonumber(Swc.config[name]) or 0 end,
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

-- The end of the last combat (a time point in the past: not in combat; quitgamepopup.luac).
Swc.SetData("Hud.LocalPlayer.CombatStatus.InCombatTimePoint", Swc.TimePoint.new({seconds = 0}))

-- ------------------------------------------------------------------------------------------------- the key names

-- The default keys of the client (defaultprofile.xml; research feel-values.md R6.3): the hints of the HUD show them.
Swc.keys = {
  ability1 = "Q", ability2 = "R", ability3 = "F", rune_ability = "G", reload = "R",
  ["quickslot-weapon1"] = "1", ["quickslot-weapon2"] = "2",
  ["quickslot-consumable-1"] = "3", ["quickslot-consumable-2"] = "4", ["quickslot-consumable-3"] = "5",
  ["quickslot-consumable-4"] = "6", sheathe = "~", dodge = "Left Shift", jump = "Space", ui_interact = "E",
  toggleInventoryWindow = "Tab", toggleMapComponent = "M", toggleSkillsComponent = "K", toggleMenuComponent = "Escape",
  mount_summon = "X", target_tag = "B",
}

function Swc.Keybind(action)
  if action == nil then
    return nil
  end
  return Swc.keys[action]
end

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
    gamedata.itemsByCrc = {}
  end
  return gamedata
end

local function item_crc(itemId)
  local crc = swcui.crc32(string.lower(itemId))
  Swc.GameData().itemsByCrc[crc] = string.lower(itemId)
  return crc
end

local function weapon_of(itemId)
  if not itemId then
    return nil
  end
  return Swc.GameData().weapons[string.lower(itemId)]
end

-- An item slot of the paperdoll (the value class of the client: IsValid, GetItemId, IsEquipped, ...).
Swc.ItemSlot = Swc.ValueClass({
  name = "ItemSlot",
  fields = {"itemId", "slot"},
  methods = {
    IsValid = function(self) return self.itemId ~= nil end,
    IsEquipped = function(self) return self.itemId ~= nil end,
    GetItemId = function(self) return self.itemId and item_crc(self.itemId) or 0 end,
    GetItemName = function(self) return self.itemId or "" end,
    GetSlotId = function(self) return self.slot end,
    GetStackSize = function(self) return self.itemId and 1 or 0 end,
  },
})

local function item_slot(slot)
  return Swc.ItemSlot.new({itemId = Swc.player.equipment[slot], slot = slot})
end

local function item_slot_of(value)
  -- A slot object of the scripts, or a slot number.
  if type(value) == "number" then
    return item_slot(value)
  end
  return value
end

Swc.Define("PaperdollRequestBus", "Event", {
  GetSlot = function(_, slot) return item_slot(slot) end,
  GetActiveSlot = function(_, alias)
    if alias == ePaperdollSlotAlias_ActiveWeapon then
      return Swc.player.activeWeaponSlot or ePaperDollSlotTypes_Invalid
    elseif alias == ePaperdollSlotAlias_ActiveOffHandWeapon then
      local active = Swc.player.activeWeaponSlot
      return active and (active + 3) or ePaperDollSlotTypes_Invalid
    end
    return ePaperDollSlotTypes_Invalid
  end,
  IsSheathed = function(_, slot) return Swc.player.sheathed[slot] == true end,
  GetItemDescriptor = function(_, slot) return item_slot(slot) end,
})
Swc.Handled("PaperdollEventBus")

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
  local weapon = slot and weapon_of(slot.itemId)
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

-- The item data (ItemDataManagerBus): the weapon data and the static item data of an item id CRC. Only the items of
-- gamedata/weapons.json have data (the weapons and the heart runes of the weapon bar).
local function item_valid(self) return self.key ~= nil end

Swc.Define("ItemDataManagerBus", "Broadcast", {
  GetWeaponData = function(itemCrc)
    local weapon = weapon_of(Swc.GameData().itemsByCrc[itemCrc])
    local equip = weapon and weapon.equipAbility
    return {mannequinTags = weapon and weapon.mannequinTags or {}, tierNumber = weapon and weapon.tier or 0,
            equipAbilityId = equip and swcui.crc32(string.lower(equip)) or nil}
  end,
  GetItemData = function(itemCrc)
    local itemId = Swc.GameData().itemsByCrc[itemCrc]
    local weapon = weapon_of(itemId)
    if not weapon then
      return {IsValid = item_valid}
    end
    return {IsValid = item_valid, key = itemId, name = weapon.name, description = weapon.description,
            itemClass = weapon.itemClass, tierNumber = weapon.tier}
  end,
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
