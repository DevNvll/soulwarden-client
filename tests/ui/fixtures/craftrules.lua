-- The crafting rules of the study research/crafting-ui.md (AST-231 part 4), no screen: the preview lines of the great axe (Primary
-- Attack, Blocking Stability, Required Level) at the gear scores 600 and 520, the craft mod catalog of the axe and of the skinning
-- knife, and the two list rules (the world tags, IsRecipeKnown).
Swc.PlayerSpawned()
local function crc(text) return swcui.crc32(string.lower(text)) end
local function preview(item, gearScore)
  local descriptor = ItemDescriptor()
  descriptor.itemId = crc(item)
  descriptor.gearScore = gearScore
  local weapon = descriptor:GetWeaponAttributes()
  local attack = weapon.primaryAttack and weapon.primaryAttack[#weapon.primaryAttack]
  return attack and math.floor(attack.amount) or -1, weapon.blockStability or -1, descriptor:GetLevelRequirement() + 1,
    attack and attack.type or ""
end
local a600, s600, l600, t600 = preview("2hGreatAxeT5", 600)
equal(a600, 251, "the great axe at 600: Primary Attack 251")
equal(s600, 27, "the great axe at 600: Blocking Stability 27")
equal(l600, 60, "the great axe at 600: Required Level 60")
equal(t600, "Slash", "the great axe: the damage type Slash")
local a520, s520, l520 = preview("2hGreatAxeT5", 520)
equal(a520, 223, "the great axe at 520: Primary Attack 223")
equal(s520, 27, "the great axe at 520: Blocking Stability 27")
equal(l520, 48, "the great axe at 520: Required Level 48")
-- The craft mods: the static catalog of the study (compat-data.json).
local axe = ItemDataManagerBus.Broadcast.GetCompatiblePerkResourceIds(crc("2hGreatAxeT5"), {})
equal(#axe, 51, "the craft mods of the great axe T5")
equal(#ItemDataManagerBus.Broadcast.GetCompatiblePerkResourceIds(crc("1hSkinningKnifeT2"), {}), 7, "the craft mods of the skinning knife T2")
local has = {}
for _, id in ipairs(axe) do has[id] = true end
check(has[crc("2HAxeCharmT5")], "the axe takes the great axe charm 2HAxeCharmT5")
check(has[crc("IronwoodResinT1")], "the axe takes IronwoodResinT1")
check(has[crc("GemSocketPinsT1")], "the axe takes the gem socket pins")
check(not has[crc("PerkCharm_GreatAxe_Maelstrom")], "a resource with a SocketType is not a craft mod")
equal(ItemDataManagerBus.Broadcast.GetDisplayPerkIdFromResource(crc("2HAxeCharmT5")), crc("PerkID_Ability_GreatAxe_Maelstrom"),
  "the display perk of 2HAxeCharmT5")
-- A selected charm reserves its labels (Skill+2hAxe): the same charm family is no longer a candidate.
local with = ItemDataManagerBus.Broadcast.GetCompatiblePerkResourceIds(crc("2hGreatAxeT5"), {crc("2HAxeCharmT5")})
local still = false
for _, id in ipairs(with) do still = still or id == crc("2HAxeCharmT5") end
check(not still and #with < #axe, "a selected charm removes the mods with its labels (" .. #with .. " left)")
-- The list rules: a recipe with world tags needs a tag of the world; IsRecipeKnown with and with no achievement.
local function listed(id)
  for _, r in ipairs(RecipeDataManagerBus.Broadcast.GetRecipes()) do
    if r == id then return true end
  end
  return false
end
check(not listed("EtherealDust_Wood"), "no world tag: EtherealDust_Wood is not listed")
Swc.config["javelin.world-tags"] = "SeasonalServer"
check(listed("EtherealDust_Wood"), "the world tag SeasonalServer: EtherealDust_Wood is listed")
Swc.config["javelin.world-tags"] = nil
Swc.HostRanks(string.format("%d 0 0", crc("Woodworking")))
check(CraftingRequestBus.Broadcast.IsRecipeKnown("TimberT2", true), "TimberT2 (Woodworking 0) is known")
check(not CraftingRequestBus.Broadcast.IsRecipeKnown("TimberT3", true), "TimberT3 (level 50) is not known at Woodworking 0")
check(not CraftingRequestBus.Broadcast.IsRecipeKnown("TimberT3", false), "the second argument false tests the rank too")
-- AST-234 (research recipe-known.md): all recipes are in the data; learning is the achievement bit of the state 3210 (the index of
-- javelindata_achievements: 16_Side_19 is 4456); a pattern recipe has its display items (the station script hides it with none).
check(listed("Pattern_OPR_Greataxe"), "a recipe with no bKnownByDefault is in the list of the data")
Swc.HostRanks(string.format("%d 200 0", crc("Stonecutting")))
Swc.HostOwnership("", "", true)
equal(Swc.AchievementIndex("16_Side_19"), 4456, "the AchievementIndex of 16_Side_19")
check(not AchievementRequestBus.Event.IsAchievementUnlocked(nil, crc("16_Side_19")), "no bit: 16_Side_19 is not unlocked")
check(not CraftingRequestBus.Broadcast.IsRecipeKnown("RuneStoneFormT1", true), "no bit: RuneStoneFormT1 is not known")
Swc.HostOwnership("4456", "", true)
check(AchievementRequestBus.Event.IsAchievementUnlocked(nil, crc("16_Side_19")), "the bit 4456: 16_Side_19 is unlocked")
check(CraftingRequestBus.Broadcast.IsRecipeKnown("RuneStoneFormT1", true), "the bit 4456: RuneStoneFormT1 is known")
check(#RecipeDataManagerBus.Broadcast.GetRecipeDisplayIngredients("Pattern_OPR_Greataxe") > 0, "the pattern recipe has its display items")
-- AST-234: Swc.CanEquip uses the native level rule: the great axe at 600 needs level 60 (zero-based 59), at 640 level 61.
local axeStatic = Swc.ItemStatic(crc("2hGreatAxeT5"))
Swc.HostProgression(60, 0)
check(Swc.CanEquip(axeStatic, 600), "level 60 equips the great axe at 600")
check(not Swc.CanEquip(axeStatic, 640), "level 60 does not equip the great axe at 640 (Required Level 61)")
Swc.HostProgression(59, 0)
check(not Swc.CanEquip(axeStatic, 600), "level 59 does not equip the great axe at 600")
Swc.HostProgression(70, 0)
-- AST-234: the values of the craft mod tooltip (GetGenericLocalizedDescription: the formulas with the perk multiplier 1).
local grit = ItemDataManagerBus.Broadcast.GetStaticPerkData(crc("PerkID_Weapon_DmgAgainstGrit")):GetGenericLocalizedDescription()
note("the grit text: " .. grit)
equal(grit, "+4.5% damage against targets with active Grit.", "the formula of the grit perk")
local maelstrom = ItemDataManagerBus.Broadcast.GetStaticPerkData(crc("PerkID_Ability_GreatAxe_Maelstrom")):GetGenericLocalizedDescription()
note("the maelstrom text: " .. maelstrom)
check(maelstrom:find("by 3%", 1, true) ~= nil and maelstrom:find("for 8s", 1, true) ~= nil, "the formulas of the maelstrom charm")
