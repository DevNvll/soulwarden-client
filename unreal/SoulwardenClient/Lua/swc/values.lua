-- The value classes of the engine that the scripts use (Vector2, Vector3, Color, UiOffsets, UiAnchors, Uuid,
-- ScriptTimePoint, Math). The engine gives them to Lua as userdata: a script tests type(x) == "userdata" (for example
-- BaseElement SetupTable), thus the values here are userdata too (newproxy of Lua 5.1) with their fields in a weak
-- table. A method that the emulation does not have is a stub.

local Swc = Swc
local fieldsOf = setmetatable({}, {__mode = "k"})
Swc.valueFields = fieldsOf

--- Makes a value class. @param spec {name, fields = {...}, defaults = {...}, methods = {...}, ops = {...}}
function Swc.ValueClass(spec)
  local proto = newproxy(true)
  local meta = getmetatable(proto)
  local methods = spec.methods or {}
  local fieldSet = {}
  for _, f in ipairs(spec.fields) do
    fieldSet[f] = true
  end
  meta.__index = function(self, key)
    local fields = fieldsOf[self]
    if fieldSet[key] then
      return fields[key]
    end
    local method = methods[key]
    if method ~= nil then
      return method
    end
    if fields and fields[key] ~= nil then
      return fields[key]
    end
    Swc.Stub("value member", spec.name .. "." .. tostring(key))
    return nil
  end
  meta.__newindex = function(self, key, value)
    fieldsOf[self][key] = value
  end
  meta.__tostring = spec.tostring or function(self)
    local parts = {}
    for _, f in ipairs(spec.fields) do
      parts[#parts + 1] = tostring(fieldsOf[self][f])
    end
    return spec.name .. "(" .. table.concat(parts, ", ") .. ")"
  end
  for op, fn in pairs(spec.ops or {}) do
    meta[op] = fn
  end
  local class = {name = spec.name, methods = methods, proto = proto}
  function class.new(values)
    local object = newproxy(proto)
    fieldsOf[object] = values
    return object
  end
  function class.is(value)
    return type(value) == "userdata" and getmetatable(value) == meta
  end
  return class
end

local function num(value, default)
  if type(value) == "number" then
    return value
  end
  return default
end

-- -------------------------------------------------------------------------------------------------------- Vector2

local V2
V2 = Swc.ValueClass({
  name = "Vector2",
  fields = {"x", "y"},
  methods = {
    GetX = function(self) return self.x end,
    GetY = function(self) return self.y end,
    SetX = function(self, v) self.x = v end,
    SetY = function(self, v) self.y = v end,
    Set = function(self, x, y) self.x = x; self.y = y or x end,
    GetLength = function(self) return math.sqrt(self.x * self.x + self.y * self.y) end,
    GetLengthSq = function(self) return self.x * self.x + self.y * self.y end,
    GetDistance = function(self, o) local dx, dy = self.x - o.x, self.y - o.y; return math.sqrt(dx * dx + dy * dy) end,
    GetDistanceSq = function(self, o) local dx, dy = self.x - o.x, self.y - o.y; return dx * dx + dy * dy end,
    Dot = function(self, o) return self.x * o.x + self.y * o.y end,
    GetNormalized = function(self)
      local l = math.sqrt(self.x * self.x + self.y * self.y)
      if l == 0 then return V2.new({x = 0, y = 0}) end
      return V2.new({x = self.x / l, y = self.y / l})
    end,
    Normalize = function(self)
      local l = math.sqrt(self.x * self.x + self.y * self.y)
      if l ~= 0 then self.x = self.x / l; self.y = self.y / l end
      return l
    end,
    Lerp = function(self, o, t) return V2.new({x = self.x + (o.x - self.x) * t, y = self.y + (o.y - self.y) * t}) end,
    Clone = function(self) return V2.new({x = self.x, y = self.y}) end,
    IsZero = function(self) return self.x == 0 and self.y == 0 end,
    IsClose = function(self, o, tolerance)
      tolerance = tolerance or 0.001
      return math.abs(self.x - o.x) <= tolerance and math.abs(self.y - o.y) <= tolerance
    end,
  },
  ops = {
    __add = function(a, b) return V2.new({x = a.x + b.x, y = a.y + b.y}) end,
    __sub = function(a, b) return V2.new({x = a.x - b.x, y = a.y - b.y}) end,
    __mul = function(a, b)
      if type(a) == "number" then return V2.new({x = a * b.x, y = a * b.y}) end
      if type(b) == "number" then return V2.new({x = a.x * b, y = a.y * b}) end
      return V2.new({x = a.x * b.x, y = a.y * b.y})
    end,
    __div = function(a, b)
      if type(b) == "number" then return V2.new({x = a.x / b, y = a.y / b}) end
      return V2.new({x = a.x / b.x, y = a.y / b.y})
    end,
    __unm = function(a) return V2.new({x = -a.x, y = -a.y}) end,
    __eq = function(a, b) return a.x == b.x and a.y == b.y end,
  },
})
Swc.Vector2 = V2
Vector2 = setmetatable({}, {
  __call = function(_, x, y)
    x = num(x, 0)
    return V2.new({x = x, y = num(y, x)})
  end,
  __index = function(_, key)
    if key == "CreateZero" then return function() return V2.new({x = 0, y = 0}) end end
    if key == "CreateOne" then return function() return V2.new({x = 1, y = 1}) end end
    Swc.Stub("value member", "Vector2." .. tostring(key))
    return nil
  end,
})

-- -------------------------------------------------------------------------------------------------------- Vector3

local V3
V3 = Swc.ValueClass({
  name = "Vector3",
  fields = {"x", "y", "z"},
  methods = {
    GetX = function(self) return self.x end,
    GetY = function(self) return self.y end,
    GetZ = function(self) return self.z end,
    SetX = function(self, v) self.x = v end,
    SetY = function(self, v) self.y = v end,
    SetZ = function(self, v) self.z = v end,
    GetLength = function(self) return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z) end,
    GetLengthSq = function(self) return self.x * self.x + self.y * self.y + self.z * self.z end,
    IsFinite = function(self)
      local function finite(v) return v == v and v ~= math.huge and v ~= -math.huge end
      return finite(self.x) and finite(self.y) and finite(self.z)
    end,
    GetDistance = function(self, o)
      local dx, dy, dz = self.x - o.x, self.y - o.y, self.z - o.z
      return math.sqrt(dx * dx + dy * dy + dz * dz)
    end,
    GetDistanceSq = function(self, o)
      local dx, dy, dz = self.x - o.x, self.y - o.y, self.z - o.z
      return dx * dx + dy * dy + dz * dz
    end,
    Dot = function(self, o) return self.x * o.x + self.y * o.y + self.z * o.z end,
    Cross = function(self, o)
      return V3.new({x = self.y * o.z - self.z * o.y, y = self.z * o.x - self.x * o.z, z = self.x * o.y - self.y * o.x})
    end,
    GetNormalized = function(self)
      local l = math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
      if l == 0 then return V3.new({x = 0, y = 0, z = 0}) end
      return V3.new({x = self.x / l, y = self.y / l, z = self.z / l})
    end,
    Normalize = function(self)
      local l = math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
      if l ~= 0 then self.x = self.x / l; self.y = self.y / l; self.z = self.z / l end
      return l
    end,
    Clone = function(self) return V3.new({x = self.x, y = self.y, z = self.z}) end,
    IsZero = function(self) return self.x == 0 and self.y == 0 and self.z == 0 end,
  },
  ops = {
    __add = function(a, b) return V3.new({x = a.x + b.x, y = a.y + b.y, z = a.z + b.z}) end,
    __sub = function(a, b) return V3.new({x = a.x - b.x, y = a.y - b.y, z = a.z - b.z}) end,
    __mul = function(a, b)
      if type(a) == "number" then return V3.new({x = a * b.x, y = a * b.y, z = a * b.z}) end
      if type(b) == "number" then return V3.new({x = a.x * b, y = a.y * b, z = a.z * b}) end
      return V3.new({x = a.x * b.x, y = a.y * b.y, z = a.z * b.z})
    end,
    __div = function(a, b)
      if type(b) == "number" then return V3.new({x = a.x / b, y = a.y / b, z = a.z / b}) end
      return V3.new({x = a.x / b.x, y = a.y / b.y, z = a.z / b.z})
    end,
    __unm = function(a) return V3.new({x = -a.x, y = -a.y, z = -a.z}) end,
    __eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end,
  },
})
Swc.Vector3 = V3
Vector3 = setmetatable({}, {
  __call = function(_, x, y, z)
    x = num(x, 0)
    return V3.new({x = x, y = num(y, x), z = num(z, x)})
  end,
  __index = function(_, key)
    if key == "CreateZero" then return function() return V3.new({x = 0, y = 0, z = 0}) end end
    if key == "CreateAxisX" then return function(s) return V3.new({x = s or 1, y = 0, z = 0}) end end
    if key == "CreateAxisY" then return function(s) return V3.new({x = 0, y = s or 1, z = 0}) end end
    if key == "CreateAxisZ" then return function(s) return V3.new({x = 0, y = 0, z = s or 1}) end end
    Swc.Stub("value member", "Vector3." .. tostring(key))
    return nil
  end,
})

