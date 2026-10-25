-- The options data of the client (stage P2.3; docs/UI.md "The options screen"): the native part of the original Options
-- screen (options.luac).
--   * The data nodes Hud.LocalPlayer.Options.*: the defaults of the options data of the client program (tools/convert-ui.sh
--     options: gamedata/options.json; the constructor of SerializedOptionsData and the bindings of the nodes), then the
--     values of this client (the display: the window in pixels, Swc.HostDisplay; the language, no voice device).
--   * A node with no known default gets the neutral value of its row at the end of the UI load (a toggle off, a slider at
--     its minimum, a drop-down at its first item): the list goes to the log (open point of docs/UI.md; stage P6.9).
--   * OptionsDataBus: the reads of the screen; a reset of a category sets the values of its rows again. The screen sets the
--     nodes itself; the Set* calls of the display, the frame rate, and the quality set their nodes too (the preset sets the
--     detail rows).
--   * The save and the apply (the game menu, docs/UI.md "The game menu"): each 0.25 s the values of the shown rows that changed go
--     to the client ("options" with one line NODE<tab>VALUE for each node); the client applies the display, the render scale,
--     the frame rate, the quality, the field of view, and the invert, and keeps the values. SerializeOptions (the exit of the
--     screen) asks the client to write them ("options-save": SWC_WORK/settings/<the session data folder>/options.json). At the
--     start the client gives the saved values back (Swc.HostOptions). The keys have their own file (swc/keys.lua).
--   * UiMainMenuBus OnAuthComplete(true): the retail client sends it at the login; here at the end of the load of the UI
--     loader (the screen builds its rows then).

local Swc = Swc
local PREFIX = "Hud.LocalPlayer.Options."

local O = {nodes = {}, fields = {}, neutral = {}, authSent = false}
Swc.options = O

local data, err = swcui.load_json("gamedata/options.json")
if not data then
  Swc.Note("no options data (tools/convert-ui.sh options): " .. tostring(err))
  data = {nodes = {}, fields = {}}
end
O.nodes = data.nodes or {}
O.fields = data.fields or {}
for node, row in pairs(O.nodes) do
  Swc.SetData(PREFIX .. node, row.default)
end

local function field(name, fallback)
  local f = O.fields[name]
  if f and f.default ~= nil then
    return f.default
  end
  return fallback
end

