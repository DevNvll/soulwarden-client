-- The camera requests of the screens (docs/UI.md "The camera of the screens"; the study work/ut567/study2): the native part of
-- JavCameraControllerRequestBus (the camera state overrides, the look-at point, the mouse input) and JavelinCameraRequestBus (the depth
-- of field). 52 scripts of the game call them: the NavBar, the inventory, the conversation, crafting, options, skills, the journal,
-- respawn, the landing screen, and the screens of later stages. One implementation for all: this module loads after swc/items,
-- swc/frontend, and swc/conversation.
--
-- The host (USwcUiSubsystem, ASwcCameraManager) takes the requests one by one (Swc.TakeCameraRequest; a queue of its own, so that the
-- order of the other host requests does not change):
--   "camera state NAME BLEND"       the top override of the stack (NAME, the blend in seconds); "camera state - BLEND": none
--   "camera lookat X Y Z"           the look-at point (meters, x east, y north, z up); "camera lookat -": none
--   "camera dof DISTANCE BLUR NEAR FAR RANGE"   the depth of field (the five values of SetDepthOfField); "camera dof -": off

Swc.camera = Swc.camera or {}
local S = Swc.camera

S.stack = {}          -- the overrides: {name = NAME, blend = SECONDS}, the last one on top
S.lookAt = nil        -- the look-at point (Vector3, meters) or nil
S.mouseInput = true   -- SetCameraEnableMouseInput: stored only (the client locks the camera while the cursor shows)
-- The depth of field. [U] The default values of the native camera: 0 (the old stubs). The scripts read them before a change and write
-- them back after it (craftingscreenv4.luac OnInit, conversationscreenv2.luac SetScreenVisible).
S.dof = {enabled = false, distance = 0, blur = 0, near = 0, far = 0, range = 0}
S.requests = {}
S.sentDof = "camera dof -"   -- the last depth of field request (no request for the same values again)
S.log = {}

local function send(line)
  S.requests[#S.requests + 1] = line
  S.log[#S.log + 1] = line
end

function Swc.TakeCameraRequest()
  return table.remove(S.requests, 1) or ""
end

local function send_state(blend)
  local top = S.stack[#S.stack]
  send(string.format("camera state %s %.3f", top and top.name or "-", tonumber(blend) or 0))
end

-- One request for each change: newinventory.luac sets the same values on each tick.
local function send_dof()
  local d = S.dof
  local line = "camera dof -"
  if d.enabled and d.blur > 0 then
    line = string.format("camera dof %.3f %.3f %.3f %.3f %.3f", d.distance, d.blur, d.near, d.far, d.range)
  end
  if line ~= S.sentDof then
    S.sentDof = line
    send(line)
  end
end

local function remove_named(name)
  for i = #S.stack, 1, -1 do
    if S.stack[i].name == name then
      table.remove(S.stack, i)
      return true
    end
  end
  return false
end

--- The overrides of a screen. OverrideCameraState(NAME, BLEND) puts NAME on top (one entry for each name: a second call moves it up).
--- RestoreCameraState(NAME, BLEND) removes NAME: craftingscreenv4.luac UpdateCameraState removes UI_CraftingStep3 and comes back to
--- UI_CraftingStep1. [I] RestoreCameraState("", BLEND) removes all: the full exits call it (craftingscreenv4.luac OnTransitionOut,
--- conversationscreenv2.luac TryCloseInteraction) after their screen pushed more than one state.
Swc.Define("JavCameraControllerRequestBus", "Broadcast", {
  OverrideCameraState = function(name, blend)
    name = tostring(name or "")
    if name == "" then
      return
    end
    remove_named(name)
    S.stack[#S.stack + 1] = {name = name, blend = tonumber(blend) or 0}
    send_state(blend)
  end,
  RestoreCameraState = function(name, blend)
    name = tostring(name or "")
    local changed
    if name == "" then
      changed = #S.stack > 0
      S.stack = {}
    else
      changed = remove_named(name)
    end
    if changed then
      send_state(blend)
    end
  end,
  ClearAllStateOverrides = function()
    if #S.stack > 0 then
      S.stack = {}
      send_state(0)
    end
  end,
  -- SetCameraLookAt(POSITION, FLAG): conversationscreenv2.luac gives GetNpcLookAtPosition and false. [U] The meaning of the flag.
  SetCameraLookAt = function(position, _)
    if type(position) ~= "table" and type(position) ~= "userdata" then
      return
    end
    local x, y, z = tonumber(position.x), tonumber(position.y), tonumber(position.z)
    if x == nil or y == nil or z == nil then
      return
    end
    S.lookAt = Vector3(x, y, z)
    send(string.format("camera lookat %.3f %.3f %.3f", x, y, z))
  end,
  ClearCameraLookAt = function()
    if S.lookAt ~= nil then
      S.lookAt = nil
      send("camera lookat -")
    end
  end,
  SetCameraEnableMouseInput = function(on)
    S.mouseInput = on and true or false
  end,
  MakeActiveView = function() end,
})

--- The depth of field. SetDepthOfField(DISTANCE, BLUR, NEAR, FAR, RANGE): craftingscreenv4.luac gives 0 0.95 0 0 10 for the result and
--- 6 0.4 0 0 10 for the craft roll (UIStyle.RANGE_DEPTH_OF_FIELD 10); camerahelperfunctions.luac SetCameraBlur gives 0 1 1 3 10. [U] The
--- units of the native values: the client maps them in ASwcCameraManager (one scale).
Swc.Define("JavelinCameraRequestBus", "Broadcast", {
  EnableDepthOfField = function(on)
    on = on and true or false
    if S.dof.enabled ~= on then
      S.dof.enabled = on
      send_dof()
    end
  end,
  SetDepthOfField = function(distance, blur, near, far, range)
    local d = S.dof
    d.distance, d.blur = tonumber(distance) or 0, tonumber(blur) or 0
    d.near, d.far, d.range = tonumber(near) or 0, tonumber(far) or 0, tonumber(range) or 0
    if d.enabled then
      send_dof()
    end
  end,
  ResetDepthOfField = function()
    local was = S.dof.enabled
    S.dof = {enabled = false, distance = 0, blur = 0, near = 0, far = 0, range = 0}
    if was then
      send_dof()
    end
  end,
  GetCurrentDepthOfFieldDistance = function() return S.dof.distance end,
  GetCurrentDepthOfFieldBlur = function() return S.dof.blur end,
})

--- The camera of a new world (the host at the entry of the world and at the exit to the menu): no override, no look-at, no depth of field.
--- [U] Whether retail clears them at a travel, a respawn, or the loading screen (the retail check of ut567).
function Swc.CameraReset()
  S.stack, S.lookAt = {}, nil
  S.dof = {enabled = false, distance = 0, blur = 0, near = 0, far = 0, range = 0}
  S.requests, S.sentDof = {}, "camera dof -"
end