-- ---------------------------------------------------------------------------------------------------------- Color

local C
C = Swc.ValueClass({
  name = "Color",
  fields = {"r", "g", "b", "a"},
  methods = {
    GetR = function(self) return self.r end,
    GetG = function(self) return self.g end,
    GetB = function(self) return self.b end,
    GetA = function(self) return self.a end,
    SetR = function(self, v) self.r = v end,
    SetG = function(self, v) self.g = v end,
    SetB = function(self, v) self.b = v end,
    SetA = function(self, v) self.a = v end,
    Clone = function(self) return C.new({r = self.r, g = self.g, b = self.b, a = self.a}) end,
    Lerp = function(self, o, t)
      return C.new({r = self.r + (o.r - self.r) * t, g = self.g + (o.g - self.g) * t, b = self.b + (o.b - self.b) * t,
                    a = self.a + (o.a - self.a) * t})
    end,
    IsClose = function(self, o, tolerance)
      tolerance = tolerance or 0.001
      return math.abs(self.r - o.r) <= tolerance and math.abs(self.g - o.g) <= tolerance and math.abs(self.b - o.b) <= tolerance
        and math.abs(self.a - o.a) <= tolerance
    end,
  },
  ops = {
    __add = function(a, b) return C.new({r = a.r + b.r, g = a.g + b.g, b = a.b + b.b, a = a.a + b.a}) end,
    __sub = function(a, b) return C.new({r = a.r - b.r, g = a.g - b.g, b = a.b - b.b, a = a.a - b.a}) end,
    __mul = function(a, b)
      if type(a) == "number" then return C.new({r = a * b.r, g = a * b.g, b = a * b.b, a = a * b.a}) end
      if type(b) == "number" then return C.new({r = a.r * b, g = a.g * b, b = a.b * b, a = a.a * b}) end
      return C.new({r = a.r * b.r, g = a.g * b.g, b = a.b * b.b, a = a.a * b.a})
    end,
    __eq = function(a, b) return a.r == b.r and a.g == b.g and a.b == b.b and a.a == b.a end,
  },
})
Swc.Color = C
Color = setmetatable({}, {
  __call = function(_, r, g, b, a)
    if r == nil then
      return C.new({r = 0, g = 0, b = 0, a = 0})
    end
    if g == nil then
      return C.new({r = r, g = r, b = r, a = r})
    end
    return C.new({r = num(r, 0), g = num(g, 0), b = num(b, 0), a = num(a, 1)})
  end,
  __index = function(_, key)
    Swc.Stub("value member", "Color." .. tostring(key))
    return nil
  end,
})

