-- The territory standing fixture (QG1, step 3 of ut567; docs/UI.md "The territory standing"; Soulwarden protocol notes 4.59; static read
-- ut567/qgplan/t3points): the point methods of swc/territory.lua with the states of the host (HostRanks: slot 65; HostPointPools: slot 67
-- with the pool categories and the card levels), the claim rows and keys, the rank banner (bannertriggers.luac), the map standing list
-- (territorystandingspane.luac), and the card popup (territorybonuspopup.luac) with the choice 4040.
-- The drive of the plan: Windsward (territory 6, progression 1DB87A14, pool TerritoryRankBonus_6 CCDDF19B) at rank 0 with 120 points; the
-- Quartermaster hand-in gives rank 1 with 70 of 375 and one token; the popup shows HousePurchase alone (retail QG1); its choice keeps the
-- token; then GainStanding +5.0%, GainXP +3.0%, GainGatherRate +5.0%; the choice of GainStanding (68C1F9AA) spends the token.
-- Needs gamedata/territory.json (tools/convert-territory.sh). Pictures: territory-standing, territory-popup.
local HUD, MAP = 2702338936, 2477632187
local banner = screen("LyShineUI/Banner/Banner.uicanvas", 30)
local map = screen("LyShineUI/WorldMap/MagicMap.uicanvas", 20)
screen("LyShineUI/Popup/Popup.uicanvas", 52)
-- The notifications (bannertriggers.luac ShouldShowTerritoryNotifications asks notifications.luac) and the card popup (the client loads it
-- at the state Map: swc/territory.lua; a fixture loads its canvases itself).
screen("LyShineUI/Notifications/Notification.uicanvas", 40, true)
local popupCanvas = screen("LyShineUI/Territory/TerritoryStanding/TerritoryBonusPopup.uicanvas", 52)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostVitals(5330, 5330, 100, 100, 100, 100, false)
Swc.HostProgression(20, 0)
Swc.HostLoading(true, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.3)
Swc.HostLoading(false, "newworld_vitaeeterna", "eLoadingScreenIds_LEVEL1")
frames(0.5)

local T = Swc.territory
local errorsBefore = swcui.error_count()
local player = Swc.ids.player
local WINDSWARD, POOL = 0x1DB87A14, 0xCCDDF19B
local function crc(text) return swcui.crc32(string.lower(text)) end
local STANDING, XP, GATHER, HOUSE = crc("TB_GainStanding"), crc("TB_GainXP"), crc("TB_GainGatherRate"), crc("TB_PurchaseHouse")
local AXE = crc("GreatAxe")

local function requests()
  local out = {}
  for _ = 1, 16 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local function texts_of(canvas)
  local out = {}
  local function walk(id, depth)
    if depth > 40 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(canvas, "root"), 0)
  return table.concat(out, " | ")
