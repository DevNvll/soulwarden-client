-- The owner states of the read path (stage MVP-A, Linear AST-214; docs/UI.md "The owner states"). The host
-- (USwcUiSubsystem::FeedOwnerStates) sends the values of the backend states at each change:
--   * the attributes (slot 27): the base value, the bonus, and the spent points of each CharacterAttributeType, and the free
--     points. The equipment screen (equipmentstatrow.luac UpdateStatValues) reads AttributeRequestBus GetAttributeBaseLevel and
--     GetAttributeBonusLevel when Hud.LocalPlayer.Attributes.<Name>.Spent changes;
--   * the ranks of the weapon masteries and the trade skills (slot 65): CategoricalProgressionRequestBus GetRank and
--     GetProgression of a progression id (the CRC-32 of the lower-case id text). An id that is not in the state has the rank 0.
--     The points of Azoth_Currency are the Azoth balance (Hud.LocalPlayer.Currency.AzothAmount; the cap: HostAzothCap);
--   * the free points of each weapon tree (slot 67);
--   * the heart rune charge (slot 48): Hud.LocalPlayer.Vitals.ChargePercent, 0 to 1 (heartgemslot.luac fills its icon);
--   * the achievements of the character (slot 66) and the entitlements of the account (slot 19): the expansion test of the client
--     reads the achievement Expansion_2023_Unlock (image RVA 0x684B340);
--   * the abilities with a cast plan of the backend (Combat/SwcBackendSupport.h): weaponquickslots.luac shows each other ability
--     disabled (its own SetAbilitySlotIsEnabled), and its key press plays the disabled flash of the slot (OnTryUseDisabledAbility).
--   * the heart rune key G (stage MVP-E): bit 450 only for Ability_Rune_Detonate with a full charge (see Swc.RuneSupported).

Swc.owner = Swc.owner or {}
local O = Swc.owner

-- The CharacterAttributeType values (enums.json; backend world/entity.h namespace attribute) and the names of the data paths.
local ATTRIBUTE_NAMES = {[0] = "Constitution", [1] = "Focus", [2] = "Strength", [3] = "Dexterity", [4] = "Intelligence"}

-- The achievement Expansion_2023_Unlock: its index in the achievement bits (backend progression-tables.json `achievements`), and the
-- CRC-32 values that the scripts can use for it.
local EXPANSION_ACHIEVEMENT = 11979
local EXPANSION_ACHIEVEMENT_KEYS = {[swcui.crc32("expansion_2023_unlock")] = EXPANSION_ACHIEVEMENT,
                                    [swcui.crc32("Expansion_2023_Unlock")] = EXPANSION_ACHIEVEMENT}

local function number_set(text)
  local out = {}
  for value in string.gmatch(text or "", "%d+") do
    out[tonumber(value)] = true
  end
  return out
end

--- The abilities that the backend plays: a text of their ability ids (the CRC-32 of the lower-case id text).
function Swc.HostSupportedAbilities(text)
  O.supported = number_set(text)
  Swc.dataBarsChanged = true
end

--- @return True when the backend has a cast plan for the ability (no list from the host: each ability).
function Swc.AbilitySupported(key)
  return O.supported == nil or O.supported[tonumber(key)] == true
end

-- The quick slots of the weapons: an ability with no rule of the backend is disabled, as a silenced or mounted character's abilities
-- (the slot dims; a press flashes it and sends nothing: the host driver sends no bit of that key).
Swc.scriptHooks["lyshineui.hud2024.weaponquickslots.weaponquickslots"] = function(base)
  local enabled = base.GetIsAbilityEnabled
  if type(enabled) ~= "function" then
    return
  end
  base.GetIsAbilityEnabled = function(self, info)
    if info and info.abilityId and not Swc.AbilitySupported(info.abilityId) then
      return false
    end
    return enabled(self, info)
  end
end

--- The attributes: the base values (5), the bonuses (5), the spent points (5), each in the order of CharacterAttributeType, then
--- the free points.
function Swc.HostAttributes(...)
  local v = {...}
  O.attributes = O.attributes or {}
  for type = 0, 4 do
    O.attributes[type] = {base = v[type + 1] or 0, bonus = v[type + 6] or 0, spent = v[type + 11] or 0}
  end
  O.unspent = v[16] or 0
  Swc.SetData("Hud.LocalPlayer.Attributes.UnspentPoints", O.unspent)
  for type = 0, 4 do
    local a = O.attributes[type]
    local path = "Hud.LocalPlayer.Attributes." .. ATTRIBUTE_NAMES[type]
    Swc.SetData(path .. ".Base", a.base)
    Swc.SetData(path .. ".Bonus", a.bonus)
    -- The observer of the equipment screen: a new value makes the rows read the bus again. The data layer tells an observer only a
    -- change, thus a new bonus with the same points sets the path through a value that no state has (the rows read the bus two
    -- times in the same frame).
    if Swc.GetData(path .. ".Spent") == a.spent then
      Swc.SetData(path .. ".Spent", -1)
    end
    Swc.SetData(path .. ".Spent", a.spent)
  end
