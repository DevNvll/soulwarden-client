-- The inventory fixture: the original NewInventory and EquipmentV3 screens (newinventory.luac, equipmentv3.luac, the item
-- lists and the item slices) with a bag and a paperdoll of the host calls of the client (Swc.HostBagSlot, HostPaperdollItem,
-- HostCoin). Tab (toggleInventoryWindow: the loadout flag) opens the state "Inventory" of the state table: the two screens
-- show with the cursor; the bag items show in their sections with the weights of the data; the equipped sword shows in
-- its weapon slot; the header has the weight of the bag and its limit. Tab closes the inventory.
-- Pictures: inventory-open.
local HUD, INVENTORY = 2702338936, 2972535350
local inventory = screen("LyShineUI/Inventory/NewInventory.uicanvas", 15)
local equipment = screen("LyShineUI/Equipment/EquipmentV3.uicanvas", 35)
local tooltip = screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
local encumbrance = screen("LyShineUI/HUD2024/ActionBar/Encumbrance.uicanvas", 36, true)
local flyout = screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 52)
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
Swc.HostPaperdollItem(24, "1hSwordT2_FTUE", 1, 125, 1000, 1000, instance(5), 1, 5, 17, "")
Swc.HostCoin(12345)
frames(0.3)
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Inventory.CurrentEncumbrance"), 30 + 45 + 3 * 5 + 25 * 2,
  "the bag weight in tenths (weight times quantity of each bag item)")

-- Open: the key Tab (toggleInventoryWindow) sets the loadout flag.
key("tab")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), INVENTORY, "Tab opens the state Inventory (the loadout flag)")
check(swcui.canvas_get(inventory, "enabled"), "the inventory screen shows")
check(swcui.canvas_get(equipment, "enabled"), "the equipment screen shows")
check(Swc.HostCursor(), "the inventory has the cursor")
picture("inventory-open")

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
equal(value and swcui.text_layout(value).lines[1].text, "14.0", "the bag weight of the header")
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
move_to(sword, 0.5, 0.5)
frames(1.5)
check(swcui.canvas_get(flyout, "enabled"), "hover: the item tooltip shows")
local stats
each_script(froot, function(c, inst)
  if (inst.__swcScript or ""):find("dynamictooltip_itemstats.luac", 1, true) and swcui.enabled_in_hierarchy(c) then
    stats = inst
  end
end)
equal(stats and text_of(stats.Properties.GearScoreText), "100", "the tooltip: the gear score of the item")
equal(stats and text_of(stats.Properties.SecondaryText), "70", "the tooltip: the core damage (item rules, level part)")
picture("inventory-tooltip")

-- A click on the item: the command list of the tooltip. "Equip" sends the request of the bag slot, the weapon slot of the
-- lowest gear score (equipmentcommon.luac EquipItemToBestSlot), the quantity, and the item instance.
local function command(label)
  local found
  local function scan(id)
    for _, c in ipairs(swcui.children(id)) do
      if swcui.enabled_in_hierarchy(c) and swcui.name(c) == "buttonText" and swcui.get(c, "text.text") == label then
        found = found or swcui.parent(c)
      end
      scan(c)
    end
  end
  scan(froot)
  return found
end
click(sword, 0.5, 0.5)
frames(0.8)
check(command("Equip") ~= nil and command("Lock") ~= nil and command("Drop") ~= nil, "the command list: Equip, Lock, Drop")
picture("inventory-commands")
Swc.hostRequests = {}
click(command("Equip"), 0.3, 0.5)
frames(0.5)
equal(Swc.hostRequests[1], "equip 0 24 1 " .. instance(1), "Equip: the request of the bag slot 0 into the weapon slot 24")

-- The answer of the server: the swords change places (the bag and the paperdoll states). The weight of the bag changes.
Swc.HostBagSlot(0, "1hSwordT2_FTUE", 1, 125, 1000, 1000, instance(5), 1, 5, 18, "")
Swc.HostPaperdollItem(24, "1hSwordT1", 1, 100, 900, 1000, instance(1), 1, 1, 18, "")
frames(0.5)
check(PaperdollRequestBus.Event.GetSlot(Swc.ids.paperdoll, 24):GetItemName() == "1hSwordT1", "after the equip: the paperdoll has the first sword")
check(cell_of(root, "1hSwordT2_FTUE") ~= nil and cell_of(root, "1hSwordT1") == nil, "after the equip: the other sword is in the bag")
equal(value and swcui.text_layout(value).lines[1].text, "14.6", "after the equip: the bag weight of the header")
picture("inventory-equipped")

-- Close: Tab again.
key("tab")
frames(0.6)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), HUD, "Tab closes the inventory")
check(not swcui.canvas_get(inventory, "enabled") and not swcui.canvas_get(equipment, "enabled"), "the two screens hide")
check(not Swc.HostCursor(), "no cursor after the inventory")
