-- The in-game scenes, the subtitles, the whispers, and the cinematic video stand-in (stage Q5, Linear AST-240; docs/QUESTS.md "The scenes,
-- the cinematics, and the whispers"; research/quest-scenes.md). The data: gamedata/scenes.json (tools/convert-scenes.sh write).
--
-- The host (Scenes/SwcScenes.cpp) plays the scene and owns the handshake; it sends lines to Swc.HostScene:
--   start NAME k=v...   the screen state PlayInGameCinematic (hideUIOnTrigger), the cutscene screen (cutscenescreen.luac) gets
--                       PlayerCutsceneComponentNotificationsBus OnCutsceneStarted(info, false) (the skip listener when canSkip)
--   end NAME REASON     OnCutsceneEnded(info, false), then the HUD state (or the video of a cinematic that the scene triggered)
--   subtitle @KEY S     a CommandClip PlayWhisperSubtitle_@KEY of the scene timeline: the subtitle as whispers.luac
--                       OnWhisperStartedSubtitleText (type Subtitle, VeryHigh, the speaker as title, maximumDuration = the clip duration,
--                       inGameCutscene for VO_Type igc_ or IGC_Subtitles, else midScreen)
--   cinematic ID        the trigger of a cinematic entity (the statue lead-in at 9.7 s): the video screen after the scene [I: the native
--                       cinematic component defers its UI request while an in-world cutscene is current, 0x2CC71F0]
-- Swc.HostWhisper(CRC): a whisper 5070 (research/quest-scenes.md 8): the command clips of its whisper timeline on their own clock (no
-- audio); the silent VO token ends after the later of the row Duration and the last command end + 0.25 s (the notification fade out).
--
-- The cinematic video stand-in (declared; research/quest-scenes.md 9; no video decoder and no audio in this client): the original
-- videoplayerscreen.luac runs on its state PlayCinematicVideo; BinkRequestBus is a clock of the length of the video (the Bink header of
-- the converter): PlayFromSource starts it (OnPlaybackStarted: the screen sends OnCinematicPlaybackStarted -> 6606), its end gives
-- OnPlaybackFinished (-> 6542), CloseVideoFile (the skip: a hold of ui_cancel 2.25 s) gives OnPlaybackStopped (-> 6542). The video image
-- is black (not the white image of a failed Bink start). The subtitles come from the screen (cinematicsubtitles start, end, next).

local Swc = Swc

local S = {data = nil, info = nil, whispers = {}, video = nil, pendingVideo = nil, log = {}}
Swc.scenes = S

local STATE_SCENE = 1223111804   -- PlayInGameCinematic (uistates: CutsceneScreen)
local STATE_VIDEO = 1526961689   -- PlayCinematicVideo (uistates: VideoPlayerScreen)
local STATE_HUD = 2702338936     -- the HUD state (videoplayerscreen.luac OnClose)
local CUTSCENE_PATH = "LyShineUI/Cutscene/CutsceneScreen"
local VIDEO_PATH = "LyShineUI/Videos/VideoPlayerScreen"
local PREFIX = "PlayWhisperSubtitle_"

local function data()
  if S.data == nil then
    S.data = swcui.load_json("gamedata/scenes.json") or {cinematics = {}, whispers = {}, attributes = {}}
  end
  return S.data
end

local function note(text)
  S.log[#S.log + 1] = text
  Swc.Note("scenes: " .. text)
end

local function attribute(key, name)
  local entry = data().attributes[string.lower(tostring(key or "")):gsub("^@", "")]
  local value = entry and entry[name] or nil
  -- The last subtitle of a chain has no dialogue-next: videoplayerscreen.luac ProcessSubtitleText builds "@" .. the value and ends
  -- the chain at "@" (its own check), so the native call gives "" there (a nil gave "attempt to concatenate a nil value" 8 s before
  -- the end of TempestRising, AST-244 item 7). [I] The other attributes stay nil when they are missing: whispers.luac tests the
  -- speaker and VO_Type for truth.
  if value == nil and name == "dialogue-next" then
    return ""
  end
  return value
end

-- The localization attributes of the keys of the scenes, the whispers, and the cinematic subtitles (speaker, VO_Type, start, end,
-- dialogue-next).
Swc.Define("LyShineScriptBindRequestBus", "Broadcast", {
  GetAttributeValueForKey = function(key, name) return attribute(key, name) end,
})

-- The subtitle option: the notification manager drops Subtitle and CinematicSubtitle when it is off (notifications.luac
-- EnqueueNotification). [I] The silent client shows subtitles by default; a saved value stays.
if Swc.GetData("Hud.LocalPlayer.Options.Accessibility.Subtitles") == nil then
  Swc.SetData("Hud.LocalPlayer.Options.Accessibility.Subtitles", true)
end

--- The subtitle of whispers.luac OnWhisperStartedSubtitleText (the text key with "@", the duration of the command clip).
local function subtitle(key, duration)
  local n = NotificationData()
  n.type = "Subtitle"
  n.text = key
  if ConfigProviderEventBus.Broadcast.GetBool("UIFeatures.enable-subtitles-v2") then
    local speaker = attribute(key, "speaker")
    if speaker and speaker ~= "UI" and speaker ~= "IU" and speaker ~= "Interfejs" then
      n.title = speaker
    end
  end
  n.priority = eNotificationPriority_VeryHigh
  n.callbackName = "OnWhisperNotificationComplete"
  n.maximumDuration = duration or 5
  local voType = attribute(key, "VO_Type")
  local extra = (voType == "IGC_Subtitles" or voType == "igc_") and {inGameCutscene = true} or {midScreen = true}
  DynamicBus.NotificationsRequestBus.Broadcast.EnqueueNotification(n, extra)
  note(string.format("subtitle %s for %.2f s (%s)", tostring(key), n.maximumDuration, extra.inGameCutscene and "in-game cutscene" or "mid screen"))
end
S.subtitle = subtitle

local function screen_loaded(path, busName)
  local bus = DynamicBus[busName]
  if bus and #bus.handlers > 0 then
    return
  end
  local common = RequireScript("LyShineUI._Common.CanvasCommon")
  Swc.LoadScreen(path, true, common and common.FULLSCREEN_DRAW_ORDER or 0)
end

local function root_id()
  return Swc.GetData("Hud.LocalPlayer.HudComponent.GDERootEntityId")
end

-- ------------------------------------------------------------------------------------------------------------------ the video stand-in

local function open_video(id)
  local c = data().cinematics[string.lower(tostring(id))]
  if c == nil then
    note("no cinematic " .. tostring(id) .. " in gamedata/scenes.json")
    return
  end
  local seconds = (c.video and c.video.seconds) or c.durationInSec or 0
  S.video = {id = c.id, crc = c.crc, entry = tonumber(c.crc, 16), seconds = seconds, time = 0, playing = false, started = false}
  Swc.SetData("Hud.CinematicVideo.EntryId", S.video.entry)
  Swc.SetData("Hud.CinematicVideo.SourceSliceName", S.sourceSlice or "")
  screen_loaded(VIDEO_PATH, "VideoPlayerScreen")
  note(string.format("the video stand-in of %s (%s): %.2f s, skip %s", c.id, c.crc, seconds, tostring(c.canSkip)))
  Swc.SetUiState(STATE_VIDEO)
end
S.open_video = open_video

Swc.Define("CinematicVideoDataManagerBus", "Broadcast", {
  GetCinematicVideoStaticData = function(entry)
    for _, c in pairs(data().cinematics) do
      if tonumber(c.crc, 16) == entry then
        return {videoName = c.videoName, videoPath = c.videoPath, canSkip = c.canSkip ~= false, subtitle = c.subtitle,
                volumeScalingFactor = c.volumeScalingFactor or 1, durationInSec = c.durationInSec}
      end
    end
    return {videoName = "", videoPath = "", canSkip = true, subtitle = "", volumeScalingFactor = 1}
  end,
})

Swc.Define("BinkRequestBus", "Broadcast", {
  PlayFromSource = function(path)
    if S.video then
      S.video.playing, S.video.time, S.video.startPending = true, 0, true
      note("the video clock starts (" .. tostring(path) .. ", no decoder)")
    end
  end,
  IsPlaying = function() return S.video ~= nil and S.video.playing == true end,
  CloseVideoFile = function()
    if S.video and S.video.playing then
      S.video.playing = false
      note(string.format("the video closes at %.2f s (a skip)", S.video.time))
      Swc.Notify("BinkEventBus", nil, "OnPlaybackStopped")
    end
  end,
  SetUseRenderTarget = function() end,
  SetVideoVolumeScalingFactor = function() end,
})

-- The image of the video: black in the stand-in (the image has no render target in this client).
Swc.Define("UiImageBus", "Event", {
  SetRenderTargetName = function(id)
    UiImageBus.Event.SetColor(id, Color(0, 0, 0, 1))
  end,
})

-- ------------------------------------------------------------------------------------------------------------------ the cutscene screen

Swc.Define("PlayerCutsceneComponentRequestBus", "Broadcast", {
  RequestStopCurrentCutscene = function()
    note("the cutscene screen asks for the stop (a skip)")
    Swc.hostRequests[#Swc.hostRequests + 1] = "scene stop Skipped"
  end,
  GetCurrentCutsceneInfo = function()
    return S.info or {IsValid = function() return false end}
  end,
  ApplyOrReleaseCameraOverrides = function() end,
  CutsceneFadeEffectCompleted = function() end,
  OnCutsceneScreenReadyToExit = function() end,
  OnCinematicPlaybackStarted = function(entry)
    note("the video screen reports the start of " .. tostring(S.video and S.video.id))
    Swc.hostRequests[#Swc.hostRequests + 1] = "cinematic start " .. string.format("%08x", entry or 0)
  end,
  OnCinematicPlaybackFinished = function(entry)
    note("the video screen reports the end of " .. tostring(S.video and S.video.id))
    Swc.hostRequests[#Swc.hostRequests + 1] = "cinematic end " .. string.format("%08x", entry or 0)
  end,
  OnCinematicPlaybackStopped = function(entry)
    note("the video screen reports the stop (skip) of " .. tostring(S.video and S.video.id))
    Swc.hostRequests[#Swc.hostRequests + 1] = "cinematic end " .. string.format("%08x", entry or 0)
  end,
})

local function info_of(fields)
  local info = {IsValid = function() return true end, dummyIsArmed = false}
  for k, v in pairs(fields) do
    if v == "1" or v == "0" then
      info[k] = v == "1"
    elseif tonumber(v) then
      info[k] = tonumber(v)
    else
      info[k] = v
    end
  end
  return info
end

function Swc.HostScene(line)
  local parts = {}
  for part in string.gmatch(line or "", "[^\t]+") do
    parts[#parts + 1] = part
  end
  local kind = parts[1]
  if kind == "start" then
    local fields = {}
    for i = 3, #parts do
      local k, v = parts[i]:match("^([^=]+)=(.*)$")
      if k then
        fields[k] = v
      end
    end
    S.info = info_of(fields)
    S.name = parts[2]
    S.sourceSlice = parts[2]
    screen_loaded(CUTSCENE_PATH, "CutsceneScreen")
    if S.info.hideUIOnTrigger then
      Swc.SetUiState(STATE_SCENE)
    end
    Swc.SetData("Hud.LocalPlayer.IsInCutscene", true)
    Swc.Notify("PlayerCutsceneComponentNotificationsBus", root_id(), "OnCutsceneStarted", S.info, false)
    note("start " .. tostring(parts[2]) .. (S.info.canSkip and " (skip allowed)" or ""))
  elseif kind == "end" then
    note("end " .. tostring(parts[2]) .. " (" .. tostring(parts[3]) .. ")")
    local info = S.info or info_of({})
    S.info = nil
    Swc.SetData("Hud.LocalPlayer.IsInCutscene", false)
    Swc.Notify("PlayerCutsceneComponentNotificationsBus", root_id(), "OnCutsceneEnded", info, false)
    if S.pendingVideo then
      local id = S.pendingVideo
      S.pendingVideo = nil
      open_video(id)
    else
      Swc.SetUiState(STATE_HUD)
    end
  elseif kind == "subtitle" then
    subtitle(parts[2], tonumber(parts[3]))
  elseif kind == "cinematic" then
    if S.info then
      S.pendingVideo = parts[2]
      note("the cinematic " .. tostring(parts[2]) .. " waits for the end of the scene")
    else
      open_video(parts[2])
    end
  end
end

-- ------------------------------------------------------------------------------------------------------------------ the whispers

function Swc.HostWhisper(crc)
  local w = data().whispers[string.lower(tostring(crc))]
  if w == nil then
    note("a whisper " .. tostring(crc) .. " with no row in gamedata/scenes.json")
    return
  end
  -- A new whisper replaces the active one (the native start stops the old timeline; no FIFO is proved).
  local last = 0
  for _, c in ipairs(w.commands or {}) do
    last = math.max(last, c["end"] or 0)
  end
  S.whisper = {id = w.id, time = 0, commands = w.commands or {}, next = 1, vo = math.max(w.duration or 0, last + 0.25), voDone = false}
  note(string.format("whisper %s (%s): %d subtitle command(s), the silent VO token ends at %.2f s", tostring(w.id), tostring(crc),
    #S.whisper.commands, S.whisper.vo))
  if w.subtitle and w.subtitle ~= "" then
    subtitle(w.subtitle, nil)   -- the direct Subtitle column: the duration of Properties.duration (5)
  end
end

Swc.Connect("TickBus", {OnTick = function()
  local now = Swc.time or 0
  local dt = math.max(0, now - (S.lastTime or now))
  S.lastTime = now
  local w = S.whisper
  if w then
    w.time = w.time + dt
    while w.commands[w.next] and w.commands[w.next].start <= w.time do
      local c = w.commands[w.next]
      w.next = w.next + 1
      local text = tostring(c.text or "")
      if (c.commandId or 0) == 0 and text:sub(1, #PREFIX) == PREFIX and text:sub(#PREFIX + 1, #PREFIX + 1) == "@" then
        subtitle(text:sub(#PREFIX + 1), (c["end"] or c.start) - c.start)
      end
    end
    if not w.voDone and w.time >= w.vo then
      w.voDone = true
      note("whisper " .. tostring(w.id) .. ": the silent VO token ends")
      S.whisper = nil
    end
  end
  local v = S.video
  if v and v.playing then
    if v.startPending then
      v.startPending = false
      Swc.Notify("BinkEventBus", nil, "OnPlaybackStarted")
    end
    v.time = v.time + dt
    if v.time >= v.seconds then
      v.playing = false
      note(string.format("the video clock ends at %.2f s", v.time))
      Swc.Notify("BinkEventBus", nil, "OnPlaybackFinished")
    end
  end
end}, nil)
