-- The options fixture (stage P2.3; docs/UI.md "The options screen"): the original Options screen (options.luac) opened from
-- the escape menu (its button Settings), with the options data of the client program (swc/options.lua) and the key table
-- (swc/keys.lua). Clicks and keys of the host only (Swc.HostInput).
--   * The categories: Game, Keyboard & Mouse, Controller, Video, Audio, Social, Accessibility, About (the flags of the build
--     show all eight), the tab of the keyboard and mouse bindings, the tab of the controller bindings: a picture of each.
--   * The values: the defaults of the client program (the camera shake on, the master volume 100).
--   * The key page: a click on the jump row starts the listen (the client gets "keybinding 1"), a key binds
--     the jump to a key that no action has (the UI actions use it at once; the client gets "rebind movement jump KEY" and
--     "rebind-save"); the key of the journal (J) on the jump row gives the conflict popup; the reset of the page sets the
--     defaults again ("rebind-reset").
-- Pictures: options-game, options-kbam, options-controller, options-video, options-audio, options-social,
-- options-accessibility, options-about, options-keys, options-keys-listen, options-keys-rebound, options-keys-conflict,
-- options-keys-reset, options-controller-bindings.
local ESC, OPTIONS = 1643432462, 3493198471
Swc.HostProgression(18, 1000)
screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 30)
screen("LyShineUI/Options/Options.uicanvas", 40)
screen("LyShineUI/Popup/Popup.uicanvas", 60)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.AuthComplete()
frames(0.5)

--- Clicks the shown element with a text (Swc.HostPointOf "text:..."). @return True when the text shows.
local function click_text(text)
  local point = Swc.HostPointOf("text:" .. text)
  if not check(point ~= "", "the text shows: " .. text) then
    return false
  end
  local x, y = point:match("^(%S+) (%S+)$")
  Swc.HostInput("move", tonumber(x), tonumber(y))
  frames(0.05)
  Swc.HostInput("button", "left", true)
  frames(0.05)
  Swc.HostInput("button", "left", false)
  frames(0.3)
  return true
end

local function requests()
  local out = {}
  while true do
    local r = Swc.TakeHostRequest()
    if r == "" then
      return out
    end
    out[r] = true
  end
end

-- The escape menu and its button Settings.
key("escape")
frames(0.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), ESC, "Escape opens the escape menu")
click_text("Settings")
frames(0.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), OPTIONS, "the button Settings opens the options screen")

-- The values of the client program.
equal(Swc.GetData("Hud.LocalPlayer.Options.Controls.CameraShake"), true, "the camera shake: on (the default of the program)")
equal(Swc.GetData("Hud.LocalPlayer.Options.Audio.MasterVolume"), 100, "the master volume: 100 (the default of the program)")

equal(LyShineScriptBindRequestBus.Broadcast.LocalizeText("@ui_monitor_name 1"), "Monitor 1",
  "a label with more text: the label is localized, the rest stays")

-- Each category of the settings tab.
local categories = {{"Game", "options-game"}, {"Keyboard & Mouse", "options-kbam"}, {"Controller", "options-controller"},
  {"Video", "options-video"}, {"Audio", "options-audio"}, {"Social", "options-social"},
  {"Accessibility", "options-accessibility"}, {"About", "options-about"}}
for _, c in ipairs(categories) do
  if click_text(c[1]) then
    frames(0.5)
    picture(c[2])
  end
end

-- The key page.
click_text("Keyboard & Mouse Bindings")
frames(0.5)
picture("options-keys")
equal(Swc.Keybind("jump", "movement"), "space", "the jump key of the profile")
requests()
check(click_text("Jump / Traverse"), "the jump row")
local r = requests()
check(r["keybinding 1"] == true, "the click starts the listen (the client sends the mouse as keys)")
picture("options-keys-listen")
-- A key that no action of the maps of the key page has (keybindinglistitem.luac FindConflictingAction).
local free
local candidates = {}
for c in string.gmatch("abcdefghijklmnopqrstuvwxyz", ".") do
  candidates[#candidates + 1] = c
end
for _, k in ipairs({"np_1", "np_2", "np_3", "np_4", "np_5", "np_6", "np_7", "np_8", "np_9", "insert", "home", "end", "pgup",
  "pgdn", "semicolon", "apostrophe", "comma", "period", "slash", "backslash", "lbracket", "rbracket", "f11", "f12"}) do
  candidates[#candidates + 1] = k
end
for _, k in ipairs(candidates) do
  local used = false
  for _, map in ipairs({"combat", "camera", "dialogue", "player", "movement", "ui", "shift_modifier_map_key", "ui_radial", "default"}) do
    used = used or #LyShineManagerBus.Broadcast.GetActionInMapByKey(k, map) > 0
  end
  if not used then
    free = k
    break
  end
end
check(free ~= nil, "a key that no action has: " .. tostring(free))
free = free or "h"
key(free)
frames(0.5)
equal(Swc.Keybind("jump", "movement"), free, "the free key binds the jump")
r = requests()
check(r["rebind movement jump " .. free] == true, "the client gets the rebind")
check(r["rebind-save"] == true, "the client saves the rebinds")
check(Swc.GetData("Hud.LocalPlayer.Keybind.jump.movement") == free, "the hints get the new key")
picture("options-keys-rebound")
-- The key of the journal (J) on the jump row: the conflict popup of the screen.
check(click_text("Jump / Traverse"), "the jump row again")
requests()
key("j")
frames(0.5)
picture("options-keys-conflict")
check(Swc.HostPointOf("text:Yes") ~= "" or Swc.HostPointOf("text:YES") ~= "", "the conflict popup asks")
equal(Swc.Keybind("jump", "movement"), free, "no rebind before the answer")
if not click_text("No") then
  key("escape")
end
frames(0.5)
-- The reset of the page.
requests()
check(click_text("Restore Defaults"), "the button of the reset")
frames(0.5)
if Swc.HostPointOf("text:Yes") ~= "" then
  click_text("Yes")
elseif Swc.HostPointOf("text:YES") ~= "" then
  click_text("YES")
end
frames(0.5)
equal(Swc.Keybind("jump", "movement"), "space", "the reset sets the jump key of the profile again")
r = requests()
check(r["rebind-reset"] == true, "the client forgets the rebinds")
picture("options-keys-reset")

click_text("Controller Bindings")
frames(0.5)
picture("options-controller-bindings")
