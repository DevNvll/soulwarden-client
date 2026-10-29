-- The factions of the player (QG7, 2a of ut567; protocol notes 4.63; Soulwarden docs/systems/factions.md): the native part of the
-- FactionRequestBus and of the JavSocialComponentBus that the faction poster (changefaction/changefactionposter.luac), the selection
-- screen (changefaction/factionselection.luac, factionselectionpanel.luac), the Bio tab (skills/bio.luac), and the HUD read.
--
-- The host gives the Faction state of the owner (slot 49: the faction, the time of the last change in Unix nanoseconds, the count) with
-- Swc.HostFaction, and the empty Social state (slot 55) with Swc.HostSocial. RequestSetFaction sends "faction set N" (3161). The retail
-- client publishes the requested faction before the answer (0x30C00B0), and each received faction field again, also the same value
-- (0x30B6210): the answer to a refused request gives the HUD the faction of the server (conversationscreenv2 ConfirmFaction waits for it).

local Swc = Swc
local F = {faction = 0, changedSeconds = 0, count = 0}
Swc.factions = F

local FACTION_NONE, FACTION_ANY = eFactionType_None or 0, eFactionType_Any or 4
-- The progression ids of the reputation and the tokens of each faction: the CRC-32 of the lower-case id (the backend table:
-- Syndicate 56FBDE12, SyndicateTokens EBD7B082, Marauder 540B63C5, MarauderTokens 89599279, Covenant F4EB80F7, CovenantTokens A0989588).
local REPUTATION = {[1] = "syndicate", [2] = "marauder", [3] = "covenant"}
local TOKENS = {[1] = "syndicatetokens", [2] = "maraudertokens", [3] = "covenanttokens"}
-- The recruitment achievements of the FactionData rows (RequiredAchievementId, RequiredLegacyAchievementId; static read QG7 S6).
local RECRUITMENT = {
  [1] = {"FactionIntro_Syndicate_Recruitment", "S_syndicatequest_1"},
  [2] = {"FactionIntro_Marauders_Recruitment", "S_marauderquest_1"},
  [3] = {"FactionIntro_Covenant_Recruitment", "S_covenantquest_1"},
}
-- Faction Change Cooldown Seconds of the Faction Data (playerbaseattributes.pbadb): 30 days.
local COOLDOWN_SECONDS = 2592000

local function result(name, value)
  return _G["eCanSetFactionResults_" .. name] or value
end

local function now_seconds()
  return WallClockTimePoint:Now().seconds or 0
end

local function cooldown_end()
  -- The getter 0x30B1DE0: the time of the last change plus the wait, also for the time 0 (it ends in 1970: no wait).
  return F.changedSeconds + COOLDOWN_SECONDS
end

local function has_recruitment(faction)
  for _, name in ipairs(RECRUITMENT[faction] or {}) do
    local bit = Swc.AchievementIndex and Swc.AchievementIndex(name)
    if bit and Swc.HasAchievement(bit) then
      return true
    end
  end
  return false
end

--- The check of the client (CanSetFactionWithResults 0x30AD140) in its order, with the states that this offline world has: the join
--- level (1), the changing flag (on), the optional achievement check, factionless allowed, no PvP flag, no sanctuary rule, the wait, the
--- cost (0 in this build), no war, no territory rule, no company. The client has no refusal for the same faction (the backend has one).
function F.can_set(faction, checkAchievement)
  if checkAchievement and F.faction ~= FACTION_NONE and faction ~= FACTION_ANY and not has_recruitment(faction) then
    return result("FailAchievementRequirement", 10)
  end
  if cooldown_end() > now_seconds() then
    return result("OnCooldown", 5)
  end
  return result("Success", 0)
end

local function republish()
  Swc.RepublishData("Hud.LocalPlayer.Faction", F.faction)
end

