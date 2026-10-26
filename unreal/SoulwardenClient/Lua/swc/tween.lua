-- The tween engine of the client (the C++ gem ScriptedEntityTweener; research ui.md 2.7). The Lua wrapper of the game
-- (scripts/scriptedentitytweener/scriptedentitytweener.luac) calls these bus methods:
--   CacheVirtualProperty(component, property) -> id        a property that the engine can animate
--   SetOptionalParams(timeIntoTween, duration, easeMethod, easeType, delay, timesToPlay, isFrom, isPlayingBackward,
--                     uuid, timelineId, onComplete, onUpdate, onLoop)   the parameters of the next AnimateEntity calls
--   AnimateEntity(entity, propertyId, target)              one animation: from the current value to the target
--   CacheAnimation(propertyId, target, isFirst) -> id, StartCachedAnimation(entity, id, duration, delay, cb, cb, cb)
--   Stop, Pause, Resume, SetSpeed, SetPlayDirectionReversed, SetInitialValue, GetVirtualPropertyValue, Reset
-- and gets ScriptedEntityTweenerNotificationBus OnComplete(uuid), OnUpdate(uuid, value, progress), OnLoop(uuid).
-- A new animation of a property of an entity replaces the animation of that property. The timings and the ease of
-- each animation are the values of the scripts (the original code); the ease functions are the Penner equations of
-- the gem.

local Swc = Swc
Swc.Handled("ScriptedEntityTweenerNotificationBus", "TickBus")

-- ------------------------------------------------------------------------------------------- virtual properties

local function getter_setter(component, property)
  local fader, image, text, transform, transform2d, cell, desaturator, scroll = UiFaderBus.Event, UiImageBus.Event,
    UiTextBus.Event, UiTransformBus.Event, UiTransform2dBus.Event, UiLayoutCellBus.Event, UiDesaturatorBus.Event, nil
  local map = {
    ["UiFaderComponent.Fade"] = {fader.GetFadeValue, fader.SetFadeValue},
    ["UiDesaturatorComponent.Saturation"] = {desaturator.GetSaturationValue, desaturator.SetSaturationValue},
    ["UiImageComponent.Color"] = {image.GetColor, image.SetColor},
    ["UiImageComponent.Alpha"] = {image.GetAlpha, image.SetAlpha},
    ["UiImageComponent.FillAmount"] = {image.GetFillAmount, image.SetFillAmount},
    ["UiImageComponent.RadialFillStartAngle"] = {image.GetRadialFillStartAngle, image.SetRadialFillStartAngle},
    ["UiLayoutCellComponent.MinWidth"] = {cell.GetMinWidth, cell.SetMinWidth},
    ["UiLayoutCellComponent.MinHeight"] = {cell.GetMinHeight, cell.SetMinHeight},
    ["UiLayoutCellComponent.TargetWidth"] = {cell.GetTargetWidth, cell.SetTargetWidth},
    ["UiLayoutCellComponent.TargetHeight"] = {cell.GetTargetHeight, cell.SetTargetHeight},
    ["UiLayoutCellComponent.ExtraWidthRatio"] = {cell.GetExtraWidthRatio, cell.SetExtraWidthRatio},
    ["UiLayoutCellComponent.ExtraHeightRatio"] = {cell.GetExtraHeightRatio, cell.SetExtraHeightRatio},
    ["UiTextComponent.FontSize"] = {text.GetFontSize, text.SetFontSize},
    ["UiTextComponent.Color"] = {text.GetColor, text.SetColor},
    ["UiTextComponent.CharacterSpacing"] = {text.GetCharacterSpacing, text.SetCharacterSpacing},
    ["UiTextComponent.LineSpacing"] = {text.GetLineSpacing, text.SetLineSpacing},
    ["UiTransform2dComponent.ScaleX"] = {transform.GetScaleX, transform.SetScaleX},
    ["UiTransform2dComponent.ScaleY"] = {transform.GetScaleY, transform.SetScaleY},
    ["UiTransform2dComponent.PivotX"] = {transform.GetPivotX, transform.SetPivotX},
    ["UiTransform2dComponent.PivotY"] = {transform.GetPivotY, transform.SetPivotY},
    ["UiTransform2dComponent.LocalPositionX"] = {transform.GetLocalPositionX, transform.SetLocalPositionX},
    ["UiTransform2dComponent.LocalPositionY"] = {transform.GetLocalPositionY, transform.SetLocalPositionY},
    ["UiTransform2dComponent.Rotation"] = {transform.GetZRotation, transform.SetZRotation},
    ["UiTransform2dComponent.LocalWidth"] = {transform2d.GetLocalWidth, transform2d.SetLocalWidth},
    ["UiTransform2dComponent.LocalHeight"] = {transform2d.GetLocalHeight, transform2d.SetLocalHeight},
    ["UiTransform2dComponent.Anchors"] = {transform2d.GetAnchors, transform2d.SetAnchors},
  }
  local pair = map[component .. "." .. property]
  if pair then
    return pair[1], pair[2]
  end
  return nil, nil
