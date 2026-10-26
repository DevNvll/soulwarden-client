-- The data layer of the fork (LyShineDataLayerBus, DataLayerNodeBus, LyShineDataLayerNotificationsBus). The contract
-- comes from its Lua wrapper lyshineui/uidatalayerinternal.luac (research ui.md 3.3):
--   GetData(path)            the node id of a path (the call makes the node); 0 is "no node"
--   GetDataFromNode(path)    the value at a path
--   SetData(path, value)     sets the value; the observers of the path get it
--   RegisterObserver(path, callbackId, isCallback, isData), UnregisterObserver(path, callbackId)
--   Call/CallOne/CallTwo/CallThree(path, ...)   a call to the callbacks of a path (the path can be its CRC-32)
--   DataLayerNodeBus GetData(node), GetChildren(node) (the names), GetChild(node, name), GetFullPath(node)
-- The wrapper connects LyShineDataLayerNotificationsBus and has CallCallback(callbackId, value or node, isData): the
-- emulation gives each change to it. An observer (isCallback false) gets a change of the value; a callback (isCallback
-- true) gets each set and each call. The C++ of the client writes the Hud.* paths; here the host writes them
-- (swc/host.lua, swc/game.lua).

local Swc = Swc
Swc.Handled("LyShineDataLayerNotificationsBus")

local nodes = {}          -- node id -> {name, parent, path, children = {name -> id}, order = {names}, value}
local byPath = {}         -- path -> node id
local byCrc = {}          -- CRC-32 of a path -> path
local observers = {}      -- path -> {callback key -> {id, isCallback, isData}}
local nextNode = 1

local function crc_of(path)
  return swcui.crc32(path)
end

