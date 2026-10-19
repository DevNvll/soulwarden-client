-- The reticle of the HUD (stage P3.H; docs/UI.md "The fight HUD"): the native part of the reticle data for the original
-- canvas lyshineui/hud2024/reticles/reticlescanvas (reticlesmanager.luac).
--   * Hud.LocalPlayer.ReticleToShow: the CRC-32 of the lower-case ReticleName of the active weapon (the weapon data,
--     gamedata/weapons.json "reticle": melee, spear, bow, pistol, rifle, blunderbuss); the manager maps it to its reticle
--     (melee 3488054177, bow 2551600555, rifle 3151297237: the crosshair; an other name: the melee reticle). No weapon: nil
--     (the manager shows its default, the melee reticle).
--   * The colors of the options Controls.ReticleColor and Controls.ReticleTargetColor: the saved options of the retail client
--     (white; 1, 0.443, 0.443 on a target). The static reader of the program does not decode a color field.
--   * Hud.LocalPlayer.HasReticleTarget: false (open point: the target rule of the native code and ReticleRayCastDistance).
-- The manager shows the reticle also while the weapon is sheathed: the option AlwaysShowReticle is on (the program default).

local Swc = Swc

Swc.SetData("Hud.LocalPlayer.Options.Controls.ReticleColor", Color(1, 1, 1, 1))
Swc.SetData("Hud.LocalPlayer.Options.Controls.ReticleTargetColor", Color(1, 0.4431373, 0.4431373, 1))
Swc.SetData("Hud.LocalPlayer.HasReticleTarget", false)

local shown = false   -- the value of ReticleToShow (false: not set yet)

--- Sets the reticle of the active weapon (after a change of the active slot or of its item).
function Swc.UpdateReticle()
  local slot = Swc.player and Swc.player.activeWeaponSlot
  local itemId = slot and Swc.player.equipment[slot]
  local weapon = itemId and Swc.GameData().weapons[string.lower(itemId)]
  local name = weapon and weapon.reticle
  local crc = name and name ~= "" and swcui.crc32(string.lower(name)) or nil
  if crc ~= shown then
    shown = crc
    Swc.SetData("Hud.LocalPlayer.ReticleToShow", crc)
  end
end
