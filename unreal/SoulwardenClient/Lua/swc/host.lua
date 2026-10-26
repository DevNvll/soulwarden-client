-- The calls of the host (the client, or the offline tests): the frame, the start of the UI, the world entry, and the
-- data of the local player. The host calls them through Runtime::call("Name", arguments).

local Swc = Swc
Swc.time = 0

--- One frame: the spawns of the last frame, the flipbooks, the tweens, then TickBus OnTick(deltaTime, timePoint), then
--- CrySystemEventBus OnCrySystemPostViewSystemUpdate (uitick.luac sends it on as DynamicBus.UITickBus; compass.luac places its icons
--- there: with no such event each compass icon stayed at the north point of the strip; AST-237).
Swc.Handled("CrySystemEventBus")
function Swc.Tick(dt)
  Swc.time = Swc.time + dt
  Swc.RunSpawns()
  Swc.TickFlipbooks(dt)
  Swc.TickTweens(dt)
  Swc.Notify("TickBus", nil, "OnTick", dt, Swc.ScriptTimePoint.new({seconds = Swc.time}))
  Swc.Notify("CrySystemEventBus", nil, "OnCrySystemPostViewSystemUpdate")
  Swc.TickInteractables()
  Swc.TickExitHovers(dt)
  Swc.InputTick()
  if Swc.FrontendTick then
    Swc.FrontendTick(dt)
  end
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
  -- The camera of the new world has no override, no look-at point, and no depth of field (swc/camera.lua).
  if Swc.CameraReset then
    Swc.CameraReset()
  end
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
  Swc.SetData("Hud.LocalPlayer.HudComponent.InventoryEntityId", ids.inventory)
  Swc.SetData("Hud.LocalPlayer.HudComponent.InteractorEntityId", ids.interactor)
  Swc.SetData("Hud.LocalPlayer.HudComponent.ItemRepairEntityId", ids.itemRepair)
  Swc.SetData("Hud.LocalPlayer.HudComponent.CraftingEntityId", ids.crafting)
  Swc.SetData("Hud.LocalPlayer.HudComponent.VitalsEntityId", ids.vitals)
  Swc.SetData("Hud.LocalPlayer.HudComponent.EventNotificationEntityId", ids.eventNotification)
  Swc.SetData("Hud.LocalPlayer.HudComponent.GatheringEntityId", ids.gathering)
  -- The coin only when the host gave it (Swc.HostCoin): lootticker2024.luac takes the first value as the start total, and a 0 before
  -- the wallet of the backend showed the wallet as a gain (user test 2026-10-26, 8c: the row "+5.40" at the world entry).
  if Swc.player.coinKnown then
    Swc.SetData("Hud.LocalPlayer.Currency.Amount", Swc.player.coin or 0)
  end
  Swc.SetData("Hud.LocalPlayer.Vitals.ComponentIsActive", true)
  -- The progression of the player is connected (xpmeter.luac then compares the level with the maximum level: the meter hides at
  -- the maximum level, as in the retail HUD at level 70, set 6).
  Swc.SetData("Hud.LocalPlayer.ProgressionRequestBus.IsConnected", true)
  Swc.SetData("Hud.LocalPlayer.Mounts.IsMounted", false)
  Swc.SetData("Hud.LocalPlayer.CombatStatus.IsInCombat", false)
  Swc.SetData("Hud.LocalPlayer.StatusEffects.IsSilenced", false)
  Swc.SetData("Hud.LocalPlayer.IsInCutscene", false)
  Swc.StartAfflictions()
  Swc.SetData("Hud.LocalPlayer.OnLocalPlayerSet", true)
  -- The marker component of the local player is set: markermanager.luac registers the marker types and the pools (swc/markers.lua).
  Swc.SetData("Hud.Markers.LocalPlayerMarkerSet", true)
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
  -- The sheathe events of the paperdoll (reticlesmanager.luac keeps the drawn slot from them: a drawn bow shows the bow
  -- reticle, a sheathed weapon the default reticle).
  local drawn = (slot and not sheathed) and slot or nil
  if Swc.player.drawnSlot ~= drawn then
    if Swc.player.drawnSlot then
      Swc.Notify("PaperdollEventBus", Swc.ids.paperdoll, "OnSlotSheathed", Swc.player.drawnSlot)
    end
    Swc.player.drawnSlot = drawn
    if drawn then
      Swc.Notify("PaperdollEventBus", Swc.ids.paperdoll, "OnSlotUnsheathed", drawn)
    end
  end
  Swc.UpdateReticle()
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

