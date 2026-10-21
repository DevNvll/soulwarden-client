-- The status tray fixture (stage MVP-I): the original canvas lyshineui/statuseffects/statuseffectsv2.dynamicuicanvas (statuseffects.luac,
-- statuseffect.luac) with the native part of swc/statustray.lua. The status map of the owner (Swc.HostStatusTray: "index potency
-- stacks end" lines; the indexes of the backend copy, gamedata/statustray.json): the stamina regen of the chest perk with no end
-- (index 0; the retail picture of set 6 at low health: the icon with "—"), a burn with 30 s and two stacks (index 1300, negative), a
-- status with no tray row (index 1 is not in the tray data: no icon). Then the burn ends, then the map is empty.
-- Pictures: tray-two, tray-one.
local canvas = screen("LyShineUI/StatusEffects/StatusEffectsV2.uicanvas", 5, true)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
frames(0.3)

local function icons()
  local list = {}
  local root = swcui.canvas_get(canvas, "root")
  local function walk(id)
    for _, c in ipairs(swcui.children(id)) do
      if string.match(swcui.name(c), "^StatusEffect%d$") and swcui.enabled_in_hierarchy(c) then
        list[#list + 1] = c
      end
      walk(c)
    end
  end
  walk(root)
  return list
end

local tray = swcui.load_json("gamedata/statustray.json") or {index = {}}
check(tray.index["0"] == "Status_Perk_Chest_HealthLowStamRegen", "the tray data: index 0 is the stamina regen of the chest perk")
check(tray.index["1300"] == "Status_ExplosiveShot_Burn", "the tray data: index 1300 is the burn of Explosive Arrow")
check(tray.index["1"] == nil, "the tray data: index 1 has no tray row")

local now = WallClockTimePoint:Now().seconds
Swc.HostStatusTray(string.format("0 1.0 1 0\n1300 1.0 2 %.3f\n1 1.0 1 0\n", now + 30))
frames(1.5)
equal(#icons(), 2, "two icons: the perk status and the burn")
check(Swc.HostPointOf("text:\226\128\148") ~= "", "the status with no end: the duration text is a dash")
check(Swc.HostPointOf("text:\195\1512") ~= "", "the burn: the stack count x2")
check(Swc.HostPointOf("text:30s") ~= "" or Swc.HostPointOf("text:29s") ~= "", "the burn: the time left in seconds (30s; the label @ui_seconds_short in the text)")
picture("tray-two")

Swc.HostStatusTray("0 1.0 1 0\n")
frames(1.5)
equal(#icons(), 1, "the burn ends: one icon")
picture("tray-one")

Swc.HostStatusTray("")
frames(1.5)
equal(#icons(), 0, "the map is empty: no icon")
