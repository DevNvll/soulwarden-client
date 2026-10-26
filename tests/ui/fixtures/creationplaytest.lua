-- The defects of the play test of the character creation (AST-267, docs/FRONTEND.md "Q3, the play test"): the original screen
-- CharacterCreation with the native part of swc/creation.lua. The names and ids are made up.
--   * The skip of the intro camera (a key while SetCanSkipCutscene is true).
--   * Escape: the quit popup on the archetype step (javelin.use-new-character-creation-flow true), Yes ends the creation; the popup of
--     the appearance step stays (one key press does not also answer it).
--   * The appearance step hides the helmet (the look request has HELMET 1).
--   * The body type: Entity(id):GetName() of the radio button, the random values, the buttons on again, OnSkinnedMeshCreated.
--   * The name icon: the script namevalidationstatus.luac shows Invalid for an empty name and Valid for a valid name.
local F = Swc.frontend
local C = Swc.creation
F.menu = true
local WORLD = "00000000-0000-4000-8000-0000000000bb"
F.worldId = WORLD
local creation = screen("LyShineUI/Main Menu/CharacterCreation.uicanvas", 5)
screen("LyShineUI/Popup/Popup.uicanvas", 60)
local root = swcui.canvas_get(creation, "root")
local CC = script_of(find(root, "CharacterCreationMaster"), "charactercreation.luac")
local AM = script_of(find(root, "CharacterCreationAppearance"), "cacappearancetab.luac")
local ID = script_of(find(root, "CharacterCreationIdentity"), "cacidentitytab.luac")
check(CC ~= nil and AM ~= nil and ID ~= nil, "the scripts of the creation")

local function requested(text)
  for _, r in ipairs(Swc.hostRequests) do
    if r == text then
      return true
    end
  end
  return false
end
local function last_look()
  local found
  for _, r in ipairs(Swc.hostRequests) do
    if string.sub(r, 1, 14) == "creation look " then
      found = r
    end
  end
  return found or ""
end
local function buttons_on()
  for _, id in ipairs(UiElementBus.Event.GetChildren(AM.Properties.TopLevelList) or {}) do
    if not UiInteractableBus.Event.IsHandlingEvents(id) then
      return false
    end
  end
  return true
end
local function icon(name)
  for _, c in ipairs(UiElementBus.Event.GetChildren(find(root, "NameValidationIcon")) or {}) do
    if UiElementBus.Event.GetName(c) == name then
      return UiElementBus.Event.IsEnabled(c)
    end
  end
end

-- The skip of the intro camera (21 s in the record).
Swc.HostCreationSequences("Archetype_Camera_Intro_Trackview_Sequence=21.000;Archetype_Mercenary_Trans_Trackview_Sequence=1.330;"
  .. "Archetype_Mercenary_Start_Trackview_Sequence=1.000;Character_Create_Loop_Trackview_Sequence=8.000;")
Swc.hostRequests = {}
Swc.CreationStart()
frames(0.5)
check(CC.isIntroSceneLevel == true, "the creation in the intro level (javelin.use-new-character-creation-flow)")
check(C.canSkip == true and not CC.archetypesIntroComplete, "the intro plays and can be skipped")
key("space")
frames(0.5)
check(CC.archetypesIntroComplete == true and CC.state == 5, "Space skips the intro: the archetype step")
check(requested("menu sequence-skip Archetype_Camera_Intro_Trackview_Sequence"), "the camera takes the end pose of the intro")

-- Escape on the archetype step: the quit popup; Escape again is No.
key("escape")
frames(0.5)
check(CC.showingQuitPopup == true, "Escape on the archetype step: the quit popup")
picture("playtest-quit-popup")
key("escape")
frames(0.5)
check(CC.showingQuitPopup == false and LyShineManagerBus.Broadcast.GetCurrentState() == 4065059436, "Escape again: No, the creation stays")

-- The appearance step: the helmet is hidden; Escape gives its popup, and it stays.
CC:navNext()
frames(2.0)
equal(CC.state, 2, "the appearance step")
check(string.sub(last_look(), -2) == " 1", "the appearance step hides the helmet: " .. last_look())
key("escape")
frames(0.5)
check(CC.showingQuitPopup == true, "Escape on the appearance step: its popup stays")
key("escape")
frames(0.5)
check(CC.showingQuitPopup == false and CC.state == 2, "Escape again: No")

