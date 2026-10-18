-- The controls fixture: original screens and slices with their own scripts, driven by input (Swc.HostInput, as the
-- client). The InfoModal screen: the hover of its button (button.luac: a glow tween), then the press (the button of
-- the game acts on the press: OnPress calls OnSelect and the callback) closes the modal. Pictures:
-- controls-button-normal, controls-button-hover.
local canvas = screen("LyShineUI/InfoModal/InfoModal.uicanvas", 60)
local root = swcui.canvas_get(canvas, "root")
DynamicBus.InfoModal.Broadcast.ShowModal({pagesData = {{title = "@ui_ok", rows = {{type = "Text", text = "@01_lore_manifest_03"}}}}})
frames(0.6)

local container = find(root, "ButtonContainer")
local button
for _, child in ipairs(swcui.children(container)) do
  if swcui.enabled_in_hierarchy(child) and swcui.component(child, "interactable") then
    button = child
  end
end
check(button ~= nil, "the modal shows a button")
if button then
  Swc.HostInput("move", 5, 5)
  frames(0.3)
  picture("controls-button-normal")
  move_to(button)
  equal(Swc.input.hover, button, "the hover interactable is the button")
  frames(0.3)
  picture("controls-button-hover")
  Swc.HostInput("button", "left", true)
  equal(Swc.input.active, button, "the press makes the button active")
  check(Swc.IState(button).pressed, "the button is pressed")
  frames(0.6)
  check(DynamicBus.InfoModal.Broadcast.GetIsModalShowing() == false, "the press of the button closes the modal (its script)")
  equal(LyShineManagerBus.Broadcast.GetCurrentState(), 2702338936, "the state goes back to the HUD")
  Swc.HostInput("button", "left", false)
  check(not Swc.IState(button).pressed, "the release ends the press")
end

-- The original slices of the controls, spawned into the modal (its BaseScreen dispatches their actions), each with its
-- own scripts: sliderwithtextinput (slider.luac, a text input), toggle (two radio buttons in a group), a check box
-- (checkbox.luac), and a drop-down (dropdown.luac: a scroll box, a scroll bar, list items cloned from a prototype).
DynamicBus.InfoModal.Broadcast.ShowModal({pagesData = {{title = "@ui_ok", rows = {{type = "Text", text = "@ui_ok"}}}}})
frames(0.6)
local rows = find(root, "RowContainer")
local function place(id, x, y)
  swcui.set(id, "transform.anchors", 0, 0, 0, 0)
  local l, t, r, b = swcui.get(id, "transform.offsets")
  local px, py = swcui.get(id, "transform.pivot")
  local w, h = r - l, b - t
  if w <= 0 then w = 400 end
  if h <= 0 then h = 40 end
  swcui.set(id, "transform.offsets", x, y, x + w, y + h)
end

-- The slider: a drag along the track sets the value (step and range of the component), the changing action updates
-- the text of the script; the text input: a click, typed digits, Enter set the value through the script.
local sliderRoot = spawn("lyshineui/slices/sliderwithtextinput.dynamicslice", rows)
place(sliderRoot, 40, 60)
frames(0.2)
local slider = find(sliderRoot, "Slider")
check(slider ~= nil and Swc.IState(slider).slider ~= nil, "the slice has its slider")
if slider then
  local track = Swc.IState(slider).slider.trackEntity
  drag(track, 0.1, 0.5, 0.5, 0.5)
  near(UiSliderBus.Event.GetValue(slider), 50, 2, "the drag to the middle of the track sets the value")
  picture("controls-slider")
  local input = find(sliderRoot, "CurrentValueTextInput")
  if input then
    click(input)
    check(Swc.IState(input).editing, "a click on the text input starts the edit")
    key("end")
    for _ = 1, 4 do key("backspace") end
    type_text("25")
    picture("controls-textinput")
    key("enter")
    check(not Swc.IState(input).editing, "Enter ends the edit")
    near(UiSliderBus.Event.GetValue(slider), 25, 0.5, "the typed value goes to the slider (sliderwithtextinput.luac)")
  end
end

-- The toggle: a click on the second radio button of the group checks it and unchecks the first.
local toggle = spawn("lyshineui/slices/toggle.dynamicslice", rows)
place(toggle, 40, 160)
frames(0.2)
local group = find(toggle, "ToggleRadioGroup")
local first, second = find(toggle, "ToggleButton1"), find(toggle, "ToggleButton2")
if group and first and second then
  click(first)
  equal(UiRadioButtonGroupBus.Event.GetState(group), first, "a click checks the first radio button")
  click(second)
  equal(UiRadioButtonGroupBus.Event.GetState(group), second, "a click on the second one checks it")
  check(not UiRadioButtonBus.Event.GetState(first), "the group unchecks the first one")
end

-- The check box: a click toggles it; its checked element follows.
local item = spawn("lyshineui/slices/checkboxdropdownlistitem.dynamicslice", rows)
place(item, 40, 240)
frames(0.2)
local checkbox = find(item, "Checkbox")
if checkbox then
  local before = UiCheckboxBus.Event.GetState(checkbox)
  click(checkbox)
  check(UiCheckboxBus.Event.GetState(checkbox) ~= before, "a click toggles the check box")
  local checked = Swc.IState(checkbox).checkbox.checkedEntity
  if checked then
    equal(swcui.enabled(checked), UiCheckboxBus.Event.GetState(checkbox), "the checked element follows the state")
  end
end

-- The drop-down: the script makes its list (SetListData clones the prototype item); a click expands it (the expanded
-- action of the component calls OnShowDropdown); the wheel scrolls the list; a click on an item selects it.
local dropdownRoot = spawn("lyshineui/slices/dropdown.dynamicslice", rows)
place(dropdownRoot, 40, 320)
frames(0.2)
local dropdown = find(dropdownRoot, "Dropdown") or dropdownRoot
local script = script_of(dropdown, "dropdown.luac")
check(script ~= nil, "the drop-down has its script")
if script then
  local data = {}
  for i = 1, 12 do
    data[i] = {text = "@ui_ok", index = i}
  end
  Swc.Call(script.SetListData, script, data)
  -- A screen sets the height of the list after the spawn (OnInit sets it before the script knows its width).
  Swc.Call(script.SetDropdownListHeight, script, 250)
  frames(0.2)
  click(dropdown)
  frames(0.4)
  check(Swc.IState(dropdown).expanded, "a click expands the drop-down")
  picture("controls-dropdown-open")
  local box = find(root, "DropdownListScrollBox")
  if box then
    move_to(box, 0.5, 0.5)
    local before = UiScrollBoxBus.Event.GetScrollOffsetY(box)
    wheel(-120)
    if UiScrollBoxBus.Event.HasVerticalContentToScroll(box) then
      check(UiScrollBoxBus.Event.GetScrollOffsetY(box) < before, "the wheel scrolls the list")
    end
    picture("controls-dropdown-scrolled")
  end
end