end

local properties = {}   -- id -> {name, get, set}
local propertyIds = {}  -- name -> id

local function property_id(component, property)
  local name = tostring(component) .. "." .. tostring(property)
  local id = propertyIds[name]
  if id then
    return id
  end
  id = #properties + 1
  local get, set = getter_setter(tostring(component), tostring(property))
  properties[id] = {name = name, get = get, set = set}
  propertyIds[name] = id
  return id
end

-- ------------------------------------------------------------------------------------------------------- easing

local pi = math.pi
local function bounce_out(t)
  if t < 1 / 2.75 then
    return 7.5625 * t * t
  elseif t < 2 / 2.75 then
    t = t - 1.5 / 2.75
    return 7.5625 * t * t + 0.75
  elseif t < 2.5 / 2.75 then
    t = t - 2.25 / 2.75
    return 7.5625 * t * t + 0.9375
  end
  t = t - 2.625 / 2.75
  return 7.5625 * t * t + 0.984375
end

-- The "in" form of each method; out(t) = 1 - in(1 - t); inOut joins the two halves.
local easeIn = {
  [ScriptedEntityTweenerEasingMethod_Linear] = function(t) return t end,
  [ScriptedEntityTweenerEasingMethod_Quad] = function(t) return t * t end,
  [ScriptedEntityTweenerEasingMethod_Cubic] = function(t) return t * t * t end,
  [ScriptedEntityTweenerEasingMethod_Quart] = function(t) return t * t * t * t end,
  [ScriptedEntityTweenerEasingMethod_Quint] = function(t) return t * t * t * t * t end,
  [ScriptedEntityTweenerEasingMethod_Sine] = function(t) return 1 - math.cos(t * pi / 2) end,
  [ScriptedEntityTweenerEasingMethod_Expo] = function(t) if t == 0 then return 0 end return 2 ^ (10 * (t - 1)) end,
  [ScriptedEntityTweenerEasingMethod_Circ] = function(t) return 1 - math.sqrt(math.max(0, 1 - t * t)) end,
  [ScriptedEntityTweenerEasingMethod_Elastic] = function(t)
    if t == 0 or t == 1 then return t end
    local p = 0.3
    return -(2 ^ (10 * (t - 1))) * math.sin((t - 1 - p / 4) * (2 * pi) / p)
  end,
  [ScriptedEntityTweenerEasingMethod_Back] = function(t) local s = 1.70158 return t * t * ((s + 1) * t - s) end,
  [ScriptedEntityTweenerEasingMethod_Bounce] = function(t) return 1 - bounce_out(1 - t) end,
}

local function ease(method, kind, t)
  local fn = easeIn[method] or easeIn[ScriptedEntityTweenerEasingMethod_Linear]
  if kind == ScriptedEntityTweenerEasingType_In then
    return fn(t)
  elseif kind == ScriptedEntityTweenerEasingType_InOut then
    if t < 0.5 then
      return fn(t * 2) / 2
    end
    return 1 - fn((1 - t) * 2) / 2
  end
  return 1 - fn(1 - t)
end

Swc.TweenEase = ease

-- ----------------------------------------------------------------------------------------------- interpolation