--- The cooldown of an ability (the cooldown state of the backend): the seconds that are left and the full time (0: the seconds
--- that are left at the start). A new end of a running cooldown (the server cuts the cooldowns of the weapon after some hits)
--- keeps the full time: the fill of the slot (weaponquickslots.luac: GetAbilityCurrentCooldown / GetTotalCooldownTime) then
--- drops, as in the retail take ui-abilities-bow of set 6 (16 to 14 at the critical hit of Rapid Shot, the fill lower).
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
  local running = cd and cd.ends > Swc.time and ends < cd.ends
  Swc.player.cooldowns[key] = {ends = ends, total = total > 0 and total or running and cd.total or left}
  Swc.Notify("CharacterAbilityNotificationBus", Swc.ids.root, "OnAbilityTriggered", key, left, false)
end

--- The heading of the camera of the player (degrees, 0 north, positive west) and its position (meters).
function Swc.HostView(yawDegrees, x, y, z)
  Swc.SetData("Hud.LocalPlayer.PlayerHeading", math.rad(yawDegrees))
  if x ~= nil then
    Swc.SetData("Hud.LocalPlayer.Position", Swc.Vector3.new({x = x, y = y, z = z}))
    -- The territory of the position (swc/worldmap.lua): again after a move of 2 m or more.
    local last = Swc.mapPlace
    if last == nil or math.abs(last[1] - x) + math.abs(last[2] - y) >= 2 then
      Swc.mapPlace = {x, y}
      Swc.MapPosition(x, y, z)
    end
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
  local wasDead = p.dead
  p.dead = dead
  -- The dead mark of the vitals state (respawn.luac and giveup.luac observe it); the end of a death is a revive.
  set_if_changed("Hud.LocalPlayer.Vitals.IsDead", dead and true or false)
  if wasDead and not dead then
    Swc.SetData("Hud.LocalPlayer.OnRevived", true)
  end
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

-- ---------------------------------------------------------------------------------------- the loading and error UI

-- The loading screen (LoadScreenBus of the fork): globals.luac registers lyshineui/hud/loadingscreen; the native code
-- shows it while a level loads and sends LoadScreenNotificationBus events to its script. Here the host shows it from
-- its start to the spawn of the player (Swc.HostLoading), with the same events: SetLoadingScreenType,
-- OnLoadingLevelDetermined, OnCanvasEnabledChanged(true), OnLoadingScreenShown; at the end OnCanvasEnabledChanged(false)
-- and OnLoadingScreenDismissed (loadingscreen.luac, respawn.luac). A respawn has the screen type eLoadingScreenIds_DEATH:
-- the client shows it from its respawn request to message 3954.
Swc.Handled("LoadScreenNotificationBus")
Swc.loading = {path = nil, canvas = nil, shown = false, tipCounts = {}}

-- The show counts of the loading screen tips (loadingscreenmanager.luac: UpdateTipPoolPriorityLevels reads the count of a tip with the
-- native data call 355238560 and its saveSettingsId, -1 for none; the call 1214891646 saves a tip id and its count). With no answer the
-- node kept the argument, and the compare with -1 was a Lua error at each travel (AST-280). The retail client keeps them in its saved
-- settings; this client keeps them for the session [I].
Swc.callValues[355238560] = function(id)
  return Swc.loading.tipCounts[tostring(id)] or -1
end
Swc.callValues[1214891646] = function(id, count)
  if id ~= nil and tonumber(count) then
    Swc.loading.tipCounts[tostring(id)] = tonumber(count)
  end
  return count
end
local LOADING_SCREEN_DRAW_ORDER = 1000   -- canvascommon.luac

Swc.Define("LoadScreenBus", "Broadcast", {
  RegisterLoadingScreen = function(path) Swc.loading.path = path end,
  IsLoadingScreenShown = function() return Swc.loading.shown end,
  GetCurrentLevelName = function() return Swc.levelName or "" end,
  SetLoadingBarFull = function() end,
})

function Swc.HostLoading(on, levelName, screenType)
  local l = Swc.loading
  if type(screenType) == "string" then
    screenType = rawget(_G, screenType) or Swc.clientEnums and Swc.clientEnums[screenType]
  end
  if on then
    if l.shown or not l.path then
      return
    end
    Swc.levelName = levelName or Swc.levelName
    if not (l.canvas and l.canvas:IsValid() and swcui.canvas_get(l.canvas, "root")) then
      l.canvas = Swc.LoadCanvas(l.path, LOADING_SCREEN_DRAW_ORDER)
    end
    if not (l.canvas and l.canvas:IsValid()) then
      return
    end
    swcui.canvas_set(l.canvas, "enabled", true)
    l.shown = true
    Swc.Notify("LoadScreenNotificationBus", nil, "SetLoadingScreenType", screenType or eLoadingScreenIds_LEVEL1)
    Swc.Notify("LoadScreenNotificationBus", nil, "OnLoadingLevelDetermined", Swc.levelName or "")
    Swc.Notify("LoadScreenNotificationBus", nil, "OnCanvasEnabledChanged", true)
    Swc.Notify("LoadScreenNotificationBus", nil, "OnLoadingScreenShown")
    Swc.Note("the loading screen shows (" .. tostring(Swc.levelName) .. ")")
  elseif l.shown then
    l.shown = false
    Swc.Notify("LoadScreenNotificationBus", nil, "OnCanvasEnabledChanged", false)
    Swc.Notify("LoadScreenNotificationBus", nil, "OnLoadingScreenDismissed")
    if l.canvas then
      swcui.canvas_set(l.canvas, "enabled", false)
    end
    Swc.Note("the loading screen hides")
  end
