-- The character creation (stage Q3, docs/FRONTEND.md "Q3, the character creation"): the native part of the original screen
-- CharacterCreation (charactercreation.luac, cacarchetypestabv2.luac, cacappearancetab.luac, cacidentitytab.luac) on the data of
-- tools/convert-creation (gamedata/creation.json):
--   * CustomizableCharacterRequestBus: the 12 appearance values of the customizable character (the menu preview body of the
--     front end), their lists and images, the random values, the archetype equipment of the preview. Each change sends the host
--     request "creation look": the host makes the body again with the resolver of F3.
--   * ArchetypeDataManagerRequestBus.GetArchetypeData(id): the row of javelindata_archetypes whose ArchetypeId has the CRC-32 id.
--   * UiCharacterServiceRequestBus: PreValidateNameCheck (the local rule of the client, swcui.check_name), ValidateName (the
--     host request "creation validate"), CreateCharacter (the host request "creation create"); the results come back with
--     Swc.HostCreationValidate and Swc.HostCreationResult.
--   * IntroControllerComponentRequestBus: OnContinueToFTUE is the login of the new character (the host request "menu login ID
--     FTUE", the path of RequestLogin(characterId, "FTUE"), research character-creation.md 4.1); OnExit goes back to the landing
--     screen. The intro film and its sequences are not played (the path of g_uiEnableStoryCinematic false, a declared limit [I]).
--   * The sequences of the creation level: the host gives their time ranges (Swc.HostCreationSequences); a sequence sends
--     Updated at its start, then Stopped and BeyondEnd at its end (the events of the script).
local F = Swc.frontend
local C = {
  data = nil,
  active = false,
  -- The customizable character is the preview body of the front end (its entity id).
  entity = F.previewEntity,
  look = {},
  backstory = "",
  faction = 0,
  archetypeIndex = 1,
  helmetHidden = false,
  -- The archetype equipment of the preview: the slot ("hat", "shirt", "gloves", "pants", "boots") -> the appearance id.
  equipment = {},
  validating = nil,
  creating = false,
  newCharacterId = "",
  canSkip = false,
  seconds = {},
  playing = {},
}
Swc.creation = C

local CREATION_STATE = 4065059436   -- the CRC-32 of "charactercreationscreen" (the state of the screen)
local LANDING_STATE = 3881446394
local IMAGES = "lyshineui/images/charactercreation/layered/"
local FIELDS = {"gender", "race", "skinTone", "hairstyle", "facialHair", "hairColor", "facialHairColor", "eyeColor", "faceMark",
  "scar", "tattoo", "tattooColor"}
-- The columns of javelindata_archetypes for the equipment slots of the preview (GetAppearance(slot, gender)).
local SLOT_COLUMNS = {hat = "Head", shirt = "Shirt", gloves = "Gloves", pants = "Pants", boots = "Boots"}

local function request(text)
  Swc.hostRequests[#Swc.hostRequests + 1] = text
end

local function data()
  if C.data == nil then
    C.data = swcui.load_json("gamedata/creation.json") or false
  end
  return C.data or {}
end

local function find(list, name)
  for i, row in ipairs(list or {}) do
    if row.name == name then
      return row, i
    end
  end
end

local function names(list)
  local out = {}
  for _, row in ipairs(list or {}) do
    if row.enabled ~= false then
      out[#out + 1] = row.name
    end
  end
  return out
end

local function races(gender)
  local g = (data().genders or {})[gender or C.look.gender]
  return g and g.races or {}
end

local function race()
  return find(races(), C.look.race) or races()[1] or {}
end

local function hair_db(facial)
  local name = facial and race().facial or race().hair
  return (data().hair or {})[name or ""] or {styles = {}, colors = {}}
end

local function tones()
  return (data().tones or {})[race().tones or ""] or {}
end

local function pick(list)
  local enabled = names(list)
  if #enabled == 0 then
    return nil
  end
  return enabled[math.random(1, #enabled)]
end

--- The host request of the look: the 12 names, the equipment slots with their appearance ids, the helmet state. The scripts
--- set many values in one frame (an archetype, a random look): the request goes once in the next tick, and only on a change.
local function write_look()
  C.lookDirty = false
  local parts = {}
  for i, field in ipairs(FIELDS) do
    parts[i] = C.look[field] or ""
  end
  local equipment = {}
  for _, slot in ipairs({"hat", "shirt", "gloves", "pants", "boots"}) do
    if C.equipment[slot] then
      equipment[#equipment + 1] = slot .. "=" .. string.format("%u", C.equipment[slot])
    end
  end
  local text = "creation look " .. table.concat(parts, "|") .. " " .. (#equipment > 0 and table.concat(equipment, ",") or "-")
    .. " " .. (C.helmetHidden and "1" or "0")
  if text ~= C.lastLook then
    C.lastLook = text
    request(text)
  end
end

local function send_look()
  C.lookDirty = true
end

--- A value is kept only when the list of the current race has it; else the first entry (the default tone for a skin tone).
local function fit()
  local r = race()
  C.look.race = r.name or C.look.race
  if not find(tones(), C.look.skinTone) then
    C.look.skinTone = r.defaultTone or (tones()[1] or {}).name
  end
  local hair = hair_db(false)
  if not find(hair.styles, C.look.hairstyle) then
    C.look.hairstyle = (hair.styles[2] or hair.styles[1] or {}).name
  end
  if not find(hair.colors, C.look.hairColor) then
    C.look.hairColor = (hair.colors[1] or {}).name
  end
  local facial = hair_db(true)
  if not find(facial.styles, C.look.facialHair) then
    C.look.facialHair = (facial.styles[1] or {}).name
  end
  if not find(facial.colors, C.look.facialHairColor) then
    C.look.facialHairColor = (facial.colors[1] or {}).name
  end
end

--- The look of a new creation: the default appearance of the default gender of the first archetype.
local function reset()
  local first = (data().archetypes or {})[1] or {}
  local gender = first.DefaultGender or "Male"
  C.look = {gender = gender}
  local prefix = gender
  C.look.race = first[prefix .. "Face"]
  C.look.skinTone = first[prefix .. "Skin"]
  C.look.hairstyle = first[prefix .. "HairStyle"]
  C.look.hairColor = first[prefix .. "HairColor"]
  C.look.facialHair = first[prefix .. "FacialHair"]
  C.look.facialHairColor = first[prefix .. "FacialHairColor"]
  C.look.eyeColor = first[prefix .. "EyeColor"]
  C.look.faceMark = first[prefix .. "FaceMark"] or "none"
  C.look.scar = first[prefix .. "Scar"] or "none"
  C.look.tattoo = first[prefix .. "Tattoo"] or "none"
  C.look.tattooColor = first[prefix .. "TattooColor"]
  fit()
  C.equipment = {}
  C.helmetHidden = false
  C.backstory = ""
  C.faction = 0
end

-- ----------------------------------------------------------------------------------------------- the archetype data

local function archetype(id)
  for _, row in ipairs(data().archetypes or {}) do
    if row.id == id then
      return row
    end
  end
end

--- The archetype data object of the scripts: the fields title, description, equipWeight, proficiencyDescription, backstory, and
--- the methods GetAppearance(slot, gender) (the CRC-32 of the lower-case armor appearance name), GetProficiencyTooltip(i) and
--- GetProficiencyImage(i) (0-based), GetArchetypeCharacteristic(crc, lowerCase) (the column whose lower-case name has the CRC).
local function archetype_object(row)
  local object = {
    id = row.id, title = row.Title or "", description = row.Description or "", equipWeight = row.EquipWeight or "",
    proficiencyDescription = row.ProficiencyDescription or "", backstory = row.BackstoryId or "",
  }
  function object:GetAppearance(slot, gender)
    local column = SLOT_COLUMNS[slot]
    local name = column and row[(gender == "Female" and "Female" or "Male") .. column]
    return name and swcui.crc32(string.lower(name)) or 0
  end
  function object:GetProficiencyTooltip(i)
    return row["ProficiencyTooltip_" .. (i + 1)] or ""
  end
  function object:GetProficiencyImage(i)
    return row["ProficiencyImage_" .. (i + 1)] or ""
  end
  function object:GetArchetypeCharacteristic(crc, lowerCase)
    for column, value in pairs(row) do
      if swcui.crc32(string.lower(column)) == crc then
        value = tostring(value)
        return lowerCase and string.lower(value) or value
      end
    end
  end
  return object
end

Swc.Define("ArchetypeDataManagerRequestBus", "Broadcast", {
  GetArchetypeData = function(id)
    local row = archetype(id)
    return row and archetype_object(row) or nil
  end,
})

-- ----------------------------------------------------------------------------------------------- the customizable character

local function setter(field, after)
  return function(entity, value)
    C.look[field] = value
    if after then
      after()
    end
    send_look()
  end
end

local function random(field, list)
  return function()
    local value = pick(list())
    if value then
      C.look[field] = value
      send_look()
    end
  end
end

local function image_of(list, name, gender)
  local row = find(list, name)
  if not row then
    return ""
  end
  if row.images then
    return row.images[gender or C.look.gender] or ""
  end
  return row.image or ""
end

Swc.Define("CustomizableCharacterRequestBus", "Broadcast", {
  -- The scripts get the customizable character of the creation as the primary one (charactercreation.luac Primary 1).
  RequestSetCustomizableCharacterEntityId = function()
    Swc.Notify("UiMainMenuBus", nil, "SetCustomizableCharacterEntityIdAndIndex", C.entity, 1)
  end,
})

Swc.Define("CustomizableCharacterRequestBus", "Event", {
  GetGender = function() return C.look.gender end,
  GetGenderAsEnum = function() return C.look.gender == "Female" and (eGender_Female or 1) or (eGender_Male or 0) end,
  GetRace = function() return C.look.race end,
  GetSkinTone = function() return C.look.skinTone end,
  GetHairstyle = function() return C.look.hairstyle end,
  GetFacialHair = function() return C.look.facialHair end,
  GetHairColor = function() return C.look.hairColor end,
  GetFacialHairColor = function() return C.look.facialHairColor end,
  GetEyeColor = function() return C.look.eyeColor end,
  GetFaceMark = function() return C.look.faceMark end,
  GetScar = function() return C.look.scar end,
  GetTattoo = function() return C.look.tattoo end,
  GetTattooColor = function() return C.look.tattooColor end,
  -- The lists (the enabled names of the databases of the current gender and race).
  GetRacesForGender = function(entity, gender) return names(races(gender)) end,
  IsRaceEnabled = function(entity, name) local row = find(races(), name); return row ~= nil and row.enabled ~= false end,
  GetSkintones = function() return names(tones()) end,
  GetHairstyles = function() return names(hair_db(false).styles) end,
  GetFacialHairs = function() return names(hair_db(true).styles) end,
  GetHairColors = function() return names(hair_db(false).colors) end,
  GetFacialHairColors = function() return names(hair_db(true).colors) end,
  GetEyeColors = function() return names(data().eyeColors) end,
  GetFaceMarks = function() return names(data().faceMarks) end,
  GetScars = function() return names(data().scars) end,
  GetTattoos = function() return names(data().tattoos) end,
  GetTattooColors = function() return names(data().tattooColors) end,
  -- The images (the files of layered/: the hair images with the current hair color [I]).
  GetRaceImagePath = function(entity, name) return image_of(races(), name) end,
  GetSkinToneImagePath = function(entity, name) return image_of(tones(), name) end,
  GetHairstyleImagePath = function(entity, name)
    local row = find(hair_db(false).styles, name)
    return row and (IMAGES .. "hairstyle/" .. row.imageKey .. "-" .. (C.look.hairColor or "haircolor1") .. ".dds") or ""
  end,
  GetFacialHairImagePath = function(entity, name)
    local row = find(hair_db(true).styles, name)
    return row and (IMAGES .. "facialhair/" .. row.imageKey .. "-" .. (C.look.facialHairColor or "haircolor1") .. ".dds") or ""
  end,
  GetHairColorImagePath = function(entity, name) return image_of(hair_db(false).colors, name) end,
  GetFacialHairColorImagePath = function(entity, name) return image_of(hair_db(true).colors, name) end,
  GetEyeColorImagePath = function(entity, name) return image_of(data().eyeColors, name) end,
  GetFaceMarkImagePath = function(entity, name) return image_of(data().faceMarks, name) end,
  GetScarImagePath = function(entity, name) return image_of(data().scars, name) end,
  GetTattooImagePath = function(entity, name) return image_of(data().tattoos, name) end,
  GetTattooColorImagePath = function(entity, name) return image_of(data().tattooColors, name) end,
  -- The setters: a new gender or race keeps each value that its lists have [I].
  SetGender = setter("gender", function()
    if not find(races(), C.look.race) then
      C.look.race = (races()[1] or {}).name
    end
    fit()
  end),
  SetRace = setter("race", fit),
  SetSkinTone = setter("skinTone"),
  SetHairstyle = setter("hairstyle"),
  SetFacialHair = setter("facialHair"),
  SetHairColor = setter("hairColor"),
  SetFacialHairColor = setter("facialHairColor"),
  SetEyeColor = setter("eyeColor"),
  SetFaceMark = setter("faceMark"),
  SetScar = setter("scar"),
  SetTattoo = setter("tattoo"),
  SetTattooColor = setter("tattooColor"),
  DefaultSkinTone = function()
    C.look.skinTone = race().defaultTone or C.look.skinTone
    send_look()
  end,
  RandomGender = function()
    C.look.gender = math.random(0, 1) == 1 and "Female" or "Male"
    C.look.race = pick(races()) or C.look.race
    fit()
    send_look()
  end,
  RandomRace = function()
    C.look.race = pick(races()) or C.look.race
    fit()
    send_look()
  end,
  RandomSkinTone = random("skinTone", tones),
  RandomHairstyle = random("hairstyle", function() return hair_db(false).styles end),
  RandomFacialHair = random("facialHair", function() return hair_db(true).styles end),
  RandomHairColor = random("hairColor", function() return hair_db(false).colors end),
  RandomEyeColor = random("eyeColor", function() return data().eyeColors end),
  RandomFaceMark = random("faceMark", function() return data().faceMarks end),
  RandomScar = random("scar", function() return data().scars end),
  RandomTattoo = random("tattoo", function() return data().tattoos end),
  RandomTattooColor = random("tattooColor", function() return data().tattooColors end),
  -- The archetype equipment of the preview.
  ShowDefaultEquipment = function()
    C.equipment = {}
    send_look()
  end,
  ClearEquipment = function()
    C.equipment = {}
    send_look()
  end,
  PreviewEquipmentPartByAppearanceId = function(entity, slot, appearanceId)
    C.equipment[tostring(slot)] = (appearanceId ~= 0) and appearanceId or nil
    send_look()
  end,
  ForceHelmetHidden = function(entity, hidden)
    C.helmetHidden = hidden and true or false
    send_look()
  end,
  SetBackstory = function(entity, backstory) C.backstory = tostring(backstory or "") end,
  SetStartingFaction = function(entity, faction) C.faction = tonumber(faction) or 0 end,
})

-- The property events of the appearance tab (the world appearance component of the player): no player entity in the creation.
Swc.Define("PlayerAppearanceRequestBus", "Event", {
  PropertyChanged = function() end,
  RevertAppearance = function() end,
})

-- ----------------------------------------------------------------------------------------------- the name and the create

Swc.Define("UiCharacterServiceRequestBus", "Broadcast", {
  PreValidateNameCheck = function(name)
    return swcui.check_name(tostring(name or ""))
  end,
  ValidateName = function(worldId, name)
    C.validating = tostring(name or "")
    request("creation validate " .. tostring(worldId or "") .. "\t" .. C.validating)
  end,
  CreateCharacter = function(worldId, name, companyName)
    if C.creating then
      return   -- one create at a time (a second press of the button)
    end
    C.creating = true
    -- The map equipment of the creation data: the appearance of the archetype row of the backstory for each slot, in the key
    -- order of the create requests of the client (gloves, shirt, hat, boots, pants; protocol notes 2.2.5).
    local equipment = {}
    for _, row in ipairs(data().archetypes or {}) do
      if row.BackstoryId == C.backstory then
        local object = archetype_object(row)
        for _, slot in ipairs({"gloves", "shirt", "hat", "boots", "pants"}) do
          local id = object:GetAppearance(slot, C.look.gender)
          if id ~= 0 then
            equipment[#equipment + 1] = slot .. "=" .. string.format("%u", id)
          end
        end
      end
    end
    if C.lookDirty then
      write_look()   -- the host writes the creation data from the last look
    end
    request("creation create " .. tostring(worldId or "") .. "\t" .. tostring(name or "") .. "\t" .. tostring(companyName or "")
      .. "\t" .. C.backstory .. "\t" .. tostring(C.faction) .. "\t" .. table.concat(equipment, ","))
  end,
})

-- The icon of the name field (cacidentitytab.luac SetNameValidationStatus): the text input draws its own state.
Swc.Define("UiCharacterNameValidationStatusBus", "Event", {
  SetStatus = function() end,
})

Swc.Define("MainMenuSystemRequestBus", "Broadcast", {
  GetSelectedWorldId = function() return F.worldId or "" end,
})

--- The result of a name check: the state of the name field. A result for an older name is ignored (the field changed).
function Swc.HostCreationValidate(name, valid, textKey)
  if tostring(name) ~= C.validating then
    return
  end
  C.validating = nil
  local state = valid and (ENameValidationState_Valid or 1) or (ENameValidationState_Invalid or 0)
  Swc.Notify("UiCharacterServiceNotificationBus", nil, "ValidateNameResult", state, valid and "" or tostring(textKey or ""))
end

--- The result of a create: the id of the new character, or the title and the message of the failure popup.
function Swc.HostCreationResult(characterId, title, message)
  C.creating = false
  characterId = tostring(characterId or "")
  Swc.Notify("UiCharacterServiceNotificationBus", nil, "CreateCharacterResult", characterId, tostring(title or ""),
    tostring(message or ""), TimePoint())
  if characterId ~= "" then
    C.newCharacterId = characterId
    -- The native success path (client code 0x2EF9740): IntroControllerComponentRequestBus.OnCharacterCreated, then the
    -- notification OnCharacterCreated to the screen (the end sequence, then OnContinueToFTUE).
    Swc.Notify("IntroControllerComponentNotificationsBus", nil, "OnCharacterCreated")
  end
end

-- ----------------------------------------------------------------------------------------------- the intro controller

Swc.Define("IntroControllerComponentRequestBus", "Broadcast", {
  OnMenuNext = function() end,
  OnMenuBack = function() end,
  RequestPlayFaceCreationAnim = function() end,
  SetCanSkipCutscene = function(value) C.canSkip = value and true or false end,
  GetCurrentArchetypeIndex = function() return C.archetypeIndex end,
  SetCurrentArchetypeIndex = function(index) C.archetypeIndex = index end,
  OnContinueToFTUE = function()
    if C.newCharacterId ~= "" then
      request("menu login " .. C.newCharacterId .. " FTUE")
    end
  end,
  OnExit = function()
    Swc.CreationEnd()
    request("creation exit")
  end,
})

-- ----------------------------------------------------------------------------------------------- the sequences

--- The time ranges of the sequences of the creation level (the host, from the scene record): "name=seconds;name=seconds".
function Swc.HostCreationSequences(text)
  C.seconds = {}
  for name, seconds in string.gmatch(tostring(text or ""), "([^=;]+)=([^;]+);") do
    C.seconds[name] = tonumber(seconds) or 0
  end
end

local play_menu = CinematicRequestBus.Broadcast.PlaySequenceByName
local stop_menu = CinematicRequestBus.Broadcast.StopSequence
Swc.Define("CinematicRequestBus", "Broadcast", {
  PlaySequenceByName = function(name)
    if not C.active then
      return play_menu(name)
    end
    C.playing[name] = {left = C.seconds[name] or 0, updated = false}
    request("menu sequence " .. tostring(name))
  end,
  StopSequence = function(name)
    if not C.active then
      return stop_menu(name)
    end
    if C.playing[name] then
      C.playing[name] = nil
      request("menu sequence-stop " .. tostring(name))
      Swc.Notify("CinematicEventBus", nil, "OnCinematicStateChanged", name, eMovieEvent_Stopped or 1)
    end
  end,
})

--- Each frame: the events of the sequences of the creation (Updated at the start, then Stopped and BeyondEnd at the end).
function Swc.CreationTick(dt)
  if not C.active then
    return
  end
  if C.lookDirty then
    write_look()
  end
  -- A copy of the names: a handler can start a sequence (charactercreation.luac plays the loop at the end of an intro).
  local list = {}
  for name in pairs(C.playing) do
    list[#list + 1] = name
  end
  for _, name in ipairs(list) do
    local seq = C.playing[name]
    if seq == nil then
      -- stopped by a handler of an earlier sequence of this frame
    elseif not seq.updated then
      seq.updated = true
      Swc.Notify("CinematicEventBus", nil, "OnCinematicStateChanged", name, eMovieEvent_Updated or 3)
    else
      seq.left = seq.left - dt
      if seq.left <= 0 then
        C.playing[name] = nil
        Swc.Notify("CinematicEventBus", nil, "OnCinematicStateChanged", name, eMovieEvent_Stopped or 1)
        Swc.Notify("CinematicEventBus", nil, "OnCinematicStateChanged", name, eMovieEvent_BeyondEnd or 6)
      end
    end
  end
end

-- ----------------------------------------------------------------------------------------------- the start and the end

--- The start of the creation (the host, after "menu intro" and the load of the creation scene): a new look, the state of the
--- screen CharacterCreation. The archetype step starts with its camera sequence (charactercreation.luac).
function Swc.CreationStart()
  C.active = true
  C.playing = {}
  C.newCharacterId = ""
  C.creating = false
  C.validating = nil
  reset()
  C.lastLook = nil
  write_look()
  LyShineManagerBus.Broadcast.SetState(CREATION_STATE)
  CinematicRequestBus.Broadcast.PlaySequenceByName("Archetype_Camera_Intro_Trackview_Sequence")
end

--- The end of the creation without a character (OnExit): back to the landing screen.
function Swc.CreationEnd()
  C.active = false
  C.playing = {}
  LyShineManagerBus.Broadcast.SetState(LANDING_STATE)
end

return C
