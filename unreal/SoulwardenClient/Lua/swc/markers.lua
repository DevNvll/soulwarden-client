-- The markers in the world (stage WF7; docs/UI.md "The world views"): the native part of the marker manager. The original scripts
-- of the canvas lyshineui/markers/markermanager.dynamicuicanvas (markermanager.luac, marker.luac, markerdata.luac,
-- markerstatuseffect.luac) register the marker types (SetMarkerParams) and the pools of marker elements (AddMarkerBucketGroup), and
-- clone the elements of each pool (Hud.LocalPlayer.Markers.<identifier><index>). The native code of the game (not in the scripts)
-- gives each marker of the world an element of its pool, writes the data nodes of that element, and puts the element at the screen
-- point of its entity. This module is that native part for the creatures (the AI pool, identifier "a"):
--   - Swc.HostViewProjection: the view projection of the camera of the frame (Unreal world space);
--   - Swc.HostPlates: the creatures of USwcCreatureSubsystem::GetPlates, one line each;
--   - the pool: the nearest creatures (the shown ones first) with a registered marker type get the elements;
--   - the data nodes of each element (MarkerComponentId, Type, EntityName, Level, HealthPercent, HealthMax, IsDead, EnemyDifficulty,
--     the status rows, IsVisible) when a value changes;
--   - the place: the feet and the offset (wX, wY, wZ) of the marker params of the type, projected; the scale and the alpha by the
--     distance (scaleStart to scaleDistance: maxScale to minScale; fadeStart to fadeDistance: maxAlpha to minAlpha). The ramps are
--     a rule of this client (the native rule is not known; the values come from markerdata.luac);
--   - the occlusion (allowWorldOcclusion, occlusionInterval): the host traces the points of Swc.HostMarkerPoints and gives the result
--     (Swc.HostMarkerOcclusion); an occluded marker hides (an inference).

local Swc = Swc

local M = {params = {}, groups = {}, objects = {}, slots = {}, view = nil, plates = {}, occluded = {}, anchors = {}, ready = false}
Swc.markers = M

local PREFIX = "Hud.LocalPlayer.Markers."
-- The status rows of a plate (the children StatusEffect1 to StatusEffect5 of the template).
local STATUS_ROWS = 5

-- The feature flags of the plates (the registration of the client settings at image RVA 0x4A3A805 to 0x4A3A92C: each entry has the
-- name, the type, a bool, and the value; an int value is the second DWORD of the value). The fork has them as config values and as
-- data nodes UIFeatures.*.
local FEATURES = {
  ["UIFeatures.multithreadCanvases"] = true,       -- 0x4A3A828
  ["UIFeatures.nameplateStamina"] = true,          -- 0x4A3A86E
  ["UIFeatures.nameplateStatusEffects"] = true,    -- 0x4A3A891 (live: the icons at the plates, Soulwarden evidence abilities-crit i7)
  ["UIFeatures.enableNameplateSlider"] = true,     -- 0x4A3A8B4
  ["UIFeatures.namePlateQuantityPc"] = 15,         -- 0x4A3A8D7
  ["UIFeatures.namePlateQuantityConsole"] = 15,    -- 0x4A3A904
}
for name, value in pairs(FEATURES) do
  Swc.config[name] = value
  Swc.SetData(name, value)
end
-- The option of the count of the plates (options.luac: the slider 2 to 100; markermanager.luac OnConfigChanged sets it to the config
-- value namePlateQuantityPc): 15 plates, 10 of them in the AI pool.
Swc.SetData("Hud.LocalPlayer.Options.Video.NameplateQuantity", 15)

-- ------------------------------------------------------------------------------------------------- the native classes

rawset(_G, "MarkerParams", function()
  return {distancePriorities = Swc.Vector("vector_int")()}
end)
rawset(_G, "MarkerBucketGroupParams", function()
  return {}
end)