end
local function ids_of(records)
  local out = {}
  for _, r in ipairs(records) do out[#out + 1] = r.id end
  return table.concat(out, " ")
end
local function value_of(records, id)
  for _, r in ipairs(records) do
    if r.id == id then return r.upgradeValue, r.canUpgrade end
  end
  return nil
end
-- The events of the points notification bus at the player.
local events = {}
local watcher = {
  OnUnallocatedProgressionPointsChanged = function(_, pool, old, new) events[#events + 1] = string.format("pool %d %d %d", pool, old, new) end,
  OnProgressionPointsChanged = function(_, id, old, new) events[#events + 1] = string.format("card %s %s %s", tostring(id), tostring(old), tostring(new)) end,
}
local handler = Swc.Connect("ProgressionPointsNotificationBus", watcher, player)
local function pools(free, cards)
  Swc.HostPointPools(string.format("%d 3 1\n%d %d 2\n", AXE, POOL, free), cards or "")
  frames(0.2)
end

if not T.data().byStanding[WINDSWARD] then
  note("no gamedata/territory.json (tools/convert-territory.sh of step 3): the territory checks are skipped")
  equal(swcui.error_count() - errorsBefore, 0, "no Lua error with no territory data")
  return
end

-- 1. The data, the claim rows, and the claim keys.
check(T.data().byStanding[WINDSWARD] ~= nil and T.data().byStanding[WINDSWARD].poolCrc == POOL, "territory.json: Windsward 1DB87A14 with its pool CCDDF19B")
local claims = MapComponentBus.Broadcast.GetClaims()
local hasWindsward = false
for _, row in ipairs(claims) do hasWindsward = hasWindsward or (row.settlementId == 6 and row.monikerId == 6) end
check(hasWindsward, "a claim row of Windsward (settlementId 6)")
-- The id of a claim row is the Claim landmark actor of its territory (C-12: the town board resolves the destinationOverride against it).
local windswardClaim
for _, row in ipairs(claims) do if row.settlementId == 6 then windswardClaim = row end end
equal(windswardClaim and windswardClaim.id, "4B657E1A68E06C9F0000000000000000", "the id of the Windsward claim row: its Claim landmark actor")
local allActors = true
for _, row in ipairs(claims) do allActors = allActors and type(row.id) == "string" and #row.id == 32 end
check(allActors, "each claim row has a Claim landmark actor (32 hex digits)")
check(#claims > 1 and (claims[1].compassIconPath == ""), "the claim rows have no compass icon (compass.luac AddCapitals skips them): " .. #claims)
-- The DevWorld territories 74 and 75 (standings and definitions, no shape of the world map): no row (retail qg1 shot 22; live t3c).
local devWorld, standingRows = false, 0
for _, row in ipairs(claims) do
  devWorld = devWorld or row.settlementId == 74 or row.settlementId == 75
end
for _, s in ipairs(T.data().standings) do
  standingRows = standingRows + ((s.territory ~= "74" and s.territory ~= "75") and 1 or 0)
end
check(not devWorld, "no claim row of the DevWorld territories 74 and 75")
equal(#claims, standingRows, "a claim row for each other standing territory (13)")
local keys = LandClaimRequestBus.Broadcast.GetClaimKeys()
local hasKey = false
for _, k in ipairs(keys) do hasKey = hasKey or k == 6 end
check(hasKey and #keys == #claims, "the claim keys: the territory ids of the claim rows")

-- 1b. The standing list of the map (territorystandingspane.luac UpdateTerritories: the claim rows with no territory 9, sorted by tokens,
-- then rank, then the localized name) with no standing: the 13 territories with a shape, Windsward among them, at its name place (the
-- live check msb showed the first rows only: Brightwood to Mourningdale; Windsward is below them).
local pane
for _, list in pairs(Swc.components) do
  for _, instance in ipairs(list) do
    if string.find(instance.__swcScript or "", "territorystandingspane.luac", 1, true) then pane = instance end
  end
end
local function pane_rows()
  pane.territories = nil
  pane:UpdateTerritories()
  local ids, names = {}, {}
  for _, row in ipairs(pane.territories or {}) do
    ids[#ids + 1] = row.territoryId
    names[#names + 1] = swcui.localize(row.nameLocalizationKey)
  end
  return ids, names
end
check(pane ~= nil, "the standing list of the map (territorystandingspane.luac)")
if pane then
  local ids, names = pane_rows()
  note("the standing list with no standing: " .. table.concat(names, ", "))
  equal(#ids, 13, "the list has the 13 territories with a shape")
  local at = 0
  for i, id in ipairs(ids) do if id == 6 then at = i end end
  check(at > 0, "the list has Windsward (with no standing: at its name place " .. at .. ")")
end

-- 2. The first states (silent): Windsward rank 0 with 120 points, the pool with no token; a weapon pool with category 1.
Swc.HostRanks(string.format("%d 0 120", WINDSWARD))
pools(0)
equal(#events, 0, "the first states send no point event")
equal(ProgressionPointRequestBus.Event.GetUnspentTokens(player, WINDSWARD), 0, "no token at rank 0")
equal(#ProgressionPointRequestBus.Event.GetAvailableProgressionPointsData(player, WINDSWARD, true), 0, "no token: no available card (the filter)")
equal(#ProgressionPointRequestBus.Event.GetAvailableProgressionPointsData(player, WINDSWARD, false), 4, "no filter: the four cards of the rank row 0")
equal(ProgressionPointRequestBus.Event.GetStaticProgressionPointData(player, AXE).poolCategory, 0, "a weapon id keeps the earlier static record")
local static = ProgressionPointRequestBus.Event.GetStaticProgressionPointData(player, STANDING)
check(static.poolCategory == ePoolCategory_Territory and static.territoryBonusCategory == eTerritoryBonus_GainStanding
  and static.upgradeCardSprite == "LyShineUI/Images/Territory/StandingRewards/GainStanding.dds" and static.requiredProgressionLevel == 0,
  "the static record of TB_GainStanding")
equal(ProgressionPointRequestBus.Event.GetStaticProgressionPointData(player, HOUSE).requiredProgressionLevel, 1, "TB_PurchaseHouse needs rank 1")

-- 3. The Quartermaster hand-in: the pools state with one token, then the rank 1 with 70 points (the host order: the pools first).
events = {}
pools(1)
Swc.HostRanks(string.format("%d 1 70", WINDSWARD))
frames(1.0)
equal(events[1], string.format("pool %d 0 1", POOL), "the token: OnUnallocatedProgressionPointsChanged(pool, 0, 1)")
equal(#events, 1, "no card event at a rank-up")
equal(ProgressionPointRequestBus.Event.GetUnspentTokens(player, WINDSWARD), 1, "one token at rank 1")
equal(CategoricalProgressionRequestBus.Event.GetRank(player, WINDSWARD), 1, "the rank 1")
equal(CategoricalProgressionRequestBus.Event.GetProgression(player, WINDSWARD), 70, "70 points")
equal(CategoricalProgressionRequestBus.Event.GetMaxPointsForRank(player, WINDSWARD, 1), 375, "of 375 (Territory_Standing rank 1)")
equal(Swc.GetData("Hud.LocalPlayer.Map.ScreenChecked"), false, "the rank-up marks the map as not checked (bannertriggers.luac)")
frames(3.0)
local bannerText = texts_of(banner)
note("the banner: " .. bannerText)
check(bannerText:find("Windsward", 1, true) ~= nil, "the rank banner names Windsward")

if pane then
  local ids, names = pane_rows()
  note("the standing list at rank 1 with one token: " .. table.concat(names, ", "))
  equal(ids[1], 6, "Windsward is the first row with its token (retail qg1 shot 22: Newcomer in Windsward)")
  equal(#ids, 13, "still 13 rows")
end

-- 4. The available cards at rank 1 with one token: the rank row 0 in its order; the values of the next level.
local available = ProgressionPointRequestBus.Event.GetAvailableProgressionPointsData(player, WINDSWARD, true)
equal(ids_of(available), table.concat({STANDING, XP, GATHER, HOUSE}, " "), "the rank row 0: GainStanding, GainXP, GainGatherRate, PurchaseHouse")
near(value_of(available, STANDING), 0.05, 1e-6, "GainStanding: the bonus of level 1 (+5.0%)")
near(value_of(available, XP), 0.03, 1e-6, "GainXP: +3.0%")
local houseValue, houseCan = value_of(available, HOUSE)
check(houseCan == true and houseValue == 1, "PurchaseHouse: canUpgrade at rank 1")
-- The handler of the script: HousePurchase alone (territorydatahandler.luac ProgressionPointsToRewards).
local handlerScript = RequireScript("LyShineUI._Common.TerritoryDataHandler")
local rewards = handlerScript:GetRedeemableTerritoryRewards(6)
check(#rewards == 1 and rewards[1].rewardId == HOUSE, "the popup rewards: HousePurchase alone (retail QG1)")

-- 5. The map: the standing list and the popup.
LyShineManagerBus.Broadcast.SetState(MAP)
frames(2.0)
check(Swc.stateCanvases[MAP] ~= nil, "the state Map has the load of the card popup (swc/territory.lua)")
check(#DynamicBus.TerritoryBonusPopupBus.handlers > 0, "the card popup script connects TerritoryBonusPopupBus")
note("the map: " .. texts_of(map))
picture("territory-standing")
DynamicBus.TerritoryBonusPopupBus.Broadcast.OpenTerritoryBonusPopup(6)
frames(1.5)
check(swcui.canvas_get(popupCanvas, "enabled"), "the card popup shows")
note("the popup: " .. texts_of(popupCanvas))
picture("territory-popup")
requests()

-- 6. HousePurchase (4040 with the territory CRC; the token stays: DoNotSpendPoint), the state: the card at level 1, one token.
ProgressionPointRequestBus.Event.RequestSpendTerritoryStandingPoint(player, WINDSWARD, HOUSE)
local sent = requests()
equal(sent[1], string.format("territory spend %d %d", WINDSWARD, HOUSE), "HousePurchase: 4040 with the territory CRC and the card")
equal(ProgressionPointRequestBus.Event.GetUnspentTokens(player, WINDSWARD), 1, "no local change before the state")
events = {}
pools(1, string.format("%d %d 1\n", POOL, HOUSE))
equal(events[1], string.format("card %d 0 1", HOUSE), "the card event OnProgressionPointsChanged(card, 0, 1); no pool event (the token stays)")
available = ProgressionPointRequestBus.Event.GetAvailableProgressionPointsData(player, WINDSWARD, true)
equal(ids_of(available), table.concat({STANDING, XP, GATHER}, " "), "after the house: GainStanding, GainXP, GainGatherRate (the house is at MaxLevel)")
rewards = handlerScript:GetRedeemableTerritoryRewards(6)
local values = {}
for _, r in ipairs(rewards) do values[#values + 1] = tostring(r.value) end
note("the rewards after the house: " .. table.concat(values, ", "))
equal(#rewards, 3, "three rewards")

-- 7. GainStanding (4040 68C1F9AA): the state with no token; the current cards with their values.
ProgressionPointRequestBus.Event.RequestSpendTerritoryStandingPoint(player, WINDSWARD, STANDING)
equal(requests()[1], string.format("territory spend %d %d", WINDSWARD, STANDING), "GainStanding: 4040 68C1F9AA")
events = {}
pools(0, string.format("%d %d 1\n%d %d 1\n", POOL, HOUSE, POOL, STANDING))
equal(events[1], string.format("pool %d 1 0", POOL), "the token goes: the pool event first")
equal(events[2], string.format("card %d 0 1", STANDING), "then the card event")
equal(#ProgressionPointRequestBus.Event.GetAvailableProgressionPointsData(player, WINDSWARD, true), 0, "no token: no available card")
local current = ProgressionPointRequestBus.Event.GetCurrentProgressionPointsData(player, WINDSWARD, true)
equal(#current, 2, "the current cards: the house and GainStanding")
near(value_of(current, STANDING), 0.05, 1e-6, "the current GainStanding: +5.0% (level 1)")
equal(T.f32(T.card_bonus(T.data().cardsByCrc[STANDING], 2)), T.card_bonus(T.data().cardsByCrc[STANDING], 2), "the card values are f32")
near(T.card_bonus(T.data().cardsByCrc[STANDING], 2), 0.09325, 1e-5, "GainStanding at level 2: +9.3% (backend card_bonus)")
near(T.card_bonus(T.data().cardsByCrc[STANDING], 102), 0.55525, 1e-4, "GainStanding at MaxLevel 102: 0.55525 (protocol notes 4.59)")

-- 8. A refused 4040: the same state again changes nothing and sends no event.
events = {}
pools(0, string.format("%d %d 1\n%d %d 1\n", POOL, HOUSE, POOL, STANDING))
equal(#events, 0, "the same state: no event")
handler:Disconnect()
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.5)
equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
