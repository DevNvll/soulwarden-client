-- The camera fixture (ut567; docs/UI.md "The camera of the screens"): the rules of swc/camera.lua with no screen. The stack of the state
-- overrides, the look-at point, the depth of field, and the requests to the client (Swc.TakeCameraRequest).
local C = JavCameraControllerRequestBus.Broadcast
local D = JavelinCameraRequestBus.Broadcast

local function take()
  local out = {}
  for _ = 1, 32 do
    local r = Swc.TakeCameraRequest()
    if r == "" then break end
    out[#out + 1] = r
  end
  return table.concat(out, " | ")
end
take()

-- The stack: the last override is on top; a second override of one name moves it up; a restore of a name removes that name only.
C.OverrideCameraState("UI_CraftingStep1", 0.5)
C.OverrideCameraState("UI_CraftingStep3", 0.2)
equal(take(), "camera state UI_CraftingStep1 0.500 | camera state UI_CraftingStep3 0.200", "two overrides")
C.RestoreCameraState("UI_CraftingStep3", 0.2)
equal(take(), "camera state UI_CraftingStep1 0.200", "the restore of the top name gives the state below it")
C.OverrideCameraState("UI_NavBar", 0.25)
C.OverrideCameraState("UI_CraftingStep1", 0.5)
equal(#Swc.camera.stack, 2, "one entry for each name")
equal(Swc.camera.stack[2].name, "UI_CraftingStep1", "a second override of a name moves it up")
C.RestoreCameraState("UI_NavBar", 0.25)
take()
C.RestoreCameraState("UI_Inventory", 0.5)
equal(take(), "", "the restore of a name that is not on the stack sends nothing")
-- A restore of "" removes all (the full exits of craftingscreenv4.luac and conversationscreenv2.luac).
C.OverrideCameraState("UI_CraftingStep3", 0.2)
take()
C.RestoreCameraState("", 0.5)
equal(take(), "camera state - 0.500", "the restore of \"\" removes all")
equal(#Swc.camera.stack, 0, "the stack is empty")
C.OverrideCameraState("UI_Inventory", 0.5)
C.ClearAllStateOverrides()
equal(take(), "camera state UI_Inventory 0.500 | camera state - 0.000", "ClearAllStateOverrides")
C.OverrideCameraState("", 0.5)
equal(take(), "", "an override with no name sends nothing")

-- The look-at point (meters): GetNpcLookAtPosition gives a Vector3.
C.SetCameraLookAt(Vector3(7420.11, 3623.74, 104.99), false)
equal(take(), "camera lookat 7420.110 3623.740 104.990", "the look-at point")
C.ClearCameraLookAt()
C.ClearCameraLookAt()
equal(take(), "camera lookat -", "one request for the clear")

-- The depth of field: no request while it is off; the scripts read back the values that they set; one request for each change.
D.SetDepthOfField(6, 0.4, 0, 0, 10)
equal(take(), "", "no request while the depth of field is off")
equal(D.GetCurrentDepthOfFieldDistance(), 6, "the stored distance")
equal(D.GetCurrentDepthOfFieldBlur(), 0.4, "the stored blur")
D.EnableDepthOfField(true)
D.SetDepthOfField(6, 0.4, 0, 0, 10)
equal(take(), "camera dof 6.000 0.400 0.000 0.000 10.000", "the depth of field on, and no second request for the same values")
D.SetDepthOfField(0, 0, 0, 0, 10)
equal(take(), "camera dof -", "a blur of 0 is no depth of field")
D.SetDepthOfField(0, 0.95, 0, 0, 10)
D.ResetDepthOfField()
equal(take(), "camera dof 0.000 0.950 0.000 0.000 10.000 | camera dof -", "the reset")
equal(D.GetCurrentDepthOfFieldBlur(), 0, "the reset values")

-- The entry of a world clears all (Swc.EnterWorld).
C.OverrideCameraState("UI_Inventory", 0.5)
C.SetCameraLookAt(Vector3(1, 2, 3), false)
Swc.CameraReset()
equal(#Swc.camera.stack + (Swc.camera.lookAt and 1 or 0), 0, "Swc.CameraReset")
equal(take(), "", "Swc.CameraReset drops the requests that the client did not take")

-- The data layer gives value objects as copies (as the value types of the native data layer): a script that sets the z of its copy to 0
-- does not change the node (GetNpcLookAtPosition read the player with z 0 in the client run s1 of ut567).
Swc.HostView(-107, 12059.64, 8048.92, 252.17)
local own = LyShineDataLayerBus.Broadcast.GetDataFromNode("Hud.LocalPlayer.Position")
own.z = 0
near(Swc.GetData("Hud.LocalPlayer.Position").z, 252.17, 0.001, "a change of the copy keeps the node")
local mine = Swc.Vector3.new({x = 1, y = 2, z = 3})
Swc.SetData("Hud.LocalPlayer.Position", mine)
mine.z = 0
near(Swc.GetData("Hud.LocalPlayer.Position").z, 3, 0.001, "a change of the object of the caller keeps the node")

equal(Swc.errorCount or 0, 0, "no Lua error")