local function lerp(a, b, t)
  if type(a) == "number" and type(b) == "number" then
    return a + (b - a) * t
  end
  if Swc.Color.is(a) and Swc.Color.is(b) then
    return Swc.Color.new({r = a.r + (b.r - a.r) * t, g = a.g + (b.g - a.g) * t, b = a.b + (b.b - a.b) * t, a = a.a + (b.a - a.a) * t})
  end
  if type(a) == "userdata" and type(b) == "userdata" and a.left ~= nil then
    return UiAnchors(a.left + (b.left - a.left) * t, a.top + (b.top - a.top) * t, a.right + (b.right - a.right) * t,
                     a.bottom + (b.bottom - a.bottom) * t)
  end
  if t >= 1 then
    return b
  end
  return a
end

-- ------------------------------------------------------------------------------------------------- animations

local animations = {}   -- tostring(entity) .. "|" .. property id -> animation
-- The animations that wait for their delay (the queued subtasks of the gem ScriptedEntityTweener): a delayed animation does not stop
-- the running animation of its property; at the end of its delay it starts and replaces it, with the value of that moment as its
-- start (damagenumber.luac: the fade in after 0.1 s, then the fade out after 1 s, on the same opacity; button.luac: the pulse of a
-- hover, a fade out and two delayed fades in). An animation with no delay removes the queued ones of its property from an earlier frame. The rules come
-- from these scripts (the code of the gem is not in the game files).
local queued = {}
local tweenFrame = 0    -- the count of the tween ticks (the frame of a queued animation)
local params = {}
local cached = {}       -- cached animation id -> {params, entries = {{property, target}}}
local nextCached = 1
local baseCallbackId = 1000

local function valid_uuid(uuid)
  return uuid ~= nil and type(uuid) == "userdata" and not uuid:IsNull()
end

local function notify(event, uuid, ...)
  if valid_uuid(uuid) then
    Swc.Notify("ScriptedEntityTweenerNotificationBus", nil, event, uuid, ...)
  end
end

