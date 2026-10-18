-- The core of the emulation: protected calls, the stub layer, the bus framework, and the loader of the game scripts.
--
-- The stub layer: a global name that no script and no module of the emulation defines is
--   * a name that ends with "Bus": a stub bus (each method returns nil; Connect gives a handler that gets no event);
--   * an enum name (eGroup_Value): a number that is the same for the run (swc/enums.lua has the known values);
--   * else nil.
-- Each first use is a line of the stub log (Runtime::note_stub): the kind, the name, and the game script that used it.

local Swc = Swc

local function pack(...)
  return {n = select("#", ...), ...}
end
Swc.pack = pack

--- @return The game script of the caller: the first chunk on the stack with a game path ("lyshineui/...").
function Swc.Caller()
  for level = 3, 60 do
    local info = debug.getinfo(level, "S")
    if not info then
      break
    end
    local source = info.source
    if source and (source:find("^@lyshineui/") or source:find("^@scripts/")) then
      return source:sub(2)
    end
  end
  return ""
end

function Swc.Stub(kind, name)
  swcui.stub(kind, name, Swc.Caller())
end

function Swc.Note(text)
  swcui.log("info", text)
end

local function traceback(err)
  return debug.traceback(tostring(err), 2)
end

--- Calls a function in protected mode. @return true and the results, or false (the error is in the error log).
function Swc.Call(fn, ...)
  local args = pack(...)
  local results = pack(xpcall(function()
    return fn(unpack(args, 1, args.n))
  end, traceback))
  if not results[1] then
    swcui.log("error", tostring(results[2]))
    return false
  end
  return unpack(results, 1, results.n)
end

-- ---------------------------------------------------------------------------------------------------------- buses

-- The state of each bus: its handlers. A handler is {table, id, connected}.
Swc.busState = {}

local Handler = {}
Handler.__index = Handler

function Handler:Disconnect(id)
  if id ~= nil and self.id ~= nil and self.id ~= id then
    return
  end
  self.connected = false
  local state = Swc.busState[self.busName]
  if state then
    for i = #state.handlers, 1, -1 do
      if state.handlers[i] == self then
        table.remove(state.handlers, i)
      end
    end
  end
end

function Handler:IsConnected()
  return self.connected
end

function Handler:Connect(id)
  if id ~= nil then
    self.id = id
  end
  if not self.connected then
    self.connected = true
    table.insert(Swc.busState[self.busName].handlers, self)
  end
end

function Handler:GetCurrentBusId()
  return self.id
end

local function state_of(name)
  local state = Swc.busState[name]
  if not state then
    state = {name = name, handlers = {}}
    Swc.busState[name] = state
  end
  return state
end

--- Connects a handler table to a bus. @return The handler (Disconnect, IsConnected).
function Swc.Connect(name, handlerTable, id)
  local h = setmetatable({busName = name, table = handlerTable, id = id, connected = true}, Handler)
  table.insert(state_of(name).handlers, h)
  if Swc.onConnect[name] then
    Swc.onConnect[name](h)
  end
  return h
end

--- Hooks of the emulation that run when a script connects to a bus (for example the state of a new screen).
Swc.onConnect = {}

