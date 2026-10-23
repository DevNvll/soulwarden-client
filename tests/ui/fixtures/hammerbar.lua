-- The war hammer bar of the HUD (AST-270): the item 2hWarhammerT5 has ItemStatsRef 2HHammerT5, its weapon row is 2HhammerT5; the
-- HUD game data joins them with no case (swc_convert.ui convert_gamedata). The weapon slots of WeaponQuickslots then get the
-- three bound abilities of WarHammerAbilityTable. Also the empty listener list of a HUD canvas at a change of the HUD state.
-- Picture: hammerbar.
local HUD = 2702338936
screen("LyShineUI/HUD2024/WeaponQuickslots/WeaponQuickslots.uicanvas", 10, true)
screen("LyShineUI/Compass/Compass.uicanvas", 14, true)
screen("LyShineUI/HUD2024/Reticles/ReticlesCanvas.uicanvas", 9, true)
screen("LyShineUI/HUD2024/EditMode/HudEditMode.uicanvas", 15, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()

local weapon = Swc.GameData().weapons["2hwarhammert5"]
check(weapon ~= nil and weapon.masteryTable == "WarHammerAbilityTable", "2hWarhammerT5 has the table WarHammerAbilityTable")

-- The bow and the hammer (MainHandOption1 24, MainHandOption2 25) with the bar of the hammer of the play test log: Wrecking Ball,
-- Shockwave, Path of Destiny.
local HAMMER = swcui.crc32("warhammerabilitytable")
Swc.HostEquipment(24, "2hBowT5")
Swc.HostEquipment(25, "2hWarhammerT5")
Swc.HostAbilityBar(HAMMER, 4248586536, 4268742667, 1009807133)
Swc.HostActiveWeapon(25, false)
Swc.HostCommit()
frames(1.0)
local paperdoll = Swc.GetData and Swc.GetData("Hud.LocalPlayer.HudComponent.PaperdollEntityId")
local slot = PaperdollRequestBus.Event.GetSlot(paperdoll, 25)
local data = CharacterAbilityRequestBus.Event.GetActiveAbilityDataByItemSlot(EntityId(), slot)
equal(CharacterAbilityRequestBus.Event.GetAbilityTableFromItemSlot(EntityId(), slot), HAMMER, "the ability table of slot 25")
check(data[1] ~= nil and data[2] ~= nil and data[3] ~= nil, "three abilities for the hammer in slot 25")
local names = {}
for i = 1, 3 do
  names[i] = data[i] and data[i].displayName or "nil"
end
equal(table.concat(names, ","), "@WreckingBall,@Shockwave,@PathOfDestiny", "the bound abilities in their order")
picture("hammerbar")

-- A change of the HUD state with the empty listener lists of the compass and the reticles: no Lua error.
LyShineManagerBus.Broadcast.SetState(3576764016)   -- a state that hides the HUD parts
frames(0.3)
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.3)
equal(swcui.error_count(), 0, "no Lua error")
