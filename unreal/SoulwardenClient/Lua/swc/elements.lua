-- The script components of the elements and the activation order of LyShine (O3DE UiCanvasComponent, facts only):
--   1. the new entities of a canvas load, a clone, or a slice spawn activate (ScriptComponent OnActivate) while the
--      canvas does not have them yet: UiElementBus.Event.GetCanvas gives an invalid id;
--   2. the canvas links them (FixupPostLoad);
--   3. the canvas sends UiInitializationBus InGamePostActivate to each new element, children before their parent;
--   4. (the fork) UiLoadingInitializationBus OnCanvasFinishedLoading to each new element of a canvas load.
-- The game script BaseElement depends on this order: its OnActivate registers the table of the entity, and its
-- InGamePostActivate (OnInit) finds the tables of the children.
--
-- A script component (Lumberyard ScriptComponent): the main chunk of the script runs one time (the require cache);
-- each entity gets an instance table with the base table as metatable (__index = base), `entityId`, and its own
-- `Properties` (the values of the canvas for the keys of base.Properties; a key is the CRC-32 of its lower-case name).

local Swc = Swc

Swc.components = {}   -- tostring(entity id) -> {instance, ...}
Swc.unlinked = {}     -- tostring(entity id) -> true while the canvas does not have the element (step 1)

Swc.Handled("UiInitializationBus", "UiLoadingInitializationBus")

