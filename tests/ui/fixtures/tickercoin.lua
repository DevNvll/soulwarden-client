-- The coin rows of the loot ticker (user test 2026-10-26, 8c: "the loot feed was stuck at +5.40 coins"). The original
-- lootticker2024.luac and progressiontickeritem.luac with the coin of the host (Swc.HostCoin, hundredths):
--   * The first value of Hud.LocalPlayer.Currency.Amount is the start total (no row). The client gives the wallet before PlayerSpawned,
--     and PlayerSpawned writes no 0 before the first wallet (before: 0, then the wallet showed as a gain).
--   * A gain shows a row "+N.NN"; a second gain while the row shows adds to it (UpdateValue); the row goes 2 s + 3 s after the last
--     gain (QueueFadeOut: the fade out with a delay of 2 s and a time of 3 s, then the row hides). Before, the fade in with no delay of
--     the same call removed the queued fade out (the tween rule of swc/tween.lua), and each new row stayed.
--   * A gain while the HUD is hidden (another UI state) goes too.
-- Pictures: tickercoin-row.
Swc.HostProgression(18, 1000)
local ticker = screen("LyShineUI/HUD2024/LootTicker/LootTicker2024.uicanvas", 20, true)
Swc.HostCoin(1000)
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostBagSize(6)
frames(0.3)

-- The shown texts of the ticker (the enabled elements; a row that hides at the end of its fade is disabled).
local function texts()
  local out = {}
  local function walk(id, depth)
    if depth > 20 then return end
    local t = swcui.has(id, "text") and swcui.get(id, "text.text")
    if t and t ~= "" and swcui.enabled_in_hierarchy(id) then out[#out + 1] = t end
    for _, c in ipairs(swcui.children(id)) do walk(c, depth + 1) end
  end
  walk(swcui.canvas_get(ticker, "root"), 0)
  return table.concat(out, " | ")
end
local function shows(text)
  return string.find(texts(), text, 1, true) ~= nil
end

check(not shows("+"), "the wallet of the world entry shows no row: " .. texts())

-- One gain: the row shows, and it goes after 2 s + 3 s.
Swc.HostCoin(1540)
frames(1.0)
check(shows("+5.40"), "a gain of 5.40 shows the row +5.40: " .. texts())
picture("tickercoin-row")
frames(3.5)
check(shows("+5.40"), "the row stays during its delay and its fade (4.5 s)")
frames(1.5)
check(not shows("+5.40"), "the row goes after 5 s: " .. texts())

-- Two gains 1 s apart: one row with the sum; it goes 5 s after the second gain.
Swc.HostCoin(1640)
frames(1.0)
Swc.HostCoin(1765)
frames(1.0)
check(shows("+2.25"), "a second gain adds to the shown row (+1.00 and +1.25 = +2.25): " .. texts())
frames(3.5)
check(shows("+2.25"), "the merged row stays 4.5 s after the second gain")
frames(1.5)
check(not shows("+2.25"), "the merged row goes 5 s after the second gain: " .. texts())

-- A gain while the HUD is hidden (the escape menu state): the row goes too.
LyShineManagerBus.Broadcast.SetState(1643432462)
frames(0.5)
Swc.HostCoin(1805)
frames(2.0)
LyShineManagerBus.Broadcast.SetState(2702338936)
frames(4.0)
check(not shows("+0.40"), "a row of a gain while the HUD was hidden goes: " .. texts())
equal(swcui.error_count(), 0, "no Lua error")
