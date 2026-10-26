-- The options fixture (stage P2.3; docs/UI.md "The options screen"): the original Options screen (options.luac) opened from
-- the escape menu (its button Settings), with the options data of the client program (swc/options.lua) and the key table
-- (swc/keys.lua). Clicks and keys of the host only (Swc.HostInput).
--   * The categories of this client (the game menu, swc/gamemenu.lua): Game, Keyboard & Mouse, Video, Accessibility, and the tab
--     of the keyboard and mouse bindings: a picture of each. Controller, Audio, Social, About, the tab of the controller bindings,
--     and the rows of the features that this client does not have do not show; the row Resolution Scaling shows the client
--     label of the temporal upscaler and the mode Quality.
--   * The save and the apply: the values of the shown rows go to the client ("options" lines), a change goes again, the
--     preset sets the detail rows, the exit of the screen asks for the save ("options-save"), the saved values of the client
--     come back (Swc.HostOptions).
--   * The values: the defaults of the client program (the camera shake on, the master volume 100).
--   * The key page: a click on the jump row starts the listen (the client gets "keybinding 1"), a key binds
--     the jump to a key that no action has (the UI actions use it at once; the client gets "rebind movement jump KEY" and
--     "rebind-save"); the key of the journal (J) on the jump row gives the conflict popup; the reset of the page sets the
--     defaults again ("rebind-reset").
-- Pictures: options-game, options-kbam, options-video, options-accessibility, options-keys, options-keys-listen,
-- options-keys-rebound, options-keys-conflict, options-keys-reset.
local ESC, OPTIONS = 1643432462, 3493198471
Swc.HostProgression(18, 1000)
screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 30)
screen("LyShineUI/Options/Options.uicanvas", 40)
screen("LyShineUI/Popup/Popup.uicanvas", 60)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
-- The order of the client: frames between the load of the screens and the end of the UI load (the hidden options screen
-- gets no scroll box update: its tab group keeps the place of tabbedlist.luac).
frames(0.5)
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
-- The tab row is in its mask (tabbedlist.luac puts the tab group 180 below the top of the mask; out of the mask the tabs
-- do not show and get no click).
local function y_of(point)
  return tonumber(point:match("^%S+ (%S+)$") or "")
end
local tabY = y_of(Swc.HostPointOf("text:Keyboard & Mouse Bindings"))
local maskTop, maskBottom = y_of(Swc.HostPointOf("TabbedListHeaderMask", 0.5, 0)), y_of(Swc.HostPointOf("TabbedListHeaderMask", 0.5, 1))
check(tabY and maskTop and maskBottom and tabY > maskTop and tabY < maskBottom,
  string.format("the tab row shows in its mask: y %s in %s to %s", tostring(tabY), tostring(maskTop), tostring(maskBottom)))

-- The values of the client program.
equal(Swc.GetData("Hud.LocalPlayer.Options.Controls.CameraShake"), true, "the camera shake: on (the default of the program)")
equal(Swc.GetData("Hud.LocalPlayer.Options.Audio.MasterVolume"), 100, "the master volume: 100 (the default of the program)")

equal(Swc.GetData("Hud.LocalPlayer.Options.Video.MaxFov"), 70, "the maximum field of view (the retail picture)")
-- The resolution rows: the client window in pixels (the client gives it; the viewport of the runtime is in Slate units).
Swc.HostDisplay(1280, 720)
equal(Swc.GetData("Hud.LocalPlayer.Options.Video.Resolution.Width"), 1280, "the resolution: the width of the client window")
equal(Swc.GetData("Hud.LocalPlayer.Options.Video.SupportedResolutions.0").y, 720, "the resolution list: the client window")
equal(Swc.GetData("Hud.LocalPlayer.Options.Localization.LanguageOption"), "system-language", "the text language (the retail picture)")
equal(LyShineScriptBindRequestBus.Broadcast.LocalizeText("@ui_monitor_name 1"), "Monitor 1",
  "a label with more text: the label is localized, the rest stays")

-- Each category of the settings tab, and the rows of each page (a hidden row: a feature that this client does not have).
local categories = {
  {"Game", "options-game", {"Auto-Zoom", "HUD Scale"}, {"Camera Shake", "Text Language", "Tutorials", "Sticky Lock"}},
  {"Keyboard & Mouse", "options-kbam", {"Invert Vertical Axis", "Horizontal Sensitivity"},
    {"Freeform Movement", "Turret Horizontal Sensitivity Multiplier"}},
  {"Video", "options-video", {"Window Mode", "Resolution", "Maximum FPS", "Resolution Scaling", "Temporal Super Resolution",
    "Field of View", "Video Quality"}, {"Brightness", "Sharpness", "AMD FSR 3.1"}},
  {"Accessibility", "options-accessibility", {"Subtitles", "Dialogue Auto Progress"}, {"Color Blindness", "Text-to-Speech"}},
}
for _, c in ipairs(categories) do
  if click_text(c[1]) then
    frames(0.5)
    picture(c[2])
    for _, text in ipairs(c[3]) do
      check(Swc.HostPointOf("text:" .. text) ~= "", c[1] .. ": the row shows: " .. text)
    end
    for _, text in ipairs(c[4]) do
      equal(Swc.HostPointOf("text:" .. text), "", c[1] .. ": the row of a feature that this client does not have does not show: " .. text)
    end
  end
