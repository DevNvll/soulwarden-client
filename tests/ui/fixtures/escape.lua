-- The escape fixture: the original EscapeMenu and Popup screens with their scripts, driven by keys and clicks through
-- the input of the host (Swc.HostInput, as the client). Escape in the HUD fires the game action toggleMenuComponent;
-- with no handler the state goes to "esc" and the escape menu shows: the cursor shows, its canvas turns off the maps
-- player, movement, and camera (a game key does not go to the game). Exit to desktop opens the quit popup over the
-- menu (TryShowById: the state stays); Escape fires ui_genpop_cancel and the popup closes; the menu has the pointer
-- again (the hover of its buttons). Escape then gives OnEscapeKeyPressed to the menu: the state goes back to HUD.
-- Pictures: escape-menu, escape-popup, escape-focus.
local HUD, ESC = 2702338936, 1643432462
local menu = screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 41)
local popup = screen("LyShineUI/Popup/Popup.uicanvas", 52)
local function state()
  return LyShineManagerBus.Broadcast.GetCurrentState()
end
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.3)
check(not Swc.HostCursor(), "the HUD has no cursor")
check(Swc.HostKeyToGame("w"), "the HUD gives the move keys to the game")

-- Open: Escape in the HUD.
key("escape")
frames(0.6)
equal(state(), ESC, "Escape in the HUD: the state of the escape menu")
check(swcui.canvas_get(menu, "enabled"), "the escape menu shows")
check(Swc.HostCursor(), "the escape menu has the cursor (requiresMouse)")
check(not Swc.ActionMapOn("movement") and not Swc.ActionMapOn("camera") and not Swc.ActionMapOn("player"),
  "the escape menu turns off the maps movement, camera, player")
check(not Swc.HostKeyToGame("w") and not Swc.HostKeyToGame("maxis_x"), "a move key and the mouse look do not go to the game")
check(#DynamicBus.EscapeKeyNotificationBus.handlers > 0, "the escape menu has its escape handler (after its tick)")
picture("escape-menu")

-- A nested popup: exit to desktop.
local root = swcui.canvas_get(menu, "root")
local exit = find(root, "ButtonExitDesktop")
check(exit ~= nil, "the menu has its exit button")
if exit then
  click(exit)
  frames(0.6)
end
check(swcui.canvas_get(popup, "enabled"), "exit to desktop opens the quit popup")
equal(state(), ESC, "the popup does not change the state")
check(not Swc.ActionMapOn("ui"), "the popup turns off the map ui (popup.luac)")
picture("escape-popup")

-- Escape closes the popup (ui_genpop_cancel), not the menu.
key("escape")
frames(0.6)
check(not swcui.canvas_get(popup, "enabled"), "Escape closes the popup")
check(swcui.canvas_get(menu, "enabled"), "the menu stays")
equal(state(), ESC, "the state stays the escape menu")
check(Swc.ActionMapOn("ui"), "the map ui is on again")

-- The return of the focus: the pointer over a menu button hovers it.
local settings = find(root, "ButtonSettings")
if settings then
  move_to(settings)
  frames(0.4)
  local hovered = Swc.input.hover
  check(hovered ~= nil and (hovered == settings or swcui.parent(hovered) == settings or find(settings, swcui.name(hovered)) ~= nil),
    "the pointer over a menu button hovers it after the popup (" .. tostring(hovered and swcui.name(hovered)) .. ")")
end
picture("escape-focus")

-- Escape closes the menu: the HUD again.
key("escape")
frames(0.6)
equal(state(), HUD, "Escape in the menu: the HUD")
check(not swcui.canvas_get(menu, "enabled"), "the escape menu hides")
check(not Swc.HostCursor(), "no cursor in the HUD")
check(Swc.ActionMapOn("movement") and Swc.ActionMapOn("camera") and Swc.ActionMapOn("player"), "the maps are on again")
note("actions: " .. table.concat(Swc.actionLog, ", "))

-- The return to the character selection (stage F6 of docs/FRONTEND.md): the exit to the main menu opens the quit popup of the
-- popup screen (its module quitgamepopup.luac), and Yes gives RequestDisconnect(eExitGameDestination_MainMenu): the host request
-- "menu quit 1". No request before Yes.
local function quit_requests()
  local out = {}
  for _, r in ipairs(Swc.hostRequests) do
    if r:find("^menu quit") then out[#out + 1] = r end
  end
  return out
end
key("escape")
frames(0.6)
equal(state(), ESC, "Escape in the HUD again: the escape menu")
local main = find(swcui.canvas_get(menu, "root"), "ButtonExitMainMenu")   -- the script field is ButtonExitMainNemu
check(main ~= nil, "the menu has its exit to the main menu button")
if main then
  click(main)
  frames(0.6)
end
check(swcui.canvas_get(popup, "enabled"), "the exit to the main menu opens the quit popup")
equal(#quit_requests(), 0, "no quit request before Yes")
picture("escape-quit-main")
local yes = find(swcui.canvas_get(popup, "root"), "ButtonYes")
check(yes ~= nil, "the quit popup has its Yes button")
if yes then
  click(yes)
  frames(0.6)
end
local quits = quit_requests()
equal(#quits, 1, "Yes: one quit request")
equal(quits[1], "menu quit " .. tostring(eExitGameDestination_MainMenu), "the quit request goes to the main menu (destination 1)")
