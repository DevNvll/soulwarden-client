-- The boot script of the bus emulation (docs/UI.md). The runtime (Source/SwcUi, Runtime::start) runs it after it opened
-- Lua 5.1.5 and made the natives (the global table `swcui`). This code is of this project; the scripts of the game come
-- from the converted content (SWC_CONTENT/ui/scripts) through the loader of swc/core.lua.

Swc = {}

--- Loads a module of the emulation (swc/<name>.lua) and returns its result.
function Swc.Load(name)
  local chunk, err = swcui.load_lua(name)
  if not chunk then
    error("emulation module " .. name .. ": " .. tostring(err), 0)
  end
  return chunk()
end

Swc.Load("swc.core")      -- protected calls, the stub layer, the bus framework, the loader of the game scripts
Swc.Load("swc.values")    -- the value classes (Vector2, Color, Uuid, ...)
Swc.Load("swc.enums")     -- the enum values that the emulation knows
Swc.Load("swc.elements")  -- the script components and the activation order of LyShine
Swc.Load("swc.lyshine")   -- the LyShine buses on the element model
Swc.Load("swc.datalayer") -- the data layer of the fork
Swc.Load("swc.manager")   -- the screen manager and the canvas load of the fork
Swc.Load("swc.tween")     -- the tween engine (the gem ScriptedEntityTweener)
Swc.Load("swc.interactables") -- buttons, check boxes, radio buttons, sliders, scroll boxes, text inputs, drop-downs
Swc.Load("swc.input")     -- the input of the UI: hover, press, release, drag, wheel, keys, text
Swc.Load("swc.dynamic")   -- the dynamic lists: the dynamic layout and the dynamic scroll box
Swc.Load("swc.game")      -- the game buses that the first HUD reads
Swc.Load("swc.host")      -- the calls of the host: Swc.Tick, Swc.Boot, Swc.EnterWorld, the player data
