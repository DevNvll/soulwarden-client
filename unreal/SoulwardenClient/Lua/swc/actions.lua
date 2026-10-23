-- The game actions of the client (CryAction of the Javelin fork; research feel-values.md R6): the action maps of the
-- client profile (tools/convert-ui.sh actionmaps: gamedata/actionmaps.json), their enable state, the input filters of
-- the scripts (UIInputRequestsBus), and the events of the actions (CryActionNotificationsBus OnCryAction(name, value):
-- a value of 1 at a press, 0 at a release). A key of the host fires each action of an enabled map that has this key and
-- that no enabled input filter holds; the maps and the filters are read before the first action of the key (a handler
-- that enables a map does not give the same key to it).
--
-- Rules of the emulation (facts of the scripts, not of the native code):
--   * The maps of the profile start with their state (enable="0": off); the development maps (debug, flycam, the
--     spectator cameras) start off.
--   * A shown canvas with action map overrides (UiJavelinCanvasComponent actionMapOverrides) turns those maps off
--     while it shows (swc/manager.lua).
--   * toggleMenuComponent (Escape) that no handler used: the handlers of DynamicBus.EscapeKeyNotificationBus get
--     OnEscapeKeyPressed (the escape menu and 40 other screens connect it while they show); with no handler, a state
--     other than HUD with an escapable screen goes back to HUD, and the HUD goes to the state of the escape menu
--     ("esc"; escapemenu.luac).

local Swc = Swc
Swc.Handled("CryActionNotificationsBus")

local HUD_STATE = 2702338936
local ESC_STATE = 1643432462   -- CRC-32 of "esc": the state of the escape menu (escapemenu.luac, navbar.luac)
local DEVELOPMENT_MAPS = {debug = true, debug_camera = true, debug_gamepad_dof_controls = true, flycam = true,
  spectatorcam = true, spectatorcamera_pvp = true}

Swc.actionMaps = {}       -- name -> {name, profileEnabled, enabled, actions}
Swc.actionMapOrder = {}   -- the map names in the order of the profile
Swc.mapOverrides = {}     -- map name -> the number of shown canvases that turn it off
Swc.inputFilters = {}     -- filter name -> {actions = {name = true}, enabled = false}

