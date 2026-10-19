-- The keys of the actions (stage P2.3; docs/UI.md "The keys"): the native key part of the client (CryAction of the fork)
-- for the scripts. A key is a CryInput name ("w", "lshift", "mouse1", "mwheel_up"): the first keyboard or mouse input of an
-- action in the action maps of the profile (gamedata/actionmaps.json; swc/actions.lua), with the rebinds of the options
-- screen on top.
--   * The queries (LyShineManagerBus): GetKeybind, GetKeybindByDevice, GetActionInputName(ByDevice) give the CryInput name
--     (the scripts make the text: "@cc_" texts, else the name); GetKeybindLocStringByDevice and GetKeybindWithoutHold give
--     the text (keybindinglistitem.luac compares the second one with the "@cc_" text of a key); GetActionInMapByKey gives
--     the actions of a map with a key (a list from index 1).
--   * The rebinds (GameRequestsBus): RebindAction(action, map, key, device) changes the key of the action in its map (the
--     UI actions use it at once), sets Hud.LocalPlayer.Keybind.<action>.<map> (the hints watch it), and asks the client
--     (host request "rebind MAP ACTION KEY": the key table of the client, UI/SwcKeyBindings); SaveRebindData asks the
--     client to save ("rebind-save"); ResetActionMaps sets the defaults again ("rebind-reset"; Hud.LocalPlayer.Keybind.Reset).
--   * Each key of the host goes to KeyInputNotificationBus (OnKeyPressed, OnKeyReleased): the key page listens to it while
--     it binds. SetIsKeybinding asks the client to send the mouse buttons and the wheel as keys ("keybinding 1").
-- Only the keyboard and the mouse (decision D2 of the parity plan); a pad device gets no key.

local Swc = Swc
Swc.Handled("KeyInputNotificationBus")

local K = {defaults = {}, overrides = {}, binding = false}
Swc.keyTable = K

local function slot(mapName, action)
  return mapName .. "/" .. action
end

--- The first input of an action that has a key (the keyboard and mouse input of the profile).
local function first_input(a)
  for _, input in ipairs(a.inputs or {}) do
    if input.key and input.key ~= "" then
      return input
    end
  end
  return nil
end

-- The defaults: the key of each action of each map at the load (the profile of the data).
for _, mapName in ipairs(Swc.actionMapOrder) do
  for _, a in ipairs(Swc.actionMaps[mapName].actions) do
    local input = first_input(a)
    if input and K.defaults[slot(mapName, a.name)] == nil then
      K.defaults[slot(mapName, a.name)] = input.key
    end
  end
end

local function action_of(mapName, action)
  local map = Swc.actionMaps[mapName]
  for _, a in ipairs(map and map.actions or {}) do
    if a.name == action then
      return a, map
    end
  end
  return nil
end

--- Sets the key of an action in its map (the UI actions and the keys that the map keeps from the game).
local function set_key(mapName, action, key)
  local a, map = action_of(mapName, action)
  local input = a and first_input(a)
  if not input then
    return false
  end
  input.key = key
  map.keys = nil
  Swc.SetData("Hud.LocalPlayer.Keybind." .. action .. "." .. mapName, key)
  return true
end

--- @return The CryInput name of the key of an action (a map first, else each map of the profile), or nil.
function Swc.Keybind(action, actionMap)
  if action == nil then
    return nil
  end
  local maps = actionMap and actionMap ~= "" and {actionMap} or Swc.actionMapOrder
  for _, mapName in ipairs(maps) do
    local a = action_of(mapName, action)
    local input = a and first_input(a)
    if input and input.key ~= " " then
      return input.key
    end
  end
  return nil
end

--- @return The text of a key: its "@cc_" text, else the name (the rule of keybindinglistitem.luac LocalizeKeyName).
function Swc.KeyText(key)
  if key == nil or key == "" then
    return ""
  end
  local text = LyShineScriptBindRequestBus.Broadcast.LocalizeText("@cc_" .. key)
  if text ~= nil and text ~= "@cc_" .. key then
    return text
  end
  return key
end

local function keyboard(device)
  return device == nil or device == eAID_KeyboardMouse
