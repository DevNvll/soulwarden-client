-- The census fixture (stage P2.3; docs/UI.md "The census"): the census table (swc/census.lua) against the canvases of the
-- game (gamedata/loader.json "canvases") and against the runs of the two loaders of the game.
--   1. The table has one row for each canvas of the game and no other row; each row has a class, a feature family, a
--      stage, and at least one way that the retail client loads it.
--   2. The templates: each of the 164 canvases loads in the runtime with its scripts (the elements of the converted
--      canvas), and goes again; the Lua errors of each canvas go to the notes.
--   3. The UI loader (uiloader.luac) at a world entry asks for exactly the canvases whose gate the table opens, in the order
--      of the load groups: with the values of the build (on-demand loading on) and with on-demand loading off.
--   4. The config table of the UI loader: when the flags of its entries turn true after the preload, it asks for exactly
--      the entries that the table opens (an onDemand entry only with on-demand loading off).
--   5. The dynamic canvas loader (dynamiccanvasloader.luac): its canvas list is exactly the dynamic entries whose gate the
--      table opens, and each state of an entry makes it ask for its canvas.
-- The fixture allows no screen of the loaders but the dynamic canvas loader: each other request is a load failure, as for
-- a screen of a later stage.
local census = Swc.Load("swc.census")
local lists, err = swcui.load_json("gamedata/loader.json")
check(lists ~= nil and lists.canvases ~= nil, "the converter wrote the canvases of the game (tools/convert-ui.sh loader): " .. tostring(err))
lists = lists or {canvases = {}}

local CLASSES = {active = true, stage = true, service = true, debug = true, dormant = true}
local HUD = 2702338936
local OD = "UIFeatures.enable-on-demand-canvas-loading"

local function key_of(path)
  local key = string.lower(tostring(path)):gsub("\\", "/"):gsub("%.dynamicuicanvas$", ""):gsub("%.uicanvas$", "")
  return (key:gsub("^lyshineui/", ""))
end