end

--- The start of a travel (the loading screen of a travel in the world; stage MVP-C): the screens of the old place close (the
--- bag, the loot panel, the map, a menu), and the HUD state is current. A death needs no rule: the state Respawn of the
--- state table closes the lower and the higher states. The interactor ends a conversation at message 3746 of the server.
function Swc.HostTravelStarts()
  local hud = 2702338936
  for _ = 1, 8 do
    local state = LyShineManagerBus.Broadcast.GetCurrentState()
    if state == hud or state == 0 or state == nil then
      return
    end
    LyShineManagerBus.Broadcast.ExitState(state)
  end
end

-- A death notice of the server (message 3950): the time to the respawn screen in seconds. The data callback
-- Hud.LocalPlayer.OnPlayerDeath gives it to respawn.luac (with Hud.LocalPlayer.Vitals.IsDead it queues the state
-- "respawn").
function Swc.HostDeathNotice(seconds)
  Swc.SetData("Hud.LocalPlayer.OnPlayerDeath", seconds)
end

-- The level of the character (the Progression state: the level of the screen, the wire level + 1; xpdetails.luac compares
-- it with GetMaxLevel() + 1) and its experience. The item cells compare the level with the level of an item.
function Swc.HostProgression(level, experience)
  if Swc.GetData("Hud.LocalPlayer.Progression.Level") ~= level then
    Swc.SetData("Hud.LocalPlayer.Progression.Level", level)
  end
  Swc.player.experience = experience
  -- The start of this level and of the next one (the level table of the client, gamedata/xplevels.json: the row of the level of the
  -- wire, the level of the screen minus 1; protocol notes 4.12): the XP bar of the progression line of the loot ticker.
  Swc.xpLevels = Swc.xpLevels or swcui.load_json("gamedata/xplevels.json") or {}
  local row = math.max(0, (level or 1) - 1)
  local start = Swc.xpLevels[row + 1] or 0
  local nextStart = Swc.xpLevels[row + 2] or start
  if Swc.GetData("Hud.LocalPlayer.Progression.PrevRequiredGlory") ~= start then
    Swc.SetData("Hud.LocalPlayer.Progression.PrevRequiredGlory", start)
  end
  if Swc.GetData("Hud.LocalPlayer.Progression.RequiredGlory") ~= nextStart then
    Swc.SetData("Hud.LocalPlayer.Progression.RequiredGlory", nextStart)
  end
  -- The total of the experience (the native Glory node: lootticker2024.luac OnXpChanged shows "+N XP", the new total less the old).
  if Swc.GetData("Hud.LocalPlayer.Progression.Glory") ~= experience then
    Swc.SetData("Hud.LocalPlayer.Progression.Glory", experience)
  end
end

-- The requests of the scripts to the client (the C++ takes them each frame: Swc.TakeHostRequest).
Swc.hostRequests = {}
Swc.Define("LocalPlayerUIRequestsBus", "Broadcast", {
  -- respawn.luac TriggerRespawn: random is true for "Random Location" (no home id).
  RequestRespawn = function(random, homeId)
    Swc.hostRequests[#Swc.hostRequests + 1] = "respawn " .. (random and "1" or "0") .. " " .. tostring(homeId or "")
    Swc.Note("respawn request: random " .. tostring(random) .. ", home point " .. tostring(homeId))
  end,
  RequestSpectate = function() end,
})

--- @return The next request of the scripts ("respawn RANDOM HOMEID"), or "".
function Swc.TakeHostRequest()
  return table.remove(Swc.hostRequests, 1) or ""
end

-- The error UI of the login path: the popup screen (popup.luac ShowError) with the error of the table of the game
-- (uierrors.luac): a refused or failed connection is UIError_login_unable_to_connect ("@mm_connection_error_title",
-- "@mm_loginservices_UnableToConnectError"). The popup loads here when the loader did not load it (no world).
function Swc.HostConnectionError()
  Swc.HostLoading(false)
  local popup
  for _, screen in pairs(Swc.screens) do
    if string.lower(screen.name) == "popup" then
      popup = screen.canvas
    end
  end
  if not popup then
    popup = Swc.LoadScreen("LyShineUI/Popup/Popup.uicanvas", true, 52)   -- canvascommon.luac POPUP_DRAW_ORDER
  end
  UiPopupBus.Broadcast.ShowError(UIError_login_unable_to_connect, nil, nil, "Swc_ConnectionError")
  Swc.Note("the error UI shows: the connection failed")
end
