-- The hit indicator fixture (stage MVP-I): the original canvas lyshineui/hitindicator/hitindicator.dynamicuicanvas
-- (hitindicatormanager.luac, hitindicatoredge.luac, hitindicatorarrow.luac) with the native part of swc/hitindicator.lua. The camera
-- looks east (the view projection of the notices fixture: Unreal +X ahead, Unreal +Y to the right); the character is at (100, 100, 50)
-- (the script reads a place with x + y + z < 0.5 as no place; the places of the world are positive).
-- Hits from the front, the right, and the back (Swc.HostHit: the part of the maximum health, the place of the source in engine
-- meters) show the red edge at the top, the right, and the bottom of the screen; a hit with a place shows the arrow; a hit with no
-- place shows only the edge (90 degrees: the bottom). Pictures: hit-front, hit-right, hit-back.
local canvas = screen("LyShineUI/HitIndicator/HitIndicator.uicanvas", 5, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
frames(0.3)
local p00, p11 = 1.0, 1920 / 1080
Swc.HostViewProjection(0, 0, 0, 1, p00, 0, 0, 0, 0, p11, 0, 0, 0, -170 * p11, 10, 0, 1920, 1080, 0, 0, 170)
-- The heading of the camera: east (0 north, positive west: -90 degrees).
Swc.HostView(-90, 100, 100, 50)

local function shown(prefix)
  local list = {}
  local root = swcui.canvas_get(canvas, "root")
  local function walk(id)
    for _, c in ipairs(swcui.children(id)) do
      local name = swcui.name(c)
      if string.sub(name, 1, #prefix) == prefix and swcui.enabled(c) then
        list[#list + 1] = c
      end
      walk(c)
    end
  end
  walk(root)
  return list
end

local function edge_point()
  local edges = shown("EdgeIndicator")
  if #edges ~= 1 then
    return nil
  end
  return point_of(find(edges[1], "EdgeImage"), 0.5, 0.5)
end

near(Swc.hitindicator.angle(105, 100), -90, 0.02, "the angle of a source ahead of the camera: -90 (the top)")
near(Swc.hitindicator.angle(100, 95), 0, 0.02, "the angle of a source to the right: 0")
near(Swc.hitindicator.angle(95, 100), 90, 0.02, "the angle of a source behind: 90 (the bottom)")

Swc.HostHit(0.05, 105, 100, 51)
frames(0.05)
local x, y = edge_point()
check(x ~= nil and y < 540 - 200 and math.abs(x - 960) < 200, "a hit from the front: the edge at the top")
equal(#shown("ArrowIndicator"), 1, "a hit with a place: one arrow")
picture("hit-front")
frames(2.0)
equal(#shown("EdgeIndicator"), 0, "the edge goes after its time (at most 1.6 s)")
equal(#shown("ArrowIndicator"), 0, "the arrow goes")

Swc.HostHit(0.3, 100, 95, 51)
frames(0.05)
x, y = edge_point()
check(x ~= nil and x > 960 + 200 and math.abs(y - 540) < 200, "a hit from the right: the edge at the right")
picture("hit-right")
frames(2.0)

Swc.HostHit(0.8, 95, 100, 51)
frames(0.05)
x, y = edge_point()
check(x ~= nil and y > 540 + 200 and math.abs(x - 960) < 200, "a hit from the back: the edge at the bottom")
picture("hit-back")
frames(2.0)

Swc.HostHit(0.1)
frames(0.05)
x, y = edge_point()
check(x ~= nil and y > 540 + 200, "a hit with no place: the edge at 90 degrees (the bottom)")
equal(#shown("ArrowIndicator"), 0, "a hit with no place: no arrow")
frames(2.0)

Swc.HostHit(0.0, 105, 100, 51)
frames(0.05)
equal(#shown("EdgeIndicator"), 0, "no damage: no indicator")
