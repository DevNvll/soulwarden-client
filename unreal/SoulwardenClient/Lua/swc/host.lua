-- The calls of the host (the client, or the offline tests): the frame, the start of the UI, the world entry, and the
-- data of the local player. The host calls them through Runtime::call("Name", arguments).

local Swc = Swc
Swc.time = 0

--- One frame: the spawns of the last frame, the flipbooks, the tweens, then TickBus OnTick(deltaTime, timePoint).
function Swc.Tick(dt)
  Swc.time = Swc.time + dt
  Swc.RunSpawns()
  Swc.TickFlipbooks(dt)
  Swc.TickTweens(dt)
  Swc.Notify("TickBus", nil, "OnTick", dt, Swc.ScriptTimePoint.new({seconds = Swc.time}))
end

--- Sends one event to each handler table of some buses one time (a script that connects to two of them gets it once).
function Swc.NotifyOnce(buses, event, ...)
  local seen = {}
  for _, name in ipairs(buses) do
    for _, h in ipairs(Swc.Handlers(name, nil)) do
      if not seen[h.table] then
        seen[h.table] = true
        local fn = h.table[event]
        if type(fn) == "function" then
          Swc.Call(fn, h.table, ...)
        end
      end
    end
  end
end

--- The start of the UI of the client. lyshineui/shared.luac is the script component of a system entity: its main chunk
--- makes the shared globals (g_cachedScripts, RequireScript), its OnActivate activates the tween wrapper and the data
--- layer, and its PostLoad the item data, the timing helpers, and the data handlers. Then the canvas of the globals
--- (its script starts the UI tick and the screen loader).
Swc.systemEntity = EntityId("6005173470066573313")   -- an id of the runtime ("SWS" in the high bytes)

function Swc.Boot()
  local ok, instance = Swc.Call(Swc.ScriptInstance, Swc.systemEntity, "lyshineui/shared.luac", nil)
  if ok and instance then
    Swc.components[tostring(Swc.systemEntity)] = {instance}
    if type(instance.OnActivate) == "function" then
      Swc.Call(instance.OnActivate, instance)
    end
    if type(instance.PostLoad) == "function" then
      Swc.Call(instance.PostLoad, instance)
    end
  else
    Swc.Note("the shared script did not start")
  end
  Swc.LoadCanvas("LyShineUI/Globals.uicanvas")
end

--- The world entry: the level load and the connection of the player, as the client sends them. The screen loader
--- loads the screens on its tick, then sets the state HUD.
function Swc.EnterWorld(levelName)
  Swc.levelName = levelName or ""
  -- A level load starts the screen state machine again: the loader sets the state HUD when its screens are loaded,
  -- and each screen then gets OnTransitionIn. (The globals script sets HUD at its activation with no screen.)
  Swc.uiState = 0
  local crc = Math.CreateCrc32(Swc.levelName)
  local buses = {"LyShineScriptBindNotificationBus", "CrySystemNotificationsBus"}
  Swc.NotifyOnce(buses, "OnLevelLoad", crc)
  Swc.NotifyOnce(buses, "OnLevelLoadComplete", crc)
  Swc.NotifyOnce(buses, "OnPlayerConnected")
end

--- The spawn of the local player: the client writes the entity ids of its components and the start values.
function Swc.PlayerSpawned()
  local ids = Swc.ids
  Swc.SetData("Hud.LocalPlayer.HudComponent.PlayerEntityId", ids.player)
  Swc.SetData("Hud.LocalPlayer.HudComponent.GDERootEntityId", ids.root)
  Swc.SetData("Hud.LocalPlayer.HudComponent.PaperdollEntityId", ids.paperdoll)
  Swc.SetData("Hud.LocalPlayer.HudComponent.ObjectiveEntityId", ids.objectives)
  Swc.SetData("Hud.LocalPlayer.Vitals.ComponentIsActive", true)
  Swc.SetData("Hud.LocalPlayer.Mounts.IsMounted", false)
  Swc.SetData("Hud.LocalPlayer.CombatStatus.IsInCombat", false)
  Swc.SetData("Hud.LocalPlayer.StatusEffects.IsSilenced", false)
  Swc.SetData("Hud.LocalPlayer.IsInCutscene", false)
  Swc.SetData("Hud.LocalPlayer.Options.Video.HudShowVitalsValues", true)
  Swc.SetData("Hud.LocalPlayer.OnLocalPlayerSet", true)
end

--- One paperdoll slot of the local player (the paperdoll state of the backend; the host gives the item id text).
function Swc.HostEquipment(slot, itemId)
  if itemId == "" then
    itemId = nil
  end
  if Swc.player.equipment[slot] == itemId then
    return
  end
  Swc.player.equipment[slot] = itemId
  Swc.Notify("PaperdollEventBus", Swc.ids.paperdoll, "OnPaperdollSlotUpdate", slot,
             Swc.ItemSlot.new({itemId = itemId, slot = slot}))
end

--- The active weapon slot and its sheathe state (the paperdoll maps of the backend).
function Swc.HostActiveWeapon(slot, sheathed)
  if slot ~= nil and slot < 0 then
    slot = nil
  end
  local changed = Swc.player.activeWeaponSlot ~= slot or Swc.player.sheathed[slot or -1] ~= (sheathed and true or nil)
  Swc.player.activeWeaponSlot = slot
  Swc.player.sheathed = {}
  if slot and sheathed then
    Swc.player.sheathed[slot] = true
  end
  if changed and slot then
    Swc.SetData("Hud.LocalPlayer.Equipment.ActiveWeaponSlot", slot)
  end
end

