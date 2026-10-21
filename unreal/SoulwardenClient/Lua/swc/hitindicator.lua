-- The hit indicator of the HUD (stage MVP-I; docs/UI.md "The fight HUD"): the red screen edge and the red arrow toward the source of a
-- hit on the own character, from the original canvas lyshineui/hitindicator/hitindicator.dynamicuicanvas (hitindicatormanager.luac,
-- hitindicatoredge.luac, hitindicatorarrow.luac).
--
-- The native part (not in the scripts): the vitals component sends VitalsComponentNotificationBus OnDamage to the root entity of the
-- HUD. The arguments, from the use in hitindicatormanager.luac OnDamage (9 parameters with self):
--   1 the source entity (not read), 2 the damage as a part of the maximum health (no hit below the epsilon; the edge sprite and
--   opacity by its thresholds), 3 the place of the source (a Vector3; nil or x + y + z < 0.5: the edge at 90 degrees, no arrow),
--   4 the angle of the edge (degrees), 5 a flag that stops the arrow (true: the edge only), 6 not read, 7 a flag that stops the hit
--   (true: no indicator), 8 not read.
-- This module sends it for each message 3601 of the own character (Swc.HostHit; the backend always sets the place, its flag bit 1).
-- The angle: the screen convention of hitindicatorarrow.luac GetAngleToWorldPosition (math.deg(math.atan2(dy, dx)) of the canvas
-- points; 0 to the right, 90 down): the source in the frame of the camera heading, its right part on x and its back part on y (a
-- source in front of the camera gives -90, the top edge). The native rule of the angle is not known: this is an inference. Flags 5
-- and 7 are false: the arrow at each hit with a place (the retail pictures of set 6, ui-fight-hud.mp4: the arrow at each bite).
--
-- LyShineManagerBus ProjectToScreen(position in meters, bool, bool): the canvas point (1080 lines, the scale of the viewport height)
-- of a world point. A point behind the camera gets a far point in the direction of its place in the frame of the camera heading
-- (the arrow then points down); the native rule for such a point is not known.

local Swc = Swc

local H = {}
Swc.hitindicator = H

local CANVAS_LINES = 1080
local FAR = 100000

--- The camera heading of the HUD (radians, 0 north, positive west) as the forward and right vectors in the engine space (x east,
--- y north).
local function heading_axes()
  local heading = Swc.GetData and Swc.GetData("Hud.LocalPlayer.PlayerHeading") or 0
  heading = tonumber(heading) or 0
  local fx, fy = -math.sin(heading), math.cos(heading)
  return fx, fy, fy, -fx
end

--- The place (engine meters) in the frame of the camera heading from the own character (ox, oy; else Hud.LocalPlayer.Position):
--- the right part and the forward part.
local function view_parts(x, y, ox, oy)
  if ox == nil then
    local own = Swc.GetData and Swc.GetData("Hud.LocalPlayer.Position")
    ox, oy = own and own.x or 0, own and own.y or 0
  end
  local dx, dy = x - ox, y - oy
  local fx, fy, rx, ry = heading_axes()
  return dx * rx + dy * ry, dx * fx + dy * fy
end

--- The screen angle of the hit (degrees; the convention of hitindicatorarrow.luac).
function H.angle(x, y, ox, oy)
  local right, forward = view_parts(x, y, ox, oy)
  if math.abs(right) + math.abs(forward) < 1e-4 then
    return 90
  end
  local angle = math.deg(math.atan2(-forward, right))
  -- hitindicatoredge.luac SetDirection gives no place (NaN) at some exact angles (0 and 225 in the fixture: its border intersection
  -- divides by zero); a measured angle seldom is exact, and 0.01 degrees moves it off these points.
  if math.abs(angle / 45 - math.floor(angle / 45 + 0.5)) < 1e-6 then
    angle = angle + 0.01
  end
  return angle
end

local function project_to_screen(position)
  local view = Swc.markers and Swc.markers.view
  if not position or not view or not view.height or view.height <= 0 then
    return Swc.Vector2.new({x = 0, y = 0})
  end
  local s = CANVAS_LINES / view.height
  local m = view.m
  local ux, uy, uz = position.x * 100, -position.y * 100, position.z * 100
  local cw = ux * m[4] + uy * m[8] + uz * m[12] + m[16]
  if cw > 0.0001 then
    local sx, sy = Swc.markers.project(ux, uy, uz)
    if sx then
      return Swc.Vector2.new({x = sx * s, y = sy * s})
    end
  end
  local right, forward = view_parts(position.x, position.y)
  local length = math.sqrt(right * right + forward * forward)
  if length < 1e-4 then
    right, forward, length = 0, -1, 1
  end
  return Swc.Vector2.new({x = view.width * 0.5 * s + right / length * FAR, y = view.height * 0.5 * s - forward / length * FAR})
end

Swc.Define("LyShineManagerBus", "Broadcast", {
  ProjectToScreen = function(position) return project_to_screen(position) end,
})

--- One hit on the own character (message 3601): the part of the maximum health, the place of the source (engine meters; no place:
--- nil), and the place of the feet of the own character (engine meters; nil: Hud.LocalPlayer.Position, which the host sends only
--- with a new heading).
function Swc.HostHit(part, x, y, z, ox, oy, oz)
  part = tonumber(part) or 0
  local place, angle = nil, 90
  if ox ~= nil then
    -- The arrow (hitindicatorarrow.luac GetAngleToWorldPosition) projects Hud.LocalPlayer.Position: the place of this hit.
    Swc.SetData("Hud.LocalPlayer.Position", Swc.Vector3.new({x = ox, y = oy, z = oz or 0}))
  end
  if x ~= nil then
    place = Swc.Vector3.new({x = x, y = y, z = z})
    angle = H.angle(x, y, ox, oy)
  end
  local heading = tonumber(Swc.GetData("Hud.LocalPlayer.PlayerHeading")) or 0
  Swc.Note(string.format("hit indicator: part %.3f, source %.2f %.2f, own %s %s, heading %.1f, angle %.1f", part, tonumber(x) or 0,
    tonumber(y) or 0, tostring(ox), tostring(oy), math.deg(heading), angle))
  Swc.Notify("VitalsComponentNotificationBus", Swc.ids and Swc.ids.root, "OnDamage", nil, part, place, angle, false, nil, false,
    nil)
end