local function node_of(path, create)
  if type(path) == "number" then
    path = byCrc[path]
    if not path then
      return nil
    end
  end
  path = tostring(path)
  local id = byPath[path]
  if id or not create then
    return id
  end
  local parentId
  local name = path
  local split = path:match("^(.*)%.[^%.]+$")
  if split then
    parentId = node_of(split, true)
    name = path:sub(#split + 2)
  end
  id = nextNode
  nextNode = nextNode + 1
  nodes[id] = {name = name, parent = parentId, path = path, children = {}, order = {}}
  byPath[path] = id
  byCrc[crc_of(path)] = path
  byCrc[crc_of(string.lower(path))] = path
  if parentId then
    local parent = nodes[parentId]
    parent.children[name] = id
    parent.order[#parent.order + 1] = name
  end
  return id
end

local function deliver(path, value, nodeId, isSet, changed)
  local list = observers[path]
  if not list then
    return
  end
  local copy = {}
  for k, o in pairs(list) do
    copy[k] = o
  end
  for _, o in pairs(copy) do
    if o.isCallback or changed then
      -- A data observer gets the value (also false), the other observers get the node.
      local payload = nodeId
      if o.isData then
        payload = value
      end
      for _, h in ipairs(Swc.Handlers("LyShineDataLayerNotificationsBus")) do
        local fn = h.table.CallCallback
        if type(fn) == "function" then
          Swc.Call(fn, h.table, o.id, payload, o.isData)
        end
      end
    end
  end
end

local function same(a, b)
  if type(a) ~= type(b) then
    return false
  end
  if type(a) == "table" then
    return false
  end
  return a == b
end

--- Gives a change below a node to the callbacks of the node (not its plain observers), with the value of the node.
local function deliver_to_callbacks(nodeId)
  local n = nodes[nodeId]
  local list = observers[n.path]
  if not list then
    return
  end
  local copy = {}
  for k, o in pairs(list) do
    if o.isCallback then
      copy[k] = o
    end
  end
  for _, o in pairs(copy) do
    for _, h in ipairs(Swc.Handlers("LyShineDataLayerNotificationsBus")) do
      local fn = h.table.CallCallback
      if type(fn) == "function" then
        Swc.Call(fn, h.table, o.id, o.isData and n.value or nodeId, o.isData)
      end
    end
  end
end

--- A copy of a value object (Vector2, Vector3, Color: userdata of Swc.ValueClass), else the value itself.
local function copy_value(value)
  if Swc.Vector3.is(value) or Swc.Vector2.is(value) or Swc.Color.is(value) then
    return value:Clone()
  end
  return value
end

--- Sets a value (the host and the scripts). A change also goes to the callbacks of each parent node: the HUD layout elements
--- (hudlayoutelement.luac) wait on Hud.ActiveLayout.<Name> and read its table after the layout manager sets each leaf
--- (uidatalayerinternal.luac SetDataWithTable); the retail HUD shows the places of the layout data. @return The node id.
function Swc.SetData(path, value)
  local id = node_of(path, true)
  local n = nodes[id]
  local changed = not n.hasValue or not same(n.value, value)
  -- A value object (Vector3, Vector2, Color) is stored as a copy: the callbacks get the object of the caller, and a script that
  -- changes it (a z of 0 for a distance on the map) does not change the node (ut567: GetNpcLookAtPosition read the player with z 0).
  n.value = copy_value(value)
  n.hasValue = true
  deliver(n.path, value, id, true, changed)
  if changed then
    local parent = n.parent
    while parent do
      deliver_to_callbacks(parent)
      parent = nodes[parent].parent
    end
  end
  return id
end

function Swc.GetData(path)
  local id = node_of(path, false)
  if not id then
    return nil
  end
  return nodes[id].value
end

Swc.Define("LyShineDataLayerBus", "Broadcast", {
  SetData = function(path, value) Swc.SetData(path, value) end,
  GetData = function(path) return node_of(path, true) or 0 end,
  -- A value object (Vector3, Vector2, Color) goes out as a copy, as the value types of the native data layer: a script that changes the
  -- copy (a z of 0 for a distance on the map) does not change the node (conversationscreenv2.luac GetNpcLookAtPosition read
  -- Hud.LocalPlayer.Position with a z of 0; ut567).
  GetDataFromNode = function(path)
    local id = node_of(path, false)
    if id then
      return copy_value(nodes[id].value)
    end
    return nil
  end,
  RegisterObserver = function(path, callbackId, isCallback, isData)
    node_of(path, true)
    path = type(path) == "number" and byCrc[path] or tostring(path)
    observers[path] = observers[path] or {}
    observers[path][tostring(callbackId)] = {id = callbackId, isCallback = isCallback, isData = isData}
  end,
  UnregisterObserver = function(path, callbackId)
    path = type(path) == "number" and byCrc[path] or tostring(path)
    if observers[path] then
      observers[path][tostring(callbackId)] = nil
    end
  end,
  Call = function(path) return Swc.DataCall(path) end,
  CallOne = function(path, a) return Swc.DataCall(path, a) end,
  CallTwo = function(path, a, b) return Swc.DataCall(path, a, b) end,
  CallThree = function(path, a, b, c) return Swc.DataCall(path, a, b, c) end,
  ClearDataTree = function(path)
    local id = node_of(path, false)
    if id then
      for _, name in ipairs(nodes[id].order) do
        local child = nodes[id].children[name]
        if child then
          nodes[child].value = nil
          nodes[child].hasValue = false
        end
      end
    end
  end,
  Reset = function() end,
})

-- The native calls of the data layer by the CRC-32 of a path whose name is not known: the CRC -> a function that gives the value of
-- the call (the node of the call has it).
Swc.callValues = {}

--- A call to the callbacks of a path: the first argument is the value of the call (a data callback gets it).
function Swc.DataCall(path, a, b, c)
  if type(path) == "number" and Swc.callValues[path] then
    if not byCrc[path] then
      byCrc[path] = "Swc.NativeCall." .. path
    end
    a = Swc.callValues[path](a, b, c)
  end
  local id = node_of(path, true)
  if not id then
    return 0
  end
  local n = nodes[id]
  n.value = a
  n.hasValue = a ~= nil
  deliver(n.path, a, id, false, true)
  return id
end

Swc.Define("DataLayerNodeBus", "Event", {
  GetData = function(id)
    local n = nodes[id]
    if n then
      return n.value
    end
    return nil
  end,
  GetChildren = function(id)
    local n = nodes[id]
    local out = {}
    if n then
      for i, name in ipairs(n.order) do
        out[i] = name
      end
    end
    return out
  end,
  GetChild = function(id, name)
    local n = nodes[id]
    return n and n.children[tostring(name)] or 0
  end,
  GetFullPath = function(id) local n = nodes[id] return n and n.path or nil end,
  GetName = function(id) local n = nodes[id] return n and n.name or nil end,
})

--- @return Each path with a value (the report of a run).
function Swc.DataPaths()
  local out = {}
  for path, id in pairs(byPath) do
    if nodes[id].hasValue then
      out[#out + 1] = path
    end
  end
  table.sort(out)
  return out
end
