"""The creatures of the backend: from the slice of a record to its model, its skeleton, its clips, and its fragments.

Usage (tools/convert-creatures.sh; output in SWC_CONTENT/creatures, never in git):
  census                      census.json: for each creature slice of the creature data of the backend copy its body
                              (.cdf), skeleton (.chr), controller, animation database, and default tags; the counts of
                              the bodies, skeletons, and clips, and an estimate of the disk.
  convert [--near X Y R] [--slices TEXT...] [--all] [--force]
                              the slices of the spawn points near a place (default: the start place 8880 4250, 300 m),
                              the slices whose path has a text, or each slice: the skeleton glTF files (skeleton, the
                              mesh of one body, the clips), the body glTF files (one for each other .cdf), and the
                              animation sets (fragments, variants, blend spaces, clips).
  wanted [FILE]               the slices that the client asked for (default SWC_WORK/creatures/wanted.json).

The chain (research feel-values.md R3.3.3; protocol notes 4.14):
  metadata state: the asset id of the slice -> the slice (creature data of the backend copy: path, action table, tags)
  slice: the SkinnedMeshComponent ("Skinned Mesh": the .cdf of the body) and the ActionList component
         (m_controllerDefinition, m_animationDatabase, m_defaultTags)
  .cdf: the skeleton (.chr of Model) and the skin attachments (CA_SKIN) with their materials
  .chrparams of the skeleton: the clip name -> file (a folder scan; the name is the file stem)
  controller: the fragment file (the wire id of a fragment is its position), the tag file (the 12 tag bytes)
  .adb (and its SubADBs): the variants of each fragment; the variant of the tag state of the slice
The clips play in place (the root bone has no track): the server moves a creature, also in its attacks (protocol notes
4.14, live check LC-Q1-1: the retail client plays a remote lunge in place).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import uuid
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

import numpy as np

from . import anim, cgf, datasheet, env, gltf, materials, source
from . import manifest as shared_manifest
from . import mesh as shared_mesh
from .character import mat_to_quat, norm, to_y_up

# The start place of the development character (protocol notes 4.14: the area of stage 3.4a).
START = (8880.0, 4250.0)
NEAR_RADIUS = 300.0
TAG_BYTES = 12
# The ActionList fields and the body field of a creature slice (the names of the slice dumps; the field id is the CRC-32 of
# the lower-case name).
F_SKINNED_MESH = source.field_id("Skinned Mesh")
F_ASSET_PATH = source.field_id("AssetPath")
F_CONTROLLER = source.field_id("m_controllerDefinition")
F_DATABASE = source.field_id("m_animationDatabase")
F_DEFAULT_TAGS = source.field_id("m_defaultTags")
F_SCOPE_DATA = source.field_id("m_scopeData")
F_WORLD_TM = source.field_id("m_worldTM")
ENTITY_TYPE = bytes.fromhex("75651658" "8663" "478d" "9090" "2432dfcafa44")
# The default items of the Paperdoll component of a slice (research creature-visuals.md section 4): slot 24 the main hand,
# slot 27 the off hand; the item id is a WeaponID of the weapon tables.
F_SLOT_TYPE = source.field_id("m_slotType")
F_ITEM_NAME = source.field_id("m_itemName")
MAIN_HAND, OFF_HAND = 24, 27
EQUIP_TYPES = "sharedassets/genericassets/items/equiptypesdatabase.equipdb"
# The hit volumes of a slice (the entities Head, Leg_F_L, ... of the slice; component HitVolume): the volume of a melee contact.
# Its facet target is the target of a contact item (protocol notes 4.7, 4.15; the id rule of 4.2).
HIT_VOLUME_TYPE = bytes.fromhex("6d3e998c" "fc19" "4c9d" "b8f6" "77c3fe985d29")
HIT_SHAPES = {bytes.fromhex("7495c65c" "9193" "4193" "bbb2" "de3343b9eb03"): "capsule",
              bytes.fromhex("7f2ef312" "4089" "4582" "89c5" "5d4156daa7fb"): "sphere",
              bytes.fromhex("c6651a66" "23d4" "4508" "b4ad" "180c516655a8"): "box"}
F_COMPONENT_ID = 0xBF396750
# The CameraLockTargetComponent of a creature slice (the lock point of the camera) and its client facet.
LOCK_TARGET_TYPE = bytes.fromhex("b7f335ff" "5b90" "4055" "8116" "2845e33d4048")
LOCK_FACET_TYPE = bytes.fromhex("abc07432" "10aa" "46ca" "af3e" "8b0667799bf6")
# AZ::Entity and its field Id (the entity id of the slice file; the backend tool slice_ids.py reads the same field).
ENTITY_TYPE = bytes.fromhex("75651658" "8663" "478d" "9090" "2432dfcafa44")
F_ID = source.field_id("Id")
# The four sockets of an equip type (field ids of equiptypesdatabase.equipdb; the names of the fields are not known): the
# main hand drawn, the main hand sheathed, the off hand drawn, the off hand sheathed; each with a position and a rotation
# (three f32 each). AlwaysUnsheathed_RHand: righthandattach, righthandattach, lefthandattach, lefthandattach.
EQUIP_SOCKETS = {"main": (0x795FD9BB, 0x169D0BA2, 0x22A1A564), "mainSheathed": (0x7534DF20, 0x24F32EBE, 0x2DC8051C),
                 "off": (0x8E6530D2, 0x9660EB28, 0x05C8562E), "offSheathed": (0x41AD3C35, 0x403659A7, 0x2F0E2087)}
# The client facet of the MarkerComponent: the rule of the name plate (wolf: near range 6 m, near angle 0.1309 rad, far angle
# 0.01745 rad, angle shrink distance 20 m; protocol notes 4.14: the plate shows at 6 m with no crosshair on the creature).
MARKER_FIELDS = {"nearRange": "m_nearRange", "nearAngle": "m_nearAngle", "farAngle": "m_farAngle",
                 "angleShrinkDistance": "m_angleShrinkDistance", "verticalOffset": "m_verticalOffset"}
MARKER_FLAGS = {"neverShowHealthBar": "m_neverShowHealthBar", "forceDisplayHealthBar": "m_forceDisplayHealthBar"}


def creature_dir() -> Path:
    return env.content_dir() / "creatures"


def gamedata_dir() -> Path:
    value = os.environ.get("SWC_GAMEDATA_DIR")
    return Path(value) if value else env.work_dir() / "backend/current/gamedata"


def backend_data() -> dict:
    """@return The creature data of the backend copy (creatures.json, made by the Soulwarden tool creatures.sh)."""
    return json.loads((gamedata_dir() / "creatures.json").read_text())


def file_stem(prefix: str, text: str) -> str:
    """@return The short file stem of a converted file (an Unreal asset name): Interchange shortens a long asset path (the
    budget of the path; a kite shield became wep_mel_shld_1h_ki_devilknight_46df73), thus the names of the creature assets
    are a letter and 8 hexadecimal digits of the hash of their key."""
    return f"{prefix}_{hashlib.sha1(text.encode()).hexdigest()[:8]}"


def clip_asset_name(clip: str) -> str:
    """@return The glTF animation name of a clip: Interchange names its sequence STEM + this name (a short name)."""
    return "_" + hashlib.sha1(clip.encode()).hexdigest()[:8]


def short_key(text: str) -> str:
    """@return A short name: the stem of a path and a hash (the folder rule of the manifest module)."""
    stem = re.sub(r"[^A-Za-z0-9_]", "_", Path(text.split("|")[0]).stem)[:40]
    return f"{stem}_{hashlib.sha1(text.encode()).hexdigest()[:6]}"


# ----------------------------------------------------------------------------------------------------------- the slice

def asset_path_of(element) -> str:
    for item in source.walk(element):
        if item.name == F_ASSET_PATH and item.value:
            return norm(item.value.decode("utf-8", "replace"))
    return ""


def read_slice(path: str) -> dict:
    """@return The body and the animation fields of one creature slice."""
    roots = source.parse(source.read(path))
    body, controller, database, tags, scopes, scale = "", "", "", [], [], 1.0
    marker: dict = {}
    items: list[dict] = []
    marker_ids = {source.field_id(name): key for key, name in MARKER_FIELDS.items()}
    flag_ids = {source.field_id(name): key for key, name in MARKER_FLAGS.items()}
    hit_volumes = read_hit_volumes(roots[0])
    lock: dict = {}
    for element in source.walk(roots[0]):
        if element.type_id == HIT_VOLUME_TYPE:
            continue
        if element.type_id == LOCK_TARGET_TYPE and not lock:
            lock = read_lock_target(element)
        slot, item = element.child(F_SLOT_TYPE), element.child(F_ITEM_NAME)
        if slot is not None and item is not None and source.text(item):
            items.append({"slot": source.number(slot), "item": source.text(item)})
        if element.name in marker_ids and element.value is not None and len(element.value) == 4:
            marker.setdefault(marker_ids[element.name], round(source.real(element), 5))
        elif element.name in flag_ids and element.value:
            marker.setdefault(flag_ids[element.name], element.value[0] != 0)
        if element.name == F_SKINNED_MESH and not body:
            asset = source.asset_id(element)
            if asset is not None:
                body = source.product(asset[0], asset[1], ".cdf") or source.product(asset[0], None, ".cdf") or ""
        elif element.name == F_CONTROLLER and not controller:
            controller = asset_path_of(element)
        elif element.name == F_DATABASE and not database:
            database = asset_path_of(element)
        elif element.name == F_DEFAULT_TAGS and not tags:
            tags = [source.text(child) for child in element.children if child.value]
        elif element.name == F_SCOPE_DATA and not scopes:
            for entry in element.children:
                names = [source.text(item) for item in source.walk(entry) if item.value and item is not entry
                         and item.name == source.field_id("ScopeName")]
                scopes.append({"scope": names[0] if names else "", "database": asset_path_of(entry)})
    return {"body": norm(body), "controller": controller, "database": database, "tags": tags, "scopeDatabases": scopes,
            "scale": scale, "marker": marker, "items": items, "hitVolumes": hit_volumes, "lock": lock}


def vector_of(element) -> list[float]:
    return [round(v, 5) for v in struct.unpack(">3f", element.value)] if element is not None and element.value and len(element.value) == 12 else []


def read_lock_target(element) -> dict:
    """@return The lock point of the camera of a creature slice (its CameraLockTargetComponent): m_Offset (engine space, m, from the
    root of the creature), the joint of the lock icon and its vertical offset (m), and from the client facet the joint and fixed
    offset flags and the pitch limits of the observer (degrees)."""
    out: dict = {}
    offset = vector_of(element.child(source.field_id("m_Offset")))
    if offset:
        out["offset"] = offset
    joint = element.child(source.field_id("m_targetLockIconJoint"))
    if joint is not None:
        out["joint"] = source.text(joint)
    joint_offset = element.child(source.field_id("m_targetLockIconJointVerticalOffset"))
    if joint_offset is not None and joint_offset.value and len(joint_offset.value) == 4:
        out["jointOffset"] = round(source.real(joint_offset), 5)
    for item in source.walk(element):
        if item.type_id != LOCK_FACET_TYPE:
            continue
        for name, key in (("m_forceJointOffset", "forceJoint"), ("m_forceFixedOffset", "forceFixed"), ("m_limitObserverPitch", "limitPitch")):
            flag = item.child(source.field_id(name))
            if flag is not None and flag.value:
                out[key] = flag.value[0] != 0
        for name, key in (("m_observerPitchMin", "pitchMin"), ("m_observerPitchMax", "pitchMax")):
            value = item.child(source.field_id(name))
            if value is not None and value.value and len(value.value) == 4:
                out[key] = round(source.real(value), 5)
        break
    return out


def read_hit_volumes(root) -> list[dict]:
    """@return The hit volumes of a slice, each with the id of its slice entity (hexadecimal). Two entities can have the same
    component ids (the left and the right leg of the werewolf), thus a volume is the pair of its entity and its component; the
    data patches of a nested slice repeat a pair: the first one is the volume of the entity."""
    volumes: list[dict] = []
    seen: set[tuple[str, str]] = set()
    stack = [(root, None)]
    while stack:
        element, entity = stack.pop()
        if element.type_id == ENTITY_TYPE:
            for child in element.children:
                if child.name == F_ID:
                    value = next((c.value for c in [child, *child.children] if c.value is not None and len(c.value) == 8), None)
                    entity = f"{int.from_bytes(value, 'big'):016X}" if value is not None else None
        if element.type_id == HIT_VOLUME_TYPE:
            volume = read_hit_volume(element)
            if volume is not None and entity is not None and (entity, volume["component"]) not in seen:
                seen.add((entity, volume["component"]))
                volumes.append(dict(volume, entity=entity))
            continue
        # The children in their order (a stack: the last child first).
        stack.extend((child, entity) for child in reversed(element.children))
    return volumes


def read_hit_volume(element) -> dict | None:
    """@return One hit volume: its component id (hexadecimal), name, bone, center (m, in the space of the bone), shape
    (capsule: radius, height with the ends, axis; sphere: radius; box: half sizes), and the head and leg marks."""
    component = None
    for item in source.walk(element):
        if item.name == F_COMPONENT_ID and item.value is not None and len(item.value) == 8:
            component = item.value.hex().upper()
            break
    shape_field = element.child(source.field_id("m_shape"))
    shapes = shape_field.children if shape_field is not None else []
    if component is None or not shapes or shapes[0].type_id not in HIT_SHAPES:
        return None
    shape = shapes[0]
    out = {"component": component, "name": source.text(element.child(source.field_id("m_volumeName"))),
           "bone": source.text(element.child(source.field_id("m_targetBoneName"))),
           "center": vector_of(element.child(source.field_id("m_center"))) or [0.0, 0.0, 0.0],
           "shape": HIT_SHAPES[shape.type_id]}
    radius = shape.child(source.field_id("m_Radius"))
    if radius is not None:
        out["radius"] = round(source.real(radius), 5)
    height = shape.child(source.field_id("m_Height"))
    if height is not None:
        out["height"] = round(source.real(height), 5)
    axis = vector_of(shape.child(source.field_id("m_axis")))
    if axis:
        out["axis"] = axis
    box = vector_of(shape.child(source.field_id("m_Box")))
    if box:
        out["box"] = box
    for key in ("m_isHeadshot", "m_isLegshot"):
        flag = element.child(source.field_id(key))
        if flag is not None and flag.value:
            out[key[4:].lower()] = flag.value[0] != 0
    mult = element.child(source.field_id("m_damageMult"))
    if mult is not None and mult.value and len(mult.value) == 4:
        out["damageMult"] = round(source.real(mult), 4)
    return out


def floats(text: str | None) -> list[float]:
    return [number_of(v) for v in (text or "").split(",")] if text else []


def read_cdf(path: str) -> dict:
    """@return The skeleton, the skin attachments, the bone sockets (CA_BONE: a socket with no binding, or a bound object),
    and the count of the other attachment records (cloth) of a character definition."""
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    model = root.find("Model")
    skins, sockets, others = [], {}, {}
    for attachment in root.iter("Attachment"):
        kind = attachment.get("Type", "")
        if kind == "CA_SKIN" and attachment.get("Binding"):
            skins.append({"name": attachment.get("AName", ""), "skin": norm(attachment.get("Binding")),
                          "material": norm(attachment.get("Material") or model.get("Material") or "")})
        elif kind == "CA_BONE":
            # RelRotation is (w, x, y, z), RelPosition (x, y, z) in meters, relative to the bone in its default pose.
            sockets[attachment.get("AName", "").lower()] = {"bone": attachment.get("BoneName", ""),
                                                            "rotation": floats(attachment.get("RelRotation")) or [1.0, 0.0, 0.0, 0.0],
                                                            "position": floats(attachment.get("RelPosition")) or [0.0, 0.0, 0.0],
                                                            "binding": norm(attachment.get("Binding") or "")}
        else:
            others[kind] = others.get(kind, 0) + 1
    return {"skeleton": norm(model.get("File", "")), "material": norm(model.get("Material") or ""), "skins": skins,
            "sockets": sockets, "otherAttachments": others}


# ------------------------------------------------------------------------------------------------- clips of a skeleton

_entries_by_folder: dict[str, list[str]] = {}


def files_below(folder: str, ending: str) -> list[str]:
    key = folder + "|" + ending
    if key not in _entries_by_folder:
        prefix = folder.rstrip("/") + "/"
        _entries_by_folder[key] = sorted(p for p in source.paks().entries if p.startswith(prefix) and p.endswith(ending))
    return _entries_by_folder[key]


def clip_list(skeleton: str, depth: int = 0) -> dict[str, str]:
    """@return The clip names of a skeleton and their files (.caf, .bspace, .comb): the animation list of its .chrparams
    ($Include files, #filepath folders, and the patterns "*/*.ENDING": each file below the folder, the name is the file
    stem; the first entry of a name wins; research feel-values.md R3.2.8)."""
    path = skeleton.rsplit(".", 1)[0] + ".chrparams"
    names: dict[str, str] = {}
    if depth > 4 or not source.has(path):
        return names
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    folder = ""
    listing = root.find("AnimationList")
    for item in (listing if listing is not None else []):
        name, value = item.get("name", ""), norm(item.get("path", ""))
        if name == "#filepath":
            folder = value.rstrip("/")
        elif name == "$Include":
            for key, file in clip_list(value, depth + 1).items():
                names.setdefault(key, file)
        elif name.startswith("$"):
            continue
        elif "*" in name or "*" in value:
            ending = value.rsplit(".", 1)[-1] if "." in value else "caf"
            for file in files_below(folder, "." + ending):
                names.setdefault(Path(file).stem, file)
        elif value:
            full = value if "/" in value and source.has(value) else f"{folder}/{value}"
            names.setdefault(name.lower(), full)
    return names


# ------------------------------------------------------------------------------------------------------- the controller

def read_xml(path: str) -> ET.Element | None:
    path = norm(path)
    if not path or not source.has(path):
        return None
    return ET.fromstring(source.read(path).decode("utf-8", "replace"))


def tag_names(path: str, depth: int = 0, seen: set | None = None) -> list[str]:
    """@return The names of a tag definition file in their order, the imported files first, a name one time (the wire id
    of a fragment is its position in the fragment file; protocol notes 4.14)."""
    seen = set() if seen is None else seen
    root = read_xml(path)
    names: list[str] = []
    if root is None or depth > 8:
        return names
    for imported in root.iter("Import"):
        names += tag_names(imported.get("filename", ""), depth + 1, seen)
    tags = root.find("Tags")
    for tag in (tags.iter("Tag") if tags is not None else []):
        name = tag.get("name", "")
        if name and name.lower() not in seen:
            seen.add(name.lower())
            names.append(name)
    return names


def tag_layout(path: str, depth: int = 0) -> tuple[list[tuple[str, list[str]]], list[str]]:
    """@return The groups (name, tags) and the single tags of a tag definition file, the imported files first."""
    groups: list[tuple[str, list[str]]] = []
    single: list[str] = []
    seen: set[str] = set()

    def read(file: str, level: int) -> None:
        root = read_xml(file)
        if root is None or level > 8:
            return
        for imported in root.iter("Import"):
            read(imported.get("filename", ""), level + 1)
        tags = root.find("Tags")
        for entry in (tags if tags is not None else []):
            if entry.tag == "Tag":
                name = entry.get("name", "")
                if name and name.lower() not in seen:
                    seen.add(name.lower())
                    single.append(name)
            else:
                names = []
                for tag in entry.iter("Tag"):
                    name = tag.get("name", "")
                    if name and name.lower() not in seen:
                        seen.add(name.lower())
                        names.append(name)
                if names:
                    groups.append((entry.get("name", ""), names))

    read(path, depth)
    return groups, single


def tag_masks(path: str) -> dict[str, tuple[int, int]]:
    """@return For each tag (lower case): its byte and its value in the 12 tag bytes (globalFragTags). The rule of the client
    (code 0x6D87130, protocol notes 4.17): byte by byte, each group that is not placed yet and fits gets bit_length(count)
    bits (value 0: no tag of the group, tag K: value K from 1), then one bit for each single tag."""
    groups, single = tag_layout(path)
    masks: dict[str, tuple[int, int]] = {}
    placed_groups: set[int] = set()
    placed_single: set[int] = set()
    for byte in range(TAG_BYTES):
        used = 0
        for index, (_name, names) in enumerate(groups):
            bits = len(names).bit_length()
            if index in placed_groups or used + bits > 8:
                continue
            placed_groups.add(index)
            for number, name in enumerate(names, start=1):
                masks[name.lower()] = (byte, (number << used) | (((1 << bits) - 1) << (used + 8)))
            used += bits
        for index, name in enumerate(single):
            if index in placed_single or used >= 8:
                continue
            placed_single.add(index)
            masks[name.lower()] = (byte, (1 << used) | ((1 << used) << 8))
            used += 1
    return masks


def decode_tags(path: str, hex_bytes: str) -> list[str]:
    """@return The tag names of 12 tag bytes (24 hexadecimal digits)."""
    data = bytes.fromhex(hex_bytes) if hex_bytes else bytes(TAG_BYTES)
    out = []
    for name, (byte, packed) in tag_masks(path).items():
        value, field = packed & 0xFF, packed >> 8
        if byte < len(data) and (data[byte] & field) == value:
            out.append(name)
    return out


def read_controller(path: str) -> dict:
    root = read_xml(path)
    if root is None:
        return {"fragments": [], "tags": "", "scopes": []}
    fragments_file = root.find("Fragments")
    tags_file = root.find("Tags")
    scopes = [scope.tag for scope in (root.find("ScopeDefs") if root.find("ScopeDefs") is not None else [])]
    definitions = {}
    for definition in (root.find("FragmentDefs") if root.find("FragmentDefs") is not None else []):
        definitions[definition.tag.lower()] = {"scopes": (definition.get("scopes") or "").split("+"),
                                               "flags": (definition.get("flags") or "").split("+")}
    return {"fragments": tag_names(fragments_file.get("filename", "")) if fragments_file is not None else [],
            "tags": norm(tags_file.get("filename", "")) if tags_file is not None else "", "scopes": scopes,
            "definitions": definitions}


# --------------------------------------------------------------------------------------------- the animation database

def number_of(text: str | None, default: float = 0.0) -> float:
    try:
        value = float(text) if text not in (None, "") else default
    except ValueError:
        return default
    return value if math.isfinite(value) else default


def split_tags(text: str) -> set[str]:
    return {name.strip().lower() for name in (text or "").replace(",", "+").split("+") if name.strip()}


def read_layer(layer: ET.Element) -> list[dict]:
    """@return The items of one AnimLayer: each Blend with its Animation. The start of an item is the sum of the ExitTime
    values before it; ExitTime -1 starts it at the end of the clip before it; an empty animation is the end of the layer
    (research feel-values.md R3.2.5)."""
    items = []
    time = 0.0
    blend: dict = {}
    for element in layer:
        if element.tag == "Blend":
            blend = dict(element.attrib)
            step = number_of(blend.get("ExitTime"))
            if step > 0.0:
                time += step
        elif element.tag == "Animation":
            items.append({"clip": (element.get("name") or "").lower(), "at": round(time, 5),
                          "afterPrevious": number_of(blend.get("ExitTime")) < 0.0,
                          "clipStart": round(number_of(blend.get("StartTime")), 5),
                          "blendIn": round(number_of(blend.get("Duration")), 5),
                          "speed": number_of(element.get("speed"), 1.0) or 1.0,
                          "weight": number_of(element.get("weight"), 1.0),
                          "loop": "Loop" in (element.get("flags") or "").split("+"),
                          "timeWarp": "TimeWarping" in (blend.get("flags") or "")})
    return items


def named_marks(variant: ET.Element) -> list[list]:
    """@return The CAGE-Named windows of the procedural layers: [name, start, end] in seconds of the fragment."""
    marks = []
    for layer in variant.findall("ProcLayer"):
        time = 0.0
        open_item = None
        for element in layer:
            if element.tag == "Blend":
                step = number_of(element.get("ExitTime"))
                if step > 0.0:
                    time += step
                if open_item is not None:
                    open_item[2] = round(time, 5)
                    open_item = None
            elif element.tag == "Procedural" and element.get("type") == "CAGE-Named":
                name = element.find("ProceduralParams/Name")
                if name is not None and name.get("value"):
                    open_item = [name.get("value"), round(time, 5), -1.0]
                    marks.append(open_item)
    return marks


def read_database(path: str, extra: frozenset = frozenset(), depth: int = 0, out: dict | None = None) -> dict[str, list[dict]]:
    """@return The variants of each fragment of one database and its SubADBs (lower-case fragment name to a list in the
    order of the files; a SubADB adds its tags to its variants)."""
    out = {} if out is None else out
    root = read_xml(path)
    if root is None or depth > 8:
        return out
    listed = root.find("FragmentList")
    for fragment in (listed if listed is not None else []):
        for variant in fragment.findall("Fragment"):
            layers = [read_layer(layer) for layer in variant.findall("AnimLayer")]
            out.setdefault(fragment.tag.lower(), []).append({
                "tags": sorted(split_tags(variant.get("Tags", "")) | extra), "fragTags": sorted(split_tags(variant.get("FragTags", ""))),
                "blendOut": round(number_of(variant.get("BlendOutDuration"), 0.2), 5), "layers": layers,
                "marks": named_marks(variant), "file": norm(path)})
    for sub in root.iter("SubADB"):
        tags = split_tags(sub.get("Tags", ""))
        read_database(sub.get("File", ""), extra | frozenset(tags), depth + 1, out)
    return out


def select_variant(variants: list[dict], tags: set[str]) -> tuple[dict | None, int]:
    """@return The variant of a tag state and its index: the variant with the most tags whose tags are all in the state, the
    first one of the files at a tie (the rule of the Soulwarden tools; research feel-values.md R3.2.5, open point O1). A
    variant with FragTags is not taken (the backend sends no FragTags)."""
    best, best_index = None, -1
    for index, variant in enumerate(variants):
        own = set(variant["tags"])
        if variant["fragTags"] or not own <= tags:
            continue
        if best is None or len(own) > len(best["tags"]):
            best, best_index = variant, index
    return best, best_index


def read_blend_space(path: str) -> dict:
    """@return The dimensions, the examples, and the precomputed grid of a .bspace (research feel-values.md R3.2.7)."""
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    dimensions = root.find("Dimensions")
    params = [{"name": p.get("Name") or p.get("name"), "min": number_of(p.get("Min") or p.get("min")),
               "max": number_of(p.get("Max") or p.get("max")), "cells": int(number_of(p.get("Cells") or p.get("cells"), 1)),
               "locked": p.get("locked") == "1"} for p in (dimensions if dimensions is not None else [])]
    examples = []
    for example in (root.find("ExampleList") if root.find("ExampleList") is not None else []):
        entry = {"clip": (example.get("AName") or "").lower(), "scale": number_of(example.get("PlaybackScale"), 1.0)}
        for index in range(3):
            if example.get(f"SetPara{index}") is not None:
                entry[f"p{index}"] = number_of(example.get(f"SetPara{index}"))
        examples.append(entry)
    grid = []
    vgrid = root.find("VGrid")
    for cell in (vgrid if vgrid is not None else []):
        grid.append([int(number_of(cell.get(f"i{n}"))) for n in range(4)] + [number_of(cell.get(f"w{n}")) for n in range(4)])
    return {"path": path, "params": params, "examples": examples, "grid": grid}


def read_comb(path: str) -> dict:
    """@return A combined blend space: the parameter that selects one of its blend spaces, and their paths."""
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    dimensions = root.find("Dimensions")
    params = [{"name": p.get("name") or p.get("Name"), "min": number_of(p.get("min") or p.get("Min")),
               "max": number_of(p.get("max") or p.get("Max")), "choose": p.get("ChooseBlendSpace") == "1",
               "locked": p.get("locked") == "1"} for p in (dimensions if dimensions is not None else [])]
    spaces = [norm(b.get("AName", "")) for b in (root.find("BlendSpaces") if root.find("BlendSpaces") is not None else [])]
    return {"path": path, "params": params, "spaces": spaces}


# --------------------------------------------------------------------------------------------------------- the census

def census() -> dict:
    """Writes census.json. @return The counts."""
    data = backend_data()
    slices, bodies, skeletons, databases, failures = [], {}, {}, {}, {}
    for entry in data["slices"]:
        try:
            info = read_slice(entry["path"])
        except Exception as error:   # a slice that does not read: the client shows no body for it
            failures[entry["path"]] = str(error)[:160]
            continue
        record = {"path": entry["path"], "asset": entry["asset"].upper(), "sub": entry["sub"], **info,
                  "backendTags": entry.get("tags", "")}
        if info["body"] and source.has(info["body"]):
            if info["body"] not in bodies:
                try:
                    bodies[info["body"]] = read_cdf(info["body"])
                except Exception as error:
                    failures[info["body"]] = str(error)[:160]
            if info["body"] in bodies:
                record["skeleton"] = bodies[info["body"]]["skeleton"]
                skeletons.setdefault(record["skeleton"], set()).add(info["body"])
        databases.setdefault(info["database"], 0)
        databases[info["database"]] += 1
        slices.append(record)
    # The kinds and the spawn points of each slice.
    kinds_of_slice: dict[int, int] = {}
    points_of_slice: dict[int, int] = {}
    for kind in data["kinds"]:
        for body, _percent in kind.get("bodies", []):
            kinds_of_slice[body] = kinds_of_slice.get(body, 0) + 1
    for point in data["spawnPoints"]:
        for body, _percent in data["kinds"][point[1]].get("bodies", []):
            points_of_slice[body] = points_of_slice.get(body, 0) + 1
    for index, record in enumerate(slices):
        record["kinds"] = kinds_of_slice.get(index, 0)
        record["spawnPoints"] = points_of_slice.get(index, 0)
    skin_bytes = 0
    skins = {s["skin"] for b in bodies.values() for s in b["skins"]}
    for skin in skins:
        try:
            skin_bytes += len(source.read(skin))
        except Exception:
            pass
    doc = {"format": 1, "slices": slices, "bodies": bodies, "skeletons": {k: sorted(v) for k, v in skeletons.items()},
           "databases": databases, "failures": failures}
    out = creature_dir() / "census.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"census": str(out), "slices": len(slices), "with a body": sum(1 for s in slices if s.get("skeleton")),
            "bodies (.cdf)": len(bodies), "skeletons (.chr)": len(skeletons), "animation databases": len(databases),
            "skins": len(skins), "skin bytes in the paks (MB)": round(skin_bytes / 1e6, 1), "failures": len(failures),
            "kinds": len(data["kinds"]), "spawn points": len(data["spawnPoints"])}


# ------------------------------------------------------------------------------------------------- the animation set

T_MOTION = 0x3002


def clip_info(path: str, bones_by_id: dict | None = None) -> dict:
    """@return The length, the move speed, the additive bit (chunk 0x3002: flags bit 0 is additive, research feel-values.md
    R3.2.9, open point O4; an additive clip has the identity rotation in its first frame: its keys are differences), and the
    bones of the skeleton that the clip animates (not the root: the clips play in place). A layer after the first one of a
    variant changes only these bones."""
    chunks = cgf.read_chunks(source.read(path))
    clip = anim.read_clip(chunks)
    motion = [c for c in chunks.chunks if c.type == T_MOTION][0].data
    flags = struct.unpack_from("<I", motion, 0)[0]
    info = {"path": path, "duration": round((clip.end - clip.start) * clip.secs_per_frame, 5),
            "fps": round(1.0 / clip.secs_per_frame, 3) if clip.secs_per_frame else 30.0,
            "moveSpeed": round(clip.move_speed, 4), "additive": bool(flags & 1)}
    if bones_by_id is not None:
        info["bones"] = sorted(bones_by_id[cid].name for cid, c in clip.controllers.items()
                               if cid in bones_by_id and bones_by_id[cid].parent >= 0 and (len(c.rotations) or len(c.positions)))
    return info


def fragment_length(items: list[dict], clips: dict[str, dict]) -> float:
    """@return The length of one layer of a variant in seconds: its end blend (the blend to an empty animation), else the end
    of its last clip (the rule of the Soulwarden tools weapon_actions.py and creature_behavior.py); -1: the last clip loops
    or a length is not known."""
    for item in items:
        if not item["clip"] and item["at"] > 0.0:
            return item["at"]
    end, previous, animation_time, previous_start, previous_speed, shift = 0.0, None, 0.0, 0.0, 1.0, 0.0
    for index, item in enumerate(items):
        if not item["clip"]:
            break
        info = clips.get(item["clip"])
        if info is None or "duration" not in info or item["speed"] <= 0.0:
            return -1.0
        if item["loop"] and index == len(items) - 1:
            return -1.0
        if item["afterPrevious"] and index > 0:
            shift = end - item["at"]
        start = item["at"] + shift
        if item["clip"] == previous:
            animation_time += (start - previous_start) * previous_speed
        else:
            animation_time = item["clipStart"]
        end = start + max(info["duration"] - animation_time, 0.0) / item["speed"]
        previous, previous_start, previous_speed = item["clip"], start, item["speed"]
    return round(end, 5) if end > 0.0 else -1.0


class ClipResolver:
    """The clips of one skeleton: a name of a fragment or a blend space -> its file, read once."""

    def __init__(self, skeleton: str) -> None:
        self.names = clip_list(skeleton)
        self.bones_by_id = {b.controller_id: b for b in cgf.read_bones(cgf.read_chunks(source.read(skeleton)))}
        self.clips: dict[str, dict] = {}
        self.spaces: dict[str, dict] = {}
        self.combs: dict[str, dict] = {}
        self.missing: set[str] = set()

    def add(self, name: str) -> str:
        """Adds a name (a clip, a blend space, or a combination). @return Its kind: clip, blendSpace, comb, or missing."""
        if not name:
            return "end"
        if name in self.clips:
            return "clip"
        if name in self.spaces:
            return "blendSpace"
        if name in self.combs:
            return "comb"
        path = self.names.get(name)
        if path is None:
            self.missing.add(name)
            return "missing"
        try:
            if path.endswith(".caf"):
                self.clips[name] = clip_info(path, self.bones_by_id)
                return "clip"
            if path.endswith(".bspace"):
                space = read_blend_space(path)
                self.spaces[name] = space
                for example in space["examples"]:
                    self.add(example["clip"])
                return "blendSpace"
            if path.endswith(".comb"):
                comb = read_comb(path)
                self.combs[name] = comb
                comb["names"] = []
                for space_path in comb["spaces"]:
                    stem = Path(space_path).stem
                    if stem not in self.names:
                        self.names[stem] = space_path
                    comb["names"].append(stem)
                    self.add(stem)
                return "comb"
        except Exception:   # a file that does not read: the fragment has no clip
            self.missing.add(name)
        return "missing"


def wanted_fragments(backend_slice: dict, fragments: list[str]) -> set[int]:
    """@return The fragment ids that the backend can send for a slice: the fragments of its action table, of its reaction
    table and its additive reactions, and the idle of the client (fragment Idle)."""
    ids = {action[2] for action in backend_slice.get("actions", []) if len(action) > 2 and action[2] is not None and action[2] >= 0}
    for row in (backend_slice.get("reactionTable") or {}).get("rows", []):
        if len(row) > 2 and isinstance(row[2], int) and row[2] >= 0:
            ids.add(row[2])
    for row in (backend_slice.get("additive") or {}).get("rows", []):
        if len(row) > 2 and isinstance(row[2], int) and row[2] >= 0:
            ids.add(row[2])
    lower = [name.lower() for name in fragments]
    for name in ("idle", "move", "move_combat", "combat_idle"):
        if name in lower:
            ids.add(lower.index(name))
    return {i for i in ids if i < len(fragments)}


def animation_set(record: dict, backend_slice: dict, resolver: ClipResolver) -> dict:
    """@return The fragments of one slice: for each fragment id the variant of its tag state with its layers, its length,
    and its blend-out time (the clips and blend spaces go to the resolver of its skeleton)."""
    controller = read_controller(record["controller"])
    state = {tag.lower() for tag in record["tags"]}
    wire_tags = backend_slice.get("tags", "")
    if controller["tags"]:
        state |= set(decode_tags(controller["tags"], wire_tags))
    database = read_database(record["database"])
    fragments = {}
    for fragment_id in sorted(wanted_fragments(backend_slice, controller["fragments"])):
        name = controller["fragments"][fragment_id]
        variant, index = select_variant(database.get(name.lower(), []), state)
        if variant is None or not variant["layers"]:
            fragments[str(fragment_id)] = {"name": name, "variant": -1}
            continue
        layers = []
        for layer in variant["layers"]:
            items = []
            for item in layer:
                entry = dict(item)
                entry["kind"] = resolver.add(item["clip"])
                items.append(entry)
            layers.append(items)
        items = layers[0]
        definition = controller["definitions"].get(name.lower(), {})
        fragments[str(fragment_id)] = {"name": name, "variant": index, "tags": variant["tags"], "blendOut": variant["blendOut"],
                                       "items": items, "layers": layers[1:], "marks": variant["marks"],
                                       "scopes": definition.get("scopes", []),
                                       "flags": definition.get("flags", [])}
    return {"controller": record["controller"], "database": record["database"], "tags": sorted(state), "wireTags": wire_tags,
            "fragmentNames": controller["fragments"], "fragments": fragments, "scopeNames": controller["scopes"]}


# ---------------------------------------------------------------------------------------------------------- the glTF

class Skeleton:
    """The bones of one creature skeleton (.chr) as glTF nodes with one skin (the layout of player.Skeleton)."""

    def __init__(self, path: str) -> None:
        self.path = path
        self.bones = cgf.read_bones(cgf.read_chunks(source.read(path)))
        self.index_of = {b.name: b.index for b in self.bones}

    def write(self, w: gltf.Writer) -> tuple[int, list[int]]:
        first = len(w.doc["nodes"])
        for b in self.bones:
            local = (np.linalg.inv(self.bones[b.parent].b2w) @ b.b2w) if b.parent >= 0 else b.b2w
            m = to_y_up(local)
            w.node(name=b.name, translation=[float(v) for v in m[:3, 3]], rotation=mat_to_quat(m))
        for b in self.bones:
            kids = [first + c.index for c in self.bones if c.parent == b.index]
            if kids:
                w.doc["nodes"][first + b.index]["children"] = kids
        roots = [first + b.index for b in self.bones if b.parent < 0]
        ibm = np.stack([to_y_up(b.w2b).T for b in self.bones]).astype(np.float32)
        w.doc["skins"] = [{"joints": [first + b.index for b in self.bones], "skeleton": roots[0],
                           "inverseBindMatrices": w.accessor(ibm.reshape(len(self.bones), 16), "MAT4", target=None),
                           "name": Path(self.path).stem}]
        return first, roots

    def add_body(self, w: gltf.Writer, body: dict, out: Path, name: str, max_texture: int) -> dict:
        """Adds each skin attachment of a character definition as the primitives of one mesh node of the skin 0 (one
        skeletal mesh asset for each body)."""
        prims, report = [], []
        for attachment in body["skins"]:
            f = cgf.read_chunks(source.read(attachment["skin"]))
            mesh = cgf.read_mesh(f, f.of_type(cgf.T_MESH)[0])
            remap = np.array([self.index_of.get(b.name, 0) for b in cgf.read_bones(f)], dtype=np.uint16)
            joints = remap[mesh.joints]
            subs = []
            if attachment["material"]:
                try:
                    subs = materials.load_mtl(attachment["material"], attachment["skin"])[1]
                except Exception:
                    subs = []
            pos = gltf.z_up_to_y_up(mesh.positions)
            nrm = gltf.z_up_to_y_up(mesh.normals) if mesh.normals is not None else None
            for s in mesh.subsets:
                mi = None
                if s.material < len(subs):
                    if materials.not_drawn(subs[s.material]):
                        continue
                    info = materials.describe(subs[s.material], max_texture)
                    files = info["files"]
                    mi = w.material(info["name"] or attachment["name"],
                                    materials.relative(files["base"], out) if "base" in files else None,
                                    materials.relative(files["normal"], out) if "normal" in files else None,
                                    blend=materials.is_glass(subs[s.material]))
                idx = mesh.indices[s.first_index:s.first_index + s.num_indices]
                prims.append(w.primitive(pos, idx, nrm, mesh.uvs, None, joints, mesh.weights, mi))
            report.append({"skin": attachment["skin"], "vertices": len(mesh.positions), "triangles": len(mesh.indices) // 3})
        if prims:
            node = w.node(name=name, mesh=w.mesh(name, prims), skin=0)
            w.doc["scenes"][0]["nodes"].append(node)
        return {"skins": report, "sections": len(prims)}

    def add_clip(self, w: gltf.Writer, first: int, clip_path: str, name: str) -> None:
        """Adds one clip as a glTF animation with no track for the root bone (the clips play in place: the server moves a
        creature)."""
        clip = anim.read_clip(cgf.read_chunks(source.read(clip_path)))
        by_id = {b.controller_id: b for b in self.bones}
        samplers, channels = [], []
        for cid, c in clip.controllers.items():
            b = by_id.get(cid)
            if b is None or b.parent < 0:
                continue
            node = first + b.index
            if len(c.rotations):
                t = ((c.rot_times - clip.start) * clip.secs_per_frame).astype(np.float32)
                q = np.array([mat_to_quat(to_y_up(np.block([[anim.quat_to_matrix(r), np.zeros((3, 1))],
                                                              [np.zeros((1, 3)), np.ones((1, 1))]])))
                              for r in c.rotations], dtype=np.float32)
                samplers.append({"input": w.accessor(t, "SCALAR", None, minmax=True), "output": w.accessor(q, "VEC4", None),
                                 "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": "rotation"}})
            if len(c.positions):
                t = ((c.pos_times - clip.start) * clip.secs_per_frame).astype(np.float32)
                p = gltf.z_up_to_y_up(c.positions).astype(np.float32)
                samplers.append({"input": w.accessor(t, "SCALAR", None, minmax=True), "output": w.accessor(p, "VEC3", None),
                                 "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": "translation"}})
        if channels:
            w.doc.setdefault("animations", []).append({"name": name, "samplers": samplers, "channels": channels})


def weapon_body(cdf_path: str, row: dict) -> dict:
    """@return The body of a weapon model: its .cdf with the skin and material overrides of its weapon row (attachments 1 and
    2)."""
    body = read_cdf(cdf_path)
    skins = list(body["skins"])
    for index in (1, 2):
        skin, mtl = row.get(f"SkinOverride{index}", ""), row.get(f"MaterialOverride{index}", "")
        if skin and index - 1 < len(skins):
            skins[index - 1] = {"name": skins[index - 1]["name"], "skin": norm(skin), "material": norm(mtl) or skins[index - 1]["material"]}
        elif mtl and index - 1 < len(skins):
            skins[index - 1] = dict(skins[index - 1], material=norm(mtl))
    return dict(body, skins=skins)


def weapon_animation_set(animations: dict, row: dict, scope: str, resolver: "ClipResolver") -> dict | None:
    """@return The animation set of the weapon scope of a weapon (Mannequin: a fragment whose FragmentDef has the scope Weapon,
    or OffHandWeapon for the off hand, plays the variant of the animation database of the weapon row on the weapon too; the
    controller of the creature names the scope context WeaponContext), or None when no fragment of the creature has a clip
    there. The variants: the tag state of the creature (its default tags and the tags of the wire). The clips go to the
    resolver of the skeleton of the weapon."""
    if not row.get("AnimDbPath"):
        return None
    database = read_database(norm(row["AnimDbPath"]))
    state = set(animations["tags"])
    fragments = {}
    for fragment_id, fragment in animations["fragments"].items():
        if scope not in fragment.get("scopes", []):
            continue
        variant, index = select_variant(database.get(fragment["name"].lower(), []), state)
        if variant is None or not any(item["clip"] for layer in variant["layers"] for item in layer):
            continue
        layers = [[dict(item, kind=resolver.add(item["clip"])) for item in layer] for layer in variant["layers"]]
        if not any(item["kind"] != "missing" and item["kind"] != "end" for layer in layers for item in layer):
            continue
        fragments[fragment_id] = {"name": fragment["name"], "variant": index, "tags": variant["tags"], "blendOut": variant["blendOut"],
                                  "items": layers[0], "layers": layers[1:], "marks": variant["marks"], "scopes": [scope],
                                  "flags": fragment.get("flags", [])}
    if not fragments:
        return None
    return {"controller": animations["controller"], "database": norm(row["AnimDbPath"]), "tags": animations["tags"],
            "wireTags": animations["wireTags"], "fragmentNames": animations["fragmentNames"], "fragments": fragments,
            "scopeNames": animations["scopeNames"], "scope": scope}


def write_weapon(cdf_path: str, row: dict, out: Path) -> dict:
    """Writes one weapon model as a static glTF mesh with the shared materials of stage 1.2 (docs/WORLD.md: each glTF
    material is the key of a material record): the skins of its .cdf in their bind pose; the skin and material overrides of
    its weapon row replace the attachments 1 and 2. The origin of the model is its attach point (the socket).
    @return The manifest entries of the mesh, its materials, and its textures."""
    body = read_cdf(cdf_path)
    skins = list(body["skins"])
    for index in (1, 2):
        skin, mtl = row.get(f"SkinOverride{index}", ""), row.get(f"MaterialOverride{index}", "")
        if skin and index - 1 < len(skins):
            skins[index - 1] = {"name": skins[index - 1]["name"], "skin": skin, "material": mtl or skins[index - 1]["material"]}
        elif mtl and index - 1 < len(skins):
            skins[index - 1] = dict(skins[index - 1], material=mtl)
    w = gltf.Writer()
    prims, records, textures, by_key = [], {}, {}, {}
    for attachment in skins:
        f = cgf.read_chunks(source.read(attachment["skin"]))
        mesh = cgf.read_mesh(f, f.of_type(cgf.T_MESH)[0])
        subs = []
        if attachment["material"]:
            try:
                subs = materials.load_mtl(attachment["material"], attachment["skin"])[1]
            except Exception:
                subs = []
        pos = gltf.z_up_to_y_up(mesh.positions)
        nrm = gltf.z_up_to_y_up(mesh.normals) if mesh.normals is not None else None
        for subset in mesh.subsets:
            sub = subs[subset.material] if 0 <= subset.material < len(subs) else None
            if sub is not None and materials.not_drawn(sub):
                continue
            record, used = materials.shared_material(sub)
            records[record["name"]] = record
            textures.update({t["asset"]: t for t in used})
            if record["name"] not in by_key:
                by_key[record["name"]] = w.material(record["name"])
            idx, (p, n, uv) = shared_mesh.compact(mesh.indices[subset.first_index:subset.first_index + subset.num_indices], pos, nrm, mesh.uvs)
            prims.append(w.primitive(p, idx, n, uv, material=by_key[record["name"]]))
    if not prims:
        raise ValueError(f"{cdf_path}: no drawn triangles")
    out.parent.mkdir(parents=True, exist_ok=True)
    w.doc["scenes"][0]["nodes"] = [w.node(name=out.stem, mesh=w.mesh(out.stem, prims))]
    w.save(out)
    translucent = any(r["blend"] == "translucent" for r in records.values())
    entries = {f"creature-weapon:{cdf_path}": None}
    entry = {"kind": "static", "file": str(out.relative_to(env.content_dir())), "version": shared_mesh.VERSION,
             "hash": shared_mesh.file_hash(out), "materials": sorted(records), "nanite": not translucent}
    out_entries = {}
    for record in list(records.values()) + list(textures.values()):
        out_entries[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
    return {"entry": entry, "records": out_entries}


def object_path(folder: str, stem: str, kind: str = "SkeletalMeshes", name: str = "") -> str:
    """@return The object path of an asset that Interchange makes from a glTF file STEM in a manifest folder:
    FOLDER/STEM/SkeletalMeshes/NAME (a skeletal mesh: NAME = STEM; a clip: NAME = STEM + clip name), FOLDER/STEM/StaticMeshes/STEM."""
    name = name or stem
    return f"{folder}/{stem}/{kind}/{name}.{name}"


# ---------------------------------------------------------------------------------------------------------- convert

def load_json(path: Path, default: dict) -> dict:
    return json.loads(path.read_text()) if path.is_file() else default


def near_slices(data: dict, census_doc: dict, center: tuple[float, float], radius: float) -> set[int]:
    """@return The indexes of the slices of the spawn points in a circle (each body of the alias of a kind)."""
    out = set()
    for point in data["spawnPoints"]:
        if math.hypot(point[3] - center[0], point[4] - center[1]) <= radius:
            for body, _percent in data["kinds"][point[1]].get("bodies", []):
                out.add(body)
    return out


def tables_of_type(kind: str) -> list[dict]:
    """@return The rows of each data table of the game with this type (all files of the table folder)."""
    rows: list[dict] = []
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")):
        if "vitals" not in path and "itemdefinitions_weapons" not in path:
            continue
        try:
            sheet = datasheet.Sheet(source.read(path))
        except Exception:
            continue
        if sheet.type == kind:
            rows += [dict(row, _file=path) for row in sheet.rows()]
    return rows


def crc_of(text: str) -> int:
    return zlib.crc32(text.lower().encode()) & 0xFFFFFFFF


def kind_looks() -> dict:
    """@return The look of the vitals rows and of the category rows (research creature-visuals.md section 3): the wire vitals id
    (the CRC-32 of the lower-case VitalsID) -> the base row (BaseVitalsId of VitalsLevelVariantData), its CharacterScale and
    CreatureType (VitalsBaseData); the wire category id -> IsNamed, MtlOverride, FemaleMtlOverride (VitalsCategoryData)."""
    base = {str(row["VitalsID"]).lower(): row for row in tables_of_type("VitalsBaseData")}
    vitals = {}
    for row in tables_of_type("VitalsLevelVariantData"):
        found = base.get(str(row.get("BaseVitalsId", "")).lower())
        if found is None:
            continue
        scale = found.get("CharacterScale", 1.0)
        vitals[str(crc_of(str(row["VitalsID"])))] = {"base": found["VitalsID"], "scale": round(float(scale), 5) if scale else 1.0,
                                                    "type": found.get("CreatureType", "")}
    categories = {}
    for row in tables_of_type("VitalsCategoryData"):
        entry = {"named": bool(row.get("IsNamed"))}
        if row.get("MtlOverride"):
            entry["material"] = norm(str(row["MtlOverride"]))
        if row.get("FemaleMtlOverride"):
            entry["femaleMaterial"] = norm(str(row["FemaleMtlOverride"]))
        categories[str(crc_of(str(row["VitalsCategoryID"])))] = entry
    return {"vitals": vitals, "categories": categories}


def equip_types() -> dict:
    """@return The sockets of each equip type of equiptypesdatabase.equipdb."""
    out = {}
    for element in source.walk(source.parse(source.read(EQUIP_TYPES))[0]):
        name = element.child(source.field_id("Name"))
        if name is None or element.child(EQUIP_SOCKETS["main"][0]) is None:
            continue
        entry = {}
        for key, (socket, position, rotation) in EQUIP_SOCKETS.items():
            values = []
            for field in (position, rotation):
                child = element.child(field)
                values.append(list(struct.unpack(">3f", child.value)) if child is not None and child.value and len(child.value) == 12 else [0.0, 0.0, 0.0])
            entry[key] = {"socket": source.text(element.child(socket)).lower(), "position": values[0], "rotation": values[1]}
        out[source.text(name)] = entry
    return out


def weapon_rows() -> dict:
    """@return The weapon rows by the lower-case WeaponID: the equip type, the mesh, the skin and material overrides, the tags,
    and the animation database of the weapon scope."""
    out = {}
    for row in tables_of_type("WeaponItemDefinitions"):
        out[str(row["WeaponID"]).lower()] = {key: (norm(str(row.get(key) or "")) if "Override" in key else str(row.get(key) or ""))
                                             for key in ("EquipType", "MeshOverride", "SkinOverride1", "MaterialOverride1",
                                                         "SkinOverride2", "MaterialOverride2", "MannequinTag", "OffHandMannequinTag",
                                                         "AnimDbPath")}
    return out


def write_names(data: dict, looks: dict | None = None) -> Path:
    """Writes names.json: the name of each category row and vitals row of the kinds (the name plate: the name of the category
    row, else the name of the vitals row; protocol notes 4.14) and the creature type (a Critter has no level mark)."""
    categories, vitals = {}, {}
    for kind in data["kinds"]:
        if kind.get("categoryId") and kind.get("name"):
            categories[str(kind["categoryId"])] = [kind["name"], kind.get("creatureType", "")]
        if kind.get("vitalsId"):
            vitals[str(kind["vitalsId"])] = [kind.get("vitalsName") or kind.get("vitals", ""), kind.get("creatureType", "")]
    # The creature type and the scale of the base row of each vitals row (the scale of each instance; research section 3).
    looks = looks or {"vitals": {}, "categories": {}}
    scales = {}
    for key, look in looks["vitals"].items():
        if key in vitals and look["type"]:
            vitals[key][1] = look["type"]
        scales[key] = look["scale"]
    out = creature_dir() / "names.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"format": 2, "categories": categories, "vitals": vitals, "scales": scales}, sort_keys=True))
    return out


def base_body_of(census_doc: dict, skeleton: str) -> str:
    """@return The body of the skeleton file: the body of the slice with the most spawn points on that skeleton (a stable
    choice: the same body for each set of converted slices)."""
    best, best_points = "", -1
    for record in census_doc["slices"]:
        if record.get("skeleton") == skeleton and record["body"] in census_doc["bodies"]:
            if record.get("spawnPoints", 0) > best_points or (record.get("spawnPoints", 0) == best_points and record["body"] < best):
                best, best_points = record["body"], record.get("spawnPoints", 0)
    return best


def convert(indexes: set[int], force: bool = False) -> dict:
    """Converts the slices of the census with these indexes: their animation sets, the skeleton files (the bones, the body of
    the skeleton, the clips of all converted sets on it), the other bodies and their material variants, and the default
    weapons. The files go into the shared manifest (SWC_CONTENT/manifest.json; docs/WORLD.md): a skeleton and a body as kind
    character (the default glTF pipeline), a weapon as kind static with the shared materials. slices.json has the object path
    of each asset (the naming rule of Interchange, object_path)."""
    data = backend_data()
    census_path = creature_dir() / "census.json"
    if not census_path.is_file():
        census()
    census_doc = json.loads(census_path.read_text())
    looks = kind_looks()
    write_names(data, looks)
    equips, weapons = equip_types(), weapon_rows()
    index_path = creature_dir() / "slices.json"
    index = load_json(index_path, {"format": 2, "slices": {}, "skeletons": {}, "bodies": {}, "weapons": {}})
    if index.get("format") != 2:
        index = {"format": 2, "slices": {}, "skeletons": {}, "bodies": {}, "weapons": {}}
    known_entries = shared_manifest.load()["assets"]
    entries: dict[str, dict] = {}
    kinds_of_slice: dict[int, list[dict]] = {}
    for kind in data["kinds"]:
        for body, _percent in kind.get("bodies", []):
            kinds_of_slice.setdefault(body, []).append(kind)
    resolvers: dict[str, ClipResolver] = {}
    by_skeleton: dict[str, list[tuple[str, dict]]] = {}
    # The bodies of the animated weapons (the model key of a weapon -> its body), and the first one on each weapon skeleton.
    weapon_bodies: dict[str, dict] = {}
    weapon_base: dict[str, str] = {}
    failures: dict[str, str] = {}
    report: dict = {"skeletons": {}, "bodies": 0, "weapons": 0, "animation sets": 0, "hit volumes": 0,
                    "hit volumes not in the backend slice": 0}

    def manifest_entry(key: str, kind: str, out: Path, extra: dict | None = None) -> str:
        """Adds a manifest entry. @return Its Unreal folder."""
        entry = {"kind": kind, "file": str(out.relative_to(env.content_dir())), "hash": shared_mesh.file_hash(out), **(extra or {})}
        entries[key] = entry
        return shared_manifest.unreal_folder(kind, key)

    def up_to_date(key: str, out: Path) -> bool:
        return not force and out.is_file() and key in known_entries and known_entries[key].get("hash") == shared_mesh.file_hash(out)

    def body_file(skeleton: str, body: dict, stem: str, folder_key: str) -> str:
        """Writes the glTF of one body on a skeleton (if needed). @return The object path of its skeletal mesh."""
        out = creature_dir() / "bodies" / f"{stem}.gltf"
        out.parent.mkdir(parents=True, exist_ok=True)
        if not up_to_date(folder_key, out):
            bones = Skeleton(skeleton)
            w = gltf.Writer()
            _first, roots = bones.write(w)
            bones.add_body(w, body, out, stem, 1024)
            w.doc["scenes"][0]["nodes"] += roots
            w.save(out)
            report["bodies"] += 1
        folder = manifest_entry(folder_key, "character", out)
        return object_path(folder, stem)

    for slice_index in sorted(indexes):
        record = census_doc["slices"][slice_index]
        if not record.get("skeleton"):
            failures[record["path"]] = "no body"
            continue
        skeleton = record["skeleton"]
        resolver = resolvers.setdefault(skeleton, ClipResolver(skeleton))
        try:
            animations = animation_set(record, data["slices"][slice_index], resolver)
        except Exception as error:
            failures[record["path"]] = f"animation set: {error}"[:200]
            continue
        set_key = short_key(f"{record['controller']}|{record['database']}|{skeleton}|{'+'.join(animations['tags'])}|{animations['wireTags']}")
        by_skeleton.setdefault(skeleton, []).append((set_key, animations))
        key = f"{record['asset']}:{record['sub']}"
        entry = {"path": record["path"], "body": record["body"], "skeleton": short_key(skeleton), "animations": set_key,
                 "marker": record.get("marker", {}), "weapons": [], "variants": {},
                 "actions": [action[0] for action in data["slices"][slice_index].get("actions", [])]}
        # The hit volumes with the local id of their entity (the entity id XOR the root id of the slice; the facet target is
        # (local id XOR G) + component id: the id rule of protocol notes 4.2).
        backend_slice = data["slices"][slice_index]
        pairs = {(entity.upper(), component.upper()) for entity, component, _name in backend_slice.get("components", [])}
        root = int(backend_slice.get("root", "0"), 16)
        volumes = []
        for volume in record.get("hitVolumes", []):
            # Only a component of the slice of the backend: the backend refuses a contact with a different facet target.
            if (volume["entity"], volume["component"]) not in pairs:
                report["hit volumes not in the backend slice"] += 1
                continue
            volumes.append({**{k: v for k, v in volume.items() if k != "entity"}, "local": f"{int(volume['entity'], 16) ^ root:016X}"})
        entry["hitVolumes"] = volumes
        report["hit volumes"] += len(volumes)
        # The body capsule: the QueryShapeCapsule of the CharacterComponent of the slice (the backend tool: radius, the length
        # of the segment, its axis and its offset in the frame of the creature: x right, y front, z up).
        if record.get("lock"):
            entry["lock"] = record["lock"]
        capsule = backend_slice.get("capsule")
        if capsule:
            entry["capsule"] = {"radius": capsule[0], "height": capsule[1], "axis": capsule[2], "offset": capsule[3]}
        body = census_doc["bodies"][record["body"]]
        # The default weapons on the sockets of their equip type (research creature-visuals.md section 4).
        for item in record.get("items", []):
            row = weapons.get(item["item"].lower())
            if item["slot"] not in (MAIN_HAND, OFF_HAND) or row is None or not row["MeshOverride"].endswith(".cdf"):
                continue
            if not source.has(row["MeshOverride"]):
                failures[item["item"]] = "no weapon model " + row["MeshOverride"]
                continue
            equip = equips.get(row["EquipType"], {}).get("main" if item["slot"] == MAIN_HAND else "off", {})
            socket = body.get("sockets", {}).get(equip.get("socket", ""))
            if socket is None:
                failures[f"{record['path']}:{item['item']}"] = f"the body has no socket {equip.get('socket')}"
                continue
            model_key = f"{row['MeshOverride']}|{row['SkinOverride1']}|{row['MaterialOverride1']}|{row['SkinOverride2']}|{row['MaterialOverride2']}"
            if model_key not in index["weapons"] or force:
                stem = file_stem("w", model_key)
                out = creature_dir() / "weapons" / f"{stem}.gltf"
                manifest_key = f"creature-weapon:{model_key}"
                try:
                    if up_to_date(manifest_key, out):
                        entries[manifest_key] = known_entries[manifest_key]
                    else:
                        written = write_weapon(row["MeshOverride"], row, out)
                        entries.update(written["records"])
                        entries[manifest_key] = written["entry"]
                        report["weapons"] += 1
                except Exception as error:
                    failures[item["item"]] = f"weapon: {error}"[:200]
                    continue
                index["weapons"][model_key] = {"mesh": object_path(shared_manifest.unreal_folder("static", manifest_key), stem, "StaticMeshes")}
            weapon_entry = {"slot": item["slot"], "item": item["item"], "model": model_key, "socket": equip["socket"],
                            "bone": socket["bone"], "position": socket["position"], "rotation": socket["rotation"],
                            "equipPosition": equip["position"], "equipRotation": equip["rotation"],
                            "tags": row["OffHandMannequinTag"] if item["slot"] == OFF_HAND else row["MannequinTag"]}
            # The weapon scope: a weapon with a skeleton and clips for the fragments of this creature is a skeletal mesh on the
            # skeleton of the weapon with its own animation set (the bow string). The static model stays for the other weapons.
            try:
                body_of_weapon = weapon_body(row["MeshOverride"], row)
                weapon_skeleton = body_of_weapon["skeleton"]
                if weapon_skeleton and row.get("AnimDbPath") and source.has(weapon_skeleton):
                    weapon_resolver = resolvers.setdefault(weapon_skeleton, ClipResolver(weapon_skeleton))
                    scope = "Weapon" if item["slot"] == MAIN_HAND else "OffHandWeapon"
                    weapon_set = weapon_animation_set(animations, row, scope, weapon_resolver)
                    if weapon_set is not None:
                        weapon_set_key = short_key(f"weapon|{weapon_set['database']}|{weapon_skeleton}|{'+'.join(weapon_set['tags'])}|{scope}")
                        by_skeleton.setdefault(weapon_skeleton, []).append((weapon_set_key, weapon_set))
                        weapon_bodies[model_key] = body_of_weapon
                        weapon_base.setdefault(weapon_skeleton, model_key)
                        weapon_entry.update({"skeleton": short_key(weapon_skeleton), "animations": weapon_set_key, "body": model_key})
                        report["animated weapons"] = report.get("animated weapons", 0) + 1
            except Exception as error:
                failures[f"{record['path']}:{item['item']}"] = f"weapon scope: {error}"[:200]
            entry["weapons"].append(weapon_entry)
        # The material variants of the category rows of the kinds of this slice (MtlOverride, FemaleMtlOverride for a female
        # body; research section 3: the exact precedence is an open point).
        female = "female" in record["body"]
        for kind in kinds_of_slice.get(slice_index, []):
            look = looks["categories"].get(str(kind.get("categoryId", 0)), {})
            material = look.get("femaleMaterial" if female else "material", "")
            if material:
                entry["variants"][str(kind["categoryId"])] = f"{record['body']}|{material}"
        index["slices"][key] = entry
    # The skeleton files: the bones, the body of the skeleton (the body of the slice with the most spawn points on it: a stable
    # choice), and the union of the clips of the converted animation sets on it.
    for skeleton, sets in by_skeleton.items():
        resolver = resolvers[skeleton]
        skeleton_key = short_key(skeleton)
        skeleton_entry = index["skeletons"].setdefault(skeleton_key, {"path": skeleton, "clips": {}})
        known = skeleton_entry["clips"]
        known.update({name: dict(info) for name, info in resolver.clips.items()})
        base_body = base_body_of(census_doc, skeleton) or weapon_base.get(skeleton, "")
        skeleton_entry["baseBody"] = base_body
        stem = file_stem("s", skeleton)
        out = creature_dir() / "skeletons" / f"{stem}.gltf"
        manifest_key = f"creature-skeleton:{skeleton}"
        clip_names = sorted(known)
        if force or not out.is_file() or skeleton_entry.get("written") != clip_names or not up_to_date(manifest_key, out):
            bones = Skeleton(skeleton)
            w = gltf.Writer()
            first, roots = bones.write(w)
            base_report = bones.add_body(w, weapon_bodies.get(base_body) or census_doc["bodies"][base_body], out, stem, 1024)
            w.doc["scenes"][0]["nodes"] += roots
            for name in clip_names:
                bones.add_clip(w, first, known[name]["path"], clip_asset_name(name))
            out.parent.mkdir(parents=True, exist_ok=True)
            w.save(out)
            skeleton_entry["written"] = clip_names
            report["skeletons"][skeleton_key] = {"bones": len(bones.bones), "clips": len(known), "base": base_report}
        folder = manifest_entry(manifest_key, "character", out, {"body": base_body})
        skeleton_entry["mesh"] = object_path(folder, stem)
        for name, info in known.items():
            info["asset"] = object_path(folder, stem, name=stem + clip_asset_name(name))
        index["bodies"][base_body] = {"skeleton": skeleton_key, "mesh": skeleton_entry["mesh"], "base": True}
        # The other bodies and the material variants of the converted slices on this skeleton.
        for entry in index["slices"].values():
            wanted_bodies = {weapon["body"] for weapon in entry.get("weapons", []) if weapon.get("skeleton") == skeleton_key}
            if entry["skeleton"] == skeleton_key:
                wanted_bodies |= {entry["body"]} | set(entry.get("variants", {}).values())
            for body_key in sorted(wanted_bodies):
                if body_key == base_body or f"creature-body:{body_key}" in entries:
                    continue
                if body_key in weapon_bodies:
                    mesh_path = body_file(skeleton, weapon_bodies[body_key], file_stem("b", body_key), f"creature-body:{body_key}")
                    index["bodies"][body_key] = {"skeleton": skeleton_key, "mesh": mesh_path, "base": False}
                    continue
                body_path, _, material = body_key.partition("|")
                body = dict(census_doc["bodies"][body_path])
                if material:
                    if not source.has(material if material.endswith(".mtl") else material + ".mtl"):
                        failures[body_key] = "no material"
                        continue
                    body["skins"] = [dict(skin, material=material) for skin in body["skins"]]
                mesh_path = body_file(skeleton, body, file_stem("b", body_key), f"creature-body:{body_key}")
                index["bodies"][body_key] = {"skeleton": skeleton_key, "mesh": mesh_path, "base": False}
        # The animation sets: the clips (with the object path of each one), the blend spaces, and the combinations of the
        # skeleton go into each set that uses them.
        for set_key, animations in sets:
            animations["skeleton"] = skeleton_key
            animations["clips"] = {name: known[name] for name in resolver.clips}
            animations["blendSpaces"] = resolver.spaces
            animations["combs"] = resolver.combs
            for fragment in animations["fragments"].values():
                if "items" in fragment:
                    fragment["length"] = fragment_length(fragment["items"], resolver.clips)
                    fragment["loop"] = bool(fragment["items"]) and fragment["items"][-1]["loop"] and fragment["length"] < 0
            animations["missingClips"] = sorted(resolver.missing)
            set_out = creature_dir() / "animations" / f"{set_key}.json"
            set_out.parent.mkdir(parents=True, exist_ok=True)
            set_out.write_text(json.dumps(animations, indent=1, sort_keys=True))
            report["animation sets"] += 1
    index_path.write_text(json.dumps(index, indent=1, sort_keys=True))
    shared_manifest.add_many(entries)
    report["manifest entries"] = len(entries)
    report["slices"] = len(indexes)
    report["missing clips"] = {Path(k).stem: sorted(r.missing)[:10] for k, r in resolvers.items() if r.missing}
    report["failures"] = failures
    report["effects"] = write_effects()
    return report


def write_effects() -> dict:
    """Writes effects.json (stage P2.9): the projectile slices and the spell slices of the backend data by the key of their
    metadata state ("GUID:SUB", the asset catalog of the game), and the spell rows of the game by the CRC-32 of their
    lower-case SpellID (the key of the Spell state, protocol notes 4.25).
      projectiles  the flight of the client (protocol notes 4.37): m_baseSpeed, m_accel (m/s/s; x east, y north, z up),
                   m_flyingLifetime and m_stuckLifetime (s)
      spells       the delayed events (slot, delay, type: 0 trigger, 2 activate, 3 deactivate; target entity), and the first
                   detection volume (the danger area): its shape (sphere, cylinder, or none: the shape of the spell row), its
                   sizes (m), and its active time from the start (s; -1: never)
      spellRows    ShapeTypes and Radius (m) of each spell row of the spell tables
      lootContainers  the loot bags (loot_containers)
      statusRows   the status rows at a name plate (status_rows)"""
    gd = gamedata_dir()
    projectiles = json.loads((gd / "projectile-data.json").read_text())
    spells = json.loads((gd / "spell-data.json").read_text())
    by_path: dict[str, str] = {}
    for guid, products in source.catalog().items():
        for sub, path in products:
            if path.endswith(".dynamicslice"):
                by_path.setdefault(path, f"{uuid.UUID(bytes=guid)}".upper() + f":{sub}")
    out: dict = {"format": 1, "projectiles": {}, "spells": {}, "spellRows": {}}
    for path, speed, _extra, _time_max, accel, flying, stuck in projectiles["slices"]:
        if path in by_path:
            out["projectiles"][by_path[path]] = {"path": path, "speed": speed, "accel": accel, "flyingLifetime": flying,
                                                 "stuckLifetime": stuck}
    for entry in spells["slices"]:
        volume = None
        for name, radius, active, shape, shape_radius, height, offset, inactive, *_rest in entry.get("volumes", []):
            volume = {"name": name, "radius": radius, "activeAfter": active, "shape": shape, "shapeRadius": shape_radius,
                      "height": height, "offset": offset, "inactiveAfter": inactive}
            break
        out["spells"][f"{entry['asset']}:{entry['sub']}"] = {
            "path": entry["path"], "events": [[event[0], event[1], event[2], event[3]] for event in entry.get("events", [])],
            "volume": volume}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")
                       and "spelltable" in p):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except Exception:
            continue
        if sheet.type != "SpellData":
            continue
        for row in sheet.rows():
            spell_id = str(row.get("SpellID") or "")
            if spell_id:
                out["spellRows"].setdefault(str(crc_of(spell_id.lower())), {"id": spell_id, "shape": str(row.get("ShapeTypes") or ""),
                                                                           "radius": number_of(str(row.get("Radius") or ""))})
    out["lootContainers"] = loot_containers()
    out["statusRows"] = status_rows()
    out_path = creature_dir() / "effects.json"
    out_path.write_text(json.dumps(out, indent=1, sort_keys=True))
    return {"projectile slices": len(out["projectiles"]), "spell slices": len(out["spells"]), "spell rows": len(out["spellRows"]),
            "loot containers": len(out["lootContainers"]), "status rows": len(out["statusRows"])}


def status_rows() -> dict:
    """@return The status rows of the backend data that show at a name plate (ability-data.json statusEffects; the compact map of
    the StatusEffects state names them by their index): the id, the base duration (s), MoveSpeedMod, Stunned, NoNav (a root),
    StopOnHitCount, and the EffectCategories."""
    path = gamedata_dir() / "ability-data.json"
    if not path.is_file():
        return {}
    out: dict = {}
    for row in json.loads(path.read_text()).get("statusEffects", []):
        name, index, duration, speed, stunned, no_nav, stop_on_hit, shown, *_rest = row
        cells = row[9] if len(row) > 9 and isinstance(row[9], dict) else {}
        if shown:
            out[str(index)] = {"id": name, "duration": duration, "moveSpeedMod": speed, "stunned": stunned, "root": no_nav,
                               "stopOnHit": stop_on_hit, "categories": str(cells.get("EffectCategories") or "")}
    return out


def loot_containers() -> dict:
    """@return The loot containers of the backend data (interaction-data.json format 3, member lootContainers; stage P2.10): by the
    key of their metadata state, the slice, the model (the first static mesh of the slice, a shared manifest entry), the slots of
    PositionInTheWorld and LootDrop, the times (s), and the Interact bounds of the template (the slot, the component, the entity
    and the root of the slice: the facet target is (entity XOR root XOR G) + component; m_interactRangeSq; the options: the option
    id and its entity, whose instance id is entity XOR root XOR G)."""
    path = gamedata_dir() / "interaction-data.json"
    if not path.is_file():
        return {}
    data = json.loads(path.read_text())
    out: dict = {}
    for name, container in (data.get("lootContainers") or {}).items():
        template = data["templates"][container["template"]]
        key = f"{uuid.UUID(template['asset'])}".upper() + f":{template['sub']}"
        mesh = ""
        roots = source.parse(source.read(template["slice"]))
        for element in source.walk(roots[0]):
            found = source.asset_id(element)
            product = source.product(found[0], found[1], ".cgf") if found else None
            if product and "_lod" not in product:
                product = norm(product)
                if product not in shared_manifest.load()["assets"]:
                    shared_mesh.convert(product)
                stem = Path(product).stem
                mesh = object_path(shared_manifest.unreal_folder("static", product), stem, "StaticMeshes")
                break
        out[key] = {"name": name, "slice": template["slice"], "root": template["root"], "mesh": mesh,
                    "positionSlot": template["slots"]["position"], "lootDropSlot": container["lootDropSlot"],
                    "lifetime": container["lifetime"], "emptyLifetime": container["emptyLifetime"], "hideDelay": container["hideDelay"],
                    "bounds": [{"slot": bound["slot"], "component": bound["component"], "entity": bound["entity"],
                                "rangeSq": bound["rangeSq"], "options": bound["options"]} for bound in template.get("bounds", [])]}
    return out


def slices_matching(census_doc: dict, texts: list[str]) -> set[int]:
    return {i for i, s in enumerate(census_doc["slices"]) if any(t.lower() in s["path"] for t in texts)}


def wanted(path: str | None = None, force: bool = False) -> dict:
    """Converts the slices of the wanted list of the client: {"slices": ["ASSET:SUB", ...]}."""
    file = Path(path) if path else env.work_dir() / "creatures" / "wanted.json"
    request = json.loads(file.read_text()) if file.is_file() else {"slices": []}
    census_path = creature_dir() / "census.json"
    if not census_path.is_file():
        census()
    census_doc = json.loads(census_path.read_text())
    keys = {key.upper() for key in request.get("slices", [])}
    indexes = {i for i, s in enumerate(census_doc["slices"]) if f"{s['asset']}:{s['sub']}" in keys}
    return convert(indexes, force) if indexes else {"slices": 0}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-convert-creatures", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("census")
    p = sub.add_parser("convert")
    p.add_argument("--near", type=float, nargs=3, metavar=("X", "Y", "RADIUS"))
    p.add_argument("--slices", nargs="+", default=[])
    p.add_argument("--all", action="store_true")
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("wanted"); p.add_argument("file", nargs="?"); p.add_argument("--force", action="store_true")
    sub.add_parser("effects")
    args = parser.parse_args(argv)
    if args.command == "census":
        result = census()
    elif args.command == "convert":
        census_path = creature_dir() / "census.json"
        if not census_path.is_file():
            census()
        census_doc = json.loads(census_path.read_text())
        if args.all:
            indexes = set(range(len(census_doc["slices"])))
        elif args.slices:
            indexes = slices_matching(census_doc, args.slices)
        else:
            x, y, radius = args.near if args.near else (*START, NEAR_RADIUS)
            indexes = near_slices(backend_data(), census_doc, (x, y), radius)
        result = convert(indexes, args.force)
    elif args.command == "effects":
        result = write_effects()
    else:
        result = wanted(args.file, args.force)
    print(json.dumps(result, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
