"""The UI content: canvases and UI slices as data, the Lua scripts, the sprites, the fonts, and the texts.

Usage (tools/convert-ui.sh; output in SWC_CONTENT/ui, never in git):
  hud                     each part that the first HUD needs (stage 1.10): the canvases of HUD_CANVASES and the UI
                          slices that they spawn, their textures, all scripts, the fonts, and the English texts
  canvases [PATH...|--all]  canvases (.dynamicuicanvas) and UI slices (.dynamicslice) -> canvases/<path>.json
  scripts                 each Lua script of the game (lyshineui/ and scripts/) -> scripts/<path>.luac (unchanged bytes)
  textures PATH...|--canvas PATH...|--folder PREFIX   textures -> textures/<path>.png (+ <path>.sprite.json)
  texture-index           the sizes, borders, and cells of the textures of the converted canvases -> textures/index.json
  wanted [FILE]           the textures that the client asked for (default SWC_WORK/ui/wanted.json)
  fonts                   the .font files -> fonts/fonts.json, the font effects -> fonts/effects.json, the font files
  text                    the English texts (localization/en-us/*.loc.xml) -> localization/en-us.json
  gamedata                the game data of the HUD (data tables): gamedata/abilities.json, gamedata/weapons.json
  enums                   the enum values of the client for the names that the scripts read -> gamedata/enums.json
  loader                  the canvases that the two loaders of the game name -> gamedata/loader.json
  actionmaps              the action maps of the client profile (keyboard and mouse) -> gamedata/actionmaps.json
  world                   the data of the UI in the world views (stage WF7): the status rows of the plates and of the damage
                          numbers -> gamedata/statuseffects.json, the damage types -> gamedata/damagetypes.json, the
                          canvases of the markers, the prompt cards, the damage numbers, and the loot ticker, and their textures
  worldmap                the world map (stage MVP-C): gamedata/worldmap.json (swc_convert/worldmap.py: the territory and POI
                          definitions, shapes, and landmarks), the canvas MagicMap, the map tiles of all levels, the map images
  crafting                the crafting screens (stage MVP-J): gamedata/crafting.json (swc_convert/crafting.py: the recipes with the
                          causes of the backend, the categories, the stations, the rank tables), the canvases CraftingScreenV4 and
                          StationProperties, and their textures

The canvas form (research ~/Games/soulwarden-client/research/ui.md, section 2): a binary object stream with one
SliceComponent; its Entities list has the canvas entity and one entity for each element. The converter keeps each
element with its entity id (a decimal text: the ids have 64 bits), its name, its children in the order of their sort
index, and the fields of its components. The runtime (docs/UI.md) builds the element tree from this data and follows
the LyShine rules. Field ids are the CRC-32 of the lower-case field names (source.field_id).

A script property has the CRC-32 of the lower-case key of the Properties table of its script as its name; the
converter keeps the CRC (the runtime finds the key). String values are base64 in the stream; the converter decodes them.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import shutil
import struct
import sys
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

from .actions import gamedata_dir
from . import crafting, datasheet, dds, env, luabc, optiondefaults, source, uienums, worldmap
from .source import field_id, number, parse, real, text

# The canvases of the first HUD (stage 1.10): the globals (the loader of the screens), the vitals, the weapon and
# ability bar, the compass.
HUD_CANVASES = ["lyshineui/globals.dynamicuicanvas", "lyshineui/hud2024/vitals/vitals2024.dynamicuicanvas",
                "lyshineui/hud2024/weaponquickslots/weaponquickslots.dynamicuicanvas",
                "lyshineui/compass/compass.dynamicuicanvas", "lyshineui/hud2024/editmode/hudeditmode.dynamicuicanvas"]
# The texture folders that the HUD scripts use by a path that they make at run time (the icons of the abilities and of
# the weapon kinds). Other
# textures come on demand: the client writes the paths that it does not find into SWC_WORK/ui/wanted.json.
HUD_TEXTURE_FOLDERS = ["lyshineui/images/icons/abilities/", "lyshineui/images/icons/items/drawing/",
                       "lyshineui/images/hud2024/", "lyshineui/images/icons/misc/", "lyshineui/images/slices/"]

# Component type ids. LyShine types: the same ids as O3DE Gems/LyShine/Code/Include/LyShine/UiComponentTypes.h
# (Apache-2.0 OR MIT, facts only). Fork types: the names of the text table of the client (research ui.md 2.3).
COMPONENTS = {
    "4a97d63e-ce7a-45b6-aae4-102db4334688": "element", "2751a5a5-3291-4a4d-9fc0-9cb0eb8d1de6": "transform",
    "bdbefd23-dbb4-4726-a32d-4feac24e51f6": "image", "5b3fb2a7-5dc4-4033-a970-001cec85b6c4": "text",
    "cd01ff77-2249-4ed8-bffb-33a66a47e17c": "fader", "2279aa38-271d-4d4f-a472-e42b984088ac": "mask",
    "8d1bc97e-c55d-4d34-a460-e63c57cd0d4b": "script", "7b2820c4-7fc7-4f02-b777-6727eb4bac13": "layoutRow",
    "4bc2e786-360b-4426-8d9c-9b254c5ea21f": "layoutColumn", "adda3ae5-b9ab-44b7-a462-8b89b398a837": "layoutGrid",
    "a0568e58-4382-47f8-8b88-77c64b99ac80": "layoutCell", "96c55390-9d03-4c47-bb0d-17d3bd5219e3": "layoutFitter",
    "aaccffa1-b694-4f8b-a289-391a56404e0c": "flipbook", "bfac7909-3a56-4980-af7c-261a99d6371b": "imageSequence",
    "5af19874-04a4-4540-82fc-5f29ec854e31": "spawner", "3c909d7b-ec4e-4474-85e2-e923d39ba42d": "desaturator",
    "690bec14-3642-4247-bd96-fe414ccb7de7": "dynamicLayout", "7329dfe8-0f3c-4629-b395-78b2cf646b96": "button",
    "50b8cf6c-b19a-4d86-afe9-96efb820d422": "canvas", "3a1f597c-b374-44b1-a563-c7f7c6834269": "javelinCanvas",
    "7dfe5e32-fafb-4a19-8a57-e0b61419ad02": "progressBar", "2422ec06-9003-4165-a609-9d6ab90c2332": "imageRepeater",
    "e7e9c0b0-f552-4008-e0b2-2aa78bc14034": "layoutColumnFixed", "661370c1-899a-4d56-364a-1d30f465469e": "layoutRowFixed",
}
# The interactive components (read_interactive): type id -> (the key of the data, the interactable kind, or None for a
# component that is not an interactable: the radio button group, the mouse wheel of a scroll bar, the drop target, ...).
INTERACTIVE = {
    "68d62281-b360-4426-aaca-e8bde8bfeb3a": ("checkbox", "checkbox"), "c96b1eef-033a-479b-829b-ed3555d0f33a": ("draggable", "draggable"),
    "e4b8ace0-fce7-42d8-9836-942f910168b4": ("dropTarget", None), "bfc4d5a3-2f7c-4079-b19c-c2a06e93f343": ("dropdown", "dropdown"),
    "6982c200-4d32-43cc-a7f6-f54fa50fcff5": ("dynamicScrollBox", None), "965cdda3-ca50-4938-9088-0cd476942146": ("exitHover", None),
    "d7eb1706-75e8-4c94-a266-61913f42431b": ("markupButton", "markupButton"), "773a0f4b-f9f9-4b82-9951-bbd73a737de4": ("radio", "radio"),
    "f89d5ef2-37c4-4ca7-9aa2-23dc3867ec9d": ("radioGroup", None), "6b283f90-3519-47da-a1dd-65a79ce119cf": ("scrollBar", "scrollBar"),
    "3ad4b591-438c-4e08-87b1-a686bcf49198": ("wheel", None), "2f539588-aeab-4341-a6a6-af645d129693": ("scrollBox", "scrollBox"),
    "2913d76b-36a0-45e0-a104-33c668eb612d": ("slider", "slider"), "2cb3872b-d2b4-4ddb-b39a-97492310ae11": ("textInput", "textInput"),
}
OTHER_COMPONENTS = {
    "ee5c91f4-3111-40d9-8549-fbd657fc326b": "UiMarkerComponent", "67dc06d3-1f16-4fab-b3f8-d8c0a3af4f61": "SequenceAgentComponent",
    "393cda3f-7a95-438a-b7f5-3230abb36c8a": "SimpleMarkerComponent", "a1ab7323-837f-43e1-9e41-23bd4932e2d3": "UiDamageNumberAnchorComponent",
    "fe8d6ff2-77e2-480d-a2df-2911312c5026": "UiDamageNumberComponent", "153a4bf9-1404-4d54-8b39-af2224b15088": "UiRequiredResourceDetailsComponent",
    "08cebe4a-a04f-4567-80c0-949e913607ef": "UiReticlesComponent", "c73d19a3-d33d-4adc-9cc9-26b4afb2d6c4": "UiSelectionComponent",
    "36ae95b8-70e5-4502-be3c-3fe818cc8bf3": "UiTelemetryComponent", "110c11db-8ada-4641-a5b8-efd01470927c": "UiTextInputAutoCompleteComponent",
    "18ffc90d-e89a-4ca5-88ac-67bc8a497ea7": "UnifiedInteractOptionComponent", "9299475d-b524-487d-9b19-697b55017225": "UnifiedInteractOptionsComponent",
    "1db3ea3a-2b5c-4765-b87e-9cb5fc5c5c21": "ApplyResourceDetailsComponent", "01dc7b07-dbf9-4594-906c-17c4c14adcb7": "RepairDetailsComponent",
}
# The type ids of the value of a generic script property (DynamicSerializableField TypeId).
VALUE_ENTITY_ID = "6383f1d3-bb27-4e6b-a49a-6409b2059eaa"
VALUE_VECTOR2 = "3d80f623-c85c-4741-90d0-e4e66164e6bf"
VALUE_COLOR = "7894072a-9050-4f0f-901b-34b1a0d29417"
# The classes of a script property.
PROPERTY_BOOLEAN = "ea7335f8-5b9f-4744-b805-fef9240451bd"
PROPERTY_NUMBER = "5bcdfdeb-a75d-4e83-bb74-c45299cb9826"
PROPERTY_STRING = "a0229c6d-b010-47e7-8985-ee220fc7bfaf"
PROPERTY_GENERIC = "80618224-814c-44d4-a7b8-14b5a36f96ed"
PROPERTY_GENERIC_ARRAY = "28e986dd-cf7c-404d-9bee-eee067180cd1"
PROPERTY_STRING_ARRAY = "899993a5-d717-41bb-b89b-04a27952ca6d"
# Lumberyard 1.12: the invalid entity id has the value 0x00000000FFFFFFFF (the canvases use it for "no element").
INVALID_ENTITY_ID = 0xFFFFFFFF

F = {name: field_id(name) for name in """Id Name Components Entities IsEnabled ChildEntityIdOrder ChildEntityId SortIndex
IsChildrenRenderSortable RenderPriority Anchors Offsets Pivot Rotation scale ScaleToDevice ComputeTransformWhenHidden left
top right bottom SpriteType SpriteTexture Index COLOR Alpha ImageType FillCenter StretchSliced BlendMode FillType
FillAmount FillStartAngle FillCornerOrigin FillEdgeOrigin FillClockwise Text MarkupEnabled ImagesEnabled FontFileName
BaseClass1 AssetPath FontSize EffectIndex TextHAlignment TextVAlignment CharacterSpacing LineSpacing OverflowMode
WrapTextSetting ShrinkToFit MinShrinkScale CaseOverride Fade UseRenderToTexture EnableMasking MaskInteraction
ChildMaskElement DrawBehind DrawInFront UseAlphaTest Padding Spacing Order ChildHAlignment ChildVAlignment
IgnoreDefaultLayoutCells CellSize HorizontalOrder VerticalOrder StartingWith LayoutVisibleOnly MinWidth MinHeight
TargetWidth TargetHeight ExtraWidthRatio ExtraHeightRatio MinWidthOverridden MinHeightOverridden TargetWidthOverridden
TargetHeightOverridden ExtraWidthRatioOverridden ExtraHeightRatioOverridden HorizontalFit VerticalFit ImageList
ImageSequenceDirectory Slice SpawnOnActivate Saturation NumChildElements Script properties Groups value TypeId m_data
Values UniqueId RootElement CanvasSize DrawOrder IsPixelAligned IsPosInputSupported TextureAtlases requiresMouse escapable
replaceable actionMapOverrides drawLayer ActionName HoverStartActionName HoverEndActionName PressedActionName ReleasedActionName
IsHandlingEvents Progress Vertical NumRepeats HoverStateActions PressedStateActions DisabledStateActions TargetEntity sprite
IsAutoActivationEnabled ActionNameRight ActionNamePressedRight UseClickBehavior ClickSqTolerance SetHoverOnPress
OptionalCheckedEntity OptionalUncheckedEntity IsChecked ChangedActionName TurnOnActionName TurnOffActionName Group
AllowRestoreUnchecked TrackEntity FillEntity ManipulatorEntity MinValue MaxValue StepValue ValueChangingActionName
ValueChangedActionName HandleEntity Orientation HandleSize MinHandlePixelSize WheelStepValue ScrollBarEntity ContentEntity
ScrollOffset ConstrainScrolling EnableEasing AllowDragging SnapMode SnapGrid AllowHorizSrolling HScrollBarEntity
HScrollBarVisibility AllowVertScrolling VScrollBarEntity VScrollBarVisibility ScrollOffsetChangingActionName
ScrollOffsetChangedActionName PlaceholderText TextSelectionColor TextCursorColor MaxStringLength CursorBlinkInterval
IsPasswordField ReplacementCharacter ClipInputText ChangeAction StartEditAction EndEditAction EnterAction
DoubleClickInterval Content ExpandedParent TextElement IconElement ExpandOnHover WaitTime CollapseOnOutsideClick
ExpandedStateActions ExpandedActionName CollapsedActionName OptionSelectedActionName DragNormalStateActions
DragValidStateActions DragInvalidStateActions DropValidStateActions DropInvalidStateActions OnDropActionName LinkColor
LinkHoverColor AlwaysCheckHover EventName Delay AutoRefreshOnPostActivate PrototypeElement VariableElementSize
AutoCalcElementSize EstimatedElementSize DefaultNumElements HasSections HeaderPrototypeElement StickyHeaders
VariableHeaderSize AutoCalcHeaderSize EstimatedHeaderSize DefaultNumSections""".split()}
for _label in ("Start Frame", "End Frame", "Loop Start Frame", "Loop Type", "Framerate Unit", "Framerate", "Start Delay",
               "Loop Delay", "Reverse Delay", "Auto Play"):
    F[_label] = field_id(_label)


def out_dir() -> Path:
    return env.content_dir() / "ui"


# ---------------------------------------------------------------------------------------------------------------- values

def uid(element) -> str:
    return str(uuid.UUID(bytes=element.type_id))


def child(element, name: str):
    return element.child(F[name]) if element is not None else None


def flag(element, name: str, default: bool = False) -> bool:
    node = child(element, name)
    if node is None or node.value is None:
        return default
    return node.value[:1] != b"\0"


def num(element, name: str, default: float = 0.0) -> float:
    node = child(element, name)
    if node is None or node.value is None:
        return default
    if len(node.value) == 4:
        return round(real(node), 6)
    if len(node.value) == 8:
        return struct.unpack(">d", node.value)[0]
    return default


def whole(element, name: str, default: int = 0, signed: bool = False) -> int:
    node = child(element, name)
    if node is None:
        return default
    value = number(node)
    if signed and node.value is not None and len(node.value) == 4 and value >= 1 << 31:
        value -= 1 << 32   # Padding is an unsigned field, and some canvases have -4 in it
    return value


def floats(node, count: int) -> list[float] | None:
    if node is None or node.value is None or len(node.value) != 4 * count:
        return None
    return [round(v, 6) for v in struct.unpack(f">{count}f", node.value)]


def vec2(element, name: str, default=(0.0, 0.0)) -> list[float]:
    return floats(child(element, name), 2) or list(default)


def color(element, name: str = "COLOR") -> list[float]:
    return floats(child(element, name), 4) or [1.0, 1.0, 1.0, 1.0]


def rect(element, name: str) -> list[float]:
    node = child(element, name)
    return [num(node, side) for side in ("left", "top", "right", "bottom")]


def entity_id(element) -> str | None:
    """@return The entity id of an AZ::EntityId field (one child Id) as a decimal text, or None when it is invalid."""
    if element is None:
        return None
    node = element.child(F["Id"]) or element
    if node.value is None or len(node.value) != 8:
        return None
    value = int.from_bytes(node.value, "big")
    return None if value == INVALID_ENTITY_ID else str(value)


def asset_hint(element) -> str | None:
    """@return The product path of an asset reference: asset id (16), 16 bytes, asset type (16), hint size (u64), hint."""
    raw = element.value if element is not None else None
    if raw is None or len(raw) < 56 or not any(raw[:16]):
        return None
    size = int.from_bytes(raw[48:56], "big")
    hint = raw[56:56 + size].decode("utf-8", "replace").lower().replace("\\", "/")
    return hint or None


def string(element, name: str) -> str:
    return text(child(element, name))


# ------------------------------------------------------------------------------------------------------------ components

def read_transform(c) -> dict:
    return {"anchors": rect(c, "Anchors"), "offsets": rect(c, "Offsets"), "pivot": vec2(c, "Pivot", (0.5, 0.5)),
            "rotation": num(c, "Rotation"), "scale": vec2(c, "scale", (1.0, 1.0)),
            "scaleToDevice": flag(c, "ScaleToDevice"), "computeWhenHidden": flag(c, "ComputeTransformWhenHidden", True)}


def read_image(c) -> dict:
    return {"sprite": asset_hint(child(c, "SpriteTexture")), "spriteType": whole(c, "SpriteType"), "index": whole(c, "Index"),
            "color": color(c), "alpha": num(c, "Alpha", 1.0), "type": whole(c, "ImageType"),
            "fillCenter": flag(c, "FillCenter", True), "stretchSliced": flag(c, "StretchSliced", True),
            "blend": whole(c, "BlendMode"), "fillType": whole(c, "FillType"), "fillAmount": num(c, "FillAmount", 1.0),
            "fillStartAngle": num(c, "FillStartAngle"), "fillCornerOrigin": whole(c, "FillCornerOrigin"),
            "fillEdgeOrigin": whole(c, "FillEdgeOrigin"), "fillClockwise": flag(c, "FillClockwise", True)}


def read_text(c) -> dict:
    font = child(child(c, "FontFileName"), "BaseClass1")
    return {"text": string(c, "Text"), "markup": flag(c, "MarkupEnabled"), "images": flag(c, "ImagesEnabled", True),
            "color": color(c), "alpha": num(c, "Alpha", 1.0), "font": string(font, "AssetPath").lower().replace("\\", "/"),
            "size": num(c, "FontSize", 32.0), "effect": whole(c, "EffectIndex"), "hAlign": whole(c, "TextHAlignment"),
            "vAlign": whole(c, "TextVAlignment"), "charSpacing": num(c, "CharacterSpacing"),
            "lineSpacing": num(c, "LineSpacing"), "overflow": whole(c, "OverflowMode"), "wrap": whole(c, "WrapTextSetting"),
            "shrink": whole(c, "ShrinkToFit"), "minShrink": num(c, "MinShrinkScale"), "case": whole(c, "CaseOverride")}


def read_mask(c) -> dict:
    return {"enabled": flag(c, "EnableMasking", True), "interaction": flag(c, "MaskInteraction", True),
            "childMask": entity_id(child(c, "ChildMaskElement")), "rtt": flag(c, "UseRenderToTexture"),
            "drawBehind": flag(c, "DrawBehind"), "drawInFront": flag(c, "DrawInFront"), "alphaTest": flag(c, "UseAlphaTest")}


def read_line_layout(c) -> dict:
    padding = child(c, "Padding")
    return {"padding": [whole(padding, side, signed=True) for side in ("left", "top", "right", "bottom")],
            "spacing": num(c, "Spacing"), "order": whole(c, "Order"), "hAlign": whole(c, "ChildHAlignment"),
            "vAlign": whole(c, "ChildVAlignment"), "ignoreDefaultCells": flag(c, "IgnoreDefaultLayoutCells", True)}


def read_grid(c) -> dict:
    padding = child(c, "Padding")
    return {"padding": [whole(padding, side, signed=True) for side in ("left", "top", "right", "bottom")],
            "spacing": vec2(c, "Spacing"), "cellSize": vec2(c, "CellSize", (100.0, 100.0)),
            "hOrder": whole(c, "HorizontalOrder"), "vOrder": whole(c, "VerticalOrder"), "startingWith": whole(c, "StartingWith"),
            "hAlign": whole(c, "ChildHAlignment"), "vAlign": whole(c, "ChildVAlignment"),
            "visibleOnly": flag(c, "LayoutVisibleOnly")}


def read_cell(c) -> dict:
    out = {}
    for key, label in (("minWidth", "MinWidth"), ("minHeight", "MinHeight"), ("targetWidth", "TargetWidth"),
                       ("targetHeight", "TargetHeight"), ("extraWidthRatio", "ExtraWidthRatio"),
                       ("extraHeightRatio", "ExtraHeightRatio")):
        if flag(c, label + "Overridden"):
            out[key] = num(c, label)
    return out


def read_flipbook(c) -> dict:
    return {"start": whole(c, "Start Frame"), "end": whole(c, "End Frame"), "loopStart": whole(c, "Loop Start Frame"),
            "loopType": whole(c, "Loop Type"), "rateUnit": whole(c, "Framerate Unit"), "rate": num(c, "Framerate", 30.0),
            "startDelay": num(c, "Start Delay"), "loopDelay": num(c, "Loop Delay"), "reverseDelay": num(c, "Reverse Delay"),
            "autoPlay": flag(c, "Auto Play")}


def read_sequence(c) -> dict:
    images = child(c, "ImageList")
    return {"type": whole(c, "ImageType"), "images": [asset_hint(i) for i in (images.children if images else [])],
            "folder": string(c, "ImageSequenceDirectory"), "index": whole(c, "Index"), "color": color(c),
            "alpha": num(c, "Alpha", 1.0), "blend": whole(c, "BlendMode")}


def read_state_actions(node) -> list[dict]:
    """The state actions of an interactable state (UiInteractableStateColor, Alpha, Sprite): each changes a visual of its
    target element (the element itself when the target is not set) while the state lasts."""
    out = []
    for action in (node.children if node is not None else []):
        entry: dict = {"targetEntity": entity_id(child(action, "TargetEntity"))}
        if child(action, "COLOR") is not None:
            entry["kind"] = "color"
            entry["color"] = color(action)
        elif child(action, "sprite") is not None:
            entry["kind"] = "sprite"
            entry["sprite"] = asset_hint(child(action, "sprite"))
            entry["index"] = whole(action, "Index")
        elif child(action, "Alpha") is not None:
            entry["kind"] = "alpha"
            entry["alpha"] = num(action, "Alpha", 1.0)
        else:
            entry["kind"] = "other"
        out.append(entry)
    return out


def read_interactable(c, kind: str) -> dict:
    """The interactable part (BaseClass1: UiInteractableComponent) of a button, check box, radio button, slider, scroll
    bar, scroll box, text input, drop-down, draggable, or markup button."""
    base = child(c, "BaseClass1")
    return {"kind": kind, "handling": flag(base, "IsHandlingEvents", True),
            "autoActivation": flag(base, "IsAutoActivationEnabled"),
            "hoverStart": string(base, "HoverStartActionName"), "hoverEnd": string(base, "HoverEndActionName"),
            "pressed": string(base, "PressedActionName"), "released": string(base, "ReleasedActionName"),
            "states": {"hover": read_state_actions(child(base, "HoverStateActions")),
                       "pressed": read_state_actions(child(base, "PressedStateActions")),
                       "disabled": read_state_actions(child(base, "DisabledStateActions"))}}


def read_button(c) -> dict:
    base = child(c, "BaseClass1")
    return {"action": string(c, "ActionName"), "actionRight": string(c, "ActionNameRight"),
            "actionPressedRight": string(c, "ActionNamePressedRight"), "clickBehavior": flag(c, "UseClickBehavior"),
            "hoverOnPressEntity": entity_id(child(c, "SetHoverOnPress")),
            "hoverStart": string(base, "HoverStartActionName"),
            "hoverEnd": string(base, "HoverEndActionName"), "pressed": string(base, "PressedActionName"),
            "released": string(base, "ReleasedActionName"), "handlingEvents": flag(base, "IsHandlingEvents", True)}


def read_interactive(name: str, c) -> dict:
    """The data of an interactive component that is not a button (the runtime emulation gives its behavior). An entity
    id field has a key that ends with "Entity" (the runtime maps them in a copy of the element)."""
    def e(field):
        return entity_id(child(c, field))
    if name in ("checkbox", "radio"):
        out = {"checkedEntity": e("OptionalCheckedEntity"), "uncheckedEntity": e("OptionalUncheckedEntity"),
               "checked": flag(c, "IsChecked"), "changed": string(c, "ChangedActionName"),
               "turnOn": string(c, "TurnOnActionName"), "turnOff": string(c, "TurnOffActionName")}
        if name == "radio":
            out["groupEntity"] = e("Group")
        return out
    if name == "radioGroup":
        return {"allowUncheck": flag(c, "AllowRestoreUnchecked"), "changed": string(c, "ChangedActionName")}
    if name == "slider":
        return {"trackEntity": e("TrackEntity"), "fillEntity": e("FillEntity"), "manipulatorEntity": e("ManipulatorEntity"),
                "value": num(c, "value"), "min": num(c, "MinValue"), "max": num(c, "MaxValue", 100.0),
                "step": num(c, "StepValue"), "changing": string(c, "ValueChangingActionName"),
                "changed": string(c, "ValueChangedActionName")}
    if name == "scrollBar":
        return {"handleEntity": e("HandleEntity"), "orientation": whole(c, "Orientation"), "value": num(c, "value"),
                "handleSize": num(c, "HandleSize", 0.1), "minHandlePixels": num(c, "MinHandlePixelSize", 16.0),
                "changing": string(c, "ValueChangingActionName"), "changed": string(c, "ValueChangedActionName")}
    if name == "wheel":
        return {"step": num(c, "WheelStepValue", 120.0), "scrollBarEntity": e("ScrollBarEntity")}
    if name == "scrollBox":
        return {"contentEntity": e("ContentEntity"), "offset": vec2(c, "ScrollOffset"),
                "constrain": flag(c, "ConstrainScrolling", True), "easing": flag(c, "EnableEasing", True),
                "dragging": flag(c, "AllowDragging", True), "snapMode": whole(c, "SnapMode"),
                "snapGrid": vec2(c, "SnapGrid", (10.0, 10.0)), "horizontal": flag(c, "AllowHorizSrolling"),
                "hScrollBarEntity": e("HScrollBarEntity"), "hVisibility": whole(c, "HScrollBarVisibility"),
                "vertical": flag(c, "AllowVertScrolling", True), "vScrollBarEntity": e("VScrollBarEntity"),
                "vVisibility": whole(c, "VScrollBarVisibility"), "changing": string(c, "ScrollOffsetChangingActionName"),
                "changed": string(c, "ScrollOffsetChangedActionName")}
    if name == "textInput":
        return {"textEntity": e("Text"), "placeholderEntity": e("PlaceholderText"),
                "selectionColor": color(c, "TextSelectionColor"), "cursorColor": color(c, "TextCursorColor"),
                "maxLength": whole(c, "MaxStringLength", 0, signed=True), "blink": num(c, "CursorBlinkInterval", 1.0),
                "password": flag(c, "IsPasswordField"), "replacement": whole(c, "ReplacementCharacter", 42),
                "clip": flag(c, "ClipInputText", True), "change": string(c, "ChangeAction"),
                "startEdit": string(c, "StartEditAction"), "endEdit": string(c, "EndEditAction"),
                "enter": string(c, "EnterAction"), "doubleClick": num(c, "DoubleClickInterval", 0.5)}
    if name == "dropdown":
        return {"contentEntity": e("Content"), "expandedParentEntity": e("ExpandedParent"), "textEntity": e("TextElement"),
                "iconEntity": e("IconElement"), "expandOnHover": flag(c, "ExpandOnHover"), "waitTime": num(c, "WaitTime"),
                "collapseOnOutsideClick": flag(c, "CollapseOnOutsideClick", True),
                "expandedStates": read_state_actions(child(c, "ExpandedStateActions")),
                "expanded": string(c, "ExpandedActionName"), "collapsed": string(c, "CollapsedActionName"),
                "optionSelected": string(c, "OptionSelectedActionName")}
    if name == "draggable":
        return {"states": {"normal": read_state_actions(child(c, "DragNormalStateActions")),
                           "valid": read_state_actions(child(c, "DragValidStateActions")),
                           "invalid": read_state_actions(child(c, "DragInvalidStateActions"))}}
    if name == "dropTarget":
        return {"states": {"valid": read_state_actions(child(c, "DropValidStateActions")),
                           "invalid": read_state_actions(child(c, "DropInvalidStateActions"))},
                "drop": string(c, "OnDropActionName")}
    if name == "markupButton":
        return {"linkColor": color(c, "LinkColor"), "linkHoverColor": color(c, "LinkHoverColor"),
                "alwaysCheckHover": flag(c, "AlwaysCheckHover")}
    if name == "exitHover":
        return {"event": string(c, "EventName"), "delay": num(c, "Delay")}
    if name == "dynamicScrollBox":
        return {"autoRefresh": flag(c, "AutoRefreshOnPostActivate", True), "prototypeEntity": e("PrototypeElement"),
                "variableSize": flag(c, "VariableElementSize"), "autoSize": flag(c, "AutoCalcElementSize"),
                "estimatedSize": num(c, "EstimatedElementSize"), "defaultCount": whole(c, "DefaultNumElements"),
                "sections": flag(c, "HasSections"), "headerPrototypeEntity": e("HeaderPrototypeElement"),
                "stickyHeaders": flag(c, "StickyHeaders"), "variableHeaderSize": flag(c, "VariableHeaderSize"),
                "autoHeaderSize": flag(c, "AutoCalcHeaderSize"), "estimatedHeaderSize": num(c, "EstimatedHeaderSize"),
                "defaultSections": whole(c, "DefaultNumSections")}
    return {}


def property_value(prop) -> dict:
    kind = uid(prop)
    value = child(prop, "value")
    if kind == PROPERTY_BOOLEAN:
        return {"t": "bool", "v": bool(value is not None and value.value and value.value[0])}
    if kind == PROPERTY_NUMBER:
        return {"t": "num", "v": struct.unpack(">d", value.value)[0] if value is not None and value.value else 0.0}
    if kind == PROPERTY_STRING:
        return {"t": "str", "v": decode_text(text(value))}
    if kind == PROPERTY_GENERIC:
        return generic_value(value)
    if kind == PROPERTY_GENERIC_ARRAY:
        values = child(prop, "Values")
        return {"t": "array", "v": [generic_value(v) for v in (values.children if values else [])]}
    if kind == PROPERTY_STRING_ARRAY:
        values = child(prop, "Values")
        return {"t": "strs", "v": [decode_text(text(v)) for v in (values.children if values else [])]}
    return {"t": "unknown", "class": kind}


def decode_text(value: str) -> str:
    try:
        return base64.b64decode(value, validate=True).decode("utf-8")
    except (ValueError, UnicodeDecodeError):
        return value


def generic_value(field) -> dict:
    if field is None:
        return {"t": "nil"}
    type_node = child(field, "TypeId")
    data = child(field, "m_data")
    kind = str(uuid.UUID(bytes=type_node.value)) if type_node is not None and type_node.value else ""
    if kind == VALUE_ENTITY_ID:
        return {"t": "id", "v": entity_id(data)}
    if kind == VALUE_VECTOR2:
        return {"t": "vec2", "v": floats(data, 2) or [0.0, 0.0]}
    if kind == VALUE_COLOR:
        return {"t": "color", "v": floats(data, 4) or [1.0, 1.0, 1.0, 1.0]}
    return {"t": "unknown", "type": kind}


def property_group(node) -> dict:
    """@return {name crc text: value} of one property table; a group is {"t": "group", "v": {...}}."""
    out = {}
    plist = child(node, "properties")
    for prop in (plist.children if plist is not None else []):
        name = string(child(prop, "BaseClass1"), "Name")
        out[name] = property_value(prop)
    groups = child(node, "Groups")
    for group in (groups.children if groups is not None else []):
        out[string(group, "Name")] = {"t": "group", "v": property_group(group)}
    return out


def read_script(c) -> dict:
    return {"script": asset_hint(child(c, "Script")), "properties": property_group(child(c, "properties"))}


# --------------------------------------------------------------------------------------------------------------- canvas

def entities(roots):
    """Yield each AZ::Entity of the Entities list of the SliceComponent of the stream."""
    for root in roots:
        stack = [root]
        while stack:
            node = stack.pop()
            if node.name == F["Entities"]:
                yield from node.children
                continue
            stack.extend(node.children)


def convert_entity(entity) -> tuple[dict, dict | None]:
    """@return (element, canvas): the data of one entity. `canvas` is set for the canvas entity."""
    out: dict = {"id": entity_id(entity.child(F["Id"])), "name": text(entity.child(F["Name"]))}
    canvas = None
    other = []
    components = entity.child(F["Components"])
    for c in (components.children if components is not None else []):
        kind = uid(c)
        name = COMPONENTS.get(kind)
        if name == "element":
            out["enabled"] = flag(c, "IsEnabled", True)
            order = child(c, "ChildEntityIdOrder")
            entries = []
            for entry in (order.children if order is not None else []):
                entries.append((whole(entry, "SortIndex"), entity_id(child(entry, "ChildEntityId"))))
            out["children"] = [i for _, i in sorted(entries, key=lambda e: e[0]) if i is not None]
            if flag(c, "IsChildrenRenderSortable"):
                out["childrenSortable"] = True
            priority = whole(c, "RenderPriority")
            if priority:
                out["renderPriority"] = priority - (1 << 32) if priority >= 1 << 31 else priority
        elif name == "transform":
            out["transform"] = read_transform(c)
        elif name == "image":
            out["image"] = read_image(c)
        elif name == "text":
            out["text"] = read_text(c)
        elif name == "fader":
            out["fader"] = {"fade": num(c, "Fade", 1.0), "rtt": flag(c, "UseRenderToTexture")}
        elif name == "mask":
            out["mask"] = read_mask(c)
        elif name == "script":
            out.setdefault("scripts", []).append(read_script(c))
        elif name in ("layoutRow", "layoutColumn"):
            out[name] = read_line_layout(c)
        elif name in ("layoutRowFixed", "layoutColumnFixed"):
            # The fixed forms of the fork: the children keep their sizes (no padding, no cells).
            out[name] = {"spacing": num(c, "Spacing"), "order": whole(c, "Order"), "hAlign": whole(c, "ChildHAlignment"),
                         "vAlign": whole(c, "ChildVAlignment"), "visibleOnly": flag(c, "LayoutVisibleOnly")}
        elif name == "layoutGrid":
            out[name] = read_grid(c)
        elif name == "layoutCell":
            out[name] = read_cell(c)
        elif name == "layoutFitter":
            out[name] = {"horizontal": flag(c, "HorizontalFit"), "vertical": flag(c, "VerticalFit")}
        elif name == "flipbook":
            out[name] = read_flipbook(c)
        elif name == "imageSequence":
            out[name] = read_sequence(c)
        elif name == "spawner":
            out[name] = {"slice": asset_hint(child(c, "Slice")), "onActivate": flag(c, "SpawnOnActivate")}
        elif name == "desaturator":
            out[name] = {"saturation": num(c, "Saturation", 1.0)}
        elif name == "dynamicLayout":
            out[name] = {"count": whole(c, "NumChildElements")}
        elif name == "button":
            out[name] = read_button(c)
            out["interactable"] = read_interactable(c, "button")
        elif name == "progressBar":
            out[name] = {"progress": num(c, "Progress"), "vertical": flag(c, "Vertical")}
        elif name == "imageRepeater":
            out[name] = {"repeats": whole(c, "NumRepeats")}
        elif name == "canvas":
            atlases = child(c, "TextureAtlases")
            canvas = {"id": out["id"], "uniqueId": str(whole(c, "UniqueId")), "root": entity_id(child(c, "RootElement")),
                      "size": vec2(c, "CanvasSize", (1920.0, 1080.0)), "drawOrder": whole(c, "DrawOrder"),
                      "pixelAligned": flag(c, "IsPixelAligned", True),
                      # IsPosInputSupported: the canvas takes the pointer (12 canvases of the game do not: the tooltip).
                      "posInput": flag(c, "IsPosInputSupported", True),
                      "atlases": [string(child(a, "BaseClass1"), "AssetPath").lower() for a in (atlases.children if atlases else [])]}
        elif name == "javelinCanvas":
            maps = child(c, "actionMapOverrides")
            out["javelin"] = {"requiresMouse": flag(c, "requiresMouse"), "escapable": flag(c, "escapable"),
                              "replaceable": flag(c, "replaceable"), "drawLayer": string(c, "drawLayer"),
                              "actionMapOverrides": [text(m) for m in (maps.children if maps else [])]}
        elif kind in INTERACTIVE:
            key, interactable = INTERACTIVE[kind]
            out[key] = read_interactive(key, c)
            if interactable is not None:
                out["interactable"] = read_interactable(c, interactable)
        else:
            other.append(OTHER_COMPONENTS.get(kind, kind))
    if other:
        out["other"] = other
    return out, canvas


def convert_canvas(path: str) -> dict:
    """@return The data of one canvas or UI slice (a game path, lower case)."""
    roots = parse(source.read(path))
    elements: list[dict] = []
    canvas = None
    javelin = None
    for entity in entities(roots):
        element, found = convert_entity(entity)
        if found is not None:
            canvas = found
            javelin = element.get("javelin")
            if element.get("scripts"):
                canvas["scripts"] = element["scripts"]   # a script of the canvas entity (the globals canvas)
            continue
        if "transform" in element or "enabled" in element:
            elements.append(element)
    doc: dict = {"format": 1, "source": path, "kind": "canvas" if path.endswith(".dynamicuicanvas") else "slice",
                 "elements": elements}
    if canvas is not None:
        if javelin is not None:
            canvas["javelin"] = javelin
        doc["canvas"] = canvas
    else:
        children = {c for e in elements for c in e.get("children", [])}
        doc["roots"] = [e["id"] for e in elements if e["id"] not in children]
    return doc


def canvas_file(path: str) -> Path:
    """@return The output file of a canvas or a UI slice: canvases/<path>.json; a slice whose name a canvas also has (the
    game has one: lyshineui/notifications/notification) is canvases/<path>.slice.json."""
    stem = path.rsplit(".", 1)[0]
    if path.endswith(".dynamicslice") and source.has(stem + ".dynamicuicanvas"):
        return out_dir() / "canvases" / (stem + ".slice.json")
    return out_dir() / "canvases" / (stem + ".json")


def references(doc: dict) -> tuple[set[str], set[str], set[str], set[str]]:
    """@return (textures, slices, scripts, fonts) that one canvas names."""
    textures, slices, scripts, fonts = set(), set(), set(), set()
    for e in doc["elements"]:
        if e.get("image", {}).get("sprite"):
            textures.add(e["image"]["sprite"])
        for image in e.get("imageSequence", {}).get("images", []):
            if image:
                textures.add(image)
        if e.get("spawner", {}).get("slice"):
            slices.add(e["spawner"]["slice"])
        for s in e.get("scripts", []):
            if s["script"]:
                scripts.add(s["script"])
        if e.get("text", {}).get("font"):
            fonts.add(e["text"]["font"])
    return textures, slices, scripts, fonts


def convert_canvases(paths: list[str]) -> dict:
    """Converts canvases and the UI slices that they spawn. @return The counts and the textures that they name."""
    todo = list(paths)
    done: set[str] = set()
    textures: set[str] = set()
    failed = {}
    count = 0
    while todo:
        path = todo.pop()
        if path in done:
            continue
        done.add(path)
        try:
            doc = convert_canvas(path)
        except Exception as error:  # noqa: BLE001  (a canvas that does not decode is a report line, not a stop)
            failed[path] = str(error)[:160]
            continue
        target = canvas_file(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(doc, separators=(",", ":")))
        count += 1
        t, s, _, _ = references(doc)
        textures |= t
        todo.extend(sorted(s - done))
    return {"converted": count, "failed": failed, "textures": sorted(textures)}


# -------------------------------------------------------------------------------------------------------------- scripts

def convert_scripts() -> dict:
    """Copies each Lua script of the game, unchanged (2 bytes, then Lua 5.1 bytecode: research ui.md 3.1)."""
    count = 0
    for prefix in ("lyshineui/", "scripts/"):
        for path in source.paks().paths(prefix, ".luac"):
            target = out_dir() / "scripts" / path
            data = source.read(path)
            if target.is_file() and target.stat().st_size == len(data):
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            count += 1
    return {"written": count}


# ------------------------------------------------------------------------------------------------------------- textures

def texture_target(path: str) -> Path:
    return out_dir() / "textures" / (path.rsplit(".", 1)[0] + ".png")


def sprite_data(path: str) -> dict | None:
    """@return The borders (fractions of the texture: left, top, right, bottom lines) or the cells of the .sprite file
    next to a texture, or None when it has none. Missing attributes are 0 (a border) or the corner of the full texture."""
    sprite = path.rsplit(".", 1)[0] + ".sprite"
    if not source.has(sprite):
        return None
    root = ET.fromstring(source.read(sprite))

    def borders(node) -> list[float]:
        if node is None:
            return [0.0, 0.0, 1.0, 1.0]
        return [float(node.get("m_left", 0)), float(node.get("m_top", 0)), float(node.get("m_right", 1)),
                float(node.get("m_bottom", 1))]

    def point(text_value: str | None, default: tuple[float, float]) -> list[float]:
        if not text_value:
            return list(default)
        x, y = text_value.split(",")
        return [float(x), float(y)]

    sheet = root.find("SpriteSheet")
    if sheet is None:
        return {"borders": borders(root.find("Sprite"))}
    cells = []
    for cell in sheet.findall("Cell"):
        top_left = point(cell.get("topLeft"), (0.0, 0.0))
        bottom_right = point(cell.get("bottomRight"), (1.0, 1.0))
        cells.append({"uv": top_left + bottom_right, "borders": borders(cell.find("Sprite")), "alias": cell.get("alias", "")})
    return {"cells": cells}


def decode_texture(path: str) -> Image.Image:
    data, width, height, fmt = dds.join(source.read, path, max_size=4096)
    if fmt in (28, 29, 87, 91):   # 8-bit RGBA or BGRA, no blocks
        pixels = np.frombuffer(data[148:148 + width * height * 4], np.uint8).reshape(height, width, 4)
        if fmt in (87, 91):
            pixels = pixels[:, :, [2, 1, 0, 3]]
        return Image.fromarray(pixels.copy(), "RGBA")
    return dds.to_image(data, width, height, fmt).convert("RGBA")


def convert_textures(paths, force: bool = False) -> dict:
    count, skipped, failed = 0, 0, {}
    for path in sorted(set(paths)):
        path = path.lower().replace("\\", "/")
        if not path.endswith(".dds"):
            path = path.rsplit(".", 1)[0] + ".dds"
        target = texture_target(path)
        if target.is_file() and not force:
            skipped += 1
            continue
        try:
            image = decode_texture(path)
        except Exception as error:  # noqa: BLE001
            failed[path] = str(error)[:120]
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        image.save(target, optimize=False, compress_level=3)
        sprite = sprite_data(path)
        side = target.with_suffix(".sprite.json")
        if sprite is not None:
            sprite["size"] = list(image.size)
            side.write_text(json.dumps(sprite))
        elif side.is_file():
            side.unlink()
        count += 1
    return {"converted": count, "present": skipped, "failed": failed}


def convert_texture_index() -> dict:
    """textures/index.json: the size, the borders, and the cells of each texture that a converted canvas or slice
    names (from the DDS header and the .sprite file; no pixels). The runtime takes the sizes from it when a texture is
    not converted yet (the layout cells of images need them)."""
    names: set[str] = set()
    for file in (out_dir() / "canvases").rglob("*.json"):
        doc = json.loads(file.read_text())
        found, _, _, _ = references(doc)
        names |= found
    index = {}
    failed = {}
    for name in sorted(names):
        path = name.lower().replace("\\", "/")
        if not path.endswith(".dds"):
            path = path.rsplit(".", 1)[0] + ".dds"
        try:
            head = source.read(path)
            if head[:4] != b"DDS ":
                raise ValueError("not a DDS file")
            height, width = struct.unpack_from("<II", head, 12)
        except Exception as error:  # noqa: BLE001
            failed[path] = str(error)[:120]
            continue
        entry = {"size": [width, height]}
        sprite = sprite_data(path)
        if sprite is not None:
            entry.update(sprite)
        index[path.rsplit(".", 1)[0]] = entry
    target = out_dir() / "textures"
    target.mkdir(parents=True, exist_ok=True)
    (target / "index.json").write_text(json.dumps(index, separators=(",", ":")))
    return {"textures": len(index), "failed": len(failed)}


def folder_textures(prefix: str) -> list[str]:
    return source.paks().paths(prefix.lower(), ".dds")


# ---------------------------------------------------------------------------------------------------------------- fonts

def convert_fonts() -> dict:
    """The .font files (XML: the font file, its size, the effect file) and the font files that they name."""
    target = out_dir() / "fonts"
    (target / "files").mkdir(parents=True, exist_ok=True)
    fonts = {}
    copied = 0
    for path in source.paks().paths("lyshineui/fonts/", ".font") + source.paks().paths("fonts/", ".font"):
        try:
            root = ET.fromstring(source.read(path))
        except ET.ParseError as error:
            fonts[path] = {"error": str(error)}
            continue
        font = root.find("font")
        if font is None:
            continue
        folder = path.rsplit("/", 1)[0] + "/"
        file_path = (folder + font.get("path", "")).lower()
        effect = root.find("effectfile")
        fonts[path] = {"file": file_path.rsplit("/", 1)[1], "source": file_path, "size": float(font.get("fontsize", 32)),
                       "effects": (effect.get("path", "") if effect is not None else "").lower()}
        out = target / "files" / file_path.rsplit("/", 1)[1]
        if source.has(file_path) and not out.is_file():
            out.write_bytes(source.read(file_path))
            copied += 1
    (target / "fonts.json").write_text(json.dumps(fonts, indent=1, sort_keys=True))
    effects = []
    root = ET.fromstring(source.read("lyshineui/fonts/_sharedfonteffects.xml"))
    for effect in root.findall("effect"):
        passes = []
        for p in effect.findall("pass"):
            c = p.find("color")
            pos = p.find("pos")
            passes.append({"color": [float(c.get(k, 1)) for k in "rgba"] if c is not None else None,
                           "offset": [float(pos.get("x", 0)), float(pos.get("y", 0))] if pos is not None else [0.0, 0.0]})
        effects.append({"name": effect.get("name", ""), "passes": passes})
    (target / "effects.json").write_text(json.dumps(effects, indent=1))
    return {"fonts": len(fonts), "font files copied": copied, "effects": [e["name"] for e in effects]}


# ----------------------------------------------------------------------------------------------------------------- text

def convert_text() -> dict:
    """The English texts: key (lower case) -> text. The runtime looks up "@key" texts in it."""
    texts: dict[str, str] = {}
    failed = []
    for path in source.paks().paths("localization/en-us/", ".loc.xml"):
        try:
            root = ET.fromstring(source.read(path))
        except ET.ParseError:
            failed.append(path)
            continue
        for node in root.iter("string"):
            key = node.get("key")
            if key:
                texts[key.lower()] = node.text or ""
    target = out_dir() / "localization" / "en-us.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(texts, ensure_ascii=False, separators=(",", ":")))
    return {"strings": len(texts), "failed files": failed}


# ------------------------------------------------------------------------------------------------------------- gamedata

def table_rows(file_name: str):
    """@return The rows of a data table of the game (any folder of the data tables)."""
    paths = [p for p in source.paks().paths("", "/" + file_name)]
    if not paths:
        return []
    return list(datasheet.Sheet(source.read(paths[0])).rows())


def number_or_none(value):
    return value if isinstance(value, float) else None


def convert_gamedata() -> dict:
    """The data of the weapon bar of the HUD, from the data tables of the game:
    abilities.json   ability key (CRC-32 of the lower-case AbilityID) -> the columns that the HUD reads, with the
                     cooldown time (javelindata_cooldowns_player, by CooldownId) and the mana cost
                     (javelindata_manacosts_player, by AbilityID); the ability tables with their CRC-32 (the
                     tableNameId of lyshineui/skills/weaponmastery/weaponmasterydata.luac)
    weapons.json     item id (lower case) -> the weapon definition of its ItemStatsRef (javelindata_itemdefinitions_weapons
                     and _runes): WeaponMasteryCategoryId (the ability table), IsShieldCompatible, ManaCostId, MannequinTag,
                     TierNumber, EquipAbility (the heart rune), ReticleName and ReticleTargetName (the reticle of the
                     HUD: reticlesmanager.luac takes the CRC-32 of the lower-case name), and the Name and Description keys
                     of the item
    afflictions.json the affliction rows (javelindata_afflictions): IntID, AfflictionID, Max, IsGlobalAffliction, ColorHex
                     (AARRGGBB), Icon, and the two tooltip keys
    """
    import zlib
    cooldowns = {r["AbilityID"]: r for r in table_rows("javelindata_cooldowns_player.datasheet") if r.get("AbilityID")}
    mana = {r["CostID"]: r.get("ManaCost") for r in table_rows("javelindata_manacosts_player.datasheet") if r.get("CostID")}
    abilities: dict[str, dict] = {}
    tables: dict[str, dict] = {}
    for path in source.paks().paths("", ".datasheet"):
        if "/weaponabilities/" not in path:
            continue
        sheet = datasheet.Sheet(source.read(path))
        tables[sheet.name] = {"crc": zlib.crc32(sheet.name.lower().encode()) & 0xFFFFFFFF, "file": path.rsplit("/", 1)[1]}
        for row in sheet.rows():
            ability = row.get("AbilityID")
            if not ability:
                continue
            cooldown = cooldowns.get(row.get("CooldownId") or ability, {})
            key = str(zlib.crc32(ability.lower().encode()) & 0xFFFFFFFF)
            abilities[key] = {
                "id": ability, "table": sheet.name, "displayName": row.get("DisplayName", ""),
                "icon": (row.get("Icon") or "").lower().replace("\\", "/"), "uiCategory": row.get("UICategory", ""),
                "weaponTag": row.get("WeaponTag", ""), "treeId": number_or_none(row.get("TreeId")) or 0,
                "active": bool(row.get("IsActiveAbility")), "cooldownId": row.get("CooldownId", ""),
                "cooldown": number_or_none(cooldown.get("Time")) or 0, "manaCost": number_or_none(mana.get(ability)) or 0,
                "holdIcon": (row.get("HoldConditionButtonIcon") or "").lower().replace("\\", "/"),
            }
    weapons_by_id = {r["WeaponID"]: r for name in ("javelindata_itemdefinitions_weapons.datasheet",
                                                   "javelindata_itemdefinitions_runes.datasheet")
                     for r in table_rows(name) if r.get("WeaponID")}
    weapons: dict[str, dict] = {}
    for path in source.paks().paths("", ".datasheet"):
        name = path.rsplit("/", 1)[1]
        if not name.startswith("javelindata_itemdefinitions_master_"):
            continue
        for row in datasheet.Sheet(source.read(path)).rows():
            if row.get("ItemType") != "Weapon" or not row.get("ItemID"):
                continue
            weapon = weapons_by_id.get(row.get("ItemStatsRef") or "")
            if weapon is None:
                continue
            weapons[row["ItemID"].lower()] = {
                "weaponId": weapon["WeaponID"], "masteryTable": weapon.get("WeaponMasteryCategoryId", ""),
                "shield": bool(weapon.get("IsShieldCompatible")), "manaCostId": weapon.get("ManaCostId", ""),
                "itemClass": row.get("ItemClass", ""),
                "mannequinTags": [t.strip() for t in (weapon.get("MannequinTag") or "").split(",") if t.strip()],
                "tier": number_or_none(weapon.get("TierNumber")) or 0,
            }
            for key, column in (("equipAbility", "EquipAbility"), ("name", "Name"), ("description", "Description"),
                                ("reticle", "ReticleName"), ("reticleTarget", "ReticleTargetName")):
                if row.get(column) or weapon.get(column):
                    weapons[row["ItemID"].lower()][key] = row.get(column) or weapon.get(column)
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "abilities.json").write_text(json.dumps({"tables": tables, "abilities": abilities}, separators=(",", ":")))
    (target / "weapons.json").write_text(json.dumps(weapons, separators=(",", ":")))
    # The afflictions (javelindata_afflictions): the meters of the HUD and of the equipment screen, in the order of the table.
    afflictions = [{"id": int(r["IntID"]), "key": r["AfflictionID"], "max": number_or_none(r.get("Max")) or 0,
                    "global": bool(r.get("IsGlobalAffliction")), "color": r.get("ColorHex", ""), "icon": r.get("Icon", ""),
                    "tooltip": r.get("AfflictionTooltipText", ""), "afflictedTooltip": r.get("AfflictedTooltipText", "")}
                   for r in table_rows("javelindata_afflictions.datasheet")
                   if r.get("AfflictionID") and isinstance(r.get("IntID"), (int, float))]
    (target / "afflictions.json").write_text(json.dumps(afflictions, separators=(",", ":")))
    owner = convert_owner_stats()
    (target / "ownerstats.json").write_text(json.dumps(owner, separators=(",", ":"), sort_keys=True))
    return {"ability tables": len(tables), "abilities": len(abilities), "weapons": len(weapons), "afflictions": len(afflictions),
            "owner stat statuses": len(owner.get("statuses", {}))}


# The columns of a status row that the character values of the equipment screen use (stage MVP-E): the armor parts and the
# absorption of each damage type (the same columns as the backend, world/statuses.cpp Statuses::values).
OWNER_STAT_COLUMNS = ("PhysicalArmor", "ElementalArmor")


def status_index(rows: dict[str, dict]) -> dict[str, str]:
    """@return The index of the StatusEffects state (the backend copy, ability-data.json statusEffects: the id and the index of each
    row) -> the status id, for the statuses of rows."""
    gamedata = Path(os.environ.get("SWC_GAMEDATA_DIR") or env.work_dir() / "backend/current/gamedata")
    path = gamedata / "ability-data.json"
    if not path.is_file():
        return {}
    return {str(row[1]): row[0] for row in json.loads(path.read_text()).get("statusEffects", []) if row[0] in rows}


def convert_owner_stats() -> dict:
    """@return The status rows with an armor part or an absorption, by the index of the StatusEffects state of the owner (slot 44: the
    compact map names a status by its index), and the limits of the status categories (the backend copy, ability-data.json
    statusEffects and statusCategories; the same data as the server). The equipment screen shows the armor of the backend rule
    (swc/owner.lua Swc.ArmorRating)."""
    gamedata = Path(os.environ.get("SWC_GAMEDATA_DIR") or env.work_dir() / "backend/current/gamedata")
    path = gamedata / "ability-data.json"
    if not path.is_file():
        return {}
    data = json.loads(path.read_text())
    statuses: dict[str, dict] = {}
    for row in data.get("statusEffects", []):
        cells = row[9] if len(row) > 9 and isinstance(row[9], dict) else {}
        values = {k: v for k, v in cells.items() if isinstance(v, (int, float)) and v != 0
                  and (k in OWNER_STAT_COLUMNS or k.startswith("ABS"))}
        if values:
            statuses[str(row[1])] = {"id": row[0], "values": values,
                                     "categories": [c for c in str(cells.get("EffectCategories") or "").split("+") if c]}
    categories = {name: limits for name, limits in data.get("statusCategories", []) if limits}
    return {"statuses": statuses, "categories": categories}


def convert_world() -> dict:
    """The data of the UI in the world views (stage WF7; docs/UI.md):
    statuseffects.json  status id -> the columns that the name plate (markerstatuseffect.luac, the native part of the marker)
                        and the damage numbers read, for each row that shows there (ShowInNameplates, ShowTextInNameplates,
                        ShowTextInDamageNumbers; javelindata_statuseffects*.datasheet): PlaceholderIcon, IsNegative,
                        UiNameplatePriority, OverrideOtherNameplateText, HidesOtherStatusEffectIcons, DisplayName
    statustray.json     the rows of the status tray of the HUD (statuseffectsv2; ShowInUITray): status id -> PlaceholderIcon,
                        IsNegative, DisplayName, Description, UIPriority, HideHudDurationNumbers; and the index of the StatusEffects
                        state of the owner -> the status id (the backend copy)
    damagetypes.json    the number of a damage type on the wire (IntID of javelindata_damagetypes) -> TypeID, DisplayName,
                        Category
    interactoptions.json  the option records of the UI database: option name -> the label key and the additional info slice
    defenses.json       the weakness and the absorption of each vitals row by damage type (convert_defenses)
    xplevels.json       the total experience at which each level starts (XPToLevel of javelindata_xpamountsbylevel)
    and the canvases of WORLD_CANVASES with their textures and the sprite folders that their scripts use."""
    statuses: dict[str, dict] = {}
    tray: dict[str, dict] = {}
    for path in sorted(source.paks().paths("", ".datasheet")):
        name = path.rsplit("/", 1)[1]
        if not re.match(r"javelindata_statuseffects[a-z0-9_]*\.datasheet$", name):
            continue
        for row in datasheet.Sheet(source.read(path)).rows():
            status = row.get("StatusID")
            if status and row.get("ShowInUITray") and status not in tray:
                tray[status] = {"icon": (row.get("PlaceholderIcon") or "").lower().replace("\\", "/"),
                                "negative": bool(row.get("IsNegative")), "name": row.get("DisplayName") or "",
                                "description": row.get("Description") or "", "priority": number_or_none(row.get("UIPriority")) or 0,
                                "hideDuration": bool(row.get("HideHudDurationNumbers"))}
            shown = bool(row.get("ShowInNameplates")) or bool(row.get("ShowTextInNameplates")) or bool(row.get("ShowTextInDamageNumbers"))
            if not status or not shown or status in statuses:
                continue
            entry = {"icon": (row.get("PlaceholderIcon") or "").lower().replace("\\", "/"), "negative": bool(row.get("IsNegative")),
                     "nameplate": bool(row.get("ShowInNameplates")), "nameplateText": bool(row.get("ShowTextInNameplates")),
                     "damageText": bool(row.get("ShowTextInDamageNumbers")), "name": row.get("DisplayName") or ""}
            for key, column in (("priority", "UiNameplatePriority"), ("stackMax", "StackMax")):
                if number_or_none(row.get(column)) is not None:
                    entry[key] = row[column]
            for key, column in (("overrideText", "OverrideOtherNameplateText"), ("hidesOthers", "HidesOtherStatusEffectIcons")):
                if row.get(column):
                    entry[key] = True
            statuses[status] = entry
    damage_types = {str(int(r["IntID"])): {"id": r["TypeID"], "name": r.get("DisplayName", ""), "category": r.get("Category", "")}
                    for r in table_rows("javelindata_damagetypes.datasheet") if r.get("TypeID") and isinstance(r.get("IntID"), float)}
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "statuseffects.json").write_text(json.dumps(statuses, separators=(",", ":"), sort_keys=True))
    tray_index = status_index(tray)
    (target / "statustray.json").write_text(json.dumps({"rows": tray, "index": tray_index}, separators=(",", ":"), sort_keys=True))
    (target / "damagetypes.json").write_text(json.dumps(damage_types, separators=(",", ":"), sort_keys=True))
    options = convert_interact_options()
    (target / "interactoptions.json").write_text(json.dumps(options, separators=(",", ":"), sort_keys=True))
    defenses = convert_defenses()
    (target / "defenses.json").write_text(json.dumps(defenses, separators=(",", ":"), sort_keys=True))
    # The total experience at which each level starts (javelindata_xpamountsbylevel XPToLevel, by the row of the level of the wire;
    # protocol notes 4.12): the XP bar of the progression line of the loot ticker.
    levels = [r.get("XPToLevel") or 0 for r in table_rows("javelindata_xpamountsbylevel.datasheet")]
    (target / "xplevels.json").write_text(json.dumps(levels, separators=(",", ":")))
    # The textures: the canvases of the world views and the UI slices that they spawn, and the folders of the sprites that the
    # scripts set (the markers, the status icons, the icons of the damage types: TooltipCommon GetIconFromDamageTypeName; the icons
    # of the gathering types: gatheringtooladditionalinfo.luac).
    canvases = convert_canvases(WORLD_CANVASES)
    textures = set(canvases.pop("textures"))
    for folder in ("lyshineui/images/markers/", "lyshineui/images/status/", "lyshineui/images/icons/tooltip/",
                   "lyshineui/images/icons/gatherables/", "lyshineui/images/hitindicator/"):
        textures |= set(folder_textures(folder))
    return {"status rows": len(statuses), "on the plates": sum(1 for e in statuses.values() if e["nameplate"]),
            "in the tray": len(tray), "tray rows of the backend": len(tray_index),
            "damage types": len(damage_types), "interact options": len(options), "defenses": len(defenses), "levels": len(levels),
            "canvases": canvases,
            "textures": convert_textures(textures),
            "texture index": convert_texture_index()}


# The option records of the UI database (sharedassets/genericassets/ui/uidatabase.uidb; the native UnifiedInteractOptionComponent fills
# the rows of the prompt card from them): the record type, and the fields Name, the label key ("@ui_storage_interact": the text of the
# row), and the name of the additional info slice of the row ("ContainerAdditionalInfo"; an element of AdditionalInfoSlices of the
# canvas lyshineui/markers/unifiedinteract). The names of the two fields are not known (their ids).
UIDB_OPTION_TYPE = bytes.fromhex("f0887e97" "5084" "413c" "bce7" "5c24cecb03c0")
F_OPTION_LABEL = 0xAECBE459
F_OPTION_ADDITIONAL_INFO = 0xD818B750


def convert_interact_options() -> dict:
    """@return The option records of the UI database: option name -> {label, additionalInfo}."""
    roots = source.parse(source.read("sharedassets/genericassets/ui/uidatabase.uidb"))
    out: dict[str, dict] = {}
    for element in source.walk(roots[0]):
        if element.type_id != UIDB_OPTION_TYPE:
            continue
        name = source.text(element.child(source.field_id("Name")))
        if not name or name in out:
            continue
        entry = {"label": source.text(element.child(F_OPTION_LABEL))}
        info = source.text(element.child(F_OPTION_ADDITIONAL_INFO))
        if info:
            entry["additionalInfo"] = info
        out[name] = entry
    return out


def convert_defenses() -> dict:
    """@return The weakness and the absorption of each vitals row by damage type (the damage numbers: the yellow number of a
    weakness, the immune text of an absorption of 1): the CRC-32 of the lower-case VitalsID (the vitals id of the wire) -> {weak,
    abs}, for the rows with one of them. The source is the backend copy (combat-tables.json: the WKN and ABS columns of the vitals
    tables of the game, for each vitals row its defense row)."""
    import zlib
    gamedata = Path(os.environ.get("SWC_GAMEDATA_DIR") or env.work_dir() / "backend/current/gamedata")
    path = gamedata / "combat-tables.json"
    if not path.is_file():
        return {}
    tables = json.loads(path.read_text())
    rows = tables.get("defenses", [])
    out: dict[str, dict] = {}
    for name, index in tables.get("vitals", {}).items():
        if not isinstance(index, int) or index >= len(rows):
            continue
        row = rows[index]
        weak, absorb = (row[2] if len(row) > 2 else {}) or {}, (row[3] if len(row) > 3 else {}) or {}
        if weak or absorb:
            out[str(zlib.crc32(name.lower().encode()) & 0xFFFFFFFF)] = {"weak": weak, "abs": absorb}
    return out


# The canvases of the UI in the world views (stage WF7): the markers (the AI plates), the prompt cards, the damage numbers, the loot
# ticker; the hit indicator and the status tray (stage MVP-I).
WORLD_CANVASES = ["lyshineui/markers/markermanager.dynamicuicanvas", "lyshineui/markers/unifiedinteract.dynamicuicanvas",
                  "lyshineui/hud/damagenumbers/damagenumbers.dynamicuicanvas",
                  "lyshineui/hud2024/lootticker/lootticker2024.dynamicuicanvas",
                  # Stage MVP-I: the hit indicator (the red edge and the arrow of a hit on the own character).
                  "lyshineui/hitindicator/hitindicator.dynamicuicanvas",
                  # Stage MVP-I: the status tray of the own character (statuseffectsv2; statustray.json).
                  "lyshineui/statuseffects/statuseffectsv2.dynamicuicanvas"]


# The columns of the item definitions that the inventory screens use (javelindata_itemdefinitions_master_*), with their
# names in items.json. A number column keeps its number; an empty value is left out.
ITEM_COLUMNS = {
    "Name": "name", "Description": "description", "ItemType": "itemType", "ItemTypeDisplayName": "itemTypeDisplayName",
    "ItemClass": "itemClass", "UiItemClass": "uiItemClass", "Tier": "tier", "Weight": "weight", "MaxStackSize": "maxStack",
    "GearScoreOverride": "gearScoreOverride", "MinGearScore": "minGearScore", "MaxGearScore": "maxGearScore",
    "Durability": "durability", "DurabilityDmgOnDeath": "durabilityDamageOnDeath", "BindOnPickup": "bindOnPickup",
    "BindOnEquip": "bindOnEquip", "Nonremovable": "nonRemovable", "NotDroppable": "notDroppable",
    "NotDroppableNotificationOverride": "notDroppableNotification", "RequiredLevel": "requiredLevel",
    "RequiredTradeskillRank": "requiredTradeskillRank", "RequiredExpansionId": "requiredExpansionId",
    "CanHavePerks": "canHavePerks", "CanReplaceGem": "canReplaceGem", "IsRepairable": "repairable",
    "IsSalvageable": "salvageable", "ConfirmBeforeUse": "confirmBeforeUse", "ConfirmDestroy": "confirmDestroy",
    "ConsumeOnUse": "consumeOnUse", "TradingCategory": "tradingCategory", "TradingFamily": "tradingFamily",
    "TradingGroup": "tradingGroup", "ItemStatsRef": "itemStatsRef", "ForceRarity": "forceRarity", "IsUniqueItem": "unique",
    "HideInLootTicker": "hideInLootTicker", "IngredientCategories": "ingredientCategories", "EquipmentSetId": "equipmentSetId",
    "Perk1": "perk1", "Perk2": "perk2", "Perk3": "perk3", "Perk4": "perk4", "Perk5": "perk5",
    "PerkBucket1": "perkBucket1", "PerkBucket2": "perkBucket2", "PerkBucket3": "perkBucket3", "PerkBucket4": "perkBucket4",
    "PerkBucket5": "perkBucket5", "ArmorAppearanceM": "armorAppearanceM", "ArmorAppearanceF": "armorAppearanceF",
    "WeaponAppearanceOverride": "weaponAppearance", "SalvageResources": "salvageResources", "RepairRecipe": "repairRecipe",
}


def icon_paths(row: dict, armor: dict, weapons: dict) -> tuple[str, str]:
    """@return (icon, hires icon) of an item: its IconPath and HiResIconPath; else the icons of its armor appearance
    (ArmorAppearanceM in javelindata_itemappearancedefinitions) or of its weapon appearance (WeaponAppearanceOverride, else the
    ItemID, in javelindata_itemdefinitions_weaponappearances). The rule of the native ItemDataManagerBus.GetIconPath is not
    known: this rule follows the tables (armor and weapon rows have no IconPath; their appearance rows have one)."""
    def norm(value):
        return (value or "").lower().replace("\\", "/")
    icon, hires = norm(row.get("IconPath")), norm(row.get("HiResIconPath"))
    if not icon:
        look = armor.get((row.get("ArmorAppearanceM") or "").lower()) or weapons.get(
            (row.get("WeaponAppearanceOverride") or row.get("ItemID") or "").lower())
        if look:
            icon, hires = norm(look.get("IconPath")), hires or norm(look.get("HiResIconPath"))
    return icon, hires


def convert_items() -> dict:
    """items.tsv (write_key_table): item id (lower case) -> the columns of ITEM_COLUMNS, the original id, and the icon paths (icon_paths), for
    each row of the master item definitions (the first row of an id wins). The inventory screens read it
    (ItemDataManagerBus); the item index of the wire comes from the item catalog of the backend. encumbrance.json: the weight
    limits of the bag of the player. perks.tsv: the perks (javelindata_perks*). consumables.tsv: the consumables."""
    armor = {(r.get("ItemID") or "").lower(): r for r in table_rows("javelindata_itemappearancedefinitions.datasheet")}
    weapons = {(r.get("WeaponAppearanceID") or "").lower(): r
               for r in table_rows("javelindata_itemdefinitions_weaponappearances.datasheet")}
    items: dict[str, dict] = {}
    for path in sorted(source.paks().paths("", ".datasheet")):
        name = path.rsplit("/", 1)[1]
        if not name.startswith("javelindata_itemdefinitions_master_"):
            continue
        for row in datasheet.Sheet(source.read(path)).rows():
            item_id = row.get("ItemID")
            if not item_id or item_id.lower() in items:
                continue
            entry = {"id": item_id}
            for column, key in ITEM_COLUMNS.items():
                value = row.get(column)
                if value not in (None, ""):
                    entry[key] = value
            icon, hires = icon_paths(row, armor, weapons)
            if icon:
                entry["icon"] = icon
            if hires:
                entry["hiresIcon"] = hires
            items[item_id.lower()] = entry
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    write_key_table(target / "items.tsv", items)
    # The perks (javelindata_perks*.datasheet): perk id (lower case) -> the columns that the tooltips and the filters read.
    perk_columns = {"DisplayName": "displayName", "Description": "description", "PerkType": "perkType", "IconPath": "iconPath",
                    "Tier": "tier", "ItemClass": "itemClass", "StatDisplayText": "statDisplayText", "Affix": "affix",
                    "AppliedPrefix": "appliedPrefix", "AppliedSuffix": "appliedSuffix", "ScalingPerGearScore": "scalingPerGearScore",
                    "EquipAbility": "equipAbility", "ExclusiveLabels": "exclusiveLabels", "Sorting": "sorting",
                    "SecondaryEffectDisplayName": "secondaryEffectDisplayName",
                    # The range of the generic text (research forge-order.md 2, AST-234): the class bonus of the display score,
                    # the cap of the scaling, the scaling of the player level.
                    "ItemClassGSBonus": "itemClassGSBonus", "MaxGearScoreForScaling": "maxGearScoreForScaling",
                    "ScalingPerPlayerLevel": "scalingPerPlayerLevel"}
    perks: dict[str, dict] = {}
    for path in sorted(source.paks().paths("", ".datasheet")):
        name = path.rsplit("/", 1)[1]
        if not re.match(r"javelindata_perks(_2025|_artifacts|_gems|_infix|_deprecated)?\.datasheet$", name):
            continue
        for row in datasheet.Sheet(source.read(path)).rows():
            perk_id = row.get("PerkID")
            if not perk_id or perk_id.lower() in perks:
                continue
            entry = {"id": perk_id}
            for column, key in perk_columns.items():
                value = row.get(column)
                if value not in (None, ""):
                    entry[key] = value.lower().replace("\\", "/") if key == "iconPath" else value
            perks[perk_id.lower()] = entry
    # The attribute values of the affixes of each perk (javelindata_affixstats MOD<attribute>, in the order of CharacterAttributeType:
    # Constitution, Focus, Strength, Dexterity, Intelligence; AST-259): CharacterAttributeRequestBus GetAttributeBonus of the tooltip
    # (dynamictooltip_itemstats.luac IsAttributePerk) is these values x the potency of the perk (backend world/gear.cpp).
    affix_mods = {}
    for row in table_rows("javelindata_affixstats.datasheet"):
        name = str(row.get("StatusID") or "").strip()
        if name:
            affix_mods[name.lower()] = [float(row.get(c) or 0.0) for c in
                                        ("MODConstitution", "MODFocus", "MODStrength", "MODDexterity", "MODIntelligence")]
    for entry in perks.values():
        total = [0.0] * 5
        for name in re.split(r"[,+]", str(entry.get("affix") or "")):
            for index, value in enumerate(affix_mods.get(name.strip().lower(), [])):
                total[index] += value
        if any(total):
            entry["attributes"] = ",".join(f"{value:g}" for value in total)
    write_key_table(target / "perks.tsv", perks)
    # The consumables (javelindata_itemdefinitions_consumables): item id (lower case) -> the cooldown and the effects.
    consumable_columns = {"CooldownId": "cooldownId", "CooldownDuration": "cooldownDuration", "AddStatusEffects": "addStatusEffects",
                          "MannequinTag": "mannequinTag", "SharedCooldownLocString": "sharedCooldownLocString",
                          "DurationScaleFactor": "durationScaleFactor", "StatusEffectMod": "statusEffectMod"}
    consumables: dict[str, dict] = {}
    for row in table_rows("javelindata_itemdefinitions_consumables.datasheet"):
        consumable_id = row.get("ConsumableID")
        if not consumable_id or consumable_id.lower() in consumables:
            continue
        entry = {"id": consumable_id}
        for column, key in consumable_columns.items():
            if row.get(column) not in (None, ""):
                entry[key] = row[column]
        consumables[consumable_id.lower()] = entry
    write_key_table(target / "consumables.tsv", consumables)
    # The weight limits of the bag of the player (javelindata_encumbrancelimits, row Player; tenths for the weights).
    limits = next((r for r in table_rows("javelindata_encumbrancelimits.datasheet") if r.get("ContainerTypeID") == "Player"), {})
    keep = ("EncumbranceBaseMax", "EncumbranceWarningPercent", "EquipLoadBaseMax", "EquipLoadWarningPercent", "FullWhenEncumbered",
            "EquipLoadRatioFast", "EquipLoadRatioNormal", "EquipLoadRatioSlow", "EquipLoadRatioOverburdened")
    (target / "encumbrance.json").write_text(json.dumps({k: limits.get(k) for k in keep}, separators=(",", ":")))
    counts = convert_item_stats(target)
    return {"items": len(items), "with icon": sum(1 for e in items.values() if "icon" in e), "perks": len(perks), **counts}


# The stat columns of the weapon definitions (javelindata_itemdefinitions_weapons and the other weapon tables, by WeaponID =
# the ItemStatsRef of an item) that the item tooltips read, with their names in weaponstats.tsv.
WEAPON_STAT_COLUMNS = {
    "BaseDamage": "baseDamage", "CritChance": "critChance", "CritDamageMultiplier": "critDamageMultiplier",
    "BlockStaminaDamage": "blockStaminaDamage", "BlockStability": "blockStability", "ScalingStrength": "scalingStrength",
    "ScalingDexterity": "scalingDexterity", "ScalingIntelligence": "scalingIntelligence", "ScalingFocus": "scalingFocus",
    "WeaponBaseDamageCompoundIncrease": "gearScoreRate", "GatheringEfficiency": "gatheringEfficiency",
    "PhysicalArmorSetScaleFactor": "physicalArmor", "ElementalArmorSetScaleFactor": "elementalArmor",
    "ArmorRatingScaleFactor": "armorRating", "PrimaryUse": "primaryUse", "MannequinTag": "mannequinTag",
    "TooltipPrimaryAttackData": "primaryAttack", "TooltipAlternateAttackData": "alternateAttack",
    "GatheringTypes": "gatheringTypes",
}
# The columns of the armor definitions (javelindata_itemdefinitions_armor, by WeaponID = the ItemStatsRef of an item), with
# the absorption (ABS*) and the resistance (RES*) columns as they are.
ARMOR_STAT_COLUMNS = {
    "PhysicalArmorSetScaleFactor": "physicalArmor", "ElementalArmorSetScaleFactor": "elementalArmor",
    "ArmorRatingScaleFactor": "armorRating", "Encumbrance": "encumbrance", "EquipLoad": "equipLoad",
    "WeightClass": "weightClass", "TrinketBonus": "trinketBonus", "ScaleHealthPerGS": "scaleHealthPerGS",
    "ScaleDamagePerGS": "scaleDamagePerGS", "ScaleCDRPerGS": "scaleCDRPerGS",
}
# The fields of the base attributes file of the player (sharedassets/genericassets/playerbaseattributes.pbadb; the field id
# is the CRC-32 of the name in lower case) for the gear score rule of the weapon damage (client code at image RVA 0x3691470,
# Soulwarden Combat::weapon_base) and for the armor rating of an item (client code at image RVA 0x5BFB400).
ITEM_RULE_FIELDS = {
    "levelDamage": ("Level Damage Multiplier", "float"),
    "gearScoreRate": ("Base Damage Compound Increase", "float"),
    "gearScoreSmallerRate": ("Compound Increase Diminishing Multiplier", "float"),
    "gearScoreInterval": ("Base Damage Gear Score Interval", "whole"),
    "gearScoreMinimum": ("Min Possible Weapon Gear Score", "whole"),
    "gearScoreLimit": ("Diminishing Gear Score Threshold", "whole"),
    "armorRatingExponent": ("Armor Set Rating Exponent", "float"),
}


def convert_item_stats(target: Path) -> dict:
    """weaponstats.tsv and armorstats.tsv (write_key_table): the stat rows of the weapon and the armor definitions, by the
    lower-case WeaponID (the ItemStatsRef of an item); itemrules.json: the constants of ITEM_RULE_FIELDS, the expansions, and
    the scaling tiers of the weapons."""
    import struct
    from .source import walk
    weapons: dict[str, dict] = {}
    armors: dict[str, dict] = {}
    for path in sorted(source.paks().paths("", ".datasheet")):
        name = path.rsplit("/", 1)[1]
        is_weapon = name.startswith("javelindata_itemdefinitions_weapons")
        if not is_weapon and name != "javelindata_itemdefinitions_armor.datasheet":
            continue
        rows, columns = (weapons, WEAPON_STAT_COLUMNS) if is_weapon else (armors, ARMOR_STAT_COLUMNS)
        for row in datasheet.Sheet(source.read(path)).rows():
            stats_id = row.get("WeaponID")
            if not stats_id or stats_id.lower() in rows:
                continue
            entry = {"id": stats_id}
            for column, key in columns.items():
                if row.get(column) not in (None, ""):
                    entry[key] = row[column]
            if not is_weapon:
                for column, value in row.items():
                    if column[:3] in ("ABS", "RES") and isinstance(value, (int, float)) and value:
                        entry[column] = value
            rows[stats_id.lower()] = entry
    write_key_table(target / "weaponstats.tsv", weapons)
    write_key_table(target / "armorstats.tsv", armors)
    values: dict[int, bytes] = {}
    for root in parse(source.read("sharedassets/genericassets/playerbaseattributes.pbadb")):
        for element in walk(root):
            if element.name is not None and element.value is not None:
                values.setdefault(element.name, element.value)
    rules = {}
    for key, (label, kind) in ITEM_RULE_FIELDS.items():
        value = values.get(field_id(label))
        if value is None or (kind == "float" and len(value) != 4):
            raise SystemExit(f"playerbaseattributes.pbadb has no field \"{label}\"")
        rules[key] = int.from_bytes(value, "big") if kind == "whole" else round(struct.unpack(">f", value)[0], 6)
    # The scaling tiers of the weapons (javelindata_weapontiers): the tier letter and its lowest scaling value, highest first.
    # The expansions (javelindata_expansions): the largest gear score of an equipped item and the largest level of each.
    rules["expansions"] = [{"id": r["ExpansionId"], "name": r.get("DisplayName", ""), "maxEquipGS": number_or_none(r.get("MaxEquipGS")) or 0,
                            "maxCraftGS": number_or_none(r.get("MaxCraftGS")) or 0,
                            "maxDisplayLevel": number_or_none(r.get("MaxDisplayLevel")) or 0}
                           for r in table_rows("javelindata_expansions.datasheet") if r.get("ExpansionId")]
    rules["weaponTiers"] = sorted(([r["Tier"], round(r["ScalingStart"], 6)] for r in table_rows("javelindata_weapontiers.datasheet")
                                   if r.get("Tier") and isinstance(r.get("ScalingStart"), (int, float))), key=lambda t: -t[1])
    # AST-231 (research crafting-ui.md 3): the rows of TooltipPrimaryAttackData and TooltipAlternateAttackData of the weapons
    # (javelindata_damagetable, the player table first): [DamageType, DmgCoef, AddDmg]; the gear score limit of each level
    # (javelindata_xpamountsbylevel: [Level Number, MaxEquippableGearScore, IgnoreItemRarity], the automatic level requirement of
    # an item, client code 0x4403360); the Level Requirement Modifier of each item rarity (Item Rarity Data of the base attributes,
    # rarity 0 first; client code 0x5C4C9B1).
    wanted = {str(w.get(k)).lower() for w in weapons.values() for k in ("primaryAttack", "alternateAttack") if w.get(k)}
    attacks: dict[str, list] = {}
    damage_tables = sorted(source.paks().paths("", ".datasheet"), key=lambda q: not q.endswith("/javelindata_damagetable.datasheet"))
    for path in damage_tables:
        if "/javelindata_damagetable" not in path:
            continue
        for row in datasheet.Sheet(source.read(path)).rows():
            key = str(row.get("DamageID") or "").lower()
            if key in wanted and key not in attacks:
                attacks[key] = [row.get("DamageType") or "", float(row.get("DmgCoef") or 0), float(row.get("AddDmg") or 0)]
    rules["attackRows"] = attacks
    rules["levelGearScores"] = [[int(r["Level Number"]), number_or_none(r.get("MaxEquippableGearScore")) or 0, bool(r.get("IgnoreItemRarity"))]
                                for r in table_rows("javelindata_xpamountsbylevel.datasheet") if r.get("Level Number") is not None]
    modifiers = []
    modifier_id = field_id("Level Requirement Modifier")
    for root in parse(source.read("sharedassets/genericassets/playerbaseattributes.pbadb")):
        for element in walk(root):
            if element.name == modifier_id and element.value is not None and len(element.value) == 4:
                modifiers.append(int.from_bytes(element.value, "big", signed=True))
    rules["rarityLevelModifiers"] = modifiers
    (target / "itemrules.json").write_text(json.dumps(rules, separators=(",", ":")))
    return {"weapon stats": len(weapons), "armor stats": len(armors), "attack rows": len(attacks), "rarity modifiers": len(modifiers)}


def write_key_table(path: Path, rows: dict[str, dict]) -> None:
    """A keyed table for the native swcui.table_row of the runtime: a head line ("key", then each column; ":n" marks a column
    whose values are all numbers), then one line for each key (lower case) with its values, tab-separated (an empty value:
    no value). A tab or a line end in a value becomes a space."""
    columns: list[str] = []
    for row in rows.values():
        for column in row:
            if column not in columns:
                columns.append(column)
    def is_number(value):
        if isinstance(value, bool):
            return True
        if isinstance(value, (int, float)):
            return True
        try:
            float(value)
            return True
        except (TypeError, ValueError):
            return False
    numeric = {c: all(is_number(r[c]) for r in rows.values() if c in r) for c in columns}
    def text(value, number):
        if number:
            value = float(value)
            return str(int(value)) if value.is_integer() else repr(value)
        return str(value).replace("\t", " ").replace("\n", " ").replace("\r", " ")
    lines = ["\t".join(["key"] + [c + (":n" if numeric[c] else "") for c in columns])]
    for key, row in rows.items():
        lines.append("\t".join([key] + [text(row[c], numeric[c]) if c in row else "" for c in columns]))
    path.write_text("\n".join(lines) + "\n")


def convert_uistates() -> dict:
    """uistates.json: the screen states of the game (libs/ui/uistates.xml, the table of the native screen manager): for each
    state its CRC-32 (of the lower-case name), its level, its close rules (CloseLowerLevels, CloseHigherLevels), and its
    screens (the canvas name, and disableCanvasOnOut: false for a screen that keeps its canvas when the state ends)."""
    import xml.etree.ElementTree as ET
    import zlib
    root = ET.fromstring(source.read("libs/ui/uistates.xml").decode("utf-8", "replace"))
    states = {}
    for state in root.findall("State"):
        name = state.get("name")
        params = {p.get("name"): p.get("value") for p in state.findall("param")}
        screens = []
        for screen in state.findall("Screens/param"):
            entry = {"name": screen.get("value")}
            if screen.get("disableCanvasOnOut") == "false":
                entry["keepCanvas"] = True
            screens.append(entry)
        states[name] = {"crc": zlib.crc32(name.lower().encode()) & 0xFFFFFFFF, "level": int(params.get("Level", "0")),
                        "closeLower": params.get("CloseLowerLevels") == "true",
                        "closeHigher": params.get("CloseHigherLevels") == "true", "screens": screens}
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "uistates.json").write_text(json.dumps(states, separators=(",", ":")))
    return {"states": len(states)}


# ---------------------------------------------------------------------------------------------------------------- enums

ENUM_NAME = re.compile(r"^([A-Za-z][A-Za-z0-9]*_[A-Za-z0-9_]+|e[A-Z][A-Za-z0-9]+)$")
CONTAINER_NAME = re.compile(r"^(vector|array|pair|unordered_map|map|set)_")


def script_globals() -> tuple[set[str], set[str]]:
    """@return (the global names that the scripts read, the global names that they write), from their bytecode."""
    read: set[str] = set()
    written: set[str] = set()
    for prefix in ("lyshineui/", "scripts/"):
        for path in source.paks().paths(prefix, ".luac"):
            r, w = luabc.globals_used(luabc.load(source.read(path)))
            read |= r
            written |= w
    return read, written


def convert_enums() -> dict:
    """The enum values of the client program (uienums.py) for each enum-like global name that a script reads and no
    script writes. gamedata/enums.json: values (name -> number), absent (names with no text in the client: nil in
    the client), unknown (names with a text but no value)."""
    read, written = script_globals()
    names = sorted(n for n in read - written if ENUM_NAME.match(n) and not CONTAINER_NAME.match(n))
    program = Path(os.environ["SOULWARDEN_GAME_DIR"]) / "Bin64" / "NewWorld.exe"
    values, unknown = uienums.enum_values(program, names)
    absent = sorted(n for n, cause in unknown.items() if cause == "no text in the client")
    # The other native globals: classes and functions of the client (read by a script, written by none).
    lua_names = set("""assert collectgarbage dofile error getfenv getmetatable ipairs load loadfile loadstring module next
        pairs pcall print rawequal rawget rawset require select setfenv setmetatable tonumber tostring type unpack xpcall
        coroutine debug io math os package string table _G _VERSION gcinfo newproxy""".split())
    natives = sorted(n for n in read - written if not ENUM_NAME.match(n) and not n.endswith("Bus") and n not in lua_names)
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "enums.json").write_text(json.dumps({
        "program": {"file": "Bin64/NewWorld.exe", "size": program.stat().st_size},
        "values": values, "absent": absent, "natives": natives,
        "unknown": {n: c for n, c in unknown.items() if n not in absent},
    }, indent=0, sort_keys=True))
    return {"names": len(names), "values": len(values), "absent": len(absent), "unknown": len(unknown) - len(absent),
            "natives": len(natives)}


# -------------------------------------------------------------------------------------------------------------- options

OPTIONS_CANVASES = ["lyshineui/options/options.dynamicuicanvas"]
OPTIONS_TEXTURE_FOLDERS = ["lyshineui/images/options/"]
# A texture that the scripts of the screen set by path (the arrows of a slider row).
OPTIONS_TEXTURES = ["lyshineui/images/attributes/modifierleftarrow.dds"]


def convert_options() -> dict:
    """The Options screen (stage P2.3): its canvas and textures, and gamedata/options.json: the defaults of the options
    data of the client program (optiondefaults.py: the fields of SerializedOptionsData, their defaults, and the data
    nodes Hud.LocalPlayer.Options.* that the component binds to them)."""
    program = Path(os.environ["SOULWARDEN_GAME_DIR"]) / "Bin64" / "NewWorld.exe"
    options = optiondefaults.read_option_defaults(program)
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "options.json").write_text(json.dumps(options, indent=0, sort_keys=True))
    # The input profiles of the controller tab (InputProfileDataManagerRequestBus GetAllInputProfileData).
    profiles = [{"displayName": r["DisplayName"], "displayString": r["DisplayString"], "path": r["Path"],
                 "isRebindable": bool(r["Rebindable"])} for r in datasheet.load("javelindata_inputprofiles.datasheet").rows()]
    (target / "inputprofiles.json").write_text(json.dumps(profiles, indent=0))
    canvases = convert_canvases(OPTIONS_CANVASES)
    textures = set(canvases.pop("textures")) | set(OPTIONS_TEXTURES)
    for folder in OPTIONS_TEXTURE_FOLDERS:
        textures |= set(folder_textures(folder))
    return {"fields": len(options["fields"]), "nodes": len(options["nodes"]), "unknown": len(options["unknown"]),
            "input profiles": len(profiles), "canvases": canvases, "textures": convert_textures(textures)}


# ------------------------------------------------------------------------------------------------------------ world map

WORLDMAP_CANVASES = ["lyshineui/worldmap/magicmap.dynamicuicanvas"]
# The tiles of all zoom levels (magicmap.luac and maptile.luac make their paths at run time: about 3,000 files, 2.8 GB), the
# map icons and overlays, and the icons of the map buttons.
WORLDMAP_TEXTURE_FOLDERS = ["lyshineui/worldtiles/newworld_vitaeeterna/", "lyshineui/images/map/", "lyshineui/images/icons/worldmap/"]


def convert_worldmap(force: bool) -> dict:
    data = worldmap.convert()
    canvases = convert_canvases(WORLDMAP_CANVASES)
    textures = set(canvases.pop("textures"))
    for folder in WORLDMAP_TEXTURE_FOLDERS:
        textures |= set(folder_textures(folder))
    return {"data": data, "canvases": canvases, "textures": convert_textures(textures, force)}


# ------------------------------------------------------------------------------------------------------------- crafting

CRAFTING_CANVASES = ["lyshineui/crafting/craftingscreenv4.dynamicuicanvas", "lyshineui/stationproperties/stationproperties.dynamicuicanvas"]
# The images that the crafting scripts name at run time (the station and category icons, the trade skill icons, the craft roll).
CRAFTING_TEXTURE_FOLDERS = ["lyshineui/images/crafting/", "lyshineui/images/icons/stations/", "lyshineui/images/icons/tradeskills/", "lyshineui/images/icons/crafting/"]


def convert_crafting(force: bool) -> dict:
    data = crafting.convert(out_dir())
    canvases = convert_canvases(CRAFTING_CANVASES)
    textures = set(canvases.pop("textures"))
    for folder in CRAFTING_TEXTURE_FOLDERS:
        textures |= set(folder_textures(folder))
    return {"data": data, "canvases": canvases, "textures": convert_textures(textures, force)}


# -------------------------------------------------------------------------------------------------------------- quests

# The images that the quest scripts name at run time (the objective type icons, the journal, the banner cards; AST-236).
# The single textures of the quest screens: the yellow flag of a pinned objective of the journal (journalscreen FlagBg).
QUEST_TEXTURES = ["lyshineui/genericui/images/square.dds"]
QUEST_TEXTURE_FOLDERS = ["lyshineui/images/objectives/", "lyshineui/images/icons/objectives/", "lyshineui/images/journal/", "lyshineui/images/banner/"]


# The base POI slice (POI_CapitalPlot): its Territory component is on the root entity (E = R), so local = G + this component id
# (research poi-cues.md section 2).
POI_BASE_SLICE = "{817279CF-C3F1-56DF-A9C7-282929522FB8}:2"
POI_TERRITORY_COMPONENT = 0x4FB021A86DF7AE7A
POI_CAPITALS = "sharedassets/coatlicue/newworld_vitaeeterna/regions/"


def convert_poi_cues(pois: list) -> dict:
    """gamedata/poi-cues.json (Linear AST-241; research poi-cues.md): the place cues 2297 and 6416 of each point of interest. The
    reference is the Territory component of the POI placement (pois.capitals.json of the region, matched by its world position): the
    actor and group of the UUID rule (G = ((H XOR L) AND NOT 7) OR 3, the UUID halves big-endian) and local = G + the component id for
    the base POI slice (other slices: no local, no cue: an open point). The territory field is the POI id. The achievements are the
    full cells DiscoveredAchievement and ChartedAchievement of the definition (worldmap.json): the CRC-32 of the lower-case name and the
    AchievementIndex bit (achievements.json); no discovery achievement: radius 0 (the loader rule)."""
    import struct
    import uuid
    import zlib
    capitals = []
    for path in source.paks().paths(POI_CAPITALS, "pois.capitals.json"):
        capitals += json.loads(source.read(path))["Capitals"]
    definitions = json.loads((out_dir() / "gamedata" / "worldmap.json").read_text())["definitions"]
    bits_file = out_dir() / "gamedata" / "achievements.json"
    bits = json.loads(bits_file.read_text()) if bits_file.is_file() else {}

    def achievement(name: str) -> list:
        name = (name or "").strip()
        return [zlib.crc32(name.lower().encode()) & 0xFFFFFFFF if name else 0, bits.get(name.lower(), -1) if name else -1]

    out, other_slices = [], 0
    for poi in pois:
        match = [c for c in capitals if sum((c["worldPosition"][k] - v) ** 2 for k, v in zip("xyz", poi["center"])) < 1.0]
        if not match:
            continue
        cap = match[0]
        hi, lo = struct.unpack(">QQ", uuid.UUID(cap["id"]).bytes)
        group = ((hi ^ lo) & ~7 & 0xFFFFFFFFFFFFFFFF) | 3
        base = cap["sliceAssetId"] == POI_BASE_SLICE
        other_slices += 0 if base else 1
        definition = definitions.get(str(poi["id"]), {})
        discovered = achievement(definition.get("discoveredAchievement", ""))
        charted = achievement(definition.get("chartedAchievement", ""))
        out.append({"id": poi["id"], "placement": cap["id"], "slice": cap["sliceAssetId"],
                    "actor": group.to_bytes(8, "little").hex() + "00" * 8, "group": f"{group:016x}",
                    "local": f"{(group + POI_TERRITORY_COMPONENT) & 0xFFFFFFFFFFFFFFFF:016x}" if base else None,
                    "center": poi["center"], "radius": float(definition.get("discoveryRadius") or 0.0) if discovered[0] else 0.0,
                    "shape": poi.get("shape", []), "discovered": discovered, "charted": charted})
    target = out_dir() / "gamedata" / "poi-cues.json"
    target.write_text(json.dumps({"format": 1, "pois": out}, separators=(",", ":")))
    return {"pois": len(out), "withLocal": len(out) - other_slices, "otherSlices": other_slices}


def convert_quests(force: bool) -> dict:
    """gamedata/quests.json (Linear AST-236; research quest-plan.md "Converter output" Q1): the quest data of the backend copy
    (gamedata/quest-data.json of the release, format 1: the tools of the Soulwarden project made it from the game data): the objectives
    (by the lower-case ObjectiveID: id, CRC-32, the cells of the row, the root task, the tree [index, task, children] in the child-first
    index order of the wire, the holes), the task rows (by the lower-case TaskID: id, CRC-32, the type and its code, the cells), and the
    GameEvent rows of the rewards. The client and the server read the same trees: the index of a task element is the index of this tree."""
    source_file = gamedata_dir() / "quest-data.json"
    data = json.loads(source_file.read_text())
    if data.get("format") != 1:
        raise SystemExit(f"{source_file}: quest data format {data.get('format')} (the client reads format 1)")
    objectives = {o["id"].lower(): {k: o[k] for k in ("id", "crc", "cells", "root", "tree", "holes") if k in o} for o in data["objectives"]}
    events = dict(data["events"]) if isinstance(data["events"], list) else data["events"]
    doc = {"format": 1, "source": str(source_file), "objectives": objectives, "tasks": data["tasks"], "events": events,
           "territories": data.get("territories", [])}
    # AST-241: the points of interest of the backend copy (poi-data.json: id, name key, tags, territory, center, radius, shape) for the
    # place names of the task texts ({POITags}) and the place cues 2297 and 6416; the NPC names of the conversation data ({destinationName}).
    poi_file = gamedata_dir() / "poi-data.json"
    cues = {}
    if poi_file.is_file():
        doc["pois"] = [{k: p[k] for k in ("id", "name", "tags", "territory", "center", "radius", "shape") if k in p}
                       for p in json.loads(poi_file.read_text())["pois"]]
        cues = convert_poi_cues(doc["pois"])
    conversation_file = gamedata_dir() / "conversation-data.json"
    if conversation_file.is_file():
        doc["npcNames"] = {n["id"].lower(): n["name"] for n in json.loads(conversation_file.read_text())["npcs"] if n.get("name")}
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "quests.json").write_text(json.dumps(doc, separators=(",", ":"), sort_keys=True))
    textures = set()
    for folder in QUEST_TEXTURE_FOLDERS:
        textures |= set(folder_textures(folder))
    textures |= set(QUEST_TEXTURES)
    return {"objectives": len(objectives), "tasks": len(data["tasks"]), "events": len(events), "pois": len(doc.get("pois", [])), "cues": cues,
            "npcNames": len(doc.get("npcNames", {})),
            "bytes": (target / "quests.json").stat().st_size, "textures": convert_textures(textures, force)}


# -------------------------------------------------------------------------------------------------------------- loaders

LOADER_SCRIPTS = {"screens": "lyshineui/uiloader.luac", "dynamic": "lyshineui/dynamiccanvasloader/dynamiccanvasloader.luac"}
LOADER_PATH = re.compile(r"^lyshineui/[a-z0-9_ ]+(/[a-z0-9_ ]+)+$")


def convert_loader() -> dict:
    """The canvases that the two loaders of the game name: gamedata/loader.json. "screens": the canvas paths in the
    string constants of uiloader.luac (its AddScreen list; the loader picks the screens of the level), "dynamic": the
    ones of dynamiccanvasloader.luac (it loads a canvas when the game asks for it). For each list, KEYNotInGame has the
    paths with no canvas in the game (the retail loader fails on them too) and KEYNotConverted the paths that are not
    converted. The loader fixture (tests/ui/fixtures/loader.lua) checks the run of the loaders against these lists.
    "canvases": each canvas of the game (the census of stage P2.3: tests/ui/fixtures/census.lua checks the table
    Lua/swc/census.lua against it)."""
    in_game = {path[:-len(".dynamicuicanvas")] for path in source.paks().paths("lyshineui/", ".dynamicuicanvas")}
    result: dict = {"canvases": sorted(in_game)}
    for key, script in LOADER_SCRIPTS.items():
        main = luabc.load((out_dir() / "scripts" / script).read_bytes())
        paths = sorted({c.lower() for c in luabc.string_constants(main) if LOADER_PATH.match(c.lower())})
        result[key] = paths
        result[key + "NotInGame"] = [path for path in paths if path not in in_game]
        result[key + "NotConverted"] = [path for path in paths if path in in_game
                                        and not (out_dir() / "canvases" / (path + ".json")).is_file()]
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "loader.json").write_text(json.dumps(result, indent=0, sort_keys=True))
    return {key: len(value) for key, value in result.items()}


# ---------------------------------------------------------------------------------------------------------- action maps

ACTION_PROFILE = "libs/config/defaultprofile.xml"
ACTION_FLAGS = {"onPress": "press", "onRelease": "release", "onHold": "hold", "blockActionsWhenConsumed": "block",
                "retriggerable": "retrigger"}


def read_actionmaps() -> list[dict]:
    """The action maps of the client profile (CryAction, the Javelin fork), with their includes, in file order: for each
    map its name, its start state, and its actions with their keyboard and mouse inputs and trigger flags. The pad
    inputs are not kept."""
    paks = source.paks()
    maps: list[dict] = []

    def flags(element, into: dict):
        for attribute, key in ACTION_FLAGS.items():
            if element.get(attribute) is not None:
                into[key] = element.get(attribute) == "1"
        if element.get("holdTriggerDelay") is not None:
            try:
                into["holdDelay"] = float(element.get("holdTriggerDelay"))
            except ValueError:
                into["holdDelay"] = element.get("holdTriggerDelay")

    def action_of(element) -> dict | None:
        action = {"name": element.get("name"), "inputs": []}
        flags(element, action)
        key = (element.get("keyboard") or "").strip()
        if key:
            action["inputs"].append({"key": key.lower()})
        for keyboard in element.findall("keyboard"):
            shared: dict = {}
            flags(keyboard, shared)
            for data in keyboard.findall("inputdata"):
                one = dict(shared)
                flags(data, one)
                if (data.get("input") or "").strip():
                    one["key"] = data.get("input").strip().lower()
                    action["inputs"].append(one)
        return action if action["inputs"] else None

    def load(path: str):
        root = ET.fromstring(paks.read(path))
        for child in root:
            if child.tag == "include":
                name = child.get("filename", "").lower()
                load(name.rsplit(".", 1)[0] + ".xml")
            elif child.tag == "actionmap":
                actions = [a for a in (action_of(e) for e in child.findall("action")) if a]
                maps.append({"name": child.get("name"), "enabled": child.get("enable", "1") != "0", "actions": actions})

    load(ACTION_PROFILE)
    return maps


def convert_actionmaps() -> dict:
    """gamedata/actionmaps.json: the action maps of the client profile (read_actionmaps). The UI emulation fires the
    game actions (CryActionNotificationsBus) of the enabled maps for each key (docs/UI.md)."""
    maps = read_actionmaps()
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "actionmaps.json").write_text(json.dumps({"maps": maps}, indent=0))
    return {"maps": len(maps), "actions": sum(len(m["actions"]) for m in maps)}


# ------------------------------------------------------------------------------------------------------------------ main

def wanted_file() -> Path:
    return env.work_dir() / "ui" / "wanted.json"


def hud(force: bool) -> dict:
    canvases = convert_canvases(HUD_CANVASES)
    textures = set(canvases.pop("textures"))
    for folder in HUD_TEXTURE_FOLDERS:
        textures |= set(folder_textures(folder))
    result = {"canvases": canvases, "scripts": convert_scripts(), "fonts": convert_fonts(), "text": convert_text(),
              "gamedata": convert_gamedata(), "enums": convert_enums(), "loader": convert_loader(),
              "actionmaps": convert_actionmaps(),
              "textures": convert_textures(textures, force),
              "texture index": convert_texture_index()}
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-convert-ui", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("hud"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("canvases"); p.add_argument("paths", nargs="*"); p.add_argument("--all", action="store_true")
    sub.add_parser("scripts")
    p = sub.add_parser("textures"); p.add_argument("paths", nargs="*"); p.add_argument("--canvas", action="append", default=[])
    p.add_argument("--folder", action="append", default=[]); p.add_argument("--force", action="store_true")
    p = sub.add_parser("wanted"); p.add_argument("file", nargs="?")
    sub.add_parser("fonts")
    sub.add_parser("text")
    sub.add_parser("gamedata")
    sub.add_parser("items")
    sub.add_parser("uistates")
    sub.add_parser("enums")
    sub.add_parser("loader")
    sub.add_parser("actionmaps")
    sub.add_parser("texture-index")
    sub.add_parser("world")
    sub.add_parser("options")
    p = sub.add_parser("worldmap"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("crafting"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("quests"); p.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "hud":
        result = hud(args.force)
    elif args.command == "canvases":
        paths = args.paths
        if args.all:
            paths = source.paks().paths("lyshineui/", ".dynamicuicanvas") + source.paks().paths("lyshineui/", ".dynamicslice")
        result = convert_canvases([p.lower() for p in paths])
        result["textures"] = len(result["textures"])
    elif args.command == "scripts":
        result = convert_scripts()
    elif args.command == "textures":
        paths = list(args.paths)
        for canvas in args.canvas:
            paths += references(convert_canvas(canvas.lower()))[0]
        for folder in args.folder:
            paths += folder_textures(folder)
        result = convert_textures(paths, args.force)
    elif args.command == "wanted":
        file = Path(args.file) if args.file else wanted_file()
        wanted = json.loads(file.read_text()) if file.is_file() else {}
        result = convert_textures(wanted.get("textures", []))
    elif args.command == "fonts":
        result = convert_fonts()
    elif args.command == "items":
        result = convert_items()
    elif args.command == "uistates":
        result = convert_uistates()
    elif args.command == "gamedata":
        result = convert_gamedata()
    elif args.command == "enums":
        result = convert_enums()
    elif args.command == "loader":
        result = convert_loader()
    elif args.command == "actionmaps":
        result = convert_actionmaps()
    elif args.command == "texture-index":
        result = convert_texture_index()
    elif args.command == "world":
        result = convert_world()
    elif args.command == "options":
        result = convert_options()
    elif args.command == "worldmap":
        result = convert_worldmap(args.force)
    elif args.command == "crafting":
        result = convert_crafting(args.force)
    elif args.command == "quests":
        result = convert_quests(args.force)
    else:
        result = convert_text()
    print(json.dumps(result, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
