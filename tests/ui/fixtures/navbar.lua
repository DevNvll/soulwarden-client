-- The NavBar fixture (the game menu, swc/gamemenu.lua; docs/UI.md "The game menu"): the original NavBar screen (navbar.luac) in
-- display-only mode with the escape menu. Escape in the HUD opens the escape menu as before (the emulation keeps ui_cancel,
-- toggleMenuComponent, and toggleJournalComponent); the NavBar shows over it with only JOURNAL, CHARACTER, MAP, and the Game
-- Menu gear (no Company, Mount, Modes, Activities, season, Store, social button); a click on each tab sets its state at once
-- (no soft load): JOURNAL -> Objectives, CHARACTER -> Skills, MAP -> Map, the gear -> Esc; the J key goes to the journal; Escape
-- opens the escape menu and closes it again. Pictures: navbar-esc, navbar-skills.
local HUD, ESC, SKILLS, JOURNAL, MAP, NAVBAR = 2702338936, 1643432462, 3576764016, 1823500652, 2477632187, 3766762380
Swc.HostProgression(18, 1000)
local menu = screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 41)
local nav = screen("LyShineUI/NavBar/NavBar.uicanvas", 45)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.5)
local function state()
  return LyShineManagerBus.Broadcast.GetCurrentState()
end
check(not swcui.canvas_get(nav, "enabled"), "the HUD has no NavBar")

-- Escape in the HUD: the escape menu, as before, with the NavBar over it.
key("escape")
frames(1.0)
equal(state(), ESC, "Escape in the HUD opens the escape menu (the NavBar does not take the action)")
check(swcui.canvas_get(menu, "enabled"), "the escape menu shows")
check(swcui.canvas_get(nav, "enabled"), "the NavBar shows in the state of the escape menu")
picture("navbar-esc")

-- The tabs of this client.
for _, text in ipairs({"Journal", "CHARACTER", "Map"}) do
  check(Swc.HostPointOf("text:" .. text) ~= "", "the NavBar has the tab " .. text)
end
for _, text in ipairs({"Company", "Mount", "Modes", "Activities", "Store", "COMPANY", "STORE"}) do
  equal(Swc.HostPointOf("text:" .. text), "", "the NavBar has no tab " .. text)
end
local navRoot = swcui.canvas_get(nav, "root")
local function nav_script()
  for _, list in pairs(Swc.components or {}) do
    for _, instance in ipairs(list) do
      if string.find(instance.__swcScript or "", "navbar/navbar.luac", 1, true) then
        return instance
      end
    end
  end
end
local script = nav_script()
check(script ~= nil, "the NavBar script runs")
if script then
  local states = {}
  for _, entry in ipairs(script.NavButtonData or {}) do
    if not entry.isSpacer then
      states[#states + 1] = entry.stateName
    end
  end
  equal(table.concat(states, " "), table.concat({JOURNAL, SKILLS, MAP, ESC}, " "), "the entries: JOURNAL, CHARACTER, MAP, the Game Menu")
  equal(script.disabledStates and script.disabledStates[SKILLS], false, "the CHARACTER tab is on (the attributes and trade skills flags)")
  equal(next(script.statesToSoftLoad or {}), nil, "no soft load (the screens of this client load at the start)")
end

--- Clicks a shown text of the NavBar.
local function click_text(text)
  local point = Swc.HostPointOf("text:" .. text)
  if not check(point ~= "", "the text shows: " .. text) then
    return
  end
  local x, y = point:match("^(%S+) (%S+)$")
  Swc.HostInput("move", tonumber(x), tonumber(y))
  frames(0.05)
  Swc.HostInput("button", "left", true)
  frames(0.05)
  Swc.HostInput("button", "left", false)
  frames(0.6)
end

click_text("CHARACTER")
equal(state(), SKILLS, "CHARACTER opens the character window (the state Skills)")
check(swcui.canvas_get(nav, "enabled"), "the NavBar stays in the state Skills")
picture("navbar-skills")
click_text("Journal")
equal(state(), JOURNAL, "JOURNAL opens the journal (the state Objectives)")
click_text("Map")
equal(state(), MAP, "MAP opens the map")
check(state() ~= NAVBAR, "no soft load state")
-- The Game Menu gear: the shown icon of the settings sprite (the gear has no text).
local function find_sprite(root, part)
  if swcui.has(root, "image") and string.find(swcui.get(root, "image.sprite") or "", part, 1, true)
    and swcui.enabled_in_hierarchy(root) then
    return root
  end
  for _, child in ipairs(swcui.children(root) or {}) do
    local found = find_sprite(child, part)
    if found then
      return found
    end
  end
end
local gear = find_sprite(navRoot, "navbar_icon_settings_2023")
check(gear ~= nil, "the NavBar has the Game Menu gear")
if gear then
  move_to(gear)
  frames(0.05)
  Swc.HostInput("button", "left", true)
  frames(0.05)
  Swc.HostInput("button", "left", false)
  frames(0.6)
end
equal(state(), ESC, "the Game Menu gear opens the escape menu")

-- The keys of the emulation: J opens the journal, Escape goes back.
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.6)
key("j")
frames(0.6)
equal(state(), JOURNAL, "J opens the journal (the journal rule of the emulation)")
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.6)
key("escape")
frames(0.6)
equal(state(), ESC, "Escape opens the escape menu again")
key("escape")
frames(0.6)
equal(state(), HUD, "Escape in the escape menu goes back to the HUD (the escape rule of the emulation)")
check(not swcui.canvas_get(nav, "enabled"), "the NavBar hides in the HUD")
equal(Swc.errorCount or 0, 0, "no Lua error")
