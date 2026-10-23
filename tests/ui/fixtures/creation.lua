-- The character creation fixture (stage Q3 of docs/FRONTEND.md): the original screen CharacterCreation (charactercreation.luac,
-- cacarchetypestabv2.luac, cacappearancetab.luac, cacidentitytab.luac) with the native part of swc/creation.lua and the data of
-- gamedata/creation.json. The names and ids are made up.
--   * The start: the archetype camera sequence, then the archetype step with its seven archetypes.
--   * The appearance step: the lists of the databases; a change of a value sends the look request.
--   * The identity step: the local name rule, the name check request after 2 s, a stale result is ignored, a name in use.
--   * The create: one request; a failure popup; a success plays the end sequence and logs in with the new id (FTUE).
-- Pictures: creation-archetype, creation-appearance, creation-identity.
local F = Swc.frontend
local C = Swc.creation
F.menu = true
local WORLD = "00000000-0000-4000-8000-0000000000aa"
F.worldId = WORLD
local creation = screen("LyShineUI/Main Menu/CharacterCreation.uicanvas", 5)
screen("LyShineUI/Popup/Popup.uicanvas", 60)
local root = swcui.canvas_get(creation, "root")
local CC = script_of(find(root, "CharacterCreationMaster"), "charactercreation.luac")
check(CC ~= nil, "the creation script")

