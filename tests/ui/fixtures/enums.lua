-- The enum fixture (open point O1): the values of the client program (gamedata/enums.json, tools/convert-ui.sh enums)
-- against the own values of the emulation and the facts of other sources.
local count = 0
for _ in pairs(Swc.clientEnums) do
  count = count + 1
end
check(count > 1500, "the enum table of the client has the names of the scripts (" .. count .. " values)")
note(count .. " client values")

-- Each own value of the emulation (swc/enums.lua, swc/game.lua) is the client value. The runtime C++ code uses the
-- LyShine values (Source/SwcUi/Public/SwcUi/Model.h): a difference there is a wrong draw.
for name, d in pairs(Swc.enumDifferences) do
  check(false, "own value of " .. name .. ": " .. tostring(d.own) .. ", client value " .. tostring(d.client))
end

-- The paperdoll slots of the Soulwarden protocol notes (section 4.11), the wire values of the backend.
equal(ePaperDollSlotTypes_Head, 0, "ePaperDollSlotTypes_Head")
equal(ePaperDollSlotTypes_Chest, 1, "ePaperDollSlotTypes_Chest")
equal(ePaperDollSlotTypes_QuickSlot6, 19, "ePaperDollSlotTypes_QuickSlot6 (the first of 5 registrations of the name)")
equal(ePaperDollSlotTypes_MainHandOption1, 24, "ePaperDollSlotTypes_MainHandOption1")
equal(ePaperDollSlotTypes_HeartGem, 45, "ePaperDollSlotTypes_HeartGem")
equal(ePaperDollSlotTypes_Num, 50, "ePaperDollSlotTypes_Num")
equal(ePaperDollSlotTypes_Invalid, 51, "ePaperDollSlotTypes_Invalid")

-- The LyShine enums in the order of O3DE (UiImageBus.h, UiTextBus.h, UiTransformBus.h).
equal(eUiImageType_Stretched, 0, "eUiImageType_Stretched")
equal(eUiImageType_StretchedToFill, 5, "eUiImageType_StretchedToFill")
equal(eUiHAlign_Right, 2, "eUiHAlign_Right")
equal(eUiVAlign_Bottom, 2, "eUiVAlign_Bottom")
equal(eUiTextOverflowMode_Ellipsis, 2, "eUiTextOverflowMode_Ellipsis")
equal(eUiTextShrinkToFit_WidthOnly, 2, "eUiTextShrinkToFit_WidthOnly")

-- A name that the client program does not have is nil, as in the client (no stub number).
local absent = next(Swc.absentNames)
check(absent ~= nil, "the table lists the names that the client does not have")
if absent then
  equal(_G[absent], nil, "the absent name " .. absent)
end