-- The values of this client: the display of the run, the language of the game (decision D2 of the parity plan: the
-- game language), no voice device, no cross-play.
--- The resolution: the size of the client window in pixels (Swc.HostDisplay), else the viewport of the runtime (before it
--- has a size: the window size of the defaults of the program).
local function set_display()
  local w, h
  if O.display then
    w, h = O.display[1], O.display[2]
    -- The window mode of the client (0 fullscreen, 1 windowed, 2 windowed fullscreen): the drop-down has Fullscreen (0) and
    -- Windowed (1) only.
    if O.display[3] then
      Swc.SetData(PREFIX .. "Video.WindowMode", O.display[3] == 1 and 1 or 0)
    end
  else
    w, h = swcui.viewport()
  end
  if not w or w <= 0 or not h or h <= 0 then
    w, h = field("m_windowWidth", 1920), field("m_windowHeight", 1080)
  end
  w, h = math.floor(w + 0.5), math.floor(h + 0.5)
  Swc.SetData(PREFIX .. "Video.Resolution.Width", w)
  Swc.SetData(PREFIX .. "Video.Resolution.Height", h)
  -- The list of the supported resolutions (resolutionmanager.luac: a vector for each): the display modes of the client
  -- (Swc.HostResolutions), with the resolution of the run.
  local list, seen = {}, {}
  for _, r in ipairs(O.resolutions or {}) do
    local k = r[1] .. "x" .. r[2]
    if not seen[k] then
      seen[k] = true
      list[#list + 1] = r
    end
  end
  if not seen[w .. "x" .. h] then
    list[#list + 1] = {w, h}
  end
  table.sort(list, function(a, b) return a[1] * a[2] < b[1] * b[2] or (a[1] * a[2] == b[1] * b[2] and a[1] < b[1]) end)
  Swc.SetData(PREFIX .. "Video.SupportedResolutions.Count", #list)
  for i, r in ipairs(list) do
    Swc.SetData(PREFIX .. "Video.SupportedResolutions." .. (i - 1), Vector2(r[1], r[2]))
  end
end
set_display()

--- The size of the client window in pixels (the client: its viewport of the runtime is in Slate units, after the DPI scale),
--- and the window mode (0 fullscreen, 1 windowed, 2 windowed fullscreen; optional).
function Swc.HostDisplay(w, h, mode)
  O.display = {w, h, mode}
  set_display()
end

--- The display modes of the monitor of the client: lines "WIDTHxHEIGHT".
function Swc.HostResolutions(lines)
  O.resolutions = {}
  for w, h in string.gmatch(lines or "", "(%d+)x(%d+)") do
    O.resolutions[#O.resolutions + 1] = {tonumber(w), tonumber(h)}
  end
  set_display()
end

Swc.SetData(PREFIX .. "Video.SelectedDisplayIndex", 0)
Swc.SetData(PREFIX .. "Video.SupportedMonitors.Count", 1)
-- The text languages: "Use System Language" (the value of the retail reference picture, set 1 settings-game-camera.png), then
-- the languages of the game copy (its localization folders). The audio language: English (the retail picture).
local languages = {"system-language", "en-us", "de-de", "es-es", "es-mx", "fr-fr", "it-it", "pl-pl", "pt-br"}
for i, id in ipairs(languages) do
  Swc.SetData(PREFIX .. "Localization.LanguageList." .. (i - 1) .. ".Id", id)
end
Swc.SetData(PREFIX .. "Localization.LanguageAudioList.0.Id", "en-us")
Swc.SetData(PREFIX .. "Localization.LanguageOption", "system-language")
Swc.SetData(PREFIX .. "Localization.LanguageAudio", "en-us")
Swc.SetData(PREFIX .. "Localization.Language", "en-us")
Swc.SetData(PREFIX .. "CrossplayEnable.Enable", false)
-- The range of the field of view (the native code sets the nodes at run time; no value in the code): the retail reference
-- picture of set 1 (settings-video-display.png) shows the maximum 70, and the knob at 57 sits at 0.564 of the track: the
-- minimum 40.
Swc.SetData(PREFIX .. "Video.MinFov", 40)
Swc.SetData(PREFIX .. "Video.MaxFov", 70)
-- The render scale (the game menu): the row Resolution Scaling has the item of the temporal upscaler of this client (TSR; the
-- client label of swc/gamemenu.lua), and the default of this client is that upscaler in the mode Quality (2: 67 % of the
-- resolution; research/opt-gfx.md item G1). The retail default is Off (1) and Native (1).
-- V-Sync: the default of this client is off, as before the game menu (the decision of 2026-10-25: V-Sync adds input delay, the
-- mouse feel of AST-255 and AST-268, and the window of the compositor does not tear); the retail default is on.
for node, value in pairs({["Video.TemporalUpscalingType"] = 2, ["Video.TemporalUpscalingMode"] = 2,
  ["Video.VsyncEnabled"] = false}) do
  if O.nodes[node] then
    O.nodes[node].default = value
  else
    O.nodes[node] = {default = value}
  end
  Swc.SetData(PREFIX .. node, value)
end
-- The tutorial classification: the drop-down of the screen reads a node with no prefix (options.luac).
Swc.SetData("PlayerTutorials.ClassificationThreshold", field("m_playerTutorialsClassificationThreshold", 0))

-- ---------------------------------------------------------------------------------------------- the rows of the screen

local LISTS = {Gameplay = "GameplayListItemData", KBaM = "KBamListItemData", Controller = "ControllerListItemData",
  Visual = "VisualListItemData", Audio = "AudioListItemData", Social = "SocialListItemData",
  Accessibility = "AccessibilityListItemData", Twitch = "TwitchListItemData", Crossplay = "CrossplayListItemData"}

--- @return The script table of the Options screen (options.luac), or nil.
local function options_script()
  for _, list in pairs(Swc.components or {}) do
    for _, instance in ipairs(list) do
      if string.find(instance.__swcScript or "", "options/options.luac", 1, true) then
        return instance
      end
    end
  end
  return nil
end

--- @return The neutral value of a row: a toggle off, a slider at its minimum, a drop-down at its first item (the windowed
--- display mode for the window mode: the runs of this client are windowed).
local function neutral(script, row)
  if row.inputType == script.INPUT_TYPE_TOGGLE then
    return false
  elseif row.inputType == script.INPUT_TYPE_SLIDER then
    return row.minValue or 0
  elseif row.inputType == script.INPUT_TYPE_DROPDOWN then
    for _, item in ipairs(row.dropdownData or {}) do
      if row.dataNode == "Video.WindowMode" and item.text == "@ui_windowed" then
        return item.value
      end
    end
    local first = row.dropdownData and row.dropdownData[1]
    if first then
      return first.value ~= nil and first.value or first.data
    end
    return 0
  end
  return nil
end

--- @return Each row of a list of the screen, also the rows of a group in the list (a table of rows).
local function rows_of(list, out)
  out = out or {}
  for _, row in pairs(list or {}) do
    if type(row) == "table" then
      if row.dataNode then
        out[#out + 1] = row
      elseif not row.text then
        rows_of(row, out)
      end
    end
  end
  return out
end

--- The value of each row of a list again: the default of the client program, else the neutral value.
local function reset_list(script, listName, onlyMissing)
  local n = 0
  for _, row in ipairs(rows_of(script[listName])) do
    local node = row.dataNode
    if node and node ~= "Video.Resolution" and not string.find(node, "List$") and not string.find(node, "Devices$") then
      local known = O.nodes[node]
      if known and not onlyMissing then
        Swc.SetData(PREFIX .. node, known.default)
      elseif not known and (not onlyMissing or Swc.GetData(PREFIX .. node) == nil) then
        local value = neutral(script, row)
        if value ~= nil then
          Swc.SetData(PREFIX .. node, value)
          O.neutral[node] = value
          n = n + 1
        end
      end
    end
  end
  return n
end

--- The end of the load of the UI: the rows of the nodes with no known default get their neutral value, then the screens
--- get OnAuthComplete (options.luac builds its rows).
function Swc.AuthComplete()
  if O.authSent then
    return
  end
  O.authSent = true
  set_display()
  local script = options_script()
  if script then
    local n = 0
    for _, listName in pairs(LISTS) do
      n = n + reset_list(script, listName, true)
    end
    local names = {}
    for node in pairs(O.neutral) do
      names[#names + 1] = node
    end
    table.sort(names)
    local known = 0
    for _ in pairs(O.nodes) do
      known = known + 1
    end
    Swc.Note(string.format("options: %d nodes with the default of the client program, %d with the neutral value of their row: %s",
      known, n, table.concat(names, ", ")))
  end
  if Swc.frontend and Swc.frontend.menu then
    -- The front end (open point FE9 of docs/FRONTEND.md): the end of the UI load is not the account event; only the
    -- options screen gets it. The landing screen gets OnAuthComplete from the account state (Swc.HostMenuAccount).
    if script and type(script.OnAuthComplete) == "function" then
      Swc.Call(script.OnAuthComplete, script, true)
    end
  else
    Swc.Notify("UiMainMenuBus", nil, "OnAuthComplete", true)
  end
end

Swc.Connect("UILoadingNotificationBus", {
  OnUILoadingComplete = function()
    Swc.AuthComplete()
  end,
})

local function reset(category)
  return function()
    local script = options_script()
    if script and LISTS[category] then
      reset_list(script, LISTS[category], false)
      Swc.Note("options: the defaults of " .. category .. " again")
    end
  end
end

Swc.Define("OptionsDataBus", "Broadcast", {
  InitializeSerializedOptions = function() end,
  GetControllerProfileId = function() return 0 end,
  GetSmartcastType = function() return field("m_smartcastType", 0) end,
  -- The value of the row (the default of this client is TSR; the screen turns the Mode row on from it).
  GetTemporalUpscalingType = function() return Swc.GetData(PREFIX .. "Video.TemporalUpscalingType") or field("m_temporalUpscalingType", 1) end,
  GetPlayerTutorialsClassificationThreshold = function() return field("m_playerTutorialsClassificationThreshold", 0) end,
  ShouldShowInGameSurvey = function() return {showSurvey = false} end,
  SerializeOptions = function()
    O.Sync()
    Swc.hostRequests[#Swc.hostRequests + 1] = "options-save"
  end,
  ResetGameplaySettings = reset("Gameplay"),
  ResetKBaMSettings = reset("KBaM"),
  ResetControllerSettings = reset("Controller"),
  ResetVisualSettings = reset("Visual"),
  ResetAudioSettings = reset("Audio"),
  ResetSocialSettings = reset("Social"),
  ResetAccessibilitySettings = reset("Accessibility"),
  ResetTwitchSettings = reset("Twitch"),
})

-- The input profiles of the controller tab (gamedata/inputprofiles.json: javelindata_inputprofiles.datasheet). The id of a
-- profile is not in the table: here the row number from 0, and the current profile is the first one (Default). An
-- inference: the device scope of this stage is the keyboard and the mouse (decision D2 of the parity plan).
local profiles = swcui.load_json("gamedata/inputprofiles.json") or {}
Swc.Define("InputProfileDataManagerRequestBus", "Broadcast", {
  GetAllInputProfileData = function()
    local out = {}
    for i, p in ipairs(profiles) do
      out[#out + 1] = {displayName = p.displayName, displayString = p.displayString, path = p.path, id = i - 1,
                       isRebindable = p.isRebindable}
    end
    return out
  end,
})

-- The graphics features of the display device of the retail client (the screen hides their rows): only the temporal upscaler.
Swc.Define("LyShineManagerBus", "Broadcast", {
  -- The temporal upscaler of the row Resolution Scaling: this client has TSR (the item has a client label; swc/gamemenu.lua).
  GetFsrSupported = function() return true end,
  GetDlssSupported = function() return false end,
  GetDlssFrameGenerationSupported = function() return false end,
  GetReflexSupported = function() return false end,
  GetHDR = function() return false end,
  IsLockHartMode = function() return false end,
  -- The rows Resolution Scaling and Mode show when the temporal upscaling is on (their externalEnabled).
  GetTemporalUpscalingEnabled = function() return true end,
})

-- ------------------------------------------------------------------------------------------------- the save and the apply

--- The native calls of the screen that change the display, the frame rate, and the quality: they set their nodes (the screen
--- may set them too). The preset (1 low to 4 very high) sets the eight detail rows; 5 is Custom.
local DETAILS = {"EffectsQuality", "LightingQuality", "ObjectsQuality", "PostProcessingQuality", "ShadowsQuality", "TerrainQuality",
  "TexturesQuality", "WaterQuality"}
local function setter(node)
  return function(value)
    if value ~= nil then
      Swc.SetData(PREFIX .. node, value)
    end
  end
end
local natives = {
  GoFullscreen = function()
    Swc.SetData(PREFIX .. "Video.WindowMode", 0)
  end,
  GoWindowed = function()
    Swc.SetData(PREFIX .. "Video.WindowMode", 1)
  end,
  SetResolution = function(w, h)
    if w and h then
      Swc.SetData(PREFIX .. "Video.Resolution.Width", w)
      Swc.SetData(PREFIX .. "Video.Resolution.Height", h)
    end
  end,
  ChangeMonitor = function() end,
  SetMaxFps = setter("Video.MaxFps"),
  SetVsync = setter("Video.VsyncEnabled"),
  SetTemporalUpscalingType = setter("Video.TemporalUpscalingType"),
  SetTemporalUpscalingMode = setter("Video.TemporalUpscalingMode"),
  SetGraphicsQuality = function(q)
    if q == nil then
      return
    end
    Swc.SetData(PREFIX .. "Video.GraphicsQuality", q)
    if q >= 1 and q <= 4 then
      for _, d in ipairs(DETAILS) do
        Swc.SetData(PREFIX .. "Video." .. d, q)
      end
    end
  end,
}
for _, d in ipairs(DETAILS) do
  natives["Set" .. d] = setter("Video." .. d)
end
Swc.Define("OptionsDataBus", "Broadcast", natives)

--- @return The text of a value for the client: b1/b0, n<number>, s<text>; nil for another type (not saved).
local function encode(value)
  local t = type(value)
  if t == "boolean" then
    return value and "b1" or "b0"
  elseif t == "number" then
    return "n" .. string.format("%.10g", value)
  elseif t == "string" then
    return "s" .. value:gsub("[\t\n]", " ")
  end
  return nil
end

local function decode(text)
  local kind, rest = text:sub(1, 1), text:sub(2)
  if kind == "b" then
    return rest == "1"
  elseif kind == "n" then
    return tonumber(rest)
  elseif kind == "s" then
    return rest
  end
  return nil
end

O.sent = {}

--- The saved values of the client (lines NODE<tab>VALUE): the data nodes take them before the screens build their rows.
function Swc.HostOptions(lines)
  local n = 0
  for node, text in string.gmatch(lines or "", "([^\t\n]+)\t([^\n]*)") do
    local value = decode(text)
    if value ~= nil then
      Swc.SetData(PREFIX .. node, value)
      O.sent[node] = text
      n = n + 1
    end
  end
  Swc.Note(string.format("options: %d saved values of the client", n))
  return n
end

--- @return The nodes that the client keeps: the nodes of the shown rows of the screen (after the hidden rows of
--- swc/gamemenu.lua went), the resolution as its width and height.
local function saved_nodes(script)
  local out = {}
  for _, listName in pairs(LISTS) do
    for _, row in ipairs(rows_of(script[listName])) do
      local node = row.dataNode
      if node == "Video.Resolution" then
        out[#out + 1] = "Video.Resolution.Width"
        out[#out + 1] = "Video.Resolution.Height"
      elseif node and not string.find(node, "List$") and not string.find(node, "Devices$") then
        out[#out + 1] = node
      end
    end
  end
  table.sort(out)
  return out
end

--- Sends the values that changed to the client (one request "options" with a line for each).
function O.Sync()
  if not O.authSent then
    return
  end
  if not O.saved then
    local script = options_script()
    if not (script and script.isUiSpawned) then
      return
    end
    O.saved = saved_nodes(script)
  end
  local lines = {}
  for _, node in ipairs(O.saved) do
    local text = encode(Swc.GetData(PREFIX .. node))
    if text and text ~= O.sent[node] then
      O.sent[node] = text
      lines[#lines + 1] = node .. "\t" .. text
    end
  end
  if #lines > 0 then
    Swc.hostRequests[#Swc.hostRequests + 1] = "options\n" .. table.concat(lines, "\n")
  end
end

local syncWait = 0
Swc.Connect("TickBus", {
  OnTick = function(_, dt)
    syncWait = syncWait - (dt or 0)
    if syncWait <= 0 then
      syncWait = 0.25
      O.Sync()
    end
  end,
})
