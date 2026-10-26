-- The skills fixture (stage MVP-G, Linear AST-222): the original character window (skills.luac, the weapon mastery and the trade
-- skills) with the owner states of a level 70 character. Pictures: skills-open.
local HUD, SKILLS = 2702338936, 3576764016
local skills = screen("LyShineUI/Skills/Skills.uicanvas", 20)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
-- The skills of a copy of the user's level 70 character (the ids of the database; no name): four weapons at rank 19 with each
-- point spent (19 nodes each), no sword mastery, the bars of the four weapons.
local C = {
  ranks = "928006727 0 2500\n1725414313 0 7\n2169986235 19 0\n3354038264 19 0\n3398787834 2 44\n3886728187 19 0\n4182525519 19 0",
  pools = "3354038264 0\n3886728187 0\n2169986235 0\n4182525519 0\n3907802902 0",
  nodes = "241886633 458703051 573361485 677780538 1109752046 1428946395 1817210973 1892136081 2111309194 2463544917 2653055768 2761217902 3046152056 3139804407 3410978936 3420743941 3695308109 3922650411 4116259303 384602941 575751406 1016658759 1062415968 1088631440 1114424783 1304533711 1381290042 1517298426 1821999461 2044902798 2472572131 2789628405 3291870509 3511089424 3589641506 3864800571 3981347643 4009920797 209669980 336113952 379137413 494822698 808042847 1511748746 1735307834 1937720331 2174212507 2551520552 2618549987 2750140556 2917260377 3112882963 3369167478 3500554636 3698348449 3905641841 4013446401 77680433 1009807133 1018832900 1131353276 1616469446 1748062814 2145730489 2515767991 2703570308 2727613963 2796083179 3019916747 3096281108 3169684558 3217210307 3841260338 4044224935 4248586536 4268742667",
  bars = {{2169986235, 0, 2463544917}, {2169986235, 1, 241886633}, {2169986235, 2, 3046152056}, {3354038264, 0, 3511089424}, {3354038264, 1, 1088631440}, {3354038264, 2, 1114424783}, {3886728187, 0, 1937720331}, {3886728187, 1, 2917260377}, {3886728187, 2, 2750140556}, {4182525519, 0, 4248586536}, {4182525519, 1, 4268742667}, {4182525519, 2, 1009807133}},
}
-- The attributes in the order of CharacterAttributeType (CON, FOC, STR, DEX, INT): STR 50 DEX 456 INT 5 FOC 5 CON 19.
Swc.HostAttributes(19, 5, 50, 456, 5, 0, 0, 0, 0, 0, 14, 0, 45, 451, 0, 0)
Swc.HostRanks(C.ranks)
Swc.HostPointPools(C.pools)
Swc.HostLearnedNodes(C.nodes)
for _, b in ipairs(C.bars) do
  local bar = Swc.player.bars[b[1]] or {0, 0, 0}
  bar[b[2] + 1] = b[3]
  Swc.player.bars[b[1]] = bar
end
frames(0.3)
key("k")
frames(1.5)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), SKILLS, "K opens the state Skills")
check(swcui.canvas_get(skills, "enabled"), "the skills screen shows")
picture("skills-open")
local root = swcui.canvas_get(skills, "root")

-- The weapon mastery tab (skills.luac OpenWeaponMastery through its own dynamic bus).
DynamicBus.SkillsBus.Broadcast.OpenWeaponMastery()
frames(1.5)
picture("skills-mastery")

-- An element of the canvas whose text is a value (enabled in the hierarchy).
local function find_text(parent, text, depth)
  depth = depth or 0
  if depth > 30 then return nil end
  if swcui.has(parent, "text") and swcui.get(parent, "text.text") == text and swcui.enabled_in_hierarchy(parent) then
    return parent
  end
  for _, c in ipairs(swcui.children(parent)) do
    local found = find_text(c, text, depth + 1)
    if found then return found end
  end
end

-- The trees of the five weapons: a click on the row of the weapon opens its tree; Escape goes back to the list.
local function learned_in(tableName)
  local n = 0
  for _, node in ipairs(swcui.load_json("gamedata/skilltrees.json").tables[tableName].nodes) do
    if Swc.skills.learned[node.crc] then n = n + 1 end
  end
  return n
end
for _, w in ipairs({{"Bow", "BowAbilityTable", 19}, {"Great Axe", "GreatAxeAbilityTable", 19}, {"Rapier", "RapierAbilityTable", 19},
                    {"War Hammer", "WarHammerAbilityTable", 19}, {"Sword and Shield", "SwordAbilityTable", 0}}) do
  local row = find_text(root, w[1])
  check(row ~= nil, "the weapon mastery tab has the row " .. w[1])
  if row then
    click(row, 0.5, 0.5)
    frames(1.5)
    equal(learned_in(w[2]), w[3], "the tree " .. w[1] .. " has the learned nodes of the database")
    picture("skills-" .. string.lower(string.gsub(w[1], " ", "-")))
    key("escape")
    frames(1.0)
    equal(LyShineManagerBus.Broadcast.GetCurrentState(), SKILLS, "Escape goes back to the list (the state Skills stays)")
  end
end

