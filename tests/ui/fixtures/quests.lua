-- The quest fixture (Linear AST-236; docs/QUESTS.md): the original tracker of the HUD (ObjectivesHud), the journal (JournalScreen, J), and the
-- banners (Banner) with the model of swc/quests.lua. The state is the start state of a new character of the backend (stage 1.8a, live check
-- L1): "Soul Survivor" (06_getoffbeach) active and tracked, the task elements of its tree in the index order. With the state alone no pin
-- shows; after the pin readiness the tracker shows the quest and its task "Talk to Aelstrom on the beach". J opens the journal; the unpin and
-- the pin send the pinned list (1458). A second state with an empty list removes the quest (the banner "Objective Failed" path).
-- Pictures: quests-hud, quests-journal.
local HUD = 2702338936
local hud = screen("LyShineUI/Objectives/Hud/ObjectivesHud.uicanvas", 12, true)
local banner = screen("LyShineUI/Banner/Banner.uicanvas", 30)
local journal = screen("LyShineUI/Objectives/Journal/JournalScreen.uicanvas", 40)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(1, 0)
-- The world entry: the loading screen ends (the banner queue waits for it: banner.luac OnLoadingScreenDismissed).
Swc.HostLoading(true, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.3)
Swc.HostLoading(false, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.5)

local function requests()
  local out = {}
  for _ = 1, 8 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local function texts_of(canvas)
  local out = {}
  local function walk(id, depth)
    if depth > 24 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(canvas, "root"), 0)
  return table.concat(out, " | ")
end
local function has(canvas, text)
  return string.find(string.lower(texts_of(canvas)), string.lower(text), 1, true) ~= nil
end

-- The start state of 06_getoffbeach (protocol notes 4.53): instance, the objective CRC E4D66020, territory 6.
local INSTANCE = "1311768467294899695"
local states = {{2, 1}, {3, 1}, {2, 1}, {2, 1}, {3, 4}, {2, 1}, {2, 1}, {1, 0}, {1, 0}, {1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 0}, {1, 0}, {1, 3}}
local function start_state()
  local row = Swc.quests.D().objectives["06_getoffbeach"]
  local lines = {"A " .. INSTANCE .. " " .. row.crc .. " 6 0"}
  for _, entry in ipairs(row.tree) do
    local index = entry[1]
    local task = Swc.quests.D().tasks[entry[2]]
    lines[#lines + 1] = string.format("T %s %d %d %d %d", INSTANCE, index, task.crc, states[index + 1][2], states[index + 1][1])
  end
  lines[#lines + 1] = "K " .. INSTANCE
  return table.concat(lines, "\n")
end
check(Swc.quests.D().objectives["06_getoffbeach"] ~= nil, "quests.json has 06_getoffbeach")
Swc.HostQuests(start_state())
frames(1.5)
note("the HUD before the readiness: " .. texts_of(hud))
check(not has(hud, "Talk to Aelstrom"), "with the Objectives state alone the tracker shows no pin (live check L1)")
note("the banner: " .. texts_of(banner))
check(has(banner, "Soul Survivor"), "the banner of the new quest shows Soul Survivor")
Swc.HostQuestPinReady(true)
frames(2.5)
note("the HUD after the readiness: " .. texts_of(hud))
check(has(hud, "Soul Survivor"), "the tracker shows Soul Survivor")
check(has(hud, "Talk to Aelstrom on the beach"), "the tracker shows the task Talk to Aelstrom on the beach")
picture("quests-hud")

-- J: the journal.
requests()
key("j")
frames(1.5)
note("the journal: " .. texts_of(journal))
check(swcui.canvas_get(journal, "enabled"), "J opens the journal")
check(has(journal, "Soul Survivor"), "the journal shows Soul Survivor")
picture("quests-journal")
note("the drive point of Unpin: " .. Swc.HostPointOf("text:Unpin", 0.5, 0.5))
check(Swc.HostPointOf("text:Unpin", 0.5, 0.5) ~= "", "the journal shows the button Unpin (the drive target text:Unpin)")
equal(ObjectivesComponentRequestBus.Event.GetObjectiveSortingPos(Swc.ids.player, ObjectivesComponentRequestBus.Event.GetObjectives(Swc.ids.player)[1]), 0,
  "the one pin has the place 0: the icon number 1 (retail I2-new-character-hud.png)")
key("escape")
frames(1.0)

-- Unpin and pin (1458).
requests()
local id = ObjectivesComponentRequestBus.Event.GetObjectives(Swc.ids.player)[1]
ObjectivesComponentRequestBus.Event.SetObjectiveTracked(Swc.ids.player, id, false)
frames(1.0)
local r = requests()
equal(r[1], "quest pins ", "unpin: the empty pinned list")
check(not has(hud, "Talk to Aelstrom"), "unpin: the tracker hides the quest")
ObjectivesComponentRequestBus.Event.SetObjectiveTracked(Swc.ids.player, id, true)
frames(1.0)
r = requests()
equal(r[1], "quest pins " .. INSTANCE, "pin: the list with the instance")
check(has(hud, "Talk to Aelstrom"), "pin: the tracker shows the quest again")

-- An empty state: the quest goes away.
Swc.HostQuests("")
frames(1.5)
check(not has(hud, "Soul Survivor"), "an empty state: no quest on the tracker")