local function requests(prefix)
  local out = {}
  for _, r in ipairs(Swc.hostRequests) do
    if string.sub(r, 1, #prefix) == prefix then
      out[#out + 1] = r
    end
  end
  return out
end

check(#(swcui.load_json("gamedata/creation.json") or {}).archetypes == 7, "the creation data has 7 archetypes")
Swc.HostCreationSequences("Archetype_Camera_Intro_Trackview_Sequence=1.000;Character_Create_End_Trackview_Sequence=1.000;")
Swc.hostRequests = {}
Swc.CreationStart()
frames(0.2)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), 4065059436, "the state CharacterCreationScreen")
check(#requests("creation look") >= 1, "the look of the new creation goes to the host")
equal(requests("menu sequence")[1], "menu sequence Archetype_Camera_Intro_Trackview_Sequence", "the archetype camera sequence plays")
frames(1.5)
check(CC.archetypesIntroComplete == true, "the end of the archetype camera sequence")
equal(CC.state, 5, "the archetype step (ArchetypeV2)")
-- The first archetype is selected (SelectArchetypeByIndex(1)): its equipment is on the preview (five slots).
local first = C.data.archetypes[1]
equal(CC.selectedArchetype, first.id, "the first archetype is selected")
local look = requests("creation look")
local last = look[#look]
check(last ~= nil and string.find(last, "shirt=" .. string.format("%u", swcui.crc32(string.lower(first.MaleShirt))), 1, true) ~= nil
  or string.find(last or "", "shirt=" .. string.format("%u", swcui.crc32(string.lower(first.FemaleShirt))), 1, true) ~= nil,
  "the archetype shirt is on the preview")
picture("creation-archetype")

-- Each archetype: its data object (the title of the table, the backstory, the appearance id of a slot).
for _, row in ipairs(C.data.archetypes) do
  local object = ArchetypeDataManagerRequestBus.Broadcast.GetArchetypeData(row.id)
  check(object ~= nil and object.title == row.Title and object.backstory == row.BackstoryId, "the archetype " .. row.ArchetypeId)
  equal(object:GetArchetypeCharacteristic(2757341904, false), row.DefaultGender, "the default gender of " .. row.ArchetypeId)
end

-- The appearance step.
Swc.hostRequests = {}
CC:navNext()
frames(1.0)
equal(CC.state, 2, "the appearance step")
local E = C.entity
local races = CustomizableCharacterRequestBus.Event.GetRacesForGender(E, "Male")
equal(#races, 31, "31 male races")
equal(#CustomizableCharacterRequestBus.Event.GetRacesForGender(E, "Female"), 30, "30 female races")
equal(#CustomizableCharacterRequestBus.Event.GetEyeColors(E), 20, "20 eye colors")
equal(#CustomizableCharacterRequestBus.Event.GetTattoos(E), 62, "62 tattoos")
check(#CustomizableCharacterRequestBus.Event.GetSkintones(E) >= 6, "the skin tones of the race")
local style = CustomizableCharacterRequestBus.Event.GetHairstyle(E)
local image = CustomizableCharacterRequestBus.Event.GetHairstyleImagePath(E, style)
check(string.find(image, "layered/hairstyle/", 1, true) ~= nil and string.find(image, CustomizableCharacterRequestBus.Event.GetHairColor(E), 1, true) ~= nil,
  "the hair image has the hair color")
check(swcui.texture_exists(image), "the hair image is converted: " .. image)
CustomizableCharacterRequestBus.Event.SetEyeColor(E, "eyecolor7")
equal(CustomizableCharacterRequestBus.Event.GetEyeColor(E), "eyecolor7", "the eye color changes")
frames(0.1)
look = requests("creation look")
check(#look >= 1 and string.find(look[#look], "eyecolor7", 1, true) ~= nil, "the new eye color goes to the host")
-- Many changes in one frame (a random look): one look request in the next tick.
Swc.hostRequests = {}
CustomizableCharacterRequestBus.Event.RandomHairstyle(E)
CustomizableCharacterRequestBus.Event.RandomHairColor(E)
CustomizableCharacterRequestBus.Event.RandomScar(E)
CustomizableCharacterRequestBus.Event.SetEyeColor(E, "eyecolor3")
frames(0.1)
equal(#requests("creation look"), 1, "one look request for the changes of one frame")
-- A new gender keeps a valid look (the race of that gender, its tone, its hair).
CustomizableCharacterRequestBus.Event.SetGender(E, "Female")
local femaleRaces = CustomizableCharacterRequestBus.Event.GetRacesForGender(E, "Female")
local raceFound = false
for _, r in ipairs(femaleRaces) do raceFound = raceFound or r == CustomizableCharacterRequestBus.Event.GetRace(E) end
check(raceFound, "the race is a female race")
picture("creation-appearance")

-- The identity step.
CC:navNext()
frames(1.0)
equal(CC.state, 3, "the identity step")
local ID = script_of(find(root, "CharacterCreationIdentity"), "cacidentitytab.luac")
check(ID ~= nil, "the identity script")
equal(UiCharacterServiceRequestBus.Broadcast.PreValidateNameCheck("Ab"), eValidStringResponse_TooShort, "a short name")
equal(UiCharacterServiceRequestBus.Broadcast.PreValidateNameCheck("Two  Spaces"), eValidStringResponse_MultipleSpaces, "two spaces")
equal(UiCharacterServiceRequestBus.Broadcast.PreValidateNameCheck("Name1234567"), eValidStringResponse_InvalidCharacters, "7 digits")
equal(UiCharacterServiceRequestBus.Broadcast.PreValidateNameCheck("Madevale"), eValidStringResponse_Valid, "a valid name")
-- A typed name: the check request after 2 s; a name in use; a stale reply is ignored; then a valid name.
Swc.hostRequests = {}
UiTextInputBus.Event.SetText(ID.Properties.NameInputField, "Takenname")
ID:editPlayerName()
frames(1.0)
equal(#requests("creation validate"), 0, "no check before 2 s")
frames(1.5)
equal(requests("creation validate")[1], "creation validate " .. WORLD .. "\tTakenname", "the name check request")
Swc.HostCreationValidate("Takenname", false, "@mm_loginservices_NameNotAvailableError")
frames(0.2)
check(ID.nameStatus ~= ENameValidationState_Valid, "a name in use is not valid")
picture("creation-identity-taken")
Swc.hostRequests = {}
UiTextInputBus.Event.SetText(ID.Properties.NameInputField, "Madevale")
ID:editPlayerName()
frames(2.5)
equal(requests("creation validate")[1], "creation validate " .. WORLD .. "\tMadevale", "the check of the new name")
Swc.HostCreationValidate("Takenname", true, "")
frames(0.2)
check(ID.nameStatus ~= ENameValidationState_Valid, "a stale reply of the old name is ignored")
Swc.HostCreationValidate("Madevale", true, "")
frames(0.2)
equal(ID.nameStatus, ENameValidationState_Valid, "the valid name")
picture("creation-identity")

-- The world step: the world list of the last list result (the native data calls of worldlistdatahandler.luac).
Swc.HostMenuWorld(WORLD, "Made World", "Made World", "set1", "ACTIVE", "OpenWorld", "1", 0, 6, 0, 1, "", 0, 0, "low")
Swc.HostMenuListEnd(4)
Swc.hostRequests = {}
CC:navNext()
frames(1.0)
equal(CC.state, 6, "the world step")
local selected = CC.WorldSelectPopup.currentSelectedWorld
equal(selected and selected.worldData and selected.worldData.worldId, WORLD, "the world step selects the world of the list")
picture("creation-world")
CC:CreateCharacter()
CC:CreateCharacter()
frames(0.2)
local creates = requests("creation create")
equal(#creates, 1, "one create request")
check(creates[1] ~= nil and string.find(creates[1], "\tMadevale\t", 1, true) ~= nil and string.find(creates[1], first.BackstoryId, 1, true) ~= nil,
  "the create has the name and the backstory: " .. tostring(creates[1]))
-- The equipment of the archetype in the key order of the client (gloves, shirt, hat, boots, pants).
check(creates[1] ~= nil and string.find(creates[1], "\tgloves=%d+,shirt=%d+,hat=%d+,boots=%d+,pants=%d+$") ~= nil,
  "the archetype equipment in the order of the client")
-- A failure: the popup of the reply; then a success: the end sequence, then the login of the new id (FTUE).
Swc.HostCreationResult("", "@mm_loginservices_CreateCharacterFailed", "@mm_loginservices_ALREADY_EXISTS")
frames(0.5)
picture("creation-failed")
Swc.hostRequests = {}
CC:CreateCharacter()
frames(0.2)
equal(#requests("creation create"), 1, "a new create after the failure")
Swc.HostCreationResult("c-new-1", "", "")
frames(0.2)
equal(requests("menu sequence")[1], "menu sequence Character_Create_End_Trackview_Sequence", "the end sequence plays")
frames(1.5)
equal(requests("menu login")[1], "menu login c-new-1 FTUE", "the login of the new character")
equal(swcui.error_count(), 0, "no Lua error")
