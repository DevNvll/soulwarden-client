-- The prompt fixture (stage WF7): the original prompt cards of lyshineui/markers/unifiedinteract.dynamicuicanvas (markermanager.luac in
-- the interact mode, unifiedinteractcard.luac, unifiedinteractoption.luac, hint.luac) with the native part of swc/interact.lua. A loot
-- bag in front of the camera: the card "DROPPED ITEMS" with the rows "Take All" (E, the hold form) and "Open" (T), at the screen
-- place of the bag; the hold of E fills the ring; T opens the panel of the bag (the request "openloot"). Then a young tree with no
-- axe: the gatherable card "Gather" with the refusal "No equipped Axe". Then no focus: the card hides.
-- Pictures: prompt-bag, prompt-hold, prompt-tree.
local cards = screen("LyShineUI/Markers/UnifiedInteract.uicanvas", 12, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(18, 0)
frames(0.3)
-- The camera at 1.7 m, along +X (the view projection of the plates fixture).
local p00, p11 = 1.0, 1920 / 1080
Swc.HostViewProjection(0, 0, 0, 1, p00, 0, 0, 0, 0, p11, 0, 0, 0, -170 * p11, 10, 0, 1920, 1080, 0, 0, 170)
local function prompt(kind, id, objectKind, name, x, y, z, phase, progress, tool, gatheringType, refusal, options)
  return table.concat({kind, id, objectKind, name, x, y, z, phase, progress, tool, gatheringType, refusal, options}, "\t")
end
-- A bag 2 m ahead, 0.5 m to the left, on the ground.
Swc.HostLootBags("57:4.0:1")
local bag = prompt("bag", "57", "", "", 200, -50, 0, "idle", 0, 0, "", "", "TakeAllOption:0.5,OpenStorageOption:0")
Swc.HostPrompt(bag)
frames(0.6)
Swc.HostPrompt(bag)
frames(0.4)
local shown = Swc.prompt.shownCards()
equal(#shown, 1, "one card shows")
local card = shown[1]
if card then
  equal(swcui.get(find(find(card, "TitleFrame"), "Text_Name"), "text.text"), "Dropped Items", "the name of the bag (the Interact component of the slice)")
  check(Swc.HostPointOf("text:Take All") ~= "", "the row Take All")
  check(Swc.HostPointOf("text:Open") ~= "", "the row Open")
  local x, y = swcui.viewport_position(card)
  note(string.format("the card at %.0f %.0f", x, y))
end
picture("prompt-bag")
-- The hold of E: the progress of the use of the client (FSwcInteractor: hold).
Swc.HostPrompt(prompt("bag", "57", "", "", 200, -50, 0, "hold", 0.5, 0, "", "", "TakeAllOption:0.5,OpenStorageOption:0"))
frames(0.1)
-- The ring of the first shown row (the template row of the card is off).
local ring
for _, row in ipairs(card and swcui.children(find(card, "Options")) or {}) do
  if not ring and swcui.enabled_in_hierarchy(row) then
    ring = find(row, "PressandHoldProgressBar")
  end
end
near(ring and UiImageBus.Event.GetFillAmount(ring) or -1, 0.5, 0.01, "the hold ring of Take All: one half")
picture("prompt-hold")
Swc.HostPrompt(bag)
frames(0.1)
-- T: the second option, OpenStorageOption: the panel of the bag (the open of Tab near a bag).
Swc.hostRequests = {}
key("t")
frames(0.2)
equal(Swc.hostRequests[1], "openloot 57", "T at the bag: the request of OpenStorageOption")

-- A young tree with no axe: the gatherable card with the refusal of the client rule.
Swc.HostLootBags("")
local tree = prompt("object", "r_+04_+02#1234", "naturalNode", "Young Tree", 300, 40, 0, "idle", 0, 1, "Chopping", "ui_notoolchopping", "GatherOption:0.5")
Swc.HostPrompt(tree)
frames(0.6)
Swc.HostPrompt(tree)
frames(0.4)
check(Swc.HostPointOf("text:Young Tree") ~= "" or Swc.HostPointOf("text:YOUNG TREE") ~= "", "the name of the tree")
check(Swc.HostPointOf("text:Gather") ~= "", "the row Gather")
check(Swc.HostPointOf("text:No equipped Axe") ~= "", "the refusal: No equipped Axe")
picture("prompt-tree")
Swc.HostPrompt("none")
frames(1.0)
equal(#Swc.prompt.shownCards(), 0, "no focus: no card")
