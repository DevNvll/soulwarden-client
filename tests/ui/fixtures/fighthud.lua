-- The fight HUD fixture (stage P3.H; docs/UI.md "The fight HUD"): the HUD screens of a fight with their original scripts and
-- the host calls of the client: the vitals, the weapon slots, the consumable quick slots, the level and XP meter, the compass.
-- Pictures: fighthud.
local HUD = 2702338936
local vitals = screen("LyShineUI/HUD2024/Vitals/Vitals2024.uicanvas", 12, true)
local weapons = screen("LyShineUI/HUD2024/WeaponQuickslots/WeaponQuickslots.uicanvas", 10, true)
local consumables = screen("LyShineUI/HUD2024/ConsumableQuickslots/ConsumableQuickslots.uicanvas", 11, true)
local xp = screen("LyShineUI/HUD2024/XP/XP.uicanvas", 13, true)
local compass = screen("LyShineUI/Compass/Compass.uicanvas", 14, true)
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
-- The level and XP meter: the level; the XP popup of the spawn is out.
local meter = find(root(xp), "XpMeter")
check(has(shown_texts(meter), "65"), "the level 65 under the vitals: " .. table.concat(shown_texts(meter), ","))
check(not UiElementBus.Event.IsEnabled(find(meter, "DeltaXpBg")), "no XP popup at rest")
-- The weapon slots: the other weapon above the active one, with its key 2.
local weaponTexts = shown_texts(root(weapons))
check(has(weaponTexts, "2") and has(weaponTexts, "g"), "the other weapon (key 2) and the heart rune (key G): " .. table.concat(weaponTexts, ","))
