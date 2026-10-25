-- The quest objects fixture (stage Q9, Linear AST-244; docs/QUESTS.md "The destructible quest objects"): the plate of a destructible
-- quest object in the AI pool of the original markers (markermanager.luac registers the type DamageableGatherable in the AI markers).
-- The line is the one of Objects/SwcQuestObjects GetPlates: the interest 0xF0000000 + index, the marker type and the creature type
-- override DamageableGatherable, the name key of the gatherable row, the health 1 of 1 (the fallback of the Lua code), the forced health
-- bar of the variant, no level. A Corrupted Nodule 6 m in front of the camera gets an element with its name and a full bar; then the
-- object is destroyed (no line): the element hides. Picture: questobjects-plate.
local markers = screen("LyShineUI/Markers/MarkerManager.uicanvas", 10, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(5, 0)
frames(0.3)
check(Swc.markers.ready, "the scripts registered the marker types and the pools")
check(Swc.markers.params.DamageableGatherable ~= nil, "the marker params of the type DamageableGatherable")

local p00, p11, lx, ly, lz = 1.0, 1920 / 1080, 0, 0, 170
Swc.HostViewProjection(0, 0, 0, 1,
             p00, 0, 0, 0,
             0, p11, 0, 0,
             -ly * p00, -lz * p11, 10, -lx,
             1920, 1080, lx, ly, lz)
local INTEREST = 0xF0000000 + 3
local function line(distance)
  return table.concat({INTEREST, 1, 600, 0, 0, "@CorruptedNodule_DisplayName", "DamageableGatherable", "", "DamageableGatherable", 0, 1,
                       1, 1, 0, -1, 0, 0, 1, distance, 1, "", 0, 0}, "\t")
end
Swc.HostPlates(line(6.0))
frames(0.5)
Swc.HostPlates(line(6.0))
frames(0.5)
local slot
for _, each in pairs(Swc.markers.slots) do
  if each.plate and each.plate.interest == INTEREST then slot = each end
end
check(slot ~= nil, "the quest object has an element of the AI pool")
local object = slot and slot.markerId and Swc.markers.objects[tostring(slot.markerId)]
local element = object and object.element
check(element ~= nil and swcui.enabled_in_hierarchy(element), "the plate of the quest object shows")
if slot then
  equal(slot.values.Type, "DamageableGatherable", "the marker type")
  equal(slot.values.HealthPercent, 1, "the health bar: full (1 of 1)")
  equal(slot.values.ForceDisplayHealthBar, true, "the forced health bar of the variant")
  equal(slot.values.CreatureTypeOverride, swcui.crc32("damageablegatherable"), "the creature type override")
end
if element then
  local name = find(element, "Text_PlayerName")
  note("the name of the plate: " .. tostring(name and swcui.get(name, "text.text")))
  check(name ~= nil and (swcui.get(name, "text.text") or "") ~= "", "the plate has the name of the object")
end
picture("questobjects-plate")

-- The object is destroyed (the LootTracker entry): the subsystem gives no line; the element hides.
Swc.HostPlates("")
frames(1.5)
check(element == nil or not swcui.enabled_in_hierarchy(element), "a destroyed object: no plate")
