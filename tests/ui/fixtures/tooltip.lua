-- The tooltip fixture: a simple tooltip of the game through the original scripts. The escape menu gives its buttons the
-- tooltip "@ui_button_disabled_dead" while the character is dead (escapemenu.luac, the dead mark of the data layer).
-- The hover of a button starts its tooltip setter (tooltipsetter.luac): after its TooltipDelay the Tooltip screen
-- (tooltip.luac ShowTooltip) shows the DynamicTooltip at the cursor (CursorBus GetCursorPosition) with the flavor
-- section (dynamictooltip_flavor.luac: the text of LocalizeWithDataSheetData, the width of the text and its padding).
-- The end of the hover hides it. With the character alive, the buttons have no tooltip.
-- Pictures: tooltip-dead.
local HUD, ESC = 2702338936, 1643432462
local menu = screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 41)
local tooltip = screen("LyShineUI/Tooltip/Tooltip.uicanvas", 90, true)
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(0, 5330, 100, 100, 100, 100, true)
frames(0.3)
LyShineManagerBus.Broadcast.SetState(ESC)
frames(0.6)
check(swcui.canvas_get(menu, "enabled"), "the escape menu shows")
check(swcui.canvas_get(tooltip, "enabled"), "the tooltip screen shows (a HUD screen)")

local root = swcui.canvas_get(menu, "root")
local troot = swcui.canvas_get(tooltip, "root")
local dynamic = find(troot, "DynamicTooltip")
local flavor = dynamic and find(dynamic, "Flavor")
local flavorText = flavor and find(flavor, "Text")
check(dynamic ~= nil and flavorText ~= nil, "the tooltip screen has the dynamic tooltip and its flavor text")
local function shown()
  return dynamic ~= nil and swcui.enabled_in_hierarchy(dynamic)
end
check(not shown(), "no tooltip before a hover")

local settings = find(root, "ButtonSettings")
local setter = settings and script_of(settings).ButtonTooltipSetter
check(setter ~= nil and setter.tooltipInfo ~= nil, "the dead mark gives the settings button a tooltip")
local delay = setter and setter.Properties.TooltipDelay or 0
note(string.format("the tooltip delay of the button: %.2f s", delay))

-- The hover: no tooltip before the delay, the tooltip at the cursor after it.
local x, y = move_to(settings, 0.5, 0.5)
frames(math.max(delay * 0.5, 1 / 60))
if delay > 0.05 then
  check(not shown(), "no tooltip before the delay")
end
frames(delay + 0.3)
check(shown(), "the tooltip shows after the delay")
if shown() then
  local p = {swcui.viewport_points(dynamic)}
  near(p[1], x, 2, "the left edge of the tooltip is at the cursor")
  near(p[2], y, 2, "the top edge of the tooltip is at the cursor")
  local layout = swcui.text_layout(flavorText)
  local text = layout.lines[1] and layout.lines[1].text or ""
  local expected = swcui.localize("@ui_button_disabled_dead")
  check(expected ~= "@ui_button_disabled_dead", "the text key has an English text")
  check(text ~= "" and string.find(expected, text, 1, true) == 1,
    "the tooltip shows the text of the key (" .. text .. ")")
  -- dynamictooltip_flavor.luac GetWidth: the width of the text and PADDING_WIDTH 30.
  near(p[3] - p[1], UiTextBus.Event.GetTextWidth(flavorText) + 30, 2, "the width of the tooltip is the text width and its padding")
  check(p[6] - p[2] > layout.height, "the tooltip is higher than its text")
  picture("tooltip-dead")
end

-- The end of the hover hides the tooltip.
local exit = find(root, "ButtonExitDesktop")
move_to(exit, 0.5, 0.5)
frames(0.1)
check(not shown(), "the end of the hover hides the tooltip")

-- Alive: the buttons have no tooltip.
move_to(settings, 0.5, 0.5)
frames(0.1)
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
frames(0.3)
move_to(exit, 0.5, 0.5)
frames(0.1)
move_to(settings, 0.5, 0.5)
frames(delay + 0.5)
check(not shown(), "with the character alive the button has no tooltip")