end
for _, text in ipairs({"Controller", "Audio", "Social", "About", "Controller Bindings"}) do
  equal(Swc.HostPointOf("text:" .. text), "", "the category or tab of a feature that this client does not have does not show: " .. text)
end
equal(Swc.GetData("Hud.LocalPlayer.Options.Video.TemporalUpscalingType"), 2, "the render scale: the temporal upscaler of this client")
equal(Swc.GetData("Hud.LocalPlayer.Options.Video.TemporalUpscalingMode"), 2, "the render scale: the mode Quality (67 %)")

-- The save and the apply: the values of the shown rows go to the client.
local function option_lines()
  local out = {}
  for _, r in ipairs(Swc.hostRequests) do
    if r:find("^options\n") then
      for line in r:gmatch("[^\n]+") do
        local node, value = line:match("^(%S+)\t(.*)$")
        if node then
          out[node] = value
        end
      end
    end
  end
  return out
end
frames(0.5)
local sent = option_lines()
equal(sent["Video.TemporalUpscalingType"], "n2", "the client gets the render scale type")
equal(sent["Video.TemporalUpscalingMode"], "n2", "the client gets the render scale mode")
equal(sent["Video.Fov"], "n57", "the client gets the field of view")
equal(sent["Controls.CameraSensitivityHorizontal"], "n0.05", "the client gets the mouse sensitivity")
check(sent["Controls.InvertLook"] == "b0", "the client gets the invert (off)")
equal(sent["Video.VsyncEnabled"], "b0", "the client gets V-Sync off (the default of this client)")
equal(sent["Video.MaxFps"], "n-1", "the client gets no frame rate limit (Uncapped)")
check(sent["Controls.CameraShake"] == nil, "a hidden row is not sent")
Swc.hostRequests = {}
Swc.SetData("Hud.LocalPlayer.Options.Video.Fov", 65)
frames(0.5)
sent = option_lines()
equal(sent["Video.Fov"], "n65", "a change goes to the client")
check(sent["Video.TemporalUpscalingType"] == nil, "a value that did not change does not go again")
OptionsDataBus.Broadcast.SetGraphicsQuality(1)
equal(Swc.GetData("Hud.LocalPlayer.Options.Video.ShadowsQuality"), 1, "the preset Low sets the detail rows")
OptionsDataBus.Broadcast.SetResolution(1600, 900)
equal(Swc.GetData("Hud.LocalPlayer.Options.Video.Resolution.Width"), 1600, "SetResolution sets the resolution nodes")
Swc.HostDisplay(1280, 720, 1)
Swc.HostOptions("Video.Fov\tn62\nControls.InvertLook\tb1\n")
equal(Swc.GetData("Hud.LocalPlayer.Options.Video.Fov"), 62, "a saved value of the client comes back")
equal(Swc.GetData("Hud.LocalPlayer.Options.Controls.InvertLook"), true, "a saved toggle of the client comes back")
OptionsDataBus.Broadcast.SerializeOptions()
local saveAsked = false
for _, r in ipairs(Swc.hostRequests) do
  saveAsked = saveAsked or r == "options-save"
end
check(saveAsked, "SerializeOptions asks the client to save")
Swc.hostRequests = {}

-- The key page.
click_text("Keyboard & Mouse Bindings")
frames(0.5)
picture("options-keys")
-- The key rows of the actions that this client does not handle do not show; the rows of the keys that work show.
for _, text in ipairs({"Spectate next", "Spectate previous", "Season Rewards", "Company", "Scoreboard", "Social", "Emote Slot 1",
  "Screenshot", "Hide UI", "Target Group Member 1", "Chat"}) do
  equal(Swc.HostPointOf("text:" .. text), "", "the key page has no row of a feature that this client does not have: " .. text)
end
for _, text in ipairs({"Zoom In", "Free Look", "Weapon 1", "Consumable 1", "Move Forward", "Inventory", "Map", "Character", "Journal"}) do
  check(Swc.HostPointOf("text:" .. text) ~= "", "the key page has the row: " .. text)
end
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


equal(Swc.errorCount or 0, 0, "no Lua error")

-- The Options screen (options.luac): the depth of field 0 1 1 3 10; the state HUD closes it, and the depth of field goes off.
LyShineManagerBus.Broadcast.SetState(2702338936)
frames(1.0)
local camera = camera_log()
camera_sent(camera, "camera dof 0.000 1.000 1.000 3.000 10.000", "the Options screen")
camera_sent(camera, "camera dof -", "the Options screen closes")
camera_clean("the Options screen")