-- The native code copies the vectors of a bucket group: markermanager.luac fills one vector of identifiers for each group in turn.
local function copy_group(params)
  local out = {}
  for name, value in pairs(params) do
    if type(value) == "table" then
      local list = {}
      for i, item in ipairs(value) do
        list[i] = item
      end
      value = list
    end
    out[name] = value
  end
  return out
end

Swc.Define("LocalPlayerMarkerRequestBus", "Broadcast", {
  IsReadyToDisplay = function() return M.ready end,
  SetMarkerParams = function(typeName, params) M.params[typeName] = params end,
  AddMarkerBucketGroup = function(params)
    M.groups[#M.groups + 1] = copy_group(params)
    return #M.groups - 1
  end,
  UpdateMarkerBucketGroup = function(index, params) M.groups[index + 1] = copy_group(params) end,
})

local function key(id)
  return id and tostring(id) or ""
end

-- The marker of an element (UiMarkerComponent): Initialize gives it the marker id of its entity.
local MarkerObject = {}
MarkerObject.__index = MarkerObject
function MarkerObject:Initialize(markerId)
  self.markerId = markerId
  M.objects[key(markerId)] = self
end
function MarkerObject:Uninitialize()
  if self.markerId then
    M.objects[key(self.markerId)] = nil
  end
  self.markerId = nil
end
function MarkerObject:SetKeepOnScreen(value) self.keepOnScreen = value end
function MarkerObject:SetFadeDistance(start, distance) self.fadeStart, self.fadeDistance = start, distance end
-- The markers of the group, the claim points, and the static places (groupmarker.luac, claimpointmarker.luac): no such marker in
-- this client yet; the place is never on the screen.
function MarkerObject:SetMarkerType(typeName) self.markerType = typeName end
function MarkerObject:OnWorldPositionChanged() return false end
function MarkerObject:OnDistanceChanged() end

local elementMarkers = {}
Swc.Define("UiMarkerBus", "Event", {
  GetMarker = function(element)
    local k = key(element)
    elementMarkers[k] = elementMarkers[k] or setmetatable({element = element}, MarkerObject)
    return elementMarkers[k]
  end,
})

local function slot_of_marker(markerId)
  for _, slot in pairs(M.slots) do
    if slot.markerId and key(slot.markerId) == key(markerId) then
      return slot
    end
  end
  return nil
end

Swc.Define("MarkerRequestBus", "Broadcast", {
  TryInitialize = function() M.ready = true end,
})
Swc.Define("MarkerRequestBus", "Event", {
  SetIsBoss = function(markerId, value) local s = slot_of_marker(markerId) if s then s.isBoss = value end end,
  SetKeepOnScreen = function(markerId, value) local s = slot_of_marker(markerId) if s then s.keepOnScreen = value end end,
  SetMarkerOcclusionEnabled = function(markerId, value) local s = slot_of_marker(markerId) if s then s.noOcclusion = not value end end,
  GetIsLockedToTopOfScreen = function() return false end,
  GetType = function(markerId) local s = slot_of_marker(markerId) return s and s.plate and s.plate.markerType or "" end,
})
Swc.Handled("MarkerNotificationBus")

-- No game mode (an outpost rush, an arena, a war): the game mode of the character is none (markermanager.luac reads the static data
-- of the last game mode for the top padding of the markers).
Swc.Define("GameModeParticipantComponentRequestBus", "Event", {
  GetLastEnteredGameMode = function() return 0 end,
  GetGameModeEntityId = function() return EntityId() end,
  IsInGameMode = function() return false end,
  GetGameModeUsingTeamNamePlate = function() return false end,
})
Swc.Define("GameModeDataManagerBus", "Broadcast", {
  GetGameModeStaticData = function() return {hideCompass = false} end,
})
Swc.Handled("GameModeParticipantComponentNotificationBus")

-- The anchor of the damage numbers at a plate (marker.luac SetIsVisible): the marker id -> the element DamageTextAnchor.
Swc.Define("DamageNumbersAnchorBus", "Event", {
  ConnectMarkerId = function(anchor, markerId) M.anchors[key(markerId)] = anchor end,
  DisconnectMarkerId = function(anchor, markerId)
    if M.anchors[key(markerId)] == anchor then
      M.anchors[key(markerId)] = nil
    end
  end,
})

-- ------------------------------------------------------------------------------------------------------- the data

local statuses = nil
local function status_data()
  if not statuses then
    statuses = swcui.load_json("gamedata/statuseffects.json") or {}
  end
  return statuses
end

--- The view of the frame: the view projection matrix (Unreal FMatrix M[row][column], row vectors: clip = (x, y, z, 1) * M), the size
--- of the viewport (pixels), and the place of the camera (Unreal cm).
function Swc.HostViewProjection(...)
  local a = {...}
  local m = {}
  for i = 1, 16 do
    m[i] = a[i]
  end
  M.view = {m = m, width = a[17], height = a[18], x = a[19], y = a[20], z = a[21]}
end

--- @return The viewport point (x, y) of a point of the world (Unreal cm), or nil behind the camera.
local function project(x, y, z)
  local v = M.view
  if not v then
    return nil
  end
  local m = v.m
  local cx = x * m[1] + y * m[5] + z * m[9] + m[13]
  local cy = x * m[2] + y * m[6] + z * m[10] + m[14]
  local cw = x * m[4] + y * m[8] + z * m[12] + m[16]
  if cw <= 0.0001 then
    return nil
  end
  return (cx / cw * 0.5 + 0.5) * v.width, (1 - (cy / cw * 0.5 + 0.5)) * v.height
end
M.project = project

local function ramp(d, from, to, near, far)
  near, far = near or 1, far or near or 1
  if not from or not to or to <= from then
    return d <= (from or math.huge) and near or far
  end
  local t = math.min(1, math.max(0, (d - from) / (to - from)))
  return near + (far - near) * t
end

local function ai_pool_size()
  for _, group in ipairs(M.groups) do
    for i, identifier in ipairs(group.identifiers or {}) do
      if identifier == "a" then
        return group.sizes and group.sizes[i] or 0
      end
    end
  end
  return 0
end

--- One line of the host for each creature (USwcCreatureSubsystem::GetPlates), the fields separated by tabs: interest, serial, the
--- feet x y z (Unreal cm), name, marker type, creature type, creature type override, level, hides level (0/1), health, health
--- maximum, dead (0/1), dead seconds, immune (0/1), never show health (0/1), force health (0/1), distance (m), shown (0/1), and the
--- statuses ("ID*STACKS,...").
local function parse_plates(text)
  local out = {}
  for line in string.gmatch(text or "", "[^\n]+") do
    local f = {}
    for field in string.gmatch(line .. "\t", "([^\t]*)\t") do
      f[#f + 1] = field
    end
    if #f >= 21 then
      local plate = {interest = tonumber(f[1]), serial = f[2], x = tonumber(f[3]), y = tonumber(f[4]), z = tonumber(f[5]),
                     name = f[6], markerType = f[7], creatureType = f[8], override = f[9], level = tonumber(f[10]) or 0,
                     hidesLevel = f[11] == "1", health = tonumber(f[12]) or 0, healthMax = tonumber(f[13]) or 0, dead = f[14] == "1",
                     deadSeconds = tonumber(f[15]) or -1, immune = f[16] == "1", neverHealth = f[17] == "1", forceHealth = f[18] == "1",
                     distance = tonumber(f[19]) or 0, shown = f[20] == "1", statuses = {}}
      for id, stacks in string.gmatch(f[21], "([^,*]+)%*(%d+)") do
        plate.statuses[#plate.statuses + 1] = {id = id, stacks = tonumber(stacks)}
      end
      out[#out + 1] = plate
    end
  end
  return out
end

-- Writes a data node of a slot when its value changed (a data callback of the scripts gets each set).
local function put(slot, name, value)
  if slot.values[name] ~= value then
    slot.values[name] = value
    Swc.SetData(slot.prefix .. "." .. name, value)
  end
end

-- The status rows of a plate: the statuses with ShowInNameplates (statuseffects.json), by UiNameplatePriority, at most 5; a status
-- with HidesOtherStatusEffectIcons shows alone. The text row: the name of the first status with ShowTextInNameplates.
local function status_rows(plate)
  local data = status_data()
  local rows, text = {}, ""
  for _, s in ipairs(plate.statuses) do
    local row = data[s.id]
    if row and row.nameplate then
      rows[#rows + 1] = {row = row, stacks = math.max(1, s.stacks or 1), id = s.id}
    end
    if row and row.nameplateText and text == "" then
      text = row.name or ""
    end
  end
  table.sort(rows, function(a, b)
    local pa, pb = a.row.priority or 0, b.row.priority or 0
    if pa ~= pb then
      return pa < pb
    end
    return a.id < b.id
  end)
  for _, r in ipairs(rows) do
    if r.row.hidesOthers then
      rows = {r}
      break
    end
  end
  return rows, text
end

local function write_plate(slot, plate)
  put(slot, "Type", plate.markerType)
  put(slot, "EntityName", plate.name)
  put(slot, "HasVitals", true)
  put(slot, "Level", plate.level)
  put(slot, "HealthMax", plate.healthMax)
  put(slot, "HealthPercent", plate.healthMax > 0 and math.max(0, math.min(1, plate.health / plate.healthMax)) or 0)
  put(slot, "IsDead", plate.dead)
  put(slot, "EnemyDifficulty", plate.creatureType ~= "" and swcui.crc32(string.lower(plate.creatureType)) or 0)
  put(slot, "CreatureTypeOverride", plate.override ~= "" and swcui.crc32(string.lower(plate.override)) or 0)
  put(slot, "ForceDisplayHealthBar", plate.forceHealth)
  -- The creatures of the backend are enemies of the character (the red bar of the plate: marker.luac IsFriendlyAI); an ally comes
  -- from its creature type (the overrideIsFriendlyAI of the difficulty table of marker.luac).
  put(slot, "IsTeamAlignmentEnemy", true)
  -- No stamina of a creature in the read path: the stamina bar of the plate hides (marker.luac UpdateStaminaVisibility: a base
  -- maximum of 1 or less).
  put(slot, "StaminaBaseMax", 0)
  put(slot, "StaminaPercent", 1)
  -- No threat data of a creature in the read path (open point).
  put(slot, "IsTargetingPlayer", false)
  local rows, text = status_rows(plate)
  -- The stack size first: markerstatuseffect.luac colours the row (IsNegative: red, else black) only while the row is on.
  for n = 1, STATUS_ROWS do
    local r = rows[n]
    put(slot, "StatusEffectStackSize." .. n, r and r.stacks or 0)
    if r then
      put(slot, "StatusEffectImage." .. n, r.row.icon)
      put(slot, "StatusEffectIsNegative." .. n, r.row.negative)
    end
  end
  put(slot, "StatusEffectsText", text)
end

local function release(slot)
  if slot.plate then
    put(slot, "IsVisible", false)
    Swc.SetData(slot.prefix .. ".StopUpdate", true)
  end
  slot.plate = nil
  slot.markerId = nil
end

local function marker_id_of(plate)
  -- A marker id for each entity of the creature view (its serial; an id of the runtime).
  return EntityId(string.format("8%015d", tonumber(plate.serial) or 0))
end

--- The creatures of the frame (the lines of parse_plates). The pool, the data nodes, and the places of the AI markers.
function Swc.HostPlates(text)
  M.plates = parse_plates(text)
  local size = ai_pool_size()
  if not M.ready or size <= 0 then
    return
  end
  -- The candidates: a marker type that markermanager.luac registered (the AI types of its table).
  local candidates = {}
  for _, plate in ipairs(M.plates) do
    if plate.markerType ~= "" and M.params[plate.markerType] then
      candidates[#candidates + 1] = plate
    end
  end
  table.sort(candidates, function(a, b)
    if a.shown ~= b.shown then
      return a.shown
    end
    return a.distance < b.distance
  end)
  local wanted = {}
  for i = 1, math.min(size, #candidates) do
    wanted[candidates[i].serial] = candidates[i]
  end
  -- The slots: a0 ... a(size - 1). A creature keeps its slot while it is in the pool.
  local free = {}
  for index = 0, size - 1 do
    local slot = M.slots[index]
    if not slot then
      slot = {index = index, prefix = PREFIX .. "a" .. index, values = {}}
      M.slots[index] = slot
    end
    if slot.plate and wanted[slot.plate.serial] then
      slot.plate = wanted[slot.plate.serial]
      wanted[slot.plate.serial] = nil
    else
      release(slot)
      free[#free + 1] = slot
    end
  end
  local queue = {}
  for _, plate in pairs(wanted) do
    queue[#queue + 1] = plate
  end
  table.sort(queue, function(a, b) return a.distance < b.distance end)
  for i, plate in ipairs(queue) do
    local slot = free[i]
    if slot then
      slot.plate = plate
      slot.values = {}
      slot.markerId = marker_id_of(plate)
      Swc.SetData(slot.prefix .. ".MarkerComponentId", slot.markerId)
    end
  end
  for index = 0, size - 1 do
    local slot = M.slots[index]
    if slot.plate then
      write_plate(slot, slot.plate)
      M.place(slot)
    end
  end
end

--- The place, the scale, the alpha, and the visibility of the element of a slot (each frame).
function M.place(slot)
  local plate = slot.plate
  local params = M.params[plate.markerType] or {}
  local object = slot.markerId and M.objects[key(slot.markerId)]
  local element = object and object.element
  local sx, sy = project(plate.x + (params.wX or 0) * 100, plate.y - (params.wY or 0) * 100, plate.z + (params.wZ or 0) * 100)
  local fadeStart = object and object.fadeStart or params.fadeStart
  local fadeEnd = object and object.fadeDistance or params.fadeDistance
  local alpha = ramp(plate.distance, fadeStart, fadeEnd, params.maxAlpha or 1, params.minAlpha or 0)
  local visible = plate.shown and sx ~= nil and alpha > 0 and not (M.occluded[plate.interest] and not slot.noOcclusion)
  put(slot, "IsVisible", visible)
  slot.screen = sx and {sx + (params.sX or 0), sy + (params.sY or 0)} or nil
  if element and visible then
    swcui.set_viewport_position(element, slot.screen[1], slot.screen[2])
    local scale = ramp(plate.distance, params.scaleStart, params.scaleDistance, params.maxScale or 1, params.minScale or 1)
    swcui.set(element, "transform.scale", scale, scale)
    swcui.set(element, "fader.fade", alpha)
  end
end

--- The points of the occlusion test (the markers with allowWorldOcclusion that show or wait): "INTEREST X Y Z ..." (Unreal cm). The
--- host traces from the camera to each point at the occlusion interval of the type.
function Swc.HostMarkerPoints()
  local out = {}
  for _, slot in pairs(M.slots) do
    local plate = slot.plate
    local params = plate and M.params[plate.markerType]
    if params and params.allowWorldOcclusion and not slot.noOcclusion then
      out[#out + 1] = string.format("%d %.1f %.1f %.1f", plate.interest, plate.x + (params.wX or 0) * 100, plate.y - (params.wY or 0) * 100,
        plate.z + (params.wZ or 0) * 100)
    end
  end
  return table.concat(out, " ")
end

--- The result of the occlusion test: "INTEREST:1 INTEREST:0 ..." (1: the world hides the point).
function Swc.HostMarkerOcclusion(text)
  M.occluded = {}
  for interest, hidden in string.gmatch(text or "", "(%d+):(%d)") do
    M.occluded[tonumber(interest)] = hidden == "1"
  end
end

--- @return The viewport point "x y" of the plate of a creature (its interest), or "" (the harness input and the checks).
function Swc.PlatePoint(interest)
  for _, slot in pairs(M.slots) do
    if slot.plate and slot.plate.interest == interest and slot.screen and slot.values.IsVisible then
      return string.format("%.1f %.1f", slot.screen[1], slot.screen[2])
    end
  end
  return ""
end