local function start(entity, propertyId, target, p)
  local property = properties[propertyId]
  if not property or not property.get then
    if property then
      Swc.Stub("tween property", property.name)
    end
    return
  end
  if entity == nil or not swcui.exists(entity) then
    return
  end
  local ok, current = Swc.Call(property.get, entity)
  if not ok then
    return
  end
  local from, to = current, target
  if p.isFrom then
    from, to = target, current
  end
  local key = tostring(entity) .. "|" .. propertyId
  local a = {
    entity = entity, property = property, from = from, to = to, duration = p.duration or 0, elapsed = p.timeIntoTween or 0,
    delay = p.delay or 0, method = p.easeMethod, kind = p.easeType, times = p.timesToPlay or 1, played = 0,
    backward = p.isPlayingBackward, timeline = p.timelineId, onComplete = p.onComplete, onUpdate = p.onUpdate,
    onLoop = p.onLoop, speed = 1, paused = false, key = key, uuid = p.uuid, target = target, isFrom = p.isFrom,
  }
  a.frame = tweenFrame
  if a.delay > 0 then
    queued[#queued + 1] = a
  else
    -- An animation with no delay also removes the queued animations of its property from an earlier frame (the end of a hover stops
    -- its pulse). A queued animation of the same frame stays (user test 2026-10-26, 8c: progressiontickeritem.luac QueueFadeOut queues
    -- the fade out after 2 s, and in the same call the row fades in with no delay; with the removal each new row of the loot ticker
    -- stayed on the screen, "+5.40" coin). An inference from these scripts: the code of the gem is not in the game files.
    local keep = {}
    for _, q in ipairs(queued) do
      if q.key ~= key or q.frame == tweenFrame then
        keep[#keep + 1] = q
      end
    end
    queued = keep
    animations[key] = a
  end
  if Swc.tweenObserver then
    -- The tests: each animation that a script starts (tests/ui/fixtures/transitions.lua).
    Swc.tweenObserver(a, property.get)
  end
  if a.delay <= 0 and a.duration <= 0 then
    -- No time: the value at once (the gem completes it at the next tick; the scripts see the value now).
    Swc.Call(property.set, entity, a.backward and a.from or a.to)
  end
end

local function step(a, dt)
  if a.paused then
    return false
  end
  dt = dt * a.speed
  if a.delay > 0 then
    a.delay = a.delay - dt
    if a.delay > 0 then
      return false
    end
    dt = -a.delay
    a.delay = 0
  end
  a.elapsed = a.elapsed + dt
  local t = a.duration > 0 and math.min(a.elapsed / a.duration, 1) or 1
  local progress = a.backward and (1 - t) or t
  local value = lerp(a.from, a.to, ease(a.method, a.kind, progress))
  if not swcui.exists(a.entity) then
    return true
  end
  Swc.Call(a.property.set, a.entity, value)
  notify("OnUpdate", a.onUpdate, value, t)
  if t >= 1 then
    a.played = a.played + 1
    if a.times < 0 or a.played < a.times then
      a.elapsed = 0
      notify("OnLoop", a.onLoop)
      return false
    end
    return true
  end
  return false
end

-- The queued animations whose delay ends start now (their start value is the value of this moment). @return The time of each
-- started animation after its delay (the step of this tick).
local function start_queued(dt)
  local started = {}
  local waiting = {}
  for _, a in ipairs(queued) do
    if not a.paused then
      a.delay = a.delay - dt * a.speed
    end
    if a.delay <= 0 and swcui.exists(a.entity) then
      local ok, current = Swc.Call(a.property.get, a.entity)
      if ok then
        if a.isFrom then
          a.from, a.to = a.target, current
        else
          a.from = current
        end
      end
      started[#started + 1] = {a = a, rest = -a.delay}
      a.delay = 0
      animations[a.key] = a
    elseif a.delay > 0 then
      waiting[#waiting + 1] = a
    end
  end
  queued = waiting
  return started
end

function Swc.TickTweens(dt)
  tweenFrame = tweenFrame + 1
  local starts = {}
  for _, s in ipairs(start_queued(dt)) do
    starts[s.a] = s.rest
  end
  local done = {}
  local list = {}
  for key, a in pairs(animations) do
    list[#list + 1] = a
  end
  for _, a in ipairs(list) do
    if animations[a.key] == a and step(a, starts[a] or dt) then
      done[#done + 1] = a
    end
  end
  for _, a in ipairs(done) do
    if animations[a.key] == a then
      animations[a.key] = nil
    end
    notify("OnComplete", a.onComplete)
  end
end

local function drop_queued(entity, propertyId)
  local prefix = entity ~= nil and (tostring(entity) .. "|") or nil
  local keep = {}
  for _, a in ipairs(queued) do
    local entityMatch = prefix == nil or a.key:sub(1, #prefix) == prefix
    local propertyMatch = propertyId == nil or (prefix ~= nil and a.key == prefix .. tostring(propertyId))
    if not (entityMatch and propertyMatch) then
      keep[#keep + 1] = a
    end
  end
  queued = keep
end

function Swc.TweenForget(entity)
  local prefix = tostring(entity) .. "|"
  for key in pairs(animations) do
    if key:sub(1, #prefix) == prefix then
      animations[key] = nil
    end
  end
  drop_queued(entity, nil)
end

--- A simple value animation of the emulation (UiFaderBus Fade): a setter function, linear.
function Swc.TweenValue(entity, name, from, to, duration, set)
  local key = tostring(entity) .. "|" .. name
  animations[key] = {entity = entity, property = {name = name, set = function(_, v) set(v) end}, from = from, to = to,
                     duration = duration, elapsed = 0, delay = 0, method = ScriptedEntityTweenerEasingMethod_Linear,
                     kind = ScriptedEntityTweenerEasingType_In, times = 1, played = 0, speed = 1, key = key}
end

function Swc.TweenIsRunning(entity, name)
  return animations[tostring(entity) .. "|" .. name] ~= nil
end

local function each_animation(entity, propertyId, fn)
  local prefix = entity ~= nil and (tostring(entity) .. "|") or nil
  for key, a in pairs(animations) do
    local entityMatch = prefix == nil or key:sub(1, #prefix) == prefix
    local propertyMatch = propertyId == nil or (prefix ~= nil and key == prefix .. tostring(propertyId))
    if entityMatch and propertyMatch then
      fn(key, a)
    end
  end
end

-- The same for the queued animations (the pause, the speed, the direction).
local function each_queued(entity, propertyId, fn)
  local prefix = entity ~= nil and (tostring(entity) .. "|") or nil
  for _, a in ipairs(queued) do
    local entityMatch = prefix == nil or a.key:sub(1, #prefix) == prefix
    local propertyMatch = propertyId == nil or (prefix ~= nil and a.key == prefix .. tostring(propertyId))
    if entityMatch and propertyMatch then
      fn(a.key, a)
    end
  end
end

Swc.Define("ScriptedEntityTweenerBus", "Broadcast", {
  GenerateBaseCallbackId = function()
    baseCallbackId = baseCallbackId + 100000
    return baseCallbackId
  end,
  GetCallbackIdRange = function() return 100000 end,
  CacheVirtualProperty = function(component, property) return property_id(component, property) end,
  SetOptionalParams = function(timeIntoTween, duration, easeMethod, easeType, delay, timesToPlay, isFrom, isPlayingBackward,
                               uuid, timelineId, onComplete, onUpdate, onLoop)
    params = {timeIntoTween = timeIntoTween, duration = duration, easeMethod = easeMethod, easeType = easeType, delay = delay,
              timesToPlay = timesToPlay, isFrom = isFrom, isPlayingBackward = isPlayingBackward, uuid = uuid,
              timelineId = timelineId, onComplete = onComplete, onUpdate = onUpdate, onLoop = onLoop}
  end,
  AnimateEntity = function(entity, propertyId, target) start(entity, propertyId, target, params) end,
  CacheAnimation = function(propertyId, target, isFirst)
    if isFirst or cached[nextCached - 1] == nil then
      cached[nextCached] = {params = params, entries = {}}
      nextCached = nextCached + 1
    end
    local id = nextCached - 1
    table.insert(cached[id].entries, {property = propertyId, target = target})
    return id
  end,
  StartCachedAnimation = function(entity, id, duration, delay, onComplete, onUpdate, onLoop)
    local c = cached[id]
    if not c then
      return
    end
    local p = {}
    for k, v in pairs(c.params) do
      p[k] = v
    end
    p.duration = duration or p.duration
    p.delay = delay or p.delay
    p.onComplete, p.onUpdate, p.onLoop = onComplete, onUpdate, onLoop
    for _, entry in ipairs(c.entries) do
      start(entity, entry.property, entry.target, p)
    end
  end,
  ReleaseCachedAnimation = function(id) cached[id] = nil end,
  Stop = function(timelineId, entity)
    each_animation(entity, nil, function(key) animations[key] = nil end)
    drop_queued(entity, nil)
  end,
  Pause = function(timelineId, entity, propertyId)
    each_animation(entity, propertyId, function(_, a) a.paused = true end)
    each_queued(entity, propertyId, function(_, a) a.paused = true end)
  end,
  Resume = function(timelineId, entity, propertyId)
    each_animation(entity, propertyId, function(_, a) a.paused = false end)
    each_queued(entity, propertyId, function(_, a) a.paused = false end)
  end,
  SetSpeed = function(timelineId, entity, propertyId, speed)
    each_animation(entity, propertyId, function(_, a) a.speed = speed end)
    each_queued(entity, propertyId, function(_, a) a.speed = speed end)
  end,
  SetPlayDirectionReversed = function(timelineId, entity, propertyId, reversed)
    each_animation(entity, propertyId, function(_, a) a.backward = reversed end)
    each_queued(entity, propertyId, function(_, a) a.backward = reversed end)
  end,
  SetInitialValue = function(uuid, entity, propertyId, value)
    each_animation(entity, propertyId, function(_, a) a.from = value end)
  end,
  GetVirtualPropertyValue = function(entity, propertyId)
    local property = properties[propertyId]
    if property and property.get then
      return property.get(entity)
    end
  end,
  -- Reset stops the running animations (the gem clears its animation tasks); the cached animations of the fork stay
  -- until ReleaseCachedAnimation (globals.luac resets the tweens after the shared scripts cached theirs).
  Reset = function()
    animations = {}
    queued = {}
  end,
})