end

--- A rebind (the options screen or the client at the start). host: true to ask the client to keep it.
function Swc.Rebind(mapName, action, key, host)
  if not set_key(mapName, action, key) then
    Swc.Note("rebind: no action " .. tostring(action) .. " in the map " .. tostring(mapName))
    return false
  end
  if key == K.defaults[slot(mapName, action)] then
    K.overrides[slot(mapName, action)] = nil
  else
    K.overrides[slot(mapName, action)] = key
  end
  if host then
    Swc.hostRequests[#Swc.hostRequests + 1] = string.format("rebind %s %s %s", mapName, action, key == " " and "-" or key)
  end
  return true
end

--- The defaults again (each map); host: true to ask the client to forget its rebinds.
function Swc.ResetKeys(host)
  for s, key in pairs(K.defaults) do
    local mapName, action = s:match("^([^/]+)/(.+)$")
    set_key(mapName, action, key)
  end
  K.overrides = {}
  Swc.SetData("Hud.LocalPlayer.Keybind.Reset", true)
  if host then
    Swc.hostRequests[#Swc.hostRequests + 1] = "rebind-reset"
  end
end

--- The rebinds that the client kept (its save file): lines "MAP ACTION KEY" ("-" for no key).
function Swc.HostKeyBindings(lines)
  local n = 0
  for line in string.gmatch(lines or "", "[^\n]+") do
    local mapName, action, key = line:match("^(%S+) (%S+) (%S+)$")
    if mapName and Swc.Rebind(mapName, action, key == "-" and " " or key, false) then
      n = n + 1
    end
  end
  Swc.Note(string.format("keys: %d rebind(s) of the client", n))
  return n
end

Swc.Define("LyShineManagerBus", "Broadcast", {
  IsKeybindBound = function(action) return Swc.Keybind(action) ~= nil end,
  GetKeybind = function(action, actionMap) return Swc.Keybind(action, actionMap) or "" end,
  GetKeybindByDevice = function(action, actionMap, device)
    return keyboard(device) and Swc.Keybind(action, actionMap) or ""
  end,
  GetKeybindLocStringByDevice = function(action, actionMap, device)
    return keyboard(device) and Swc.KeyText(Swc.Keybind(action, actionMap)) or ""
  end,
  GetActionInputName = function(action, actionMap) return Swc.Keybind(action, actionMap) or "" end,
  GetActionInputNameByDevice = function(action, actionMap, device)
    return keyboard(device) and Swc.Keybind(action, actionMap) or ""
  end,
  GetKeybindWithoutHold = function(action, actionMap) return Swc.KeyText(Swc.Keybind(action, actionMap)) end,
  GetActionInMapByKey = function(key, mapName)
    local out = {}
    local map = Swc.actionMaps[mapName]
    for _, a in ipairs(map and map.actions or {}) do
      local input = first_input(a)
      if input and input.key == key then
        out[#out + 1] = a.name
      end
    end
    return out
  end,
  -- The shift modifier of an action (the modifier map of the profile): none.
  GetShiftModifierNameCrcByAction = function() return 0 end,
  SetIsKeybinding = function(on)
    K.binding = on and true or false
    Swc.hostRequests[#Swc.hostRequests + 1] = "keybinding " .. (K.binding and "1" or "0")
  end,
  IsKeybinding = function() return K.binding end,
  -- No game pad (the device scope of this stage): gamepadcustomdata.luac then shows the default pad picture.
  GetCurrentGamepadIdentifier = function() return eGPINone end,
})

Swc.Define("GameRequestsBus", "Broadcast", {
  RebindAction = function(action, actionMap, key, device)
    if keyboard(device) then
      Swc.Rebind(actionMap, action, key, true)
    end
  end,
  SaveRebindData = function()
    Swc.hostRequests[#Swc.hostRequests + 1] = "rebind-save"
  end,
  ResetActionMaps = function(device)
    if keyboard(device) then
      Swc.ResetKeys(true)
    end
  end,
})

-- The last input device of the client (the scripts read the global and the bus): the keyboard and the mouse.
g_lastInputDevice = eAID_KeyboardMouse
