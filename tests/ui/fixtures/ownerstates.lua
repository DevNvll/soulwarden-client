-- The owner states fixture (stage MVP-A, Linear AST-214; docs/UI.md "The owner states"): the host calls of the read path into the
-- original HUD scripts and the buses of the screens. The abilities with no rule of the backend show disabled in the weapon quick
-- slots (the dimmer of weaponabilityslot.luac), the rune charge fills Hud.LocalPlayer.Vitals.ChargePercent, and the buses give the
-- attributes, the ranks, and the expansion of the states. Pictures: ownerstates-sword, ownerstates-rapier.
local HUD = 2702338936
screen("LyShineUI/HUD2024/Vitals/Vitals2024.uicanvas", 12, true)
screen("LyShineUI/HUD2024/WeaponQuickslots/WeaponQuickslots.uicanvas", 10, true)
screen("LyShineUI/HUD2024/EditMode/HudEditMode.uicanvas", 15, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(4869, 4869, 100, 100, 100, 100, false)
Swc.HostProgression(70, 0)

-- The weapons of the fight HUD fixture: the sword (Reverse Stab, Whirling Blade, Leaping Strike) and the rapier (Flurry, Flourish,
-- Riposte); the bars are the CRC-32 values of the lower-case ids.
Swc.HostEquipment(24, "1hSwordT52")
Swc.HostEquipment(45, "RuneBileBombT4A")
Swc.HostEquipment(25, "artifact_set1_1hrapier")
Swc.HostAbilityBar(2169986235, 617453373, 241886633, 2855651066)
Swc.HostAbilityBar(3907802902, 2977457251, 248953190, 3873560184)
Swc.HostActiveWeapon(24, false)
frames(1.0)

--- @return The ability slot scripts of the active weapon and the count of them with the dimmer (a disabled slot).
local function active_slots()
  local slots, dimmed = 0, 0
  for _, list in pairs(Swc.components) do
    for _, instance in ipairs(list) do
      if string.find(string.lower(instance.__swcScript or ""), "weaponabilityslot", 1, true) and instance.isActiveWeapon then
        slots = slots + 1
        if instance.isDimmerVisible then
          dimmed = dimmed + 1
        end
      end
    end
  end
  return slots, dimmed
end

local slots, dimmed = active_slots()
check(slots == 3 and dimmed == 0, "no list of the host: the three sword slots are enabled (" .. slots .. " slots, " .. dimmed .. " dimmed)")

-- The list of the host (Combat/SwcBackendSupport.h): no sword ability, Riposte of the rapier.
local supported = {"ability_greataxe_charge", "ability_greataxe_maelstorm", "ability_greataxe_gravitywell", "ability_warhammer_wreckingball",
                   "ability_warhammer_shockwave", "ability_warhammer_pathofdestiny", "ability_rapier_riposte", "ability_greataxe_reap",
                   "ability_greataxe_executioner", "ability_greataxe_whirlwind", "ability_warhammer_mightygavel",
                   "ability_warhammer_armorbreaker", "ability_warhammer_clearout", "ability_bow_penetratingshot",
                   "ability_bow_splintershot", "ability_bow_rapidshot"}
local text = {}
for _, id in ipairs(supported) do
  text[#text + 1] = tostring(swcui.crc32(id))
end
Swc.HostSupportedAbilities(table.concat(text, " "))
Swc.HostCommit()
frames(1.0)
slots, dimmed = active_slots()
check(slots == 3 and dimmed == 3, "the sword: its three abilities have no rule, three slots dimmed (" .. dimmed .. ")")
picture("ownerstates-sword")
Swc.HostActiveWeapon(25, false)
frames(1.0)
slots, dimmed = active_slots()
check(slots == 3 and dimmed == 2, "the rapier: Flurry and Flourish dimmed, Riposte enabled (" .. dimmed .. " of " .. slots .. ")")
picture("ownerstates-rapier")

-- The heart rune charge (slot 48): the percent of the state, 0 to 1 in the data layer.
Swc.HostCharge(50)
near(Swc.GetData("Hud.LocalPlayer.Vitals.ChargePercent"), 0.5, 1e-6, "the charge 50 percent fills half of the rune icon")

-- The attributes (slot 27): base CON 155 FOC 5 STR 305 DEX 45 INT 5, bonus CON 60 FOC 10 STR 35, spent, no free point.
Swc.HostAttributes(155, 5, 305, 45, 5, 60, 10, 35, 0, 0, 150, 0, 300, 40, 0, 0)
equal(AttributeRequestBus.Event.GetAttributeBaseLevel(nil, CharacterAttributeType_Strength), 305, "STR base 305")
equal(AttributeRequestBus.Event.GetAttributeBonusLevel(nil, CharacterAttributeType_Strength), 35, "STR bonus 35")
equal(AttributeRequestBus.Event.GetAttributeBaseLevel(nil, CharacterAttributeType_Constitution), 155, "CON base 155")
equal(AttributeRequestBus.Event.GetAttributeBonusLevel(nil, CharacterAttributeType_Focus), 10, "FOC bonus 10")
equal(Swc.GetData("Hud.LocalPlayer.Attributes.Strength.Spent"), 300, "the data path of the spent STR points")
-- A food: a new bonus with the same points (the observers of the path get the value again).
Swc.HostAttributes(155, 5, 305, 45, 5, 60, 10, 55, 0, 0, 150, 0, 300, 40, 0, 0)
equal(AttributeRequestBus.Event.GetAttributeBonusLevel(nil, CharacterAttributeType_Strength), 55, "the food: STR bonus 55")
equal(Swc.GetData("Hud.LocalPlayer.Attributes.Strength.Spent"), 300, "the spent points stay 300")

-- The ranks (slot 65): Mining 150, the great axe mastery 20; an id with no entry has the rank 0.
Swc.HostRanks(swcui.crc32("mining") .. " 150 2000000\n" .. swcui.crc32("greataxe") .. " 20 1512000\n")
equal(CategoricalProgressionRequestBus.Event.GetRank(nil, swcui.crc32("mining")), 150, "Mining rank 150")
equal(CategoricalProgressionRequestBus.Event.GetProgression(nil, swcui.crc32("greataxe")), 1512000, "the great axe points")
equal(CategoricalProgressionRequestBus.Event.GetRank(nil, swcui.crc32("harvesting")), 0, "Harvesting rank 0")
Swc.HostRanks(swcui.crc32("greataxe") .. " 20 1512000\n")
equal(CategoricalProgressionRequestBus.Event.GetRank(nil, swcui.crc32("mining")), 0, "a new map replaces the old map: Mining 0")
-- The Azoth balance: the points of Azoth_Currency, the cap of the crafting data of the backend.
Swc.HostAzothCap(2500)
Swc.HostRanks(swcui.crc32("azoth_currency") .. " 0 2500\n")
equal(Swc.GetData("Hud.LocalPlayer.Currency.AzothAmount"), 2500, "the Azoth balance 2500")
equal(Swc.GetData("Hud.LocalPlayer.Currency.AzothMax"), 2500, "the Azoth cap 2500")

-- The expansion: the achievement Expansion_2023_Unlock (index 11979) of the character.
check(PlayerComponentRequestsBus.Event.IsExpansionOwnerScript(nil, "Expansion2023"), "no Achievement state yet: the grant policy (owner)")
Swc.HostOwnership("11979", "47 223 1484", true)
check(PlayerComponentRequestsBus.Event.IsExpansionOwnerScript(nil, "Expansion2023"), "the achievement bit: the character owns Expansion2023")
check(AchievementRequestBus.Event.IsAchievementUnlocked(nil, swcui.crc32("expansion_2023_unlock")), "IsAchievementUnlocked of the expansion")
Swc.HostOwnership("", "", true)
check(not PlayerComponentRequestsBus.Event.IsExpansionOwnerScript(nil, "Expansion2023"), "no bit: the character does not own Expansion2023")
check(PlayerComponentRequestsBus.Event.IsExpansionOwnerScript(nil, "None"), "the base game stays owned")
Swc.HostOwnership("11979", "47 223 1484", true)