--- @return The value of a raw script property of the canvas data ({t = ..., v = ...}).
local function convert(raw, default)
  if raw == nil then
    return Swc.CopyDefault(default)
  end
  local t = raw.t
  if t == "id" then
    return raw.v and EntityId(raw.v) or EntityId()
  elseif t == "num" or t == "bool" or t == "str" then
    return raw.v
  elseif t == "vec2" then
    return Vector2(raw.v[1], raw.v[2])
  elseif t == "color" then
    return Color(raw.v[1], raw.v[2], raw.v[3], raw.v[4])
  elseif t == "array" then
    -- An array property is 0-based in the scripts (weaponabilityset: self.AbilitySlots[0], for i = 0, #slots).
    local out = {}
    for i, element in ipairs(raw.v) do
      out[i - 1] = convert(element, nil)
    end
    return out
  elseif t == "strs" then
    local out = {}
    for i, text in ipairs(raw.v) do
      out[i - 1] = text
    end
    return out
  end
  return Swc.CopyDefault(default)
end

function Swc.CopyDefault(value)
  if type(value) ~= "table" or getmetatable(value) ~= nil then
    return value
  end
  local out = {}
  for k, v in pairs(value) do
    out[k] = Swc.CopyDefault(v)
  end
  return out
end

--- Resolves the Properties of an instance: each key of the definitions gets the value of the canvas, else its default.
--- A definition is {default = value, ...}, a plain value, or a group (a table with no default).
function Swc.ResolveProperties(definitions, raw)
  local out = {}
  if type(definitions) ~= "table" then
    return out
  end
  for key, definition in pairs(definitions) do
    if type(key) == "string" then
      local value = raw and raw[tostring(swcui.crc32(string.lower(key)))]
      if type(definition) == "table" and getmetatable(definition) == nil then
        if definition.default ~= nil then
          out[key] = convert(value, definition.default)
        else
          out[key] = Swc.ResolveProperties(definition, value and value.t == "group" and value.v or nil)
        end
      else
        out[key] = convert(value, definition)
      end
    end
  end
  return out
end

--- @return The base table of a component script (its main chunk runs one time: the require cache of the client).
function Swc.ScriptBase(path)
  local name = Swc.ModuleName(path)
  local base = package.loaded[name]
  if base == nil then
    base = require(name)
  end
  if type(base) ~= "table" then
    error("script " .. path .. " gave no table", 0)
  end
  if rawget(base, "__index") == nil then
    rawset(base, "__index", base)
  end
  return base
end

local function key(id)
  return tostring(id)
end

--- @return A new instance of a component script for an entity (the properties are the raw values of the canvas).
function Swc.ScriptInstance(entityId, path, properties)
  local base = Swc.ScriptBase(path)
  local instance = {}
  instance.Properties = Swc.ResolveProperties(rawget(base, "Properties"), properties)
  instance.entityId = entityId
  setmetatable(instance, base)
  return instance
end

local function create_instance(entityId, script)
  return Swc.ScriptInstance(entityId, script.script, script.properties)
end

--- Activates the script components of one entity.
function Swc.ActivateEntity(id)
  local k = key(id)
  if Swc.components[k] then
    return
  end
  local list = {}
  Swc.components[k] = list
  for _, script in ipairs(swcui.scripts(id)) do
    local ok, instance = Swc.Call(create_instance, id, script)
    if ok and instance then
      list[#list + 1] = instance
      instance.__swcScript = script.script
      if type(instance.OnActivate) == "function" then
        Swc.Call(instance.OnActivate, instance)
      end
    end
  end
  Swc.ActivateComponents(id)
end

--- The run-time parts of the components with behavior (flipbooks, spawners), when an entity activates.
function Swc.ActivateComponents(id)
  if swcui.has(id, "flipbook") and swcui.get(id, "flipbook.autoPlay") then
    Swc.FlipbookStart(id)
  end
end

local function post_order(root, out)
  for _, child in ipairs(swcui.children(root)) do
    post_order(child, out)
  end
  out[#out + 1] = root
  return out
end

--- Steps 1 to 4 for new elements. @param ids The new elements, parents before children. @param root The root of the
--- new subtree. @param loading True for a canvas load (step 4).
function Swc.ActivateElements(ids, root, loading)
  for _, id in ipairs(ids) do
    Swc.unlinked[key(id)] = true
  end
  for _, id in ipairs(ids) do
    Swc.ActivateEntity(id)
  end
  for _, id in ipairs(ids) do
    Swc.unlinked[key(id)] = nil
  end
  local order = post_order(root, {})
  for _, id in ipairs(order) do
    Swc.Notify("UiInitializationBus", id, "InGamePostActivate")
  end
  if loading then
    for _, id in ipairs(order) do
      Swc.Notify("UiLoadingInitializationBus", id, "OnCanvasFinishedLoading")
    end
  end
end

--- Deactivates the script components of entities (children before parents).
function Swc.DeactivateEntities(ids)
  for _, id in ipairs(ids) do
    local k = key(id)
    local list = Swc.components[k]
    if list then
      Swc.components[k] = nil
      for i = #list, 1, -1 do
        local instance = list[i]
        if type(instance.OnDeactivate) == "function" then
          Swc.Call(instance.OnDeactivate, instance)
        end
        Swc.DropHandlersOf(instance)
      end
    end
    Swc.FlipbookForget(id)
    Swc.TweenForget(id)
  end
end

--- Destroys an element: its scripts deactivate (children first), then the element model removes the subtree.
function Swc.DestroyElement(id)
  if not swcui.exists(id) then
    return
  end
  Swc.DeactivateEntities(post_order(id, {}))
  swcui.destroy(id)
end

--- Clones an element (UiCanvasBus CloneElement): the copy activates with the LyShine order.
function Swc.CloneElement(source, parent, before)
  local id, created = swcui.clone(source, parent, before)
  if id and id:IsValid() then
    Swc.ActivateElements(created, id, false)
  end
  return id
end

-- ------------------------------------------------------------------------------------------------- the spawner

Swc.Handled("UiSpawnerNotificationBus")
local pendingSpawns = {}
local ticketSerial = 0
local Ticket = {}
Ticket.__index = Ticket
Ticket.__tostring = function(self) return "SliceInstantiationTicket(" .. self.serial .. ")" end
Ticket.__eq = function(a, b) return a.serial == b.serial end
function Ticket:IsValid() return true end

--- UiSpawnerBus SpawnSlice: the slice comes at the next tick (the spawn of LyShine is asynchronous).
function Swc.SpawnSlice(spawner, slicePath)
  ticketSerial = ticketSerial + 1
  local ticket = setmetatable({serial = ticketSerial}, Ticket)
  pendingSpawns[#pendingSpawns + 1] = {spawner = spawner, path = slicePath, ticket = ticket}
  return ticket
end

function Swc.RunSpawns()
  if #pendingSpawns == 0 then
    return
  end
  local list = pendingSpawns
  pendingSpawns = {}
  for _, spawn in ipairs(list) do
    Swc.Notify("UiSpawnerNotificationBus", spawn.spawner, "OnSpawnBegin", spawn.ticket)
    local root, created = swcui.spawn(spawn.path, spawn.spawner)
    if not root then
      Swc.Note("spawn of " .. tostring(spawn.path) .. " failed: " .. tostring(created))
      Swc.Notify("UiSpawnerNotificationBus", spawn.spawner, "OnSpawnFailed", spawn.ticket)
    else
      Swc.ActivateElements(created, root, false)
      for _, id in ipairs(created) do
        Swc.Notify("UiSpawnerNotificationBus", spawn.spawner, "OnEntitySpawned", spawn.ticket, id)
      end
      Swc.Notify("UiSpawnerNotificationBus", spawn.spawner, "OnTopLevelEntitiesSpawned", spawn.ticket, {root})
      Swc.Notify("UiSpawnerNotificationBus", spawn.spawner, "OnSpawnEnd", spawn.ticket)
    end
  end
end

-- ------------------------------------------------------------------------------------------------ the flipbooks

-- UiFlipbookAnimationComponent (O3DE UiFlipbookAnimationComponent, facts only): the frame of a sprite sheet steps
-- from the start frame to the end frame at the frame rate; the loop types none, linear (from the loop start frame),
-- and ping-pong; the delays.
Swc.Handled("UiFlipbookAnimationNotificationsBus")
local flipbooks = {}

function Swc.FlipbookStart(id)
  local start = swcui.get(id, "flipbook.start")
  swcui.set(id, "flipbook.frame", start)
  swcui.set(id, "flipbook.playing", true)
  flipbooks[key(id)] = {id = id, time = 0, direction = 1, delay = swcui.get(id, "flipbook.startDelay") or 0}
  Swc.Notify("UiFlipbookAnimationNotificationsBus", id, "OnAnimationStarted")
end

function Swc.FlipbookStop(id)
  if flipbooks[key(id)] then
    flipbooks[key(id)] = nil
    swcui.set(id, "flipbook.playing", false)
    Swc.Notify("UiFlipbookAnimationNotificationsBus", id, "OnAnimationStopped")
  end
end

function Swc.FlipbookForget(id)
  flipbooks[key(id)] = nil
end

function Swc.FlipbookIsPlaying(id)
  return flipbooks[key(id)] ~= nil
end

function Swc.TickFlipbooks(dt)
  for k, book in pairs(flipbooks) do
    local id = book.id
    if not swcui.exists(id) then
      flipbooks[k] = nil
    else
      local rate = swcui.get(id, "flipbook.rate") or 30
      local unit = swcui.get(id, "flipbook.rateUnit") or 0
      local frameTime = unit == 1 and rate or (rate > 0 and 1 / rate or 1)
      if book.delay > 0 then
        book.delay = book.delay - dt
      else
        book.time = book.time + dt
        while book.time >= frameTime and flipbooks[k] do
          book.time = book.time - frameTime
          local frame = swcui.get(id, "flipbook.frame")
          local first = swcui.get(id, "flipbook.start")
          local last = swcui.get(id, "flipbook.end")
          local loopType = swcui.get(id, "flipbook.loopType")
          local nextFrame = frame + book.direction
          if nextFrame > last or nextFrame < first then
            if loopType == 1 then
              nextFrame = swcui.get(id, "flipbook.loopStart")
              book.delay = swcui.get(id, "flipbook.loopDelay") or 0
              Swc.Notify("UiFlipbookAnimationNotificationsBus", id, "OnLoopSequenceCompleted")
            elseif loopType == 2 then
              book.direction = -book.direction
              nextFrame = frame + book.direction
              if book.direction < 0 then
                book.delay = swcui.get(id, "flipbook.reverseDelay") or 0
              else
                book.delay = swcui.get(id, "flipbook.loopDelay") or 0
                Swc.Notify("UiFlipbookAnimationNotificationsBus", id, "OnLoopSequenceCompleted")
              end
            else
              Swc.FlipbookStop(id)
              break
            end
          end
          swcui.set(id, "flipbook.frame", nextFrame)
        end
      end
    end
  end
end
