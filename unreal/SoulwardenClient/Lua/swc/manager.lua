-- The screen system of the fork: the canvas load with its scripts (LyShineScriptBindRequestBus ScriptLoadCanvas,
-- UiCanvasManagerBus LoadCanvas), the events of the load to the UI loader (UILoadingRequestsBus), and the screen state
-- machine (LyShineManagerBus, LyShineManagerNotificationBus). The rules come from the use in the scripts
-- (lyshineui/uiloader.luac, _common/basescreeninternal.luac; research modding-native-ui.md 2.5):
--   * ScriptLoadCanvas(path, autohide, drawOrder, async, flag) loads at once and gives the canvas id; the loader
--     counts a screen at OnCanvasLoaded(canvasId) or OnCanvasLoadingFailed(path), then sets the state HUD.
--   * SetState(state) sends OnTransitionIn(from, 0, to, 0) to each screen (a handler with a canvas id).
-- Only the screens of the stage (Runtime::Options::allowedCanvases) load; each other screen is a load failure for the
-- loader (the screens of later stages), and the log names it.

local Swc = Swc
Swc.Handled("LyShineManagerNotificationBus", "UILoadingRequestsBus", "UILoadingNotificationBus",
            "LyShineScriptBindNotificationBus", "CrySystemNotificationsBus")

Swc.screens = {}         -- tostring(canvas id) -> {autohide, path}
Swc.skippedScreens = {}  -- the paths that the stage does not load

local function name_of(path)
  local name = tostring(path):gsub("\\", "/"):gsub("%.dynamicuicanvas$", ""):gsub("%.uicanvas$", "")
  return name:match("([^/]+)$") or name
end

--- Loads a canvas and activates its scripts with the LyShine order. @return The canvas id (invalid on a failure).
function Swc.LoadCanvas(path, drawOrder)
  local canvas, created = swcui.load_canvas(path)
  if not canvas then
    Swc.Note("canvas " .. tostring(path) .. " did not load: " .. tostring(created))
    return EntityId(), false
  end
  if #created > 0 then
    if drawOrder ~= nil and drawOrder >= 0 then
      swcui.canvas_set(canvas, "drawOrder", drawOrder)
    end
    Swc.canvasNames[tostring(canvas)] = name_of(path)
    -- The canvas entity activates first (its own script components: the globals canvas has one).
    Swc.ActivateEntity(canvas)
    Swc.ActivateElements(created, swcui.canvas_get(canvas, "root"), true)
  end
  return canvas, true
end

--- @return The number of screens that the stage did not load.
function Swc.SkippedScreenCount()
  local n = 0
  for _ in pairs(Swc.skippedScreens) do
    n = n + 1
  end
  return n
end

