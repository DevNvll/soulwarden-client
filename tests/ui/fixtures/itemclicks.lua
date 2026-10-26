-- The item click fixture (Linear AST-260, the user test "right click I think it should equip/unequip"): the rule of the original
-- scripts. newinventory.luac ItemDoubleClickAction: a double click equips a bag item (or uses it, or opens a loot container) and
-- unequips an equipped item; OnCursorPressed with the right button (the CRC-32 of "mouse_button_right", 369457006): over an item it
-- shows the tooltip and, when it can compare, the compare tooltip; elsewhere the mouse goes to the camera (EnableMouse false).
-- itemdraggable.luac OnReleased ends at once after a right button press; OpenContextMenu is empty: a right click equips nothing.
-- A tool (AST-244 item 7): its class SkinningKnife takes paperdoll slot 35, so a double click equips it; its gather speed is MinGatherEFF
-- to MaxGatherEFF over its gear score range (the T1 knife: 2.0, "200%"); its cast distance is 0 (craftingstatspanel.luac compares it).
-- Pictures: itemclicks-open, itemclicks-right-held.
local HUD, INVENTORY = 2702338936, 2972535350
local inventory = screen("LyShineUI/Inventory/NewInventory.uicanvas", 15)
local equipment = screen("LyShineUI/Equipment/EquipmentV3.uicanvas", 35)
local tooltip = screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
local encumbrance = screen("LyShineUI/HUD2024/ActionBar/Encumbrance.uicanvas", 36, true)
local flyout = screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 52)
local notification = screen("LyShineUI/Notifications/Notification.uicanvas", 55, true)
allow("LyShineUI/Popup/Popup")
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(5, 1234)

-- The bag (the item index of the wire is the business of the client; here the item ids): a sword, an armor, potions, ore.
local function instance(n)
  return string.format("{%08X-0000-0000-0000-%012X}", n, n)
end
Swc.HostBagSize(6)
Swc.HostBagSlot(0, "1hSwordT1", 1, 100, 900, 1000, instance(1), 1, 1, 17, "")
Swc.HostBagSlot(1, "LightChestT1", 1, 60, 250, 250, instance(2), 1, 2, 17, "")
Swc.HostBagSlot(2, "PotionHealthT1", 3, 100, 0, 0, instance(3), 0, 3, 17, "")
Swc.HostBagSlot(3, "OreT1", 25, 0, 0, 0, instance(4), 0, 4, 17, "")
Swc.HostBagSlot(4, "1hSkinningKnifeT1", 1, 100, 1000, 1000, instance(6), 1, 6, 17, "")
Swc.HostPaperdollItem(24, "1hSwordT2_FTUE", 1, 125, 1000, 1000, instance(5), 1, 5, 17, "")
Swc.HostCoin(12345)
frames(0.3)
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Inventory.CurrentEncumbrance"), 30 + 45 + 3 * 5 + 25 * 2 + 20,
  "the bag weight in tenths (weight times quantity of each bag item)")

-- Open: the key Tab (toggleInventoryWindow) sets the loadout flag.
key("tab")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), INVENTORY, "Tab opens the state Inventory (the loadout flag)")
check(swcui.canvas_get(inventory, "enabled"), "the inventory screen shows")
check(swcui.canvas_get(equipment, "enabled"), "the equipment screen shows")
check(Swc.HostCursor(), "the inventory has the cursor")
picture("itemclicks-open")

-- The sections of the bag: each item in the section of its UI class, with the weight of its stack.
local root = swcui.canvas_get(inventory, "root")
local sections = {}
local function each_script(id, fn)
  for _, c in ipairs(swcui.children(id)) do
    for _, inst in ipairs(Swc.components[tostring(c)] or {}) do fn(c, inst) end
    each_script(c, fn)
  end
