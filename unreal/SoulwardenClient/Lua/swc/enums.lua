-- The enum values that the emulation knows. The values of the client are inside its generated code (open point O1 of
-- the research); the scripts and the emulation use the same table, thus the values only need to agree with the
-- emulation. The LyShine enums have the orders of O3DE (UiImageBus.h, UiTextBus.h, UiTransformBus.h; facts only);
-- the runtime (Source/SwcUi/Public/SwcUi/Model.h) uses the same numbers. An enum that is not here is a stub with a
-- number of its own (swc/core.lua).

local function define(group, names, first)
  local value = first or 0
  for _, name in ipairs(names) do
    rawset(_G, group .. "_" .. name, value)
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
define("eUiRecompute", {"RectOnly", "TransformOnly", "RectAndTransform", "ViewportTransformOnly", "RectAndTransformForced"})
-- SetTextWithFlags: bit 0 escapes the markup, bit 1 localizes (O3DE UiTextInterface::SetTextFlags).
rawset(_G, "eUiTextSet_SetAsIs", 0)
rawset(_G, "eUiTextSet_SetEscapeMarkup", 1)
rawset(_G, "eUiTextSet_SetLocalized", 2)

-- The tween engine: the ease methods and the ease types of the gem (swc/tween.lua has the functions).
define("ScriptedEntityTweenerEasingMethod", {"None", "Linear", "Quad", "Cubic", "Quart", "Quint", "Sine", "Expo",
                                             "Circ", "Elastic", "Back", "Bounce"})
define("ScriptedEntityTweenerEasingType", {"In", "Out", "InOut"})

-- The input devices of the actions (LyShineManagerBus GetLastActionInputDevice, LastInputDeviceNotificationBus): this
-- stage has only the keyboard and the mouse.
define("eAID", {"KeyboardMouse", "AzPad"})
