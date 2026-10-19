-- The notices fixture (stage WF7): the damage numbers of the original canvas lyshineui/hud/damagenumbers/damagenumbers.dynamicuicanvas
-- (damagenumber.luac) with the native part of swc/notices.lua, and the loot ticker lyshineui/hud2024/lootticker/lootticker2024
-- (lootticker2024.luac) with the bag feed of swc/items.lua. Damage events of the host (Swc.HostDamage): 64 slash at a grey wolf 5 m
-- ahead, a critical hit, a thrust hit (the weakness of the wolf: the yellow number), 151 strike to the own character (the red number),
-- and a heal of 120 (the green "+120"). Then the bag of the server gets 9 iron ore with the cause 1 (ItemGathered): the loot ticker
-- shows the item; 27 experience give the progression line "+27 XP"; a load of the bag (the cause 0x11) shows nothing.
-- Pictures: notices-damage, notices-loot, notices-xp.
-- The progression of the world entry comes before the HUD screens load (the client: the first frame; the loader loads later):
-- lootticker2024.luac takes the first total with no notice.
Swc.HostProgression(18, 1000)
local numbers = screen("LyShineUI/HUD/DamageNumbers/DamageNumbers.uicanvas", 5, true)
local ticker = screen("LyShineUI/HUD2024/LootTicker/LootTicker2024.uicanvas", 20, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostBagSize(6)
Swc.HostCoin(0)
frames(0.3)
local p00, p11 = 1.0, 1920 / 1080
Swc.HostViewProjection(0, 0, 0, 1, p00, 0, 0, 0, 0, p11, 0, 0, 0, -170 * p11, 10, 0, 1920, 1080, 0, 0, 170)
-- The vitals id of the grey wolf (the CRC-32 of "wolf_grey_7": the weakness Thrust 0.2 of defenses.json).
local wolf = tostring(swcui.crc32("wolf_grey_7"))
local function event(kind, x, y, z, flags, entries)
  return table.concat({kind, 57, x, y, z, flags, 25, kind == "enemy" and wolf or "", entries}, "\t")
end
Swc.HostDamage(event("enemy", 500, 0, 100, 0, "4:64.9:1"))
frames(0.05)
Swc.HostDamage(event("enemy", 500, 0, 100, 64, "4:132:1"))
frames(0.05)
Swc.HostDamage(event("enemy", 500, 0, 100, 0, "5:85:1"))
frames(0.05)
Swc.HostDamage(event("player", 150, 50, 160, 2, "6:151:1"))
frames(0.05)
Swc.HostDamage(event("heal", 150, 50, 160, 0, "3:120:1"))
frames(0.25)
local shown = {}
for _, number in ipairs(Swc.notices.pool) do
  if number.busy and swcui.enabled_in_hierarchy(number.id) then
    shown[#shown + 1] = number
  end
end
equal(#shown, 5, "five numbers show")
check(Swc.HostPointOf("text:64") ~= "", "the slash number: 64 (the whole part of 64.9)")
check(Swc.HostPointOf("text:132") ~= "", "the critical hit: 132")
check(Swc.HostPointOf("text:85") ~= "", "the thrust hit: 85")
check(Swc.HostPointOf("text:151") ~= "", "the damage to the own character: 151")
check(Swc.HostPointOf("text:+120") ~= "", "the heal: +120")
check(Swc.HostPointOf("text:+1000") == "", "the first total of the experience gives no line")
picture("notices-damage")
frames(3.0)
local busy = 0
for _, number in ipairs(Swc.notices.pool) do
  busy = busy + (number.busy and 1 or 0)
end
equal(busy, 0, "the animations end: the numbers go back to the pool")

-- The loot ticker: 9 iron ore with the cause 1 (ItemGathered).
Swc.HostBagSlot(1, "OreT1", 9, 0, 0, 0, "{00000011-0000-0000-0000-000000000011}", 0, 1, 1, "")
frames(1.0)
check(Swc.HostPointOf("text:Iron Ore") ~= "", "the loot ticker: Iron Ore")
check(Swc.HostPointOf("text:9") ~= "", "the icon of the ticker: the count of the item in the bag (9)")
picture("notices-loot")
frames(6.0)
check(Swc.HostPointOf("text:Iron Ore") == "", "the line of the ticker goes after its time")
-- The experience of a kill: "+27 XP" (the Glory node: the new total less the old).
Swc.HostProgression(18, 1027)
frames(1.0)
check(Swc.HostPointOf("text:+27 XP") ~= "" or Swc.HostPointOf("text:+27") ~= "", "the progression line: +27 XP")
picture("notices-xp")
frames(6.0)
-- A load of the bag (the cause 0x11): no notice.
Swc.HostBagSlot(2, "1hSwordT1", 1, 100, 900, 1000, "{00000012-0000-0000-0000-000000000012}", 0, 2, 17, "")
frames(1.0)
check(Swc.HostPointOf("text:Longsword") == "" and Swc.HostPointOf("text:Iron Sword") == "", "the load of the bag: no notice")
