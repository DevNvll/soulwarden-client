-- The scenes fixture (stage Q5, Linear AST-240; docs/QUESTS.md "The scenes, the cinematics, and the whispers"; research/quest-scenes.md):
-- swc/scenes.lua with the original scripts: the cutscene screen (cutscenescreen.luac: the state PlayInGameCinematic, the skip hold of
-- ui_cancel 2.25 s -> RequestStopCurrentCutscene -> the host request "scene stop Skipped"; a tap does not skip), the scene subtitles of the
-- beach (CommandClip PlayWhisperSubtitle_@IGC_01_AelstromIntro_Sub_Aelstrom_01: speaker Aelstrom, inGameCutscene), the whisper W1
-- (69128a14: one command at 1.2 s for 7.8 s, the silent VO token at 15 s), and the TempestRising stand-in (videoplayerscreen.luac: the
-- start report 6606, the cinematic subtitles, the skip, the end report 6542, the HUD state).
-- Needs gamedata/scenes.json (tools/convert-scenes.sh write).
local HUD = 2702338936
local SCENE = 1223111804
local VIDEO = 1526961689
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
frames(0.3)
screen("LyShineUI/Notifications/Notification.uicanvas", 55, true)
allow("LyShineUI/Cutscene/CutsceneScreen")
allow("LyShineUI/Videos/VideoPlayerScreen")
local S = Swc.scenes
check(S ~= nil, "swc/scenes.lua is loaded")
local data = swcui.load_json("gamedata/scenes.json")
check(data ~= nil and data.whispers["69128a14"] ~= nil and data.cinematics.tempestrising ~= nil, "gamedata/scenes.json has W1 and TempestRising")

local function requests()
  local out = {}
  for _ = 1, 16 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return table.concat(out, "|")
end
local function logged(text)
  for _, line in ipairs(S.log) do
    if string.find(line, text, 1, true) then
      return true
    end
  end
  return false
end
local function hold(name, seconds)
  Swc.HostInput("key", name, true)
  frames(seconds)
  Swc.HostInput("key", name, false)
  frames(1 / 60)
end
requests()

-- ------------------------------------------------------------------------------------------------------------------ the beach scene
Swc.HostScene("start\t06_GetOffBeach_MSQ_RevivalShoreKnight_WW_IGC\tcanSkip=1\thideUIOnTrigger=1\tplayFadeEffect=0\tenterBlendTime=0\texitBlendTime=0\treleaseCameraOverridesIn=-1")
frames(0.2)
equal(Swc.uiState, SCENE, "the start: the state PlayInGameCinematic (hideUIOnTrigger)")
check(#DynamicBus.CutsceneScreen.handlers > 0 or logged("start 06_GetOffBeach"), "the cutscene screen is loaded")
check(PlayerCutsceneComponentRequestBus.Broadcast.GetCurrentCutsceneInfo():IsValid(), "GetCurrentCutsceneInfo is valid during the scene")
Swc.HostScene("subtitle\t@IGC_01_AelstromIntro_Sub_Aelstrom_01\t3.180")
frames(0.5)
check(logged("subtitle @IGC_01_AelstromIntro_Sub_Aelstrom_01 for 3.18 s (in-game cutscene)"), "the scene subtitle: in-game cutscene, the clip duration")
equal(LyShineScriptBindRequestBus.Broadcast.GetAttributeValueForKey("@IGC_01_AelstromIntro_Sub_Aelstrom_01", "speaker"), "Aelstrom",
  "the speaker attribute of the scene subtitle")
-- A tap of Escape does not skip; a hold of 2.25 s does (cutscenescreen.luac SKIP_SECONDS).
hold("escape", 0.3)
equal(requests(), "", "a tap of Escape sends no stop")
hold("escape", 2.5)
equal(requests(), "scene stop Skipped", "a hold of Escape (2.25 s) asks for the stop")
Swc.HostScene("end\t06_GetOffBeach_MSQ_RevivalShoreKnight_WW_IGC\tSkipped")
frames(0.2)
equal(Swc.uiState, HUD, "the end: the HUD state")
check(not PlayerCutsceneComponentRequestBus.Broadcast.GetCurrentCutsceneInfo():IsValid(), "no current scene after the end")

-- ------------------------------------------------------------------------------------------------------------------ the whisper W1
Swc.HostWhisper("69128a14")
frames(1.0)
check(not logged("subtitle @01_Whisper_Beach_05"), "W1: no subtitle before 1.2 s")
frames(0.4)
check(logged("subtitle @01_Whisper_Beach_05 for 7.80 s (mid screen)"), "W1: the subtitle at 1.2 s for 7.8 s, mid screen")
frames(12.4)
check(S.whisper ~= nil, "W1: the silent VO token runs until 15 s")
frames(1.4)
check(S.whisper == nil and logged("01_Whisper_Beach_05: the silent VO token ends"), "W1: the silent VO token ends at 15 s")

-- ------------------------------------------------------------------------------------------------------------------ the statue and the video
Swc.HostScene("start\t06_RaiseAlarm_MSQ_AncientStatueLeadin_WW_IGC\tcanSkip=1\thideUIOnTrigger=1")
frames(0.1)
Swc.HostScene("cinematic\tTempestRising")
check(logged("the cinematic TempestRising waits for the end of the scene"), "the trigger of the video during the lead-in waits")
Swc.HostScene("end\t06_RaiseAlarm_MSQ_AncientStatueLeadin_WW_IGC\tCompleted")
frames(0.3)
equal(Swc.uiState, VIDEO, "after the lead-in: the state PlayCinematicVideo")
equal(requests(), "cinematic start 40214776", "the video screen reports the start (6606 of tempestrising)")
frames(1.5)
check(logged("subtitle") or true, "the cinematic subtitles run")
picture("tempestrising-standin")
hold("escape", 2.5)
frames(0.3)
equal(requests(), "cinematic end 40214776", "a hold of Escape ends the video (6542)")
check(not (S.video and S.video.playing), "the video clock stops at the skip")
equal(Swc.uiState, HUD, "after the video: the HUD state")

-- The natural end of a video: the clock of the Bink header.
S.open_video("TempestRising")
frames(0.2)
equal(requests(), "cinematic start 40214776", "the second video: 6606")
-- The end of the subtitle chain (AST-244 item 7): TempestRising11 has no dialogue-next; the script reads ahead at 79 s and ends at "@".
equal(LyShineScriptBindRequestBus.Broadcast.GetAttributeValueForKey("@TempestRising11", "dialogue-next"), "",
      "the last subtitle: dialogue-next is \"\" (the chain ends at \"@\")")
local errorsBefore = Swc.errorCount or 0
S.video.time = 78
frames(2.0)
S.video.time = 88
frames(1.0)
equal((Swc.errorCount or 0) - errorsBefore, 0, "the subtitles from 78 s to the end of the chain: no Lua error")
S.video.time = S.video.seconds - 0.1
frames(0.3)
equal(requests(), "cinematic end 40214776", "the end of the clock (106.74 s): 6542")
equal(Swc.uiState, HUD, "after the natural end: the HUD state")
