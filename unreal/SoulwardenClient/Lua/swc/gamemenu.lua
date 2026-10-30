-- The game menu of this client (docs/UI.md "The game menu"): the original escape menu and Options screen with only the
-- features that this offline client has. The canvases and the scripts of the game do not change; the hooks of the client
-- (Swc.scriptHooks) hide elements and rows, and swc/options.lua saves and applies the values.
--   * The escape menu (escapemenu.luac): the store goes by the small frame of the script itself (the node
--     UIFeatures.g_uiEnableEntitlements false: no store, no Buy, no world name). The buttons of online services and of the
--     backend features that this backend does not have are hidden (G.escapeHidden); after each layout of the script
--     (UpdateScreen, UpdateMainMenuEsc) the client places the shown buttons again with the 50 px step of the script.
--   * The Options screen (options.luac): the categories, the tab, the rows that this client cannot apply, and the key rows of the
--     actions that it does not handle are taken out of the row tables of the script before it builds them (G.optionsHidden*,
--     G.keyHiddenActions). The render scale uses the retail row "Resolution
--     Scaling" with a client label for its item (this client uses the temporal upscaler of Unreal, TSR, not FSR).

local Swc = Swc
local G = {}
Swc.gamemenu = G

-- ------------------------------------------------------------------------------------------------------- the escape menu

-- The store: escapemenu.luac OnInit uses its small frame (EscapeMenuHolderNoEntitelments) when this node is not true. The
-- in-game layout of the script then still shows the store holder (UpdateScreen calls its OnSetVisible) and keeps the wide header,
-- and the small frame of the canvas is shorter than the buttons (the design: the frame ends at 840, Exit to Desktop at 893). The
-- client does what the front-end layout of the script does (UpdateMainMenuEsc: the store holder, the shop label, and the world
-- name off, the header at the small width), and fits the small frame to the last shown button with the bottom margin of the
-- large frame of the canvas (the design: 940 - 893 = 47 px).
Swc.SetData("UIFeatures.g_uiEnableEntitlements", false)
Swc.config["UIFeatures.g_uiEnableEntitlements"] = false

--- The hidden elements of the escape menu (the names of the canvas) and the reason.
G.escapeHidden = {
  ButtonUnstuck = "the backend has no unstuck (LocalPlayerComponentRequestBus RequestUnstuck)",
  ButtonRespawn = "Recall to Inn: the backend has no inn bind, no recall, and no recall cooldown (in a dungeon mode it is the dungeon leave)",
  ButtonFeedback = "an online service (the feedback screen)",
  ButtonHelp = "a web page",
  ButtonNewsletter = "a web page",
  ButtonNewsModal = "an online service (the news)",
  ButtonCompensations = "an online service (the make-good items)",
  ButtonFreeWeekend = "an online service",
  ButtonExternalRewards = "an online service (the entitlements)",
  ButtonConnectToTwitch = "an online service",
}
--- The hidden elements that show in a state of the script: in a mode with IsDungeon the button of Recall to Inn is the dungeon leave
--- ("Leave", @ui_dungeon_leave and the postfix of the mode; its popup Popup_ExitDungeon sends ForfeitArena 926: backend stage QG9e).
G.escapeShownWhen = {
  ButtonRespawn = function(self) return self.isInDungeon == true end,
}
--- The button sections of the escape menu, from the top (a section with no shown button hides with its label).
local SECTIONS = {"GameButtonsContainer", "SupportButtonsContainer", "ExitButtonsContainer"}
--- The step of the buttons of a section (escapemenu.luac UpdateScreen: LOADK 50.0 for each button).
local STEP = 50
--- The elements whose height the script sets from the number of its buttons (UpdateScreen, UpdateMainMenuEsc).
local HEIGHTS = {"EscapeHolderNoEntitlements", "InputBlocker", "EscapeMenuHolder", "WorldNameHolder"}

local function find(root, name)
  if swcui.name(root) == name then
    return root
  end
  for _, child in ipairs(swcui.children(root) or {}) do
    local found = find(child, name)
    if found then
      return found
    end
  end
  return nil
end

--- @return The element of a property of the script (escapemenu.luac: EscapeMenuHolder is the element EscapeMenuHoder) or of a
--- name in the canvas of a script instance (cached).
local function element(self, name)
  local cache = rawget(self, "__swcGameMenu")
  if not cache then
    cache = {ids = {}, undo = nil}
    rawset(self, "__swcGameMenu", cache)
  end
  local id = cache.ids[name]
  if id == nil then
    local property = self.Properties and self.Properties[name]
    id = (type(property) == "userdata" and swcui.exists(property) and property) or find(self.entityId, name) or false
    if not id then
      -- The holder elements are siblings of the Window in some canvases: search from the root of the canvas.
      local top = self.entityId
      while swcui.exists(swcui.parent(top)) do
        top = swcui.parent(top)
      end
      id = find(top, name) or false
    end
    cache.ids[name] = id
  end
  return id or nil
