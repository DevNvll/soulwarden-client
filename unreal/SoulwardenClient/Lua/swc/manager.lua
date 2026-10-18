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
  local start = swcui.wall_clock()
  local canvas, created = swcui.load_canvas(path)
  if not canvas then
    Swc.Note("canvas " .. tostring(path) .. " did not load: " .. tostring(created))
    return EntityId(), false
  end
  if #created > 0 then
    local loaded = swcui.wall_clock()
    if drawOrder ~= nil and drawOrder >= 0 then
      swcui.canvas_set(canvas, "drawOrder", drawOrder)
    end
    Swc.canvasNames[tostring(canvas)] = name_of(path)
    -- The canvas entity activates first (its own script components: the globals canvas has one).
    Swc.ActivateEntity(canvas)
    Swc.ActivateElements(created, swcui.canvas_get(canvas, "root"), true)
    local done = swcui.wall_clock()
    if done - start > 0.1 then
      Swc.Note(string.format("canvas %s: %d elements, load %.2f s, scripts %.2f s", tostring(path), #created,
        loaded - start, done - loaded))
    end
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
  for state in pairs(Swc.StatesOfScreen(name)) do
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
  -- The text with the values of the data sheets. Open point (O13): the expressions of the data sheets ({[...]}) stay
  -- as text.
  LocalizeWithDataSheetData = function(text) return swcui.localize(tostring(text or "")) end,
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
--   * an autohide screen (a menu) has the state of its name (the CRC-32 of the canvas name) and the states of the state
--     table that list it: it shows and gets OnTransitionIn when a state of the stack has it, and gets OnTransitionOut and
--     hides when no state of the stack has it;
--   * a shown autohide screen turns off the action maps of its canvas component (UiJavelinCanvasComponent
--     actionMapOverrides) until it hides;
--   * each other screen (the HUD) gets OnTransitionIn when the new state is HUD, else OnTransitionOut, with the new
--     state as the third argument (the HUD screens show or hide themselves with HudStateCommon).
local HUD_STATE = 2702338936

-- The screen states of the game (gamedata/uistates.json: libs/ui/uistates.xml, the table of the native screen manager;
-- tools/convert-ui.sh uistates): each state has a level, its close rules, and its screens. The machine keeps a stack of
-- states: a new state closes the states of its level, the lower levels when it has CloseLowerLevels, and the higher levels
-- when it has CloseHigherLevels; the current state is the state of the highest level (the escape menu of the respawn state,
-- level 6, opens over the respawn state, level 5, and the respawn screen stays). An autohide screen shows while a state of
-- the stack has it.
local uiStates          -- state CRC -> {name, level, closeLower, closeHigher, screens = {lower-case name -> true}}
local statesOfScreen    -- lower-case screen name -> {state CRC -> true}

local function load_states()
  if uiStates ~= nil then
    return
  end
  uiStates, statesOfScreen = {}, {}
  local data, err = swcui.load_json("gamedata/uistates.json")
  if not data then
    Swc.Note("no screen states: " .. tostring(err) .. " (tools/convert-ui.sh uistates)")
    return
  end
  for name, state in pairs(data) do
    local entry = {name = name, level = state.level or 0, closeLower = state.closeLower == true,
                   closeHigher = state.closeHigher == true, screens = {}}
    for _, screen in ipairs(state.screens or {}) do
      local key = string.lower(screen.name)
      entry.screens[key] = true
      statesOfScreen[key] = statesOfScreen[key] or {}
      statesOfScreen[key][state.crc] = true
    end
    uiStates[state.crc] = entry
  end
end

--- @return The states of the table that have a screen (a canvas name), as a set of CRCs.
function Swc.StatesOfScreen(name)
  load_states()
  return statesOfScreen[string.lower(name)] or {}
end

--- @return The state of the table for a CRC, or nil.
function Swc.UiStateInfo(state)
  load_states()
  return uiStates[state]
end

Swc.stateStack = {}

--- @return The action maps that a screen turns off while it shows (the canvas component; the scripts change them).
local function overrides_of(screen)
  if screen.overrides == nil then
    screen.overrides = {}
    for _, map in ipairs(swcui.canvas_get(screen.canvas, "actionMapOverrides") or {}) do
      screen.overrides[map] = true
    end
  end
  return screen.overrides
end

function Swc.SetScreenShown(screen, shown)
  if (screen.shown or false) == shown then
    return
  end
  screen.shown = shown
  for map in pairs(overrides_of(screen)) do
    Swc.mapOverrides[map] = math.max(0, (Swc.mapOverrides[map] or 0) + (shown and 1 or -1))
  end
end

