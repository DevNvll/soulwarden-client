-- The plates fixture (stage WF7): the original AiMarker plates of lyshineui/markers/markermanager.dynamicuicanvas (markermanager.luac,
-- marker.luac, markerstatuseffect.luac) with the native part of swc/markers.lua. The scripts register the marker types and the pools;
-- two creatures of the host (Swc.HostPlates) get the elements a0 and a1 of the AI pool: a Grey Wolf 5 m in front of the camera with
-- full health and a stun (the status row), and a second wolf 12 m away at one half of its health (the alpha of the fade from 10 m to
-- 15 m, the scale from 1 m to 15 m). Then the first wolf dies (the bar empties), and the second leaves the pool (its element hides).
-- AST-256 (research/marker-fade.md): the native fade (0.5 at 12.5 m), the timed target past the end of the fade and the end of its
-- timer, the occlusion of a timed target, and the order of the pool of 4 with 7 wolves (on the screen, the timed target, the nearest).
-- Pictures: plates, plates-dead, plates-pool.
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

-- AST-256: the cases of the study. A line with the timed target and the time of its activation (the two last fields).
local function timed(interest, serial, x, y, distance, on, stamp)
  return line(interest, serial, x, y, "Grey Wolf", 1690.9, 1690.9, false, distance, "") .. "\t" .. (on and 1 or 0) .. "\t" .. (stamp or 0)
end
local function element_of(interest)
  for index = 0, 3 do
    local slot = Swc.markers.slots[index]
    if slot and slot.plate and slot.plate.interest == interest then
      return slot_element(index), slot
    end
  end
end
local function shows(interest)
  local element = element_of(interest)
  return element ~= nil and swcui.enabled_in_hierarchy(element)
end
equal(#(function() local n = {} for i = 0, 9 do if Swc.markers.slots[i] then n[#n + 1] = i end end return n end)(), 4,
  "the AI pool has 4 elements (markermanager.luac main register 15)")

Swc.HostPlates(timed(60, 10, 1250, 0, 12.5, false))
frames(0.5)
local e60 = element_of(60)
check(shows(60), "an untagged wolf at 12.5 m shows")
near(e60 and swcui.get(e60, "fader.fade") or -1, 0.5, 0.01, "an untagged wolf at 12.5 m: alpha 0.5 (maxAlpha - (d - 10) / (15 - 10))")

Swc.HostPlates(timed(61, 11, 2000, 0, 20.0, true, 100.0))
frames(0.5)
local e61 = element_of(61)
check(shows(61), "the timed target at 20 m shows (past the end of the fade)")
near(e61 and swcui.get(e61, "fader.fade") or -1, 1.0, 0.01, "the timed target at 20 m: alpha 1 (no distance fade)")
Swc.HostMarkerOcclusion("61:1")
Swc.HostPlates(timed(61, 11, 2000, 0, 20.0, true, 100.0))
frames(0.3)
check(shows(61), "the timed target shows behind the world (no occlusion)")
Swc.HostPlates(timed(61, 11, 2000, 0, 20.0, false))
frames(0.3)
check(not shows(61), "the end of the timer: the wolf at 20 m hides (alpha 0)")
Swc.HostPlates(timed(61, 11, 1100, 0, 11.0, false))
frames(0.3)
check(not shows(61), "an untagged wolf at 11 m behind the world hides (the occlusion)")
Swc.HostMarkerOcclusion("")

-- 7 wolves: one 3 m behind the camera, five on the screen at 5 to 9 m, and the timed target at 20 m.
local seven = {timed(70, 20, -300, 0, 3.0, false)}
for i = 0, 4 do
  seven[#seven + 1] = timed(71 + i, 21 + i, 500 + 100 * i, 50 * i, 5.0 + i, false)
end
seven[#seven + 1] = timed(76, 26, 2000, -200, 20.0, true, 200.0)
Swc.HostPlates(table.concat(seven, "\n"))
frames(0.5)
local pool = {}
for index = 0, 3 do
  local slot = Swc.markers.slots[index]
  pool[#pool + 1] = slot and slot.plate and slot.plate.interest or 0
end
table.sort(pool)
equal(table.concat(pool, " "), "71 72 73 76", "the pool of 4: the timed target and the three nearest on the screen; not the wolf behind the camera")
check(shows(76) and shows(71) and shows(72) and shows(73), "the four plates of the pool show")
picture("plates-pool")
local before = {}
for index = 0, 3 do before[index] = Swc.markers.slots[index].plate.interest end
seven[2] = timed(71, 21, 520, 0, 5.2, false)
Swc.HostPlates(table.concat(seven, "\n"))
frames(0.3)
local stable = true
for index = 0, 3 do stable = stable and Swc.markers.slots[index].plate.interest == before[index] end
check(stable, "each creature keeps its slot while it stays in the pool")