end

local function attribute(type)
  return O.attributes and O.attributes[tonumber(type) or -1]
end

Swc.Define("AttributeRequestBus", "Event", {
  GetAttributeBaseLevel = function(_, type)
    local a = attribute(type)
    return a and a.base or 0
  end,
  GetAttributeBonusLevel = function(_, type)
    local a = attribute(type)
    return a and a.bonus or 0
  end,
  GetAttributeLevel = function(_, type)
    local a = attribute(type)
    return a and (a.base + a.bonus) or 0
  end,
  GetUnspentAttributePoints = function() return O.unspent or 0 end,
})

-- The Azoth balance: the points of the categorical progression Azoth_Currency (backend world/skills.h kAzothCurrency); the inventory
-- (newinventory.luac) reads Hud.LocalPlayer.Currency.AzothAmount and AzothMax.
local AZOTH_CURRENCY = swcui.crc32("azoth_currency")

--- The cap of the Azoth balance (the backend copy: crafting-data.json `currencies`).
function Swc.HostAzothCap(cap)
  Swc.SetData("Hud.LocalPlayer.Currency.AzothMax", cap or 0)
end

--- The ranks of the weapon masteries and the trade skills: "id rank points" lines (a new state replaces the map).
function Swc.HostRanks(text)
  local ranks = {}
  for id, rank, points in string.gmatch(text or "", "(%d+) (%d+) (%d+)") do
    ranks[tonumber(id)] = {rank = tonumber(rank), points = tonumber(points)}
  end
  O.ranks = ranks
  local azoth = ranks[AZOTH_CURRENCY]
  Swc.SetData("Hud.LocalPlayer.Currency.AzothAmount", azoth and azoth.points or 0)
end

--- @return The rank entry of a progression id (a number, or a text: its CRC-32 of the lower-case text).
local function rank_of(id)
  if type(id) == "string" then
    id = swcui.crc32(string.lower(id))
  end
  return O.ranks and O.ranks[tonumber(id) or -1]
end

Swc.Define("CategoricalProgressionRequestBus", "Event", {
  GetRank = function(_, id)
    local entry = rank_of(id)
    return entry and entry.rank or 0
  end,
  GetProgression = function(_, id)
    local entry = rank_of(id)
    return entry and entry.points or 0
  end,
})

--- The free points of each weapon tree: "pool free" lines.
function Swc.HostPointPools(text)
  local pools = {}
  for pool, free in string.gmatch(text or "", "(%d+) (%d+)") do
    pools[tonumber(pool)] = tonumber(free)
  end
  O.pools = pools
end

--- The heart rune charge in percent (0 to 100).
function Swc.HostCharge(percent)
  O.charge = math.max(0, math.min(100, percent or 0))
  Swc.SetData("Hud.LocalPlayer.Vitals.ChargePercent", O.charge / 100)
end

-- The heart rune (paperdoll slot 45; backend world/runes.h): the key G (the action rune_ability of the action maps) sends the input bit
-- 450 (protocol notes, live check I6). The backend activates only the equip ability Ability_Rune_Detonate, with a full charge. The
-- rule of the client (stage MVP-E):
--   * a rune with an other equip ability (Bile Bomb) never shows the state CHARGED of heartgemslot.luac (no ready glow): the fill
--     shows the charge of the server, and a press of G plays the red flash of the script (as with no full charge) and sends nothing;
--   * a supported rune with a full charge: the press sends bit 450 and the script plays its flash (RuneAbilityFlash). The icon empties
--     at the next charge state of the server (slot 48), not at the press: a refused press does not show a false use.
local RUNE_SLOT = 45
local RUNE_BIT = 450
local RUNE_ACTIVATION = "ability_rune_detonate"

--- @return The equip ability of the heart rune in the paperdoll (the lower-case id text), or nil with no rune.
function Swc.RuneAbility()
  local slot = Swc.player and Swc.player.paperdoll and Swc.player.paperdoll[RUNE_SLOT]
  local static = slot and slot.static
  local weapon = static and Swc.GameData().weapons[static.key]
  local equip = weapon and weapon.equipAbility
  return equip and equip ~= "" and string.lower(equip) or nil