-- UiJavCanvasComponentBus SetOverridesActionMap(canvas, map, on): a script turns an override of its canvas on or off
-- (newinventory.luac: no "movement" override, the character walks while the inventory shows).
Swc.Define("UiJavCanvasComponentBus", "Event", {
  SetOverridesActionMap = function(canvas, map, on)
    local screen = Swc.screens[tostring(canvas)]
    if not screen then
      return
    end
    local overrides = overrides_of(screen)
    on = on and true or nil
    if overrides[map] == on then
      return
    end
    overrides[map] = on
    if screen.shown then
      Swc.mapOverrides[map] = math.max(0, (Swc.mapOverrides[map] or 0) + (on and 1 or -1))
    end
  end,
})

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

local function level_of(state)
  local info = Swc.UiStateInfo(state)
  return info and info.level or 0
end

local function in_stack(stack, state)
  for _, s in ipairs(stack) do
    if s == state then
      return true
    end
  end
  return false
end

--- @return True when a state of the stack has the screen.
local function shown_by(screen, stack)
  for _, state in ipairs(stack) do
    if screen.states[state] then
      return true
    end
  end
  return false
end

--- The new stack of states: each screen that a state of the new stack has shows (OnTransitionIn), each other autohide screen
--- hides (OnTransitionOut). A shown screen whose state stays in the stack gets no transition; a shown screen whose state left
--- (a state of the same level replaced it) gets OnTransitionIn of the new state.
local function apply_stack(stack, from)
  local before = Swc.stateStack
  Swc.stateStack = stack
  local to = stack[#stack]
  Swc.uiState = to
  Swc.Note("ui state " .. tostring(from) .. " -> " .. tostring(to))
  for _, screen in pairs(Swc.screens) do
    if screen.autohide then
      local now = shown_by(screen, stack)
      if now and not screen.shown then
        swcui.canvas_set(screen.canvas, "enabled", true)
        Swc.SetScreenShown(screen, true)
        send(screen.canvas, "OnTransitionIn", from, to)
      elseif now then
        local kept = false
        for _, state in ipairs(before) do
          if screen.states[state] and in_stack(stack, state) then
            kept = true
          end
        end
        if not kept then
          send(screen.canvas, "OnTransitionIn", from, to)
        end
      elseif screen.shown then
        send(screen.canvas, "OnTransitionOut", from, to)
        swcui.canvas_set(screen.canvas, "enabled", false)
        Swc.SetScreenShown(screen, false)
      end
    elseif from ~= to then
      send(screen.canvas, to == HUD_STATE and "OnTransitionIn" or "OnTransitionOut", from, to)
    end
  end
end

--- Enters a state (SetState, QueueState): the close rules of its table entry (a state with no entry replaces the stack).
function Swc.SetUiState(state)
  local from = Swc.uiState
  if from == state then
    return
  end
  local info = Swc.UiStateInfo(state)
  local level = info and info.level or 0
  local stack = {}
  for _, s in ipairs(Swc.stateStack) do
    local l = level_of(s)
    local keep = info ~= nil and Swc.UiStateInfo(s) ~= nil and s ~= state and l ~= level
      and not (info.closeLower and l < level) and not (info.closeHigher and l > level)
    if keep then
      stack[#stack + 1] = s
    end
  end
  stack[#stack + 1] = state
  table.sort(stack, function(a, b) return level_of(a) < level_of(b) end)
  apply_stack(stack, from)
end

--- Leaves a state of the stack (ToggleState, ExitState): the state of the next lower level is current again (HUD when the
--- stack is empty).
local function leave_state(state)
  local stack = {}
  for _, s in ipairs(Swc.stateStack) do
    if s ~= state then
      stack[#stack + 1] = s
    end
  end
  if #stack == 0 then
    stack[1] = HUD_STATE
  end
  apply_stack(stack, Swc.uiState)
end

Swc.Define("LyShineManagerBus", "Broadcast", {
  SetState = function(state) Swc.SetUiState(state) end,
  QueueState = function(state) Swc.SetUiState(state) end,
  ToggleState = function(state)
    if in_stack(Swc.stateStack, state) then
      leave_state(state)
    else
      Swc.SetUiState(state)
    end
  end,
  -- ExitState(0): the current state.
  ExitState = function(state)
    if state == 0 then
      state = Swc.uiState
    end
    if in_stack(Swc.stateStack, state) then
      leave_state(state)
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
  -- The map scan of a game pad (magicmap.luac reads its speeds): the pad is not used.
  GetControllerMapScan = function() return {controllerMapScanSpeed_X = 1, controllerMapScanSpeed_Y = 1} end,
  SetMapScrollTimerBetweenSteps = function() end,
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