-- ---------------------------------------------------------------------------------------------- UiOffsets, UiAnchors

local function edges_class(name)
  local class
  class = Swc.ValueClass({
    name = name,
    fields = {"left", "top", "right", "bottom"},
    methods = {},
    ops = {
      __eq = function(a, b) return a.left == b.left and a.top == b.top and a.right == b.right and a.bottom == b.bottom end,
      __add = function(a, b)
        return class.new({left = a.left + b.left, top = a.top + b.top, right = a.right + b.right, bottom = a.bottom + b.bottom})
      end,
    },
  })
  return class
end
Swc.UiOffsets = edges_class("UiOffsets")
Swc.UiAnchors = edges_class("UiAnchors")
Swc.UiPadding = edges_class("UiPadding")
UiOffsets = function(l, t, r, b)
  l = num(l, 0)
  return Swc.UiOffsets.new({left = l, top = num(t, l), right = num(r, l), bottom = num(b, l)})
end
UiAnchors = function(l, t, r, b)
  l = num(l, 0)
  return Swc.UiAnchors.new({left = l, top = num(t, l), right = num(r, l), bottom = num(b, l)})
end
UiPadding = function(l, t, r, b)
  l = num(l, 0)
  return Swc.UiPadding.new({left = l, top = num(t, l), right = num(r, l), bottom = num(b, l)})
end

-- ----------------------------------------------------------------------------------------------------------- Uuid

