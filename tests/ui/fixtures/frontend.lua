-- The front end fixture (stage F2 of docs/FRONTEND.md): the original landing screen (LandingScreenV2, landingscreenv2.luac)
-- with the character list of the host (swc/frontend.lua). The names and ids are made up.
--   * No list request before the account is ready; the end of the UI load is not the account event (FE9).
--   * No rows before the three ready flags (the login information, the regions, the world CMS data), in each completion order.
--   * Five characters and an empty slot (the row template clones past the four rows of the canvas); the level of a row is the
--     document value + 1; the default selection is the last played character.
--   * A click on a row: the selected id, the radio state, the preview hides and shows again (the world of the row), the world.
--   * A refresh with a new order keeps the selected character by its id (FE10).
--   * No character: no preview, no login. No world.
--   * Options and back, the exit state and back, Enter (frontEndContinue): Play gives one login request of the selected id.
-- Pictures: frontend-list, frontend-select, frontend-empty.
local LANDING, OPTIONS, MAIN_ESC = 3881446394, 2717041095, 2907552185
-- The wall clock of the emulation follows the time of the fixture (the frames are faster than the real time): the landing
-- script shows the error popup only when more than 5 s passed since its start or its last error popup.
local wallBase = os.time()
os.time = function() return wallBase + math.floor(Swc.time) end
local F = Swc.frontend
F.menu = true
local landing = screen("LyShineUI/Main Menu/LandingScreenV2.uicanvas", 5)
screen("LyShineUI/Options/Options.uicanvas", 30)
screen("LyShineUI/EscapeMenu/EscapeMenu.uicanvas", 41)
screen("LyShineUI/Popup/Popup.uicanvas", 60)
Swc.HostMenuRegion("soulwarden-local", "@mm_us_east", "us-east-1", 1)
local root = swcui.canvas_get(landing, "root")
local L = script_of(find(root, "LandingMaster"), "landingscreenv2.luac")
check(L ~= nil, "the landing script")

