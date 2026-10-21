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
