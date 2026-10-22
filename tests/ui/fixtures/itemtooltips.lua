-- The item tooltip fixture (Linear AST-259): the hover of each item opens its tooltip (the flyout menu, dynamictooltip_itemstats.luac)
-- with no script error, and the end of the hover closes it. The play test of 2026-10-22 had "dynamictooltip_itemstats.luac: attempt to
-- compare number with nil" in IsAttributePerk (CharacterAttributeRequestBus GetAttributeBonus had no definition): the tooltip of each
-- item with a perk stopped, and an open one stayed. The items: a fixed list of the item classes, and the items of a copy of the user's
-- database when the local test file testdata/ast259-items.json is there (not in git; work/ast259).
-- Pictures: itemtooltips-amulet.
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
Swc.HostProgression(60, 1234)
Swc.HostCoin(12345)

local items = {
  {itemId = "1hSwordT1", gearScore = 100, perks = ""},
  {itemId = "LightChestT1", gearScore = 60, perks = ""},
  {itemId = "LightHeadBT4", gearScore = 440, perks = "PerkID_Stat_ArmorArtificer,PerkID_Common_CDR"},
  {itemId = "AmuletIsabellaT1", gearScore = 100, perks = ""},
  {itemId = "AmuletOpalT5", gearScore = 600, perks = "PerkID_StatSwap_HP,PerkID_Amulet_DefSlash,PerkID_Gem_ElementalWard4"},
  {itemId = "RingSTRT4", gearScore = 440, perks = "PerkID_Stat_ArmorSoldier,PerkID_Common_CDRonHit,PerkID_Ring_DmgPhysical"},
  {itemId = "PotionHealthT1", gearScore = 0, quantity = 3, perks = ""},
  {itemId = "OreT1", gearScore = 0, quantity = 25, perks = ""},
}
local user = swcui.load_json("testdata/ast259-items.json")
if user and user.items then
  for _, item in ipairs(user.items) do
    items[#items + 1] = item
  end
end
note(string.format("%d items (%d of the local list)", #items, user and user.items and #user.items or 0))

local function instance(n)
  return string.format("{%08X-0000-0000-0000-%012X}", n, n)
end
Swc.HostBagSize(4)
Swc.HostBagSlot(0, "OreT1", 1, 0, 0, 0, instance(1), 0, 1, 17, "")
frames(0.3)
key("tab")
frames(1.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), INVENTORY, "Tab opens the inventory")
local root = swcui.canvas_get(inventory, "root")
local froot = swcui.canvas_get(flyout, "root")

local function each_script(id, fn)
  for _, c in ipairs(swcui.children(id)) do
    for _, inst in ipairs(Swc.components[tostring(c)] or {}) do fn(c, inst) end
    each_script(c, fn)
  end
end
local function cell_of(itemCrc)
  local found
  each_script(root, function(c, inst)
    if (inst.__swcScript or ""):find("itemdraggable.luac", 1, true) and swcui.enabled_in_hierarchy(c) and inst.ItemLayout
      and inst.ItemLayout.itemId == itemCrc then
      found = c
    end
  end)
  return found
end
local function open()
  return swcui.canvas_get(flyout, "enabled") and swcui.enabled_in_hierarchy(froot)
end

local errors, notShown, notClosed, missing, done = {}, {}, {}, {}, 0
for index, item in ipairs(items) do
  local static = Swc.ItemStatic(item.itemId)
  if not static then
    missing[#missing + 1] = item.itemId
  else
    Swc.HostBagSlot(0, item.itemId, item.quantity or 1, item.gearScore or 0, 100, 100, instance(100 + index), 0, 10 + index, 17,
                    item.perks or "")
    frames(0.5)
    local cell = cell_of(static.crc)
    if not cell then
      missing[#missing + 1] = item.itemId .. " (no cell)"
    else
      local before = swcui.error_count()
      move_to(cell, 0.5, 0.5)
      frames(1.5)
      local shown = open()
      if item.itemId == "AmuletOpalT5" and shown then
        picture("itemtooltips-amulet")
      end
      move_to(root, 0.5, 0.02)
      frames(0.6)
      local after = swcui.error_count()
      if after > before then errors[#errors + 1] = string.format("%s (%d)", item.itemId, after - before) end
      if not shown then notShown[#notShown + 1] = item.itemId end
      if open() then notClosed[#notClosed + 1] = item.itemId end
      done = done + 1
    end
  end
end
note("the items with no item data or no cell: " .. table.concat(missing, ", "))
note("the items with script errors: " .. table.concat(errors, ", "))
note("the items with no tooltip: " .. table.concat(notShown, ", "))
note("the items whose tooltip stayed open: " .. table.concat(notClosed, ", "))
check(done >= 8, string.format("the hover of %d items", done))
equal(#errors, 0, "no script error in the tooltip of an item")
equal(#notShown, 0, "each item opens its tooltip")
equal(#notClosed, 0, "the end of the hover closes each tooltip")
