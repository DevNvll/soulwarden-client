-- The loot bags of the character and the panel of an open bag (stage P2.4; protocol notes 4.42; docs/UI.md). The panel is
-- the original CatContainer screen (catcontainer.luac) in the state Container, next to the inventory screens:
--   - the bags near the character: the native code calls the data callbacks Hud.LocalPlayer.LootDrop.TriggerAreaEntered and
--     TriggerAreaExited with the entity of a bag (newinventory.luac keeps them; Tab near a bag opens the nearest one);
--   - LootDropRequestsBus OpenLootDrop: the client sends the interaction OpenStorageOption (action 26) and the panel opens at
--     once (live check L5 of the backend): the native code gives the container of the open bag to the screen
--     (ContainerScreenRequestBus RegisterWithLootDropInteractable), which sets the loadout flag (the state Container);
--   - the items of the panel: the selected Container of the character (slot 76; the open clears it);
--   - Take All (LocalPlayerUIRequestsBus TakeAll: 2939) and the take of one item (TradeBatchAddItem and TradeBatchExecute: 2942);
--   - the close of the panel (InteractPanelClosed: 2473), and the end of the bag (its Looted claim, the stop of its record): the
--     panel closes (UnregisterInteractable).

local Swc = Swc

-- The container of the open bag (the selected Container of the character). An id of the runtime.
Swc.ids.lootContainer = EntityId("6005173470066573579")
Swc.loot = {count = 0, slots = {}, bags = {}, open = nil}
Swc.containers = Swc.containers or {}
Swc.containers[tostring(Swc.ids.lootContainer)] = Swc.loot

-- The entity of a bag for the scripts: an id of the runtime made from the interest number of the bag.
local function bag_id(interest)
  return EntityId(string.format("7%010d", interest))
end

local function interest_of(id)
  local digits = string.match(tostring(id), "(%d+)")
  if not digits or #digits ~= 11 or digits:sub(1, 1) ~= "7" then
    return nil
  end
  return tonumber(digits:sub(2))
end

--- The bags near the character (the C++ of the client: the bags of the creature view in the range of their Interact bound, with
--- no Looted claim): "INTEREST:DISTANCESQ:KIND ..." (m²; the kind of the claim, 1 AILoot or 5 Discarded). A new bag gives
--- TriggerAreaEntered, a bag that left gives TriggerAreaExited.
function Swc.HostLootBags(list)
  local now = {}
  for interest, distanceSq, kind in string.gmatch(list or "", "(%d+):([%d.]+):?(%d*)") do
    now[tonumber(interest)] = tonumber(distanceSq)
    Swc.loot.kinds = Swc.loot.kinds or {}
    Swc.loot.kinds[tonumber(interest)] = tonumber(kind) or 1
  end
  for interest in pairs(Swc.loot.bags) do
    if not now[interest] then
      Swc.loot.bags[interest] = nil
      Swc.DataCall("Hud.LocalPlayer.LootDrop.TriggerAreaExited", bag_id(interest))
    end
  end
  for interest, distanceSq in pairs(now) do
    local entered = Swc.loot.bags[interest] == nil
    Swc.loot.bags[interest] = distanceSq
    if entered then
      Swc.DataCall("Hud.LocalPlayer.LootDrop.TriggerAreaEntered", bag_id(interest))
    end
  end
end

--- The number of slots of the open bag (the selected Container).
function Swc.HostLootSize(count)
  local loot = Swc.loot
  for i = count, loot.count - 1 do
    loot.slots[i] = nil
  end
  loot.count = count
  for i = 0, count - 1 do
    loot.slots[i] = loot.slots[i] or Swc.NewItemSlot("loot", i, nil)
  end
  Swc.Notify("ContainerEventBus", Swc.ids.lootContainer, "OnContainerChanged")
end

--- One slot of the open bag (the values of Swc.HostBagSlot).
function Swc.HostLootSlot(slot, itemId, quantity, gearScore, durability, maxDurability, instance, flags, chrono, action, perks)
  if slot >= Swc.loot.count then
    Swc.HostLootSize(slot + 1)
  end
  local list = {}
  for perk in string.gmatch(perks or "", "[^,]+") do
    list[#list + 1] = perk
  end
  local item = Swc.NewItemSlot("loot", slot, {itemId = itemId ~= "" and itemId or nil, quantity = quantity, gearScore = gearScore,
                                              durability = durability, maxDurability = maxDurability,
                                              instance = instance ~= "" and instance or nil, flags = flags, chrono = chrono,
                                              action = action, perks = list})
  Swc.loot.slots[slot] = item
  Swc.Notify("ContainerEventBus", Swc.ids.lootContainer, "OnSlotUpdate", slot, item, action or 0)
  Swc.Notify("ContainerEventBus", Swc.ids.lootContainer, "OnContainerSlotChanged", slot, item, action or 0)
end

--- The use of the open bag ended (its Looted claim, the end of the bag, a refusal of the open): the panel closes.
function Swc.HostLootBagClosed(interest)
  local open = Swc.loot.open
  if not open or interest_of(open) ~= interest then
    return
  end
  Swc.loot.open = nil
  Swc.Note("the use of the bag " .. tostring(interest) .. " ended: the panel closes")
  Swc.Notify("ContainerScreenRequestBus", nil, "UnregisterInteractable", open)
end

Swc.Define("LootDropRequestsBus", "Event", {
  OpenLootDrop = function(id)
    local interest = interest_of(id)
    if not interest then
      return
    end
    -- The open clears the panel: a state before the request is not in it (live check L5).
    Swc.HostLootSize(0)
    -- The title of the panel (Hud.LocalPlayer.InteractName, catcontainer.luac): "Loot" for the bag of a kill (live proof B1 of the
    -- backend stage 3.5b.2); a discard has the name of the Interact component of the slice ("@inv_DroppedItems": "Items").
    local kind = Swc.loot.kinds and Swc.loot.kinds[interest] or 1
    Swc.SetData("Hud.LocalPlayer.InteractName", kind == 5 and "@inv_DroppedItems" or "@ui_loot")
    Swc.loot.open = id
    Swc.hostRequests[#Swc.hostRequests + 1] = "openloot " .. interest
    Swc.Note("item request: openloot " .. interest)
    Swc.Notify("ContainerScreenRequestBus", nil, "RegisterWithLootDropInteractable", Swc.ids.lootContainer, id)
  end,
  GetDistanceSqToLocalPlayer = function(id)
    local interest = interest_of(id)
    return interest and Swc.loot.bags[interest] or nil
  end,
})
Swc.Handled("ContainerScreenRequestBus")

local function request(text)
  Swc.hostRequests[#Swc.hostRequests + 1] = text
  Swc.Note("item request: " .. text)
end

-- The takes of the panel. A batch of the trade bus has one item of the open bag (catcontainer.luac TakeItem).
local batch = {}
Swc.Define("LocalPlayerUIRequestsBus", "Broadcast", {
  TakeAll = function(container)
    if container == Swc.ids.lootContainer then
      request("takeall")
    end
  end,
  TradeBatchAddItem = function(give, slot, _, item, quantity)
    if not give and item and item.instance then
      batch[#batch + 1] = {instance = item.instance, quantity = quantity or item.quantity or 1}
    end
  end,
  TradeBatchExecute = function(_, container)
    if container == Swc.ids.lootContainer then
      for _, entry in ipairs(batch) do
        request(string.format("take %s %d", entry.instance, entry.quantity))
      end
    end
    batch = {}
  end,
})
