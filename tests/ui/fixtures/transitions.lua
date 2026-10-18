-- The transitions fixture: the open and the close of the original InfoModal screen. Its script (infomodal.luac) runs
-- its own tweens through the tween wrapper of the game (scriptedentitytweener.luac) when the state machine enters and
-- shows (OnTransitionIn: fadeInQuadOut), and the button of the modal (button.luac) runs its tweens on the hover. The
-- fixture records each animation that a script starts and follows it frame by frame (60 frames a second): its time
-- goes with the frames, its property has the eased value at each frame, the final value at the end, and it ends after
-- its delay and duration. The close (CloseModal) hides the modal at once: its script has no animation for it. Then the
-- ease functions of the emulation against values of the Penner equations of the gem. Pictures: frame sequences of the
-- open (transitions-open-00, -03, -06, -12, -24) and of the hover (transitions-hover-00, -06, -12, -24), and the close.
local canvas = screen("LyShineUI/InfoModal/InfoModal.uicanvas", 60)
local frame = 1 / 60

local records = {}
local replaced = 0
-- A new animation of a property of an entity replaces the one before when it starts (the rule of the gem, swc/tween.lua): at once
-- with no delay (it also removes the queued ones), else at the end of its delay (a delayed animation waits in the queue of the gem).
local function replace_before(record, queuedToo)
  for _, r in ipairs(records) do
    if r ~= record and not r.done and r.a.key == record.a.key and r.serial < record.serial and (queuedToo or not r.queuedNow) then
      r.done, r.replaced, r.queuedNow = r.ticks, true, false
      if not r.untracked then
        replaced = replaced + 1
      end
    end
  end
