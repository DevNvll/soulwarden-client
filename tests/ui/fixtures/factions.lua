-- The faction fixture (QG7, 2a of ut567; protocol notes 4.63; Soulwarden docs/systems/factions.md): the Social and the Faction buses of
-- swc/factions.lua with the host calls of the owner states (HostSocial: the empty Social state of slot 55; HostFaction: the Faction state of
-- slot 49), the republish of the same faction (the answer to a refused request), the check of CanSetFactionWithResults in the client order,
-- the request 3161, the Bio tab with a faction, and the selection screen of Change Faction (state 1040880896).
-- Pictures: factions-bio, factions-selection.
local HUD, SKILLS, SELECTION = 2702338936, 3576764016, 1040880896
local skills = screen("LyShineUI/Skills/Skills.uicanvas", 20)
local selection = screen("LyShineUI/ChangeFaction/FactionSelection.uicanvas", 52)
screen("LyShineUI/Popup/Popup.uicanvas", 52)
Swc.factions.selectionLoaded = true   -- the fixture loads the canvas (the state hook would load it in the client)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(16, 0)   -- the level index 16: the screen level 17 of the recruitment
frames(0.3)

local function texts_of(canvas)
  local out = {}
  local function walk(id, depth)
    if depth > 30 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(canvas, "root"), 0)
  return table.concat(out, " | ")
end
local function requests()
  local out = {}
  for _ = 1, 16 do
    local r = Swc.TakeHostRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return out
end
local player = Swc.ids.player
local errorsBefore = swcui.error_count()