end

--- Sets the values of the last layout of the client back (each change has its old value): the script then sees the canvas
--- as it made it.
local function undo(self)
  local cache = rawget(self, "__swcGameMenu")
  if not (cache and cache.undo) then
    return
  end
  for i = #cache.undo, 1, -1 do
    cache.undo[i]()
  end
  cache.undo = nil
end

local function record(self, fn)
  local cache = rawget(self, "__swcGameMenu")
  cache.undo = cache.undo or {}
  cache.undo[#cache.undo + 1] = fn
end

local function set_enabled(self, id, on)
  local was = UiElementBus.Event.IsEnabled(id)
  if was ~= on then
    UiElementBus.Event.SetIsEnabled(id, on)
    record(self, function() UiElementBus.Event.SetIsEnabled(id, was) end)
  end
end

local function set_y(self, id, y)
  local was = UiTransformBus.Event.GetLocalPositionY(id)
  if math.abs(was - y) > 0.01 then
    UiTransformBus.Event.SetLocalPositionY(id, y)
    record(self, function() UiTransformBus.Event.SetLocalPositionY(id, was) end)
  end
end

local function shrink(self, id, by)
  local was = UiTransform2dBus.Event.GetLocalHeight(id)
  UiTransform2dBus.Event.SetLocalHeight(id, was - by)
  record(self, function() UiTransform2dBus.Event.SetLocalHeight(id, was) end)
end

local function shrink_frame(self, frame, by)
  if type(frame) ~= "table" or type(frame.GetHeight) ~= "function" or not frame.entityId
    or not UiElementBus.Event.IsEnabled(frame.entityId) then
    return
  end
  local was = frame:GetHeight()
  frame:SetHeight(was - by)
  record(self, function() frame:SetHeight(was) end)
end

--- The layout of the client after a layout of the script: the hidden buttons go, the shown buttons of a section take the
--- first places of the buttons that the script showed, the sections below move up, and the frame gets smaller.
--- @return The removed height (pixels).
function G.LayoutEscapeMenu(self)
  local removed = 0
  for _, sectionName in ipairs(SECTIONS) do
    local section = element(self, sectionName)
    if section and UiElementBus.Event.IsEnabled(section) then
      if removed > 0 then
        set_y(self, section, UiTransformBus.Event.GetLocalPositionY(section) - removed)
      end
      local shown, places = {}, {}
      for _, child in ipairs(swcui.children(section) or {}) do
        local name = swcui.name(child) or ""
        if name:find("^Button") and UiElementBus.Event.IsEnabled(child) then
          places[#places + 1] = UiTransformBus.Event.GetLocalPositionY(child)
          if G.escapeHidden[name] and not (G.escapeShownWhen[name] and G.escapeShownWhen[name](self)) then
            set_enabled(self, child, false)
          else
            shown[#shown + 1] = {id = child, y = places[#places]}
          end
        end
      end
      table.sort(places)
      table.sort(shown, function(a, b) return a.y < b.y end)
      if #shown == 0 and #places > 0 then
        -- The whole section goes: the place of the next shown section is the removed height.
        local nextY
        local after = false
        for _, otherName in ipairs(SECTIONS) do
          local other = element(self, otherName)
          if after and other and UiElementBus.Event.IsEnabled(other) and nextY == nil then
            nextY = UiTransformBus.Event.GetLocalPositionY(other) - removed
          end
          after = after or otherName == sectionName
        end
        local y = UiTransformBus.Event.GetLocalPositionY(section)
        set_enabled(self, section, false)
        removed = removed + (nextY and (nextY - y) or #places * STEP)
      else
        for i, button in ipairs(shown) do
          set_y(self, button.id, places[i])
        end
        removed = removed + (#places - #shown) * STEP
      end
    end
  end
  if self.enableSmallFrame then
    G.FitSmallFrame(self)
  elseif removed > 0 then
    for _, name in ipairs(HEIGHTS) do
      local id = element(self, name)
      if id then
        shrink(self, id, removed)
      end
    end
    shrink_frame(self, self.FrameLarge, removed)
  end
  return removed
end

--- The bottom margin of the frame under the last button: the large frame of the canvas and Exit to Desktop at OnInit.
local DEFAULT_MARGIN = 47

--- The small frame (the store is off): the store holder off, the header at the width of the small frame, and the frame and
--- its holders end the margin below the last shown button.
function G.FitSmallFrame(self)
  for _, name in ipairs({"StoreButtonHolder", "ShopLabel", "WorldNameHolder"}) do
    local id = self.Properties and self.Properties[name]
    if id and UiElementBus.Event.IsEnabled(id) then
      set_enabled(self, id, false)
    end
  end
  local header = self.FrameHeader
  local width = self.escapeHolderNoEntitlementsWidthOriginal
  if type(header) == "table" and type(header.GetWidth) == "function" and width and math.abs(header:GetWidth() - width) > 0.5 then
    local was = header:GetWidth()
    header:SetWidth(width)
    record(self, function() header:SetWidth(was) end)
  end
  local bottom
  for _, sectionName in ipairs(SECTIONS) do
    local section = element(self, sectionName)
    if section and UiElementBus.Event.IsEnabled(section) then
      for _, child in ipairs(swcui.children(section) or {}) do
        if (swcui.name(child) or ""):find("^Button") and UiElementBus.Event.IsEnabled(child) then
          local _, _, _, b = swcui.rect(child)
          bottom = math.max(bottom or b, b)
        end
      end
    end
  end
  local frame = self.FrameSmall
  if not (bottom and type(frame) == "table" and frame.entityId) then
    return
  end
  local _, _, _, frameBottom = swcui.rect(frame.entityId)
  local delta = bottom + (rawget(self, "__swcFrameMargin") or DEFAULT_MARGIN) - frameBottom
  if math.abs(delta) < 0.5 then
    return
  end
  for _, name in ipairs(HEIGHTS) do
    local id = element(self, name)
    if id then
      shrink(self, id, -delta)
    end
  end
  local was = frame:GetHeight()
  frame:SetHeight(was + delta)
  record(self, function() frame:SetHeight(was) end)
end

local function wrap_layout(base, name)
  local original = rawget(base, name)
  if type(original) ~= "function" then
    return
  end
  base[name] = function(self, ...)
    undo(self)
    local results = {original(self, ...)}
    G.LayoutEscapeMenu(self)
    return unpack(results)
  end
end

Swc.scriptHooks["lyshineui.escapemenu.escapemenu"] = function(base)
  wrap_layout(base, "UpdateScreen")
  wrap_layout(base, "UpdateMainMenuEsc")
  local init = rawget(base, "OnInit")
  if type(init) == "function" then
    base.OnInit = function(self, ...)
      local results = {init(self, ...)}
      -- The bottom margin of the design: the large frame and the last button of the canvas.
      local large, exit = self.Properties and self.Properties.FrameLarge, element(self, "ButtonExitDesktop")
      if large and exit then
        local _, _, _, frameBottom = swcui.rect(large)
        local _, _, _, buttonBottom = swcui.rect(exit)
        if frameBottom and buttonBottom and frameBottom > buttonBottom then
          rawset(self, "__swcFrameMargin", frameBottom - buttonBottom)
        end
      end
      return unpack(results)
    end
  end
end

-- -------------------------------------------------------------------------------------------------- the Options screen

--- The hidden categories of the settings tab (the text of the category) and the hidden tabs.
G.optionsHiddenCategories = {
  ["@ui_options_controller"] = "no game pad in the device scope of this client",
  ["@ui_options_audio"] = "this client has no sound yet",
  ["@ui_options_social"] = "no chat screen, no voice chat, no invites (no service)",
  ["@ui_options_about"] = "web links",
}
G.optionsHiddenTabs = {
  ["@ui_controllerRemap"] = "no game pad in the device scope of this client",
}
--- The hidden rows: the data node (or the text of a button row with no node) and the reason. A row that this client
--- cannot apply and that no loaded original script reads.
local HIDDEN_ROWS = {
  -- Game
  {"no language but English is converted", "Localization.LanguageList", "Localization.LanguageAudioList"},
  {"this client has no camera shake yet", "Controls.CameraShake"},
  {"the camera uses the values of the camera data", "Controls.ModifiedCurveEnabled", "Controls.PercentCameraZSmoothing"},
  {"no client function", "Controls.SmartcastType", "Controls.AutoTraverse", "Misc.EnableAllyTeamColorTinting",
    "Misc.EnableEnemyTeamColorTinting", "Accessibility.ToggleBlockAimDownSights"},
  {"no tutorials", "PlayerTutorials.ClassificationThreshold", "@ui_reset_tutorials"},
  {"no mounts", "Controls.MountAutoRoadFollow", "Controls.MountDashToggle", "Controls.MountSlowWalk"},
  {"no PvP, groups, raids, or social actions", "Controls.ShowPvpXp", "Controls.ShowInspectHint", "Video.NameplateGroupPriority",
    "Hud.ShowFullRaidForPvpModes", "Controls.TargetedHealingEnabled"},
  {"the target lock of this client has the defaults only", "Controls.CameraLockTargetFollow", "Controls.CameraLockRetarget",
    "Controls.CameraLockStickyLock", "Controls.ForceTargetLockWhenStartingHealing", "Controls.CameraLockSwapToggle",
    "Controls.CameraLockGroupMode", "Controls.TargetOnlyMyGroupInRaid", "Controls.CameraTargetLock",
    "Controls.CameraLockTargetSwitchInterpolationSpeed", "Controls.CameraLockTargetAutoSwitch"},
  {"no such events", "Misc.MetaAchievementPopupsEnabled", "Misc.WarNotificationsEnabled", "Misc.HousingNotificationsEnabled"},
  {"no network service", "Network.BandwidthMode", "Misc.AnalyticsEnabled", "Misc.ExitSurveyEnabled",
    "Misc.NetworkStatusIconsEnabled"},
  -- Keyboard & Mouse
  {"no client function (the client has the retail default behavior)", "Controls.AllowFreeformMovement",
    "Controls.AttackFacingCameraEnabled", "Controls.SelfCorrectingFollowCamEnabledKBam"},
  {"no raids, no turrets", "Controls.SelectRaidMembersByGroupAndHotkey", "Controls.ShowHealingHotkeysInRaidHud",
    "Controls.CameraSensitivityHorizontalTurret", "Controls.CameraSensitivityVerticalTurret"},
  -- Video
  {"no matched value (the look waits for the user)", "Video.TemporalUpscalingSharpness", "Video.Brightness", "Video.Contrast",
    "Video.HdrWhitePoint", "Video.Gamma"},
  {"no client function", "Video.RenderMode", "Video.WaterQuality", "Video.EmoteFx", "Video.EnableViewportScale",
    "Video.CapUnfocusedFPS", "Video.IsDisplayFrameCount", "Video.MotionBlurEnabled"},
  -- Accessibility
  {"no client function", "Accessibility.ColorBlindness", "Accessibility.GENA", "Accessibility.TTS", "Accessibility.TTSVolume",
    "Accessibility.TTSRate", "Accessibility.SpeechToText", "Accessibility.SpellAlignmentVfx", "Accessibility.Rumble",
    "Accessibility.RumbleSensitivity"},
}
G.optionsHiddenRows = {}
for _, group in ipairs(HIDDEN_ROWS) do
  for i = 2, #group do
    G.optionsHiddenRows[group[i]] = group[1]
  end
end

--- The hidden rows of the key page (Keyboard & Mouse Bindings): the first action of the row (bindingData[1].bindingName) and the
--- reason. A row stays when the player controller maps its action (ASwcPlayerController::MapPlayKeys), or a loaded original
--- script or the emulation handles it (the inventory, the map, the character window, the journal, the quick slots, the
--- notifications, the salvage keys).
local HIDDEN_KEYS = {
  {"no client function", "take_screenshot", "ui_visible", "toggleMoveSpeed", "shiftModifier1", "shiftModifier2"},
  {"no PvP spectator camera", "pvp_spectatorcam_next_target", "pvp_spectatorcam_prev_target"},
  {"no emotes and no social actions", "use_emote_slot1", "use_emote_slot2", "use_emote_slot3", "use_emote_slot4",
    "toggleEmoteWindow", "social_align"},
  {"the target lock of this client switches by the mouse move only", "camera_lock_next_target_cw", "camera_lock_next_target_ccw"},
  {"no groups and no ally targets", "self_target", "lifestaff_target_lock", "ally_target_lock_modifier", "lifestaff_next_target_cw",
    "lifestaff_next_target_ccw", "camera_lock_target_group1", "camera_lock_target_group2", "camera_lock_target_group3",
    "camera_lock_target_group4", "camera_lock_target_group5", "camera_lock_group_mode_toggle"},
  {"no gear set storage", "toggleEquipmentSetWindow", "equipLoadout1", "equipLoadout2", "equipLoadout3", "equipLoadout4",
    "equipLoadout5", "equipLoadout6", "equipLoadout7", "equipLoadout8", "equipLoadout9", "gss_menu"},
  {"no fishing and no music performance", "fishing_activate", "fishing_primary", "fishing_reset", "musicalactions_activate"},
  {"no mounts", "mount_summon", "mount_dash", "mount_jump", "mount_stop"},
  {"no season rewards, company, war scoreboard, social window, army, camp, or augments", "toggleSeasonsRewardsComponent",
    "toggleGuildComponent", "toggleWarboardInGame", "toggleSocialWindow", "toggleRaidWindow", "makeCampOn", "toggleTokenWindow"},
  {"no chat screen, voice chat, or narration", "toggleChatComponent", "toggleChatComponentSlash", "toggleMicrophoneOn", "toggleGENA"},
  {"no PvP flag, duels, activity wheel, pings, or ready check", "togglePvpFlag", "duel_forfeit", "activity_menu", "target_tag",
    "target_tag_modifier", "target_tag_selection", "target_tag_shout", "target_tag_move", "target_tag_attack", "target_tag_someone",
    "target_tag_need_help", "target_tag_need_healing", "target_tag_loot", "target_tag_repair", "target_tag_defend",
    "target_tag_caution", "target_tag_danger", "ready_up_accept"},
}
G.keyHiddenActions = {}
for _, group in ipairs(HIDDEN_KEYS) do
  for i = 2, #group do
    G.keyHiddenActions[group[i]] = group[1]
  end
end
--- The two columns of the key page.
G.keyLists = {"KeyBindingListItemText1", "KeyBindingListItemText2"}

--- Takes the rows of hidden actions out of a column of the key page (in place); a title with no row after it goes.
--- @return The number of rows that went.
local function filter_key_list(list)
  local removed, kept = 0, {}
  for _, row in ipairs(list) do
    local first = type(row) == "table" and type(row.bindingData) == "table" and row.bindingData[1]
    if first and G.keyHiddenActions[first.bindingName] then
      removed = removed + 1
    else
      kept[#kept + 1] = row
    end
  end
  local final = {}
  for i, row in ipairs(kept) do
    local nextRow = kept[i + 1]
    if not (row.title and (nextRow == nil or nextRow.title)) then
      final[#final + 1] = row
    end
  end
  for i = #list, 1, -1 do
    list[i] = nil
  end
  for i, row in ipairs(final) do
    list[i] = row
  end
  return removed
end

--- The row lists of options.luac (swc/options.lua has the same names).
G.optionLists = {"GameplayListItemData", "KBamListItemData", "ControllerListItemData", "VisualListItemData", "AudioListItemData",
  "SocialListItemData", "AccessibilityListItemData", "AboutListItemData", "TwitchListItemData", "CrossplayListItemData"}

local function row_hidden(row)
  return row.dataNode and G.optionsHiddenRows[row.dataNode] or (not row.dataNode and row.text and G.optionsHiddenRows[row.text])
end

--- Takes the hidden rows out of a list in place (a group with no row left goes; a subheader with no row after it goes).
--- @return The number of rows that went.
local function filter_list(list)
  local removed = 0
  local out = {}
  for _, row in ipairs(list) do
    if type(row) == "table" then
      if row.text == nil and row.subheader == nil then
        -- A group (the rows under "More Options"): its rows are the array part.
        local kept = {}
        for _, sub in ipairs(row) do
          if type(sub) == "table" and row_hidden(sub) then
            removed = removed + 1
          else
            kept[#kept + 1] = sub
          end
        end
        for i = #row, 1, -1 do
          row[i] = nil
        end
        for i, sub in ipairs(kept) do
          row[i] = sub
        end
        if #kept > 0 then
          out[#out + 1] = row
        end
      elseif row_hidden(row) then
        removed = removed + 1
      else
        out[#out + 1] = row
      end
    else
      out[#out + 1] = row
    end
  end
  -- A subheader followed by another subheader or by the end has no row.
  local final = {}
  for i, row in ipairs(out) do
    local nextRow = out[i + 1]
    if not (type(row) == "table" and row.subheader ~= nil and row.text == nil
      and (nextRow == nil or (type(nextRow) == "table" and nextRow.subheader ~= nil and nextRow.text == nil))) then
      final[#final + 1] = row
    end
  end
  for i = #list, 1, -1 do
    list[i] = nil
  end
  for i, row in ipairs(final) do
    list[i] = row
  end
  return removed
end

local function filter_entries(list, hidden)
  if type(list) ~= "table" then
    return 0
  end
  local removed = 0
  for i = #list, 1, -1 do
    if type(list[i]) == "table" and hidden[list[i].text] then
      table.remove(list, i)
      removed = removed + 1
    end
  end
  return removed
end

--- The client labels of the items of the render scale (this client uses TSR, the temporal upscaler of Unreal; the retail text of
--- the item is "AMD FSR 3.1"). The labels are in this code; the localization of the game does not change.
G.upscalerLabel = "Temporal Super Resolution"
G.upscalerNativeLabel = "Native"

Swc.scriptHooks["lyshineui.options.options"] = function(base)
  local init = rawget(base, "OnInit")
  if type(init) == "function" then
    base.OnInit = function(self, ...)
      local results = {init(self, ...)}
      for _, item in ipairs(self.TemporalUpscalingTypeData or {}) do
        if item.text == "@ui_options_temporalUpscaling_fsr" then
          item.text = G.upscalerLabel
        end
      end
      for _, item in ipairs(self.FsrTemporalUpscalingModeData or {}) do
        if item.text == "@ui_options_temporalUpscaling_native_fsr" then
          item.text = G.upscalerNativeLabel
        end
      end
      return unpack(results)
    end
  end
  -- SetTemporalUpscalingModeData reads the selected item of the Mode drop-down when the type is not Off. With the default of
  -- this client (TSR) it runs at the first show of the screen, before the drop-down has a selected item (the retail default
  -- Off skips it): then the row keeps the mode list of its row data (the same list) and the call does nothing.
  local modeData = rawget(base, "SetTemporalUpscalingModeData")
  if type(modeData) == "function" then
    base.SetTemporalUpscalingModeData = function(self, ...)
      local mode = self.temporalUpscalingModeEntity
      if mode and type(mode.GetSelectedItemData) == "function" and mode:GetSelectedItemData() == nil then
        return
      end
      return modeData(self, ...)
    end
  end
  local build = rawget(base, "TryLoadAndBuildScreen")
  if type(build) == "function" then
    base.TryLoadAndBuildScreen = function(self, ...)
      if not self.isUiSpawned and not rawget(self, "__swcFiltered") then
        rawset(self, "__swcFiltered", true)
        local categories = filter_entries(self.MenuButtonData, G.optionsHiddenCategories)
        local tabs = filter_entries(self.TabbedListHeaderData, G.optionsHiddenTabs)
        local rows = 0
        for _, name in ipairs(G.optionLists) do
          if type(self[name]) == "table" then
            rows = rows + filter_list(self[name])
          end
        end
        local keys = 0
        for _, name in ipairs(G.keyLists) do
          if type(self[name]) == "table" then
            keys = keys + filter_key_list(self[name])
          end
        end
        Swc.Note(string.format("game menu: the options screen without %d categories, %d tabs, %d rows and %d key rows of features "
          .. "that this client does not have", categories, tabs, rows, keys))
      end
      return build(self, ...)
    end
  end
end

-- ------------------------------------------------------------------------------------------------------------- the NavBar

-- The NavBar (lyshineui/navbar, navbar.luac): the retail menu bar on top of the screens of the states Esc, Map, Objectives
-- (the journal), Skills, and Options (gamedata/uistates.json). Display only (the decision of 2026-10-25):
--   * The tabs: only the screens that this client has: JOURNAL, CHARACTER (the character window), MAP, and the Game Menu gear
--     (the escape menu). The other entries of NavButtonData (Company, Mount, Modes, Activities, the season, Store, the group
--     finder, the social button) go before SetNavMenu builds the tabs. The bar stays at the left, as retail (the 2023 tab list
--     gives the spacers no width; evidence/MVP-G/side-mastery.png: the retail bar starts at the left with its social icon).
--   * The tab CHARACTER: the script turns the Skills tab on when UIFeatures.g_uiEnableAttributes or g_uiEnableTradeskills is on
--     (its observers; swc/skills.lua sets both, as the retail window has these tabs).
--   * No soft load: with the on-demand loading, the script goes to the state NavBar, queues the state of a tab, and sets it
--     when DynamicCanvasLoaderBus PrepareState says that its canvases are ready. This client loads its screens at the start
--     (HudScreens): statesToSoftLoad is empty, and a tab sets its state at once.
--   * The keys: the NavBar does not take ui_cancel, toggleMenuComponent, or toggleJournalComponent. The escape rule and the
--     journal rule of the emulation stay (swc/actions.lua; open point O12). The retail Escape logic of navbar.luac (for each
--     state, popup, and screen) is a later task (full parity).

--- The kept entries of the NavBar (their stateName: the CRC-32 of the state).
G.navKeptStates = {
  [3576764016] = "CHARACTER (Skills)",
  [1823500652] = "JOURNAL (Objectives)",
  [2477632187] = "MAP",
  [1643432462] = "Game Menu (Esc)",
}
--- The actions that the emulation keeps (the NavBar does not take them).
G.navIgnoredActions = {ui_cancel = true, toggleMenuComponent = true, toggleJournalComponent = true}

-- No season (the seasons tab goes): OnInit reads the display name of the current season.
Swc.Define("SeasonsRewardsSeasonDataManagerBus", "Broadcast", {
  GetSeasonData = function() return {displayName = ""} end,
})

--- Takes the entries of the features that this client does not have out of NavButtonData (in place).
--- @return The number of entries that stay.
function G.FilterNavButtons(self)
  local list = self.NavButtonData
  if type(list) ~= "table" then
    return 0
  end
  local kept = {}
  for _, entry in ipairs(list) do
    if entry.isSpacer or (entry.stateName and G.navKeptStates[entry.stateName]) then
      kept[#kept + 1] = entry
    end
  end
  for i = #list, 1, -1 do
    list[i] = nil
  end
  for i, entry in ipairs(kept) do
    list[i] = entry
  end
  return #kept
end

Swc.scriptHooks["lyshineui.navbar.navbar"] = function(base)
  local init = rawget(base, "OnInit")
  if type(init) == "function" then
    base.OnInit = function(self, ...)
      -- BeginSwitchingState (a press of a tab) reads statesToSoftLoad; OnInit makes it.
      local results = {init(self, ...)}
      self.statesToSoftLoad = {}
      return unpack(results)
    end
  end
  local setNavMenu = rawget(base, "SetNavMenu")
  if type(setNavMenu) == "function" then
    base.SetNavMenu = function(self, ...)
      if not rawget(self, "__swcNavFiltered") then
        rawset(self, "__swcNavFiltered", true)
        local n = G.FilterNavButtons(self)
        Swc.Note(string.format("game menu: the NavBar with %d entries (JOURNAL, CHARACTER, MAP, Game Menu, and the spacers)", n))
      end
      return setNavMenu(self, ...)
    end
  end
  local onCryAction = rawget(base, "OnCryAction")
  if type(onCryAction) == "function" then
    base.OnCryAction = function(self, action, ...)
      if G.navIgnoredActions[action] then
        return nil
      end
      return onCryAction(self, action, ...)
    end
  end
end

-- ------------------------------------------------------------------------- the hints and the journal tabs of absent features
-- The cleanup rule of the user ("removing unused features", 2026-10-25): a screen shows no hint and no category of a feature that this
-- client does not have.

--- The help bar (helpbar.luac PushToStack(device, list, id)): a hint whose action is a hidden action of the key page
--- (G.keyHiddenActions, with its reason there) goes, for example the ping wheel hint of the map (mapmenu.luac SetHelpBar:
--- target_tag_selection, "@ui_ping_wheel"; this client has no pings).
function G.FilterHints(list)
  if type(list) ~= "table" then
    return list
  end
  local kept = {}
  for _, hint in ipairs(list) do
    if not (type(hint) == "table" and hint.action and G.keyHiddenActions[hint.action]) then
      kept[#kept + 1] = hint
    end
  end
  return kept
end
Swc.scriptHooks["lyshineui.helpbar.helpbar"] = function(base)
  local push = rawget(base, "PushToStack")
  if type(push) == "function" then
    base.PushToStack = function(self, device, list, id, ...)
      return push(self, device, G.FilterHints(list), id, ...)
    end
  end
end

--- The tabs of the journal (objectivestab.luac OnInit builds 14 tabs and gives them to TabbedList SetListData). A tab whose objective
--- types the backend cannot give is hidden (the TabbedList rule isVisible false: the next tabs move up; the script keeps each tab
--- index). The kept tabs: Main Story (MainStoryQuest), Faction Story (FactionStory_*), Skill Progression, Mount Quest, Side Stories
--- (Journey, Quest). Mount Quest stays (the decision of the orchestrator, 2026-10-25: the keep rule wins): the backend supports and
--- offers 9 MountUnlock rows (the support manifest of backend 1.8f), so the player can see and finish them, also with no mount in this
--- client. The text key of each hidden tab, with its
--- reason, and its list of the content panel (the property of objectivestab.luac: its header, its count, and its Auto-Pin toggle).
G.journalHiddenTabs = {
  ["@objective_location"] = {list = "LocationList", reason = "the POI, darkness, arena, dungeon, and trial objectives: no backend source"},
  ["@objective_affinity"] = {list = "AffinityList", reason = "Favor (Affinity): no backend source"},
  ["@objective_seasonquest"] = {list = "SeasonQuestList", reason = "Season Story: no season in this client"},
  ["@objective_epicequipment"] = {list = "EpicEquipmentList", reason = "Epic Equipment: no backend source"},
  ["@objective_mission"] = {list = "MissionList", reason = "Faction Missions: no faction and no mission board"},
  ["@objective_event"] = {list = "EventList", reason = "Event Quests: no event in this client"},
  ["@objective_townproject"] = {list = "CommunityList", reason = "Town Projects: no community goal contract (backend roadmap 5.4)"},
  ["@objective_recipes"] = {list = "RecipeList", reason = "Recipes (Crafting objectives): no backend source"},
  ["@objective_artifacts"] = {list = "ArtifactList",
                              reason = "Artifacts: no supported artifact objective (backend support manifest 1.8f: 0 of 73)"},
}
Swc.scriptHooks["lyshineui.objectives.journal.objectivestab"] = function(base)
  local init = rawget(base, "OnInit")
  if type(init) == "function" then
    base.OnInit = function(self, ...)
      local results = {init(self, ...)}
      -- The list and its header row (the parent of <Name>TitleText: the title, the count, the dot, the Auto-Pin toggle).
      for _, tab in pairs(G.journalHiddenTabs) do
        local name = tab.list:gsub("List$", "")
        local list = self.Properties and self.Properties[tab.list]
        local title = self.Properties and self.Properties[name .. "TitleText"]
        -- A disabled child keeps its place in a LyShine layout: the height 0 too (the rule of the script for its MSQMessage).
        for _, id in ipairs({list or false, title and UiElementBus.Event.GetParent(title) or false}) do
          if id then
            UiElementBus.Event.SetIsEnabled(id, false)
            UiLayoutCellBus.Event.SetTargetHeight(id, 0)
            UiLayoutCellBus.Event.SetMinHeight(id, 0)
          end
        end
      end
      return unpack(results)
    end
  end
end
Swc.scriptHooks["lyshineui.slices.tabbedlist"] = function(base)
  local set = rawget(base, "SetListData")
  if type(set) == "function" then
    base.SetListData = function(self, data, ...)
      local hidden = false
      if type(data) == "table" then
        for _, entry in ipairs(data) do
          if type(entry) == "table" and G.journalHiddenTabs[entry.text] then
            entry.isVisible = false
            hidden = true
          end
        end
      end
      -- The rule of the list for hidden buttons (EnableHiddenButtons: each next button moves up by the place of a hidden one).
      if hidden and self.Properties then
        self.Properties.EnableHiddenButtons = true
      end
      return set(self, data, ...)
    end
  end
end

-- The loading screen tips (AST-244 item 5): the tips of a feature that this client does not have go (the cleanup rule of the user;
-- docs/UI.md "Absent features"). The prefixes of the tip keys (loadingscreenmanager.luac ConstructTip, the key after "@tip_") with
-- the reason of each. The tips of the places that this client cannot load (the dungeons, the trials, the arenas) stay: their pools
-- show only at a travel to such a place.
local HIDDEN_TIPS = {
  {"no housing", "overworld_housing_"},
  {"no company", "overworld_company_"},
  {"no trading post and no trade between players", "overworld_tradingpost_", "overworld_social_p2ptrading"},
  {"no social window", "overworld_social_donotdisturb"},
  {"no PvP flag, duels, PvP rewards track, or PvP missions", "overworld_combat_pvpflagging", "overworld_combat_duels",
    "overworld_pvprewardstrack_", "overworld_factions_pvpmissions"},
  {"no faction missions (the journal tab Faction Missions is hidden)", "overworld_factions_pvemissions",
    "overworld_factions_huntmissions"},
  {"no war, invasion, or Outpost Rush", "war_", "invasion_", "outpostrush_", "overworld_gamemodes_outpostrush"},
  {"no pings", "overworld_pings_"},
  {"no fishing", "overworld_fishing_"},
  {"no camp (makeCampOn is hidden)", "overworld_camping_"},
}
G.tipHidden = function(key)
  local name = string.lower(tostring(key or "")):gsub("^@tip_", "")
  for _, group in ipairs(HIDDEN_TIPS) do
    for i = 2, #group do
      if name:sub(1, #group[i]) == group[i] then
        return group[1]
      end
    end
  end
  return nil
end

--- Takes the tips of absent features out of a tip list (in place, the order kept). A list keeps its tips when no tip would stay
--- (CreateTipQueue stops with "tip queue is empty").
local function filter_tips(pool)
  if type(pool) ~= "table" or type(pool.tips) ~= "table" or pool.swcTipsFiltered then
    return
  end
  pool.swcTipsFiltered = true
  local kept = {}
  for _, tip in ipairs(pool.tips) do
    if not G.tipHidden(tip.localizationKey) then
      kept[#kept + 1] = tip
    end
  end
  if #kept > 0 and #kept < #pool.tips then
    for i = #pool.tips, 1, -1 do
      pool.tips[i] = nil
    end
    for i, tip in ipairs(kept) do
      pool.tips[i] = tip
    end
  end
end

Swc.scriptHooks["lyshineui.loadingscreen.loadingscreenmanager"] = function(base)
  local create = rawget(base, "CreateTipQueue")
  if type(create) == "function" then
    base.CreateTipQueue = function(self, pool, destination, ...)
      filter_tips(pool)
      filter_tips(destination)
      return create(self, pool, destination, ...)
    end
  end
end
