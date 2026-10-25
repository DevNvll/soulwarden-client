-- The unused features fixture (the cleanup rule of the user, 2026-10-25; docs/UI.md "Absent features"): the journal hides the tabs whose
-- objective types the backend cannot give (G.journalHiddenTabs: Location, Favor, Season Story, Epic Equipment, Faction Missions, Event
-- Quests, Town Projects, Recipes, Artifacts) and keeps Main Story, Faction Story, Skill Progression, Mount Quest, and Side Stories; the
-- quest of a new character still shows. The help bar takes no hint of a hidden action of the key page (the ping wheel of the map).
-- Pictures: unusedfeatures-journal.
local HUD = 2702338936
screen("LyShineUI/Objectives/Hud/ObjectivesHud.uicanvas", 12, true)
local journal = screen("LyShineUI/Objectives/Journal/JournalScreen.uicanvas", 40)
local helpbar = screen("LyShineUI/HelpBar/HelpBar.uicanvas", 30, true)
Swc.HostProgression(1, 0)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
frames(0.5)

local function texts_of(canvas)
  local out = {}
  local function walk(id, depth)
    if depth > 30 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(canvas, "root"), 0)
  return table.concat(out, " | ")
end

-- The start quest of a new character (06_getoffbeach, tracked).
local row = Swc.quests.D().objectives["06_getoffbeach"]
local lines = {"A 77 " .. row.crc .. " 6 0"}
for _, entry in ipairs(row.tree) do
  local task = Swc.quests.D().tasks[entry[2]]
  lines[#lines + 1] = string.format("T 77 %d %d 0 %d", entry[1], task.crc, entry[1] == row.root and 1 or 0)
end
lines[#lines + 1] = "K 77"
Swc.HostQuests(table.concat(lines, "\n"))
Swc.HostQuestPinReady(true)
frames(1.0)
key("j")
frames(1.5)
check(swcui.canvas_get(journal, "enabled"), "J opens the journal")
local text = texts_of(journal)
note("the journal: " .. text)
for _, hidden in ipairs({"Season Story", "Favor", "Epic Equipment", "Faction Missions", "Event Quests", "Town Projects", "Recipes",
                         "Artifacts", "Location"}) do
  check(not text:find(hidden, 1, true), "the journal hides the tab " .. hidden)
end
for _, kept in ipairs({"Main Story", "Faction Story", "Skill Progression", "Mount Quest", "Side Stories"}) do
  check(text:find(kept, 1, true) ~= nil, "the journal keeps the tab " .. kept)
end
check(text:find("Soul Survivor", 1, true) ~= nil, "the journal shows the quest of the new character")
picture("unusedfeatures-journal")
key("escape")
frames(0.5)

-- The help bar: a hint of a hidden action of the key page goes, the other hints stay.
local list = {{action = "target_tag_selection", label = "@ui_ping_wheel", actionMap = "ping", showHold = true},
              {action = "centerMap", label = "@ui_center_hint", actionMap = "ui"}}
DynamicBus.HelpBar.Broadcast.PushToStack(eAID_KeyboardMouse, list, "FixtureMap")
frames(0.3)
local stack
for _, l in pairs(Swc.components) do
  for _, inst in ipairs(l) do
    if (inst.__swcScript or ""):find("helpbar/helpbar.luac", 1, true) and inst.helpBarStack then stack = inst.helpBarStack end
  end
end
local pushed = stack and stack.FixtureMap and stack.FixtureMap[eAID_KeyboardMouse]
check(pushed ~= nil and #pushed == 1 and pushed[1].action == "centerMap", "the help bar: the ping wheel hint goes, the center hint stays")
DynamicBus.HelpBar.Broadcast.PopFromStack("FixtureMap")
equal(Swc.errorCount or 0, 0, "no Lua error")
