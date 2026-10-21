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

-- The background of an ability icon (stage MVP-E): abilitiescommon.luac GetBackgroundPath with the CRC-32 of the UICategory of the
-- ability data. Flurry and Flourish are Attack (abilities_bg2), Riposte is Defense (abilities_bg3).
local backgrounds = {}
for _, list in pairs(Swc.components) do
  for _, instance in ipairs(list) do
    if string.find(string.lower(instance.__swcScript or ""), "weaponabilityslot", 1, true) and instance.isActiveWeapon then
      backgrounds[#backgrounds + 1] = tostring(UiImageBus.Event.GetSpritePathname(instance.Properties.Bg))
    end
  end
end
table.sort(backgrounds)
equal(table.concat(backgrounds, " "), "lyshineui/images/icons/abilities/abilities_bg2.dds lyshineui/images/icons/abilities/abilities_bg2.dds "
  .. "lyshineui/images/icons/abilities/abilities_bg3.dds", "the backgrounds of the rapier slots: two Attack, one Defense")

-- The heart rune charge (slot 48): the percent of the state, 0 to 1 in the data layer.
Swc.HostCharge(50)
near(Swc.GetData("Hud.LocalPlayer.Vitals.ChargePercent"), 0.5, 1e-6, "the charge 50 percent fills half of the rune icon")

-- The heart rune key G (stage MVP-E): bit 450 only for Ability_Rune_Detonate with a full charge. The rune of the fixture is Bile Bomb.
local rune
for _, list in pairs(Swc.components) do
  for _, instance in ipairs(list) do
    if string.find(string.lower(instance.__swcScript or ""), "heartgemslot", 1, true) then
      rune = instance
    end
  end
end
check(rune ~= nil, "the heart rune slot of the HUD")
--- @return The host requests of one press of G.
local function press_rune()
  Swc.hostRequests = {}
  rune:OnCryAction("rune_ability", 1)
  return table.concat(Swc.hostRequests, " | ")
end
equal(tostring(Swc.RuneAbility()), "ability_rune_bilebomb", "the equip ability of the fixture rune")
Swc.HostCharge(100)
frames(0.5)
check(rune.heartGemState ~= rune.HEARTGEM_STATE_CHARGED, "Bile Bomb with a full charge: no state CHARGED (no ready glow)")
equal(press_rune(), "", "Bile Bomb: G sends nothing")
Swc.HostEquipment(45, "RuneDetonateT3C")
Swc.HostCharge(99)
frames(0.5)
equal(press_rune(), "", "Detonate with charge 99: G sends nothing")
Swc.HostCharge(100)
frames(0.5)
equal(rune.heartGemState, rune.HEARTGEM_STATE_CHARGED, "Detonate with a full charge: the state CHARGED")
equal(press_rune(), "press 450", "Detonate with a full charge: G sends bit 450")
Swc.HostEquipment(45, "RuneBileBombT4A")
Swc.HostCharge(50)
frames(0.5)
Swc.hostRequests = {}

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

-- The equipment screen values (stage MVP-E, research mvp-e-static.md). The gear of the current character of the user: armor 725 (feet
-- 800), amulet and ring 725, earring 800, bow 800 and rapier 725. A = trunc(725 x 0.9 + 800 x 0.1 + 0.5) = 733, W = (800 + 725) / 2 =
-- 762, J = (725 + 800 + 725) / 3 = 750, GS = trunc(0.35 x 762 + 0.45 x 733 + 0.20 x 750) = 746.
local gear = {[0] = {"LightHead_HercyneRaid_CraftedSchematic", 725}, [1] = {"LightChest_HercyneRaid_CraftedSchematic", 725},
              [2] = {"MediumHands_HercyneRaid_CraftedSchematic", 725}, [3] = {"LightLegs_HercyneRaid_CraftedSchematic", 725},
              [4] = {"Artifact_Set1_MediumFeet", 800}, [5] = {"AmuletT5_HercyneRaid_CraftedSchematic", 725},
              [6] = {"Artifact_Set1_Earring1", 800}, [7] = {"RingT5_HercyneRaid_CraftedShard", 725},
              [24] = {"Artifact_Set1_2hBow", 800}, [25] = {"Rapier_DoubloonShopNamedT5", 725}}
for slot, entry in pairs(gear) do
  Swc.HostPaperdollItem(slot, entry[1], 1, entry[2], 100, 100, "", 0, 0, 0, "")
end
equal(PaperdollRequestBus.Event.GetPowerLevel(nil, 0), 746, "the gear score of the equipment screen: 746")
Swc.HostPaperdollItem(25, "", 0, 0, 0, 0, "", 0, 0, 0, "")
equal(PaperdollRequestBus.Event.GetPowerLevel(nil, 0), 619, "no weapon in slot 25 (it counts in the divisor): W = 400, the gear score 619")
Swc.HostPaperdollItem(25, "Rapier_DoubloonShopNamedT5", 1, 725, 100, 100, "", 0, 0, 0, "")

-- The armor: the item sum of the slots 0 to 4 times max(1 + the part of the statuses, 0). The dust GemstoneDustT5 (index 269: ElementalArmor
-- +0.35, ArmorFortify), the Detonate Dominator status (index 1589: -0.5 for both, ArmorRend, limit -0.7).
local PHYS, ELEM = 3609800712, 4218213731
Swc.HostStatuses("")
local phys = VitalsComponentRequestBus.Event.GetVitalsArmorModifierValueById(nil, PHYS)
local elem = VitalsComponentRequestBus.Event.GetVitalsArmorModifierValueById(nil, ELEM)
check(phys > 0 and elem > 0, "the armor of the five armor items (PHYS " .. phys .. ", ELEM " .. elem .. ")")
Swc.HostStatuses("269 1.000000 1\n")
near(VitalsComponentRequestBus.Event.GetVitalsArmorModifierValueById(nil, ELEM), elem * 1.35, 1e-3, "the dust: ELEM x 1.35")
near(VitalsComponentRequestBus.Event.GetVitalsArmorModifierValueById(nil, PHYS), phys, 1e-3, "the dust: PHYS unchanged")
Swc.HostStatuses("269 1.000000 1\n1589 1.000000 1\n")
near(VitalsComponentRequestBus.Event.GetVitalsArmorModifierValueById(nil, PHYS), phys * 0.5, 1e-3, "Dominator: PHYS x 0.5")
near(VitalsComponentRequestBus.Event.GetVitalsArmorModifierValueById(nil, ELEM), elem * 0.85, 1e-3, "the dust and Dominator: ELEM x 0.85")
Swc.HostStatuses("1589 1.000000 2\n")
near(VitalsComponentRequestBus.Event.GetVitalsArmorModifierValueById(nil, PHYS), phys * 0.3, 1e-3, "two stacks of the rend: the limit -0.7")
Swc.HostStatuses("")
