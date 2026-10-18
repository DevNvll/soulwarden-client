-- The items of the player (stage P2.4; docs/UI.md): the static data of the items (gamedata/items.tsv of the converter, read
-- by the native swcui.table_row), the item slots and the item descriptors of the bag and of the paperdoll (the states of the
-- backend through the host), the containers (ContainerRequestBus, ContainerEventBus), the paperdoll (PaperdollRequestBus),
-- and the weight and the coin of the data layer.

-- --------------------------------------------------------------------------------------------------- static data

local itemCache = {}   -- lower-case item id -> static data table (false: no such item)

--- @return The static data of an item (an id text in any letter case, or the CRC-32 of the lower-case id), or nil.
function Swc.ItemStatic(key)
  if key == nil or key == "" or key == 0 then
    return nil
  end
  local cacheKey = type(key) == "number" and key or string.lower(key)
  local cached = itemCache[cacheKey]
  if cached ~= nil then
    return cached or nil
  end
  local row = swcui.table_row("items", key)
  if row then
    row.crc = swcui.crc32(row.key)
    row.classes = {}
    for tag in string.gmatch(row.itemClass or "", "[^+]+") do
      row.classes[string.lower(tag)] = true
    end
    if row.uiItemClass then
      row.classes[string.lower(row.uiItemClass)] = true
    end
    row.maxStack = tonumber(row.maxStack) or 1
    itemCache[row.key] = row
    itemCache[row.crc] = row
  end
  itemCache[cacheKey] = row or false
  return row
end

-- The names of the item classes of the client (eItemClass_*): the value -> the tag of the data (lower case).
local classNames
local function class_name(value)
  if classNames == nil then
    classNames = {}
    for name, v in pairs(_G) do
      if type(name) == "string" and type(v) == "number" and name:sub(1, 11) == "eItemClass_" then
        classNames[v] = string.lower(name:sub(12))
      end
    end
  end
  return classNames[value]
end

local perkCache = {}

--- @return The static data of a perk (a perk id text in any letter case, or the CRC-32 of the lower-case id), or nil.
function Swc.PerkStatic(key)
  if key == nil or key == "" or key == 0 then
    return nil
  end
  local cacheKey = type(key) == "number" and key or string.lower(key)
  local cached = perkCache[cacheKey]
  if cached ~= nil then
    return cached or nil
  end
  local row = swcui.table_row("perks", key)
  if row then
    row.crc = swcui.crc32(row.key)
    perkCache[row.key] = row
    perkCache[row.crc] = row
  end
  perkCache[cacheKey] = row or false
  return row
end

--- @return True when the static data of an item has the item class (an eItemClass_* value or a tag text).
function Swc.ItemHasClass(static, class)
  if not static then
    return false
  end
  local name = type(class) == "number" and class_name(class) or string.lower(tostring(class))
  return name ~= nil and static.classes[name] == true
end

-- The paperdoll slot names of equipmentcommon.luac (nameToPaperdollSlotMap; the names of the slot elements of the equipment
-- screen): the native GetEquipSlots gives the names, and the requests of the scripts give a name. The slots with no name (8
-- paint, 18 and 19 quick slots 5 and 6, 20 backpack, 26 and 29 weapon 3) have no element in the screens.
local SLOT_NAMES = {
  ["main-hand-option-1"] = 24, ["main-hand-option-2"] = 25, ["off-hand-option-1"] = 27, ["off-hand-option-2"] = 28,
  ["quickslot-1"] = 14, ["quickslot-2"] = 15, ["quickslot-3"] = 16, ["quickslot-4"] = 17, ["bag-slot-1"] = 30,
  ["bag-slot-2"] = 31, ["bag-slot-3"] = 32, ["arrow-ammo"] = 12, ["cartridge-ammo"] = 13, head = 0, chest = 1, hands = 2,
  legs = 3, feet = 4, amulet = 5, token = 6, ring = 7, ["main-hand"] = 9, ["chopping-slot"] = 33, ["cutting-slot"] = 34,
  ["dressing-slot"] = 35, ["mining-slot"] = 36, ["azothstaff-slot"] = 38, ["fishing-slot"] = 37,
  ["instrument-flute-slot"] = 39, ["instrument-guitar-slot"] = 40, ["instrument-mandolin-slot"] = 41,
  ["instrument-uprightbass-slot"] = 42, ["instrument-drums-slot"] = 43, ["heart-gem-slot"] = 45,
  ["mount-buff-slot-1"] = 46, ["mount-buff-slot-2"] = 47, ["mount-attachment-slot-1"] = 48, ["mount-attachment-slot-2"] = 49,
}
local NAME_OF_SLOT = {}
for name, slot in pairs(SLOT_NAMES) do
  NAME_OF_SLOT[slot] = name
end

local function paperdoll_slot_number(value)
  if type(value) == "number" then
    return value
  end
  return SLOT_NAMES[tostring(value)] or tonumber(value)
end

-- The paperdoll slots that an item class takes (the backend rule world/item.cpp slot_accepts; protocol notes 4.11).
local SLOT_TAGS = {
  [0] = "equippablehead", [1] = "equippablechest", [2] = "equippablehands", [3] = "equippablelegs", [4] = "equippablefeet",
  [5] = "equippableamulet", [6] = "equippabletoken", [7] = "equippablering", [8] = "equippablepaint",
  [20] = "equippablebackpack", [30] = "bag", [31] = "bag", [32] = "bag", [45] = "heartgem",
}
local WEAPON_SLOTS = {24, 25, 26}
local OFFHAND_SLOTS = {27, 28, 29}
local QUICK_SLOTS = {14, 15, 16, 17, 18, 19}

