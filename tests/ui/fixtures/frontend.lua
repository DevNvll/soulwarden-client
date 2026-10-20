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
    Swc.HostMenuCharacter(c[1], "persona-x", WORLD, c[2], c[3], 0, 1, 2, 3, c[4], 1, 0, 0, 0, 0)
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