end
each_script(root, function(c, inst)
  if (inst.__swcScript or ""):find("catsection.luac", 1, true) and #inst.itemDefs > 0 and swcui.enabled_in_hierarchy(c) then
    sections[inst.itemClass] = {count = #inst.itemDefs, weight = inst.totalWeight}
  end
end)
local function section(class)
  return sections[class] or {count = 0, weight = 0}
end
check(section(eItemClass_UI_Weapon).count == 1 and section(eItemClass_UI_Weapon).weight == 30, "the weapon section: the sword, 3.0")
check(section(eItemClass_UI_Armor).count == 1 and section(eItemClass_UI_Armor).weight == 45, "the armor section: the shirt, 4.5")
check(section(eItemClass_UI_Consumable).count == 1 and section(eItemClass_UI_Consumable).weight == 15, "the consumable section: 3 potions, 1.5")
check(section(eItemClass_UI_Smelting).count == 1 and section(eItemClass_UI_Smelting).weight == 50, "the smelting section: 25 ore, 5.0")
local weight = find(root, "WeightBar")
local value = weight and find(weight, "ValueText")
local maximum = weight and find(weight, "MaxValueText")
equal(value and swcui.text_layout(value).lines[1].text, "16.0", "the bag weight of the header")
check(maximum and swcui.text_layout(maximum).lines[1].text:find("200.0", 1, true) ~= nil, "the weight limit of the header (EncumbranceBaseMax 2000)")
check(PaperdollRequestBus.Event.GetSlot(Swc.ids.paperdoll, 24):GetItemName() == "1hSwordT2_FTUE", "the paperdoll has the sword in slot 24")

-- The tooltip of a bag item: the hover of the sword cell opens the item tooltip in the flyout menu, with the gear score and
-- the core damage of the item rules (64 at gear score 100, times 1.1 for level 5: 70).
local froot = swcui.canvas_get(flyout, "root")
local function cell_of(rootId, key)
  local found
  each_script(rootId, function(c, inst)
    if (inst.__swcScript or ""):find("itemdraggable.luac", 1, true) and swcui.enabled_in_hierarchy(c) and inst.ItemLayout
      and inst.ItemLayout.itemId == Swc.ItemStatic(key).crc then
      found = c
    end
  end)
  return found
end
local function text_of(id)
  local layout = id and swcui.text_layout(id)
  return layout and layout.lines[1] and layout.lines[1].text
end

local sword = cell_of(root, "1hSwordT1")
Swc.hostRequests = {}
click(sword, 0.5, 0.5)
frames(0.08)
click(sword, 0.5, 0.5)
frames(1.0)
equal(Swc.hostRequests[1], "equip 0 24 1 " .. instance(1), "a double click on the bag sword: the equip request into the weapon slot")
Swc.HostInput("move", -1, -1)
frames(0.5)
local chest = cell_of(root, "LightChestT1")
Swc.hostRequests = {}
move_to(chest, 0.5, 0.5)
frames(1.5)
Swc.HostInput("button", "right", true)
frames(0.8)
check(swcui.canvas_get(flyout, "enabled"), "the right button held over an item: its tooltip shows")
equal(#Swc.hostRequests, 0, "the right button held: no request")
picture("itemclicks-right-held")
Swc.HostInput("button", "right", false)
frames(0.5)
equal(#Swc.hostRequests, 0, "the right button released: no request (no equip on a right click)")
local equipped = cell_of(swcui.canvas_get(equipment, "root"), "1hSwordT2_FTUE")
check(equipped ~= nil, "the equipped sword has its cell")
if equipped then
  Swc.hostRequests = {}
  click(equipped, 0.5, 0.5); frames(0.08); click(equipped, 0.5, 0.5); frames(1.0)
  equal(Swc.hostRequests[1], "unequip 24 -1 1", "a double click on the equipped sword: the unequip request")
end

-- The tool: the equip into slot 35 (dressing-slot) by a double click, its gather speed, and its cast distance.
local knifeSlot = Swc.NewItemSlot("bag", 4, {itemId = "1hSkinningKnifeT1", quantity = 1, gearScore = 100})
check(knifeSlot:IsEquippable() and knifeSlot:CanEquipInSlot("dressing-slot"), "the skinning knife can go into the dressing slot (35)")
local attributes = knifeSlot:GetWeaponAttributes()
local knifeRow = swcui.table_row("weaponstats", "1hSkinningKnifeT1")
if knifeRow and knifeRow.maxGatherEff ~= nil then
  equal(attributes.gatheringEfficiency, 2, "the gather speed of the T1 knife at 100: 2.0 (MinGatherEFF, the tooltip 200%)")
else
  note("weaponstats.tsv has no MaxGatherEFF (the deploy of AST-244 item 7 waits): the gather speed check is skipped")
end
equal(attributes.maxCastDistance, 0, "the cast distance of a tool: 0 (not nil)")
local knife = cell_of(root, "1hSkinningKnifeT1")
check(knife ~= nil, "the knife has its cell in the bag")
if knife then
  Swc.hostRequests = {}
  click(knife, 0.5, 0.5); frames(0.08); click(knife, 0.5, 0.5); frames(1.0)
  equal(Swc.hostRequests[1], "equip 4 35 1 " .. instance(6), "a double click on the knife: the equip request into slot 35")
end