-- 1. The Social state: before it, the bus is not ready; after it, empty lists (the poster's GetPlayers_ServerCall ends at once).
check(not JavSocialComponentBus.Broadcast.IsReady(), "the Social bus is not ready before the Social state")
Swc.HostSocial()
frames(0.1)
check(JavSocialComponentBus.Broadcast.IsReady() and Swc.GetData("Hud.LocalPlayer.JavSocialComponentBus.IsReady") == true,
  "the Social state: the bus and its data node are ready")
local friends = JavSocialComponentBus.Broadcast.GetFriends()
check(type(friends) == "table" and #friends == 0, "GetFriends gives an empty list (not nil)")
equal(JavSocialComponentBus.Broadcast.GetFriendCountInFaction(eFactionType_Faction1), 0, "no friend in the Syndicate")

-- 2. The progression ids of the reputation and the tokens (the backend table of docs/systems/factions.md).
equal(FactionRequestBus.Event.GetFactionReputationProgressionIdFromType(player, eFactionType_Faction1), 0x56FBDE12, "the Syndicate reputation id")
equal(FactionRequestBus.Event.GetFactionTokensProgressionIdFromType(player, eFactionType_Faction1), 0xEBD7B082, "the Syndicate tokens id")
equal(FactionRequestBus.Event.GetFactionReputationProgressionIdFromType(player, eFactionType_Faction3), 0xF4EB80F7, "the Covenant reputation id")
equal(FactionRequestBus.Event.GetFactionTokensProgressionIdFromType(player, eFactionType_Faction3), 0xA0989588, "the Covenant tokens id")
equal(FactionRequestBus.Event.GetFactionChangeAzothCost(player), 0, "the change costs 0 Azoth (the data of this build)")

-- 3. No faction: the join check passes (no achievement check with no faction; no wait).
equal(FactionRequestBus.Event.CanSetFactionWithResults(player, eFactionType_Faction1, false), eCanSetFactionResults_Success, "the join check")

-- 4. The Faction state: a join (the Syndicate, time 0, count 0). Each received faction field publishes the node, also the same value.
local dataLayer = RequireScript("LyShineUI.UiDataLayer")
local seen = {}
local watcher = {}
dataLayer:RegisterDataObserver(watcher, "Hud.LocalPlayer.Faction", function(_, value) seen[#seen + 1] = value end)
Swc.HostFaction(1, "0", 0)
frames(0.1)
Swc.HostFaction(1, "0", 0)
frames(0.1)
check(#seen >= 2 and seen[#seen] == 1 and seen[#seen - 1] == 1, "the same faction twice: two publishes of the node (" .. #seen .. ")")
equal(Swc.GetData("Hud.LocalPlayer.Faction"), 1, "the HUD faction is the Syndicate")
check(Swc.GetData("Hud.LocalPlayer.FactionRequestBus.IsConnected") == true, "the Faction bus is connected")

-- 5. A change with the achievement check (the Bio panel): no recruitment achievement of the Marauders refuses it; the time 0 of a join
-- has no wait.
equal(FactionRequestBus.Event.CanSetFactionWithResults(player, eFactionType_Faction2, true), eCanSetFactionResults_FailAchievementRequirement,
  "a change with the check and no recruitment achievement")
equal(FactionRequestBus.Event.CanSetFactionWithResults(player, eFactionType_Faction2, false), eCanSetFactionResults_Success,
  "a change with no check (the poster) and the time 0 of a join: no wait")

-- 6. The Bio tab with the Syndicate.
Swc.HostRanks(string.format("%d 1 120\n%d 0 35", 0x56FBDE12, 0xEBD7B082))
LyShineManagerBus.Broadcast.SetState(SKILLS)
frames(0.5)
DynamicBus.SkillsBus.Broadcast.OpenBio()
frames(1.5)
local bio = texts_of(skills)
note("the bio tab: " .. bio)
check(bio:lower():find("syndicate", 1, true) ~= nil, "the bio tab names the Syndicate")
-- The wallet of the faction (bio.luac UpdateWallet, after Hud.LocalPlayer.CategoricalProgressionRequestBus.IsConnected): the rank name,
-- the reputation, and the tokens; before the node, the tab kept the canvas text 2000 / 2000.
check(bio:find("Scrivener | 120 |", 1, true) ~= nil, "the reputation of the Syndicate: the rank name Scrivener and 120 points")
check(bio:find("Faction Tokens | 35", 1, true) ~= nil, "the tokens of the Syndicate: 35")
check(not bio:find("2000 / 2000", 1, true), "no canvas default 2000 / 2000")
-- The cap of a rank (the tooltip @owg_influence_to_rank; crafting.json ranks: MerchantGuild rank 1 has MaximumInfluence 11000, SyndicateTokens
-- rank 0 has 15000).
if (swcui.load_json("gamedata/crafting.json").ranks or {}).MerchantGuild then
  equal(CategoricalProgressionRequestBus.Event.GetMaxPointsForRank(player, 0x56FBDE12, 1), 11000, "the cap of rank 1 of the Syndicate reputation")
  equal(CategoricalProgressionRequestBus.Event.GetMaxPointsForRank(player, 0xEBD7B082, 0), 15000, "the cap of rank 0 of the Syndicate tokens")
else
  note("crafting.json has no faction rank table (tools/convert-ui.sh crafting of 2a): no cap check")
end
picture("factions-bio")
equal(swcui.error_count() - errorsBefore, 0, "the bio tab with a faction: no Lua error")

-- 7. The selection screen of Change Faction (bio.luac OnChangeFactionPressed sets the state).
LyShineManagerBus.Broadcast.SetState(SELECTION)
frames(1.5)
local selectionText = texts_of(selection)
note("the selection screen: " .. selectionText)
check(selectionText ~= "", "the selection screen shows")
picture("factions-selection")
equal(swcui.error_count() - errorsBefore, 0, "the selection screen: no Lua error")

-- 8. The request (the Yes of the selection or of the poster): "faction set N" (3161) and the HUD faction at once.
requests()
FactionRequestBus.Event.RequestSetFaction(player, eFactionType_Faction2)
frames(0.1)
local sent = false
for _, r in ipairs(requests()) do sent = sent or r == "faction set 2" end
check(sent, "RequestSetFaction asks the host for 3161 with the Marauders")
equal(Swc.GetData("Hud.LocalPlayer.Faction"), 2, "the client shows its request at once (0x30C00B0)")
-- A refusal: the server answers with its faction (the Syndicate); the HUD gets it again.
Swc.HostFaction(1, "0", 0)
frames(0.1)
equal(Swc.GetData("Hud.LocalPlayer.Faction"), 1, "the answer to a refused request gives the HUD the faction of the server")

-- 9. A change now: the wait of 30 days.
local nowNs = string.format("%d000000000", math.floor(WallClockTimePoint:Now().seconds))
Swc.HostFaction(2, nowNs, 1)
frames(0.1)
equal(FactionRequestBus.Event.CanSetFactionWithResults(player, eFactionType_Faction3, false), eCanSetFactionResults_OnCooldown, "a change in the wait")
local left = FactionRequestBus.Event.GetCooldownEndTime(player):Subtract(WallClockTimePoint:Now()):ToSeconds()
near(left, 2592000, 2, "the wait ends 30 days after the change")
LyShineManagerBus.Broadcast.SetState(HUD)
frames(0.5)
equal(swcui.error_count() - errorsBefore, 0, "no Lua error")
