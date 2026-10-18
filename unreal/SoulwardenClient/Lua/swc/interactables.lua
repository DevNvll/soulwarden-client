-- The interactive components of LyShine: the interactable states with their state actions, the button, the check box,
-- the radio button and its group, the slider, the scroll bar (and the mouse wheel of the fork), the scroll box, the text
-- input, the drop-down, and the markup button. O3DE Gems/LyShine is a reference only (facts, no code). The data comes
-- from the converted canvases (swcui.component); the runtime state is here; swc/input.lua gives the input.
--
-- An action of a component goes to the canvas of the element: UiCanvasNotificationBus OnAction(entity, action name)
-- (the fork BaseScreen calls the function of the action scope: entity:, entityParent:, ancestor:, self:).

local Swc = Swc
local V2 = function(x, y) return Swc.Vector2.new({x = x or 0, y = y or 0}) end

local function valid(id)
  return id ~= nil and type(id) == "userdata" and id:IsValid() and swcui.exists(id)
end

local states = {}          -- tostring(entity) -> the runtime state of the components of an element
Swc.interactiveStates = states
local scrollBoxes = {}     -- tostring(entity) -> entity: the scroll boxes that follow the size of their content
local queued = {}          -- the actions that wait for the end of the input event (button clicks)

--- The runtime state of the interactive components of an element (made from the converted data at the first use).
function Swc.IState(id)
  local k = tostring(id)
  local s = states[k]
  if s == nil then
    s = {id = id, hover = false, pressed = false}
    local data = swcui.component(id, "interactable")
    if data then
      s.kind = data.kind
      s.states = data.states or {}
      s.actions = {hoverStart = data.hoverStart or "", hoverEnd = data.hoverEnd or "", pressed = data.pressed or "",
                   released = data.released or ""}
    end
    for _, name in ipairs(swcui.components(id)) do
      if name ~= "interactable" then
        s[name] = swcui.component(id, name)
      end
    end
    local button = swcui.button(id)
    if button then
      s.button = {action = button.action or "", actionRight = button.actionRight or "",
                  actionPressedRight = button.actionPressedRight or ""}
    end
    states[k] = s
  end
  return s
end

function Swc.ForgetInteractive(id)
  states[tostring(id)] = nil
  scrollBoxes[tostring(id)] = nil
end

--- Sends an action of a component to the canvas of the element (nothing for an empty name).
function Swc.SendAction(id, action)
  if action == nil or action == "" or not valid(id) then
    return
  end
  Swc.Notify("UiCanvasNotificationBus", swcui.canvas_of(id), "OnAction", id, action)
end