Swc.Define("FactionRequestBus", "Event", {
  GetFaction = function() return F.faction end,
  GetFactionReputationProgressionIdFromType = function(_, faction)
    local name = REPUTATION[faction]
    return name and swcui.crc32(name) or 0
  end,
  GetFactionTokensProgressionIdFromType = function(_, faction)
    local name = TOKENS[faction]
    return name and swcui.crc32(name) or 0
  end,
  -- The cost (0x30B1EC0): 0 with no faction or under the level of the cost; else min(Max, Min + count x Increment): Min 0, Max 0 in this
  -- build, thus 0.
  GetFactionChangeAzothCost = function() return 0 end,
  GetCooldownEndTime = function() return Swc.WallClockTimePoint.new({seconds = cooldown_end()}) end,
  GetFactionChangeCount = function() return F.count end,
  CanSetFactionWithResults = function(_, faction, checkAchievement) return F.can_set(faction, checkAchievement) end,
  RequestSetFaction = function(_, faction)
    if faction == nil or faction < 1 or faction > 3 then
      return
    end
    Swc.hostRequests[#Swc.hostRequests + 1] = "faction set " .. tostring(faction)
    -- The client shows its request at once (0x30C00B0 calls the HUD setter); the answer of the server sets it again.
    F.faction = faction
    republish()
  end,
})

-- The Social component of the character (QG7): empty lists. The poster passes GetFriends to socialDataHandler:GetPlayers_ServerCall, which
-- ends at once for an empty list; RefreshStats reads the friends in a faction.
Swc.Define("JavSocialComponentBus", "Broadcast", {
  GetFriends = function() return {} end,
  GetFriendCountInFaction = function() return 0 end,
  IsReady = function() return F.socialReady == true end,
  -- The title part of the Social state (group 0: the title 0, the pronoun 0): skills/titlesection.luac reads it when the bus is ready.
  -- No title categories: the title list is not part of this stage (an open point).
  GetActiveTitleId = function() return 0 end,
  GetNeutralTitleId = function(title) return title or 0 end,
  GetPronounType = function() return 0 end,
  GetAllTitleCategoryIds = function() return {} end,
})

-- The selection screen of the Bio tab: the retail dynamic canvas loader loads it when the state 1040880896 starts (dynamiccanvasloader.luac
-- canvasInfos: FactionSelection, LyShineUI/ChangeFaction/FactionSelection, autohide, POPUP_DRAW_ORDER of CanvasCommon); that loader does
-- not run in the client, so this file loads it in the same way (bio.luac OnChangeFactionPressed sets the state).
Swc.stateCanvases[1040880896] = function()
  if F.selectionLoaded then
    return
  end
  local common = RequireScript("LyShineUI._Common.CanvasCommon")
  F.selectionLoaded = Swc.LoadScreen("LyShineUI/ChangeFaction/FactionSelection.uicanvas", true, common and common.POPUP_DRAW_ORDER or 0) ~= nil
  Swc.Note("factions: the screen FactionSelection loads")
end

if Swc.GetData("Hud.LocalPlayer.Faction") == nil then
  Swc.SetData("Hud.LocalPlayer.Faction", FACTION_NONE)
end

--- The Faction state of the owner (the host, each received faction field): the faction, the time of the last change (Unix nanoseconds as a
--- text: 64 bits), and the count of the changes.
function Swc.HostFaction(faction, changedNs, count)
  F.faction = tonumber(faction) or 0
  local ns = tostring(changedNs or "0")
  -- Seconds from the text: the last 9 digits are the nanoseconds.
  F.changedSeconds = #ns > 9 and (tonumber(string.sub(ns, 1, #ns - 9)) or 0) or 0
  F.count = tonumber(count) or 0
  Swc.SetData("Hud.LocalPlayer.FactionRequestBus.IsConnected", true)
  republish()
end

--- The empty Social state (the host, one time a session): the friend containers exist.
function Swc.HostSocial()
  F.socialReady = true
  Swc.SetData("Hud.LocalPlayer.FactionRequestBus.IsConnected", true)
  Swc.SetData("Hud.LocalPlayer.JavSocialComponentBus.IsReady", true)
end

return F