end

--- @return True when the backend activates the equipped heart rune (no rune: false).
function Swc.RuneSupported()
  return Swc.RuneAbility() == RUNE_ACTIVATION
end

Swc.scriptHooks["lyshineui.hud2024.weaponquickslots.heartgemslot"] = function(base)
  local update, onAction = base.UpdateHeartGemState, base.OnCryAction
  if type(update) ~= "function" or type(onAction) ~= "function" then
    return
  end
  -- The states of the script (its own constants: DEPLETED 1, CHARGING 2, CHARGED 3).
  local CHARGED, CHARGING = base.HEARTGEM_STATE_CHARGED or 3, base.HEARTGEM_STATE_CHARGING or 2
  base.UpdateHeartGemState = function(self, state)
    if state == CHARGED and not Swc.RuneSupported() then
      state = CHARGING
    end
    return update(self, state)
  end
  base.OnCryAction = function(self, action, value)
    if action == "rune_ability" then
      local ready = self.heartGemState == CHARGED and not self.isSilenced
      if ready and Swc.RuneSupported() then
        Swc.hostRequests[#Swc.hostRequests + 1] = string.format("press %d", RUNE_BIT)
        Swc.Note("heart rune: G sends bit 450 (" .. tostring(Swc.RuneAbility()) .. ", charge " .. tostring(O.charge) .. ")")
      else
        Swc.Note("heart rune: G sends nothing (rune " .. tostring(Swc.RuneAbility()) .. ", charge " .. tostring(O.charge)
          .. (Swc.RuneSupported() and "" or ", no rule of the backend") .. ")")
      end
    end
    return onAction(self, action, value)
  end
end

-- The equipment screen values (stage MVP-E; research mvp-e-static.md).
--
-- The gear score (PaperdollRequestBus.GetPowerLevel(paperdoll, 0); retail 0x6828F20): A = trunc(sum(w x GS) + 0.5) over the armor slots
-- 0 to 4 with the fixed weights head 0.20, chest 0.35, hands 0.15, legs 0.20, feet 0.10 (an empty slot gives 0); W = the integer mean of
-- the unlocked main-hand options (an empty unlocked option counts in the divisor; a weapon with IsShieldCompatible and a shield in its
-- off-hand slot counts floor((weapon + shield) / 2)); J = the integer mean of the jewelry slots 5, 6, 7; GS = trunc(float32(float32(0.35 W
-- + 0.45 A) + 0.20 J)). The floor path 7 x (level + 1) of the native code is not applied (its trigger is not known).
local ARMOR_WEIGHTS = {[0] = 0.20, [1] = 0.35, [2] = 0.15, [3] = 0.20, [4] = 0.10}
-- The main-hand options that count: 24 and 25. Option 3 (slot 26) is locked (an inference: the retail HUD has two weapon slots).
local WEAPON_OPTIONS = {24, 25}
-- The off-hand item of a main-hand option: the wire slots 27 to 29 (protocol notes 4.11). The study names slot 10 (an open point).
local OFFHAND_OF = {[24] = 27, [25] = 28, [26] = 29}
local JEWELRY_SLOTS = {5, 6, 7}

local function gear_score_of(slot)
  local item = Swc.player and Swc.player.paperdoll and Swc.player.paperdoll[slot]
  return item and item.static and (item.gearScore or 0) or 0
end

--- @return The gear score of the equipment screen (see above).
function Swc.PowerLevel()
  local armor = 0
  for slot, weight in pairs(ARMOR_WEIGHTS) do
    armor = armor + weight * gear_score_of(slot)
  end
  armor = math.floor(armor + 0.5)
  local weapons = 0
  for _, slot in ipairs(WEAPON_OPTIONS) do
    local score = gear_score_of(slot)
    local item = Swc.player.paperdoll[slot]
    local weapon = item and item.static and Swc.GameData().weapons[item.static.key]
    local shield = gear_score_of(OFFHAND_OF[slot])
    if weapon and weapon.shield and shield > 0 then
      score = math.floor((score + shield) / 2)
    end
    weapons = weapons + score
  end
  weapons = math.floor(weapons / #WEAPON_OPTIONS)
  local jewelry = 0
  for _, slot in ipairs(JEWELRY_SLOTS) do
    jewelry = jewelry + gear_score_of(slot)
  end
  jewelry = math.floor(jewelry / #JEWELRY_SLOTS)
  -- Lua has doubles, the native code float32: a total that is a whole number within a float32 step can differ by 1.
  return math.floor(0.35 * weapons + 0.45 * armor + 0.20 * jewelry)
end

-- The armor (PHYS and ELEM of the screen): the retail screen reads a native cache whose full equation is not proved. The client shows
-- the armor of the backend rule, the value that changes the damage (world/combat.cpp Combat::armor_of and world/statuses.cpp
-- Statuses::values): the sum of the item armor of the slots 0 to 4, times max(1 + part, 0). The part: for each status of the owner its
-- PhysicalArmor or ElementalArmor x potency x stacks; a sum goes to the first category of the status with a limit for the column
-- (ArmorFortify +2.0, ArmorRend -0.7), else it is free (gamedata/ownerstats.json, the backend copy). Not in the client: the armor parts
-- of the perks of the gear (world/gear.cpp; the gear of the current character has none) and the attribute thresholds.
local owner_stats

--- The statuses of the owner (slot 44, the full map): "index potency stacks" lines.
function Swc.HostStatuses(text)
  local list = {}
  for index, potency, stacks in string.gmatch(text or "", "(%d+) ([%d%.%-e]+) (%d+)") do
    list[#list + 1] = {index = tonumber(index), potency = tonumber(potency), stacks = tonumber(stacks)}
  end
  O.statuses = list
end

--- The in-use state of a consumable (the Use action of the character): its paperdoll slot, 0 at the end. The native code emits
--- Hud.LocalPlayer.Equipment.SetConsumeSlot at the start and CancelConsumeSlot at the end (0x6925570, 0x67A3800); consumablequickslots.luac
--- then dims the four keys and checks them again.
function Swc.HostConsumeSlot(slot)
  slot = tonumber(slot) or 0
  if slot ~= 0 then
    Swc.SetData("Hud.LocalPlayer.Equipment.SetConsumeSlot", slot)
  else
    Swc.SetData("Hud.LocalPlayer.Equipment.CancelConsumeSlot", true)
  end
end

--- @return The armor part of the statuses of the owner for a column (PhysicalArmor or ElementalArmor).
local function status_part(column)
  owner_stats = owner_stats or swcui.load_json("gamedata/ownerstats.json") or {}
  local rows, categories = owner_stats.statuses or {}, owner_stats.categories or {}
  local free, limited = 0, {}
  for _, status in ipairs(O.statuses or {}) do
    local row = rows[tostring(status.index)]
    local value = row and row.values and row.values[column]
    if value and value ~= 0 then
      value = value * (status.potency or 1) * (status.stacks or 1)
      local placed = false
      for _, name in ipairs(row.categories or {}) do
        local limit = categories[name] and categories[name][column]
        if limit then
          local entry = limited[name] or {sum = 0, limit = limit}
          entry.sum = entry.sum + value
          limited[name] = entry
          placed = true
          break
        end
      end
      if not placed then
        free = free + value
      end
    end
  end
  for _, entry in pairs(limited) do
    free = free + (entry.limit > 0 and math.min(entry.sum, entry.limit) or math.max(entry.sum, entry.limit))
  end
  return free
end

--- @return The armor of the screen for "physical" or "elemental" (see above).
function Swc.ArmorRating(kind)
  local sum = 0
  for slot = 0, 4 do
    sum = sum + Swc.ItemArmorRating(Swc.player.paperdoll[slot], kind)
  end
  local part = status_part(kind == "physical" and "PhysicalArmor" or "ElementalArmor")
  return sum * math.max(1 + part, 0)
end

--- The achievements of the character and the entitlements of the account: the indexes of their set bits; the mark "received".
function Swc.HostOwnership(achievements, entitlements, received)
  O.achievements = number_set(achievements)
  O.entitlements = number_set(entitlements)
  O.entitlementsReceived = received and true or false
end

--- @return True when the character owns the expansion Expansion2023 (the achievement bit). With no Achievement state yet the
--- grant policy of the backend (each account owns the expansion: backend world/ownership.h) applies.
function Swc.OwnsExpansion()
  if O.achievements == nil then
    return true
  end
  return O.achievements[EXPANSION_ACHIEVEMENT] == true
end

Swc.Define("AchievementRequestBus", "Event", {
  IsAchievementUnlocked = function(_, key)
    local index = EXPANSION_ACHIEVEMENT_KEYS[tonumber(key) or -1]
    return index ~= nil and O.achievements ~= nil and O.achievements[index] == true
  end,
})
