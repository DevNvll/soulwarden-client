-- The notices of the HUD in the world views (stage WF7; docs/UI.md "The world views"): the damage numbers and the heal numbers of the
-- original canvas lyshineui/hud/damagenumbers/damagenumbers.dynamicuicanvas (damagenumber.luac), and the flags of the loot ticker
-- (lyshineui/hud2024/lootticker/lootticker2024.luac; its events come from the bag feed of swc/items.lua and the progression feed).
--
-- The native damage numbers of the game (not in the scripts) keep a pool of copies of the template element, put a copy at the screen
-- point of the target, and send it the event (DamageNumbersNotificationBus OnCombinedDamageDisplayed); the script animates the
-- element in it and gives it back (DamageNumbersBus OnAnimDone). This module does it from the events of the client (Swc.HostDamage):
--   - enemy: the damage number of the player (2071; USwcCreatureSubsystem::GetDamageEvents), at the point of the creature, the place
--     updated for 10 frames (Hud.LocalPlayer.DamageNumbers.Enemy.UpdatePositionForFrames of damagenumber.luac);
--   - player: the damage that the own character took (3601; OwnEntity::damages), the red number (isLocalPlayerDamage);
--   - heal: a heal of the own character (OwnEntity::heals): a negative amount, the green "+N";
--   the player and heal numbers at 1.6 m above the feet and 0.5 m to the right (ZOffsetBase, HorizontalOffsetBase of the Player
--   settings of damagenumber.luac; the C++ makes the point), with no place update (UpdatePositionForFrames 0).
-- The entries: the damage type of the wire number (gamedata/damagetypes.json), the amount, the part before the defense; the weakness
-- and the absorption of the target for that type (gamedata/defenses.json: the defense row of the vitals row of the creature, from the
-- backend copy; the client reads them from its own tables: protocol notes 4.15, the yellow number with the arrow for a weakness).

local Swc = Swc

local N = {pool = {}, damageTypes = nil, defenses = nil}
Swc.notices = N

-- The flags of the ticker and of the numbers. g_enableNewProgressionTicker: the registration of the client settings at image RVA
-- 0x4A3A1F7 (a bool, true). UseNewDamageNumbers (the option "floating" of options.luac; the native default is not known): the
-- floating numbers, an inference from the retail pictures (the numbers of one target in several directions: Soulwarden evidence
-- fight F12).
Swc.config["UIFeatures.g_enableNewProgressionTicker"] = true
Swc.SetData("UIFeatures.g_enableNewProgressionTicker", true)
-- The XP multiplier of the character (progressiontickeritem.luac: the boost text of the XP line): the setting
-- javelin.xp-multipliers.character, a float of the registration function at image RVA 0x616C670 (0x61747F3) with the value 1.0
-- (the constant at 0x7EFE900, in xmm7 from 0x616D116).
Swc.config["javelin.xp-multipliers.character"] = 1.0
Swc.SetData("javelin.xp-multipliers.character", 1.0)
Swc.SetData("Hud.LocalPlayer.Options.Video.UseNewDamageNumbers", true)

local function damage_types()
  if not N.damageTypes then
    N.damageTypes = swcui.load_json("gamedata/damagetypes.json") or {}
  end
  return N.damageTypes
end

local function defenses()
  if not N.defenses then
    N.defenses = swcui.load_json("gamedata/defenses.json") or {}
  end
  return N.defenses
end

-- One damage entry of the native data (the script reads damageAmount, damageMitigation, and GetDamageTypeString).
local Entry = {}
Entry.__index = Entry
function Entry:GetDamageTypeString() return self.typeName end

-- ------------------------------------------------------------------------------------------------------- the pool

local function child(id, name)
  local found = id and swcui.find_descendant(id, name)
  if found and found:IsValid() then
    return found
  end
  return nil
end

local function numbers_canvas()
  for _, screen in pairs(Swc.screens or {}) do
    if string.find(string.lower(swcui.canvas_get(screen.canvas, "path") or ""), "damagenumbers/damagenumbers", 1, true) then
      return screen.canvas
    end
  end
  return nil
end

local function template()
  local canvas = numbers_canvas()
  if not canvas then
    return nil
  end
  local name = Swc.GetData("Hud.LocalPlayer.Options.Video.UseNewDamageNumbers") and "DamageNumberContainerV2" or "DamageNumberContainer"
  return child(swcui.canvas_get(canvas, "root"), name)
