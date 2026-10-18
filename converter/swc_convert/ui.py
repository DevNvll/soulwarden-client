"""The UI content: canvases and UI slices as data, the Lua scripts, the sprites, the fonts, and the texts.

Usage (tools/convert-ui.sh; output in SWC_CONTENT/ui, never in git):
  hud                     each part that the first HUD needs (stage 1.10): the canvases of HUD_CANVASES and the UI
                          slices that they spawn, their textures, all scripts, the fonts, and the English texts
  canvases [PATH...|--all]  canvases (.dynamicuicanvas) and UI slices (.dynamicslice) -> canvases/<path>.json
  scripts                 each Lua script of the game (lyshineui/ and scripts/) -> scripts/<path>.luac (unchanged bytes)
  textures PATH...|--canvas PATH...|--folder PREFIX   textures -> textures/<path>.png (+ <path>.sprite.json)
  wanted [FILE]           the textures that the client asked for (default SWC_WORK/ui/wanted.json)
  fonts                   the .font files -> fonts/fonts.json, the font effects -> fonts/effects.json, the font files
  text                    the English texts (localization/en-us/*.loc.xml) -> localization/en-us.json
  gamedata                the game data of the HUD (data tables): gamedata/abilities.json, gamedata/weapons.json
  enums                   the enum values of the client for the names that the scripts read -> gamedata/enums.json

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

from . import datasheet, dds, env, luabc, source, uienums
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
}
OTHER_COMPONENTS = {
    "68d62281-b360-4426-aaca-e8bde8bfeb3a": "UiCheckboxComponent", "c96b1eef-033a-479b-829b-ed3555d0f33a": "UiDraggableComponent",
    "e4b8ace0-fce7-42d8-9836-942f910168b4": "UiDropTargetComponent", "bfc4d5a3-2f7c-4079-b19c-c2a06e93f343": "UiDropdownComponent",
    "6982c200-4d32-43cc-a7f6-f54fa50fcff5": "UiDynamicScrollBoxComponent", "965cdda3-ca50-4938-9088-0cd476942146": "UiExitHoverEventComponent",
    "e7e9c0b0-f552-4008-e0b2-2aa78bc14034": "UiLayoutColumnFixedComponent", "661370c1-899a-4d56-364a-1d30f465469e": "UiLayoutRowFixedComponent",
    "ee5c91f4-3111-40d9-8549-fbd657fc326b": "UiMarkerComponent", "d7eb1706-75e8-4c94-a266-61913f42431b": "UiMarkupButtonComponent",
    "773a0f4b-f9f9-4b82-9951-bbd73a737de4": "UiRadioButtonComponent", "f89d5ef2-37c4-4ca7-9aa2-23dc3867ec9d": "UiRadioButtonGroupComponent",
    "6b283f90-3519-47da-a1dd-65a79ce119cf": "UiScrollBarComponent", "3ad4b591-438c-4e08-87b1-a686bcf49198": "UiScrollBarMouseWheelComponent",
    "2f539588-aeab-4341-a6a6-af645d129693": "UiScrollBoxComponent", "2913d76b-36a0-45e0-a104-33c668eb612d": "UiSliderComponent",
    "2cb3872b-d2b4-4ddb-b39a-97492310ae11": "UiTextInputComponent", "67dc06d3-1f16-4fab-b3f8-d8c0a3af4f61": "SequenceAgentComponent",
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
Values UniqueId RootElement CanvasSize DrawOrder IsPixelAligned TextureAtlases requiresMouse escapable replaceable
actionMapOverrides drawLayer ActionName HoverStartActionName HoverEndActionName PressedActionName ReleasedActionName
IsHandlingEvents Progress Vertical NumRepeats""".split()}
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


def read_button(c) -> dict:
    base = child(c, "BaseClass1")
    return {"action": string(c, "ActionName"), "hoverStart": string(base, "HoverStartActionName"),
            "hoverEnd": string(base, "HoverEndActionName"), "pressed": string(base, "PressedActionName"),
            "released": string(base, "ReleasedActionName"), "handlingEvents": flag(base, "IsHandlingEvents", True)}


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
        elif name == "progressBar":
            out[name] = {"progress": num(c, "Progress"), "vertical": flag(c, "Vertical")}
        elif name == "imageRepeater":
            out[name] = {"repeats": whole(c, "NumRepeats")}
        elif name == "canvas":
            atlases = child(c, "TextureAtlases")
            canvas = {"id": out["id"], "uniqueId": str(whole(c, "UniqueId")), "root": entity_id(child(c, "RootElement")),
                      "size": vec2(c, "CanvasSize", (1920.0, 1080.0)), "drawOrder": whole(c, "DrawOrder"),
                      "pixelAligned": flag(c, "IsPixelAligned", True),
                      "atlases": [string(child(a, "BaseClass1"), "AssetPath").lower() for a in (atlases.children if atlases else [])]}
        elif name == "javelinCanvas":
            maps = child(c, "actionMapOverrides")
            out["javelin"] = {"requiresMouse": flag(c, "requiresMouse"), "escapable": flag(c, "escapable"),
                              "replaceable": flag(c, "replaceable"), "drawLayer": string(c, "drawLayer"),
                              "actionMapOverrides": [text(m) for m in (maps.children if maps else [])]}
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
    return out_dir() / "canvases" / (path.rsplit(".", 1)[0] + ".json")


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
                     TierNumber, EquipAbility (the heart rune), and the Name and Description keys of the item
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
            for key, column in (("equipAbility", "EquipAbility"), ("name", "Name"), ("description", "Description")):
                if row.get(column) or weapon.get(column):
                    weapons[row["ItemID"].lower()][key] = row.get(column) or weapon.get(column)
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "abilities.json").write_text(json.dumps({"tables": tables, "abilities": abilities}, separators=(",", ":")))
    (target / "weapons.json").write_text(json.dumps(weapons, separators=(",", ":")))
    return {"ability tables": len(tables), "abilities": len(abilities), "weapons": len(weapons)}


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
    target = out_dir() / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "enums.json").write_text(json.dumps({
        "program": {"file": "Bin64/NewWorld.exe", "size": program.stat().st_size},
        "values": values, "absent": absent,
        "unknown": {n: c for n, c in unknown.items() if n not in absent},
    }, indent=0, sort_keys=True))
    return {"names": len(names), "values": len(values), "absent": len(absent), "unknown": len(unknown) - len(absent)}


# ------------------------------------------------------------------------------------------------------------------ main

def wanted_file() -> Path:
    return env.work_dir() / "ui" / "wanted.json"


def hud(force: bool) -> dict:
    canvases = convert_canvases(HUD_CANVASES)
    textures = set(canvases.pop("textures"))
    for folder in HUD_TEXTURE_FOLDERS:
        textures |= set(folder_textures(folder))
    result = {"canvases": canvases, "scripts": convert_scripts(), "fonts": convert_fonts(), "text": convert_text(),
              "gamedata": convert_gamedata(), "enums": convert_enums(), "textures": convert_textures(textures, force)}
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
    sub.add_parser("enums")
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
    elif args.command == "gamedata":
        result = convert_gamedata()
    elif args.command == "enums":
        result = convert_enums()
    else:
        result = convert_text()
    print(json.dumps(result, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
