-- The input of the UI (the rules of the LyShine canvas, O3DE UiCanvasComponent: facts only): one hover interactable and
-- one active interactable. A move sets the hover (only the active one while one is active) and drags the active one; a
-- press of the left button goes to the front interactable at the point (it becomes active); the release goes to the
-- active one; the wheel goes to the scroll box under the point; keys and text go to the active one (a text input that
-- edits). The host calls Swc.HostInput (the client from Slate, the tests from tests/ui/fixtures/prelude.lua).
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

local function key(name, down)
  if not down then
    return
  end
  if valid(input.active) and Swc.IState(input.active).editing then
    Swc.TextInputKey(input.active, name)
  end
end

local function text(utf8)
  if valid(input.active) and Swc.IState(input.active).editing then
    Swc.TextInputType(input.active, utf8)
  end
end

--- One input event of the host: ("move", x, y), ("button", "left"|"right", down), ("wheel", delta), ("key", name,
--- down), ("text", utf8).
function Swc.HostInput(kind, a, b)
  if kind == "move" then
    move(a, b)
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
  elseif kind == "wheel" then
    wheel(a)
  elseif kind == "key" then
    key(a, b)
  elseif kind == "text" then
    text(a)
  end
  Swc.FlushActions()
end

--- The interactable under the cursor (UiCanvasBus GetHoverInteractable).
function Swc.HoverInteractable()
  return input.hover or EntityId()
end

-- The cursor of the UI (the fork UiCursorBus): the position of the last input event, in viewport pixels.
Swc.Define("UiCursorBus", "Broadcast", {
  GetUiCursorPosition = function() return Swc.Vector2.new({x = input.x, y = input.y}) end,
})
