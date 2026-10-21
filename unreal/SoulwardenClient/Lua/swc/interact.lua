-- The prompt of the focus (stage WF7; docs/UI.md "The world views"): the native part of the prompt cards of the canvas
-- lyshineui/markers/unifiedinteract.dynamicuicanvas (markermanager.luac in the interact mode, unifiedinteractcard.luac,
-- unifiedinteractoption.luac, interactcardresizer.luac, hint.luac). The native interactor of the game (not in the scripts) sends the
-- focus and the unfocus of an object to the scripts (UiInteractorComponentNotificationsBus), its option component fills the rows of
-- a card (UnifiedInteractOptionsComponentRequestsBus PopulateInteractOptions), and the engine writes the screen place of the focus.
-- This module does it from the prompt of the client (FSwcInteractor::GetPrompt, through Swc.HostPrompt):
--   - a new focus: OnInteractUnfocus of the old one, then OnInteractFocus(data, no marker, the marker type of the object);
--   - the rows: one copy of the option template of the card for each option of the bound, with the label of the option record
--     (gamedata/interactoptions.json) and the key hint of its place: the first option ui_interact (E), the second ui_interact_sec
--     (T); a hold time gives the hold hint (Icon_EHold, Icon_NHold);
--   - the hold ring: the fill of PressandHoldProgressBar of the row and CryActionHoldProgress of the hints while the key is held;
--   - the place: Hud.LocalPlayer.UnifiedInteract.ScreenPosition.<entity> (the place of the bound, projected, in canvas units);
--   - the tool refusal of a gather ("No equipped Axe"): the key hides, the crossed ring shows, and the gather info of the row
--     (gatheringtooladditionalinfo.luac) gets the gathering type and an empty tool list (the tool rule of the client);
--   - the key T: the action ui_interact_sec of the map "ui" opens the panel of the bag of the focus (LootDropRequestsBus
--     OpenLootDrop: the request OpenStorageOption, as Tab near a bag).

local Swc = Swc

local P = {focus = nil, rows = {}, options = nil, infos = {}}
Swc.prompt = P
Swc.Handled("UiInteractorComponentNotificationsBus", "UnifiedInteractOptionNotificationsBus")

local function option_data()
  if not P.options then
    P.options = swcui.load_json("gamedata/interactoptions.json") or {}
  end
  return P.options
end

-- The marker type of the focus for the card category of markermanager.luac (UpdateFocusedInteractable: a Gatherable card for a type
-- with useGatherableInteract, a Camp card, else a Structure card). The objects of the level data have the kind and the gathering type
-- of the client data; the marker types of their slices are not converted: this table is an inference from the type names of
-- markermanager.luac. A bag and a fixture have no marker type (the Structure card).
-- A body (the skinning, the gathering type Dressing) is a creature: an AI type of the table (each AI type but IceGauntlet has the
-- Gatherable card; the retail picture F-29 of the backend gathering stage shows "E CHALKY RABBIT" on it). The prompt data has no
-- marker type of the creature of the body: "Wolf" stands for the AI types.
local GATHER_TYPES = {Chopping = "Master_Tree", Mining = "Master_Minerals", Cutting = "Master_Plants", Harvesting = "Master_Plants",
                      Dressing = "Wolf"}
local function marker_type(focus)
  if focus.kind ~= "object" then
    return ""
  end
  if focus.objectKind == "chest" then
    return "Master_LootContainer"
  end
  if focus.objectKind == "lorePage" then
    return "LoreItems"
  end
  return GATHER_TYPES[focus.gatheringType] or ""
end

-- An entity id of the runtime for the object of a focus: a bag has the id of swc/loot.lua (7 and the interest), a fixture 5 and its
-- interest, an object of the level data 9 and the CRC-32 of its key.
local function entity_of(focus)
  if focus.kind == "bag" then
    return EntityId(string.format("7%010d", tonumber(focus.id) or 0))
  elseif focus.kind == "fixture" then
    return EntityId(string.format("5%010d", tonumber(focus.id) or 0))
  end
  return EntityId(string.format("9%010d", swcui.crc32(focus.id or "")))
end

