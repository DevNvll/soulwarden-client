"""The player character: the character creation databases, the armor appearances, and the parts of the modular body.

Usage (tools/convert-player.sh; output in SWC_CONTENT/player, never in git):
  tables            looks.json: for each gender the races, skin tones, hair styles, hair colors, facial hair, and the
                    default body slots; each armor appearance row. Game paths only, no conversion.
  locomotion        locomotion.json: the default unarmed locomotion fragments of the player Mannequin database
                    (player.adb), their blend spaces (.bspace with the precomputed weight grid), and their clips.
  base              base/<gender>.gltf: the skeleton of each gender, one base mesh (the leader of the modular body),
                    and the clips of locomotion.json with the root bone locked (the data moves the character).
  weapons           weapons.json: the weapon items, their equip types, the bone attachments, the draw and sheathe times.
  actions           actions.json: the stances, the draw and sheathe, and the attack graph of each weapon tag set
                    (actions.py, stage P2.7).
  interactions      interactions.json: the templates of the server objects that the client uses (the fixtures, the
                    personal loot bag) from interaction-data.json of the backend copy (actions.py, stage P2.10).
  movement          movement.json: the motion and facing lists of the movement actions, their named conditions, and the
                    movement columns of the status rows (movement.py, stage P3.3).
  roads [NAME...]   SWC_CONTENT/roads: the road weight of the terrain at 1 m for each region (default: the 49 regions of the
                    main world; roads.py, stage P3.3).
  wanted [FILE]     converts the parts, the weapons, and the clip packs that the client asked for
                    (default SWC_WORK/player/wanted.json, written by the client).
  clips GENDER TAGS the clip pack of one weapon tag set ("1H_Melee,Sword") on the skeleton of a gender.
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
LOCOMOTION_FRAGMENTS = {"Idle": ["", "Crouch", "Prone"], "Strafe": ["", "Crouch", "Prone"], "Sprint": ["", "Crouch", "Prone"],
                        "Sprint_Start": ["", "Crouch", "Prone"], "Fall": [""],
                        "Land": ["Soft", "Normal", "Hard"], "Jump": ["ForwardNav", "BackNav", "LeftNav", "RightNav", ""],
                        "Jump_Standing": [""],
                        # Stage P3.3: the posture transitions (playeractions_idleposetransition, sprintposetransition) and the stun of
                        # the player (playeractions_reaction: r_Stun, r_Stun_Loop, r_Stun_End).
                        "Idle_PostureTransition": ["Crouch+FromStand", "Stand+FromCrouch", "Prone+FromStand", "Stand+FromProne",
                                                   "Crouch+FromProne", "Prone+FromCrouch"],
                        "Sprint_PostureTransition": ["Crouch+FromStand", "Crouch+FromStand+LeftNav", "Crouch+FromStand+RightNav",
                                                     "Prone+FromStand", "Stand+FromProne", "Crouch+FromProne", "Prone+FromCrouch"],
                        "r_Stun": [""], "r_Stun_Loop": [""], "r_Stun_End": [""],
                        # Stage WF5: the swim (playeractions_swim: Swim_Idle, the clip unarmed_swim_idle; Swim_Nav, the blend space
                        # swim_nav_blend with MoveSpeed and TurnSpeed; research wf5-swim/swim-fragments.json).
                        "Swim_Idle": [""], "Swim_Nav": [""]}
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
        if entry.get("cdf"):
            cloth = cloth_parts(entry["cdf"])
            if cloth:
                entry["cloth"] = cloth
        rows[row["ItemID"]] = entry
    return rows


_cloth_cache: dict[str, list] = {}


def cloth_parts(cdf_path: str) -> list[list[str]]:
    """The cloth attachments (capes, skirts) of the character file of an appearance (AppearanceCDF): [attachment name,
    render skin, material]. A .cloth file names its render mesh, a skin on the player skeleton; the client draws it as a
    skinned part with no cloth simulation."""
    if cdf_path in _cloth_cache:
        return _cloth_cache[cdf_path]
    out = []
    try:
        cdf = ET.fromstring(source.read(cdf_path).decode("utf-8", "replace"))
        for attachment in cdf.iter("Attachment"):
            if attachment.get("Type") != "CA_CLOTH" or not attachment.get("Binding"):
                continue
            data = source.read(norm(attachment.get("Binding")))
            skins = re.findall(rb"[A-Za-z0-9_/\.\-]+\.skin", data)
            if skins:
                out.append([attachment.get("AName") or "cloth", norm(skins[0].decode()), norm(attachment.get("Material") or "")])
    except Exception:
        out = []
    _cloth_cache[cdf_path] = out
    return out


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
        # A bone of the skin that the base skeleton does not have (the simulation bones of a cloth skin) takes the weight
        # of its nearest parent in the skin file that the skeleton has: the missing chain stays in its bind pose (research
        # gear.md, "Cloth without a solver"). Not bone 0.
        skin_bones = cgf.read_bones(f)
        missing = set()

        def bone_of(index: int) -> int:
            bone = skin_bones[index]
            while bone.name not in self.index_of and bone.parent >= 0:
                missing.add(bone.name)
                bone = skin_bones[bone.parent]
            return self.index_of.get(bone.name, 0)

        remap = np.array([bone_of(index) for index in range(len(skin_bones))], dtype=np.uint16)
        joints = remap[mesh.joints]
        used = {skin_bones[index].name for index in np.unique(mesh.joints[mesh.weights > 0])}
        missing &= used
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
                "sections": len(prims), "frozen bones": sorted(missing)}

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


# The weapon look (research equipment.md 6.2): WeaponAppearanceOverride of the item -> WeaponAppearanceDefinitions
# (MeshOverride: a character file with a weapon skeleton and its skins); ItemStatsRef -> WeaponItemDefinitions (EquipType,
# MannequinTag, HideMainWeaponMeshWhileSheathed); the accessory table (the quiver of a bow).
WEAPON_APPEARANCES = "javelindata_itemdefinitions_weaponappearances.datasheet"
WEAPON_STATS = "javelindata_itemdefinitions_weapons.datasheet"
WEAPON_ACCESSORIES = "javelindata_itemdefinitions_weaponaccessories.datasheet"
# The equip database: for each EquipType of the weapon table the attachment in the hand and in the sheath, for the main
# hand and the off hand, with their offsets (research gear.md "Attachments and transforms"; fields CRC-32 of the names).
EQUIP_DATABASE = "sharedassets/genericassets/items/equiptypesdatabase.equipdb"
EQUIP_FIELDS = {"hand": "attachment", "handPosition": "attachment offset position", "handRotation": "attachment offset rotation (degrees)",
                "sheathed": "sheath data", "sheathPosition": "sheath offset position", "sheathRotation": "sheath offset rotation (degrees)"}


def crc(name: str) -> int:
    return zlib.crc32(name.lower().encode()) & 0xFFFFFFFF


def equip_types() -> dict:
    """The equip types of the equip database: {name: {"main": {...}, "offHand": {...}}}; each side has the hand and the
    sheath attachment names, the offset positions (m), and the offset rotations (degrees about x, y, z)."""
    root = source.parse(source.read(EQUIP_DATABASE))[0]
    out = {}
    for element in root.child(crc("Equip Types")).children:
        entry = {}
        for side, prefix in (("main", ""), ("offHand", "off-hand ")):
            values = {}
            for key, field in EQUIP_FIELDS.items():
                node = element.child(crc(prefix + field))
                values[key] = floats_of(node) if key.endswith(("Position", "Rotation")) else text_of(node)
            entry[side] = values
        out[text_of(element.child(F_NAME))] = entry
    return out


def weapon_skins(cdf_path: str) -> list[list[str]]:
    """@return The (skin, material) pairs of the skin attachments of a weapon character file."""
    cdf = ET.fromstring(source.read(norm(cdf_path)).decode("utf-8", "replace"))
    return [[norm(a.get("Binding")), norm(a.get("Material") or "")] for a in cdf.iter("Attachment")
            if a.get("Type") == "CA_SKIN" and a.get("Binding")]


def weapon_model(cdf_path: str) -> str:
    cdf = ET.fromstring(source.read(norm(cdf_path)).decode("utf-8", "replace"))
    model = cdf.find("Model")
    return norm(model.get("File") or "") if model is not None else ""


def weapon_look(row: dict) -> dict | None:
    """The look of an appearance or accessory row: its model, and the skins of its character file with the overrides of
    the row (SkinOverrideN replaces skin N, MaterialOverrideN its material; an unset override keeps the file; research
    gear.md, "From a paperdoll entry to a weapon", step 6)."""
    mesh = (row.get("MeshOverride") or "").strip()
    if not mesh:
        return None
    try:
        skins = weapon_skins(mesh)
        model = weapon_model(mesh)
    except Exception:
        return None
    for n in range(1, 5):
        skin = norm((row.get(f"SkinOverride{n}") or "").strip())
        material = norm((row.get(f"MaterialOverride{n}") or "").strip())
        while (skin or material) and len(skins) < n:
            skins.append(["", ""])
        if skin:
            skins[n - 1][0] = skin
        if material:
            skins[n - 1][1] = material
    skins = [pair for pair in skins if pair[0]]
    if not skins:
        return None
    key = "weapon|" + norm(mesh) + "|" + hashlib.sha1(json.dumps(skins).encode()).hexdigest()[:8]
    # A model on the player skeleton (a gauntlet) is a part of the body, not a rigid weapon (gear.md, step 5).
    return {"mesh": norm(mesh), "model": model, "playerModel": model.startswith("objects/characters/player/"), "skins": skins, "key": key}


def attachments() -> dict:
    """The bone attachments of the player character file of each gender: bone, position (m), rotation (w, x, y, z)."""
    out = {}
    for gender, folder in GENDERS.items():
        cdf = ET.fromstring(source.read(f"objects/characters/player/{folder}/player_{folder}.cdf").decode("utf-8", "replace"))
        out[gender] = {a.get("AName"): {"bone": a.get("BoneName"),
                                        "position": [float(v) for v in (a.get("RelPosition") or "0,0,0").split(",")],
                                        "rotation": [float(v) for v in (a.get("RelRotation") or "1,0,0,0").split(",")]}
                       for a in cdf.iter("Attachment") if a.get("Type") == "CA_BONE" and a.get("BoneName")}
    return out


# The context tags of a standing character with no other state: a draw or a sheathe fragment of the settled stance.
STANCE_TAGS = {"Stand"}
_adb_cache: dict[str, ET.Element] = {}


def adb_root(path: str) -> ET.Element:
    path = norm(path)
    if path not in _adb_cache:
        _adb_cache[path] = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    return _adb_cache[path]


def fragment_timing(fragment: ET.Element) -> dict:
    """The first clip of a fragment (name, speed, start) and its named markers."""
    animation = fragment.find("AnimLayer/Animation")
    out = {"markers": fragment_markers(fragment)}
    if animation is not None and animation.get("name"):
        out.update({"clip": animation.get("name").lower(), "speed": float(animation.get("speed") or 1.0),
                    "start": float(animation.get("startTime") or animation.get("start") or 0.0)})
    return out


def weapon_timing(tag_text: str) -> dict:
    """The swap times of the weapon tags (research gear.md "Sheathe and draw animation"): the draw fragment
    Unsheathe_UpperBody of the first database of player.adb whose SubADB tags the weapon has (the variant with the most
    tags of the weapon and no context tag but Stand), and the sheathe fragment Sheathe_UpperBody of player.adb with the
    From_ tag that names the most tags of the weapon. The choice of the From_ tag is an inference."""
    ordered = [tag.strip().lower() for tag in tag_text.split(",") if tag.strip()]
    tags = {tag.strip() for tag in tag_text.split(",") if tag.strip()}
    lower = set(ordered)
    out = {}
    root = adb_root(ADB)
    # Mannequin tags compare without case (the tag GreatSword selects the SubADB Greatsword).
    stance = {tag.lower() for tag in STANCE_TAGS}
    for sub in root.find("SubADBs"):
        if not set(sub.get("Tags", "").lower().split("+")) <= lower:
            continue
        node = adb_root(sub.get("File")).find("FragmentList/Unsheathe_UpperBody")
        best = None
        for fragment in node if node is not None else []:
            names = set(filter(None, (fragment.get("Tags") or "").lower().split("+")))
            if names <= lower | stance and (best is None or len(names) > best[0]):
                best = (len(names), fragment)
        if best is not None:
            out["draw"] = {**fragment_timing(best[1]), "adb": norm(sub.get("File")), "tags": best[1].get("Tags") or ""}
            break
    candidates = []
    for fragment in root.find("FragmentList/Sheathe_UpperBody"):
        names = set(filter(None, (fragment.get("Tags") or "").lower().split("+")))
        origin = [name for name in names if name.startswith("from_")]
        if len(origin) != 1 or not (names - set(origin)) <= stance:
            continue
        rest = origin[0][5:].lower()
        matched = 0
        last = -1
        for tag in sorted(lower, key=len, reverse=True):
            if tag in rest:
                matched += 1
                rest = rest.replace(tag, "")
                last = max(last, ordered.index(tag))
        if matched:
            # A tie goes to the tag that is later in the tags of the weapon (the more specific one: GreatSword, not 2H_Melee).
            candidates.append((matched - (1 if rest.strip("_") else 0), matched, last, fragment))
    if candidates:
        fragment = max(candidates, key=lambda item: item[:3])[3]
        out["sheathe"] = {**fragment_timing(fragment), "adb": ADB, "tags": fragment.get("Tags") or ""}
    return out


def weapons() -> dict:
    """Writes weapons.json: the bone attachments of the player, the attachment rules of each EquipType, and the look of
    each weapon item of the master item tables."""
    # The ids compare without case (gear.md: a master reference can have a different case than its row).
    looks = {row["WeaponAppearanceID"].lower(): row for row in datasheet.load(WEAPON_APPEARANCES).rows()}
    stats = {row["WeaponID"].lower(): row for row in datasheet.load(WEAPON_STATS).rows()}
    accessories = {}
    for row in datasheet.load(WEAPON_ACCESSORIES).rows():
        accessories.setdefault(row["WeaponID"].lower(), row)
    items = {}
    for name in sorted(n for n in source.paks().entries if n.endswith(".datasheet") and "itemdefinitions_master" in n.lower()):
        for row in datasheet.Sheet(source.read(name)).rows():
            stat = stats.get((row.get("ItemStatsRef") or "").lower())
            if stat is None or row["ItemID"] in items:
                continue
            look_row = looks.get((row.get("WeaponAppearanceOverride") or "").lower())
            look = weapon_look(look_row) if look_row else None
            if look is None:
                look = weapon_look(stat)
            if look is None:
                continue
            entry = {**look, "stats": row.get("ItemStatsRef"), "equipType": stat.get("EquipType") or "", "tag": stat.get("MannequinTag") or "",
                     "offHandTag": stat.get("OffHandMannequinTag") or "", "animDb": norm(stat.get("AnimDbPath") or ""),
                     "shieldCompatible": stat.get("IsShieldCompatible") not in (None, "", 0, 0.0, False, "0", "false", "FALSE"),
                     "hideSheathed": stat.get("HideMainWeaponMeshWhileSheathed") not in (None, "", 0, 0.0, False, "0", "false", "FALSE")}
            # The accessory row names the master item (gear.md "Bow, quiver, and arrows"); the stats row is a fallback.
            accessory_row = accessories.get(row["ItemID"].lower()) or accessories.get((row.get("ItemStatsRef") or "").lower())
            accessory = weapon_look(accessory_row) if accessory_row else None
            if accessory:
                entry["accessory"] = {**accessory, "equipType": accessory_row.get("EquipType") or "",
                                      "sheathMode": accessory_row.get("SheathMode") or ""}
            items[row["ItemID"]] = entry
    timings = {}
    for entry in items.values():
        if entry["tag"] not in timings:
            timings[entry["tag"]] = weapon_timing(entry["tag"])
    doc = {"format": 2, "sources": [EQUIP_DATABASE, WEAPON_APPEARANCES, WEAPON_STATS, WEAPON_ACCESSORIES, ADB],
           "attachments": attachments(), "equipTypes": equip_types(), "timings": timings, "items": items}
    out = player_dir() / "weapons.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"weapons": str(out), "items": len(items), "meshes": len({entry["key"] for entry in items.values()})}


def weapon(key: str, skins: list, force: bool = False, doc: dict | None = None) -> dict:
    """Converts one weapon look (its skins, with no skeleton) into a static mesh glTF."""
    folder = unreal_folder("Weapons", key.split("|", 1)[1])
    out = player_dir() / "weapons" / (folder.rsplit("/", 1)[-1] + ".gltf")
    own = doc is None
    doc = doc if doc is not None else load_manifest()
    if out.is_file() and not force and key in doc["assets"]:
        return {"weapon": key, "skipped": "exists"}
    out.parent.mkdir(parents=True, exist_ok=True)
    w = gltf.Writer()
    prims = []
    for skin_path, mtl in skins:
        f = cgf.read_chunks(source.read(skin_path))
        mesh = cgf.read_mesh(f, f.of_type(cgf.T_MESH)[0])
        subs = []
        if mtl:
            try:
                subs = materials.load_mtl(mtl, skin_path)[1]
            except Exception:
                subs = []
        pos = gltf.z_up_to_y_up(mesh.positions)
        nrm = gltf.z_up_to_y_up(mesh.normals) if mesh.normals is not None else None
        for s in mesh.subsets:
            mi = None
            if s.material < len(subs):
                if materials.not_drawn(subs[s.material]):
                    continue
                info = materials.describe(subs[s.material], 1024)
                files = info["files"]
                mi = w.material(info["name"], materials.relative(files["base"], out) if "base" in files else None,
                                materials.relative(files["normal"], out) if "normal" in files else None,
                                blend=materials.is_glass(subs[s.material]))
            prims.append(w.primitive(pos, mesh.indices[s.first_index:s.first_index + s.num_indices], nrm, mesh.uvs, None, None, None, mi))
    if not prims:
        raise ValueError("no drawn section")
    name = "SM_" + folder.rsplit("/", 1)[-1]
    w.doc["scenes"][0]["nodes"].append(w.node(name=name, mesh=w.mesh(name, prims)))
    w.save(out)
    doc["assets"][key] = {"kind": "weapon", "file": str(out.relative_to(env.content_dir())), "unreal": folder}
    if own:
        save_manifest(doc)
    return {"weapon": key, "sections": len(prims)}


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


def clip_key(tags: list[str]) -> str:
    """The key of the clip pack of a weapon tag set: the tags in lower case, joined by '_' ("1h_melee_sword"); "unarmed"
    for no tag."""
    return "_".join(tag.lower() for tag in tags) or "unarmed"


def clip_pack(gender: str, tag_text: str, force: bool = False, doc: dict | None = None) -> dict:
    """Converts the clips of one weapon tag set of actions.json (its stance, off-hand, draw, sheathe, attack, and ranged
    fragments, with the clips of their blend spaces) into clips/<gender>/<key>/clips.gltf: the skeleton of the gender
    and one animation for each clip, the root bone locked (the client moves the character with the root track of
    actions.json: AnimationDrivenMotion). Unreal imports the animations onto the skeleton of the base."""
    actions_doc = json.loads((player_dir() / "actions.json").read_text())
    wanted_tags = [tag.strip().lower() for tag in tag_text.replace("+", ",").split(",") if tag.strip() and tag.strip().lower() != "unarmed"]
    entry = next((w for w in actions_doc["weapons"] if [tag.lower() for tag in w["tags"]] == wanted_tags), None)
    if entry is None:
        raise ValueError(f"actions.json has no weapon tag set {tag_text}")
    folder = GENDERS[gender]
    key_text = clip_key(entry["tags"])
    key = f"clips|{folder}|{key_text}"
    out = player_dir() / "clips" / folder / key_text / "clips.gltf"
    own = doc is None
    doc = doc if doc is not None else load_manifest()
    if out.is_file() and not force and key in doc["assets"]:
        return {"clips": key, "skipped": "exists"}
    fragment_keys = list(entry["stance"].values()) + list(entry["offhand"].values()) + list(entry.get("dodge", {}).values())
    fragment_keys += list(entry.get("life", {}).values()) + list(entry.get("use", {}).values())
    fragment_keys += [entry[kind] for kind in ("draw", "sheathe") if kind in entry]
    fragment_keys += [item["fragment"] for item in entry.get("graph", []) if "fragment" in item]
    # The ranged and magic actions (stage P3.6): each variant of the fragments of the ranged program.
    fragment_keys += [variant["fragment"] for variants in entry.get("ranged", {}).get("fragments", {}).values() for variant in variants]
    names = set()
    for fragment_key in fragment_keys:
        for layer in actions_doc["fragments"][fragment_key]["layers"]:
            for item in layer:
                if item.get("clip"):
                    names.add(item["clip"])
                elif item.get("blendSpace"):
                    names.update(example["clip"] for example in actions_doc["blendSpaces"][item["blendSpace"]]["examples"])
    clips = actions_doc["clips"]
    usable = sorted(name for name in names if name in clips and "path" in clips[name])
    out.parent.mkdir(parents=True, exist_ok=True)
    skeleton = Skeleton(gender)
    w = gltf.Writer()
    first, roots = skeleton.write(w)
    w.doc["scenes"][0]["nodes"] += roots
    failed = {}
    for name in usable:
        try:
            skeleton.add_clip(w, first, clips[name]["path"], name, root_lock=True)
        except Exception as error:   # a clip that does not read: the client plays no clip for it
            failed[name] = str(error)[:120]
    w.save(out)
    doc["assets"][key] = {"kind": "clips", "gender": gender, "tags": entry["tags"], "file": str(out.relative_to(env.content_dir())),
                          "unreal": f"{UNREAL_ROOT}/{gender}/Clips/{key_text}"}
    if own:
        save_manifest(doc)
    return {"clips": key, "count": len(usable) - len(failed), "failed": failed, "not in actions.json": sorted(names - set(usable))[:10]}


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
    """Converts the parts, the weapons, and the clip packs of the wanted list of the client: {"parts": [[gender, skin,
    material], ...], "weapons": [item id, ...], "clips": [[gender, weapon tags], ...]} (weapons.json gives the look of each
    item, and of its accessory; actions.json gives the clips of each weapon tag set)."""
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
    if request.get("weapons"):
        table = json.loads((player_dir() / "weapons.json").read_text())["items"]
        for item in request["weapons"]:
            entry = table.get(item)
            for look in ([entry] + ([entry["accessory"]] if "accessory" in entry else [])) if entry else []:
                try:
                    weapon(look["key"], look["skins"], force, doc)
                    done += 1
                except Exception as error:   # a weapon that does not convert: the client shows no mesh for it
                    failed[look["key"]] = str(error)[:160]
    for gender, tags in request.get("clips", []):
        try:
            clip_pack(gender, tags, force, doc)
            done += 1
        except Exception as error:   # a clip pack that does not convert: the client plays the unarmed clips
            failed[f"clips|{gender}|{tags}"] = str(error)[:160]
    save_manifest(doc)
    return {"converted": done, "failed": len(failed), "failures": failed}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-convert-player", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("tables")
    sub.add_parser("locomotion")
    sub.add_parser("camera")
    sub.add_parser("weapons")
    sub.add_parser("actions")
    sub.add_parser("interactions")
    sub.add_parser("movement")
    p = sub.add_parser("roads"); p.add_argument("names", nargs="*")
    p = sub.add_parser("base"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("wanted"); p.add_argument("file", nargs="?"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("clips"); p.add_argument("gender", choices=list(GENDERS)); p.add_argument("tags"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("part"); p.add_argument("gender", choices=list(GENDERS)); p.add_argument("skin"); p.add_argument("mtl", nargs="?", default="")
    p.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "tables":
        result = tables()
    elif args.command == "locomotion":
        result = locomotion()
    elif args.command == "camera":
        result = camera()
    elif args.command == "weapons":
        result = weapons()
    elif args.command == "actions":
        from . import actions
        result = actions.actions()
    elif args.command == "interactions":
        from . import actions
        result = actions.interactions()
    elif args.command == "movement":
        from . import movement
        result = movement.movement()
    elif args.command == "roads":
        from . import census, roads
        result = roads.convert(args.names or census.world_regions())
    elif args.command == "base":
        result = base(args.force)
    elif args.command == "wanted":
        result = wanted(args.file, args.force)
    elif args.command == "clips":
        result = clip_pack(args.gender, args.tags, args.force)
    else:
        result = part(args.gender, args.skin, args.mtl, args.force)
    print(json.dumps(result, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
