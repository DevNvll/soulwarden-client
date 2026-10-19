-- The fight HUD fixture (stage P3.H; docs/UI.md "The fight HUD"): the HUD screens of a fight with their original scripts and
-- the host calls of the client: the vitals, the weapon slots, the consumable quick slots, the level and XP meter, the compass, the
-- reticle. Pictures: fighthud, fighthud-bow.
local HUD = 2702338936
local vitals = screen("LyShineUI/HUD2024/Vitals/Vitals2024.uicanvas", 12, true)
local weapons = screen("LyShineUI/HUD2024/WeaponQuickslots/WeaponQuickslots.uicanvas", 10, true)
local consumables = screen("LyShineUI/HUD2024/ConsumableQuickslots/ConsumableQuickslots.uicanvas", 11, true)
local xp = screen("LyShineUI/HUD2024/XP/XP.uicanvas", 13, true)
local compass = screen("LyShineUI/Compass/Compass.uicanvas", 14, true)
local reticles = screen("LyShineUI/HUD2024/Reticles/ReticlesCanvas.uicanvas", 9, true)
-- The HUD edit mode holds the layout manager (hudlayoutmanager.luac): it sets the default HUD layout (Hud.ActiveLayout: the
-- places of the HUD parts and the opacity of the full bars), as in the client.
screen("LyShineUI/HUD2024/EditMode/HudEditMode.uicanvas", 15, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(4869, 4869, 100, 100, 100, 100, false)
Swc.HostProgression(65, 1234)

local function instance(n)
  return string.format("{%08X-0000-0000-0000-%012X}", n, n)
end
-- The weapons (the equipment of the wire: MainHandOption1 24, MainHandOption2 25, the heart rune 45) with their ability bars
-- (the tables of the HUD test of the runtime), and the consumables of the quick slots (the paperdoll slots QuickSlot1 to
-- QuickSlot4: 14 to 17).
Swc.HostEquipment(24, "1hSwordT52")
Swc.HostEquipment(45, "RuneBileBombT4A")
Swc.HostEquipment(25, "artifact_set1_1hrapier")
Swc.HostAbilityBar(2169986235, 617453373, 241886633, 2855651066)
Swc.HostAbilityBar(3907802902, 2977457251, 248953190, 3873560184)
Swc.HostActiveWeapon(24, false)
Swc.HostPaperdollItem(14, "PotionHealthT1", 9, 100, 0, 0, instance(3), 0, 3, 17, "")
Swc.HostPaperdollItem(15, "PotionRegenT1", 8, 100, 0, 0, instance(4), 0, 4, 17, "")
Swc.HostPaperdollItem(17, "FoodHealthRecoveryT1", 6, 100, 0, 0, instance(5), 0, 5, 17, "")
-- After the XP popup of the spawn (xpmeter.luac ShowIncreaseValue: 0.15 s in, 1.3 s, 0.6 s out).
frames(3.0)
picture("fighthud")

--- @return The texts of the shown elements below an element (it and its children enabled).
local function shown_texts(id, out)
  out = out or {}
  if not UiElementBus.Event.IsEnabled(id) then
    return out
  end
  if swcui.has(id, "text") then
    out[#out + 1] = UiTextBus.Event.GetText(id)
  end
  for _, child in ipairs(swcui.children(id)) do
    shown_texts(child, out)
  end
  return out
end

local function has(list, text)
  for _, t in ipairs(list) do
    if t == text then
      return true
    end
  end
  return false
end

local function root(canvas)
  return swcui.canvas_get(canvas, "root")
end

-- The consumable quick slots: the item count and the key of each slot (the keys 3 to 6 of the profile).
local slot1 = shown_texts(find(root(consumables), "ConsumableQuickslot"))
check(has(slot1, "9") and has(slot1, "3"), "quick slot 1: the count 9 of the health potions and the key 3: " .. table.concat(slot1, ","))
local slot3 = shown_texts(find(root(consumables), "ConsumableQuickslot3"))
check(has(slot3, "5") and not has(slot3, "9"), "quick slot 3: empty, the key 5: " .. table.concat(slot3, ","))
-- A consumable cooldown (Swc.HostItemCooldown: the cooldown state of the backend, field 3): the quick slot shows the seconds,
-- as the retail take ui-weapon-swap-potion (the potion: 31.2 s).
Swc.HostItemCooldown(swcui.crc32("potionhealtht1"), 31.2)
frames(1.2)
local cooling = shown_texts(find(root(consumables), "ConsumableQuickslot"))
check(has(cooling, "30") or has(cooling, "29"), "the potion in its cooldown shows the seconds: " .. table.concat(cooling, ","))
picture("fighthud-potion-cooldown")
Swc.HostItemCooldown(swcui.crc32("potionhealtht1"), 0)
frames(0.3)
check(not has(shown_texts(find(root(consumables), "ConsumableQuickslot")), "29"), "the count goes at the end of the cooldown")
-- The level and XP meter: the level; the XP popup of the spawn is out.
local meter = find(root(xp), "XpMeter")
check(has(shown_texts(meter), "65"), "the level 65 under the vitals: " .. table.concat(shown_texts(meter), ","))
check(not UiElementBus.Event.IsEnabled(find(meter, "DeltaXpBg")), "no XP popup at rest")
-- The weapon slots: the other weapon above the active one, with its key 2.
local weaponTexts = shown_texts(root(weapons))
check(has(weaponTexts, "2") and has(weaponTexts, "g"), "the other weapon (key 2) and the heart rune (key G): " .. table.concat(weaponTexts, ","))

-- The reticle (swc/reticle.lua): the ReticleName of the active weapon (the sword: melee). The option AlwaysShowReticle is on:
-- the reticle also shows while the weapon is sheathed (the retail pictures).
local aim = find(root(reticles), "AimPositioner")
equal(Swc.GetData("Hud.LocalPlayer.ReticleToShow"), 3488054177, "the reticle of the sword: CRC-32 of melee")
check(UiElementBus.Event.IsEnabled(find(root(reticles), "ReticlesManager")), "the reticle shows")
check(UiElementBus.Event.IsEnabled(find(aim, "ReticleMelee")) and not UiElementBus.Event.IsEnabled(find(aim, "ReticleBow")),
  "the melee reticle")
Swc.HostActiveWeapon(24, true)
frames(0.3)
check(UiElementBus.Event.IsEnabled(find(root(reticles), "ReticlesManager")), "the reticle shows with the weapon sheathed")
-- A bow in the active slot: the bow reticle.
Swc.HostEquipment(24, "2hBow_BrevisT5")
Swc.HostActiveWeapon(24, false)
frames(0.5)
equal(Swc.GetData("Hud.LocalPlayer.ReticleToShow"), 2551600555, "the reticle of the bow: CRC-32 of bow")
check(UiElementBus.Event.IsEnabled(find(aim, "ReticleBow")) and not UiElementBus.Event.IsEnabled(find(aim, "ReticleMelee")),
  "the bow reticle")
-- The bow reticle at rest and in the aim (swc/reticle.lua: the inaccuracy 1 and 0): the bars and the dots at the places of the
-- retail stills of set 6 (ui-crosshair-bow.png, ui-crosshair-bow-aim.png), white.
local bow = find(aim, "ReticleBow")
near(math.abs(UiTransformBus.Event.GetLocalPositionY(find(bow, "Top"))), 26, 0.5, "the bow bar at rest: 26 from the center")
near(math.abs(UiTransformBus.Event.GetLocalPositionX(find(bow, "Right"))), 18, 0.5, "the bow dot at rest: 18 from the center")
local white = UiImageBus.Event.GetColor(find(bow, "Center"))
check(white and white.r > 0.99 and white.g > 0.99 and white.b > 0.99, "the bow reticle is white (the option ReticleColor)")
picture("fighthud-bow")
Swc.HostAim(true)
frames(0.5)
near(math.abs(UiTransformBus.Event.GetLocalPositionY(find(bow, "Top"))), 18, 0.5, "the bow bar in the aim: 18 from the center")
near(math.abs(UiTransformBus.Event.GetLocalPositionX(find(bow, "Right"))), 11, 0.5, "the bow dot in the aim: 11 from the center")
picture("fighthud-bow-aim")
Swc.HostAim(false)

-- The HUD at the window size of the retail pictures of the Soulwarden evidence (1256 x 660; the canvas is 2055 x 1080 as in
-- the client window): the sword in the hand, for tools/lib/hud_compare.py.
Swc.HostEquipment(24, "1hSwordT52")
Swc.HostActiveWeapon(24, false)
swcui.set_viewport(1256, 660)
frames(0.5)
picture("fighthud-1256")
swcui.set_viewport(1920, 1080)
