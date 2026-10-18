-- The loading and error UI fixture. The loading screen of the game (loadingscreen.luac; globals.luac registers it with
-- LoadScreenBus) shows at the start of the host and hides at the spawn, with the events of the native code. A refused
-- connection of the login path gives the original error popup (popup.luac ShowError with UIError_login_unable_to_connect
-- of uierrors.luac): its title and body texts of the game, the cursor, and its OK button closes it.
-- Pictures: loading-start, loading-later, loading-error.
check(Swc.loading.path ~= nil, "globals.luac registers the loading screen: " .. tostring(Swc.loading.path))
Swc.HostLoading(true, "newworld_vitaeeterna")
frames(0.5)
local canvas = Swc.loading.canvas
check(canvas ~= nil and swcui.canvas_get(canvas, "enabled"), "the loading screen shows")
check(LoadScreenBus.Broadcast.IsLoadingScreenShown(), "LoadScreenBus tells that it shows")
equal(swcui.canvas_get(canvas, "drawOrder"), 1000, "the draw order of the loading screen")
picture("loading-start")
frames(3)
picture("loading-later")
Swc.HostLoading(false)
frames(0.2)
check(not swcui.canvas_get(canvas, "enabled"), "the loading screen hides at the spawn")
check(not LoadScreenBus.Broadcast.IsLoadingScreenShown(), "LoadScreenBus tells that it hides")

-- A refused connection.
allow("LyShineUI/Popup/Popup")
Swc.HostLoading(true, "newworld_vitaeeterna")
frames(0.3)
Swc.HostConnectionError()
frames(0.8)
check(not Swc.loading.shown, "the error hides the loading screen")
local popup
for _, screen in pairs(Swc.screens) do
  if string.lower(screen.name) == "popup" then
    popup = screen.canvas
  end
end
check(popup ~= nil and swcui.canvas_get(popup, "enabled"), "the error popup shows")
check(Swc.HostCursor(), "the error popup has the cursor")
if popup then
  local root = swcui.canvas_get(popup, "root")
  local message = find(root, "PopupMessage")
  local text = message and swcui.text_layout(message)
  local shown = text and text.lines[1] and text.lines[1].text or ""
  check(shown ~= "", "the popup has the text of the error (" .. shown:sub(1, 60) .. ")")
  equal(swcui.localize("@mm_loginservices_UnableToConnectError") ~= "@mm_loginservices_UnableToConnectError", true,
    "the body key of the error has an English text")
  picture("loading-error")
  local ok = nil
  for _, name in ipairs({"ButtonOk", "ButtonYes"}) do
    local b = find(root, name)
    if b and swcui.enabled_in_hierarchy(b) and not ok then
      ok = b
    end
  end
  check(ok ~= nil, "the error popup has a shown OK button")
  if ok then
    click(ok)
    frames(0.6)
    check(not swcui.canvas_get(popup, "enabled"), "OK closes the error popup")
    check(not Swc.HostCursor(), "no cursor after the popup")
  end
end
