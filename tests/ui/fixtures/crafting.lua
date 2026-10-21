-- The crafting fixture (stage MVP-J, Linear AST-226): the original CraftingScreenV4 (craftingscreenv4.luac and its panels) and
-- StationProperties with the data of swc/crafting.lua. Discovery version.
local HUD, CRAFTING = 2702338936, 3024636726
local crafting = screen("LyShineUI/Crafting/CraftingScreenV4.uicanvas", 20)
local station = screen("LyShineUI/StationProperties/StationProperties.uicanvas", 21)
local tooltip = screen("LyShineUI/Tooltip/Tooltip.uicanvas", 54, true)
local notices = screen("LyShineUI/Notifications/Notification.uicanvas", 50)
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
-- The expansion of the character (the achievement Expansion_2023_Unlock, bit 11979: the backend gives each player Expansion2023).
Swc.HostOwnership("11979", "", true)
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

-- Decision C3: a recipe with a cause of the backend data (the Cunning Heartrune of Bile Bomb at the stonecutter: Expansion2023 and
-- an output with a gear score range and no rule) shows the requirement row with its cause in red, and CRAFT gives no request. A recipe with world tags is not
-- in the list.
Swc.HostStation("close")
frames(0.5)
Swc.HostStation("open", "Masonry5", true, "@MasonryT5_MasterName")
frames(10.0)
requests()
-- The row of the recipe (the screen's own SelectRecipe: CraftingRecipePanel SetRecipe; the live run types the search).
local ethereal = "RuneBileBombT4A"
DynamicBus.CraftingScreenBus.Broadcast.SelectRecipe(Swc.crafting.RecipeDataOf(Swc.crafting.Recipe(ethereal)))
frames(1.0)
equal(Swc.HostPointOf("recipe:Ethereal_LongswordT5"), "", "a recipe with world tags is not listed")
if ethereal then
  local reason = ""
  for _, instances in pairs(Swc.components) do
    for _, instance in ipairs(instances) do
      local rows = type(instance) == "table" and rawget(instance, "Properties") and instance.Properties.CraftingRequirements
      if type(rows) == "table" and rows.VersionDescText and UiElementBus.Event.IsEnabled(rows.VersionRequirement) then
        reason = UiTextBus.Event.GetText(rows.VersionDescText) or reason
      end
    end
  end
  note("the reason row: " .. reason)
  check(reason:find(Swc.crafting.refusalPrefix, 1, true) == 1, "the requirement row has the cause of the backend")
  picture("crafting-refusal")
  click(find(root, "CraftButton"))
  frames(0.5)
  equal(#requests(), 0, "CRAFT of the refused recipe gives no request")
end

-- The craft request (CraftRecipe of craftingscreenv4.luac OnCraftClicked) at the smelter: one host request with the recipe CRC, the
-- count, the fee claim of the client rule, the gear score claim, the upgrade, no perk item, and the item of each ingredient.
Swc.HostStation("open", "Smelting5", true, "@SmeltingT5_MasterName")
frames(2.0)
requests()
CraftingRequestBus.Broadcast.CraftRecipe("IngotT2", 3, {}, 0, {}, 0)
local craft = requests()
local recipe = Swc.crafting.Recipe("IngotT2")
local expected = string.format("craft %d 3 %d 0 0 - %d", crc("IngotT2"), Swc.crafting.Fee(recipe, 3), crc("OreT1"))
equal(craft[1], expected, "the craft request of 3 Iron Ingots (" .. tostring(craft[1]) .. ")")
check(Swc.crafting.Fee(recipe, 3) >= 1, "a recipe with UseCraftingTax at a station with a fee costs at least 1")
-- A currency ingredient is the item 0 (live check L5c).
local currency
for id, r in pairs(Swc.crafting.Data().recipes) do
  for _, ingredient in ipairs(r.ingredients) do
    if ingredient[1] == "Currency" and not currency then currency = id end
  end
end
note("a recipe with a currency ingredient: " .. tostring(currency))
-- The progress of the server goes to CraftingEventBus (the expected item updates of a batch).
Swc.HostCraftProgress("IngotT2", 3, 3)
equal(Swc.crafting.progress.completed, 3, "the progress 3 of 3 reaches the client state")
Swc.HostStation("close")
frames(0.5)

-- The full path of the screen at the smelter: the search "Iron Ingot", the row, CRAFT (one request of one job), then the result of the
-- server: the new stack with the cause ItemCreated (3) and the progress 1 of 1; the result screen shows the item 2 s after it.
Swc.HostStation("open", "Smelting5", true, "@SmeltingT5_MasterName")
frames(4.0)
requests()
local root = swcui.canvas_get(crafting, "root")
click(find(root, "FilterInputField"))
type_text("Iron Ingot")
frames(1.5)
local at = Swc.HostPointOf("recipe:IngotT2")
check(at ~= "", "the search shows the row of Iron Ingot")
local x, y = string.match(at, "(%S+) (%S+)")
Swc.HostInput("move", tonumber(x), tonumber(y))
Swc.HostInput("button", "left", true)
Swc.HostInput("button", "left", false)
frames(1.0)
picture("crafting-iron-ingot")
click(find(root, "CraftButton"))
frames(0.5)
local sent = requests()
equal(sent[1], string.format("craft %d 1 %d 0 0 - %d", crc("IngotT2"), Swc.crafting.Fee(Swc.crafting.Recipe("IngotT2"), 1), crc("OreT1")),
  "CRAFT gives one request of one Iron Ingot (" .. tostring(sent[1]) .. ")")
Swc.HostBagSlot(3, crc("OreT1"), 28, 0, 0, 0, "4", 0, 0, 0, "")
Swc.HostBagSlot(4, crc("IngotT2"), 1, 0, 0, 0, "5", 0, 0, 3, "")
Swc.HostCraftProgress("IngotT2", 1, 1)
frames(4.0)
picture("crafting-result")

-- A procedural recipe at the forge: the Orichalcum Great Axe (Weaponsmithing 150) with T5 materials; the screen shows its craft gear
-- score and its perk rows, and CRAFT gives one request with the gear score claim of the quality bar.
Swc.HostStation("close")
frames(0.5)
Swc.HostStation("open", "Blacksmith5", true, "@BlacksmithT5_MasterName")
frames(4.0)
requests()
click(find(root, "FilterInputField"))
type_text("Orichalcum Great Axe")
frames(2.0)
local axe = Swc.HostPointOf("recipe:Procedural_2hGreatAxeT5")
check(axe ~= "", "the search shows the row of the Orichalcum Great Axe")
local ax, ay = string.match(axe, "(%S+) (%S+)")
if ax then
  Swc.HostInput("move", tonumber(ax), tonumber(ay))
  Swc.HostInput("button", "left", true)
  Swc.HostInput("button", "left", false)
end
frames(1.0)
picture("crafting-axe")
click(find(root, "CraftButton"))
frames(0.5)
local axeRequest = requests()[1] or ""
note("the axe request: " .. axeRequest)
check(axeRequest:find("^craft " .. crc("Procedural_2hGreatAxeT5") .. " 1 ") ~= nil, "CRAFT gives one request of the axe")

-- The minor notice "Crafting attempt failed." of the 10 s timer (craftingscreenv4.luac CleanupFailureAndReturn) ends after the duration of
-- its type (NotificationData starts with maximumDuration 2.0, the defaultDuration of notifications.luac).
local function notice_text()
  local found = ""
  local function walk(id, depth)
    if depth > 14 then return end
    if swcui.name(id) == "MinorNotifcationText" and swcui.enabled_in_hierarchy(id) then found = swcui.get(id, "text.text") or "" end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(notices, "root"), 0)
  return found
end
local failed = NotificationData()
failed.type = "Minor"
failed.text = "@crafting_failed"
UiNotificationsBus.Broadcast.EnqueueNotification(failed)
frames(1.0)
equal(notice_text(), "Crafting attempt failed.", "the minor notice shows")
frames(8.0)
equal(notice_text(), "", "the minor notice ends")

-- The trade skill points of a job: the reward of the event times the units of the ingredients that are not a currency (the Runestone:
-- 5 + 1 + 1; the server gave 21,000 Stonecutting for a job of the reward 3,000 in run mvpj-batch2).
equal(Swc.crafting.RecipeDataOf(Swc.crafting.Recipe("BlockT51")):GetIngredientCount(), 7, "the ingredient units of the Runestone")

-- A recipe with no trade skill (the woodshop lists EtherealDust_PlayerXPBox): GetRecipeTradeskill gives "None", the value that
-- craftingstatspanel.luac UpdateRequirements tests (before, the panel failed at the woodshop).
equal(CraftingRequestBus.Broadcast.GetRecipeTradeskill("EtherealDust_PlayerXPBox"), "None", "a recipe with no trade skill")

-- The known test of the tree: Lumber (TimberT3, Woodworking 50) at Woodworking 0 is "Unlearned"; Timber (level 0) is known.
check(not CraftingRequestBus.Broadcast.IsRecipeKnown("TimberT3", true), "Lumber above the rank is not known for the tree")
check(CraftingRequestBus.Broadcast.IsRecipeKnown("TimberT2", true), "Timber at level 0 is known")
check(CraftingRequestBus.Broadcast.IsRecipeKnown("TimberT3"), "Lumber is a known recipe of the list")
Swc.HostStation("close")
frames(0.5)
Swc.HostStation("open", "Carpentry3", true, "@CarpentryT2_MasterName")
frames(4.0)
picture("crafting-woodshop")
