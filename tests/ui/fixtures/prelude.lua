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
