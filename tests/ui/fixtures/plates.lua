-- The plates fixture (stage WF7): the original AiMarker plates of lyshineui/markers/markermanager.dynamicuicanvas (markermanager.luac,
-- marker.luac, markerstatuseffect.luac) with the native part of swc/markers.lua. The scripts register the marker types and the pools;
-- two creatures of the host (Swc.HostPlates) get the elements a0 and a1 of the AI pool: a Grey Wolf 5 m in front of the camera with
-- full health and a stun (the status row), and a second wolf 12 m away at one half of its health (the alpha of the fade from 10 m to
-- 15 m, the scale from 1 m to 15 m). Then the first wolf dies (the bar empties), and the second leaves the pool (its element hides).
-- Picture: plates.
local markers = screen("LyShineUI/Markers/MarkerManager.uicanvas", 10, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(18, 0)
frames(0.3)
check(Swc.markers.ready, "the scripts registered the marker types and the pools")
check(Swc.markers.params.Wolf ~= nil and Swc.markers.params.Wolf.wZ == 1.5, "the marker params of the type Wolf (wZ 1.5 m)")

-- The camera at 1.7 m, along +X (Unreal), 90 degrees of horizontal field of view, 1920 x 1080 (the view projection of Unreal:
-- the view axes Y, Z, X; reversed Z).
local p00, p11, lx, ly, lz = 1.0, 1920 / 1080, 0, 0, 170
Swc.HostViewProjection(0, 0, 0, 1,
             p00, 0, 0, 0,
             0, p11, 0, 0,
             -ly * p00, -lz * p11, 10, -lx,
             1920, 1080, lx, ly, lz)
local function line(interest, serial, x, y, name, health, max, dead, distance, statuses)
  return table.concat({interest, serial, x, y, 0, name, "Wolf", "Solo", "", 18, 0, health, max, dead and 1 or 0, dead and 0.5 or -1, 0, 0, 0,
                       distance, 1, statuses}, "\t")
end
local both = line(57, 1, 500, 0, "Grey Wolf", 1690.9, 1690.9, false, 5.0, "AI_Stun_4sec*1") .. "\n"
  .. line(58, 2, 1200, 300, "Grey Wolf", 845.4, 1690.9, false, 12.4, "")
Swc.HostPlates(both)
frames(0.5)
Swc.HostPlates(both)
frames(0.5)
local function slot_element(index)
  local slot = Swc.markers.slots[index]
  local object = slot and slot.markerId and Swc.markers.objects[tostring(slot.markerId)]
  return object and object.element
end
local nearPlate, farPlate = slot_element(0), slot_element(1)
check(nearPlate ~= nil and farPlate ~= nil, "the two creatures have the elements a0 and a1")
if nearPlate and farPlate then
  check(swcui.enabled_in_hierarchy(nearPlate) and swcui.enabled_in_hierarchy(farPlate), "the two plates show (IsVisible)")
  equal(swcui.get(find(nearPlate, "Text_PlayerName"), "text.text"), "Grey Wolf", "the name of the plate")
  equal(swcui.get(find(nearPlate, "LevelText"), "text.text"), "18", "the level of the plate")
  local x, y = swcui.viewport_position(nearPlate)
  near(x, 960, 2, "the plate of the near wolf: the middle of the view")
  near(y, 578.5, 3, "the plate of the near wolf: 1.5 m above the feet (wZ of the type Wolf)")
  near(swcui.get(farPlate, "fader.fade"), 0.52, 0.02, "the far wolf (12.4 m): the alpha of the fade from 10 m to 15 m")
  local statusRow = find(nearPlate, "StatusEffect1")
  check(statusRow ~= nil and swcui.enabled_in_hierarchy(statusRow), "the status row of the stun shows")
  check(find(farPlate, "StatusEffect1") == nil or not swcui.enabled_in_hierarchy(find(farPlate, "StatusEffect1")), "no status row at the far wolf")
end
picture("plates")

-- The near wolf dies; the far one leaves the pool (the creature view does not have it any more).
Swc.HostPlates(line(57, 1, 500, 0, "Grey Wolf", 0, 1690.9, true, 5.0, ""))
frames(1.5)
if nearPlate and farPlate then
  check(not swcui.enabled_in_hierarchy(farPlate), "the plate of a creature out of the pool hides")
end
picture("plates-dead")