function Swc.UnloadCanvas(canvas)
  if canvas == nil or not canvas:IsValid() then
    return
  end
  local root = swcui.canvas_get(canvas, "root")
  if root then
    local order = {}
    local function walk(id)
      for _, child in ipairs(swcui.children(id)) do
        walk(child)
      end
      order[#order + 1] = id
    end
    walk(root)
    order[#order + 1] = canvas
    Swc.DeactivateEntities(order)
  end
  local screen = Swc.screens[tostring(canvas)]
  if screen then
    Swc.SetScreenShown(screen, false)
  end
  Swc.screens[tostring(canvas)] = nil
  swcui.unload_canvas(canvas)
end

--- A screen of the loader: the allowed screens load, the other ones are a failure for the loader. The loader counts
--- a failure by the CRC-32 of the last part of the path with no ending (uiloader.luac OnCanvasLoadingFailed).
function Swc.LoadScreen(path, autohide, drawOrder)
  local failed = tostring(path):gsub("%.uicanvas$", ""):gsub("%.dynamicuicanvas$", "")
  if not swcui.allowed(path) then
    if not Swc.skippedScreens[path] then
      Swc.skippedScreens[path] = true
      Swc.Note("screen " .. tostring(path) .. " is not a screen of this stage: not loaded")
    end
    Swc.Notify("UILoadingRequestsBus", nil, "OnCanvasLoadingFailed", failed)
    return EntityId()
  end
  local canvas, ok = Swc.LoadCanvas(path, drawOrder)
  if not ok then
    Swc.Notify("UILoadingRequestsBus", nil, "OnCanvasLoadingFailed", failed)
    return canvas
  end
  local name = name_of(path)
  local states = {[Math.CreateCrc32(name)] = true}
  for _, state in ipairs(Swc.extraScreenStates[string.lower(name)] or {}) do
    states[state] = true
  end
  Swc.screens[tostring(canvas)] = {autohide = autohide and true or false, path = path, name = name, canvas = canvas,
                                    state = Math.CreateCrc32(name), states = states}
  if autohide then
    -- An autohide screen is hidden until the state machine enters its state.
    swcui.canvas_set(canvas, "enabled", false)
  end
  Swc.Notify("UILoadingRequestsBus", nil, "OnCanvasLoaded", canvas)
  return canvas
end

-- ---------------------------------------------------------------------------------------------- the script bind

Swc.cvars = {
  -- A retail client has the console off (BaseScreen and the loader test it).
  sys_DeactivateConsole = 1,
}

-- The localized text of a key with its {name} parts replaced (the formula parts {[...]} stay).
local function replace(text, map)
  return (text:gsub("{([%w_%.]+)}", function(name)
    local value = map[name]
    if value ~= nil then
      return tostring(value)
    end
  end))
end

Swc.Define("LyShineScriptBindRequestBus", "Broadcast", {
  ScriptLoadCanvas = function(path, autohide, drawOrder) return Swc.LoadScreen(path, autohide, drawOrder) end,
  LocalizeText = function(text) return swcui.localize(tostring(text or "")) end,
  -- One value: it replaces the first {name} part of the text.
  LocalizeTextWithReplacement = function(text, value)
    local localized = swcui.localize(tostring(text or ""))
    return (localized:gsub("{[%w_%.]+}", (tostring(value):gsub("%%", "%%%%")), 1))
  end,
  -- Two vectors of the same length: the names and the values (common.luac TableToKeyValueVectors).
  LocalizeTextWithReplacements = function(text, keys, values)
    local map = {}
    for i = 1, #(keys or {}) do
      map[tostring(keys[i])] = values[i]
    end
    return replace(swcui.localize(tostring(text or "")), map)
  end,
  LocalizeTextWithReplacementAndPronoun = function(text, value)
    local localized = swcui.localize(tostring(text or ""))
    return (localized:gsub("{[%w_%.]+}", (tostring(value):gsub("%%", "%%%%")), 1))
  end,
  LocalizeTextWithReplacementsAndPronoun = function(text, keys, values)
    local map = {}
    for i = 1, #(keys or {}) do
      map[tostring(keys[i])] = values[i]
    end
    return replace(swcui.localize(tostring(text or "")), map)
  end,
  GetCVar = function(name) return Swc.cvars[name] end,
  SetCVar = function(name, value) Swc.cvars[name] = value end,
  GetLanguage = function() return "en-us" end,
  IsEditor = function() return false end,
  IsMaintenanceBypass = function() return false end,
  AddLevelToUnloadCanvases = function() end,
  ReleasedCanvases = function() end,
  IsFileExists = function(path) return swcui.texture_exists(tostring(path)) end,
  GetViewportSize = function() return Swc.Vector2.new({x = select(1, swcui.viewport()), y = select(2, swcui.viewport())}) end,
})

-- --------------------------------------------------------------------------------------------- the state machine

Swc.uiState = 0
Swc.mouseOwners = {}

-- The transitions of a state change (the rule of this emulation; the order of the events is open point O4):
--   * an autohide screen (a menu) has the state of its name (the CRC-32 of the canvas name) and the states of
--     Swc.extraScreenStates: it shows and gets OnTransitionIn when the machine enters one of its states, and gets
--     OnTransitionOut and hides when the machine leaves its states;
--   * a shown autohide screen turns off the action maps of its canvas component (UiJavelinCanvasComponent
--     actionMapOverrides) until it hides;
--   * each other screen (the HUD) gets OnTransitionIn when the new state is HUD, else OnTransitionOut, with the new
--     state as the third argument (the HUD screens show or hide themselves with HudStateCommon).
local HUD_STATE = 2702338936

-- The states of a screen that are not the state of its name (the native code of the client knows them; these come
-- from the scripts): the escape menu shows in "esc" (1643432462) and in the two states that escapemenu.luac tests in
-- OnTransitionIn (2907552185, the escape menu of the main menu level; 921475099, the respawn state).
Swc.extraScreenStates = {
  escapemenu = {1643432462, 2907552185, 921475099},
}

function Swc.SetScreenShown(screen, shown)
  if (screen.shown or false) == shown then
    return
  end
  screen.shown = shown
  local overrides = swcui.canvas_get(screen.canvas, "actionMapOverrides") or {}
  for _, map in ipairs(overrides) do
    Swc.mapOverrides[map] = math.max(0, (Swc.mapOverrides[map] or 0) + (shown and 1 or -1))
  end
end

--- @return True when a shown screen of a state is escapable (UiJavelinCanvasComponent).
function Swc.StateEscapable(state)
  for _, screen in pairs(Swc.screens) do
    if screen.autohide and screen.states[state] and swcui.canvas_get(screen.canvas, "escapable") then
      return true
    end
  end
  return false
end

--- @return True when the UI needs the pointer: a mouse owner (AddMouseOwner) or a shown screen whose canvas
--- component requires the mouse.
function Swc.CursorWanted()
  if next(Swc.mouseOwners) ~= nil then
    return true
  end
  for _, screen in pairs(Swc.screens) do
    if screen.autohide and screen.shown and swcui.canvas_get(screen.canvas, "requiresMouse") then
      return true
    end
  end
  return false
end

local function send(canvas, event, from, to)
  for _, h in ipairs(Swc.Handlers("LyShineManagerNotificationBus", canvas)) do
    if h.id ~= nil and h.id == canvas then
      local fn = h.table[event]
      if type(fn) == "function" then
        Swc.Call(fn, h.table, from, 0, to, 0)
      end
    end
  end
end

local function transition(from, to)
  for _, screen in pairs(Swc.screens) do
    if screen.autohide then
      if screen.states[to] then
        swcui.canvas_set(screen.canvas, "enabled", true)
        Swc.SetScreenShown(screen, true)
        send(screen.canvas, "OnTransitionIn", from, to)
      elseif screen.states[from] then
        send(screen.canvas, "OnTransitionOut", from, to)
        swcui.canvas_set(screen.canvas, "enabled", false)
        Swc.SetScreenShown(screen, false)
      end
    else
      send(screen.canvas, to == HUD_STATE and "OnTransitionIn" or "OnTransitionOut", from, to)
    end
  end
end

function Swc.SetUiState(state)
  local from = Swc.uiState
  if from == state then
    return
  end
  Swc.uiState = state
  Swc.Note("ui state " .. tostring(from) .. " -> " .. tostring(state))
  transition(from, state)
end

Swc.Define("LyShineManagerBus", "Broadcast", {
  SetState = function(state) Swc.SetUiState(state) end,
  QueueState = function(state) Swc.SetUiState(state) end,
  ToggleState = function(state)
    if Swc.uiState == state then
      Swc.SetUiState(2702338936)
    else
      Swc.SetUiState(state)
    end
  end,
  ExitState = function(state)
    if Swc.uiState == state then
      Swc.SetUiState(2702338936)   -- the state HUD (the CRC-32 of "hud")
    end
  end,
  GetCurrentState = function() return Swc.uiState end,
  IsInState = function(state) return Swc.uiState == state end,
  TransitionOutComplete = function() end,
  -- A screen that shows over the state (the popup: popup.luac ShowPopup, HidePopup): its canvas shows or hides and
  -- gets OnTransitionIn or OnTransitionOut with the current state; the state does not change.
  TryShowById = function(canvas)
    local screen = Swc.screens[tostring(canvas)]
    if screen and not screen.shown then
      swcui.canvas_set(canvas, "enabled", true)
      Swc.SetScreenShown(screen, true)
      send(canvas, "OnTransitionIn", Swc.uiState, Swc.uiState)
    end
  end,
  TryHideById = function(canvas)
    local screen = Swc.screens[tostring(canvas)]
    if screen and screen.shown then
      send(canvas, "OnTransitionOut", Swc.uiState, Swc.uiState)
      swcui.canvas_set(canvas, "enabled", false)
      Swc.SetScreenShown(screen, false)
    end
  end,
  AddMouseOwner = function(canvas) Swc.mouseOwners[tostring(canvas)] = true end,
  RemoveMouseOwner = function(canvas) Swc.mouseOwners[tostring(canvas)] = nil end,
  EnableMouse = function() end,
  DeregisterScreen = function(canvas)
    local screen = Swc.screens[tostring(canvas)]
    if screen then
      Swc.SetScreenShown(screen, false)
    end
    Swc.screens[tostring(canvas)] = nil
  end,
  DeregisterAllScreens = function() end,
  PushMemoryContext = function() end,
  PopMemoryContext = function() end,
  IsMemoryTracking = function() return false end,
  ProfileBegin = function() end,
  ProfileEnd = function() end,
  CreateSpriteCachePool = function() return 1 end,
  GetResolutionRatio = function()
    local w, h = swcui.viewport()
    return w / h
  end,
  OnMapChange = function() end,
  SetServerMOTD = function() end,
  SetWorldName = function(name) Swc.worldName = tostring(name or "") end,
  GetWorldName = function() return Swc.worldName or "" end,
  UpdateVirtualCursorSettings = function() end,
  IsKeybindBound = function(action) return Swc.Keybind(action) ~= nil end,
  GetKeybind = function(action, actionMap) return Swc.Keybind(action, actionMap) or "" end,
  -- The input name of the key of an action (a CryInput name: "q", "mouse1"); the hint shows an icon for a mouse button.
  GetActionInputName = function(action, actionMap)
    local key = Swc.Keybind(action, actionMap)
    return key and string.lower(key) or ""
  end,
  GetActionInputNameByDevice = function(action, actionMap)
    local key = Swc.Keybind(action, actionMap)
    return key and string.lower(key) or ""
  end,
  GetKeybindWithoutHold = function(action, actionMap) return Swc.Keybind(action, actionMap) or "" end,
  GetLastActionInputDevice = function() return eAID_KeyboardMouse end,
  IsActionHoldWithTriggerDelay = function() return false end,
})
