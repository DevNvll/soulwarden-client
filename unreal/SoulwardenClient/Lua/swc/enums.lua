-- The enum values. The client values come from a static read of the client program (tools/convert-ui.sh enums,
-- gamedata/enums.json; docs/UI.md): they apply first. The own values below apply only to names that the table does not
-- have (the table has only the names that the scripts read). Where an own value differs from the client value, the
-- client value applies and Swc.enumDifferences records the name (the enum fixture of tools/test-ui.sh checks it: the
-- runtime C++ code uses the LyShine values, Source/SwcUi/Public/SwcUi/Model.h). An enum that is in neither table is a
-- stub with a number of its own (swc/core.lua). A name that the client does not have stays nil, as in the client.

Swc.clientEnums = {}
Swc.absentNames = {}
Swc.enumDifferences = {}
do
  local data, err = swcui.load_json("gamedata/enums.json")
  if data then
    for name, value in pairs(data.values or {}) do
      Swc.clientEnums[name] = value
      rawset(_G, name, value)
    end
    for _, name in ipairs(data.absent or {}) do
      Swc.absentNames[name] = true
    end
    Swc.nativeNames = {}
    for _, name in ipairs(data.natives or {}) do
      Swc.nativeNames[name] = true
    end
  else
    Swc.Note("no enum table of the client: " .. tostring(err) .. " (tools/convert-ui.sh enums); own values only")
  end
end

--- Defines one enum value of the emulation: the client value when the table has the name, else the own value.
function Swc.DefineEnum(name, own)
  local client = Swc.clientEnums[name]
  if client ~= nil then
    if client ~= own then
      Swc.enumDifferences[name] = {own = own, client = client}
    end
    return
  end
  rawset(_G, name, own)
end

local function define(group, names, first)
  local value = first or 0
  for _, name in ipairs(names) do
    Swc.DefineEnum(group .. "_" .. name, value)
    value = value + 1
  end
end

define("eUiImageType", {"Stretched", "Sliced", "Fixed", "Tiled", "StretchedToFit", "StretchedToFill"})
define("eUiFillType", {"None", "Linear", "Radial", "RadialCorner", "RadialEdge"})
define("eUiFillEdgeOrigin", {"Left", "Top", "Right", "Bottom"})
define("eUiFillCornerOrigin", {"TopLeft", "TopRight", "BottomRight", "BottomLeft"})
define("eUiBlendMode", {"Normal", "Add", "Screen", "Darken", "Lighten"})
define("eUiHAlign", {"Left", "Center", "Right"})
define("eUiVAlign", {"Top", "Center", "Bottom"})
define("eUiTextOverflowMode", {"OverflowText", "ClipText", "Ellipsis"})
define("eUiTextWrapTextSetting", {"NoWrap", "Wrap", "WrapSplitWords"})
define("eUiTextShrinkToFit", {"None", "Uniform", "WidthOnly"})
define("eUiTextCaseSetting", {"Normal", "Lower", "Upper"})
define("eUiFlipbookAnimationLoopType", {"None", "Linear", "PingPong"})
define("eUiFlipbookAnimationFramerateUnits", {"FPS", "SecondsPerFrame"})
define("eUiDragState", {"Normal", "Valid", "Invalid"})
define("eUiDropState", {"Normal", "Valid", "Invalid"})
define("eUiScrollerOrientation", {"Horizontal", "Vertical"})
define("eUiInteractableState", {"Normal", "Hover", "Pressed", "Disabled"})
define("eUiNavigationMode", {"Automatic", "Custom", "None"})
define("eUiLayoutOrder", {"LeftToRight", "RightToLeft"})
define("eUiScaleToDeviceMode", {"None", "UniformScaleToFit", "UniformScaleToFill", "UniformScaleToFitX",
                                "UniformScaleToFitY", "NonUniformScale", "ScaleXOnly", "ScaleYOnly"})
define("eUiRecompute", {"RectOnly", "TransformOnly", "RectAndTransform", "ViewportTransformOnly", "TransformOnlyForced",
                        "RectAndTransformForced"})
-- SetTextWithFlags: bit 0 escapes the markup, bit 1 localizes (O3DE UiTextInterface::SetTextFlags).
Swc.DefineEnum("eUiTextSet_SetAsIs", 0)
Swc.DefineEnum("eUiTextSet_SetEscapeMarkup", 1)
Swc.DefineEnum("eUiTextSet_SetLocalized", 2)

-- The tween engine: the ease methods and the ease types of the gem (swc/tween.lua has the functions).
define("ScriptedEntityTweenerEasingMethod", {"Linear", "Quad", "Cubic", "Quart", "Quint", "Sine", "Expo", "Circ",
                                             "Elastic", "Back", "Bounce"})
define("ScriptedEntityTweenerEasingType", {"In", "Out", "InOut"})

-- The input devices of the actions (LyShineManagerBus GetLastActionInputDevice, LastInputDeviceNotificationBus): bit
-- flags in the client (KeyboardMouse 1, AzPad 16).
Swc.DefineEnum("eAID_KeyboardMouse", 1)
Swc.DefineEnum("eAID_AzPad", 16)
-- The option OpenFactionGuide of the conversation (the PvP_Guide service; the enum name binding 0x5A5C78B to 0x5A5C795: 20; 2b of ut567,
-- static read ut567/qgplan/f4service). The scripts do not read the name, so the table of the client does not have it.
Swc.DefineEnum("eConversationOptionType_OpenFactionGuide", 20)
