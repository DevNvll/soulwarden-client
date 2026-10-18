"""The player character: the character creation databases, the armor appearances, and the parts of the modular body.

Usage (tools/convert-player.sh; output in SWC_CONTENT/player, never in git):
  tables            looks.json: for each gender the races, skin tones, hair styles, hair colors, facial hair, and the
                    default body slots; each armor appearance row. Game paths only, no conversion.
  base              base/<gender>.gltf: the skeleton of each gender, one base mesh (the leader of the modular body),
                    and the idle clip.
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
from pathlib import Path

import numpy as np

from . import anim, cgf, datasheet, env, gltf, materials, source
from .character import mat_to_quat, norm, to_y_up

DATABASES = "sharedassets/genericassets/character/charactercreationdatabase/"
GENDERS = {"Male": "male", "Female": "female"}
# The idle clip of each gender for the base (the player clips are in the male folder; research feel-values.md R2.7).
IDLE_CLIPS = {"male": "animations/gameplay/character/player/male/unarmed/navigation/idle/unarmed_idle.caf",
              "female": "animations/gameplay/character/player/male/unarmed/navigation/idle/female_unarmed_idle.caf"}
# The base mesh: the leader of the modular body (the client hides it). The naked chest of the default appearance.
BASE_SLOT = "shirt1"
UNREAL_ROOT = "/Game/Imported/Player"

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
    """@return A short Unreal folder: /Game/Imported/Player/KIND/STEM_HASH (the folder rule of the manifest module)."""
    stem = re.sub(r"[^A-Za-z0-9_]", "_", Path(key.split("|")[0]).stem)[:40]
    return f"{UNREAL_ROOT}/{kind}/{stem}_{hashlib.sha1(key.encode()).hexdigest()[:6]}"


def manifest_path() -> Path:
    return player_dir() / "manifest.json"


def load_manifest() -> dict:
    path = manifest_path()
    return json.loads(path.read_text()) if path.is_file() else {"format": 1, "assets": {}}


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

    def add_clip(self, w: gltf.Writer, first: int, clip_path: str) -> dict:
        clip = anim.read_clip(cgf.read_chunks(source.read(clip_path)))
        by_id = {b.controller_id: b for b in self.bones}
        samplers, channels = [], []
        for cid, c in clip.controllers.items():
            b = by_id.get(cid)
            if b is None:
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
        w.doc.setdefault("animations", []).append({"name": Path(clip_path).stem, "samplers": samplers, "channels": channels})
        return {"clip": clip_path, "frames": clip.end - clip.start, "fps": round(1 / clip.secs_per_frame, 3),
                "controllers": len(clip.controllers)}


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
        clip = skeleton.add_clip(w, first, IDLE_CLIPS[folder])
        w.save(out)
        doc["assets"][key] = {"kind": "base", "gender": gender, "file": str(out.relative_to(env.content_dir())),
                              "unreal": f"{UNREAL_ROOT}/{gender}/Base"}
        report[gender] = {"bones": len(skeleton.bones), "base": part, "clip": clip, "gltf": str(out)}
    save_manifest(doc)
    return report


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
    p = sub.add_parser("base"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("wanted"); p.add_argument("file", nargs="?"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("part"); p.add_argument("gender", choices=list(GENDERS)); p.add_argument("skin"); p.add_argument("mtl", nargs="?", default="")
    p.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "tables":
        result = tables()
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