--- @return The paperdoll slots that an item can go into (in the order of the client), or {}.
function Swc.ItemEquipSlots(static)
  local out = {}
  if not static then
    return out
  end
  for slot = 0, 45 do
    local tag = SLOT_TAGS[slot]
    if tag and static.classes[tag] then
      out[#out + 1] = slot
    end
  end
  if static.classes.equippablemainhand or static.classes.equippabletwohand then
    for _, slot in ipairs(WEAPON_SLOTS) do
      out[#out + 1] = slot
    end
  end
  if static.classes.equippableoffhand then
    for _, slot in ipairs(OFFHAND_SLOTS) do
      out[#out + 1] = slot
    end
  end
  if static.classes.consumable or static.classes.blueprint then
    for _, slot in ipairs(QUICK_SLOTS) do
      out[#out + 1] = slot
    end
  end
  return out
end

--- @return The names of the paperdoll slots that an item can go into (the native GetEquipSlots; only the slots with a name).
function Swc.ItemEquipSlotNames(static)
  local out = {}
  for _, slot in ipairs(Swc.ItemEquipSlots(static)) do
    if NAME_OF_SLOT[slot] then
      out[#out + 1] = NAME_OF_SLOT[slot]
    end
  end
  return out
end

-- --------------------------------------------------------------------------------------------------- item objects

--- @return The rarity level of an item: the ForceRarity of its data, else its perk count (0 to 4). The backend sends no
--- rarity; the client computes it from the perk count (backend docs/systems/items-and-equipment.md).
local function rarity_of(static, perks)
  if static and (static.forceRarity or 0) > 0 then
    return static.forceRarity
  end
  return math.min(#(perks or {}), 4)
end

--- @return True when an item has a rarity (an item that can have perks).
local function uses_rarity(static)
  return static ~= nil and (static.canHavePerks or 0) == 1
end

-- ---------------------------------------------------------------------------------------------------- item stats

-- The constants of the gear score rule and of the armor rating (gamedata/itemrules.json: playerbaseattributes.pbadb).
local rules = swcui.load_json("gamedata/itemrules.json") or {}

--- @return The weapon stat row (weaponstats.tsv) and the armor stat row (armorstats.tsv) of an item (by its ItemStatsRef), or nil.
local function stat_rows(static)
  if not static or not static.itemStatsRef then
    return nil, nil
  end
  if static.weaponStats == nil then
    static.weaponStats = swcui.table_row("weaponstats", static.itemStatsRef) or false
    static.armorStats = swcui.table_row("armorstats", static.itemStatsRef) or false
  end
  return static.weaponStats or nil, static.armorStats or nil
end

local function one_decimal(value)
  return math.floor(value * 10 + 0.5) / 10
end

--- @return The base damage of a weapon at a gear score: one factor for each full interval above the lowest gear score, and a
--- smaller one for each full interval above the limit (client code at image RVA 0x3691470; Soulwarden Combat::weapon_base).
local function weapon_base(weapon, gearScore)
  local interval = rules.gearScoreInterval or 0
  if interval <= 0 then
    return weapon.baseDamage or 0
  end
  local rate = (weapon.gearScoreRate or 0) > 0 and weapon.gearScoreRate or (rules.gearScoreRate or 0)
  local lower = math.min(gearScore, rules.gearScoreLimit)
  local first = lower > rules.gearScoreMinimum and math.floor((lower - rules.gearScoreMinimum) / interval) or 0
  local second = gearScore > rules.gearScoreLimit and math.floor((gearScore - rules.gearScoreLimit) / interval) or 0
  return (weapon.baseDamage or 0) * (1 + rate) ^ first * (1 + rate * (rules.gearScoreSmallerRate or 0)) ^ second
end

--- @return The armor rating of one item for a scale factor (physical or elemental): a power of its gear score times the factor,
--- then times its rating factor, each to one decimal (client code at image RVA 0x5BFB400; Soulwarden Combat::armor).
local function armor_rating(row, factor, gearScore)
  if not factor or factor <= 0 or not rules.armorRatingExponent then
    return 0
  end
  return one_decimal((row.armorRating or 0) * one_decimal(gearScore ^ rules.armorRatingExponent * factor))
end

--- @return The core damage of a weapon for the local player: the base at the gear score times 1 + the level part (the level
--- damage for each level above 1; client code at image RVA 0x5BFB530, Soulwarden Combat::damage). The attribute part waits
--- for the attribute state of the read path (open point of stage P2.4): the part of the lowest attribute values is 0.
local function core_damage(static, gearScore)
  local weapon = stat_rows(static)
  if not weapon then
    return 0
  end
  local level = math.max(Swc.GetData("Hud.LocalPlayer.Progression.Level") or 1, 1)
  return weapon_base(weapon, gearScore) * (1 + (rules.levelDamage or 0) * (level - 1))
end

--- @return The weapon attributes of an item at a gear score (the stat lines of the item tooltips), or {} for no weapon.
local function weapon_attributes(static, gearScore)
  local weapon = stat_rows(static)
  if not weapon then
    return {}
  end
  -- isMagic (the label "core power" of the tooltip): the native rule is not known (open point); false gives "core damage".
  return {isValid = true, isMagic = false, baseDamage = weapon_base(weapon, gearScore), critChance = weapon.critChance or 0,
          critDamageMultiplier = weapon.critDamageMultiplier or 0, blockStaminaDamage = weapon.blockStaminaDamage or 0,
          blockStability = weapon.blockStability or 0, gatheringEfficiency = weapon.gatheringEfficiency or 0,
          physicalArmorRating = armor_rating(weapon, weapon.physicalArmor, gearScore),
          elementalArmorRating = armor_rating(weapon, weapon.elementalArmor, gearScore),
          primaryAttack = {}, alternateAttack = {}}
end

--- @return The armor attributes of an item at a gear score, or {} for no armor. The modifiers of the jewelry: gear score x
--- Scale...PerGS x TrinketBonus x 0.01 (client code 0x3636A50; Soulwarden docs/systems/gear-and-perks.md).
local function armor_attributes(static, gearScore)
  local _, armor = stat_rows(static)
  if not armor then
    return {}
  end
  local trinket = gearScore * (armor.trinketBonus or 0) * 0.01
  local out = {isValid = true, physicalArmorRating = armor_rating(armor, armor.physicalArmor, gearScore),
               elementalArmorRating = armor_rating(armor, armor.elementalArmor, gearScore),
               encumbranceModifier = armor.encumbrance or 0, equipLoadModifier = armor.equipLoad or 0,
               healthModifier = trinket * (armor.scaleHealthPerGS or 0), damageModifier = trinket * (armor.scaleDamagePerGS or 0),
               cooldownModifier = trinket * (armor.scaleCDRPerGS or 0), absorption = {}, resistances = {}}
  for column, value in pairs(armor) do
    if type(value) == "number" and value ~= 0 then
      local kind, name = column:sub(1, 3), column:sub(4)
      if kind == "ABS" then
        out.absorption[#out.absorption + 1] = {name = name, amount = value}
      elseif kind == "RES" then
        out.resistances[#out.resistances + 1] = {name = name, amount = value}
      end
    end
  end
  return out
end

--- An item descriptor (ItemDescriptor of the client): the item id (CRC-32), the quantity, the gear score, the perks.
Swc.ItemDescriptor = Swc.ValueClass({
  name = "ItemDescriptor",
  fields = {"itemId", "quantity", "gearScore", "perks"},
  methods = {
    IsValid = function(self) return self.itemId ~= nil and self.itemId ~= 0 end,
    GetItemKey = function(self)
      local static = Swc.ItemStatic(self.itemId)
      return static and static.id or ""
    end,
    GetDisplayName = function(self)
      local static = Swc.ItemStatic(self.itemId)
      return static and static.name or ""
    end,
    GetGearScore = function(self) return self.gearScore or 0 end,
    -- A pair: the gear score that applies, and the game mode that changes it (0: none).
    GetEffectiveGearScore = function(self) return {first = self.gearScore or 0, second = 0} end,
    GetWeaponAttributes = function(self) return weapon_attributes(Swc.ItemStatic(self.itemId), self.gearScore or 0) end,
    GetArmorAttributes = function(self) return armor_attributes(Swc.ItemStatic(self.itemId), self.gearScore or 0) end,
    GetAmmoAttributes = function() return {} end,
    GetItemDisplayName = function(self)
      local static = Swc.ItemStatic(self.itemId)
      return static and static.name or ""
    end,
    GetPerkCount = function(self) return #(self.perks or {}) end,
    GetPerksVectorSize = function(self) return #(self.perks or {}) end,
    -- The perk id CRC of a perk place (0 ...): ItemDataManagerBus GetStaticPerkData reads it.
    GetPerk = function(self, index)
      local perk = Swc.PerkStatic((self.perks or {})[(index or 0) + 1])
      return perk and perk.crc or 0
    end,
    GetGemPerk = function() return 0 end,
    SetPerks = function(self, perks) self.perks = perks end,
    SetPerksNoSort = function(self, perks) self.perks = perks end,
    GetPerkBucketCount = function() return 0 end,
    GetMaxPerksForGearScore = function() return 0 end,
    GetAttributeBonus = function() return 0 end,
    CanDisplayAllPerksInUI = function() return true end,
    HasGem = function() return false end,
    GetCoreDamageForOwner = function(self) return core_damage(Swc.ItemStatic(self.itemId), self.gearScore or 0) end,
    GetEstimatedConsumablePotency = function() return 0 end,
    MatchesDescriptorExactly = function(self, other)
      return other ~= nil and other.itemId == self.itemId and other.gearScore == self.gearScore
    end,
    HasItemClass = function(self, class) return Swc.ItemHasClass(Swc.ItemStatic(self.itemId), class) end,
    UsesRarity = function(self) return uses_rarity(Swc.ItemStatic(self.itemId)) end,
    GetRarityLevel = function(self) return rarity_of(Swc.ItemStatic(self.itemId), self.perks) end,
    GetTierNumber = function(self)
      local static = Swc.ItemStatic(self.itemId)
      return static and static.tier or 0
    end,
    GetLevelRequirement = function(self)
      local static = Swc.ItemStatic(self.itemId)
      return static and static.requiredLevel or 0
    end,
    -- The trade skill rank that the item needs: a pair (first: the trade skill, second: the rank); no rank: 0 and 0.
    GetTradeskillRankRequirement = function() return {first = 0, second = 0} end,
    GetInCombatRequirement = function() return false end,
    GetIsMountedRequirement = function() return false end,
    Clone = function(self)
      return Swc.ItemDescriptor.new({itemId = self.itemId, quantity = self.quantity, gearScore = self.gearScore, perks = self.perks})
    end,
    ApplyStaticPerkSettings = function() end,
  },
})

local function static_of(slot)
  return slot.static
end

--- An item slot (ItemSlot of the client): an item of the bag, of the paperdoll, or of an open loot bag, with the values of
--- the backend (instance, quantity, gear score, durability, flags, perks). An empty slot has no static data.
Swc.ItemSlot = Swc.ValueClass({
  name = "ItemSlot",
  fields = {"itemId", "slot", "quantity", "gearScore", "durability", "maxDurability", "instance", "flags", "perks",
            "container", "chrono", "action", "static"},
  methods = {
    IsValid = function(self) return self.static ~= nil and (self.quantity or 0) > 0 end,
    IsEquipped = function(self) return self.container == "paperdoll" and self.static ~= nil end,
    GetItemId = function(self) return self.static and self.static.crc or 0 end,
    GetItemName = function(self) return self.static and self.static.id or "" end,
    GetItemKey = function(self) return self.static and self.static.id or "" end,
    GetSlotId = function(self) return self.slot end,
    GetStackSize = function(self) return self.static and (self.quantity or 0) or 0 end,
    GetMaxStackSize = function(self) return self.static and self.static.maxStack or 0 end,
    GetItemDescriptor = function(self)
      return Swc.ItemDescriptor.new({itemId = self.static and self.static.crc or 0, quantity = self.quantity or 0,
                                     gearScore = self.gearScore or 0, perks = self.perks or {}})
    end,
    GetItemInstanceId = function(self) return Swc.Uuid.new({text = self.instance or "{00000000-0000-0000-0000-000000000000}"}) end,
    HasItemClass = function(self, class) return Swc.ItemHasClass(self.static, class) end,
    GetItemType = function(self) return self.static and self.static.itemType or "" end,
    GetTierNumber = function(self) return self.static and self.static.tier or 0 end,
    GetGearScore = function(self) return self.gearScore or 0 end,
    GetDurability = function(self) return self.durability or 0 end,
    GetDurabilityPercent = function(self)
      local maximum = self.maxDurability or 0
      return maximum > 0 and (self.durability or 0) / maximum or 1
    end,
    GetMaxDurability = function(self) return self.maxDurability or 0 end,
    -- The weight of the stack in tenths (catsection.luac adds it to the weight of its section).
    GetWeight = function(self) return self.static and (self.static.weight or 0) * (self.quantity or 0) or 0 end,
    GetIconPath = function(self) return self.static and self.static.icon or "" end,
    GetEquipSlots = function(self) return Swc.ItemEquipSlotNames(self.static) end,
    IsEquippable = function(self) return #Swc.ItemEquipSlots(self.static) > 0 end,
    CanEquipItem = function(self) return #Swc.ItemEquipSlots(self.static) > 0 end,
    CanEquipInSlot = function(self, slot)
      slot = paperdoll_slot_number(slot)
      for _, s in ipairs(Swc.ItemEquipSlots(self.static)) do
        if s == slot then
          return true
        end
      end
      return false
    end,
    IsBoundToPlayer = function(self) return (self.flags or 0) % 2 == 1 end,
    IsLocked = function(self) return math.floor((self.flags or 0) / 2) % 2 == 1 end,
    CanBeLocked = function(self) return self.static ~= nil and self.container == "bag" end,
    IsBindOnEquip = function(self) return self.static ~= nil and (self.static.bindOnEquip or 0) == 1 end,
    IsNonRemovableFromPlayer = function(self) return self.static ~= nil and (self.static.nonRemovable or 0) == 1 end,
    IsNonRemovableFromInventory = function(self) return self.static ~= nil and (self.static.nonRemovable or 0) == 1 end,
    IsConfirmDestroy = function(self) return self.static ~= nil and (self.static.confirmDestroy or 0) == 1 end,
    ShouldConfirmBeforeUse = function(self) return self.static ~= nil and (self.static.confirmBeforeUse or 0) == 1 end,
    CanUseItem = function(self) return self.static ~= nil and self.static.classes.consumable == true end,
    CanRepairItem = function(self) return false end,
    CanSalvageItem = function(self) return false end,
    UsesRarity = function(self) return uses_rarity(self.static) end,
    GetRarityLevel = function(self) return rarity_of(self.static, self.perks) end,
    IsCharmItemClass = function() return false end,
    HasCharmPerkSlot = function() return false end,
    GetResourceGemPerkForItem = function() return 0 end,
    GetDyeData = function() return {} end,
    GetAppearanceId = function() return 0 end,
    GetOnDeathDurabilityPenalty = function(self)
      return self.static and self.static.durabilityDamageOnDeath or 0
    end,
    GetContainerChrono = function(self) return self.chrono or 0 end,
    GetActionCausingSync = function(self) return self.action or 0 end,
    HasEntitlements = function() return false end,
    GetPerkActiveFlag = function() return true end,
    IsConsumable = function(self) return self.static ~= nil and self.static.classes.consumable == true end,
    GetHiResIconPath = function(self) return self.static and (self.static.hiresIcon or self.static.icon) or "" end,
    HasGem = function() return false end,
    HasPerkType = function() return false end,
    IsSellable = function() return false end,
    HasDyeData = function() return false end,
    GetGearScoreRangeMod = function() return 0 end,
    GetCoreDamage = function(self) return core_damage(self.static, self.gearScore or 0) end,
    -- The stat blocks of the tooltips (the ammunition: open point).
    GetWeaponAttributes = function(self) return weapon_attributes(self.static, self.gearScore or 0) end,
    GetArmorAttributes = function(self) return armor_attributes(self.static, self.gearScore or 0) end,
    GetAmmoAttributes = function() return {} end,
    MatchesDescriptorExactly = function(self, descriptor)
      return descriptor ~= nil and self.static ~= nil and descriptor.itemId == self.static.crc and descriptor.gearScore == self.gearScore
    end,
  },
})

--- @return A new item slot from the values of the host (an empty slot when the item id is empty).
function Swc.NewItemSlot(container, slot, values)
  values = values or {}
  local static = values.itemId and values.itemId ~= "" and Swc.ItemStatic(values.itemId) or nil
  return Swc.ItemSlot.new({itemId = static and static.crc or 0, slot = slot, quantity = static and (values.quantity or 1) or 0,
                           gearScore = values.gearScore or 0, durability = values.durability or 0,
                           maxDurability = values.maxDurability or 0, instance = values.instance, flags = values.flags or 0,
                           perks = values.perks or {}, container = container, chrono = values.chrono or 0,
                           action = values.action or 0, static = static})
end

-- --------------------------------------------------------------------------------------------------- the player

-- The ids of the item components of the local player (Hud.LocalPlayer.HudComponent.*: the inventory is the Container of the
-- bag; the paperdoll id is Swc.ids.paperdoll). Ids of the runtime.
Swc.ids.inventory = EntityId("6005173470066573573")
Swc.ids.interactor = EntityId("6005173470066573574")
Swc.ids.itemRepair = EntityId("6005173470066573575")
Swc.ids.crafting = EntityId("6005173470066573576")
Swc.ids.vitals = EntityId("6005173470066573577")
Swc.ids.eventNotification = EntityId("6005173470066573578")

Swc.player.bag = {count = 0, slots = {}}       -- bag slot (0 ...) -> ItemSlot
Swc.player.paperdoll = {}                       -- paperdoll slot -> ItemSlot
Swc.player.coin = 0                             -- hundredths of a coin
-- The weight limits of the bag (gamedata/encumbrance.json: javelindata_encumbrancelimits, row Player; tenths).
local limits = swcui.load_json("gamedata/encumbrance.json") or {}
Swc.player.encumbranceMax = limits.EncumbranceBaseMax or 2000
Swc.player.encumbranceWarningPercent = limits.EncumbranceWarningPercent or 80
Swc.player.equipLoadMax = limits.EquipLoadBaseMax or 500
Swc.player.fullWhenEncumbered = limits.FullWhenEncumbered or 1
Swc.player.lookingThroughLoadout = false

local function empty_slot(container, slot)
  return Swc.NewItemSlot(container, slot, nil)
end

--- @return The item slot of a paperdoll slot (an empty slot when it has no item).
function Swc.PaperdollSlot(slot)
  return Swc.player.paperdoll[slot] or empty_slot("paperdoll", slot)
end

--- @return The bag weight in tenths (the backend rule: the weight of each bag item times its quantity; the paperdoll is
--- not in it).
function Swc.BagWeight()
  local total = 0
  for i = 0, Swc.player.bag.count - 1 do
    local slot = Swc.player.bag.slots[i]
    if slot and slot.static then
      total = total + (slot.static.weight or 0) * (slot.quantity or 0)
    end
  end
  return total
end

local function update_weight()
  local weight = Swc.BagWeight()
  local used = 0
  for i = 0, Swc.player.bag.count - 1 do
    local slot = Swc.player.bag.slots[i]
    if slot and slot.static then
      used = used + 1
    end
  end
  Swc.SetData("Hud.LocalPlayer.Inventory.CurrentEncumbrance", weight)
  Swc.SetData("Hud.LocalPlayer.Inventory.EncumbranceMax", Swc.player.encumbranceMax)
  Swc.SetData("Hud.LocalPlayer.Equipment.EquipLoadMax", Swc.player.equipLoadMax)
  Swc.SetData("Hud.LocalPlayer.Inventory.TotalInventorySlotsUsed", used)
end

--- The size of the bag of the backend (the slots of the Container; the vector never shrinks in the world).
function Swc.HostBagSize(count)
  local bag = Swc.player.bag
  if count == bag.count then
    return
  end
  for i = count, bag.count - 1 do
    bag.slots[i] = nil
  end
  local old = bag.count
  bag.count = count
  for i = old, count - 1 do
    bag.slots[i] = bag.slots[i] or empty_slot("bag", i)
  end
  Swc.Notify("ContainerEventBus", Swc.ids.inventory, "OnContainerChanged")
  update_weight()
end

local function values_of(itemId, quantity, gearScore, durability, maxDurability, instance, flags, chrono, action, perks)
  local list = {}
  for perk in string.gmatch(perks or "", "[^,]+") do
    list[#list + 1] = perk
  end
  return {itemId = itemId, quantity = quantity, gearScore = gearScore, durability = durability, maxDurability = maxDurability,
          instance = instance ~= "" and instance or nil, flags = flags, chrono = chrono, action = action, perks = list}
end

--- One bag slot of the backend: the item id text (empty: no item), the quantity, the gear score, the durability, the
--- instance id ({...} text), the flags (bit 0 bound, bit 1 locked), the chrono, the sync cause, and the perk ids ("a,b").
function Swc.HostBagSlot(slot, itemId, quantity, gearScore, durability, maxDurability, instance, flags, chrono, action, perks)
  if slot >= Swc.player.bag.count then
    Swc.HostBagSize(slot + 1)
  end
  local item = Swc.NewItemSlot("bag", slot, values_of(itemId, quantity, gearScore, durability, maxDurability, instance, flags,
                                                      chrono, action, perks))
  Swc.player.bag.slots[slot] = item
  -- The slot id is a number, as in the list of dynamicitemlist.luac (UpdateLists queues the numbers 0 ...).
  Swc.Notify("ContainerEventBus", Swc.ids.inventory, "OnSlotUpdate", slot, item, action or 0)
  Swc.Notify("ContainerEventBus", Swc.ids.inventory, "OnContainerSlotChanged", slot, item, action or 0)
  update_weight()
end

--- One paperdoll slot of the backend (the values of Swc.HostBagSlot).
function Swc.HostPaperdollItem(slot, itemId, quantity, gearScore, durability, maxDurability, instance, flags, chrono, action, perks)
  if itemId == "" then
    itemId = nil
  end
  local item = Swc.NewItemSlot("paperdoll", slot, values_of(itemId, quantity, gearScore, durability, maxDurability, instance,
                                                            flags, chrono, action, perks))
  Swc.player.paperdoll[slot] = item.static and item or nil
  Swc.player.equipment[slot] = item.static and item.static.key or nil
  Swc.Notify("PaperdollEventBus", Swc.ids.paperdoll, "OnPaperdollSlotUpdate", slot, item, action or 0)
end

--- One paperdoll slot with only an item id (the look; the fixtures and the HUD tests).
function Swc.HostEquipment(slot, itemId)
  if itemId == "" then
    itemId = nil
  end
  if Swc.player.equipment[slot] == (itemId and string.lower(itemId) or nil) then
    return
  end
  Swc.HostPaperdollItem(slot, itemId or "", 1, 0, 0, 0, "", 0, 0, 0, "")
end

--- The coin of the backend (hundredths).
function Swc.HostCoin(coin)
  Swc.player.coin = coin
  Swc.SetData("Hud.LocalPlayer.Currency.Amount", coin)
end

--- The weight limit of the bag (tenths; EncumbranceBaseMax of the game data).
function Swc.HostEncumbrance(maximum)
  Swc.player.encumbranceMax = maximum
  update_weight()
end

-- --------------------------------------------------------------------------------------------------- the buses

local function container_of(id)
  if id == Swc.ids.inventory then
    return Swc.player.bag
  end
  return Swc.containers and Swc.containers[tostring(id)] or nil
end

local function slot_of(id, slotId)
  local container = container_of(id)
  local index = tonumber(slotId)
  if not container or index == nil then
    return empty_slot("bag", index or -1)
  end
  return container.slots[index] or empty_slot(container == Swc.player.bag and "bag" or "loot", index)
end

Swc.Define("ContainerRequestBus", "Event", {
  GetSlot = slot_of,
  GetSlotRef = slot_of,
  GetNumSlots = function(id)
    local container = container_of(id)
    return container and container.count or 0
  end,
  GetNumEmptySlots = function(id)
    local container = container_of(id)
    local n = 0
    for i = 0, (container and container.count or 0) - 1 do
      if not container.slots[i] or not container.slots[i].static then
        n = n + 1
      end
    end
    return n
  end,
  GetSlotIdByItemInstanceId = function(id, instance)
    local container = container_of(id)
    local text = instance and instance.ToString and instance:ToString() or tostring(instance)
    for i = 0, (container and container.count or 0) - 1 do
      local slot = container.slots[i]
      if slot and slot.instance == text then
        return i
      end
    end
    return -1
  end,
  IsPlayerContainer = function(id) return id == Swc.ids.inventory end,
  GetCurrentEncumbrance = function(id) return id == Swc.ids.inventory and Swc.BagWeight() or 0 end,
  GetMaximumEncumbrance = function(id) return id == Swc.ids.inventory and Swc.player.encumbranceMax or 0 end,
  IsEncumbered = function(id) return id == Swc.ids.inventory and Swc.BagWeight() > Swc.player.encumbranceMax end,
  IsFullWhenEncumbered = function() return false end,
  GetEncumbranceWarningPercent = function() return Swc.player.encumbranceWarningPercent end,
  -- FullWhenEncumbered of the weight limits (2 for the player; newinventory.luac multiplies the maximum with it).
  GetFullWhenEncumberedModifier = function(id) return id == Swc.ids.inventory and Swc.player.fullWhenEncumbered or 1 end,
  HasSpaceForItems = function() return true end,
  IsAtItemCapacity = function() return false end,
  CanTransferItems = function() return true end,
  CheckItemClassCompatibility = function() return true end,
  ContainsSalvageableItem = function() return false end,
  GetItemCount = function(id, itemCrc)
    local container = container_of(id)
    local n = 0
    for i = 0, (container and container.count or 0) - 1 do
      local slot = container.slots[i]
      if slot and slot.static and slot.static.crc == itemCrc then
        n = n + (slot.quantity or 0)
      end
    end
    return n
  end,
  GetFilteredContent = function(id, class)
    local container = container_of(id)
    local out = {}
    for i = 0, (container and container.count or 0) - 1 do
      local slot = container.slots[i]
      if slot and slot.static and Swc.ItemHasClass(slot.static, class) then
        out[#out + 1] = slot
      end
    end
    return out
  end,
})
Swc.Handled("ContainerEventBus")

Swc.Define("PaperdollRequestBus", "Event", {
  GetSlot = function(_, slot) return Swc.PaperdollSlot(paperdoll_slot_number(slot)) end,
  GetItemDescriptor = function(_, slot) return Swc.PaperdollSlot(paperdoll_slot_number(slot)):GetItemDescriptor() end,
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
  -- The level and the other requirements of a slot: the backend does not check them (protocol notes 4.11); the client
  -- does. The emulation: the level of the item against the level of the character.
  HasLevelRequirementForSlot = function() return true end,
  HasAchievementRequirementForSlot = function() return true end,
  HasTradeskillRankRequirementForSlot = function() return true end,
  GetLevelRequirementForSlot = function() return 0 end,
  GetTradeskillRankRequirementForSlot = function() return 0 end,
  SlotBlockedByCooldown = function() return false end,
  IsSlotLockedInCombat = function() return false end,
  CanEquipItemFromSlot = function() return true end,
  GetPowerLevel = function() return 0 end,
  GetEquipLoadRatio = function() return 0 end,
  GetEquipLoadCategoryForWeight = function() return 0 end,
  GetLoadouts = function() return {} end,
  GetNumLoadouts = function() return 0 end,
  GetActiveLoadoutIndex = function() return 0 end,
  IsLoadoutSwitchThrottled = function() return false end,
})
Swc.Handled("PaperdollEventBus")

-- The item data (ItemDataManagerBus): the static data of an item id CRC (gamedata/items.tsv), the weapon data of the weapon
-- bar (gamedata/weapons.json).
local function static_data(itemCrc)
  local static = Swc.ItemStatic(itemCrc)
  if not static then
    return {IsValid = function() return false end}
  end
  local data = {}
  for k, v in pairs(static) do
    data[k] = v
  end
  data.key = static.id
  data.id = static.crc
  data.tierNumber = static.tier
  data.displayName = static.name
  data.IsValid = function() return true end
  data.HasDurabilityEnabled = function() return (static.durability or 0) > 0 end
  return data
end

Swc.Define("ItemDataManagerBus", "Broadcast", {
  -- The weapon data: the Mannequin tags and the equip ability (weapons.json), the attribute scaling (weaponstats.tsv; the line
  -- "scales with" of the tooltips).
  GetWeaponData = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    local weapon = static and Swc.GameData().weapons[static.key]
    local equip = weapon and weapon.equipAbility
    local stats = stat_rows(static) or {}
    return {mannequinTags = weapon and weapon.mannequinTags or {}, tierNumber = weapon and weapon.tier or 0,
            equipAbilityId = equip and swcui.crc32(string.lower(equip)) or nil,
            strengthScaling = stats.scalingStrength or 0, dexterityScaling = stats.scalingDexterity or 0,
            intelligenceScaling = stats.scalingIntelligence or 0, focusScaling = stats.scalingFocus or 0}
  end,
  GetItemData = static_data,
  GetIconPath = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static and static.icon or ""
  end,
  GetHiresIconPath = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static and (static.hiresIcon or static.icon) or ""
  end,
  GetDisplayName = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static and static.name or ""
  end,
  IsEquippable = function(itemCrc) return #Swc.ItemEquipSlots(Swc.ItemStatic(itemCrc)) > 0 end,
  IsFFARestricted = function() return false end,
  -- The consumable data of an item (consumables.tsv): the item id CRC (id), the cooldown id and duration, the effects.
  GetConsumableData = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    local row = static and swcui.table_row("consumables", static.key)
    local data = {IsValid = function() return row ~= nil end}
    for k, v in pairs(row or {}) do
      data[k] = v
    end
    if row then
      data.id = static.crc
      data.key = static.id
    end
    return data
  end,
  -- The static data of a perk (perks.tsv): displayName, description, perkType, iconPath, ... and IsValid.
  GetStaticPerkData = function(perkCrc)
    local perk = Swc.PerkStatic(perkCrc)
    local data = {IsValid = function() return perk ~= nil end}
    for k, v in pairs(perk or {}) do
      data[k] = v
    end
    if perk then
      data.key = perk.id
      data.id = perk.crc
    end
    return data
  end,
  GetNotDroppableNotificationOverride = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static and static.notDroppableNotification or ""
  end,
})

-- The scaling tier of an attribute scaling value (itemrules.json: javelindata_weapontiers): the letter of the highest tier whose
-- lowest value the scaling reaches, or "" below the lowest tier.
Swc.Define("WeaponTierDataManagerBus", "Broadcast", {
  GetTier = function(scaling)
    for _, tier in ipairs(rules.weaponTiers or {}) do
      if (scaling or 0) >= tier[2] then
        return tier[1]
      end
    end
    return ""
  end,
})

-- --------------------------------------------------------------------------------------------------- the requests

local function request(text)
  Swc.hostRequests[#Swc.hostRequests + 1] = text
  Swc.Note("item request: " .. text)
end

local function instance_of(slot)
  return slot and slot.instance or "{00000000-0000-0000-0000-000000000000}"
end

Swc.Define("LocalPlayerUIRequestsBus", "Broadcast", {
  -- equipmentcommon.luac: the bag slot, the paperdoll slot name, the quantity (the full stack), the source container.
  EquipItem = function(bagSlot, slotName, quantity)
    local slot = Swc.player.bag.slots[bagSlot]
    local target = paperdoll_slot_number(slotName)
    if not slot or not slot.static or target == nil then
      return
    end
    request(string.format("equip %d %d %d %s", bagSlot, target, quantity or slot.quantity, instance_of(slot)))
  end,
  -- The paperdoll slot, the bag slot (-1: the first free slot), the quantity, the target container.
  UnequipItem = function(paperdollSlot, bagSlot, quantity)
    request(string.format("unequip %d %d %d", paperdollSlot, bagSlot or -1, quantity or 1))
  end,
  -- The slot of the drag, the slot of the drop (equipmentitemdroptarget.luac), the loadout index.
  PaperdollSwapItems = function(from, to)
    from, to = paperdoll_slot_number(from), paperdoll_slot_number(to)
    if from == nil or to == nil then
      return
    end
    request(string.format("swap %d %d %s %s", to, from, instance_of(Swc.player.paperdoll[to]), instance_of(Swc.player.paperdoll[from])))
  end,
  InventoryUseItem = function(bagSlot) request(string.format("use %d", bagSlot)) end,
  InventoryDropItem = function(bagSlot, quantity)
    local slot = Swc.player.bag.slots[bagSlot]
    if slot and slot.static then
      request(string.format("drop %d %d %s", bagSlot, quantity or slot.quantity, instance_of(slot)))
    end
  end,
  PaperdollDropItem = function(paperdollSlot, quantity)
    request(string.format("dropequipped %d %d", paperdollSlot, quantity or 1))
  end,
  -- The inventory screen: the native flag "looking through loadout" (the server has its copy; Tab and the bag button
  -- of the HUD set it). The player component tells the scripts.
  SetIsLookingThroughLoadout = function(looking)
    looking = looking and true or false
    if Swc.player.lookingThroughLoadout == looking then
      return
    end
    Swc.player.lookingThroughLoadout = looking
    Swc.Notify("PlayerComponentNotificationsBus", Swc.ids.player, "OnIsLookingThroughLoadoutChanged", looking)
  end,
  GetMaximumEncumbrance = function() return Swc.player.encumbranceMax end,
  IsEncumbered = function() return Swc.BagWeight() > Swc.player.encumbranceMax end,
  IsImmobilizedByEncumbrance = function() return Swc.BagWeight() > Swc.player.encumbranceMax * 1.5 end,
  ShouldWarnAboutEncumbrance = function() return false end,
  MeetsWeaponRequirements = function() return true end,
  IsFlyoutMenuEnabled = function() return true end,
  IsItemTransferEnabled = function() return true end,
  InteractPanelClosed = function() request("panelclosed") end,
})
Swc.Handled("PlayerComponentNotificationsBus")

-- --------------------------------------------------------------------------------------------------- other buses

-- The synced wall clock of the server (timehelperfunctions.luac ServerNow): the wall clock of the runtime.
Swc.Define("LocalPlayerComponentRequestBus", "Broadcast", {
  GetCurrentSyncedWallClockTime = function() return WallClockTimePoint:Now() end,
})
-- The depth of field of the camera (the inventory blurs the world): the client has no depth of field (the look).
Swc.Define("JavelinCameraRequestBus", "Broadcast", {
  GetCurrentDepthOfFieldDistance = function() return 0 end,
  GetCurrentDepthOfFieldBlur = function() return 0 end,
  SetDepthOfField = function() end,
})
-- The afflictions (gamedata/afflictions.json: javelindata_afflictions): the meters of the HUD and of the equipment screen. The
-- backend sends no affliction state (open point): each meter starts with no damage and hides (Swc.StartAfflictions).
local afflictions = swcui.load_json("gamedata/afflictions.json") or {}
local afflictionById, afflictionByKey = {}, {}
for _, row in ipairs(afflictions) do
  afflictionById[row.id] = row
  afflictionByKey[row.key] = row
end

local function hex_color(text)
  local a, r, g, b = string.match(text or "", "^(%x%x)(%x%x)(%x%x)(%x%x)$")
  if not a then
    return Color(1, 1, 1, 1)
  end
  return Color(tonumber(r, 16) / 255, tonumber(g, 16) / 255, tonumber(b, 16) / 255, tonumber(a, 16) / 255)
end

local function affliction_field(key, field, default)
  local row = afflictionByKey[key]
  return row and row[field] or default
end

Swc.Define("DamageDataBus", "Broadcast", {
  GetAfflictionRowKeys = function()
    local out = {}
    for i, row in ipairs(afflictions) do
      out[i] = row.id
    end
    return out
  end,
  GetAfflictionRowKey = function(id)
    local row = afflictionById[id]
    return row and row.key or ""
  end,
  GetAfflictionIcon = function(key) return affliction_field(key, "icon", "") end,
  GetAfflictionColor = function(key) return hex_color(affliction_field(key, "color", "")) end,
  GetAfflictionTooltipText = function(key) return affliction_field(key, "tooltip", "") end,
  GetAfflictedTooltipText = function(key) return affliction_field(key, "afflictedTooltip", "") end,
})

-- The equipment sets (javelindata_equipmentsets): the id of the set of an item (the CRC-32 of its EquipmentSetId, 0: no set).
-- Open point: the perks and the names of the sets; a set has no bonus lines in the tooltips.
Swc.Define("EquipmentSetDataManagerRequestBus", "Broadcast", {
  GetEquipmentSetIdFromItemId = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static and static.equipmentSetId and swcui.crc32(string.lower(static.equipmentSetId)) or 0
  end,
  GetEquipmentSetPerks = function() return {} end,
  GetEquipmentSetName = function() return "" end,
  GetPerkThreshold = function() return 0 end,
})

--- The start values of the afflictions in the data layer (Hud.LocalPlayer.Afflictions.<IntID>.*): the maximum of the row, no
--- damage, not afflicted. The meters hide with these values.
function Swc.StartAfflictions()
  for _, row in ipairs(afflictions) do
    local base = "Hud.LocalPlayer.Afflictions." .. tostring(row.id)
    Swc.SetData(base .. ".Max", row.max)
    Swc.SetData(base .. ".Amount", 0)
    Swc.SetData(base .. ".IsAfflicted", false)
    Swc.SetData(base .. ".IsGlobal", row.global)
  end
end
-- The trade skill and mount skill ranks (the CategoricalProgression state is not in the read path: open point).
Swc.Define("CategoricalProgressionRequestBus", "Event", {
  GetRank = function() return 0 end,
  GetProgression = function() return 0 end,
})

-- The other values that the inventory screens read. A value of the client that the backend does not give.
Swc.Define("ClientCurrencyRequestBus", "Broadcast", {
  -- The coin cap of the backend (server/rep/states/currency_state.cpp: 100,000,000 hundredths).
  GetWalletCap = function() return 100000000 end,
})
Swc.Define("PlayerDataManagerBus", "Broadcast", {
  -- The level that unlocks the free gear sets: the gear sets are off (javelin.gear-set-storage-enabled), no level.
  GetPlayerDisplayLevelUnlockFreeGearSets = function() return 1000 end,
})
Swc.Define("GameModeParticipantComponentRequestBus", "Event", {
  IsInAnyGameMode = function() return false end,
  IsInInventoryBypassGameMode = function() return false end,
})
Swc.Define("StatusEffectsRequestBus", "Event", {
  HasStatusEffect = function() return false end,
  IsPreviewingWithMeshOverride = function() return false end,
})
Swc.Define("UiInteractorComponentRequestsBus", "Event", {
  IsInCommittedInteraction = function() return false end,
})
Swc.Define("AchievementRequestBus", "Event", {
  IsAchievementUnlocked = function() return false end,
})
Swc.Define("ItemDataManagerBus", "Broadcast", {
  GetAmmoType = function() return 0 end,
})
Swc.Define("LyShineManagerBus", "Broadcast", {
  GetHDR10OutputEnabled = function() return false end,
  GetControllerIconPath = function() return "" end,
})
-- The camera of the inventory (JavCameraControllerRequestBus OverrideCameraState): open point (the camera track).
Swc.Define("JavCameraControllerRequestBus", "Broadcast", {
  OverrideCameraState = function() end,
  ClearAllStateOverrides = function() end,
  SetCameraEnableMouseInput = function() end,
})
Swc.Define("JavelinCameraRequestBus", "Broadcast", {
  EnableDepthOfField = function() end,
})

-- The weight values exist from the start (the client computes them from its container: 0 with no bag).
update_weight()

-- The repair (ItemRepairRequestBus): the backend does not repair (roadmap 3.7e of the backend): no job, no cost.
local function no_repair()
  return {hasJobs = false, repairParts = 0, coin = 0, repairKits = {}}
end
Swc.Define("ItemRepairRequestBus", "Event", {
  GetCostForRepairAllEquipment = no_repair,
  GetCostForRepairAllInventory = no_repair,
  GetCostForRepairAll = no_repair,
  GetCostForRepairAllItemClass = no_repair,
  CanRepairItem = function() return false end,
  GetRepairKitsForSlot = function() return {} end,
  CanItemGearScoreBeUpgraded = function() return false end,
  ShowItemGearScoreUpgradeOption = function() return false end,
  -- No upgrade option shows (ShowItemGearScoreUpgradeOption false): the commands section only compares the range.
  GetMinimumGSToUpgrade = function() return 0 end,
  GetMaximumGSToUpgrade = function() return 0 end,
})

--- A refused request of the equipment (message 3626): the client shows its paperdoll slots again (handler 0x687BB60, protocol
--- notes 4.11): each slot goes to the scripts again with its current item.
function Swc.HostEquipFail()
  Swc.Note("the server refused a request of the equipment (3626)")
  for slot = 0, 49 do
    Swc.Notify("PaperdollEventBus", Swc.ids.paperdoll, "OnPaperdollSlotUpdate", slot, Swc.PaperdollSlot(slot), 0)
  end
end

-- The lazy getters of the static item data (staticitemdatamanager.luac converters) and the other item queries.
local function field(name, default)
  return function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    local value = static and static[name]
    if value == nil then
      return default
    end
    return value
  end
end
Swc.Define("ItemDataManagerBus", "Broadcast", {
  GetEquipSlots = function(itemCrc) return Swc.ItemEquipSlotNames(Swc.ItemStatic(itemCrc)) end,
  GetTierNumber = field("tier", 0),
  GetBaseGearScore = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static and (static.gearScoreOverride or static.minGearScore) or 0
  end,
  IsRepairable = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static ~= nil and (static.repairable or 0) == 1
  end,
  IsSalvageable = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static ~= nil and (static.salvageable or 0) == 1
  end,
  CanHavePerks = function(itemCrc)
    local static = Swc.ItemStatic(itemCrc)
    return static ~= nil and (static.canHavePerks or 0) == 1
  end,
  GetRepairRecipe = field("repairRecipe", ""),
  GetCraftingRecipe = function() return "" end,
  GetJsonString = function() return "" end,
  GetTooltipLayoutValue = function() return "" end,
  GetRequiredExpansion = function() return 0 end,
  GetCatSection = function() return 0 end,
  GetIngredientCategories = function() return {} end,
  IsItemDisabledByConfig = function() return false end,
  IsItemDisabledBySchedule = function() return false end,
  -- The gathering type of a tool (GatheringTypes of the weapon definitions); "None" for a different item.
  GetWeaponGatheringType = function(itemCrc)
    local weapon = stat_rows(Swc.ItemStatic(itemCrc))
    return weapon and weapon.gatheringTypes or "None"
  end,
  CanSalvageResources = function() return false end,
})

-- The salvage and the repair of the action menu: the backend does neither (backend roadmap 3.7d, 3.7e): no output, no
-- amounts.
Swc.Define("GameEventRequestBus", "Broadcast", {
  GetSalvageCoinAmount = function() return 0 end,
  GetSalvageAzothAmount = function() return 0 end,
  GetSalvageFactionTokenAmount = function() return 0 end,
})
Swc.Define("CraftingRequestBus", "Broadcast", {
  GetSalvageOutputFromDescriptor = function() return {} end,
})
Swc.Define("RecipeDataManagerBus", "Broadcast", {
  GetSalvageDataFromLootTable = function() return {} end,
  IsLootTableSalvage = function() return false end,
  GetRepairDustQuantity = function() return 0 end,
  GetRepairGoldQuantity = function() return 0 end,
})
Swc.Define("ItemRepairRequestBus", "Event", {
  GetRepairRecipeScript = function() return {} end,
})
Swc.Define("PaperdollRequestBus", "Event", {
  GetSlotIdByItemInstanceId = function(_, instance)
    local text = instance and instance.ToString and instance:ToString() or tostring(instance)
    for slot, item in pairs(Swc.player.paperdoll) do
      if item.instance == text then
        return slot
      end
    end
    return ePaperDollSlotTypes_Invalid
  end,
  IsItemInGSSExcludedPaperdollSlot = function() return false end,
})
-- The armor modifiers of the vitals (the equipment screen): the backend sends none in the read path (0).
Swc.Define("VitalsComponentRequestBus", "Event", {
  GetVitalsArmorModifierValueById = function() return 0 end,
})
Swc.Define("UiCanvasBus", "Event", {
  SetIsPositionalInputSupported = function() end,
  QueueHoverInteractableReset = function() end,
})
Swc.Define("ItemDataManagerBus", "Broadcast", {
  GetResourceData = function() return {IsValid = function() return false end} end,
})