--- The ability bar of one ability table (the ability state of the backend): the three ability keys (0: empty).
function Swc.HostAbilityBar(tableCrc, a1, a2, a3)
  local bar = {a1 ~= 0 and a1 or nil, a2 ~= 0 and a2 or nil, a3 ~= 0 and a3 or nil}
  local old = Swc.player.bars[tableCrc]
  if old and old[1] == bar[1] and old[2] == bar[2] and old[3] == bar[3] then
    return
  end
  Swc.player.bars[tableCrc] = bar
  Swc.dataBarsChanged = true
end

--- The cooldown of an ability (the cooldown state of the backend): the seconds that are left and the full time.
function Swc.HostCooldown(key, left, total)
  if left <= 0 then
    if Swc.player.cooldowns[key] then
      Swc.player.cooldowns[key] = nil
      Swc.Notify("AbilityCooldownNotificationsBus", nil, "OnAbilityCooldownReset", key)
    end
    return
  end
  local cd = Swc.player.cooldowns[key]
  local ends = Swc.time + left
  if cd and math.abs(cd.ends - ends) < 0.25 then
    return
  end
  Swc.player.cooldowns[key] = {ends = ends, total = total > 0 and total or left}
  Swc.Notify("CharacterAbilityNotificationBus", Swc.ids.root, "OnAbilityTriggered", key, left, false)
end

--- The heading of the camera of the player (degrees, 0 north, positive west) and its position (meters).
function Swc.HostView(yawDegrees, x, y, z)
  Swc.SetData("Hud.LocalPlayer.PlayerHeading", math.rad(yawDegrees))
  if x ~= nil then
    Swc.SetData("Hud.LocalPlayer.Position", Swc.Vector3.new({x = x, y = y, z = z}))
  end
end

--- After a frame of host data: the HUD reads the ability bars again when they changed.
function Swc.HostCommit()
  if Swc.dataBarsChanged then
    Swc.dataBarsChanged = false
    Swc.DataCall("Hud.LocalPlayer.Abilities.OnDataUpdate", true)
  end
end

--- Data of the host for a data layer path (Hud.LocalPlayer...).
function Swc.HostData(path, value)
  Swc.SetData(path, value)
end

-- The local player (the decoded own entity). The host sets the values each frame; a change goes to the data layer.
Swc.player = Swc.player or {}

local function set_if_changed(path, value)
  if Swc.GetData(path) ~= value then
    Swc.SetData(path, value)
  end
end

--- The vitals of the local player (vitals, stamina, and mana states of the backend).
function Swc.HostVitals(health, healthMax, stamina, staminaMax, mana, manaMax, dead)
  local p = Swc.player
  p.health, p.healthMax, p.stamina, p.staminaMax, p.mana, p.manaMax = health, healthMax, stamina, staminaMax, mana, manaMax
  p.dead = dead
  if healthMax and healthMax > 0 then
    set_if_changed("Hud.LocalPlayer.Vitals.HealthMax", healthMax)
    set_if_changed("Hud.LocalPlayer.Vitals.HealthPercent", math.max(0, math.min(1, health / healthMax)))
  end
  if staminaMax and staminaMax > 0 then
    set_if_changed("Hud.LocalPlayer.Vitals.StaminaMax", staminaMax)
    set_if_changed("Hud.LocalPlayer.Vitals.StaminaPercent", math.max(0, math.min(1, stamina / staminaMax)))
  end
  if manaMax and manaMax > 0 then
    set_if_changed("Hud.LocalPlayer.Vitals.ManaMax", manaMax)
    set_if_changed("Hud.LocalPlayer.Vitals.ManaValue", mana)
    set_if_changed("Hud.LocalPlayer.Vitals.ManaPercent", math.max(0, math.min(1, mana / manaMax)))
  end
end

--- The report of a run: the data paths that have a value, and the counts.
function Swc.Report()
  local paths = Swc.DataPaths()
  Swc.Note("data layer: " .. #paths .. " paths with a value")
  local screens = {}
  for k, s in pairs(Swc.screens) do
    screens[#screens + 1] = s.path
  end
  table.sort(screens)
  Swc.Note("screens loaded: " .. table.concat(screens, ", ") .. "; screens of later stages not loaded: " .. Swc.SkippedScreenCount())
end

--- Runs a Lua text of the host (a check of the offline tests: tools/test-ui.sh --lua TEXT).
function Swc.Eval(text)
  local chunk, err = loadstring(text, "=eval")
  if not chunk then
    Swc.Note("eval: " .. tostring(err))
    return
  end
  local results = Swc.pack(Swc.Call(chunk))
  local parts = {}
  for i = 2, results.n do
    parts[#parts + 1] = tostring(results[i])
  end
  Swc.Note("eval: " .. table.concat(parts, " "))
end

--- Runs a fixture of the offline tests (tests/ui/fixtures): the prelude (the helpers), then the fixture.
-- @return "N checks, M failed", then the lines of the failed checks and the notes.
function Swc.RunFixture(name, prelude, text)
  local result = {checks = 0, failed = 0, lines = {}}
  Swc.fixture = result
  for _, part in ipairs({{"prelude", prelude}, {name, text}}) do
    local chunk, err = loadstring(part[2], "=" .. part[1])
    local ok, why = chunk ~= nil, err
    if chunk then
      ok, why = xpcall(chunk, debug.traceback)
    end
    if not ok then
      result.failed = result.failed + 1
      result.lines[#result.lines + 1] = "error: " .. tostring(why)
      break
    end
  end
  local head = string.format("%d checks, %d failed", result.checks, result.failed)
  if #result.lines == 0 then
    return head
  end
  return head .. "\n" .. table.concat(result.lines, "\n")
end
