-- The input of the UI (the rules of the LyShine canvas, O3DE UiCanvasComponent: facts only): one hover interactable and
-- one active interactable. A move sets the hover (only the active one while one is active) and drags the active one; a
-- press of the left button goes to the front interactable at the point (it becomes active); the release goes to the
-- active one; the wheel goes to the scroll box under the point; keys and text go to the active one (a text input that
-- edits), else the keys fire the game actions (swc/actions.lua). The host calls Swc.HostInput (the client from
-- Slate and its harness input, the tests from tests/ui/fixtures/prelude.lua).
--
-- Coordinates: viewport pixels. Key names: lower case ("enter", "escape", "backspace", "delete", "left", "right",
-- "home", "end", or a letter).

local Swc = Swc

Swc.input = {hover = nil, active = nil, activePressed = false, stayActive = false, x = -1, y = -1}
local input = Swc.input

local function valid(id)
  return id ~= nil and type(id) == "userdata" and id:IsValid() and swcui.exists(id)
end

local function clear_hover()
  if input.hover then
    local id = input.hover
    input.hover = nil
    if valid(id) then
      Swc.InteractableHoverEnd(id)
    end
  end
end

local function set_hover(id)
  if input.hover ~= id then
    clear_hover()
    input.hover = id
    if valid(id) then
      Swc.InteractableHoverStart(id)
    end
  end
end

local function clear_active()
  if input.active then
    local id = input.active
    input.active = nil
    input.stayActive = false
    if valid(id) then
      Swc.InteractableLostActive(id)
    end
  end
end

local function move(x, y)
  input.x, input.y = x, y
  local latest
  if valid(input.active) then
    if swcui.contains(input.active, x, y) then
      latest = input.active
    end
  else
    latest = swcui.interactable_at(x, y)
  end
  if input.hover and input.hover ~= latest then
    clear_hover()
  end
  if latest and not input.hover then
    set_hover(latest)
  end
  if valid(input.active) and input.activePressed then
    Swc.InteractableDrag(input.active, x, y)
  end
end

local function press(x, y)
  input.x, input.y = x, y
  local hit = swcui.interactable_at(x, y)
  if not hit or hit ~= input.active then
    clear_active()
  end
  if hit then
    local handled, stay = Swc.InteractablePressed(hit, x, y)
    if handled then
      input.active = hit
      input.stayActive = stay or Swc.IState(hit).stayActive or false
      input.activePressed = true
    end
  end
end

local function release(x, y)
  local previous = input.active
  if valid(input.active) then
    Swc.InteractableReleased(input.active, x, y)
    if not input.stayActive then
      input.active = nil
    end
  end
  input.activePressed = false
  Swc.DropdownCanvasReleased(previous)
  -- After a release the hover follows the point again.
  move(x, y)
end

--- The right button: a button with right actions (the fork ActionNamePressedRight, ActionNameRight).
local function right(x, y, down)
  local hit = swcui.interactable_at(x, y)
  if not hit then
    return
  end
  local b = Swc.IState(hit).button
  if b then
    if down then
      Swc.SendAction(hit, b.actionPressedRight)
    else
      Swc.QueueAction(hit, b.actionRight)
    end
  end
end

local function wheel(delta)
  -- The scroll box under the point (from the front interactable up to its parents).
  local at = swcui.interactable_at(input.x, input.y)
  while valid(at) do
    if Swc.IState(at).scrollBox then
      return Swc.ScrollBoxWheel(at, delta)
    end
    at = swcui.parent(at)
  end
  return false
end

local function editing()
  return valid(input.active) and Swc.IState(input.active).editing
end

local function key(name, down)
  if editing() then
    -- A text input that edits takes the keys (no game action).
    if down then
      Swc.TextInputKey(input.active, name)
    end
    return true
  end
  return Swc.FireActions(name, down)
end

local function text(utf8)
  if editing() then
    Swc.TextInputType(input.active, utf8)
    return true
  end
  return false
end

--- One input event of the host: ("move", x, y), ("button", "left"|"right", down), ("wheel", delta), ("key", name,
--- down), ("text", utf8). Key names are the CryInput names of the action maps ("escape", "w", "mouse1", "f10").
--- @return True when the UI used the event: a key that an editing text input or a game action used, a text that an
--- editing text input took, a pointer event while the UI has the cursor.
function Swc.HostInput(kind, a, b)
  local used = false
  if kind == "move" then
    move(a, b)
    used = Swc.CursorWanted()
  elseif kind == "button" then
    if a == "left" then
      if b then
        press(input.x, input.y)
      else
        release(input.x, input.y)
      end
    elseif a == "right" then
      right(input.x, input.y, b)
    end
    used = Swc.CursorWanted()
  elseif kind == "wheel" then
    wheel(a)
    used = Swc.CursorWanted()
  elseif kind == "key" then
    used = key(a, b)
  elseif kind == "text" then
    used = text(a)
  end
  Swc.FlushActions()
  return used
end

-- The game maps of the client (its player controller has their actions): a key goes to the game unless each of these
-- maps that binds the key is off.
local GAME_MAPS = {"movement", "combat", "player", "camera"}

--- @return False when the game must not get a key (a CryInput name; "maxis_x" for the mouse moves): each game map that
--- binds it is off (a screen turned it off), or an editing text input has the keys.
function Swc.HostKeyToGame(name)
  if editing() then
    return false
  end
  local bound, on = false, false
  for _, mapName in ipairs(GAME_MAPS) do
    if Swc.KeysOfMap(mapName)[name] then
      bound = true
      on = on or Swc.ActionMapOn(mapName)
    end
  end
  return on or not bound
end

--- @return True when the UI needs the pointer (the client shows the cursor and keeps the mouse from the game).
function Swc.HostCursor()
  return Swc.CursorWanted()
end

--- The interactable under the cursor (UiCanvasBus GetHoverInteractable).
function Swc.HoverInteractable()
  return input.hover or EntityId()
end

-- The cursor of the UI (the fork UiCursorBus): the position of the last input event, in viewport pixels.
Swc.Define("UiCursorBus", "Broadcast", {
  GetUiCursorPosition = function() return Swc.Vector2.new({x = input.x, y = input.y}) end,
})

--- The harness input of the client: the point at fractions of the rect of the first shown element with a name, in
--- the shown canvases from the top draw order down. @return "x y" in viewport pixels, or "" when no shown element has
--- the name.
function Swc.HostPointOf(name, fx, fy)
  local canvases = {}
  for _, screen in pairs(Swc.screens) do
    if swcui.canvas_get(screen.canvas, "enabled") then
      canvases[#canvases + 1] = screen.canvas
    end
  end
  table.sort(canvases, function(a, b) return swcui.canvas_get(a, "drawOrder") > swcui.canvas_get(b, "drawOrder") end)
  for _, canvas in ipairs(canvases) do
    local id = swcui.find_descendant(swcui.canvas_get(canvas, "root"), name)
    if id and id:IsValid() and swcui.enabled_in_hierarchy(id) then
      local p = {swcui.viewport_points(id)}
      fx, fy = fx or 0.5, fy or 0.5
      local tx = p[1] + (p[3] - p[1]) * fx
      local ty = p[2] + (p[4] - p[2]) * fx
      local bx = p[7] + (p[5] - p[7]) * fx
      local by = p[8] + (p[6] - p[8]) * fx
      return string.format("%.1f %.1f", tx + (bx - tx) * fy, ty + (by - ty) * fy)
    end
  end
  return ""
end

--- @return The position of the UI pointer: "x y" in viewport pixels.
function Swc.HostPointer()
  return string.format("%.1f %.1f", input.x, input.y)
end