-- A bar change: the character has three learned active abilities for each weapon, all on the bars; the edit is a move: the bow
-- ability of place 1 goes to place 3 (it leaves place 1; the backend rule of 2701). The request 2701 with the table index 25 and
-- the bar of the client at once.
local bowTable = swcui.load_json("gamedata/skilltrees.json").tables.BowAbilityTable
local moved = Swc.player.bars[bowTable.crc][1]
Swc.skills.requests = {}
local player = LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.HudComponent.PlayerEntityId")
CharacterAbilityRequestBus.Event.RequestChangeMappedAbility(player, CharacterAbilityRequestBus.Event.GetSelectedTableIndexFromTableName(player, bowTable.crc), 2, moved)
equal(Swc.skills.requests[1], string.format("bar 25 2 %d", moved), "the bar change asks for 2701 with the table index 25 (bow), place 2")
equal(Swc.player.bars[bowTable.crc][3], moved, "the bar of the client has the ability at place 3")
check(Swc.player.bars[bowTable.crc][1] == nil, "the ability left place 1")

-- The trade skills tab (the rank screen of the trade skills).
DynamicBus.SkillsBus.Broadcast.OpenTradeSkills()
frames(1.5)
picture("skills-trade")
check((swcui.get(find_text(root, "Trade Skill Level Cap: 250") or 0, "text.text") or "") ~= "", "the trade skill cap is 250 (the expansion)")
-- The names of the elements of the drive (the tabs and a weapon row).
local function chain(id)
  local names = {}
  while id and #names < 6 do names[#names + 1] = swcui.name(id) or "?"; id = swcui.parent(id) end
  return table.concat(names, " < ")
end
for _, text in ipairs({"WEAPON MASTERY", "TRADE SKILLS", "Weapon Mastery", "Trade Skills"}) do
  local id = find_text(root, text)
  if id then note("drive name: " .. text .. ": " .. chain(id)) end
end
DynamicBus.SkillsBus.Broadcast.OpenWeaponMastery()
frames(1.0)
local bowRow = find_text(root, "Bow")
if bowRow then note("drive name: Bow: " .. chain(bowRow)) end
-- The drive target of a tree node: the bow tree, the node of Penetrating Shot.
click(bowRow, 0.5, 0.5)
frames(1.5)
local at = Swc.HostPointOf("node:Ability_Bow_PenetratingShot")
note("drive point node:Ability_Bow_PenetratingShot: " .. at)
check(at ~= "", "the drive finds the tree node of an ability")
-- The bar flow of the retail window: a click on the third place of the bar opens the list of the abilities (AbilitySelection).
local window
for element, instances in pairs(Swc.components) do
  for _, instance in ipairs(instances) do
    if string.find(instance.__swcScript or "", "masterytreewindow", 1, true) then window = instance end
  end
end
check(window ~= nil, "the tree window script")
local slot3 = window and window.Properties.AbilitySlot3
note("slot3 element: " .. tostring(slot3 and swcui.name(slot3)))
if slot3 then
  click(slot3, 0.5, 0.5)
  frames(1.0)
  picture("skills-bow-selection")
  local sel = window.Properties.AbilitySelection
  local names = {}
  local function walk(id, depth)
    if depth > 6 then return end
    if swcui.enabled_in_hierarchy(id) then names[#names + 1] = (swcui.name(id) or "?") end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  if sel then walk(sel, 0) end
  note("selection elements: " .. table.concat(names, ","))
end
local button = Swc.HostPointOf("ability:Ability_Bow_RapidShot")
note("drive point ability:Ability_Bow_RapidShot: " .. button)
check(button ~= "", "the drive finds the button of an ability in the list of a place")
Swc.skills.requests = {}
local bx, by = button:match("([%d.]+) ([%d.]+)")
Swc.HostInput("move", tonumber(bx), tonumber(by))
Swc.HostInput("button", "left", true)
frames(0.05)
Swc.HostInput("button", "left", false)
frames(1.0)
picture("skills-bow-chosen")
equal(Swc.skills.requests[1], string.format("bar 25 2 %d", swcui.crc32("ability_bow_rapidshot")),
  "a click on Rapid Shot in the list of place 3 asks for 2701 (table index 25, place 2)")

-- The bio tab (user test 2026-10-26, 8d): bio.luac SetupBonuses reads the territory of the player and the owned houses
-- (PlayerHousingClientRequestBus GetOwnedHouseData: an empty list, no housing in the backend; the stub nil gave "attempt to get length
-- of a nil value" at each open of the tab). The territory: Everfall (the ClaimKey of the client at the town).
local errorsBefore = swcui.error_count()
Swc.SetData("Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey", 2)
DynamicBus.SkillsBus.Broadcast.OpenBio()
frames(1.5)
picture("skills-bio")
equal(swcui.error_count() - errorsBefore, 0, "the bio tab opens with no Lua error")

-- The character window (skills.luac): the depth of field 0 1 1 3 10; the state HUD closes it, and the depth of field goes off.
LyShineManagerBus.Broadcast.SetState(HUD)
frames(1.0)
local camera = camera_log()
camera_sent(camera, "camera dof 0.000 1.000 1.000 3.000 10.000", "the character window")
camera_sent(camera, "camera dof -", "the character window closes")
camera_clean("the character window")