--- @return The handlers of a bus for an address (nil: each handler), as a copy.
function Swc.Handlers(name, id)
  local out = {}
  local state = Swc.busState[name]
  if not state then
    return out
  end
  for _, h in ipairs(state.handlers) do
    if h.connected and (id == nil or h.id == nil or h.id == id) then
      out[#out + 1] = h
    end
  end
  return out
end

--- Sends an event of a notification bus to its handlers (each in protected mode).
--- @return The results of the last handler that has the event.
function Swc.Notify(name, id, event, ...)
  local results
  for _, h in ipairs(Swc.Handlers(name, id)) do
    if h.connected then
      local fn = h.table[event]
      if type(fn) == "function" then
        results = pack(Swc.Call(fn, h.table, ...))
      end
    end
  end
  if results and results[1] then
    return unpack(results, 2, results.n)
  end
end

--- Drops the handlers of a script table (a script component that the runtime deactivated).
function Swc.DropHandlersOf(handlerTable)
  for _, state in pairs(Swc.busState) do
    for i = #state.handlers, 1, -1 do
      local h = state.handlers[i]
      if h.table == handlerTable then
        h.connected = false
        table.remove(state.handlers, i)
      end
    end
  end
end

local function stub_member(kind, busName, mode)
  return {
    __index = function(t, method)
      local full = busName .. "." .. mode .. "." .. tostring(method)
      local fn = function()
        Swc.Stub(kind, full)
        return nil
      end
      rawset(t, method, fn)
      return fn
    end,
  }
end

--- @return The bus object of a global name: Broadcast, Event, Connect. The methods that the emulation does not have
--- are stubs.
function Swc.Bus(name)
  local bus = rawget(_G, name)
  if bus ~= nil then
    return bus
  end
  state_of(name)
  bus = {}
  bus.Broadcast = setmetatable({}, stub_member("broadcast", name, "Broadcast"))
  bus.Event = setmetatable({}, stub_member("event", name, "Event"))
  bus.Connect = function(handlerTable, id)
    if Swc.handledBuses[name] == nil then
      Swc.Stub("connect", name)
    end
    return Swc.Connect(name, handlerTable, id)
  end
  bus.CreateHandler = function(handlerTable, id)
    return Swc.Connect(name, handlerTable, id)
  end
  setmetatable(bus, {
    __index = function(_, key)
      Swc.Stub("bus member", name .. "." .. tostring(key))
      return nil
    end,
  })
  rawset(_G, name, bus)
  return bus
end

--- The notification buses whose events the emulation sends (a Connect to them is not a stub).
Swc.handledBuses = {}

-- The calls of the methods that the emulation has (the run-time surface of a screen, with the stubs of the stub log).
Swc.surface = {}

--- Defines methods of a bus: Swc.Define("UiElementBus", "Event", {SetIsEnabled = function(id, v) ... end}).
function Swc.Define(busName, mode, methods)
  local bus = Swc.Bus(busName)
  local target = bus[mode]
  for method, fn in pairs(methods) do
    local full = busName .. "." .. mode .. "." .. method
    rawset(target, method, function(...)
      Swc.surface[full] = (Swc.surface[full] or 0) + 1
      if Swc.traced[full] then
        Swc.TraceCall(full, ...)
      end
      return fn(...)
    end)
  end
  return bus
end

-- A trace of chosen methods (a check of a run): Swc.Trace("UiImageBus.Event.SetColor").
Swc.traced = {}
function Swc.Trace(name)
  Swc.traced[name] = true
end

local function describe(value)
  if type(value) == "userdata" and getmetatable(value) == getmetatable(EntityId()) then
    local name = swcui.name(value)
    return name and (name .. tostring(value)) or tostring(value)
  end
  return tostring(value)
end

function Swc.TraceCall(name, ...)
  local parts = {}
  for i = 1, select("#", ...) do
    parts[#parts + 1] = describe((select(i, ...)))
  end
  swcui.log("info", "trace " .. name .. "(" .. table.concat(parts, ", ") .. ") from " .. Swc.Caller())
end

--- @return The emulated methods that the scripts called, one line each ("name<TAB>count"), sorted.
function Swc.SurfaceText()
  local lines = {}
  for name, count in pairs(Swc.surface) do
    lines[#lines + 1] = name .. "\t" .. count
  end
  table.sort(lines)
  return table.concat(lines, "\n") .. "\n"
end

--- A class of the engine that the emulation does not have: a callable that makes an object; the object keeps the
--- fields that the scripts set, and a member that no script set is a stub.
function Swc.NativeClass(name)
  local meta = {
    __index = function(_, key)
      Swc.Stub("native member", name .. "." .. tostring(key))
      return nil
    end,
  }
  local class = setmetatable({}, {
    __call = function(_, ...)
      Swc.Stub("native class", name)
      return setmetatable({}, meta)
    end,
    __index = function(_, key)
      Swc.Stub("native member", name .. "." .. tostring(key))
      return nil
    end,
  })
  rawset(_G, name, class)
  return class
end

--- Marks notification buses whose events the emulation sends.
function Swc.Handled(...)
  for _, name in ipairs({...}) do
    Swc.handledBuses[name] = true
    Swc.Bus(name)
  end
end

-- ----------------------------------------------------------------------------------------------------- the globals

Swc.enumValues = {}
Swc.enumGroups = {}

local function enum_stub(name)
  -- eGroup_Value, eGroupValue (eStoreTabBundles), and GroupType_Value (CharacterAttributeType_Strength).
  local group
  if name:find("^e%u") then
    group = name:match("^(.-)_[^_]*$") or name
  elseif name:find("^%u%w*Type_%w+$") then
    group = name:match("^(.-)_[^_]*$")
  end
  if not group then
    return nil
  end
  local value = Swc.enumValues[name]
  if value == nil then
    local count = (Swc.enumGroups[group] or 0)
    Swc.enumGroups[group] = count + 1
    -- A value that no known enum has: 1000 times the group serial plus the serial of the name (unique in a run).
    value = 100000 + count
    Swc.enumValues[name] = value
    Swc.Stub("enum", name)
  end
  return value
end

setmetatable(_G, {
  __index = function(_, name)
    if type(name) ~= "string" then
      return nil
    end
    if name:find("Bus$") or name:find("Bus%u%w*$") then
      Swc.Stub("bus", name)
      return Swc.Bus(name)
    end
    if Swc.absentNames and Swc.absentNames[name] then
      -- A name that the client program does not have (gamedata/enums.json): nil, as in the client.
      Swc.Stub("absent in the client", name)
      return nil
    end
    local value = enum_stub(name)
    if value ~= nil then
      return value
    end
    if name:find("^vector_") or name:find("^unordered_map_") then
      local class = Swc.Vector(name)
      rawset(_G, name, class)
      return class
    end
    Swc.Stub("global", name)
    return nil
  end,
})

-- ------------------------------------------------------------------------------------------------- the script loader

-- The loader of the client (research modding-native-ui.md 2.4): one loader replaces package.loaders. A module name
-- ("LyShineUI._Common.BaseElement") is a game path in lower case with "/" for "." and the ending .luac. The client
-- removed loadstring, loadfile, and dofile; the scripts do not use them.
package.loaders = {
  function(name)
    local chunk, err = swcui.load_game_script(name)
    if chunk then
      return chunk
    end
    return "\n\t" .. tostring(err)
  end,
}
package.path = ""
package.cpath = ""

--- @return The module name of a script path ("lyshineui/a/b.luac" -> "lyshineui.a.b").
function Swc.ModuleName(path)
  local name = string.lower(path):gsub("\\", "/"):gsub("%.luac$", ""):gsub("/", ".")
  return name
end

-- ------------------------------------------------------------------------------------------------------ the log bus

Debug = {}
function Debug.Log(text)
  swcui.log("info", "script: " .. tostring(text))
end
function Debug.Warning(condition, text)
  if not condition then
    swcui.log("info", "script warning: " .. tostring(text))
  end
end
function Debug.Error(condition, text)
  if not condition then
    swcui.log("info", "script error: " .. tostring(text))
  end
end
function Debug.Assert(condition, text)
  if not condition then
    swcui.log("info", "script assert: " .. tostring(text))
  end
end
setmetatable(Debug, {
  __index = function(_, key)
    Swc.Stub("function", "Debug." .. tostring(key))
    return function() end
  end,
})

-- ------------------------------------------------------------------------------------------------- DynamicBus

-- DynamicBus: buses between Lua scripts that the fork makes by name (research ui.md 3.2: 250 names). A script connects
-- a table with an address (DynamicBus.X.Connect(busId, table)); Broadcast.M(...) calls table:M(...) on each handler,
-- Event.M(busId, ...) on the handlers of that address. The result is the result of the last handler. A call that no
-- handler takes is a line of the stub log (kind "dynamic"): often a screen of a later stage.
local function dynamic_bus(name)
  local bus = {handlers = {}}
  local function call(list, method, ...)
    local results
    for _, h in ipairs(list) do
      local fn = h.table[method]
      if type(fn) == "function" then
        results = pack(Swc.Call(fn, h.table, ...))
      end
    end
    if results == nil then
      Swc.Stub("dynamic (no handler)", name .. "." .. tostring(method))
      return nil
    end
    if results[1] then
      return unpack(results, 2, results.n)
    end
  end
  local function copy(id)
    local out = {}
    for _, h in ipairs(bus.handlers) do
      if id == nil or h.id == id then
        out[#out + 1] = h
      end
    end
    return out
  end
  bus.Connect = function(id, handlerTable)
    for _, h in ipairs(bus.handlers) do
      if h.id == id and h.table == handlerTable then
        return h
      end
    end
    local h = {id = id, table = handlerTable}
    h.Disconnect = function() bus.Disconnect(id, handlerTable) end
    bus.handlers[#bus.handlers + 1] = h
    return h
  end
  bus.Disconnect = function(id, handlerTable)
    for i = #bus.handlers, 1, -1 do
      local h = bus.handlers[i]
      if (id == nil or h.id == id) and (handlerTable == nil or h.table == handlerTable) then
        table.remove(bus.handlers, i)
      end
    end
  end
  bus.Broadcast = setmetatable({}, {
    __index = function(_, method)
      return function(...) return call(copy(nil), method, ...) end
    end,
  })
  bus.Event = setmetatable({}, {
    __index = function(_, method)
      return function(id, ...) return call(copy(id), method, ...) end
    end,
  })
  return bus
end

DynamicBus = setmetatable({}, {
  __index = function(t, name)
    local bus = dynamic_bus(tostring(name))
    rawset(t, name, bus)
    return bus
  end,
})