local function load_maps()
  local data, err = swcui.load_json("gamedata/actionmaps.json")
  if not data then
    Swc.Note("no action maps (tools/convert-ui.sh actionmaps): " .. tostring(err))
    return
  end
  for _, m in ipairs(data.maps or {}) do
    local map = Swc.actionMaps[m.name]
    if not map then
      map = {name = m.name, profileEnabled = m.enabled and not DEVELOPMENT_MAPS[m.name], actions = {}}
      map.enabled = map.profileEnabled
      Swc.actionMaps[m.name] = map
      Swc.actionMapOrder[#Swc.actionMapOrder + 1] = m.name
    end
    for _, a in ipairs(m.actions or {}) do
      map.actions[#map.actions + 1] = a
    end
  end
end
load_maps()

--- @return True when a map is on: its own state and no shown canvas turns it off.
function Swc.ActionMapOn(name)
  local map = Swc.actionMaps[name]
  return map ~= nil and map.enabled and (Swc.mapOverrides[name] or 0) == 0
end

local function filtered(actionName)
  for _, filter in pairs(Swc.inputFilters) do
    if filter.enabled and filter.actions[actionName] then
      return true
    end
  end
  return false
end

local function filter_of(name)
  local filter = Swc.inputFilters[name]
  if not filter then
    filter = {actions = {}, enabled = false}
    Swc.inputFilters[name] = filter
  end
  return filter
end

Swc.Define("UIInputRequestsBus", "Broadcast", {
  SetActionMapEnabled = function(name, on)
    local map = Swc.actionMaps[name]
    if map then
      map.enabled = on and true or false
    end
  end,
  IsActionMapEnabled = function(name) return Swc.ActionMapOn(name) end,
  ResetActionMapsToDefaultEnableState = function()
    for _, map in pairs(Swc.actionMaps) do
      map.enabled = map.profileEnabled
    end
  end,
  CreateInputFilter = function(name) filter_of(name) end,
  AddActionToInputFilter = function(name, action) filter_of(name).actions[action] = true end,
  AddActionMapActionsToInputFilter = function(name, mapName)
    local filter = filter_of(name)
    local map = Swc.actionMaps[mapName]
    for _, a in ipairs(map and map.actions or {}) do
      filter.actions[a.name] = true
    end
  end,
  EnableInputFilter = function(name, on) filter_of(name).enabled = on and true or false end,
  EnableInputFilterWithContext = function(name, on) filter_of(name).enabled = on and true or false end,
  IsInputFilterEnabled = function(name) return filter_of(name).enabled end,
})

--- The actions that a key fires now: {map, action, input} for each enabled map, in the order of the profile.
local function actions_of(key, down)
  local out = {}
  for _, mapName in ipairs(Swc.actionMapOrder) do
    if Swc.ActionMapOn(mapName) then
      for _, a in ipairs(Swc.actionMaps[mapName].actions) do
        for _, input in ipairs(a.inputs) do
          if input.key == key then
            local press = input.press
            if press == nil then press = a.press end
            local release = input.release
            if release == nil then release = a.release end
            if (down and press) or (not down and release) then
              out[#out + 1] = {map = mapName, name = a.name}
            end
          end
        end
      end
    end
  end
  return out
end

--- The keys that the actions of a map use (the client keeps them from the game while the map is off).
function Swc.KeysOfMap(mapName)
  local map = Swc.actionMaps[mapName]
  if not map then
    return {}
  end
  if not map.keys then
    map.keys = {}
    for _, a in ipairs(map.actions) do
      for _, input in ipairs(a.inputs) do
        map.keys[input.key] = true
      end
    end
  end
  return map.keys
end

local function escape_rule()
  local handlers = DynamicBus.EscapeKeyNotificationBus.handlers
  if #handlers > 0 then
    DynamicBus.EscapeKeyNotificationBus.Broadcast.OnEscapeKeyPressed()
    return true
  end
  -- The front end has no HUD: the screens of the main menu handle Escape with their ui_cancel handlers (the landing screen,
  -- the options: ExitScreen to the state MainMenuEsc). The native rule of the main menu is not known.
  if Swc.frontend and Swc.frontend.menu then
    return false
  end
  local state = LyShineManagerBus.Broadcast.GetCurrentState()
  if state == HUD_STATE then
    LyShineManagerBus.Broadcast.SetState(ESC_STATE)
    return true
  end
  if Swc.StateEscapable(state) then
    LyShineManagerBus.Broadcast.SetState(HUD_STATE)
    return true
  end
  return false
end

-- The inventory key (toggleInventoryWindow, Tab of the map ui): the native client handles it (no script binds it; the
-- server copy of the flag: PlayerComponentServerFacet_RequestSetIsLookingThroughLoadout). The rule of the emulation: in
-- the state Inventory or Container the key leaves the state (the screen turns the flag off at its transition out); else
-- it sets the flag "looking through loadout" (newinventory.luac then queues the state Inventory).
local INVENTORY_STATE, CONTAINER_STATE = 2972535350, 3349343259
local function inventory_rule()
  local state = LyShineManagerBus.Broadcast.GetCurrentState()
  if state == INVENTORY_STATE or state == CONTAINER_STATE then
    LyShineManagerBus.Broadcast.ExitState(state)
  else
    LocalPlayerUIRequestsBus.Broadcast.SetIsLookingThroughLoadout(true)
  end
  return true
end

-- The journal key (toggleJournalComponent, J of the map ui; AST-236): no loaded script binds it (the NavBar of the retail menus names it
-- as its key hint). The rule of the emulation: in the state Objectives (the journal, 1823500652) the key leaves the state; else, with
-- javelin.enable-journal on, it enters the state (journalscreen.luac shows its objectives tab).
local JOURNAL_STATE = 1823500652
local function journal_rule()
  local state = LyShineManagerBus.Broadcast.GetCurrentState()
  if state == JOURNAL_STATE then
    LyShineManagerBus.Broadcast.ExitState(state)
    return true
  end
  if not Swc.config["javelin.enable-journal"] then
    return false
  end
  LyShineManagerBus.Broadcast.SetState(JOURNAL_STATE)
  return true
end

Swc.actionLog = {}   -- the last actions (the tests read it)

--- A step of the wheel (the CryInput unit: 120 for each step, positive up): the actions of the key mwheel_up or mwheel_down
--- get the signed value, as the native input gives it.
function Swc.FireWheelActions(delta)
  delta = tonumber(delta) or 0
  if delta == 0 then
    return false
  end
  local used = false
  for _, a in ipairs(actions_of(delta > 0 and "mwheel_up" or "mwheel_down", true)) do
    if not filtered(a.name) then
      for _, h in ipairs(Swc.Handlers("CryActionNotificationsBus", a.name)) do
        local fn = h.connected and h.table.OnCryAction
        if type(fn) == "function" then
          local ok, result = Swc.Call(fn, h.table, a.name, delta)
          used = used or (ok and result == true)
        end
      end
    end
  end
  return used
end

--- One key of the host: the actions of the key go to their handlers (OnCryAction). @return True when a handler used an
--- action (returned true) or the escape rule ran.
function Swc.FireActions(key, down)
  local used = false
  local escape = false
  local inventory = false
  local journal = false
  -- The listeners of all the actions of the key, taken before the first call: a listener that connects during this press does
  -- not get it (AST-267: Escape on the appearance step of the creation opened the quit popup with ui_cancel, and the same
  -- press closed it with ui_genpop_cancel; in retail the popup stays).
  local listeners = {}
  for i, a in ipairs(actions_of(key, down)) do
    listeners[i] = Swc.Handlers("CryActionNotificationsBus", a.name)
  end
  for i, a in ipairs(actions_of(key, down)) do
    if not filtered(a.name) then
      Swc.actionLog[#Swc.actionLog + 1] = a.name .. (down and " 1" or " 0")
      if #Swc.actionLog > 50 then
        table.remove(Swc.actionLog, 1)
      end
      for _, h in ipairs(listeners[i]) do
        local fn = h.connected and h.table.OnCryAction
        if type(fn) == "function" then
          local ok, result = Swc.Call(fn, h.table, a.name, down and 1 or 0)
          used = used or (ok and result == true)
        end
      end
      if a.name == "toggleMenuComponent" and down then
        escape = true
      end
      if a.name == "toggleInventoryWindow" and down then
        inventory = true
      end
      if a.name == "toggleJournalComponent" and down then
        journal = true
      end
    end
  end
  if escape and not used then
    used = escape_rule() or used
  end
  if journal and not used then
    used = journal_rule() or used
  end
  if inventory and not used then
    used = inventory_rule() or used
  end
  return used
end