--- The prompt of the frame: tab-separated fields (kind none|bag|object|fixture, id, object kind, name, the place x y z in Unreal cm,
--- phase idle|hold|use|done, progress 0..1, tool missing 0|1, gathering type, refusal key, options "NAME:HOLD,...", rank missing
--- 0|1, the required rank of the row).
local function parse_prompt(line)
  local f = {}
  for field in string.gmatch((line or "") .. "\t", "([^\t]*)\t") do
    f[#f + 1] = field
  end
  if #f < 13 or f[1] == "none" or f[1] == "" then
    return nil
  end
  local focus = {kind = f[1], id = f[2], objectKind = f[3], name = f[4], x = tonumber(f[5]) or 0, y = tonumber(f[6]) or 0,
                 z = tonumber(f[7]) or 0, phase = f[8], progress = tonumber(f[9]) or 0, toolMissing = f[10] == "1",
                 gatheringType = f[11], refusalKey = f[12], options = {}, rankMissing = f[14] == "1",
                 requiredRank = tonumber(f[15] or "") or 0}
  for name, hold in string.gmatch(f[13], "([^,:]+):([%d.]+)") do
    focus.options[#focus.options + 1] = {name = name, hold = tonumber(hold) or 0}
  end
  -- A new refusal state (the tool came into the paperdoll) is a new focus: the card fills its rows again.
  focus.key = focus.kind .. ":" .. focus.id .. (focus.toolMissing and ":refused" or "") .. (focus.rankMissing and ":lowskill" or "")
  return focus
end

-- The name of the focus (the interactName of the native data): the name of the Interact component of the bag slice for each bag
-- ("@inv_DroppedItems": the live check L5 of the backend shows "DROPPED ITEMS" for both kinds of bag), else the name of the row.
local function interact_name(focus)
  if focus.kind == "bag" then
    return "@inv_DroppedItems"
  end
  return focus.name
end

-- The option list of the native data (unifiedInteractOptions): the fields that the card reads, and the values of the rows.
local function option_list(focus)
  local list = {}
  for i, o in ipairs(focus.options) do
    local record = option_data()[o.name] or {}
    list[i] = {name = o.name, label = record.label or o.name, hold = o.hold, slot = i, requiredLevel = 0, requiredAchievement = 0,
               refused = i == 1 and (focus.toolMissing or focus.rankMissing), additionalInfo = record.additionalInfo,
               gatheringType = focus.gatheringType, rankMissing = focus.rankMissing, requiredRank = focus.requiredRank}
  end
  return list
end

local function focus_data(focus)
  local entity = entity_of(focus)
  return {
    interactableEntityId = entity, interactorEntityId = Swc.ids.interactor, interactableRootEntityId = entity,
    markerId = "", markerEntityId = EntityId(), interactName = interact_name(focus), unifiedInteractOptions = option_list(focus),
    isHomePoint = false,
    GetOwnershipBind = function()
      return {ownershipBindEntityId = EntityId(), GetOwnership = function() return {} end}
    end,
  }
end

-- ------------------------------------------------------------------------------------------------------- the rows

local function child(id, name)
  local found = swcui.find_descendant(id, name)
  if found and found:IsValid() then
    return found
  end
  return nil
end

-- The key hints of a row: the first option has the key of ui_interact (Icon_E), the second of ui_interact_sec (Icon_N); a hold time
-- gives the hold form (Icon_EHold, Icon_NHold: hint.luac HintForceAsKeyboardHold).
local HINTS = {{"Icon_E", "Icon_EHold"}, {"Icon_N", "Icon_NHold"}}

-- The additional info of a row (the option record: "GatherAdditionalInfo", "ContainerAdditionalInfo"): a copy of that element of
-- AdditionalInfoSlices of the canvas in the element AdditionalInfoRoot of the row. Only the gather info has native data in this
-- client (UiInteractOptionAdditionalInfoRequestsBus PopulateGatheringToolAdditionalInfo; the type 5 of OnAdditionalInfoChanged, the
-- one that eInteractAdditionalType does not name, is an inference). A bag shows no container info (live check L5 of the backend).
local ADDITIONAL_GATHER = 5

local function additional_info(row, option)
  local root = child(row, "AdditionalInfoRoot")
  local state = P.infos[tostring(row)]
  local wanted = option.additionalInfo == "GatherAdditionalInfo" and option.gatheringType ~= "" and option.additionalInfo or nil
  if state and state.name ~= wanted then
    UiElementBus.Event.DestroyElement(state.element)
    P.infos[tostring(row)] = nil
    state = nil
  end
  if not wanted or not root then
    Swc.Notify("UnifiedInteractOptionNotificationsBus", row, "OnAdditionalInfoChanged", 0, EntityId())
    return
  end
  if not state then
    local canvas = swcui.canvas_of(row)
    local source = canvas and child(swcui.canvas_get(canvas, "root"), wanted)
    if not source then
      return
    end
    state = {name = wanted, element = Swc.CloneElement(source, root, nil)}
    P.infos[tostring(row)] = state
    UiElementBus.Event.SetIsEnabled(state.element, true)
  end
  Swc.Notify("UiInteractOptionAdditionalInfoRequestsBus", state.element, "PopulateGatheringToolAdditionalInfo", {
    itemDescriptor = ItemDescriptor(), gatheringType = swcui.crc32(string.lower(option.gatheringType)), requiredNumGatherers = 0,
    -- The rank gate of the row (the client rule of the object layer: the trade skill rank of the owner against
    -- RequiredTradeskillLevel): the script shows "Requires Mining skill" and the level circle of the required rank.
    hasTradeskillRank = not option.rankMissing, isInRequiredFaction = true, hasControllingFactionPaidUpkeep = true,
    requiredRank = option.requiredRank or 0,
    requiredFaction = eFactionType_None, isFFARestricted = false,
  })
  Swc.Notify("UnifiedInteractOptionNotificationsBus", row, "OnAdditionalInfoChanged", ADDITIONAL_GATHER, state.element)
end

-- A row of an option. A refused option (no tool for the row): the key hints hide and the crossed ring of the card shows
-- (DontHave_Tool; no script sets it: the retail pictures F-03 and L6-02 of the backend gathering stage show it, and no key).
local function fill_row(group, row, option)
  local name = child(row, "Text_Name")
  if name then
    swcui.set(name, "text.text", swcui.localize(option.label))
  end
  for slot, pair in ipairs(HINTS) do
    for form, iconName in ipairs(pair) do
      local icon = child(row, iconName)
      if icon then
        local on = not option.refused and slot == option.slot and ((form == 2) == (option.hold > 0))
        UiElementBus.Event.SetIsEnabled(icon, on)
      end
    end
  end
  local ring = child(row, "PressandHoldProgressBar")
  if ring then
    UiElementBus.Event.SetIsEnabled(ring, option.hold > 0 and not option.refused)
    UiImageBus.Event.SetFillAmount(ring, 0)
  end
  if option.slot == 1 then
    local cross = child(group, "DontHave_Tool")
    if cross then
      UiElementBus.Event.SetIsEnabled(cross, option.refused == true)
    end
  end
  additional_info(row, option)
  Swc.Notify("UnifiedInteractOptionNotificationsBus", row, "OnInteractOptionPopulated")
  Swc.Notify("UnifiedInteractOptionNotificationsBus", row, "OnCanExecuteInteractOptionChanged", not option.refused)
end

local function rows_of(group)
  local k = tostring(group)
  P.rows[k] = P.rows[k] or {group = group, list = {}}
  return P.rows[k]
end

Swc.Define("UnifiedInteractOptionsComponentRequestsBus", "Event", {
  PopulateInteractOptions = function(group, playerData, options)
    local state = rows_of(group)
    local parent = child(group, "Options")
    if not parent then
      return
    end
    -- The rows: the children of the group in the canvas first (the template, off in the canvas, and the other rows), then copies of
    -- the template. The layout of the group counts the rows that are off (no "visible only" flag): the rows of the options come
    -- first, the rows that are not used stay off after them (the retail card has the first row right under the title).
    if #state.list == 0 then
      for i, c in ipairs(swcui.children(parent)) do
        state.list[i] = c
      end
    end
    local template = state.list[1]
    for i, option in ipairs(options or {}) do
      local row = state.list[i]
      if not row then
        row = Swc.CloneElement(template, parent, nil)
        state.list[i] = row
      end
      UiElementBus.Event.SetIsEnabled(row, true)
      fill_row(group, row, option)
    end
    for i = #(options or {}) + 1, #state.list do
      UiElementBus.Event.SetIsEnabled(state.list[i], false)
    end
    state.options = options
    swcui.update_layouts()
  end,
  RemoveAllInteractOptions = function(group)
    local state = rows_of(group)
    for _, row in ipairs(state.list) do
      UiElementBus.Event.SetIsEnabled(row, false)
    end
    state.options = nil
  end,
})

-- ------------------------------------------------------------------------------------------------- the focus

-- The component ids of the local player (the native class LocalPlayerComponentData): interactcommon.luac fills them from the data
-- nodes Hud.LocalPlayer.HudComponent.* when the crafting id is not valid.
rawset(_G, "LocalPlayerComponentData", function()
  local data = {}
  for _, name in ipairs({"craftingEntityId", "inventoryEntityId", "itemRepairEntityId", "paperdollEntityId", "vitalsEntityId",
                         "staminaEntityId", "interactorEntityId", "socialEntityId", "currencyConversionEntityId", "gatheringEntityId",
                         "playerEntityId"}) do
    data[name] = EntityId()
  end
  return data
end)

-- The native requests of the card for an object of this client: no NPC, no owner, no reset time, no costs, no restriction.
local NoTime = {ToSeconds = function() return 0 end}
Swc.Define("UiInteractorComponentRequestsBus", "Event", {StopPositionUpdates = function() end})
Swc.Define("TransformBus", "Event", {
  GetRootId = function(id) return id end,
  GetWorldTranslation = function()
    local f = P.focus
    return Vector3(f and f.x / 100 or 0, f and -f.y / 100 or 0, f and f.z / 100 or 0)
  end,
})
Swc.Define("NpcComponentRequestBus", "Event", {
  GetNpcData = function() return nil end,
  GetCurrentStateWithPlayer = function() return nil end,
})
Swc.Define("VitalsComponentRequestBus", "Event", {IsDeathsDoor = function() return false end})
Swc.Define("UiInteractRequestsBus", "Event", {
  ShouldHidePermissionDeniedElement = function() return true end,
  GetRequiredProgression = function() return nil end,
  GetTerritoryEntityId = function() return EntityId() end,
  GetNonInstancedSSGatherableEntityId = function() return EntityId() end,
})
Swc.Define("LootTrackerRequestBus", "Event", {GetSlayerScriptInstancedResetTimeLeft = function() return NoTime end})
Swc.Define("UiGatherableComponentRequestsBus", "Event", {
  GetGatherableAdditionalInfoForUIC = function() return "" end,
  GetGatherableDescriptionForUIC = function() return "" end,
  GetResetTimeLeft = function() return NoTime end,
  GetProgressionCurrencyCosts = function() return {} end,
  GetGatheringType = function() return 0 end,
  GetItemRestrictionInfo = function() end,
  CurrentNumGatherers = function() return 0 end,
  RequiredNumGatherers = function() return 0 end,
  HasRestrictionPrompt = function() return false end,
  GetRestrictionPrompt = function() return nil end,
  IsRestrictionMet = function() return true end,
})
Swc.Define("UiDataManagerBus", "Broadcast", {ShouldClosePromptOnInteraction = function() return false end})
-- The tool of a gather (gatheringtooladditionalinfo.luac GetTool): valid when the client rule found a tool for the row of the focus.
local function tool_of()
  local missing = P.focus and P.focus.toolMissing
  return {IsValid = function() return not missing end}
end
Swc.Define("UiGatheringComponentRequestsBus", "Event", {
  GetValidGatheringToolList = function() return tool_of() end,
  GetFirstSlotWithItem = function() return tool_of() end,
})
Swc.Handled("UiInteractOptionAdditionalInfoRequestsBus")

local function send_focus(focus)
  Swc.Notify("UiInteractorComponentNotificationsBus", Swc.ids.interactor, "OnInteractFocus", focus_data(focus), false, marker_type(focus))
end

local function send_unfocus()
  Swc.Notify("UiInteractorComponentNotificationsBus", Swc.ids.interactor, "OnInteractUnfocus")
end

-- The canvas of the prompt cards, and its shown cards (the card of the focus; markermanager.luac keeps its cards in its own table).
local function prompt_canvas()
  for _, screen in pairs(Swc.screens or {}) do
    if string.find(string.lower(swcui.canvas_get(screen.canvas, "path") or ""), "markers/unifiedinteract", 1, true) then
      return screen.canvas
    end
  end
  return nil
end

local function shown_cards()
  local out = {}
  local canvas = prompt_canvas()
  if not canvas then
    return out
  end
  local function scan(id)
    for _, c in ipairs(swcui.children(id)) do
      local name = swcui.name(c) or ""
      if string.find(name, "UnifiedInteractCard", 1, true) then
        if swcui.enabled_in_hierarchy(c) then
          out[#out + 1] = c
        end
      else
        scan(c)
      end
    end
  end
  scan(swcui.canvas_get(canvas, "root"))
  return out
end
P.shownCards = shown_cards

-- The rows of the focus: the hold ring of the option of the use, and the key hints (CryActionHoldProgress).
local function update_rows(focus)
  for _, state in pairs(P.rows) do
    for i, row in ipairs(state.list) do
      local option = state.options and state.options[i]
      if option and UiElementBus.Event.IsEnabled(row) then
        local held = focus.phase == "hold" and i == 1
        local amount = held and focus.progress or 0
        local ring = child(row, "PressandHoldProgressBar")
        if ring and option.hold > 0 then
          UiImageBus.Event.SetFillAmount(ring, amount)
        end
      end
    end
  end
  local held = focus.phase == "hold" and focus.progress or 0
  if held ~= P.lastHold then
    P.lastHold = held
    for _, h in ipairs(Swc.Handlers("CryActionNotificationsBus", "ui_interact")) do
      local fn = h.connected and h.table.CryActionHoldProgress
      if type(fn) == "function" then
        Swc.Call(fn, h.table, "ui_interact", held)
      end
    end
  end
end

-- The place of the focus in canvas units (the card adds the offset of its type and keeps the place in the middle part of the
-- canvas).
local function place(focus)
  local project = Swc.markers and Swc.markers.project
  local view = Swc.markers and Swc.markers.view
  if not project or not view then
    return
  end
  local sx, sy = project(focus.x, focus.y, focus.z)
  if not sx then
    return
  end
  local canvas = prompt_canvas()
  local w, h = 1920, 1080
  if canvas then
    local size = UiCanvasBus.Event.GetCanvasSize(canvas)
    if size then
      w, h = size.x, size.y
    end
  end
  Swc.SetData("Hud.LocalPlayer.UnifiedInteract.ScreenPosition." .. tostring(entity_of(focus)),
    Vector2(sx * w / view.width, sy * h / view.height))
end

function Swc.HostPrompt(line)
  local focus = parse_prompt(line)
  local old = P.focus
  if old and (not focus or focus.key ~= old.key) then
    P.focus = nil
    send_unfocus()
  end
  if focus and (not old or focus.key ~= old.key) then
    P.focus = focus
    send_focus(focus)
  end
  P.focus = focus
  if focus then
    place(focus)
    update_rows(focus)
  end
end

-- The key T (the action ui_interact_sec of the map "ui"; actionmaps.json): the second option of the focus. For a bag it is
-- OpenStorageOption: the panel of the bag opens (the open of Tab near a bag).
Swc.Connect("CryActionNotificationsBus", {
  OnCryAction = function(_, action, value)
    local focus = P.focus
    if value ~= 1 or not focus or focus.kind ~= "bag" or #focus.options < 2 then
      return false
    end
    Swc.Note("the key of ui_interact_sec at the bag " .. focus.id .. ": " .. focus.options[2].name)
    LootDropRequestsBus.Event.OpenLootDrop(entity_of(focus))
    return true
  end,
}, "ui_interact_sec")