end

local function take()
  for _, number in ipairs(N.pool) do
    if not number.busy then
      return number
    end
  end
  local source = template()
  if not source then
    return nil
  end
  local number = {id = Swc.CloneElement(source, swcui.parent(source), nil)}
  N.pool[#N.pool + 1] = number
  return number
end

Swc.Define("DamageNumbersBus", "Broadcast", {
  OnAnimDone = function(id)
    for _, number in ipairs(N.pool) do
      if number.id == id then
        number.busy = false
        UiElementBus.Event.SetIsEnabled(id, false)
      end
    end
  end,
})
Swc.Handled("DamageNumbersNotificationBus")

-- The index of a generic number element (damagenumber.luac OnInit: the data layer call 2201955587 gives the index of the data paths
-- DamageNumbers.OnGenericTextDisplayed.<i>; the name of the call is not known). No generic text event in this client.
local genericIndex = 0
Swc.callValues[2201955587] = function()
  genericIndex = genericIndex + 1
  return genericIndex
end

local function place(number)
  local project = Swc.markers and Swc.markers.project
  local sx, sy
  if project then
    sx, sy = project(number.x, number.y, number.z)
  end
  if sx then
    swcui.set_viewport_position(number.id, sx, sy)
  end
end

--- One damage or heal event: tab-separated fields (kind enemy|player|heal, interest, the point x y z in Unreal cm, the flags of the
--- message, the squared distance from the character (m^2), the vitals id of the target, the entries "TYPE:AMOUNT:PART,...").
function Swc.HostDamage(line)
  local f = {}
  for field in string.gmatch((line or "") .. "\t", "([^\t]*)\t") do
    f[#f + 1] = field
  end
  if #f < 9 then
    return
  end
  local kind, interest = f[1], tonumber(f[2]) or 0
  local flags, distSq, vitals = tonumber(f[6]) or 0, tonumber(f[7]) or 0, f[8]
  local defense = defenses()[vitals] or {}
  local entries, absorbWeak = {}, {}
  for typeNumber, amount, part in string.gmatch(f[9], "(%d+):([-%d.]+):([%d.]+)") do
    local row = damage_types()[typeNumber]
    local typeName = row and row.id or "Standard"
    local value = tonumber(amount) or 0
    if kind == "heal" then
      value = -math.abs(value)
    end
    entries[#entries + 1] = setmetatable({damageAmount = value, damageMitigation = tonumber(part) or 1, typeName = typeName}, Entry)
    absorbWeak[#absorbWeak + 1] = {first = (defense.abs or {})[typeName] or 0, second = (defense.weak or {})[typeName] or 0}
  end
  if #entries == 0 then
    return
  end
  local number = take()
  if not number then
    return
  end
  -- The special number (orange): bit 6 Crit, or bit 8 Headshot with bit 0 Ranged and bit 5 CanCrit (protocol notes 4.30, live L6).
  local crit = (math.floor(flags / 64) % 2 == 1) or (math.floor(flags / 256) % 2 == 1 and flags % 2 == 1 and math.floor(flags / 32) % 2 == 1)
  local displaying = 0
  for _, other in ipairs(N.pool) do
    if other.busy and other.interest == interest and other.kind == kind then
      displaying = displaying + 1
    end
  end
  number.busy, number.kind, number.interest = true, kind, interest
  number.x, number.y, number.z = tonumber(f[3]) or 0, tonumber(f[4]) or 0, tonumber(f[5]) or 0
  number.frames = kind == "enemy" and 10 or 0
  place(number)
  local marker = ""
  for _, slot in pairs(Swc.markers and Swc.markers.slots or {}) do
    if slot.plate and slot.plate.interest == interest and slot.markerId then
      marker = tostring(slot.markerId)
    end
  end
  Swc.Notify("DamageNumbersNotificationBus", number.id, "OnCombinedDamageDisplayed", entries, crit, false, distSq, displaying,
    {value = marker}, kind ~= "enemy", absorbWeak, "")
end

--- Each frame: the place of the enemy numbers for their first frames (UpdatePositionForFrames).
function Swc.TickNotices()
  for _, number in ipairs(N.pool) do
    if number.busy and number.frames > 0 then
      number.frames = number.frames - 1
      place(number)
    end
  end
end
