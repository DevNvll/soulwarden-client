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

local M = {params = {}, groups = {}, objects = {}, slots = {}, view = nil, plates = {}, occluded = {}, anchors = {}, boss = {}, ready = false}
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
-- value namePlateQuantityPc): 15 player plates, 5 full and 10 medium. The AI pool does not change with it: 4 elements, the main
-- register 15 of markermanager.luac (AST-256; research/marker-plates.md section 5: the raw bindings; the decoded text shows the
-- wrong upvalue).
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
  SetIsBoss = function(markerId, value)
    local s = slot_of_marker(markerId)
    if s then
      s.isBoss = value
      if s.plate then M.boss[s.plate.serial] = value or nil end
    end
  end,
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
  -- The places of the elements that follow a point of the world for some frames (the enemy damage numbers).
  if Swc.TickNotices then
    Swc.TickNotices()
  end
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

local function pool_size(wanted)
  for _, group in ipairs(M.groups) do
    for i, identifier in ipairs(group.identifiers or {}) do
      if identifier == wanted then
        return group.sizes and group.sizes[i] or 0
      end
    end
  end
  return 0
end
local function ai_pool_size()
  return pool_size("a")
end

--- One line of the host for each creature (USwcCreatureSubsystem::GetPlates), the fields separated by tabs: interest, serial, the
--- feet x y z (Unreal cm), name, marker type, creature type, creature type override, level, hides level (0/1), health, health
--- maximum, dead (0/1), dead seconds, immune (0/1), never show health (0/1), force health (0/1), distance (m), shown (0/1: the
--- admission of the marker; an AI marker is admitted after the common gates), the statuses ("ID*STACKS,..."), and (AST-256) the timed
--- target (0/1) with the time of its activation (s of the world).
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
                     distance = tonumber(f[19]) or 0, shown = f[20] == "1", statuses = {}, timed = f[22] == "1",
                     stamp = tonumber(f[23]) or 0}
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
-- The log of the plate decisions (AST-256: the user tests showed missing plates and the play log had no plate line): one line for each
-- creature when its decision changes ("plate NAME (interest, D m): shown | the show rule | no marker type | no pool slot | occluded |
-- off screen | alpha 0"), at most 20 lines each second.
local decisions, logBudget, logSecond = {}, 0, -1
local function decide(plate, reason)
  local last = decisions[plate.serial]
  if last == reason then
    return
  end
  decisions[plate.serial] = reason
  local second = math.floor(Swc.time or 0)
  if second ~= logSecond then
    logSecond, logBudget = second, 20
  end
  if logBudget <= 0 then
    return
  end
  logBudget = logBudget - 1
  swcui.log("info", string.format("plate %s (%d, %.1f m): %s", plate.name or "?", plate.interest or 0, plate.distance or 0, reason))
end
M.decide = decide

-- The comparator of the pool (see Swc.HostPlates): true when a comes before b.
local function priority(a, b)
  if a.boss ~= b.boss then
    return a.boss
  end
  if a.onScreen ~= b.onScreen then
    return a.onScreen
  end
  if a.timed ~= b.timed then
    return a.timed
  end
  if a.timed and a.stamp ~= b.stamp then
    return a.stamp > b.stamp
  end
  if a.distance ~= b.distance then
    return a.distance < b.distance
  end
  return a.interest < b.interest
end

function Swc.HostPlates(text)
  M.TickQuestPins()
  M.TickNpcMarkers()
  M.plates = parse_plates(text)
  local present = {}
  for _, plate in ipairs(M.plates) do
    present[plate.serial] = true
    if plate.markerType == "" or not M.params[plate.markerType] then
      decide(plate, "no marker type" .. (plate.markerType ~= "" and (" (" .. plate.markerType .. " not registered)") or ""))
    elseif not plate.shown then
      decide(plate, "not admitted (the show rule of the host)")
    end
  end
  for serial in pairs(decisions) do
    if not present[serial] then
      decisions[serial] = nil
    end
  end
  for serial in pairs(M.boss) do
    if not present[serial] then
      M.boss[serial] = nil
    end
  end
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
  -- The order of the native pool (the comparator 0x4944800; research/marker-fade.md section 4): a boss first, then a marker on the
  -- screen (the comparator flag at 0x9F2900C: its value in retail is not read [U]), then a timed target (the newer activation first),
  -- then the nearest. A creature keeps its slot while it stays in the leading candidates.
  for _, plate in ipairs(candidates) do
    local params = M.params[plate.markerType]
    plate.boss = M.boss[plate.serial] == true
    plate.onScreen = project(plate.x + (params.wX or 0) * 100, plate.y - (params.wY or 0) * 100, plate.z + (params.wZ or 0) * 100) ~= nil
  end
  table.sort(candidates, priority)
  local wanted = {}
  for i = 1, math.min(size, #candidates) do
    wanted[candidates[i].serial] = candidates[i]
  end
  for i = size + 1, #candidates do
    decide(candidates[i], string.format("no pool slot (pool %d)", size))
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
  table.sort(queue, priority)
  for i, plate in ipairs(queue) do
    local slot = free[i]
    if slot then
      slot.plate = plate
      slot.values = {}
      -- A new entity in the slot: no occlusion result of the former one, and the occlusion of the script again (marker.luac sets it).
      M.occluded[plate.interest] = nil
      slot.noOcclusion = nil
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
-- The alpha (the native UI callback 0x49C32E0; research/marker-fade.md section 1): maxAlpha; beyond fadeStart it goes down by the
-- distance over the length of the fade, to minAlpha. fadeDistance is the end of the fade (Wolf: 1 at 10 m, 0.5 at 12.5 m, 0 at 15 m).
-- A timed target keeps maxAlpha and skips the occlusion (0x49C3311, 0x493C01C).
function M.place(slot)
  local plate = slot.plate
  local params = M.params[plate.markerType] or {}
  local object = slot.markerId and M.objects[key(slot.markerId)]
  local element = object and object.element
  local sx, sy = project(plate.x + (params.wX or 0) * 100, plate.y - (params.wY or 0) * 100, plate.z + (params.wZ or 0) * 100)
  local fadeStart = object and object.fadeStart or params.fadeStart
  local fadeEnd = object and object.fadeDistance or params.fadeDistance
  local maxAlpha, minAlpha = params.maxAlpha or 1, params.minAlpha or 0
  local alpha = maxAlpha
  if not plate.timed and fadeStart and fadeEnd and fadeEnd > fadeStart and plate.distance > fadeStart then
    alpha = math.max(minAlpha, maxAlpha - (plate.distance - fadeStart) / (fadeEnd - fadeStart))
  end
  local occluded = M.occluded[plate.interest] and params.allowWorldOcclusion and not slot.noOcclusion and not plate.timed
  local visible = plate.shown and sx ~= nil and alpha > 0 and not occluded
  put(slot, "IsVisible", visible)
  if plate.shown then
    decide(plate, visible and "shown" or (sx == nil and "off screen") or (alpha <= 0 and "alpha 0 (the fade of the type)")
      or "occluded (a line from the camera hits the world)")
  end
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
    if params and params.allowWorldOcclusion and not slot.noOcclusion and not plate.timed then
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

-- ------------------------------------------------------------------------------------------------- the quest pins (stage Q2)

-- The world pins of the quests (AST-237; docs/QUESTS.md "The pins (Q2, AST-237)"; research/quest-pins.md section 4): the marker type
-- DiegeticObjectivePin of the static pool (identifier "w"; markermanager.luac typeToMarkerMap; staticworldmarker.luac shows the icon of
-- the objective of the data node DiegeticPinObjectiveInstanceId). The candidates come from the one pin list of swc/questpins.lua. [R]:
--   - a candidate: a published task position (enabled and unfinished) of a task with ShowDiegeticTaskPin; isTracked is not a filter
--     (the native add consumer 0x39D5E90);
--   - the pool: the 5 local pin entities of the player slice; the candidates by squared XYZ distance, the nearest first; a candidate
--     beyond 150 m (javelin.objectives-max-diegeticPinDistance) has no pin; a candidate keeps its slot while it stays in the pool
--     (allocator 0x39E28C0); the distances and the allocation each 2 s (javelin.objectives-diegeticPinUpdateIntervalSeconds) and at each
--     change of the pin list (the bypass of a large move is not measured [U]);
--   - the height (0x39D5F79): a DiegeticPinZOverride of at least -100 replaces Z; else Z + 1.75 m (javelin.objectives-diegeticPinZOffset);
--   - the place, the scale, and the alpha: the marker params of the type (markerdata.luac: scale 1 at 4 m to 0.7 at 120 m, alpha 1 at
--     100 m to 0 at 150 m); no occlusion (the params have no allowWorldOcclusion). The gatherable entity pins are a separate path [U].
local QUEST_PIN_TYPE, QUEST_PIN_LIMIT, QUEST_PIN_RANGE, QUEST_PIN_Z, QUEST_PIN_INTERVAL = "DiegeticObjectivePin", 5, 150, 1.75, 2
M.questSlots = {}
local questAllocation = {time = -math.huge, revision = -1}

local function quest_pin_candidates(own)
  local out = {}
  for _, pin in ipairs(Swc.questPins and Swc.questPins.list or {}) do
    if pin.kind == "task" and pin.record and pin.record.showPin then
      local z = pin.record.pinZ or (pin.position[3] + QUEST_PIN_Z)
      local d2 = (pin.position[1] - own[1]) ^ 2 + (pin.position[2] - own[2]) ^ 2 + (z - own[3]) ^ 2
      out[#out + 1] = {pin = pin, x = pin.position[1], y = pin.position[2], z = z, d2 = d2, distance = math.sqrt(d2)}
    end
  end
  table.sort(out, function(a, b) return a.d2 < b.d2 end)
  return out
end

--- The quest pins of the frame: the static slots w0 ... w(n - 1) of the pool (each frame, before the creatures).
function M.TickQuestPins()
  if Swc.questPins then
    Swc.questPins.Refresh()
  end
  local size = math.min(pool_size("w"), QUEST_PIN_LIMIT)
  if not M.ready or size <= 0 then
    return
  end
  local own = Swc.questPins and Swc.questPins.PlayerPosition() or {0, 0, 0}
  local revision = Swc.questPins and Swc.questPins.revision or 0
  local allocate = (Swc.time or 0) - questAllocation.time >= QUEST_PIN_INTERVAL or revision ~= questAllocation.revision
  if allocate then
    questAllocation.time, questAllocation.revision = Swc.time or 0, revision
    local wanted = {}
    local candidates = quest_pin_candidates(own)
    for i = 1, math.min(size, #candidates) do
      if candidates[i].distance <= QUEST_PIN_RANGE then
        wanted[candidates[i].pin.key] = candidates[i]
      end
    end
    local free = {}
    for index = 0, size - 1 do
      local slot = M.questSlots[index]
      if not slot then
        slot = {index = index, prefix = PREFIX .. "w" .. index, values = {}}
        M.questSlots[index] = slot
      end
      if slot.quest and wanted[slot.quest.pin.key] then
        slot.quest = wanted[slot.quest.pin.key]
        wanted[slot.quest.pin.key] = nil
      else
        if slot.quest then
          put(slot, "IsVisible", false)
          put(slot, "DiegeticPinObjectiveInstanceId", nil)
          Swc.SetData(slot.prefix .. ".StopUpdate", true)
          swcui.log("info", "quest pin off: " .. slot.quest.pin.key)
        end
        slot.quest, slot.markerId = nil, nil
        free[#free + 1] = slot
      end
    end
    local queue = {}
    for _, c in pairs(wanted) do
      queue[#queue + 1] = c
    end
    table.sort(queue, function(a, b) return a.d2 < b.d2 end)
    for i, c in ipairs(queue) do
      local slot = free[i]
      if slot then
        slot.quest, slot.values = c, {}
        -- A marker id for each task of a pin (an id of the runtime: 9, then the slot and the task index).
        slot.markerId = EntityId(string.format("9%03d%012d", slot.index, c.pin.index))
        Swc.SetData(slot.prefix .. ".MarkerComponentId", slot.markerId)
        put(slot, "Type", QUEST_PIN_TYPE)
        put(slot, "DiegeticPinObjectiveInstanceId", Swc.quests.objective_id(c.pin.objective))
        swcui.log("info", string.format("quest pin on: %s at %.2f %.2f %.2f (w%d, %.1f m)", c.pin.key, c.x, c.y, c.z, slot.index, c.distance))
      end
    end
  end
  local params = M.params[QUEST_PIN_TYPE] or {}
  for index = 0, size - 1 do
    local slot = M.questSlots[index]
    local c = slot and slot.quest
    if c then
      -- The distance of the frame for the scale and the alpha (the pool itself changes only at an allocation).
      c.distance = math.sqrt((c.x - own[1]) ^ 2 + (c.y - own[2]) ^ 2 + (c.z - own[3]) ^ 2)
      local object = slot.markerId and M.objects[key(slot.markerId)]
      local element = object and object.element
      -- The engine space (m; Y north) to Unreal (cm; Y south): docs/ARCHITECTURE.md "Spaces".
      local sx, sy = project((c.x + (params.wX or 0)) * 100, -(c.y + (params.wY or 0)) * 100, (c.z + (params.wZ or 0)) * 100)
      local alpha = params.maxAlpha or 1
      if params.fadeStart and params.fadeDistance and params.fadeDistance > params.fadeStart and c.distance > params.fadeStart then
        alpha = math.max(params.minAlpha or 0, alpha - (c.distance - params.fadeStart) / (params.fadeDistance - params.fadeStart))
      end
      local visible = sx ~= nil and alpha > 0
      put(slot, "IsVisible", visible)
      slot.screen = sx and {sx + (params.sX or 0), sy + (params.sY or 0)} or nil
      if element and visible then
        swcui.set_viewport_position(element, slot.screen[1], slot.screen[2])
        local scale = ramp(c.distance, params.scaleStart, params.scaleDistance, params.maxScale or 1, params.minScale or 1)
        swcui.set(element, "transform.scale", scale, scale)
        swcui.set(element, "fader.fade", alpha)
      end
    end
  end
end

-- ------------------------------------------------------------------------------------------------- the NPC markers (stage Q4)

-- The overhead Conversation markers of the persons (AST-239; research/quest-conversation.md 8.1): the marker type Conversation of the
-- static pool "w" (markermanager.luac); staticworldmarker.luac shows the icon of the data node NPCState (an object: State, ObjectiveType,
-- ConversationObjectiveInstanceId) and of ObjectiveType. The states come from swc/npcstates.lua (the native query 0x37AC8C0). The place,
-- the scale (1 to 0.35 from 0 to 40 m), and the alpha (the fade start and end at 125 m) come from the marker params of the type
-- (markerdata.luac; wZ 1.9 m); no occlusion. The allocation [I: the native marker allocation of the NPC entities is not traced]: the
-- slots of the pool after the quest pins, the persons within the end of the fade, the persons with a quest state first, then the
-- nearest; the allocation each second (the states of swc/npcstates.lua) and a slot stays with its person.
local NPC_MARKER_TYPE = "Conversation"
M.npcSlots = {}
local npcAllocation = {revision = -1}

local function same_state(a, b)
  return a and b and a.State == b.State and a.ObjectiveType == b.ObjectiveType and a.ConversationObjectiveInstanceId == b.ConversationObjectiveInstanceId
end

--- The NPC markers of the frame (each frame, after the quest pins).
function M.TickNpcMarkers()
  local states = Swc.npcStates
  local first = math.min(pool_size("w"), QUEST_PIN_LIMIT)
  local size = pool_size("w")
  if not M.ready or not states or size <= first then
    return
  end
  local params = M.params[NPC_MARKER_TYPE] or {}
  local range = params.fadeDistance or 125
  local own = Swc.GetData("Hud.LocalPlayer.Position")
  local px, py, pz = own and own.x or 0, own and own.y or 0, own and own.z or 0
  if states.revision ~= npcAllocation.revision then
    npcAllocation.revision = states.revision
    local candidates = {}
    for _, person in ipairs(states.near or {}) do
      local d2 = (person.x - px) ^ 2 + (person.y - py) ^ 2 + (person.z - pz) ^ 2
      if person.state and d2 <= range * range then
        candidates[#candidates + 1] = {person = person, d2 = d2, quest = person.state.State ~= states.STATE.none}
      end
    end
    table.sort(candidates, function(a, b)
      if a.quest ~= b.quest then
        return a.quest
      end
      return a.d2 < b.d2
    end)
    local wanted = {}
    for i = 1, math.min(size - first, #candidates) do
      wanted[candidates[i].person.key] = candidates[i].person
    end
    local free = {}
    for index = first, size - 1 do
      local slot = M.npcSlots[index]
      if not slot then
        slot = {index = index, prefix = PREFIX .. "w" .. index, values = {}}
        M.npcSlots[index] = slot
      end
      if slot.person and wanted[slot.person.key] then
        slot.person = wanted[slot.person.key]
        wanted[slot.person.key] = nil
      else
        if slot.person then
          put(slot, "IsVisible", false)
          Swc.SetData(slot.prefix .. ".StopUpdate", true)
        end
        slot.person, slot.markerId, slot.state = nil, nil, nil
        free[#free + 1] = slot
      end
    end
    local i = 0
    for _, person in pairs(wanted) do
      i = i + 1
      local slot = free[i]
      if slot then
        slot.person, slot.values, slot.state = person, {}, nil
        slot.markerId = EntityId(string.format("7%03d%010d", slot.index, person.crc % 10000000000))
        Swc.SetData(slot.prefix .. ".MarkerComponentId", slot.markerId)
        put(slot, "Type", NPC_MARKER_TYPE)
      end
    end
  end
  for index = first, size - 1 do
    local slot = M.npcSlots[index]
    local person = slot and slot.person
    if person and person.state then
      local state = {State = person.state.State, ObjectiveType = person.state.ObjectiveType,
                     ConversationObjectiveInstanceId = person.state.ConversationObjectiveInstanceId}
      if not same_state(slot.state, state) then
        slot.state = state
        Swc.SetData(slot.prefix .. ".ObjectiveType", state.ObjectiveType)
        Swc.SetData(slot.prefix .. ".NPCState", state)
      end
      local distance = math.sqrt((person.x - px) ^ 2 + (person.y - py) ^ 2 + (person.z - pz) ^ 2)
      local object = slot.markerId and M.objects[key(slot.markerId)]
      local element = object and object.element
      local sx, sy = project((person.x + (params.wX or 0)) * 100, -(person.y + (params.wY or 0)) * 100, (person.z + (params.wZ or 0)) * 100)
      local alpha = params.maxAlpha or 1
      if params.fadeStart and params.fadeDistance and params.fadeDistance > params.fadeStart and distance > params.fadeStart then
        alpha = math.max(params.minAlpha or 0, alpha - (distance - params.fadeStart) / (params.fadeDistance - params.fadeStart))
      elseif params.fadeDistance and distance > params.fadeDistance then
        alpha = 0
      end
      local visible = sx ~= nil and alpha > 0
      put(slot, "IsVisible", visible)
      slot.screen = sx and {sx + (params.sX or 0), sy + (params.sY or 0)} or nil
      if element and visible then
        swcui.set_viewport_position(element, slot.screen[1], slot.screen[2])
        local scale = ramp(distance, params.scaleStart, params.scaleDistance, params.maxScale or 1, params.minScale or 1)
        swcui.set(element, "transform.scale", scale, scale)
        swcui.set(element, "fader.fade", alpha)
      end
    end
  end
end

--- @return The viewport point "x y" of the overhead marker of a person (its key), or "" (the harness input and the checks).
function Swc.NpcMarkerPoint(personKey)
  for _, slot in pairs(M.npcSlots) do
    if slot.person and slot.person.key == personKey and slot.screen and slot.values.IsVisible then
      return string.format("%.1f %.1f", slot.screen[1], slot.screen[2])
    end
  end
  return ""
end

--- @return The viewport point "x y" of the world pin of a task key ("INSTANCE:INDEX"), or "" (the harness input and the checks).
function Swc.QuestPinPoint(taskKey)
  for _, slot in pairs(M.questSlots) do
    if slot.quest and slot.quest.pin.key == taskKey and slot.screen and slot.values.IsVisible then
      return string.format("%.1f %.1f", slot.screen[1], slot.screen[2])
    end
  end
  return ""
end