local function sorted_keys(set)
  local out = {}
  for k in pairs(set) do
    out[#out + 1] = k
  end
  table.sort(out)
  return out
end

--- Checks two sets of paths; the label names the first difference.
local function same_set(actual, expected, label)
  local extra, missing = {}, {}
  for k in pairs(actual) do
    if not expected[k] then
      extra[#extra + 1] = k
    end
  end
  for k in pairs(expected) do
    if not actual[k] then
      missing[#missing + 1] = k
    end
  end
  table.sort(extra)
  table.sort(missing)
  return check(#extra == 0 and #missing == 0, string.format("%s: %d asked, %d expected%s%s", label, #sorted_keys(actual),
    #sorted_keys(expected), extra[1] and (", not expected " .. table.concat(extra, " ")) or "",
    missing[1] and (", not asked " .. table.concat(missing, " ")) or ""))
end

-- ------------------------------------------------------------------------------------------------- 1. the table

local inGame = {}
for _, path in ipairs(lists.canvases) do
  inGame[key_of(path)] = true
end
local rows, classes, features = 0, {}, {}
for key, row in pairs(census.canvases) do
  rows = rows + 1
  classes[row.class] = (classes[row.class] or 0) + 1
  check(CLASSES[row.class] == true, "a class of the table: " .. key .. " " .. tostring(row.class))
  check(type(row.feature) == "string" and row.feature:match("^F%d%d$") ~= nil, "a feature family: " .. key)
  check(type(row.stage) == "string" and row.stage ~= "", "a stage: " .. key)
  check(row.ui or row.config or row.dynamic or row.native or row.script, "a way that the retail client loads it: " .. key)
  features[row.feature] = true
end
same_set(census.canvases, inGame, "the rows of the table against the canvases of the game")
equal(rows, 164, "the rows of the table")
note(string.format("classes: %d active, %d stage, %d service, %d debug, %d dormant; %d feature families",
  classes.active or 0, classes.stage or 0, classes.service or 0, classes.debug or 0, classes.dormant or 0, #sorted_keys(features)))
for key, row in pairs(census.paths) do
  check(not inGame[key] and row.class == "stale", "a loader path with no canvas is stale: " .. key)
end

-- ---------------------------------------------------------------------------------------------- 2. the templates

local errors = 0
local log = swcui.log
swcui.log = function(kind, text, ...)
  if kind == "error" then
    errors = errors + 1
  end
  return log(kind, text, ...)
end
--- @return The number of elements of a converted canvas below its root (the element tree of LyShine; a file can have
--- entities that no element lists as a child: they are not elements of the canvas).
local function tree_size(converted)
  local byId = {}
  for _, e in ipairs(converted.elements) do
    byId[e.id] = e
  end
  local n, stack = 0, {converted.canvas.root}
  while #stack > 0 do
    local e = byId[table.remove(stack)]
    if e then
      n = n + 1
      for _, child in ipairs(e.children or {}) do
        stack[#stack + 1] = child
      end
    end
  end
  return n
end

local loaded, withErrors, elements = 0, {}, 0
for _, key in ipairs(sorted_keys(census.canvases)) do
  local before = errors
  local converted = swcui.load_json("canvases/lyshineui/" .. key .. ".json")
  local live = swcui.find_canvas("LyShineUI/" .. key .. ".uicanvas")
  if live ~= nil and live:IsValid() then
    -- A canvas that the runtime has already (the globals): it stays.
    loaded = loaded + 1
  else
    local canvas, created = swcui.load_canvas("LyShineUI/" .. key .. ".uicanvas")
    if check(canvas ~= nil and type(created) == "table", "the template loads: " .. key) then
      equal(#created, converted and tree_size(converted) or -1, "the elements of the template " .. key)
      elements = elements + #created
      Swc.ActivateEntity(canvas)
      Swc.ActivateElements(created, swcui.canvas_get(canvas, "root"), true)
      frames(1 / 60)
      Swc.UnloadCanvas(canvas)
      loaded = loaded + 1
    end
  end
  if errors > before then
    withErrors[#withErrors + 1] = string.format("%s (%d)", key, errors - before)
  end
end
swcui.log = log
equal(loaded, 164, "the templates that load with their scripts")
note(string.format("templates: %d loaded, %d elements; %d with Lua errors at their activation (screens of later stages): %s",
  loaded, elements, #withErrors, table.concat(withErrors, ", ")))
for key, row in pairs(census.canvases) do
  if row.class == "active" then
    for _, line in ipairs(withErrors) do
      -- The two known errors at the load of the HUD (docs/UI.md, open points).
      if line:find(key, 1, true) == 1 and key ~= "hud2024/lootticker/lootticker2024" then
        note("an active canvas has errors at its activation alone: " .. line)
      end
    end
  end
end

-- ------------------------------------------------------------------------------------------------ the gates

local values = {console = false, editor = false, ftue = false, data = {}}

--- @return True when the gate of an entry is open with the config of the emulation and the values above.
local function open(gate)
  if gate.od ~= nil and gate.od ~= (Swc.config[OD] == true) then return false end
  if gate.console and not values.console then return false end
  if gate.editor and not values.editor then return false end
  if gate.ftue and not values.ftue then return false end
  if gate.data and not values.data[gate.data] then return false end
  for _, flag in ipairs(gate.flags or {}) do
    if Swc.config[flag] ~= true then return false end
  end
  for _, flag in ipairs(gate.notFlags or {}) do
    if Swc.config[flag] == true then return false end
  end
  return true
end

--- @return The paths (and their load group) whose UI loader entry is open.
local function expected_preload()
  local set, groups = {}, {}
  for _, tbl in ipairs({census.canvases, census.paths}) do
    for key, row in pairs(tbl) do
      for _, entry in ipairs(row.ui or {}) do
        if open(entry.gate) then
          set[key] = true
          groups[key] = entry.group
        end
      end
    end
  end
  return set, groups
end

-- Each request of a loader (LyShineScriptBindRequestBus ScriptLoadCanvas -> Swc.LoadScreen).
local requests = {}
local loadScreen = Swc.LoadScreen
Swc.LoadScreen = function(path, autohide, drawOrder)
  requests[#requests + 1] = key_of(path)
  return loadScreen(path, autohide, drawOrder)
end
local function take()
  local set, order = {}, requests
  for _, key in ipairs(requests) do
    set[key] = true
  end
  requests = {}
  return set, order
end

-- ------------------------------------------------------------------------------- 3. the UI loader at a world entry

local loader = g_cachedScripts and g_cachedScripts["lyshineui.uiloader"]
check(loader ~= nil, "the globals script has the loader module")
local world = Math.CreateCrc32("newworld_vitaeeterna")

local function wait_hud(label)
  local waited = 0
  while LyShineManagerBus.Broadcast.GetCurrentState() ~= HUD and waited < 30 do
    frames(0.5)
    waited = waited + 0.5
  end
  equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, label .. ": the loader sets the state HUD after its screens")
end

--- One preload of the UI loader for the world level (the first one through the level events of the host).
local function preload(first, label)
  LyShineManagerBus.Broadcast.SetState(0)
  if first then
    Swc.EnterWorld("newworld_vitaeeterna")
  elseif loader then
    loader.hasInitializedScreenList = false
    loader.screensToPreload = {}
    Swc.Call(loader.StartLoading, loader, world, nil)
  end
  wait_hud(label)
  local asked, order = take()
  local expected, groups = expected_preload()
  same_set(asked, expected, label)
  local last, ordered = 0, true
  for _, key in ipairs(order) do
    local group = groups[key] or 0
    if group < last then
      ordered = false
    end
    last = math.max(last, group)
  end
  check(ordered, label .. ": the requests come in the order of the load groups")
  return asked
end

local build = Swc.config[OD]
equal(build, true, "the value of the build: on-demand canvas loading (the registration at image RVA 0x4A3B08E)")
local on = preload(true, "the UI loader, values of the build")
Swc.config[OD] = false
local off = preload(false, "the UI loader, on-demand loading off")
note(string.format("the UI loader asks for %d canvases with the values of the build, %d with on-demand loading off",
  #sorted_keys(on), #sorted_keys(off)))

-- --------------------------------------------------------------------------------------- 4. the config table

--- The flags of the config table off at the preload, then on: the loader asks for the entries that the table opens.
local function config_run(od, label)
  Swc.config[OD] = od
  local saved = {}
  for _, tbl in ipairs({census.canvases, census.paths}) do
    for _, row in pairs(tbl) do
      for _, entry in ipairs(row.config or {}) do
        saved[entry.flag] = Swc.config[entry.flag]
        Swc.config[entry.flag] = false
        values.data[entry.flag] = false
      end
    end
  end
  preload(false, label .. " (the preload with the flags off)")
  for flag in pairs(saved) do
    Swc.config[flag] = true
    values.data[flag] = true
  end
  Swc.Notify("ConfigSystemEventBus", nil, "OnConfigChanged")
  frames(0.2)
  local asked = take()
  local expected = {}
  for _, tbl in ipairs({census.canvases, census.paths}) do
    for key, row in pairs(tbl) do
      for _, entry in ipairs(row.config or {}) do
        if not (entry.onDemand and od) then
          expected[key] = true
        end
      end
    end
  end
  same_set(asked, expected, label .. ": the loads when the flags turn on")
  for flag, value in pairs(saved) do
    Swc.config[flag] = value
    values.data[flag] = nil
  end
end
config_run(false, "the config table, on-demand loading off")
config_run(true, "the config table, values of the build")
Swc.config[OD] = build

-- ------------------------------------------------------------------------------------ 5. the dynamic canvas loader

allow("LyShineUI/DynamicCanvasLoader/DynamicCanvasLoader.uicanvas")
requests = {}
local dclCanvas = Swc.LoadScreen("LyShineUI/DynamicCanvasLoader/DynamicCanvasLoader.uicanvas", true, 1)
take()
local dcl = dclCanvas:IsValid() and script_of(find(swcui.canvas_get(dclCanvas, "root"), "Screen"), "dynamiccanvasloader.luac")
if check(dcl ~= nil and type(dcl.canvasInfos) == "table", "the dynamic canvas loader has its canvas list") then
  local listed, expected = {}, {}
  for _, info in ipairs(dcl.canvasInfos) do
    listed[key_of(info.path)] = true
  end
  for key, row in pairs(census.canvases) do
    for _, entry in ipairs(row.dynamic or {}) do
      if open(entry.gate) then
        expected[key] = true
      end
    end
  end
  same_set(listed, expected, "the canvas list of the dynamic loader against the table")
  -- Each state of an entry: the loader asks for the canvas (the load fails here; the loader goes back).
  local tested, wrong = 0, {}
  for key, row in pairs(census.canvases) do
    for _, entry in ipairs(row.dynamic or {}) do
      if open(entry.gate) then
        for _, state in ipairs(entry.states or {}) do
          -- A canvas that the loader waits for gets no second request: each state starts with no wait.
          for _, info in ipairs(dcl.canvasInfos) do
            info.isLoaded = false
          end
          dcl.expectedCanvases = {}
          dcl.pendingStateTransition = nil
          Swc.Call(dcl.OnTransitionIn, dcl, HUD, 0, state, 0)
          frames(0.05)
          local asked = take()
          tested = tested + 1
          if not asked[key] then
            wrong[#wrong + 1] = key .. " (" .. tostring(state) .. ")"
          end
          Swc.Call(dcl.OnTransitionOut, dcl, state, 0, HUD, 0)
          frames(0.05)
          take()
        end
      end
    end
  end
  check(#wrong == 0 and tested > 0, string.format("each state of a dynamic entry loads its canvas: %d states%s", tested,
    wrong[1] and (", not loaded: " .. table.concat(wrong, ", ")) or ""))
end
Swc.LoadScreen = loadScreen
