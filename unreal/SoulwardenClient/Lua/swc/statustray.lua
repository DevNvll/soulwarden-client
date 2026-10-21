-- The status tray of the own character (stage MVP-I; docs/UI.md "The fight HUD"): the original canvas
-- lyshineui/statuseffects/statuseffectsv2.dynamicuicanvas (statuseffects.luac, statuseffect.luac) above the health bar.
--
-- The native part (not in the scripts): the status component of the player fills the data nodes of the eight elements of the tray,
-- Hud.LocalPlayer.StatusEffects.N (N = 1 to 8; statuseffects.luac SetStatusEffectDataPath): .Potency, .EndTime (a wall clock time
-- point; statuseffect.luac UpdateDuration subtracts GetCurrentSyncedWallClockTime), .StackSize, and .EffectId (the status id; an
-- empty id ends the element). StatusEffectsRequestBus GetStatusEffectDataByCrc(player, the CRC-32 of the lower-case id) gives the
-- icon, the name, the description, and isNegative of the row.
-- This module does it from the status map of the owner (slot 44; Swc.HostStatusTray: "index potency stacks end" lines, the end in
-- Unix seconds on the wall clock of the server, 0: no end). The rows: gamedata/statustray.json (the status rows with ShowInUITray;
-- the index of the state -> the id, from the backend copy). A status keeps its element while it stays; a new status takes the first
-- free element; with more than eight, the rows with the higher UIPriority come first. The order rule of the native code is not
-- known: this is an inference. The retail picture (set 6 ui-fight-hud.mp4): no icon at full health; the stamina regen icon of the
-- chest perk at low health (Status_Perk_Chest_HealthLowStamRegen, no end).

local Swc = Swc

local T = {data = nil, slots = {}}
Swc.statustray = T

local SLOTS = 8

local function data()
  if not T.data then
    T.data = swcui.load_json("gamedata/statustray.json") or {}
    T.data.rows = T.data.rows or {}
    T.data.index = T.data.index or {}
  end
  return T.data
end

local function crc_rows()
  if not T.byCrc then
    T.byCrc = {}
    for id, row in pairs(data().rows) do
      T.byCrc[swcui.crc32(string.lower(id))] = row
    end
  end
  return T.byCrc
end

local function path(slot, field)
  return "Hud.LocalPlayer.StatusEffects." .. tostring(slot) .. "." .. field
end

local function set_slot(slot, status)
  local endTime = status.ends > 0 and status.ends or 0
  Swc.SetData(path(slot, "Potency"), status.potency)
  Swc.SetData(path(slot, "EndTime"), Swc.WallClockTimePoint.new({seconds = endTime}))
  Swc.SetData(path(slot, "StackSize"), status.stacks)
  Swc.SetData(path(slot, "EffectId"), status.id)
end

--- The status map of the owner for the tray: "index potency stacks end" lines.
function Swc.HostStatusTray(text)
  local d = data()
  local list, byId = {}, {}
  for line in string.gmatch(text or "", "[^\n]+") do
    local index, potency, stacks, ends = string.match(line, "^(%d+) (%S+) (%d+) (%S+)$")
    local id = index and d.index[index]
    local row = id and d.rows[id]
    if row and not byId[id] then
      local status = {id = id, row = row, potency = tonumber(potency) or 1, stacks = math.max(tonumber(stacks) or 1, 1),
        ends = tonumber(ends) or 0, order = #list}
      list[#list + 1] = status
      byId[id] = status
    end
  end
  -- The statuses that the tray can show: the higher UIPriority first (more than eight), then the order of the map.
  table.sort(list, function(a, b)
    if (a.row.priority or 0) ~= (b.row.priority or 0) then
      return (a.row.priority or 0) > (b.row.priority or 0)
    end
    return a.order < b.order
  end)
  local keep = {}
  for i = 1, math.min(#list, SLOTS) do
    keep[list[i].id] = list[i]
  end
  -- The elements of the statuses that end; then the statuses that stay keep their element.
  local placed = {}
  for slot = 1, SLOTS do
    local id = T.slots[slot]
    if id and keep[id] then
      placed[id] = slot
      local status, last = keep[id], T.last and T.last[id]
      if not last or last.potency ~= status.potency or last.stacks ~= status.stacks or last.ends ~= status.ends then
        set_slot(slot, status)
      end
    elseif id then
      T.slots[slot] = nil
      Swc.SetData(path(slot, "EffectId"), "")
    end
  end
  for i = 1, math.min(#list, SLOTS) do
    local status = list[i]
    if not placed[status.id] then
      for slot = 1, SLOTS do
        if not T.slots[slot] then
          T.slots[slot] = status.id
          placed[status.id] = slot
          set_slot(slot, status)
          break
        end
      end
    end
  end
  T.last = keep
end

Swc.Define("StatusEffectsRequestBus", "Event", {
  GetStatusEffectDataByCrc = function(_, crc)
    local row = crc_rows()[crc]
    if not row then
      return nil
    end
    return {icon = row.icon, name = row.name, description = row.description, isNegative = row.negative == true}
  end,
})
