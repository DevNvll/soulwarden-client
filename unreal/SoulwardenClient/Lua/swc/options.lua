-- The options data of the client (stage P2.3; docs/UI.md "The options screen"): the native part of the original Options
-- screen (options.luac).
--   * The data nodes Hud.LocalPlayer.Options.*: the defaults of the options data of the client program (tools/convert-ui.sh
--     options: gamedata/options.json; the constructor of SerializedOptionsData and the bindings of the nodes), then the
--     values of this client (the display: the window in pixels, Swc.HostDisplay; the language, no voice device).
--   * A node with no known default gets the neutral value of its row at the end of the UI load (a toggle off, a slider at
--     its minimum, a drop-down at its first item): the list goes to the log (open point of docs/UI.md; stage P6.9).
--   * OptionsDataBus: the reads of the screen; a reset of a category sets the values of its rows again. The other calls
--     (Set*, SerializeOptions) are stubs in this stage: the screen sets the nodes itself; the options are not saved and
--     do not change the client (stage P6.9). The keys are the exception (swc/keys.lua: they are saved and used).
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
  else
    w, h = swcui.viewport()
  end
  if not w or w <= 0 or not h or h <= 0 then
    w, h = field("m_windowWidth", 1920), field("m_windowHeight", 1080)
  end
  w, h = math.floor(w + 0.5), math.floor(h + 0.5)
  Swc.SetData(PREFIX .. "Video.Resolution.Width", w)
  Swc.SetData(PREFIX .. "Video.Resolution.Height", h)
  -- The list of the supported resolutions (resolutionmanager.luac: a vector for each): the resolution of the run.
  Swc.SetData(PREFIX .. "Video.SupportedResolutions.Count", 1)
  Swc.SetData(PREFIX .. "Video.SupportedResolutions.0", Vector2(w, h))
end
set_display()

--- The size of the client window in pixels (the client: its viewport of the runtime is in Slate units, after the DPI scale).
function Swc.HostDisplay(w, h)
  O.display = {w, h}
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
  Swc.Notify("UiMainMenuBus", nil, "OnAuthComplete", true)
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
  GetTemporalUpscalingType = function() return field("m_temporalUpscalingType", 0) end,
  GetPlayerTutorialsClassificationThreshold = function() return field("m_playerTutorialsClassificationThreshold", 0) end,
  ShouldShowInGameSurvey = function() return {showSurvey = false} end,
  SerializeOptions = function() end,
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

-- The graphics features of the display device of the retail client (the screen hides their rows): none here.
Swc.Define("LyShineManagerBus", "Broadcast", {
  GetFsrSupported = function() return false end,
  GetDlssSupported = function() return false end,
  GetDlssFrameGenerationSupported = function() return false end,
  GetReflexSupported = function() return false end,
  GetHDR = function() return false end,
  IsLockHartMode = function() return false end,
  GetTemporalUpscalingEnabled = function() return false end,
})
