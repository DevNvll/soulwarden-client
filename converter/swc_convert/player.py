"""The player character: the character creation databases, the armor appearances, and the parts of the modular body.

Usage (tools/convert-player.sh; output in SWC_CONTENT/player, never in git):
  tables            looks.json: for each gender the races, skin tones, hair styles, hair colors, facial hair, and the
                    default body slots; each armor appearance row. Game paths only, no conversion.
  locomotion        locomotion.json: the default unarmed locomotion fragments of the player Mannequin database
                    (player.adb), their blend spaces (.bspace with the precomputed weight grid), and their clips.
  base              base/<gender>.gltf: the skeleton of each gender, one base mesh (the leader of the modular body),
                    and the clips of locomotion.json with the root bone locked (the data moves the character).
  wanted [FILE]     converts the parts that the client asked for
                    (default SWC_WORK/player/wanted.json, written by the client).
  part GENDER SKIN [MTL]   converts one part.

A part is one skin attachment (.skin) with one material (.mtl) and the full skeleton of its gender: Unreal imports it
onto the skeleton of the base (tools/import-player.sh), and the client attaches it to the base with Leader Pose. A body
part has the material of the skin tone of the character (a different tone is a different part); a hair part has the
material of its style, and the client tints it with the hair color at run time.

Sources (Soulwarden research appearance.md, character-creation.md, menu-equipment.md, equipment.md; field ids below are
the CRC-32 of the lower-case field names of the object streams, read from the files):
  <gender>.charcreatedb    races (head skin, skin tone database, hair databases, default tone), default appearance data
  <race>.skintonedb        skin tones: the head material and the body material of each tone
  <gender>.hairstyledb, <race>_facial.hairstyledb   styles (skins of the hair variants, material), hair colors (tint)
  javelindata_itemappearancedefinitions.datasheet    ArmorAppearanceDefinitions: Skin1/Material1, Skin2, the hide rules
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

import numpy as np

from . import anim, cgf, datasheet, env, gltf, materials, source
from .character import mat_to_quat, norm, to_y_up

DATABASES = "sharedassets/genericassets/character/charactercreationdatabase/"
GENDERS = {"Male": "male", "Female": "female"}
# The idle clip of each gender for the base (the player clips are in the male folder; research feel-values.md R2.7).
IDLE_CLIPS = {"male": "animations/gameplay/character/player/male/unarmed/navigation/idle/unarmed_idle.caf",
              "female": "animations/gameplay/character/player/male/unarmed/navigation/idle/female_unarmed_idle.caf"}
# The player Mannequin database and the locomotion fragments of stage 1.7 (research feel-values.md R2.7, R3): the default
# (unarmed) variant of each fragment; for Jump the variant with forward input and the default one; for Land its three classes.
ADB = "animations/mannequin/adb/player/player.adb"
LOCOMOTION_FRAGMENTS = {"Idle": [""], "Strafe": [""], "Sprint": [""], "Sprint_Start": [""], "Fall": [""],
                        "Land": ["Soft", "Normal", "Hard"], "Jump": ["ForwardNav", "BackNav", "LeftNav", "RightNav", ""],
                        "Jump_Standing": [""]}
# The fragments with root motion: the jumps move the character with the root bone of their clip until the marker
# Enable_Gravity (research feel-values.md R2.4.1, R2.8.4). locomotion.json keeps the root track of their clips.
ROOT_MOTION_FRAGMENTS = {"Jump", "Jump_Standing"}
CLIP_FOLDER = "animations/gameplay/character/player/"
# The female idle (the female body has its own idle clip in the male folder; the other clips are common).
FEMALE_IDLE = {"unarmed_idle": "female_unarmed_idle"}
# The base mesh: the leader of the modular body (the client hides it). The naked chest of the default appearance.
BASE_SLOT = "shirt1"
# The player assets are in the shared content plugin of the imported content (tools/content-link.sh), with the world.
UNREAL_ROOT = "/SwcGameContent/Player"
OLD_UNREAL_ROOT = "/Game/Imported/Player"

# Field ids of the creation databases (CRC-32 of the lower-case field name; meaning from the dumps of the files).
F_NAME = 0x5E237E06
F_GENDER = 0xC7470A42
F_RACES = 0x5DBD1EC9
F_DEFAULT_SLOTS = 0x4728C513
F_RACE_TONE_DB = 0xD6E8FFBE
F_RACE_HAIR_DB = 0x782576AA
F_RACE_FACIAL_DB = 0xFE5F05DE
F_RACE_HEAD = 0xC5E10BD6
F_RACE_DEFAULT_TONE = 0xB8E781D1
F_SLOT_KEY = 0xA2756C5A
F_SLOT_DATA = 0x3B7C3DE0
F_SLOT_SKIN = 0x0279681E
F_SLOT_MATERIAL = 0xEBC12E43
F_SLOT_VISIBLE = 0xC9B70E09
F_TONES = 0xCF9B389E
F_TONE_HEAD = 0xA3E13AB0
F_TONE_BODY = 0x2E502AEF
F_STYLES = 0x69256A07
F_STYLE_VARIANTS = 0xBD5087CE
F_VARIANT_INDEX = 0xF143BFAD
F_VARIANT_SKIN = 0xF7CE6DCB
F_STYLE_MATERIAL = 0xAE3C4906
F_COLORS = 0x1DC1536D
F_COLOR_VALUE = 0x61C8D87E

ARMOR_TABLE = "javelindata_itemappearancedefinitions.datasheet"
ARMOR_COLUMNS = {"AppearanceName": "name", "Gender": "gender", "ItemClass": "class", "HairChop": "hairChop",
                 "HideHair": "hideHair", "HideFacialHair": "hideFacialHair", "HideLegs": "hideLegs", "HideHead": "hideHead",
                 "ShowBaseModelOnly": "baseModelOnly", "Skin1": "skin1", "Material1": "material1", "IsSkin1": "isSkin1",
                 "Skin2": "skin2", "Material2": "material2", "IsSkin2": "isSkin2", "LeftHandOnlySkin": "leftHandOnly",
                 "RightHandOnlySkin": "rightHandOnly", "AppearanceCDF": "cdf"}


def player_dir() -> Path:
    return env.content_dir() / "player"


def parse_db(name: str):
    return source.parse(source.read(DATABASES + name))[0]


def path_text(element) -> str:
    """@return The text of a path field (the text is in the deepest first child), lower case."""
    while element is not None and element.value is None and element.children:
        element = element.children[0]
    return norm(element.value.decode("utf-8", "replace")) if element is not None and element.value else ""


def asset_path(element) -> str:
    """@return The product path of an asset reference field (16 bytes of the asset id, then the sub id)."""
    if element is None or element.value is None or len(element.value) < 16:
        return ""
    return source.product(element.value[:16]) or ""


def text_of(element) -> str:
    return element.value.decode("utf-8", "replace") if element is not None and element.value is not None else ""


def floats_of(element) -> list[float]:
    if element is None or element.value is None or len(element.value) % 4:
        return []
    return [round(v, 5) for v in struct.unpack(f">{len(element.value) // 4}f", element.value)]


def db_name(product: str) -> str:
    """@return The database file name of a product path (male-european.skintonedb)."""
    return product.rsplit("/", 1)[-1]


def read_tones(name: str) -> list[dict]:
    root = parse_db(name)
    return [{"name": text_of(tone.child(F_NAME)), "head": path_text(tone.child(F_TONE_HEAD)),
             "body": path_text(tone.child(F_TONE_BODY))} for tone in root.child(F_TONES).children]


def read_styles(name: str) -> dict:
    root = parse_db(name)
    styles = []
    for style in root.child(F_STYLES).children:
        variants = {}
        for variant in (style.child(F_STYLE_VARIANTS).children if style.child(F_STYLE_VARIANTS) is not None else []):
            index = int.from_bytes(variant.child(F_VARIANT_INDEX).value, "big")
            variants[str(index)] = path_text(variant.child(F_VARIANT_SKIN))
        styles.append({"name": text_of(style.child(F_NAME)), "variants": variants,
                       "material": path_text(style.child(F_STYLE_MATERIAL))})
    colors = [{"name": text_of(color.child(F_NAME)), "color": floats_of(color.child(F_COLOR_VALUE))}
              for color in (root.child(F_COLORS).children if root.child(F_COLORS) is not None else [])]
    return {"styles": styles, "colors": colors}


def read_gender(gender: str) -> dict:
    """@return The tables of one gender: races (with their database names), slots, the skeleton."""
    folder = GENDERS[gender]
    root = parse_db(f"{folder}.charcreatedb")
    races = []
    for race in root.child(F_RACES).children:
        races.append({"name": text_of(race.child(F_NAME)), "head": path_text(race.child(F_RACE_HEAD)),
                      "tones": db_name(asset_path(race.child(F_RACE_TONE_DB))),
                      "hair": db_name(asset_path(race.child(F_RACE_HAIR_DB))),
                      "facial": db_name(asset_path(race.child(F_RACE_FACIAL_DB))),
                      "defaultTone": text_of(race.child(F_RACE_DEFAULT_TONE))})
    slots = {}
    for pair in root.child(F_DEFAULT_SLOTS).children:
        data = pair.child(F_SLOT_DATA)
        visible = data.child(F_SLOT_VISIBLE)
        slots[text_of(pair.child(F_SLOT_KEY))] = {"skin": path_text(data.child(F_SLOT_SKIN)),
                                                  "material": path_text(data.child(F_SLOT_MATERIAL)),
                                                  "visible": bool(visible is not None and visible.value and visible.value[0])}
    cdf_path = f"objects/characters/player/{folder}/player_{folder}.cdf"
    cdf = ET.fromstring(source.read(cdf_path).decode("utf-8", "replace"))
    # The material of each attachment of the character file: the default material of a slot.
    for attachment in cdf.iter("Attachment"):
        name = attachment.get("AName", "")
        if name in slots and attachment.get("Material") and not slots[name]["material"]:
            slots[name]["material"] = norm(attachment.get("Material"))
    return {"cdf": cdf_path, "skeleton": norm(cdf.find("Model").get("File")), "idleProperty": text_of(root.child(0xD211EFE4)),
            "races": races, "slots": slots}


def armor_rows() -> dict:
    rows = {}
    for row in datasheet.load(ARMOR_TABLE).rows():
        entry = {}
        for column, key in ARMOR_COLUMNS.items():
            value = row.get(column, "")
            if isinstance(value, float):
                value = int(value)
            if key in ("skin1", "material1", "skin2", "material2", "leftHandOnly", "rightHandOnly", "cdf") and isinstance(value, str):
                value = norm(value)
            if value not in ("", 0, False):
                entry[key] = value
        rows[row["ItemID"]] = entry
    return rows


def tables() -> dict:
    """Writes looks.json with the full tables (game paths). @return Counts."""
    doc = {"format": 1, "genders": {}, "toneSets": {}, "hairSets": {}, "armor": armor_rows()}
    for gender in GENDERS:
        info = read_gender(gender)
        doc["genders"][gender] = info
        for race in info["races"]:
            if race["tones"] and race["tones"] not in doc["toneSets"]:
                doc["toneSets"][race["tones"]] = read_tones(race["tones"])
            for key in ("hair", "facial"):
                if race[key] and race[key] not in doc["hairSets"]:
                    doc["hairSets"][race[key]] = read_styles(race[key])
    out = player_dir() / "looks.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"looks": str(out), "races": {g: len(v["races"]) for g, v in doc["genders"].items()},
            "tone sets": len(doc["toneSets"]), "hair sets": len(doc["hairSets"]), "armor rows": len(doc["armor"])}


# ---------------------------------------------------------------------------------------------------------- the glTF

def unreal_folder(kind: str, key: str) -> str:
    """@return A short Unreal folder: /SwcGameContent/Player/KIND/STEM_HASH (the folder rule of the manifest module)."""
    stem = re.sub(r"[^A-Za-z0-9_]", "_", Path(key.split("|")[0]).stem)[:40]
    return f"{UNREAL_ROOT}/{kind}/{stem}_{hashlib.sha1(key.encode()).hexdigest()[:6]}"


def manifest_path() -> Path:
    return player_dir() / "manifest.json"


def load_manifest() -> dict:
    path = manifest_path()
    doc = json.loads(path.read_text()) if path.is_file() else {"format": 1, "assets": {}}
    # An entry of the time before the content plugin: the same folder under the new root.
    for entry in doc["assets"].values():
        if entry.get("unreal", "").startswith(OLD_UNREAL_ROOT):
            entry["unreal"] = UNREAL_ROOT + entry["unreal"][len(OLD_UNREAL_ROOT):]
    return doc


def save_manifest(doc: dict) -> None:
    manifest_path().parent.mkdir(parents=True, exist_ok=True)
    manifest_path().write_text(json.dumps(doc, indent=1, sort_keys=True))


class Skeleton:
    """The bones of the player skeleton of one gender, as glTF nodes with one skin."""

    def __init__(self, gender: str) -> None:
        folder = GENDERS[gender]
        cdf = ET.fromstring(source.read(f"objects/characters/player/{folder}/player_{folder}.cdf").decode("utf-8", "replace"))
        self.path = norm(cdf.find("Model").get("File"))
        self.bones = cgf.read_bones(cgf.read_chunks(source.read(self.path)))
        self.index_of = {b.name: b.index for b in self.bones}

    def write(self, w: gltf.Writer) -> tuple[int, list[int]]:
        """Adds the bone nodes and the skin 0. @return (index of the first bone node, root nodes)."""
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

    def add_skin(self, w: gltf.Writer, skin_path: str, mtl: str, out: Path, name: str, max_texture: int) -> dict:
        """Adds one skin attachment as a mesh node of the skin 0. @return A report."""
        f = cgf.read_chunks(source.read(skin_path))
        mesh = cgf.read_mesh(f, f.of_type(cgf.T_MESH)[0])
        remap = np.array([self.index_of.get(b.name, 0) for b in cgf.read_bones(f)], dtype=np.uint16)
        joints = remap[mesh.joints]
        subs = []
        if mtl:
            try:
                subs = materials.load_mtl(mtl, skin_path)[1]
            except Exception:
                subs = []
        pos = gltf.z_up_to_y_up(mesh.positions)
        nrm = gltf.z_up_to_y_up(mesh.normals) if mesh.normals is not None else None
        prims = []
        for s in mesh.subsets:
            mi = None
            if s.material < len(subs):
                if materials.not_drawn(subs[s.material]):
                    continue
                info = materials.describe(subs[s.material], max_texture)
                files = info["files"]
                mi = w.material(info["name"], materials.relative(files["base"], out) if "base" in files else None,
                                materials.relative(files["normal"], out) if "normal" in files else None,
                                blend=materials.is_glass(subs[s.material]))
            idx = mesh.indices[s.first_index:s.first_index + s.num_indices]
            prims.append(w.primitive(pos, idx, nrm, mesh.uvs, None, joints, mesh.weights, mi))
        if prims:
            node = w.node(name=name, mesh=w.mesh(name, prims), skin=0)
            w.doc["scenes"][0]["nodes"].append(node)
        return {"skin": skin_path, "material": mtl, "vertices": len(mesh.positions), "triangles": len(mesh.indices) // 3,
                "sections": len(prims)}

    def add_clip(self, w: gltf.Writer, first: int, clip_path: str, name: str | None = None, root_lock: bool = False) -> dict:
        """Adds one clip as a glTF animation. root_lock: no track for the root bone (Xform carries the motion of a clip;
        the movement data moves the character, research feel-values.md R2.8.5)."""
        clip = anim.read_clip(cgf.read_chunks(source.read(clip_path)))
        by_id = {b.controller_id: b for b in self.bones}
        samplers, channels = [], []
        for cid, c in clip.controllers.items():
            b = by_id.get(cid)
            if b is None or (root_lock and b.parent < 0):
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
        w.doc.setdefault("animations", []).append({"name": name or Path(clip_path).stem, "samplers": samplers, "channels": channels})
        return {"clip": clip_path, "frames": clip.end - clip.start, "fps": round(1 / clip.secs_per_frame, 3),
                "duration": round((clip.end - clip.start) * clip.secs_per_frame, 5), "controllers": len(clip.controllers),
                "moveSpeed": round(clip.move_speed, 4)}


def base(force: bool = False) -> dict:
    """Writes base/<gender>.gltf for each gender: the skeleton, the base mesh, and the idle clip."""
    looks = json.loads((player_dir() / "looks.json").read_text())
    doc = load_manifest()
    report = {}
    for gender, folder in GENDERS.items():
        out = player_dir() / "base" / f"{folder}.gltf"
        key = f"base|{folder}"
        if out.is_file() and not force:
            report[gender] = "exists"
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        skeleton = Skeleton(gender)
        w = gltf.Writer()
        first, roots = skeleton.write(w)
        slot = looks["genders"][gender]["slots"][BASE_SLOT]
        part = skeleton.add_skin(w, slot["skin"], slot["material"], out, f"SK_Player{gender}", 512)
        w.doc["scenes"][0]["nodes"] += roots
        # The idle clip first (the clip of stage 1.6), then each clip of the locomotion table with the root bone locked.
        clips = [skeleton.add_clip(w, first, IDLE_CLIPS[folder], "unarmed_idle")]
        locomotion_file = player_dir() / "locomotion.json"
        if locomotion_file.is_file():
            for name, clip in sorted(json.loads(locomotion_file.read_text())["clips"].items()):
                if name == "unarmed_idle":
                    continue
                path = clip["path"]
                if gender == "Female" and name in FEMALE_IDLE:
                    path = clip.get("female", path)
                clips.append(skeleton.add_clip(w, first, path, name, root_lock=True))
        w.save(out)
        doc["assets"][key] = {"kind": "base", "gender": gender, "file": str(out.relative_to(env.content_dir())),
                              "unreal": f"{UNREAL_ROOT}/{gender}/Base"}
        report[gender] = {"bones": len(skeleton.bones), "base": part, "clips": len(clips), "gltf": str(out)}
    save_manifest(doc)
    return report


def stem_index(ending: str) -> dict[str, list[str]]:
    """@return The pak entries of the player clip folder with one ending, by file stem (the clip name rule of the
    .chrparams animation lists: the name of a clip is its file stem, research feel-values.md R3.2.8)."""
    index: dict[str, list[str]] = {}
    for name in source.paks().entries:
        if name.startswith(CLIP_FOLDER) and name.endswith(ending):
            index.setdefault(Path(name).stem, []).append(name)
    return index


def pick(paths: list[str]) -> str:
    """@return The path of a clip name: the male player folder first, then the unarmed folders."""
    ranked = sorted(paths, key=lambda path: ("/player/male/" not in path, "/unarmed/" not in path, path))
    return ranked[0]


def read_blend_space(path: str) -> dict:
    """@return The dimensions, the examples, and the precomputed grid of a .bspace (research feel-values.md R3.2.7)."""
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    params = [{"name": p.get("Name"), "min": float(p.get("Min")), "max": float(p.get("Max")), "cells": int(p.get("Cells"))}
              for p in root.find("Dimensions")]
    # Clip names are file stems: lower case, as the pak paths (the files of the game write some with capitals).
    examples = [{"clip": e.get("AName").lower(), "scale": float(e.get("PlaybackScale") or 1.0)} for e in root.find("ExampleList")]
    grid = []
    vgrid = root.find("VGrid")
    for cell in (vgrid if vgrid is not None else []):
        grid.append([int(cell.get(f"i{n}", 0)) for n in range(4)] + [float(cell.get(f"w{n}", 0)) for n in range(4)])
    return {"path": path, "params": params, "examples": examples, "grid": grid}


def fragment_markers(fragment: ET.Element) -> dict:
    """The named markers of a fragment (procedural CAGE-Named, for example Enable_Gravity): name -> time in seconds from
    the start of the fragment (the sum of the ExitTime values of the Blend elements of its layer before it)."""
    markers = {}
    for layer in fragment.findall("ProcLayer"):
        time = 0.0
        for item in layer:
            if item.tag == "Blend":
                time += float(item.get("ExitTime", 0))
            elif item.tag == "Procedural" and item.get("type") == "CAGE-Named":
                name = item.find("ProceduralParams/Name")
                if name is not None and name.get("value"):
                    markers.setdefault(name.get("value"), round(time, 5))
    return markers


def locomotion() -> dict:
    """Writes locomotion.json: the fragments, the blend spaces, and the clips of the player locomotion."""
    adb = ET.fromstring(source.read(ADB).decode("utf-8", "replace"))
    fragments_xml = adb.find("FragmentList")
    cafs, spaces = stem_index(".caf"), stem_index(".bspace")
    fragments, blend_spaces, clips = {}, {}, {}

    def add_clip(name: str, root: bool = False) -> None:
        if name not in clips and name in cafs:
            clips[name] = {"path": pick(cafs[name])}
            female = FEMALE_IDLE.get(name)
            if female and female in cafs:
                clips[name]["female"] = pick(cafs[female])
        if root and name in clips:
            clips[name]["rootMotion"] = True

    for fragment, variants in LOCOMOTION_FRAGMENTS.items():
        node = fragments_xml.find(fragment)
        for tags in variants:
            match = next((f for f in node if (f.get("Tags") or "") == tags), None) if node is not None else None
            if match is None:
                continue
            layer = match.find("AnimLayer")
            blend = layer.find("Blend")
            animation = layer.find("Animation")
            name = animation.get("name").lower()
            entry = {"blendIn": round(float(blend.get("Duration", 0)), 4), "blendOut": round(float(match.get("BlendOutDuration", 0.2)), 4),
                     "loop": "Loop" in (animation.get("flags") or ""), "speed": float(animation.get("speed") or 1.0)}
            markers = fragment_markers(match)
            if markers:
                entry["markers"] = markers
            if name in spaces:
                entry["blendSpace"] = name
                if name not in blend_spaces:
                    blend_spaces[name] = read_blend_space(pick(spaces[name]))
                    for example in blend_spaces[name]["examples"]:
                        add_clip(example["clip"])
            else:
                entry["clip"] = name
                add_clip(name, fragment in ROOT_MOTION_FRAGMENTS)
            fragments[fragment + ("+" + tags if tags else "")] = entry
    root_id = zlib.crc32(b"Xform")
    for name, clip in clips.items():
        info = anim.read_clip(cgf.read_chunks(source.read(clip["path"])))
        clip["duration"] = round((info.end - info.start) * info.secs_per_frame, 5)
        clip["moveSpeed"] = round(info.move_speed, 4)
        root = info.controllers.get(root_id)
        if clip.pop("rootMotion", False) and root is not None and len(root.positions):
            # The root bone Xform in the space of the character (meters; x right, y forward, z up): [time, x, y, z].
            times = (root.pos_times - info.start) * info.secs_per_frame
            clip["root"] = [[round(float(t), 5)] + [round(float(v), 5) for v in p] for t, p in zip(times, root.positions)]
    doc = {"format": 1, "source": ADB, "fragments": fragments, "blendSpaces": blend_spaces, "clips": clips}
    out = player_dir() / "locomotion.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    missing = [example["clip"] for space in blend_spaces.values() for example in space["examples"] if example["clip"] not in clips]
    return {"locomotion": str(out), "fragments": sorted(fragments), "blend spaces": {k: len(v["grid"]) for k, v in blend_spaces.items()},
            "clips": len(clips), "missing clips": sorted(set(missing))}


CAMERA_SETTINGS = "libs/camera/gamecamera.xml"
CAMERA_ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_camera.actionlist"


def camera_value(text: str):
    """A value of gamecamera.xml: a number ("2.0f"), a list of numbers ("0.5, 0.0, 1.7"), a flag, or a text."""
    text = (text or "").strip()
    if text in ("true", "false"):
        return text == "true"
    parts = [part.strip().rstrip("f") for part in text.split(",")]
    try:
        numbers = [float(part) for part in parts]
    except ValueError:
        return text
    return numbers[0] if len(numbers) == 1 else numbers


def camera() -> dict:
    """Writes camera.json: the settings of gamecamera.xml, each camera state with its include chain resolved (a child
    value replaces the value of its parent) and its own FromTransition times, and the ActivateCamera entries of the
    camera action list (research feel-values.md R1)."""
    root = ET.fromstring(source.read(CAMERA_SETTINGS).decode("utf-8", "replace"))
    settings = {param.get("name"): camera_value(param.get("value")) for param in root.findall("param")}
    raw = {}
    for state in root.iter("CameraState"):
        transitions = {}
        for transition in state.iter("FromTransition"):
            values = {param.get("name"): param.get("value") for param in transition.findall("param")}
            if values.get("FromCamera"):
                transitions[values["FromCamera"]] = camera_value(values.get("SmoothTime", "0"))
        raw[state.get("name")] = {"include": state.get("include"), "params": {param.get("name"): camera_value(param.get("value"))
                                                                           for param in state.findall("param")},
                                  "transitions": transitions}

    def resolve(name: str, seen: tuple = ()) -> tuple[dict, list]:
        state = raw[name]
        include = state["include"]
        params, chain = ({}, []) if not include or include not in raw or include in seen else resolve(include, seen + (name,))
        return {**params, **state["params"]}, [name] + chain

    states = {}
    for name, state in raw.items():
        params, chain = resolve(name)
        states[name] = {"chain": chain, "params": params, "transitions": state["transitions"]}
    actions = ET.fromstring(source.read(CAMERA_ACTIONS).decode("utf-8", "replace"))
    activations = {}
    for activity in actions.iter("ActivateCamera"):
        for entry in activity:
            activations.setdefault(entry.get("Name") or entry.tag, {"camera": entry.get("CameraName"),
                                                                   "smoothTime": camera_value(entry.get("SmoothTime", "0"))})
    doc = {"format": 1, "source": [CAMERA_SETTINGS, CAMERA_ACTIONS], "settings": settings, "states": states, "activations": activations}
    out = player_dir() / "camera.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"camera": str(out), "settings": len(settings), "states": len(states), "activations": len(activations)}


def part(gender: str, skin: str, mtl: str = "", force: bool = False, doc: dict | None = None) -> dict:
    """Converts one part (a skin with its material) onto the skeleton of its gender."""
    skin, mtl = norm(skin), norm(mtl) if mtl else ""
    key = f"{skin}|{mtl}"
    folder = unreal_folder("Parts", key)
    out = player_dir() / "parts" / (folder.rsplit("/", 1)[-1] + ".gltf")
    own = doc is None
    doc = doc if doc is not None else load_manifest()
    if out.is_file() and not force and key in doc["assets"]:
        return {"part": key, "skipped": "exists"}
    out.parent.mkdir(parents=True, exist_ok=True)
    skeleton = Skeleton(gender)
    w = gltf.Writer()
    _, roots = skeleton.write(w)
    report = skeleton.add_skin(w, skin, mtl, out, "SK_" + folder.rsplit("/", 1)[-1], 1024)
    w.doc["scenes"][0]["nodes"] += roots
    w.save(out)
    doc["assets"][key] = {"kind": "part", "gender": gender, "file": str(out.relative_to(env.content_dir())), "unreal": folder}
    if own:
        save_manifest(doc)
    return report


def wanted(path: str | None = None, force: bool = False) -> dict:
    """Converts the parts of the wanted list of the client: {"parts": [[gender, skin, material], ...]}."""
    file = Path(path) if path else env.work_dir() / "player" / "wanted.json"
    request = json.loads(file.read_text())
    doc = load_manifest()
    done, failed = 0, {}
    for gender, skin, mtl in request.get("parts", []):
        try:
            part(gender, skin, mtl, force, doc)
            done += 1
        except Exception as error:   # a part that does not convert: the client keeps the slot empty
            failed[skin] = str(error)[:160]
    save_manifest(doc)
    return {"converted": done, "failed": len(failed), "failures": failed}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-convert-player", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("tables")
    sub.add_parser("locomotion")
    sub.add_parser("camera")
    p = sub.add_parser("base"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("wanted"); p.add_argument("file", nargs="?"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("part"); p.add_argument("gender", choices=list(GENDERS)); p.add_argument("skin"); p.add_argument("mtl", nargs="?", default="")
    p.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "tables":
        result = tables()
    elif args.command == "locomotion":
        result = locomotion()
    elif args.command == "camera":
        result = camera()
    elif args.command == "base":
        result = base(args.force)
    elif args.command == "wanted":
        result = wanted(args.file, args.force)
    else:
        result = part(args.gender, args.skin, args.mtl, args.force)
    print(json.dumps(result, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