--- A queued action (LyShine QueueEvent: a button click goes after the input event, so that the script can delete
--- elements).
function Swc.QueueAction(id, action, notify)
  queued[#queued + 1] = {id = id, action = action, notify = notify}
end

function Swc.FlushActions()
  while #queued > 0 do
    local list = queued
    queued = {}
    for _, q in ipairs(list) do
      Swc.SendAction(q.id, q.action)
      if q.notify then
        Swc.Notify(q.notify[1], q.id, q.notify[2])
      end
    end
  end
end

-- ------------------------------------------------------------------------------------------------ the states

local function target_of(action, id)
  if action.targetEntity and valid(action.targetEntity) then
    return action.targetEntity
  end
  return id
end

local function apply_action(action, id)
  local target = target_of(action, id)
  if not valid(target) then
    return
  end
  if action.kind == "color" and action.color then
    swcui.set(target, "visual.overrideColor", action.color[1], action.color[2], action.color[3], action.color[4])
  elseif action.kind == "alpha" then
    swcui.set(target, "visual.overrideAlpha", action.alpha or 1)
  elseif action.kind == "sprite" and action.sprite then
    swcui.set(target, "visual.overrideSprite", action.sprite, action.index or 0)
  end
end

local function clear_actions(list, id)
  for _, action in ipairs(list or {}) do
    local target = target_of(action, id)
    if valid(target) then
      swcui.set(target, "visual.clearOverrides")
    end
  end
end

--- The state of an interactable (O3DE ComputeInteractableState): disabled when it does not handle events; pressed when
--- pressed and hovered; hover when hovered or pressed; else normal.
local function state_of(s)
  if not s.handling then
    return "disabled"
  end
  if s.pressed and s.hover then
    return "pressed"
  end
  if s.hover or s.pressed then
    return "hover"
  end
  return "normal"
end

--- Applies the state actions of the current state (the overrides of the other states go away first).
function Swc.UpdateInteractableState(id)
  local s = Swc.IState(id)
  if not s.kind then
    return
  end
  s.handling = swcui.get(id, "interactable.handling")
  local state = state_of(s)
  if state == s.appliedState then
    return
  end
  s.appliedState = state
  for _, list in pairs(s.states) do
    clear_actions(list, id)
  end
  for _, action in ipairs(s.states[state] or {}) do
    apply_action(action, id)
  end
end

-- ----------------------------------------------------------------------------------------- the interactable

local function trigger(s, which, notification)
  if notification then
    Swc.Notify("UiInteractableNotificationBus", s.id, notification)
  end
  Swc.SendAction(s.id, s.actions[which])
end

function Swc.InteractableHoverStart(id)
  local s = Swc.IState(id)
  s.hover = true
  trigger(s, "hoverStart", "OnHoverStart")
  Swc.UpdateInteractableState(id)
  if s.dropdown and s.dropdown.expandOnHover and not s.expanded then
    Swc.DropdownExpand(id)
  end
end

function Swc.InteractableHoverEnd(id)
  local s = Swc.IState(id)
  s.hover = false
  trigger(s, "hoverEnd", "OnHoverEnd")
  Swc.UpdateInteractableState(id)
end

-- ------------------------------------------------------------------------------------------------- the slider

local function slider_unit(s)
  local d = s.slider
  local range = math.abs(d.max - d.min)
  return range > 0 and math.abs(d.value - d.min) / range or 0
end

--- UiSliderComponent SetValue: clamp, round to the step, then the anchors of the fill and of the manipulator.
function Swc.SliderSetValue(id, value)
  local s = Swc.IState(id)
  local d = s.slider
  if not d then
    return
  end
  local lo, hi = math.min(d.min, d.max), math.max(d.min, d.max)
  value = math.max(lo, math.min(hi, value))
  if d.step and d.step ~= 0 then
    value = value + d.step / 2
    value = value - math.fmod(value, d.step)
  end
  d.value = value
  local unit = slider_unit(s)
  if valid(d.fillEntity) then
    local l, t, r, b = swcui.get(d.fillEntity, "transform.offsets")
    swcui.set(d.fillEntity, "transform.offsets", 0, t, 0, b)
    local al, at, ar, ab = swcui.get(d.fillEntity, "transform.anchors")
    swcui.set(d.fillEntity, "transform.anchors", 0, at, unit, ab)
  end
  if valid(d.manipulatorEntity) then
    local al, at, ar, ab = swcui.get(d.manipulatorEntity, "transform.anchors")
    swcui.set(d.manipulatorEntity, "transform.anchors", unit, at, unit, ab)
  end
end

local function slider_value_at(s, x, y)
  local d = s.slider
  if not valid(d.trackEntity) then
    return d.value
  end
  local px = swcui.to_rect_space(d.trackEntity, x, y)
  local left, _, right = swcui.rect(d.trackEntity)
  local unit = right > left and (px - left) / (right - left) or 0
  local range = math.abs(d.max - d.min)
  if d.min < d.max then
    return d.min + range * unit
  end
  return d.min - range * unit
end

-- ---------------------------------------------------------------------------------------------- the scroll bar

local function scrollbar_length(id)
  local l, t, r, b = swcui.rect(id)
  local s = Swc.IState(id)
  return s.scrollBar.orientation == 0 and (r - l) or (b - t)
end

--- UiScrollBarComponent DoSetValue: the anchors of the handle along the bar.
local function scrollbar_place(id)
  local s = Swc.IState(id)
  local d = s.scrollBar
  if not valid(d.handleEntity) then
    return
  end
  local size = d.displayedSize or d.handleSize
  local al, at, ar, ab = swcui.get(d.handleEntity, "transform.anchors")
  if d.orientation == 0 then
    al = (1 - size) * d.value
    ar = al + size
  else
    at = (1 - size) * d.value
    ab = at + size
  end
  swcui.set(d.handleEntity, "transform.anchors", al, at, ar, ab)
end

function Swc.ScrollBarSetHandleSize(id, size)
  local s = Swc.IState(id)
  local d = s.scrollBar
  d.handleSize = math.max(0, math.min(1, size))
  local length = scrollbar_length(id)
  local minimum = length > 0 and math.min(1, d.minHandlePixels / length) or 0
  d.displayedSize = math.max(minimum, math.min(1, d.handleSize))
  scrollbar_place(id)
end

function Swc.ScrollBarSetValue(id, value, fromScrollable)
  local s = Swc.IState(id)
  local d = s.scrollBar
  value = math.max(0, math.min(1, value))
  if d.value == value then
    return
  end
  d.value = value
  scrollbar_place(id)
  if fromScrollable then
    Swc.SendAction(id, d.changing)
    return
  end
  Swc.SendAction(id, d.changed)
  -- The scroll box of the bar follows (UiScrollerToScrollableNotificationBus).
  for k, box in pairs(scrollBoxes) do
    local b = Swc.IState(box).scrollBox
    if b and (b.vScrollBarEntity == id or b.hScrollBarEntity == id) then
      Swc.ScrollBoxFromScroller(box, id, value)
    end
  end
end

-- ----------------------------------------------------------------------------------------------- the scroll box

local function content_and_parent(d)
  if not valid(d.contentEntity) then
    return nil
  end
  local parent = swcui.parent(d.contentEntity)
  if not valid(parent) then
    return nil
  end
  return d.contentEntity, parent
end

--- The rect of the content with its own transform (the axis-aligned box), and the rect of its parent.
local function content_rects(d)
  local content, parent = content_and_parent(d)
  if not content then
    return nil
  end
  local cl, ct, cr, cb = swcui.rect(content)
  local pl, pt, pr, pb = swcui.rect(parent)
  return {cl, ct, cr, cb}, {pl, pt, pr, pb}
end

local function constrain(d, offset)
  local c, p = content_rects(d)
  if not c then
    return offset
  end
  local dx, dy = offset.x - d.offset[1], offset.y - d.offset[2]
  local cl, ct, cr, cb = c[1] + dx, c[2] + dy, c[3] + dx, c[4] + dy
  local x, y = offset.x, offset.y
  if cr - cl <= p[3] - p[1] then
    x = 0
  elseif cl > p[1] then
    x = x - (cl - p[1])
  elseif cr < p[3] then
    x = x + (p[3] - cr)
  end
  if cb - ct <= p[4] - p[2] then
    y = 0
  elseif ct > p[2] then
    y = y - (ct - p[2])
  elseif cb < p[4] then
    y = y + (p[4] - cb)
  end
  return V2(x, y)
end

--- O3DE DoSetScrollOffset: the pivot of the content goes to the offset.
local function scrollbox_place(d)
  if not valid(d.contentEntity) then
    return
  end
  local l, t, r, b = swcui.get(d.contentEntity, "transform.offsets")
  local px, py = swcui.get(d.contentEntity, "transform.pivot")
  local w, h = r - l, b - t
  local left = d.offset[1] - w * px
  local top = d.offset[2] - h * py
  swcui.set(d.contentEntity, "transform.offsets", left, top, left + w, top + h)
end

local function normalized(d)
  local c, p = content_rects(d)
  if not c then
    return 0, 0
  end
  local nx, ny = 0, 0
  if c[3] - c[1] > p[3] - p[1] then
    local minimum = d.offset[1] - (c[1] - p[1])
    local maximum = d.offset[1] - (c[3] - p[3])
    nx = (d.offset[1] - minimum) / (maximum - minimum)
  end
  if c[4] - c[2] > p[4] - p[2] then
    local minimum = d.offset[2] - (c[2] - p[2])
    local maximum = d.offset[2] - (c[4] - p[4])
    ny = (d.offset[2] - minimum) / (maximum - minimum)
  end
  return nx, ny
end

local function update_scrollbars(id)
  local d = Swc.IState(id).scrollBox
  local c, p = content_rects(d)
  if not c then
    return
  end
  local nx, ny = normalized(d)
  local cw, ch = c[3] - c[1], c[4] - c[2]
  local pw, ph = p[3] - p[1], p[4] - p[2]
  for _, bar in ipairs({{d.hScrollBarEntity, cw > 0 and pw / cw or 1, nx, d.hVisibility, cw > pw},
                        {d.vScrollBarEntity, ch > 0 and ph / ch or 1, ny, d.vVisibility, ch > ph}}) do
    local barId = bar[1]
    if valid(barId) and Swc.IState(barId).scrollBar then
      Swc.ScrollBarSetHandleSize(barId, bar[2])
      local sb = Swc.IState(barId).scrollBar
      if sb.value ~= bar[3] then
        sb.value = math.max(0, math.min(1, bar[3]))
        scrollbar_place(barId)
      end
      -- Visibility: 0 always, 1 auto hide, 2 auto hide and resize the view (the view part: open point).
      if bar[4] ~= 0 then
        swcui.set_enabled(barId, bar[5])
      end
    end
  end
end

function Swc.ScrollBoxSetOffset(id, offset, changing)
  local d = Swc.IState(id).scrollBox
  if not d then
    return
  end
  if d.constrain then
    offset = constrain(d, offset)
  end
  if offset.x == d.offset[1] and offset.y == d.offset[2] then
    return
  end
  d.offset = {offset.x, offset.y}
  scrollbox_place(d)
  update_scrollbars(id)
  if Swc.IState(id).dynamicScrollBox then
    Swc.DynamicScrollBoxUpdate(id)
  end
  Swc.SendAction(id, changing and d.changing or d.changed)
  Swc.Notify("UiScrollBoxNotificationBus", id, changing and "OnScrollOffsetChanging" or "OnScrollOffsetChanged", V2(d.offset[1], d.offset[2]))
end

--- A scroll bar moved: the offset of the box from the normalized value (O3DE NormalizedScrollValueToScrollOffset).
function Swc.ScrollBoxFromScroller(id, bar, value)
  local d = Swc.IState(id).scrollBox
  local c, p = content_rects(d)
  if not c then
    return
  end
  if bar == d.vScrollBarEntity and d.vertical and c[4] - c[2] > p[4] - p[2] then
    local minimum = d.offset[2] - (c[2] - p[2])
    local maximum = d.offset[2] - (c[4] - p[4])
    d.offset[2] = minimum + (maximum - minimum) * value
  elseif bar == d.hScrollBarEntity and d.horizontal and c[3] - c[1] > p[3] - p[1] then
    local minimum = d.offset[1] - (c[1] - p[1])
    local maximum = d.offset[1] - (c[3] - p[3])
    d.offset[1] = minimum + (maximum - minimum) * value
  else
    return
  end
  scrollbox_place(d)
  if Swc.IState(id).dynamicScrollBox then
    Swc.DynamicScrollBoxUpdate(id)
  end
  Swc.SendAction(id, d.changing)
end

--- The wheel over a scroll box: the fork UiScrollBarMouseWheelComponent moves by its step value for each notch (120 in
--- a notch of the wheel; open point: the source of the fork is not known).
function Swc.ScrollBoxWheel(id, delta)
  local s = Swc.IState(id)
  local d = s.scrollBox
  if not d then
    return false
  end
  local step = s.wheel and s.wheel.step or 120
  local amount = delta / 120 * step
  if d.vertical then
    Swc.ScrollBoxSetOffset(id, V2(d.offset[1], d.offset[2] + amount))
  elseif d.horizontal then
    Swc.ScrollBoxSetOffset(id, V2(d.offset[1] + amount, d.offset[2]))
  end
  return true
end

-- ----------------------------------------------------------------------------------------------- the text input

local function utf8_chars(text)
  local out = {}
  for ch in string.gmatch(text or "", "[%z\1-\127\194-\244][\128-\191]*") do
    out[#out + 1] = ch
  end
  return out
end

local function text_input_text(s)
  local d = s.textInput
  return valid(d.textEntity) and (UiTextBus.Event.GetText(d.textEntity) or "") or ""
end

--- The displayed text (a password shows the replacement character) and the placeholder (on with an empty text).
local function text_input_show(s)
  local d = s.textInput
  local text = s.textValue or text_input_text(s)
  if valid(d.textEntity) then
    local shown = text
    if d.password then
      shown = string.rep(string.char(math.min(d.replacement or 42, 127)), #utf8_chars(text))
    end
    swcui.set(d.textEntity, "text.text", shown)
    swcui.set(d.textEntity, "text.cursor", s.editing and (s.cursor or #utf8_chars(text)) or -1,
              d.cursorColor[1], d.cursorColor[2], d.cursorColor[3], d.cursorColor[4])
  end
  if valid(d.placeholderEntity) then
    swcui.set_enabled(d.placeholderEntity, text == "")
  end
end

function Swc.TextInputSetText(id, text)
  local s = Swc.IState(id)
  s.textValue = text or ""
  local n = #utf8_chars(s.textValue)
  if s.cursor and s.cursor > n then
    s.cursor = n
  end
  text_input_show(s)
end

function Swc.TextInputBeginEdit(id)
  local s = Swc.IState(id)
  if s.editing then
    return
  end
  s.editing = true
  s.textValue = s.textValue or text_input_text(s)
  s.cursor = #utf8_chars(s.textValue)
  text_input_show(s)
  Swc.SendAction(id, s.textInput.startEdit)
end

function Swc.TextInputEndEdit(id)
  local s = Swc.IState(id)
  if not s.editing then
    return
  end
  s.editing = false
  s.cursor = nil
  text_input_show(s)
  Swc.SendAction(id, s.textInput.endEdit)
  Swc.Notify("UiTextInputNotificationBus", id, "OnTextInputEndEdit", s.textValue or "")
end

local function text_input_change(s, text)
  s.textValue = text
  text_input_show(s)
  Swc.SendAction(s.id, s.textInput.change)
  Swc.Notify("UiTextInputNotificationBus", s.id, "OnTextInputChange", text)
end

--- Typed text (O3DE HandleTextInput): a backspace erases; a character goes in at the cursor (the maximum length stops it).
function Swc.TextInputType(id, utf8)
  local s = Swc.IState(id)
  if not s.editing or not s.handling then
    return false
  end
  local chars = utf8_chars(s.textValue)
  local cursor = s.cursor or #chars
  if utf8 == "\b" or utf8 == "\127" then
    if cursor > 0 then
      table.remove(chars, cursor)
      s.cursor = cursor - 1
      text_input_change(s, table.concat(chars))
    end
    return true
  end
  if #utf8 == 1 and utf8:byte() < 32 then
    return false
  end
  local max = s.textInput.maxLength or -1
  for _, ch in ipairs(utf8_chars(utf8)) do
    if max >= 0 and #chars >= max then
      break
    end
    table.insert(chars, cursor + 1, ch)
    cursor = cursor + 1
  end
  s.cursor = cursor
  text_input_change(s, table.concat(chars))
  return true
end

--- The keys of a text input that edits: Enter (the enter action, the end of the edit), Escape (the end), Delete, the
--- arrows, Home, End.
function Swc.TextInputKey(id, key)
  local s = Swc.IState(id)
  if not s.editing then
    return false
  end
  local chars = utf8_chars(s.textValue)
  local cursor = s.cursor or #chars
  if key == "enter" then
    Swc.SendAction(id, s.textInput.enter)
    Swc.Notify("UiTextInputNotificationBus", id, "OnTextInputEnter", s.textValue or "")
    Swc.TextInputEndEdit(id)
  elseif key == "escape" then
    Swc.TextInputEndEdit(id)
  elseif key == "backspace" then
    Swc.TextInputType(id, "\b")
  elseif key == "delete" then
    if cursor < #chars then
      table.remove(chars, cursor + 1)
      text_input_change(s, table.concat(chars))
    end
  elseif key == "left" then
    s.cursor = math.max(0, cursor - 1)
    text_input_show(s)
  elseif key == "right" then
    s.cursor = math.min(#chars, cursor + 1)
    text_input_show(s)
  elseif key == "home" then
    s.cursor = 0
    text_input_show(s)
  elseif key == "end" then
    s.cursor = #chars
    text_input_show(s)
  else
    return false
  end
  return true
end

-- ------------------------------------------------------------------------------------------------- the drop-down

local expandedDropdowns = {}

function Swc.DropdownExpand(id)
  local s = Swc.IState(id)
  local d = s.dropdown
  if not d or s.expanded then
    return
  end
  s.expanded = true
  expandedDropdowns[tostring(id)] = id
  if valid(d.contentEntity) then
    swcui.set_enabled(d.contentEntity, true)
    -- The content goes on top of the canvas (O3DE: under a new element at the end of the root) at the same place.
    local x, y = swcui.viewport_position(d.contentEntity)
    s.baseParent = swcui.parent(d.contentEntity)
    local top = valid(d.expandedParentEntity) and d.expandedParentEntity or swcui.canvas_get(swcui.canvas_of(id), "root")
    swcui.reparent(d.contentEntity, top, nil)
    swcui.set_viewport_position(d.contentEntity, x, y)
  end
  for _, action in ipairs(d.expandedStates or {}) do
    apply_action(action, id)
  end
  Swc.SendAction(id, d.expanded)
  Swc.Notify("UiDropdownNotificationBus", id, "OnDropdownExpanded")
end

function Swc.DropdownCollapse(id)
  local s = Swc.IState(id)
  local d = s.dropdown
  if not d or not s.expanded then
    return
  end
  s.expanded = false
  expandedDropdowns[tostring(id)] = nil
  if valid(d.contentEntity) then
    local x, y = swcui.viewport_position(d.contentEntity)
    if valid(s.baseParent) then
      swcui.reparent(d.contentEntity, s.baseParent, nil)
      swcui.set_viewport_position(d.contentEntity, x, y)
    end
    swcui.set_enabled(d.contentEntity, false)
  end
  clear_actions(d.expandedStates, id)
  Swc.SendAction(id, d.collapsed)
  Swc.Notify("UiDropdownNotificationBus", id, "OnDropdownCollapsed")
end

--- A release in the canvas (O3DE OnCanvasPrimaryReleased): an option of the drop-down or a point outside its content
--- collapses it.
function Swc.DropdownCanvasReleased(target)
  for k, id in pairs(expandedDropdowns) do
    local s = Swc.IState(id)
    if target ~= id then
      local option = target and Swc.IState(target).owningDropdown
      if option == id then
        Swc.DropdownCollapse(id)
      elseif s.dropdown.collapseOnOutsideClick then
        local content = s.dropdown.contentEntity
        local inside = false
        local at = target
        while valid(at) do
          if at == content then
            inside = true
            break
          end
          at = swcui.parent(at)
        end
        if not inside then
          Swc.DropdownCollapse(id)
        end
      end
    end
  end
end

-- --------------------------------------------------------------------------------------- press, release, drag

--- A press on an interactable. @return handled, stay active (O3DE HandlePressed).
function Swc.InteractablePressed(id, x, y)
  local s = Swc.IState(id)
  s.handling = swcui.get(id, "interactable.handling")
  if not s.handling then
    return false, false
  end
  s.pressed = true
  s.pressPoint = {x, y}
  s.dragging = false
  trigger(s, "pressed", "OnPressed")
  Swc.UpdateInteractableState(id)
  if s.scrollBox and s.scrollBox.dragging then
    s.pressOffset = {s.scrollBox.offset[1], s.scrollBox.offset[2]}
  end
  if s.scrollBar then
    s.pressValue = s.scrollBar.value
  end
  return true, s.kind == "textInput"
end

local function released_inside(id, x, y)
  return swcui.contains(id, x, y)
end

function Swc.InteractableReleased(id, x, y)
  local s = Swc.IState(id)
  local wasPressed = s.pressed
  s.pressed = false
  s.dragging = false
  if not s.handling then
    Swc.UpdateInteractableState(id)
    return
  end
  local inside = released_inside(id, x, y)
  local kind = s.kind
  if kind == "slider" and wasPressed then
    Swc.SliderSetValue(id, slider_value_at(s, x, y))
    trigger(s, "released", "OnReleased")
    Swc.SendAction(id, s.slider.changed)
  elseif kind == "checkbox" and inside then
    s.checkbox.checked = not s.checkbox.checked
    Swc.CheckboxShow(id)
    trigger(s, "released", "OnReleased")
    Swc.SendAction(id, s.checkbox.checked and s.checkbox.turnOn or s.checkbox.turnOff)
    Swc.SendAction(id, s.checkbox.changed)
    Swc.Notify("UiCheckboxNotificationBus", id, "OnCheckboxStateChange", s.checkbox.checked)
  elseif kind == "radio" and inside then
    trigger(s, "released", "OnReleased")
    Swc.RadioRequest(id, not s.radio.checked)
  elseif kind == "textInput" then
    if not s.editing and inside then
      Swc.TextInputBeginEdit(id)
    end
    trigger(s, "released", "OnReleased")
  elseif kind == "dropdown" and inside then
    trigger(s, "released", "OnReleased")
    if not s.expanded then
      Swc.DropdownExpand(id)
    elseif not s.dropdown.expandOnHover then
      Swc.DropdownCollapse(id)
    end
  elseif kind == "scrollBox" or kind == "scrollBar" then
    trigger(s, "released", "OnReleased")
    if s.scrollBox and s.moved then
      Swc.SendAction(id, s.scrollBox.changed)
    end
    if s.scrollBar and s.moved then
      Swc.SendAction(id, s.scrollBar.changed)
    end
  elseif inside then
    trigger(s, "released", "OnReleased")
    if s.button then
      Swc.QueueAction(id, s.button.action, {"UiButtonNotificationBus", "OnButtonClick"})
    end
  else
    trigger(s, "released", "OnReleased")
  end
  s.moved = false
  Swc.UpdateInteractableState(id)
end

--- The cursor moved while an interactable is active (O3DE InputPositionUpdate): a slider, a scroll bar, or a scroll box
--- drags.
function Swc.InteractableDrag(id, x, y)
  local s = Swc.IState(id)
  if not s.pressed then
    return
  end
  if s.slider and valid(s.slider.trackEntity) then
    Swc.SliderSetValue(id, slider_value_at(s, x, y))
    Swc.SendAction(id, s.slider.changing)
  elseif s.scrollBar and valid(s.scrollBar.handleEntity) then
    local d = s.scrollBar
    local length = scrollbar_length(id)
    local travel = length * (1 - (d.displayedSize or d.handleSize))
    if travel > 0 then
      local p0 = {swcui.to_rect_space(id, s.pressPoint[1], s.pressPoint[2])}
      local p1 = {swcui.to_rect_space(id, x, y)}
      local delta = d.orientation == 0 and (p1[1] - p0[1]) or (p1[2] - p0[2])
      s.moved = true
      Swc.ScrollBarSetValue(id, s.pressValue + delta / travel)
    end
  elseif s.scrollBox and s.scrollBox.dragging and s.pressOffset then
    local d = s.scrollBox
    local p0 = {swcui.to_rect_space(id, s.pressPoint[1], s.pressPoint[2])}
    local p1 = {swcui.to_rect_space(id, x, y)}
    local ox = d.horizontal and s.pressOffset[1] + (p1[1] - p0[1]) or d.offset[1]
    local oy = d.vertical and s.pressOffset[2] + (p1[2] - p0[2]) or d.offset[2]
    s.moved = true
    Swc.ScrollBoxSetOffset(id, V2(ox, oy), true)
  end
end

--- The active interactable lost its active status (a press elsewhere): a text input ends its edit.
function Swc.InteractableLostActive(id)
  local s = Swc.IState(id)
  if s.editing then
    Swc.TextInputEndEdit(id)
  end
  -- LyShine LostActiveStatus: the press ends (a canvas that hid during a press).
  if s.pressed then
    s.pressed = false
    Swc.UpdateInteractableState(id)
  end
end

-- ------------------------------------------------------------------------- check box, radio button, radio group

function Swc.CheckboxShow(id)
  local d = Swc.IState(id).checkbox
  if valid(d.checkedEntity) then
    swcui.set_enabled(d.checkedEntity, d.checked)
  end
  if valid(d.uncheckedEntity) then
    swcui.set_enabled(d.uncheckedEntity, not d.checked)
  end
end

local function radio_set(id, on, notify)
  local s = Swc.IState(id)
  local d = s.radio
  if d.checked == on then
    return
  end
  d.checked = on
  if valid(d.checkedEntity) then
    swcui.set_enabled(d.checkedEntity, on)
  end
  if valid(d.uncheckedEntity) then
    swcui.set_enabled(d.uncheckedEntity, not on)
  end
  if notify then
    Swc.SendAction(id, on and d.turnOn or d.turnOff)
    Swc.SendAction(id, d.changed)
    Swc.Notify("UiRadioButtonNotificationBus", id, "OnRadioButtonStateChange", on)
  end
end

local function group_of(id)
  local g = Swc.IState(id).radio and Swc.IState(id).radio.groupEntity
  return valid(g) and g or nil
end

local function group_state(group)
  local s = Swc.IState(group)
  s.radioGroup = s.radioGroup or {allowUncheck = false, changed = ""}
  s.members = s.members or {}
  return s
end

--- O3DE SetStateCommon of the radio button group.
function Swc.RadioGroupSet(group, radio, on, notify)
  local g = group_state(group)
  if not valid(radio) or not g.members[tostring(radio)] then
    return
  end
  if on then
    if g.checked and g.checked ~= radio then
      radio_set(g.checked, false, notify)
    end
    g.checked = radio
    radio_set(radio, true, notify)
  elseif g.radioGroup.allowUncheck and radio == g.checked then
    radio_set(radio, false, notify)
    g.checked = nil
  else
    return
  end
  if notify then
    Swc.SendAction(group, g.radioGroup.changed)
    Swc.Notify("UiRadioButtonGroupNotificationBus", group, "OnRadioButtonGroupStateChange", g.checked or EntityId())
  end
end

function Swc.RadioRequest(radio, on)
  local group = group_of(radio)
  if group then
    Swc.RadioGroupSet(group, radio, on, true)
  end
end

function Swc.RadioGroupAdd(group, radio)
  local g = group_state(group)
  g.members[tostring(radio)] = radio
  if Swc.IState(radio).radio then
    Swc.IState(radio).radio.groupEntity = group
  end
end

-- ------------------------------------------------------------------------------------------- the activation

--- The component part of InGamePostActivate (before the scripts of the element): the states of the check boxes and
--- radio buttons, the value of a slider, the offset of a scroll box, the text of a text input, the state actions.
function Swc.InitInteractive(id)
  local kinds = swcui.components(id)
  if #kinds == 0 then
    return
  end
  local s = Swc.IState(id)
  if s.checkbox then
    Swc.CheckboxShow(id)
  end
  if s.radio then
    local group = group_of(id)
    if group then
      Swc.RadioGroupAdd(group, id)
      if s.radio.checked then
        s.radio.checked = false
        Swc.RadioGroupSet(group, id, true, false)
      else
        radio_set(id, true, false)
        radio_set(id, false, false)
      end
    end
  end
  if s.slider then
    Swc.SliderSetValue(id, s.slider.value)
  end
  if s.scrollBar then
    Swc.ScrollBarSetHandleSize(id, s.scrollBar.handleSize)
  end
  if s.scrollBox then
    scrollBoxes[tostring(id)] = id
    s.scrollBox.offset = s.scrollBox.offset or {0, 0}
    scrollbox_place(s.scrollBox)
    update_scrollbars(id)
  end
  if s.textInput then
    s.textValue = text_input_text(s)
    text_input_show(s)
  end
  if s.kind then
    Swc.UpdateInteractableState(id)
  end
  if s.dynamicLayout or s.dynamicScrollBox then
    Swc.InitDynamic(id)
  end
end

--- Each frame: a scroll box follows the size of its content (its bars and its constraint).
function Swc.TickInteractables()
  for k, id in pairs(scrollBoxes) do
    if not swcui.exists(id) then
      scrollBoxes[k] = nil
    else
      local d = Swc.IState(id).scrollBox
      local c = content_rects(d)
      if c then
        local size = (c[3] - c[1]) .. "x" .. (c[4] - c[2])
        if size ~= d.lastSize then
          d.lastSize = size
          if d.constrain then
            local offset = constrain(d, V2(d.offset[1], d.offset[2]))
            if offset.x ~= d.offset[1] or offset.y ~= d.offset[2] then
              d.offset = {offset.x, offset.y}
              scrollbox_place(d)
            end
          end
          update_scrollbars(id)
        end
      end
    end
  end
  Swc.FlushActions()
end

-- ------------------------------------------------------------------------------------------------------ buses

Swc.Define("UiInteractableBus", "Event", {
  IsHandlingEvents = function(id) return valid(id) and swcui.get(id, "interactable.handling") or false end,
  SetIsHandlingEvents = function(id, v)
    if valid(id) then
      swcui.set(id, "interactable.handling", v and true or false)
      Swc.UpdateInteractableState(id)
    end
  end,
  IsPressed = function(id) return Swc.IState(id).pressed end,
  GetIsAutoActivationEnabled = function() return false end,
  IsAutoActivationEnabled = function() return false end,
  SetIsAutoActivationEnabled = function() end,
  SetStayActiveAfterRelease = function(id, v) Swc.IState(id).stayActive = v end,
  SetHoverEnterEventHandlingScale = function() end,
  SetMagnetismMode = function() end,
  SetFrictionOverrideValue = function() end,
  SetIsVirtualCursorEffector = function() end,
})

local function action_accessors()
  local out = {}
  for name, field in pairs({HoverStartActionName = "hoverStart", HoverEndActionName = "hoverEnd",
                            PressedActionName = "pressed", ReleasedActionName = "released"}) do
    out["Get" .. name] = function(id) local a = Swc.IState(id).actions return a and a[field] or "" end
    out["Set" .. name] = function(id, v)
      local s = Swc.IState(id)
      s.actions = s.actions or {hoverStart = "", hoverEnd = "", pressed = "", released = ""}
      s.actions[field] = v or ""
    end
  end
  return out
end
Swc.Define("UiInteractableActionsBus", "Event", action_accessors())

Swc.Define("UiButtonBus", "Event", {
  GetOnClickActionName = function(id) local b = Swc.IState(id).button return b and b.action or "" end,
  SetOnClickActionName = function(id, v)
    local s = Swc.IState(id)
    s.button = s.button or {action = "", actionRight = "", actionPressedRight = ""}
    s.button.action = v or ""
  end,
  SetUseClickBehavior = function() end,
})

Swc.Define("UiCheckboxBus", "Event", {
  GetState = function(id) local d = Swc.IState(id).checkbox return d and d.checked or false end,
  SetState = function(id, on)
    local d = Swc.IState(id).checkbox
    if d then
      d.checked = on and true or false
      Swc.CheckboxShow(id)
    end
  end,
  ToggleState = function(id)
    local d = Swc.IState(id).checkbox
    if d then
      d.checked = not d.checked
      Swc.CheckboxShow(id)
    end
  end,
  GetCheckedEntity = function(id) local d = Swc.IState(id).checkbox return d and d.checkedEntity or EntityId() end,
  GetUncheckedEntity = function(id) local d = Swc.IState(id).checkbox return d and d.uncheckedEntity or EntityId() end,
})

Swc.Define("UiRadioButtonBus", "Event", {
  GetState = function(id) local d = Swc.IState(id).radio return d and d.checked or false end,
  GetGroup = function(id) return group_of(id) or EntityId() end,
})

Swc.Define("UiRadioButtonGroupBus", "Event", {
  GetState = function(group) return group_state(group).checked or EntityId() end,
  SetState = function(group, radio, on) Swc.RadioGroupSet(group, radio, on, false) end,
  AddRadioButton = function(group, radio) Swc.RadioGroupAdd(group, radio) end,
  RemoveRadioButton = function(group, radio)
    local g = group_state(group)
    g.members[tostring(radio)] = nil
    if g.checked == radio then
      g.checked = nil
    end
  end,
  ContainsRadioButton = function(group, radio) return group_state(group).members[tostring(radio)] ~= nil end,
  GetAllowUncheck = function(group) return group_state(group).radioGroup.allowUncheck end,
  SetAllowUncheck = function(group, v) group_state(group).radioGroup.allowUncheck = v and true or false end,
})

Swc.Define("UiSliderBus", "Event", {
  GetValue = function(id) local d = Swc.IState(id).slider return d and d.value or 0 end,
  SetValue = function(id, v) Swc.SliderSetValue(id, v) end,
  GetMinValue = function(id) local d = Swc.IState(id).slider return d and d.min or 0 end,
  SetMinValue = function(id, v) local d = Swc.IState(id).slider if d then d.min = v Swc.SliderSetValue(id, d.value) end end,
  GetMaxValue = function(id) local d = Swc.IState(id).slider return d and d.max or 0 end,
  SetMaxValue = function(id, v) local d = Swc.IState(id).slider if d then d.max = v Swc.SliderSetValue(id, d.value) end end,
  GetStepValue = function(id) local d = Swc.IState(id).slider return d and d.step or 0 end,
  SetStepValue = function(id, v) local d = Swc.IState(id).slider if d then d.step = v end end,
  SetIsValueChangingEnabled = function() end,
  GetValueChangingActionName = function(id) local d = Swc.IState(id).slider return d and d.changing or "" end,
  SetValueChangingActionName = function(id, v) local d = Swc.IState(id).slider if d then d.changing = v end end,
  GetValueChangedActionName = function(id) local d = Swc.IState(id).slider return d and d.changed or "" end,
  SetValueChangedActionName = function(id, v) local d = Swc.IState(id).slider if d then d.changed = v end end,
})

Swc.Define("UiScrollerBus", "Event", {
  GetValue = function(id) local d = Swc.IState(id).scrollBar return d and d.value or 0 end,
  SetValue = function(id, v) if Swc.IState(id).scrollBar then Swc.ScrollBarSetValue(id, v) end end,
})
Swc.Define("UiScrollBarBus", "Event", {
  GetHandleSize = function(id) local d = Swc.IState(id).scrollBar return d and d.handleSize or 0 end,
  SetHandleSize = function(id, v) if Swc.IState(id).scrollBar then Swc.ScrollBarSetHandleSize(id, v) end end,
  GetMinHandlePixelSize = function(id) local d = Swc.IState(id).scrollBar return d and d.minHandlePixels or 0 end,
  SetMinHandlePixelSize = function(id, v) local d = Swc.IState(id).scrollBar if d then d.minHandlePixels = v end end,
})

local function box(id)
  return Swc.IState(id).scrollBox
end

Swc.Define("UiScrollBoxBus", "Event", {
  GetScrollOffset = function(id) local d = box(id) return d and V2(d.offset[1], d.offset[2]) or V2(0, 0) end,
  SetScrollOffset = function(id, v) Swc.ScrollBoxSetOffset(id, V2(v.x, v.y)) end,
  GetScrollOffsetX = function(id) local d = box(id) return d and d.offset[1] or 0 end,
  GetScrollOffsetY = function(id) local d = box(id) return d and d.offset[2] or 0 end,
  SetScrollOffsetX = function(id, x) local d = box(id) if d then Swc.ScrollBoxSetOffset(id, V2(x, d.offset[2])) end end,
  SetScrollOffsetY = function(id, y) local d = box(id) if d then Swc.ScrollBoxSetOffset(id, V2(d.offset[1], y)) end end,
  GetScrollOffsetMin = function(id)
    local d = box(id)
    local c, p
    if d then c, p = content_rects(d) end
    if not c then return V2(0, 0) end
    return V2(math.min(0, d.offset[1] - (c[3] - p[3])), math.min(0, d.offset[2] - (c[4] - p[4])))
  end,
  GetScrollOffsetMax = function(id)
    local d = box(id)
    local c, p
    if d then c, p = content_rects(d) end
    if not c then return V2(0, 0) end
    return V2(math.max(0, d.offset[1] - (c[1] - p[1])), math.max(0, d.offset[2] - (c[2] - p[2])))
  end,
  GetNormalizedScrollValue = function(id) local d = box(id) if not d then return V2(0, 0) end return V2(normalized(d)) end,
  HasVerticalContentToScroll = function(id)
    local d = box(id)
    local c, p
    if d then c, p = content_rects(d) end
    return c ~= nil and (c[4] - c[2]) > (p[4] - p[2])
  end,
  HasHorizontalContentToScroll = function(id)
    local d = box(id)
    local c, p
    if d then c, p = content_rects(d) end
    return c ~= nil and (c[3] - c[1]) > (p[3] - p[1])
  end,
  GetContentEntity = function(id) local d = box(id) return d and d.contentEntity or EntityId() end,
  SetContentEntity = function(id, e) local d = box(id) if d then d.contentEntity = e end end,
  GetIsScrollingConstrained = function(id) local d = box(id) return d and d.constrain or false end,
  SetIsScrollingConstrained = function(id, v) local d = box(id) if d then d.constrain = v end end,
  GetIsVerticalScrollingEnabled = function(id) local d = box(id) return d and d.vertical or false end,
  SetIsVerticalScrollingEnabled = function(id, v) local d = box(id) if d then d.vertical = v end end,
  GetIsHorizontalScrollingEnabled = function(id) local d = box(id) return d and d.horizontal or false end,
  SetIsHorizontalScrollingEnabled = function(id, v) local d = box(id) if d then d.horizontal = v end end,
  GetVerticalScrollBarEntity = function(id) local d = box(id) return d and d.vScrollBarEntity or EntityId() end,
  GetHorizontalScrollBarEntity = function(id) local d = box(id) return d and d.hScrollBarEntity or EntityId() end,
})

Swc.Define("UiTextInputBus", "Event", {
  GetText = function(id) local s = Swc.IState(id) if not s.textInput then return "" end return s.textValue or text_input_text(s) end,
  SetText = function(id, text) if Swc.IState(id).textInput then Swc.TextInputSetText(id, text) end end,
  BeginEdit = function(id) if Swc.IState(id).textInput then Swc.TextInputBeginEdit(id) end end,
  EndEdit = function(id) if Swc.IState(id).textInput then Swc.TextInputEndEdit(id) end end,
  GetMaxStringLength = function(id) local d = Swc.IState(id).textInput return d and d.maxLength or -1 end,
  SetMaxStringLength = function(id, v) local d = Swc.IState(id).textInput if d then d.maxLength = v end end,
  GetIsPasswordField = function(id) local d = Swc.IState(id).textInput return d and d.password or false end,
  SetIsPasswordField = function(id, v)
    local s = Swc.IState(id)
    if s.textInput then
      s.textInput.password = v and true or false
      text_input_show(s)
    end
  end,
  GetTextEntity = function(id) local d = Swc.IState(id).textInput return d and d.textEntity or EntityId() end,
  GetPlaceHolderTextEntity = function(id) local d = Swc.IState(id).textInput return d and d.placeholderEntity or EntityId() end,
  SetTextSelectionColor = function(id, c) local d = Swc.IState(id).textInput if d and c then d.selectionColor = {c.r, c.g, c.b, c.a} end end,
  SetTextCursorColor = function(id, c) local d = Swc.IState(id).textInput if d and c then d.cursorColor = {c.r, c.g, c.b, c.a} end end,
  SetBlockVirtualKeyboardAndNotifyScript = function() end,
})

Swc.Define("UiDropdownBus", "Event", {
  Expand = function(id) Swc.DropdownExpand(id) end,
  Collapse = function(id) Swc.DropdownCollapse(id) end,
  GetValue = function(id) return Swc.IState(id).dropdownValue or EntityId() end,
  SetValue = function(id, option)
    local s = Swc.IState(id)
    s.dropdownValue = option
    if s.dropdown then
      Swc.SendAction(id, s.dropdown.optionSelected)
    end
    Swc.Notify("UiDropdownNotificationBus", id, "OnDropdownValueChanged", option)
  end,
  SetExpandedParentId = function(id, parent) local d = Swc.IState(id).dropdown if d then d.expandedParentEntity = parent end end,
  GetExpandedParentId = function(id) local d = Swc.IState(id).dropdown return d and d.expandedParentEntity or EntityId() end,
  CollapseAnimationFinish = function() end,
})

Swc.Define("UiDropdownOptionBus", "Event", {
  GetOwningDropdown = function(id) return Swc.IState(id).owningDropdown or EntityId() end,
  SetOwningDropdown = function(id, dropdown) Swc.IState(id).owningDropdown = dropdown end,
})

Swc.Define("UiMarkupButtonBus", "Event", {
  SetLinkColor = function(id, c) local d = Swc.IState(id).markupButton if d and c then d.linkColor = {c.r, c.g, c.b, c.a} end end,
  SetLinkHoverColor = function(id, c) local d = Swc.IState(id).markupButton if d and c then d.linkHoverColor = {c.r, c.g, c.b, c.a} end end,
  SetOverrideLinkColor = function() end,
  ClearOverrideLinkColors = function() end,
})

Swc.Handled("UiCanvasNotificationBus", "UiInteractableNotificationBus", "UiButtonNotificationBus",
            "UiCheckboxNotificationBus", "UiRadioButtonNotificationBus", "UiRadioButtonGroupNotificationBus",
            "UiScrollBoxNotificationBus", "UiTextInputNotificationBus", "UiDropdownNotificationBus")