-- The body type: the radio button of the list, the buttons off while the body changes, then on.
local gender = CustomizableCharacterRequestBus.Event.RandomGender(C.entity)
check(gender == "Male" or gender == "Female", "RandomGender gives a gender: " .. tostring(gender))
check(CustomizableCharacterRequestBus.Event.DefaultSkinTone(C.entity, "Female", "face21") ~= nil, "DefaultSkinTone gives a tone")
local list = find(root, "GenderList")
local female
for _, c in ipairs(UiElementBus.Event.GetChildren(list) or {}) do
  if UiElementBus.Event.GetName(c) == "Female" then
    female = c
  end
end
check(female ~= nil and Entity(female):GetName() == "Female", "Entity(id):GetName() of the radio button")
UiRadioButtonGroupBus.Event.SetState(list, female, true)
AM:setGender()
frames(0.1)
check(not buttons_on(), "the buttons are off while the body changes")
frames(0.6)
check(buttons_on(), "the buttons are on again after the change of the body type")
equal(AM.currentCharacterSelection.gender, "Female", "the screen has the new body type")
check(string.sub(last_look(), 1, 21) == "creation look Female|", "the look of the new body type: " .. last_look())
check(string.find(last_look(), "hat=592172824", 1, true) ~= nil, "OnSkinnedMeshCreated: the hidden hat of the female body")

-- The name icon of the identity step.
CC:navNext()
frames(3.0)
equal(CC.state, 3, "the identity step")
check(icon("Invalid") == true and icon("Valid") == false, "an empty name: the invalid icon")
-- The typed name (user test 2026-10-26, 8a): a click starts the edit; then each key of the keyboard comes as a key event and as a
-- character (the C++ gives each character of USwcGameViewportClient::InputChar to Swc.HostInput "text"). The key of a letter adds
-- no text and goes to no game map; BackSpace is a key only (the C++ drops the character '\b' of the platform layer).
click(ID.Properties.NameInputField)
check(Swc.IState(ID.Properties.NameInputField).editing == true, "a click on the name field starts the edit")
for _, ch in ipairs({"M", "a", "d", "x"}) do
  check(Swc.HostInput("key", string.lower(ch), true) == true, "the key " .. ch .. " goes to the text input")
  check(Swc.HostKeyToGame(string.lower(ch)) == false, "the key " .. ch .. " does not go to the game")
  Swc.HostInput("text", ch)
  Swc.HostInput("key", string.lower(ch), false)
end
key("backspace")
frames(0.1)
equal(UiTextInputBus.Event.GetText(ID.Properties.NameInputField), "Mad", "the typed name with one backspace")
UiTextInputBus.Event.SetText(ID.Properties.NameInputField, "Madevale")
ID:editPlayerName()
frames(0.3)
check(icon("Validating") == true, "a typed name: the validating icon")
frames(2.5)
Swc.HostCreationValidate("Madevale", true, "")
frames(0.3)
check(icon("Valid") == true and icon("Invalid") == false, "a valid name: the valid icon")
picture("playtest-name-valid")

-- Back to the archetype step and Escape, Yes: the creation ends (OnExit), and the landing screen shows.
CC:navBack()
frames(0.5)
CC:navBack()
frames(0.5)
if CC.showingQuitPopup then
  CC:OnPopupResult(ePopupResult_Yes, CC.QuitCharacterAppearanceEventId)
  frames(2.0)
end
equal(CC.state, 5, "back to the archetype step")
Swc.hostRequests = {}
key("escape")
frames(0.5)
CC:OnPopupResult(ePopupResult_Yes, CC.QuitCharacterCreationEventId)
frames(0.5)
check(requested("creation exit"), "Yes in the quit popup ends the creation")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), 3881446394, "the landing screen")

-- The mouse owner of the creation (user test 2026-10-26, 8b): charactercreation.luac SetState adds its canvas as a mouse owner on each
-- step but the name step, and OnExit does not remove it. PLAY unloads the menu screens: the owner goes with its canvas, and the world
-- gets the mouse.
check(Swc.mouseOwners[tostring(creation)] == true, "the creation canvas is still a mouse owner after OnExit (the original script)")
Swc.UnloadCanvas(creation)
check(Swc.mouseOwners[tostring(creation)] == nil, "the unload of the canvas removes its mouse owner")
check(next(Swc.mouseOwners) == nil, "no mouse owner after the menu screens unload")
equal(swcui.error_count(), 0, "no Lua error")