end
local serial = 0
Swc.tweenObserver = function(a, get)
  serial = serial + 1
  local record = {a = a, get = get, delay = a.delay, duration = a.duration, ticks = 0, done = nil, bad = nil, serial = serial,
                  queuedNow = a.delay > 0}
  if a.delay <= 0 then
    -- An animation with no delay replaces the running one and removes the queued ones of its property.
    replace_before(record, true)
  end
  if type(a.from) == "number" and type(a.to) == "number" and a.duration > 0 and (a.times or 1) == 1 and get then
    records[#records + 1] = record
  elseif a.delay > 0 then
    -- A queued animation that the fixture does not follow still replaces the one before at its start.
    records[#records + 1] = record
    record.untracked = true
  end
end

--- One frame: each recorded animation that runs gets one more frame; its value must be the eased value at its time.
local checked = 0
local function tick()
  local before = serial
  swcui.tick(frame)
  for _, r in ipairs(records) do
    -- An animation that a script started in this tick (an OnComplete, an OnTick) gets its first step in the next tick.
    r.fresh = r.serial > before
  end
  for _, r in ipairs(records) do
    if r.queuedNow and not r.fresh and r.ticks * frame + frame >= r.delay - 1e-6 then
      -- The end of the delay of this frame: the animation starts and replaces the one before.
      r.queuedNow = false
      replace_before(r)
    end
  end
  for _, r in ipairs(records) do
    if r.fresh then
      -- No step in the tick of its start.
    elseif r.untracked then
      r.ticks = r.ticks + 1
      r.done = r.done or (not r.queuedNow and r.ticks) or nil
    elseif not r.done then
      r.ticks = r.ticks + 1
      local a = r.a
      local time = r.ticks * frame - r.delay   -- the time of the animation after its delay
      if time >= r.duration - 1e-6 or a.elapsed >= a.duration then
        r.done = r.ticks
      end
      if time > 0 and math.abs(a.elapsed - math.min(time, a.elapsed)) > 1e-4 then
        r.bad = r.bad or string.format("elapsed %.4f at the time %.4f", a.elapsed, time)
      end
      if time > 0 and swcui.exists(a.entity) then
        local t = math.min(math.max(time / r.duration, 0), 1)
        local p = a.backward and (1 - t) or t
        local expected = a.from + (a.to - a.from) * Swc.TweenEase(a.method, a.kind, p)
        local ok, value = Swc.Call(r.get, a.entity)
        if ok and type(value) == "number" then
          checked = checked + 1
          if math.abs(value - expected) > 1e-3 then
            r.bad = r.bad or string.format("%s %.4f at %.3f s (expected %.4f)", a.property.name, value, time, expected)
          end
        end
      end
    end
  end
end
local function run(seconds, prefix, shots)
  local n = math.floor(seconds * 60 + 0.5)
  for i = 0, n do
    if shots and shots[i] then
      picture(string.format("%s-%02d", prefix, i))
    end
    if i < n then
      tick()
    end
  end
end

local INFOMODAL = Math.CreateCrc32("InfoModal")
local root = swcui.canvas_get(canvas, "root")
-- The open: ShowModal sets the state of the modal; OnTransitionIn starts the tweens.
DynamicBus.InfoModal.Broadcast.ShowModal({pagesData = {{title = "@ui_ok", rows = {{type = "Text", text = "@01_lore_manifest_03"}}}}})
equal(LyShineManagerBus.Broadcast.GetCurrentState(), INFOMODAL, "the modal sets its state")
local opened = #records
run(0.6, "transitions-open", {[0] = true, [3] = true, [6] = true, [12] = true, [24] = true})
check(opened > 0 or #records > 0, "the open starts animations (" .. #records .. ")")

-- The hover of the button of the modal (button.luac): its own tweens (the glow, the text color).
local container = find(root, "ButtonContainer")
local button
for _, child in ipairs(swcui.children(container)) do
  if swcui.enabled_in_hierarchy(child) and swcui.component(child, "interactable") then
    button = child
  end
end
check(button ~= nil, "the modal shows a button")
local before = #records
if button then
  move_to(button)
  run(0.5, "transitions-hover", {[0] = true, [6] = true, [12] = true, [24] = true})
  check(#records > before, "the hover starts animations (" .. (#records - before) .. ")")
  Swc.HostInput("move", 5, 5)
  run(0.5)
end

-- The close: CloseModal of the script (as its close button) hides the modal at once (no animation in its script).
before = #records
Swc.Call(DynamicBus.InfoModal.Broadcast.CloseModal)
run(0.2, "transitions-close", {[0] = true})
equal(#records - before, 0, "animations of the close")
check(not swcui.canvas_get(canvas, "enabled"), "the modal is hidden after the close")
check(LyShineManagerBus.Broadcast.GetCurrentState() == 2702338936, "the state goes back to the HUD")

local ended, late, wrong = 0, 0, 0
local untracked = 0
for _, r in ipairs(records) do
  if r.untracked then
    untracked = untracked + 1
  elseif r.done and not r.replaced then
    ended = ended + 1
    -- The last frame of an animation: the one at its delay plus its duration (the next frame at most).
    local expected = math.ceil((r.delay + r.duration) * 60 - 1e-6)
    if math.abs(r.done - expected) > 1 then
      late = late + 1
      note(string.format("%s: ends at frame %d, expected %d", r.a.property.name, r.done, expected))
    end
  end
  if r.bad then
    wrong = wrong + 1
    note("wrong: " .. r.bad)
  end
end
note(string.format("%d animations, %d ended, %d replaced by a new one, %d values checked", #records, ended, replaced, checked))
equal(ended + replaced, #records - untracked, "each animation ends in the time of the fixture")
equal(late, 0, "animations that end at another frame than their delay plus duration")
equal(wrong, 0, "animations with another value than the ease at their time")
check(checked > 20, "the fixture checked the values of the animations (" .. checked .. ")")

-- The ease functions against values of the Penner equations.
local function e(method, kind, t)
  return Swc.TweenEase(method, kind, t)
end
local In, Out, InOut = ScriptedEntityTweenerEasingType_In, ScriptedEntityTweenerEasingType_Out, ScriptedEntityTweenerEasingType_InOut
near(e(ScriptedEntityTweenerEasingMethod_Quad, In, 0.5), 0.25, 1e-6, "quad in at 0.5")
near(e(ScriptedEntityTweenerEasingMethod_Quad, Out, 0.5), 0.75, 1e-6, "quad out at 0.5")
near(e(ScriptedEntityTweenerEasingMethod_Quad, InOut, 0.25), 0.125, 1e-6, "quad in-out at 0.25")
near(e(ScriptedEntityTweenerEasingMethod_Cubic, Out, 0.5), 0.875, 1e-6, "cubic out at 0.5")
near(e(ScriptedEntityTweenerEasingMethod_Sine, In, 0.5), 1 - math.cos(math.pi / 4), 1e-6, "sine in at 0.5")
near(e(ScriptedEntityTweenerEasingMethod_Expo, In, 0.5), 0.03125, 1e-6, "expo in at 0.5")
near(e(ScriptedEntityTweenerEasingMethod_Back, In, 0.5), -0.0876975, 1e-6, "back in at 0.5")
near(e(ScriptedEntityTweenerEasingMethod_Bounce, Out, 0.5), 0.765625, 1e-6, "bounce out at 0.5")
near(e(ScriptedEntityTweenerEasingMethod_Linear, InOut, 0.3), 0.3, 1e-6, "linear at 0.3")
Swc.tweenObserver = nil
