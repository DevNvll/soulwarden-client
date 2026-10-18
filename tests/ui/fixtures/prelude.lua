-- The helpers of the fixtures (tests/ui/fixtures/*.lua). A fixture runs after the boot of the emulation (the globals
-- canvas) in a runtime of its own, with a viewport of 1920 x 1080 and no screen of the loader.
local fixture = Swc.fixture

function check(ok, label)
  fixture.checks = fixture.checks + 1
  if not ok then
    fixture.failed = fixture.failed + 1
    fixture.lines[#fixture.lines + 1] = "FAIL " .. label
  end
  return ok
end

function equal(actual, expected, label)
  return check(actual == expected, label .. ": " .. tostring(actual) .. " (expected " .. tostring(expected) .. ")")
end

function near(actual, expected, tolerance, label)
  local ok = type(actual) == "number" and math.abs(actual - expected) <= tolerance
  return check(ok, label .. ": " .. tostring(actual) .. " (expected " .. tostring(expected) .. " +- " .. tolerance .. ")")
end

function note(text)
  fixture.lines[#fixture.lines + 1] = "note " .. text
end

--- Runs frames of 1/60 s for a time in seconds.
function frames(seconds)
  for _ = 1, math.max(1, math.floor(seconds * 60 + 0.5)) do
    swcui.tick(1 / 60)
  end
end

--- Writes the draw list of the frame (SWC_WORK/build/ui-tests/reports/draw/NAME.json; tools/test-ui.sh --preview
--- makes NAME.png).
function picture(name)
  return swcui.write_draw(name)
end

--- Loads a canvas of the game (a path with the ending .uicanvas) with its scripts. @return The canvas id.
function load(path, drawOrder)
  local canvas, ok = Swc.LoadCanvas(path, drawOrder or 1)
  check(ok, "the canvas loads: " .. path)
  return canvas
end

--- @return The element at a path of names below the root of a canvas ("Window/List/Item"), or nil.
function element(canvas, path)
  local id = swcui.canvas_get(canvas, "root")
  for name in string.gmatch(path, "[^/]+") do
    id = swcui.find_child(id, name)
    if id == nil or not id:IsValid() then
      return nil
    end
  end
  return id
end

-- The screens of a fixture: the fixture runtime refuses the screens of the loader; screen() allows one and loads it as
-- the loader does (autohide: hidden until the state machine enters its state).
local allowedScreens = {}
local nativeAllowed = swcui.allowed
local function screen_key(path)
  return (string.lower(path):gsub("%.dynamicuicanvas$", ""):gsub("%.uicanvas$", ""))
end
swcui.allowed = function(path)
  return allowedScreens[screen_key(path)] or nativeAllowed(path)
end

--- Loads a screen of the game (a path with the ending .uicanvas). @return The canvas id.
function screen(path, drawOrder)
  allowedScreens[screen_key(path)] = true
  local canvas = Swc.LoadScreen(path, true, drawOrder or 50)
  check(canvas:IsValid(), "the screen loads: " .. path)
  return canvas
end

--- @return The first element with a name below an element (breadth first), or nil.
function find(parent, name)
  local id = swcui.find_descendant(parent, name)
  if id == nil or not id:IsValid() then
    return nil
  end
  return id
end

--- Spawns a UI slice of the game (a path with the ending .dynamicslice) under an element, with its scripts.
--- @return The root of the copy.
function spawn(slicePath, parent)
  local root, created = swcui.spawn(slicePath, parent)
  check(root ~= nil, "the slice spawns: " .. slicePath)
  if root then
    Swc.ActivateElements(created, root, false)
  end
  return root
end

-- The input of the fixtures: the same entry as the client (Swc.HostInput), in viewport pixels.

--- @return The point of an element at fractions of its rect (0.5, 0.5: the middle), in viewport pixels.
function point_of(id, fx, fy)
  local p = {swcui.viewport_points(id)}
  fx, fy = fx or 0.5, fy or 0.5
  -- top left, top right, bottom right, bottom left (x, y each)
  local tx = p[1] + (p[3] - p[1]) * fx
  local ty = p[2] + (p[4] - p[2]) * fx
  local bx = p[7] + (p[5] - p[7]) * fx
  local by = p[8] + (p[6] - p[8]) * fx
  return tx + (bx - tx) * fy, ty + (by - ty) * fy
end

function move_to(id, fx, fy)
  local x, y = point_of(id, fx, fy)
  Swc.HostInput("move", x, y)
  return x, y
end

function click(id, fx, fy)
  move_to(id, fx, fy)
  Swc.HostInput("button", "left", true)
  frames(0.05)
  Swc.HostInput("button", "left", false)
  frames(0.05)
end

--- Drags from one point of an element to another (fractions of its rect), in steps.
function drag(id, fx0, fy0, fx1, fy1, steps)
  move_to(id, fx0, fy0)
  Swc.HostInput("button", "left", true)
  steps = steps or 8
  for i = 1, steps do
    local t = i / steps
    move_to(id, fx0 + (fx1 - fx0) * t, fy0 + (fy1 - fy0) * t)
    frames(1 / 60)
  end
  Swc.HostInput("button", "left", false)
  frames(0.05)
end

function wheel(delta)
  Swc.HostInput("wheel", delta)
  frames(1 / 60)
end

function key(name)
  Swc.HostInput("key", name, true)
  Swc.HostInput("key", name, false)
  frames(1 / 60)
end

function type_text(text)
  for ch in string.gmatch(text, "[%z\1-\127\194-\244][\128-\191]*") do
    Swc.HostInput("text", ch)
  end
  frames(1 / 60)
end

--- @return The script table of an element (the first script component, or the one of a script path), as the element
--- registrar of the scripts gives it.
function script_of(id, path)
  for _, instance in ipairs(Swc.components[tostring(id)] or {}) do
    if path == nil or string.find(instance.__swcScript or "", path, 1, true) then
      return instance
    end
  end
  return nil
end