local function requests(prefix)
  local out = {}
  for _, r in ipairs(Swc.hostRequests) do
    if string.sub(r, 1, #prefix) == prefix then
      out[#out + 1] = r
    end
  end
  return out
end

local WORLD = "00000000-0000-4000-8000-0000000000aa"
--- Sends a list result: characters {id, name, currentLevel, elapsed}.
local function answer(characters, worlds, maxCharacters)
  Swc.HostMenuListBegin()
  for _, w in ipairs(worlds or {WORLD}) do
    Swc.HostMenuWorld(w, "Made World", "Made World", "set1", "ACTIVE", "OpenWorld", "1", 0, 6, 0, 1, "", 0, 0, "low")
  end
  for _, c in ipairs(characters) do
    -- c[5]: the first-time experience is not complete (FTUE); c[6]: the character must be renamed.
    Swc.HostMenuCharacter(c[1], "persona-x", WORLD, c[2], c[3], 0, 1, 2, 3, c[4], c[5] and 0 or 1, c[6] and 1 or 0, 0, 0, 0)
  end
  Swc.HostMenuListEnd(maxCharacters or 6)
end

local five = {{"c-1", "Alpha Made", 69, 300}, {"c-2", "Beta Made", 4, 50}, {"c-3", "Gamma Made", 0, 900},
  {"c-4", "Delta Made", 29, 0}, {"c-5", "Epsilon Made", 59, 700}}

-- The account is not ready: the end of the UI load does not start the list (FE9).
Swc.HostMenuAccount(false, false)
LyShineManagerBus.Broadcast.SetState(LANDING)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
UILoadingNotificationBus = UILoadingNotificationBus
Swc.Notify("UILoadingNotificationBus", nil, "OnUILoadingComplete")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), LANDING, "the state LandingScreen")
check(swcui.canvas_get(landing, "enabled"), "the landing screen shows")
equal(#requests("menu list"), 0, "no list request before the account is ready (the end of the UI load is not the account event)")
check(not L.authSuccess, "the landing script has no auth success")

-- The camera sequence of the fresh landing (stage F4): the transition to the character select panel plays IntroToCharacter
-- (landingscreenv2.luac: CINEMATIC_LANDING_TO_CHARACTER) and queues it for the end of the menu level load; the host plays its
-- camera tracks ("menu sequence NAME"), and the end event comes after its time range (1 s).
equal(requests("menu sequence")[1], "menu sequence IntroToCharacter", "the transition plays the camera sequence IntroToCharacter")
check(L.queueIntroCinematic == true, "the sequence is queued for the end of the menu level load")
Swc.hostRequests = {}
Swc.HostMenuLevelLoaded()
frames(0.1)
equal(requests("menu sequence")[1], "menu sequence IntroToCharacter", "the end of the level load plays the queued sequence")
check(F.sequences.IntroToCharacter ~= nil, "the sequence runs")
frames(1.0)
check(F.sequences.IntroToCharacter == nil and L.isIntroCinematicCompleted == true, "the sequence ends after its time range (1 s)")

-- The account is ready: the list request. Order 1: the list, then the CMS data, then the regions.
F.hold.cms, F.hold.regions = true, true
Swc.HostMenuAccount(true, true)
frames(0.2)
check(#requests("menu list") >= 1, "the account gives the list request")
Swc.hostRequests = {}
answer(five)
frames(0.2)
check(L.characterDataReady and not L.worldListReady and not L.regionDataReady, "order 1: the list only")
equal(#L.currentCharacterData, 0, "order 1: no rows with one flag")
Swc.FrontendRelease("cms")
frames(0.1)
check(L.worldListReady and not L.regionDataReady, "order 1: the CMS data")
equal(#L.currentCharacterData, 0, "order 1: no rows with two flags")
Swc.FrontendRelease("regions")
frames(0.1)
equal(#L.currentCharacterData, 5, "order 1: the rows after the third flag")

-- Order 2: the regions, then the list, then the CMS data (a refresh resets the three flags).
F.hold.cms, F.hold.regions = true, true
L:RefreshWorldList()
frames(0.1)
check(not L.characterDataReady and not L.worldListReady and not L.regionDataReady, "a refresh resets the three flags")
Swc.FrontendRelease("regions")
frames(0.1)
answer(five)
frames(0.1)
check(L.regionDataReady and L.characterDataReady and not L.worldListReady, "order 2: the regions and the list")
Swc.FrontendRelease("cms")
frames(0.1)
check(L.worldListReady, "order 2: the CMS data last")
equal(#L.currentCharacterData, 5, "order 2: the rows")

-- Order 3: the list, then the regions, then the CMS data.
F.hold.cms, F.hold.regions = true, true
L:RefreshWorldList()
frames(0.1)
answer(five)
frames(0.1)
Swc.FrontendRelease("regions")
frames(0.1)
check(L.characterDataReady and L.regionDataReady and not L.worldListReady, "order 3: the list and the regions")
Swc.FrontendRelease("cms")
frames(0.5)
check(L.worldListReady and #L.currentCharacterData == 5, "order 3: the rows")

-- The rows: five characters and an empty slot (6 slots), the levels of the screen, the default selection.
local list = L.Properties.CharacterList
local rows = 0
local names = {}
for _, child in ipairs(swcui.children(list)) do
  if swcui.enabled_in_hierarchy(child) then
    rows = rows + 1
  end
end
check(rows >= 6, "five character rows and an empty slot (" .. rows .. " shown rows)")
check(Swc.HostPointOf("text:Epsilon_Made") ~= "", "the fifth row (the clone of the row template) shows its name")
check(Swc.HostPointOf("text:70") ~= "" and Swc.HostPointOf("text:60") ~= "" and Swc.HostPointOf("text:1") ~= "",
  "the row levels are the document values + 1 (70, 60, 1)")
equal(L.selectedCharacterId, "c-2", "the default selection: the smallest nonzero elapsed time (the last played)")
equal(F.selectedId, "c-2", "the host got the selected id (SetVisibleCharacter)")
check(F.previewShown, "the preview shows")
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Position").x, 1, "the position node of the selection")
picture("frontend-list")

-- A click on the third row.
Swc.hostRequests = {}
local third = find(swcui.children(list)[3], "CharacterInfoHolder")
click(third)
frames(0.05)
equal(L.selectedCharacterId, "c-3", "the click selects the third row")
equal(UiRadioButtonGroupBus.Event.GetState(list), third, "the radio state of the list is the third row")
-- SelectCharacter hides the preview, gives the id to the host, and selects the world of the character: SelectServer shows
-- the preview again in the same call (CheckCharacterVisibility); the check of 0.25 s later keeps it.
equal(table.concat(Swc.hostRequests, " | "), "menu show 0 | menu preview c-3 | menu show 1",
  "the order: hide, the selected id, show")
frames(0.3)
check(F.previewShown, "the preview shows after the check of 0.25 s")
equal(F.worldId, WORLD, "the world of the selected character")
equal(#requests("menu login"), 0, "a selection alone gives no login request")
picture("frontend-select")

-- A refresh with a new order keeps the selection by its id (FE10).
F.hold.cms, F.hold.regions = false, false
L:RefreshWorldList()
frames(0.1)
answer({five[5], five[4], five[3], five[2], five[1]})
frames(0.5)
equal(L.selectedCharacterId, "c-3", "a refresh with a new order keeps the selected character")

-- The selection over a return to the menu (stage F6): the host gives the id of the last selected character before the first
-- list of the new runtime (Swc.HostMenuSelected); the list selects that character, not the last played one (c-2).
Swc.HostMenuSelected("c-4")
L:RefreshWorldList()
frames(0.1)
answer(five)
frames(0.5)
equal(L.selectedCharacterId, "c-4", "the host selection c-4 (not the last played c-2)")
Swc.HostMenuSelected("c-3")
L:RefreshWorldList()
frames(0.1)
answer(five)
frames(0.5)
equal(L.selectedCharacterId, "c-3", "the host selection c-3 again")

-- Stage F8: the delete of a character. OnCharacterDeletePressed (the delete of the options of a row) opens the popup of the
-- script with the hold-to-accept buttons; No and a short hold send nothing; a full hold of Yes closes the popup, shows the server
-- spinner, and its callback calls DeactivateCharacter once ("menu delete ID"); the result of the host makes the script ask for
-- its list again.
local function popup_root()
  for _, canvas in ipairs(swcui.canvases and swcui.canvases() or {}) do
    if string.find(string.lower(swcui.canvas_get(canvas, "path") or ""), "popup/popup", 1, true) then
      return canvas, swcui.canvas_get(canvas, "root")
    end
  end
end
local popupCanvas, popupTop = popup_root()
local function hold(id, seconds)
  move_to(id)
  Swc.HostInput("button", "left", true)
  frames(seconds)
  Swc.HostInput("button", "left", false)
  frames(0.1)
end
local function deletes()
  return requests("menu delete")
end
-- The path of the player: the options button of the row ("...", MoreInfoButton), then Delete in its list.
local function click_point(name)
  local p = Swc.HostPointOf(name)
  local x, y = p:match("([%d%.]+) ([%d%.]+)")
  if not x then
    return false
  end
  Swc.HostInput("move", tonumber(x), tonumber(y))
  Swc.HostInput("button", "left", true)
  frames(0.05)
  Swc.HostInput("button", "left", false)
  frames(0.3)
  return true
end
Swc.hostRequests = {}
check(click_point("in:Gamma_Made:MoreInfoButton"), "delete: the options button of the row of c-3 shows")
check(Swc.HostPointOf("DeleteButton") ~= "", "delete: the options of the row show Delete")
check(click_point("DeleteButton"), "delete: a click on Delete")
frames(0.5)
check(popupCanvas ~= nil and swcui.canvas_get(popupCanvas, "enabled"), "delete: the popup of the script shows")
-- The hold buttons: the elements ButtonYes and ButtonNo of ButtonHolderYesNoHold (the script fields ButtonYesHold, ButtonNoHold).
local holdHolder = popupTop and find(popupTop, "ButtonHolderYesNoHold")
local yesHold, noHold = holdHolder and find(holdHolder, "ButtonYes"), holdHolder and find(holdHolder, "ButtonNo")
check(yesHold ~= nil and swcui.enabled_in_hierarchy(yesHold), "delete: the hold-to-accept Yes button shows")
picture("frontend-delete")
if noHold then
  click(noHold)
end
frames(0.5)
equal(#deletes(), 0, "delete: No sends nothing")
check(not swcui.canvas_get(popupCanvas, "enabled"), "delete: No closes the popup")
L:OnCharacterDeletePressed()
frames(0.5)
if yesHold then
  hold(yesHold, 0.3)
end
frames(0.5)
equal(#deletes(), 0, "delete: a short hold of Yes (0.3 s) sends nothing")
check(swcui.canvas_get(popupCanvas, "enabled"), "delete: after a short hold the popup stays")
if yesHold then
  hold(yesHold, 3.0)
end
frames(2.0)
local sent = deletes()
equal(#sent, 1, "delete: a full hold of Yes sends one request")
equal(sent[1], "menu delete c-3", "delete: the request has the selected id")
check(not swcui.canvas_get(popupCanvas, "enabled"), "delete: the popup closes after the hold")
Swc.hostRequests = {}
Swc.HostMenuDeleteResult("c-3", true)
frames(0.5)
check(#requests("menu list") >= 1, "delete: the result makes the script ask for its list")
answer({five[1], five[2], five[4], five[5]})
frames(0.5)
equal(#L.currentCharacterData, 4, "delete: the list has 4 rows after the delete")
check(L.selectedCharacterId ~= "c-3" and L.selectedCharacterId ~= "", "delete: an other character is selected (" .. tostring(L.selectedCharacterId) .. ")")
equal(#deletes(), 0, "delete: no second request")
-- A failed delete (also the refusal of a character of an other account: the backend answers 404): the list keeps the row.
Swc.hostRequests = {}
L:OnCharacterDeletePressed()
frames(0.5)
if yesHold then
  hold(yesHold, 3.0)
end
frames(2.0)
local failedId = string.match(deletes()[1] or "", "^menu delete (.+)$")
check(failedId ~= nil, "delete: the second delete sends its request")
Swc.hostRequests = {}
Swc.HostMenuDeleteResult(failedId, false)
frames(0.5)
check(#requests("menu list") >= 1, "a failed delete: the script asks for its list")
answer({five[1], five[2], five[4], five[5]})
frames(0.5)
equal(#L.currentCharacterData, 4, "a failed delete: the row stays")
check(L.PlayButton:IsEnabled(), "a failed delete: PLAY is enabled again")
-- The delete of the last character: the list is empty after the refresh (no rows, no preview, no login).
L:RefreshWorldList()
frames(0.1)
answer({five[2]})
frames(0.5)
equal(#L.currentCharacterData, 1, "the last character: one row")
Swc.hostRequests = {}
L:OnCharacterDeletePressed()
frames(0.5)
if yesHold then
  hold(yesHold, 3.0)
end
frames(2.0)
equal(deletes()[1], "menu delete c-2", "the last character: its delete request")
Swc.HostMenuDeleteResult("c-2", true)
frames(0.5)
answer({})
frames(0.5)
equal(#L.currentCharacterData, 0, "the last character: no row after the delete")
check(not F.previewShown, "the last character: the preview is hidden")
-- Back to the five rows for the checks below (the selection c-3 by the host).
Swc.HostMenuSelected("c-3")
L:RefreshWorldList()
frames(0.1)
answer(five)
frames(0.5)
equal(L.selectedCharacterId, "c-3", "the five rows and c-3 again")

-- Options and back: options.luac ExitScreen goes to the escape menu of the main menu (MainMenuEsc), Escape there to the
-- landing screen.
L:LandingOptions()
frames(0.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), OPTIONS, "the options state")
key("escape")
frames(0.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), MAIN_ESC, "Escape on the options: the escape menu of the main menu")
for _ = 1, 2 do
  if LyShineManagerBus.Broadcast.GetCurrentState() ~= LANDING then
    key("escape")
    frames(0.5)
  end
end
equal(LyShineManagerBus.Broadcast.GetCurrentState(), LANDING, "Escape: back to the landing screen")
-- The exit state (the escape menu of the main menu) and back.
L:LandingExit()
frames(0.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), MAIN_ESC, "the exit state of the main menu")
key("escape")
frames(0.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), LANDING, "Escape: back to the landing screen")
equal(#requests("menu quit"), 0, "no quit request")

-- Play: Enter (frontEndContinue), then a click on PLAY: one login request of the selected id.
-- The landing screen asks for the list again at its transition in (BeginConnection): the host answers.
check(#requests("menu list") >= 1, "the return to the landing screen asks for the list again")
answer({five[5], five[4], five[3], five[2], five[1]})
frames(0.5)
equal(L.selectedCharacterId, "c-3", "the selection stays after the return")
check(L.PlayButton:IsEnabled(), "PLAY is enabled")
Swc.hostRequests = {}
key("enter")
frames(0.2)
click(L.Properties.PlayButton)
frames(0.2)
local logins = requests("menu login")
equal(#logins, 1, "Enter and a click give one login request")
equal(logins[1], "menu login c-3 OpenWorld", "the login of the selected id, world type OpenWorld")

-- Stage F5: a failed login (the host has no login token): the popup of the error, its OK, and PLAY again.
Swc.HostMenuConnection(3)
frames(0.2)
check(L.currentState == L.STATE_CONNECTING_TO_GAME, "the login runs (the state of the script)")
Swc.HostMenuLoginFailed("@mm_loginservices_UnableToConnectError")
frames(1.0)
local popupRoot = nil
for _, canvas in ipairs(swcui.canvases and swcui.canvases() or {}) do
  if string.find(string.lower(swcui.canvas_get(canvas, "path") or ""), "popup/popup", 1, true) then
    popupRoot = swcui.canvas_get(canvas, "root")
  end
end
local function find_text(id, label)
  for _, c in ipairs(swcui.children(id)) do
    if swcui.enabled_in_hierarchy(c) then
      local text = swcui.get(c, "text.text")
      if text and (text == label or swcui.localize(text) == label) then
        return c
      end
      local found = find_text(c, label)
      if found then
        return found
      end
    end
  end
  return nil
end
local ok = find_text(popupRoot or swcui.canvas_get(landing, "root"), "OK")
check(ok ~= nil, "the error popup shows its OK button")
Swc.hostRequests = {}
if ok then
  click(ok)
end
frames(0.5)
check(not L.playButtonClicked, "OK resets the landing screen (PLAY can try again)")
check(#requests("menu list") >= 1, "OK asks for the list again (ResetScreenAfterError: AutoRefreshWorldList)")
answer({five[5], five[4], five[3], five[2], five[1]})
frames(0.5)
check(L.PlayButton:IsEnabled(), "PLAY is enabled after the refresh")
Swc.hostRequests = {}
key("enter")
frames(0.2)
equal(requests("menu login")[1], "menu login c-3 OpenWorld", "PLAY again after the error: a new login request")

-- The world type FTUE for a character with no complete first-time experience; no login for a character that must be renamed.
L.playButtonClicked = false
L:ResetScreenAfterError(true)
L:RefreshWorldList()
frames(0.1)
answer({{"c-6", "Zeta Made", 0, 10, true}, {"c-7", "Eta Made", 9, 900, false, true}})
frames(0.5)
equal(L.selectedCharacterId, "c-6", "the last played character c-6")
Swc.hostRequests = {}
key("enter")
frames(0.2)
equal(requests("menu login")[1], "menu login c-6 FTUE", "the world type FTUE")
-- The login of c-6 fails: OK of the popup refreshes the list, then the second row.
Swc.HostMenuLoginFailed("@mm_loginservices_UnableToConnectError")
frames(0.5)
local ok2 = find_text(popupRoot or swcui.canvas_get(landing, "root"), "OK")
Swc.hostRequests = {}
if ok2 then
  click(ok2)
end
frames(0.3)
answer({{"c-6", "Zeta Made", 0, 10, true}, {"c-7", "Eta Made", 9, 900, false, true}})
frames(0.5)
click(find(swcui.children(list)[2], "CharacterInfoHolder"))
frames(0.3)
equal(L.selectedCharacterId, "c-7", "the second row c-7 (must rename)")
Swc.hostRequests = {}
key("enter")
frames(0.3)
equal(#requests("menu login"), 0, "no login for a character that must be renamed (the rename popup of the script)")

-- A new account state: no character, then no world (a second landing screen run).
L.playButtonClicked = false
L:RefreshWorldList()
frames(0.1)
Swc.hostRequests = {}
answer({})
frames(0.5)
equal(#L.currentCharacterData, 0, "no character: no rows")
check(not F.previewShown, "no character: the preview is hidden")
equal(#requests("menu preview"), 0, "no character: no selection")
picture("frontend-empty")
L:RefreshWorldList()
frames(0.1)
answer({}, {})
frames(0.5)
equal(#L.selectableWorldList, 0, "no world: no selectable world")
equal(#requests("menu login"), 0, "no login request with no character")


equal(Swc.errorCount or 0, 0, "no Lua error")

-- The landing screen (landingscreenv2.luac): the depth of field 0 1 1 3 10, off at its close.
local camera = camera_log()
camera_sent(camera, "camera dof 0.000 1.000 1.000 3.000 10.000", "the landing screen")
camera_clean("the landing screen")
