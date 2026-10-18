-- The loot fixture: the panel of an open loot bag (stage P2.4; protocol notes 4.42) with the original CatContainer screen
-- (catcontainer.luac) next to NewInventory and EquipmentV3. A bag near the character (the trigger area of the native code,
-- Swc.HostLootBags) and Tab: newinventory.luac opens it (OpenNearbyLootContainers: the request "openloot"), the container screen
-- gets the open bag and the state Container shows the three screens. The items of the server (the selected Container) show in
-- the panel ("2 items"); LSHIFT and a click take one item (the request "take INSTANCE QUANTITY": 2942); Take All gives "takeall"
-- (2939); the end of the use of the bag (its Looted claim) closes the panel (the state Inventory).
-- Pictures: loot-panel, loot-taken.
local CONTAINER, INVENTORY = 3349343259, 2972535350
local inventory = screen("LyShineUI/Inventory/NewInventory.uicanvas", 15)
local equipment = screen("LyShineUI/Equipment/EquipmentV3.uicanvas", 35)
local container = screen("LyShineUI/Container/CatContainer.uicanvas", 15)
local tooltip = screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
local flyout = screen("LyShineUI/FlyoutMenu/FlyoutMenu.uicanvas", 52)
allow("LyShineUI/Popup/Popup")
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(5, 1234)
local function instance(n)
  return string.format("{%08X-0000-0000-0000-%012X}", n, n)
end
Swc.HostBagSize(6)
Swc.HostBagSlot(0, "1hSwordT1", 1, 100, 900, 1000, instance(1), 1, 1, 17, "")
Swc.HostCoin(0)
frames(0.3)

-- A bag near the character, then Tab: the open of the bag and the state Container.
Swc.HostLootBags("57:4.0:1")
frames(0.2)
Swc.hostRequests = {}
key("tab")
frames(1.0)
equal(Swc.hostRequests[1], "openloot 57", "Tab near a bag: the open of its panel (OpenStorageOption)")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CONTAINER, "the state Container")
check(swcui.canvas_get(container, "enabled") and swcui.canvas_get(inventory, "enabled"), "the panel and the inventory show")
equal(LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.ActiveContainerIsLootDrop"), true, "the active container is a loot bag")

-- The items of the open bag (the selected Container of the server).
Swc.HostLootSize(2)
Swc.HostLootSlot(0, "OreT1", 9, 0, 0, 0, instance(11), 0, 1, 0, "")
Swc.HostLootSlot(1, "1hSwordT2", 1, 125, 1000, 1000, instance(12), 0, 2, 0, "")
frames(1.0)
check(Swc.HostPointOf("text:2 items") ~= "", "the panel: 2 items")
check(Swc.HostPointOf("text:5.4") ~= "", "the panel: the weight of its items (9 ore 1.8, the sword 3.6)")
check(Swc.HostPointOf("text:LOOT") ~= "" or Swc.HostPointOf("text:Loot") ~= "", "the panel: the title of the bag of a kill")
local croot = swcui.canvas_get(container, "root")
local function cell_of(key)
  local found
  local static = Swc.ItemStatic(key)
  local function scan(id)
    for _, c in ipairs(swcui.children(id)) do
      if swcui.enabled_in_hierarchy(c) then
        for _, inst in ipairs(Swc.components[tostring(c)] or {}) do
          if (inst.__swcScript or ""):find("itemdraggable.luac", 1, true) and inst.ItemLayout and inst.ItemLayout.itemId == static.crc then
            found = found or c
          end
        end
        scan(c)
      end
    end
  end
  scan(croot)
  return found
end
check(cell_of("OreT1") ~= nil and cell_of("1hSwordT2") ~= nil, "the panel: the cells of the ore and of the sword")
picture("loot-panel")

-- LSHIFT and a click on the ore: the take of the stack (2942).
Swc.hostRequests = {}
Swc.HostInput("key", "lshift", true)
frames(0.1)
click(cell_of("OreT1"), 0.5, 0.5)
-- The single click comes after the time of a double click (clickrecognizer.luac): LSHIFT stays down until then.
frames(1.0)
Swc.HostInput("key", "lshift", false)
frames(0.1)
equal(Swc.hostRequests[1], "take " .. instance(11) .. " 9", "LSHIFT and a click: the take of the ore stack")
-- The answer of the server: the ore leaves the open bag and comes into the bag of the character.
Swc.HostLootSlot(0, "", 0, 0, 0, 0, "", 0, 3, 0, "")
Swc.HostBagSlot(1, "OreT1", 9, 0, 0, 0, instance(11), 0, 3, 0, "")
frames(0.5)
check(cell_of("OreT1") == nil and cell_of("1hSwordT2") ~= nil, "after the take: the sword stays in the panel")
check(Swc.HostPointOf("text:1 item") ~= "", "the panel: 1 item")

-- Take All (2939); the answer of the server; the Looted claim ends the use of the bag: the panel closes.
Swc.hostRequests = {}
click(swcui.parent(element(container, "ButtonTakeAll") or croot), 0.5, 0.5)
local takeAll = Swc.HostPointOf("text:Take All", 0.5, 0.5)
if Swc.hostRequests[1] ~= "takeall" and takeAll ~= "" then
  local x, y = takeAll:match("([%d.]+) ([%d.]+)")
  Swc.HostInput("move", tonumber(x), tonumber(y))
  frames(0.1)
  Swc.HostInput("button", "left", true)
  frames(0.05)
  Swc.HostInput("button", "left", false)
end
frames(0.3)
equal(Swc.hostRequests[#Swc.hostRequests], "takeall", "Take All: the request 2939")
Swc.HostLootSlot(1, "", 0, 0, 0, 0, "", 0, 4, 0, "")
Swc.HostBagSlot(2, "1hSwordT2", 1, 125, 1000, 1000, instance(12), 0, 4, 0, "")
frames(0.3)
picture("loot-taken")
Swc.HostLootBagClosed(57)
frames(0.8)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), INVENTORY, "the end of the bag: the state Inventory")
check(not swcui.canvas_get(container, "enabled"), "the panel hides")
