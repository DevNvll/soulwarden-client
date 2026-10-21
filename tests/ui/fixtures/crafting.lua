-- The crafting fixture (stage MVP-J, Linear AST-226): the original CraftingScreenV4 (craftingscreenv4.luac and its panels) and
-- StationProperties with the data of swc/crafting.lua. Discovery version.
local HUD, CRAFTING = 2702338936, 3024636726
local crafting = screen("LyShineUI/Crafting/CraftingScreenV4.uicanvas", 20)
local station = screen("LyShineUI/StationProperties/StationProperties.uicanvas", 21)
local tooltip = screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
-- A level 70 character with a few trade skill ranks and a bag with materials.
Swc.HostProgression(70, 0)
local function crc(text) return swcui.crc32(string.lower(text)) end
Swc.HostRanks(string.format("%d 150 0\n%d 0 0\n%d 2 0\n%d 0 2500", crc("Weaponsmithing"), crc("Smelting"), crc("Logging"),
  crc("Azoth_Currency")))
Swc.HostCoin(99900)
-- The place: Windsward (the territory of the fee line; swc/worldmap.lua sets the territory of the position).
Swc.HostView(-45, 8880, 4250, 137)
Swc.HostBagSize(40)
Swc.HostBagSlot(0, crc("IngotT5"), 30, 0, 0, 0, "1", 0, 0, 0, "")
Swc.HostBagSlot(1, crc("TimberT5"), 6, 0, 0, 0, "2", 0, 0, 0, "")
Swc.HostBagSlot(2, crc("LeatherT5"), 4, 0, 0, 0, "3", 0, 0, 0, "")
Swc.HostBagSlot(3, crc("OreT1"), 32, 0, 0, 0, "4", 0, 0, 0, "")
frames(0.3)

local function requests()
  local out = {}
  for _ = 1, 8 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end

-- The focus of the station (swc/interact.lua sends OnInteractFocus at the prompt of the station; stationproperties.luac keeps it).
Swc.Notify("UiInteractorComponentNotificationsBus", Swc.ids.interactor, "OnInteractFocus", {
  interactableEntityId = EntityId(), interactName = "@BlacksmithT5_MasterName",
  GetOwnershipBind = function() return {ownershipBindEntityId = EntityId()} end,
  GetHealthBind = function() return {vitalsStatBindEntityId = EntityId()} end, supportsCustomName = false,
}, false, 0)
-- The client opens the screen at the request of OpenCraftingOption (UI/SwcUiCrafting).
Swc.HostStation("open", "Blacksmith5", true, "@BlacksmithT5_MasterName")
frames(4.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CRAFTING, "the open sets the state CraftingStation")
check(swcui.canvas_get(crafting, "enabled"), "the crafting screen shows")
check(swcui.canvas_get(station, "enabled"), "the station header shows")
equal(CraftingRequestBus.Broadcast.GetCraftingStationName(), "Blacksmith5", "the station name of the use")
picture("crafting-open")

-- Escape: the screen leaves the state and asks for the end of the station use one time.
requests()
key("escape")
frames(1.0)
local r = requests()
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= CRAFTING, "Escape closes the screen")
equal(#r, 1, "Escape gives one request")
equal(r[1], "station end", "the request is the end of the station use")
Swc.HostStation("close")
frames(0.5)

-- The close of the client (a refusal, 3746): the screen closes with no request.
Swc.HostStation("open", "Smelting5", true, "@SmeltingT5_MasterName")
frames(2.0)
equal(LyShineManagerBus.Broadcast.GetCurrentState(), CRAFTING, "the second open")
requests()
Swc.HostStation("close")
frames(1.0)
check(LyShineManagerBus.Broadcast.GetCurrentState() ~= CRAFTING, "the close of the client leaves the state")
equal(#requests(), 0, "the close of the client gives no request")