local uuidSerial = 0
local U
U = Swc.ValueClass({
  name = "Uuid",
  fields = {"text"},
  methods = {
    IsNull = function(self) return self.text == "{00000000-0000-0000-0000-000000000000}" end,
    ToString = function(self) return self.text end,
  },
  tostring = function(self) return Swc.valueFields[self].text end,
  ops = {
    __eq = function(a, b) return Swc.valueFields[a].text == Swc.valueFields[b].text end,
  },
})
Swc.Uuid = U
local function new_uuid()
  uuidSerial = uuidSerial + 1
  return U.new({text = string.format("{5357432D-0000-4000-8000-%012X}", uuidSerial)})
end
local function null_uuid()
  return U.new({text = "{00000000-0000-0000-0000-000000000000}"})
end
Uuid = setmetatable({
  -- The scripts call both Uuid:Create() and Uuid.Create().
  Create = function() return new_uuid() end,
  CreateRandom = function() return new_uuid() end,
  CreateNull = function() return null_uuid() end,
  CreateString = function(a, b)
    local text = type(a) == "string" and a or b
    return U.new({text = tostring(text)})
  end,
}, {
  __call = function(_, text)
    if text == nil then
      return null_uuid()
    end
    return U.new({text = tostring(text)})
  end,
  __index = function(_, key)
    Swc.Stub("value member", "Uuid." .. tostring(key))
    return nil
  end,
})

-- --------------------------------------------------------------------------------------------------- time points

Swc.ScriptTimePoint = Swc.ValueClass({
  name = "ScriptTimePoint",
  fields = {"seconds"},
  methods = {
    GetSeconds = function(self) return self.seconds end,
    GetMilliseconds = function(self) return self.seconds * 1000 end,
  },
})

-- The wall clock (WallClockTimePoint): seconds. The scripts call WallClockTimePoint(), WallClockTimePoint:Now(), an
-- instance :Now(), :Subtract(other) (a Duration), :GetTimeSinceEpoc() (the name of the fork), FromIso8601.
local function now_seconds()
  return os.time() + (swcui.clock() % 1)
end
local WallClockMethods = {}
Swc.WallClockTimePoint = Swc.ValueClass({
  name = "WallClockTimePoint",
  fields = {"seconds"},
  methods = WallClockMethods,
  ops = {
    __lt = function(a, b) return a.seconds < b.seconds end,
    __le = function(a, b) return a.seconds <= b.seconds end,
    __eq = function(a, b) return a.seconds == b.seconds end,
  },
})
Swc.Duration = Swc.ValueClass({
  name = "Duration",
  fields = {"seconds"},
  methods = {
    ToSeconds = function(self) return self.seconds end,
    ToMilliseconds = function(self) return self.seconds * 1000 end,
    ToSecondsRoundedUp = function(self) return math.ceil(self.seconds) end,
    ToMinutes = function(self) return self.seconds / 60 end,
    ToHours = function(self) return self.seconds / 3600 end,
  },
  ops = {
    __lt = function(a, b) return a.seconds < b.seconds end,
    __le = function(a, b) return a.seconds <= b.seconds end,
    __eq = function(a, b) return a.seconds == b.seconds end,
    __add = function(a, b) return Swc.Duration.new({seconds = a.seconds + b.seconds}) end,
    __sub = function(a, b) return Swc.Duration.new({seconds = a.seconds - b.seconds}) end,
  },
})
local function wall(seconds)
  return Swc.WallClockTimePoint.new({seconds = seconds})
end
WallClockMethods.Now = function() return wall(now_seconds()) end
WallClockMethods.Subtract = function(self, other) return Swc.Duration.new({seconds = self.seconds - other.seconds}) end
WallClockMethods.GetTimeSinceEpoc = function(self) return Swc.Duration.new({seconds = self.seconds}) end
WallClockMethods.GetTimeSinceEpoch = WallClockMethods.GetTimeSinceEpoc
WallClockMethods.AddDuration = function(self, d) return wall(self.seconds + d.seconds) end
WallClockMethods.SubtractDuration = function(self, d) return wall(self.seconds - d.seconds) end
WallClockMethods.IsZero = function(self) return self.seconds == 0 end
WallClockTimePoint = setmetatable({
  Now = function() return wall(now_seconds()) end,
  CreateFromSecondsSinceEpoch = function(s) return wall(s) end,
  FromIso8601 = function(text)
    local y, mo, d, h, mi, se = tostring(text):match("(%d+)-(%d+)-(%d+)T(%d+):(%d+):(%d+)")
    if not y then
      return wall(0)
    end
    return wall(os.time({year = tonumber(y), month = tonumber(mo), day = tonumber(d), hour = tonumber(h), min = tonumber(mi),
                         sec = tonumber(se)}))
  end,
}, {
  __call = function() return wall(now_seconds()) end,
  __index = function(_, key)
    Swc.Stub("value member", "WallClockTimePoint." .. tostring(key))
    return nil
  end,
})
-- The game clock (TimePoint): monotonic seconds of the runtime. The scripts call TimePoint:Now(), an instance
-- :Subtract(other) (a Duration), :AddDuration, :SubtractDuration, :IsValid, :IsZero, TimePoint.FromIso.
local TimePointMethods = {}
Swc.TimePoint = Swc.ValueClass({
  name = "TimePoint",
  fields = {"seconds"},
  methods = TimePointMethods,
  ops = {
    __lt = function(a, b) return a.seconds < b.seconds end,
    __le = function(a, b) return a.seconds <= b.seconds end,
    __eq = function(a, b) return a.seconds == b.seconds end,
  },
})
local function game_time(seconds)
  return Swc.TimePoint.new({seconds = seconds})
end
TimePointMethods.Now = function() return game_time(swcui.clock()) end
TimePointMethods.Subtract = function(self, other) return Swc.Duration.new({seconds = self.seconds - other.seconds}) end
TimePointMethods.AddDuration = function(self, d) return game_time(self.seconds + d.seconds) end
TimePointMethods.SubtractDuration = function(self, d) return game_time(self.seconds - d.seconds) end
TimePointMethods.IsValid = function(self) return self.seconds ~= 0 end
TimePointMethods.IsZero = function(self) return self.seconds == 0 end
TimePoint = setmetatable({
  Now = function() return game_time(swcui.clock()) end,
  FromIso = function() return game_time(0) end,
}, {
  __call = function() return game_time(0) end,
  __index = function(_, key)
    Swc.Stub("value member", "TimePoint." .. tostring(key))
    return nil
  end,
})

Duration = setmetatable({}, {
  __call = function(_, seconds) return Swc.Duration.new({seconds = seconds or 0}) end,
  __index = function(_, key)
    if key == "CreateFromSeconds" or key == "FromSecondsUnrounded" then return function(s) return Swc.Duration.new({seconds = s}) end end
    if key == "CreateFromMilliseconds" then return function(ms) return Swc.Duration.new({seconds = ms / 1000}) end end
    if key == "CreateFromMinutes" or key == "FromMinutesUnrounded" then return function(m) return Swc.Duration.new({seconds = m * 60}) end end
    if key == "FromHoursUnrounded" then return function(h) return Swc.Duration.new({seconds = h * 3600}) end end
    Swc.Stub("value member", "Duration." .. tostring(key))
    return nil
  end,
})

-- ----------------------------------------------------------------------------------------------------------- Math

Math = setmetatable({
  CreateCrc32 = function(text) return swcui.crc32(string.lower(tostring(text))) end,
  Clamp = function(v, lo, hi) return math.min(math.max(v, lo), hi) end,
  Lerp = function(a, b, t) return a + (b - a) * t end,
  Sign = function(v) if v > 0 then return 1 elseif v < 0 then return -1 end return 0 end,
  Round = function(v) return math.floor(v + 0.5) end,
  RadToDeg = function(v) return v * 180 / math.pi end,
  DegToRad = function(v) return v * math.pi / 180 end,
  IsClose = function(a, b, tolerance) return math.abs(a - b) <= (tolerance or 0.001) end,
  -- The text of a value for a log line (respawn.luac logs the state of a transition).
  GetDebugString = function(v) return tostring(v) end,
}, {
  __index = function(_, key)
    Swc.Stub("function", "Math." .. tostring(key))
    return nil
  end,
})

Crc32 = function(text)
  return swcui.crc32(string.lower(tostring(text)))
end

-- ------------------------------------------------------------------------------------------------- engine vectors

-- The AZStd vectors of the engine classes (vector_int, vector_basic_string_char_char_traits_char, ...): a Lua table
-- with the elements at 1..n (as the vectors that the buses return) and the methods of the engine class.
local VectorMethods = {
  push_back = function(self, v) self[#self + 1] = v end,
  pop_back = function(self) self[#self] = nil end,
  size = function(self) return #self end,
  empty = function(self) return #self == 0 end,
  clear = function(self) for i = #self, 1, -1 do self[i] = nil end end,
  at = function(self, i) return self[i] end,
  front = function(self) return self[1] end,
  back = function(self) return self[#self] end,
}
local VectorMeta = {__index = VectorMethods}
function Swc.Vector(name)
  return function(...)
    local v = setmetatable({}, VectorMeta)
    for i, x in ipairs({...}) do
      v[i] = x
    end
    return v
  end
end
