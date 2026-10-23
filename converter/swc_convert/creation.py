"""The data of the character creation (stage Q3, docs/FRONTEND.md "Q3, the character creation").

Commands:
  write   SWC_CONTENT/ui/gamedata/creation.json (the Lua host reads it): the 7 archetypes of javelindata_archetypes.datasheet (the button id is the CRC-32
          of the lower-case ArchetypeId), and the lists of the creation databases
          (sharedassets/genericassets/character/charactercreationdatabase/, 128 files) with the images of the creation screen;
          SWC_WORK/ui/wanted-creation.json: the images to convert (then tools/convert-ui.sh wanted FILE).

The lists of the databases (the type ids of their rows are in the files; the field names are CRC-32 values of lower-case
names):
  <gender>.charcreatedb      Races: name, the skin tone, hair, and facial hair databases, the head skin, the default tone,
                             the image of the race with its default tone
  <race>.skintonedb          Skin Tones: name, the head and body materials, the image
  <gender|race_facial>.hairstyledb   Hair Styles: name, the image key ("male-hairstyle01m"), enabled; Hair Colors: name, image
  eyecolors.eyecolordb       Eye Colors: name, image, enabled
  facemarks.facemarkdb, scars.scardb, tattoos.tattoodb   the styles: name, the maps of the skin, the images of each gender,
                             enabled; tattoos also Tattoo Colors
The native getters of the image paths (CustomizableCharacterRequestBus.Get*ImagePath) are not read: the image of a hair style
is the file layered/hairstyle/<key>-<hair color>.dds (2190 files = 73 keys x 30 colors) and of a facial hair
layered/facialhair/<key>-<hair color>.dds; the image of a race is layered/<gender>/<gender>-<race>-<skin tone>.dds [I, the
names of the files].
"""
from __future__ import annotations

import argparse
import json
import sys
import zlib

from . import datasheet, env, player, source

ARCHETYPES = "javelindata_archetypes.datasheet"
IMAGE_ROOT = "lyshineui/images/charactercreation/layered/"
F_ENABLED = zlib.crc32(b"enabled")
LIST_NAMES = ["Races", "Skin Tones", "Hair Styles", "Hair Colors", "Eye Colors", "Face Mark Styles", "Scar Styles",
              "Tattoo Styles", "Tattoo Colors"]
LISTS = {zlib.crc32(name.lower().encode()): name for name in LIST_NAMES}
# The text field of the image key of a hair style (male-hairstyle01m; the field name is not known).
F_STYLE_IMAGE_KEY = 0xB956C298


def out_path():
    return env.content_dir() / "ui" / "gamedata" / "creation.json"


def wanted_path():
    return env.work_dir() / "ui" / "wanted-creation.json"


def texts(element) -> list[str]:
    """@return The texts of the subtree of an element (the path fields of the asset references), in the order of the file."""
    out = []
    value = element.value
    if isinstance(value, (bytes, bytearray)) and value:
        text = bytes(value).rstrip(b"\0")
        if text and all(32 <= b < 127 for b in text):
            out.append(text.decode())
    for child in element.children or []:
        out.extend(texts(child))
    return out


def enabled(entry) -> bool:
    flag = entry.child(F_ENABLED)
    return not (flag is not None and flag.value is not None and len(flag.value) and flag.value[0] == 0)


def rows(root, list_name: str) -> list:
    for child in root.children or []:
        if LISTS.get(child.name) == list_name:
            return list(child.children or [])
    return []


def styles(name: str, list_name: str) -> list[dict]:
    """The rows of a style list of the eye color, face mark, scar, or tattoo database: the name, enabled, the images (by gender
    when the database has one for each), and the maps of the skin (objects/...)."""
    out = []
    for entry in rows(player.parse_db(name), list_name):
        found = [t.lower() for t in texts(entry)]
        images = [t for t in found if t.startswith("lyshineui/images/")]
        record = {"name": player.text_of(entry.child(player.F_NAME)), "enabled": enabled(entry),
                  "maps": [t for t in found if t.startswith("objects/")]}
        female = [t for t in images if "-female" in t.rsplit("/", 1)[-1]]
        male = [t for t in images if "-male" in t.rsplit("/", 1)[-1]]
        if female or male:
            record["images"] = {"Female": female[0] if female else "", "Male": male[0] if male else ""}
        elif images:
            record["image"] = images[0]
        out.append(record)
    return out


def hair(name: str) -> dict:
    root = player.parse_db(name)
    styles_out = []
    for entry in rows(root, "Hair Styles"):
        key = entry.child(F_STYLE_IMAGE_KEY)
        styles_out.append({"name": player.text_of(entry.child(player.F_NAME)), "imageKey": player.text_of(key).lower(),
                           "enabled": enabled(entry)})
    colors = []
    for entry in rows(root, "Hair Colors"):
        images = [t.lower() for t in texts(entry) if t.lower().startswith("lyshineui/images/")]
        colors.append({"name": player.text_of(entry.child(player.F_NAME)), "image": images[0] if images else ""})
    return {"styles": styles_out, "colors": colors}


def tones(name: str) -> list[dict]:
    out = []
    for entry in rows(player.parse_db(name), "Skin Tones"):
        images = [t.lower() for t in texts(entry) if t.lower().startswith("lyshineui/images/")]
        out.append({"name": player.text_of(entry.child(player.F_NAME)), "image": images[0] if images else "",
                    "enabled": enabled(entry)})
    return out


def races(gender: str) -> list[dict]:
    out = []
    for entry in rows(player.parse_db(f"{player.GENDERS[gender]}.charcreatedb"), "Races"):
        images = [t.lower() for t in texts(entry) if t.lower().startswith("lyshineui/images/")]
        out.append({"name": player.text_of(entry.child(player.F_NAME)),
                    "tones": player.db_name(player.asset_path(entry.child(player.F_RACE_TONE_DB))),
                    "hair": player.db_name(player.asset_path(entry.child(player.F_RACE_HAIR_DB))),
                    "facial": player.db_name(player.asset_path(entry.child(player.F_RACE_FACIAL_DB))),
                    "defaultTone": player.text_of(entry.child(player.F_RACE_DEFAULT_TONE)),
                    "image": images[0] if images else "", "enabled": enabled(entry)})
    return out


def archetypes() -> list[dict]:
    """The rows of javelindata_archetypes in the order of the table, with the id of the buttons of cacarchetypestabv2."""
    out = []
    for row in datasheet.load(ARCHETYPES).rows():
        record = {key: (int(value) if isinstance(value, float) and value.is_integer() else value) for key, value in row.items()
                  if value not in ("", None)}
        record["id"] = zlib.crc32(str(row["ArchetypeId"]).lower().encode())
        for key in [k for k in record if k.startswith("ProficiencyImage_")]:
            record[key] = str(record[key]).lower()
        out.append(record)
    return out


def write() -> dict:
    genders = {gender: {"races": races(gender)} for gender in player.GENDERS}
    tone_dbs = sorted({race["tones"] for g in genders.values() for race in g["races"] if race["tones"]})
    hair_dbs = sorted({race[k] for g in genders.values() for race in g["races"] for k in ("hair", "facial") if race[k]})
    doc = {"format": 1, "archetypes": archetypes(), "genders": genders,
           "tones": {name: tones(name) for name in tone_dbs}, "hair": {name: hair(name) for name in hair_dbs},
           "eyeColors": styles("eyecolors.eyecolordb", "Eye Colors"), "faceMarks": styles("facemarks.facemarkdb", "Face Mark Styles"),
           "scars": styles("scars.scardb", "Scar Styles"), "tattoos": styles("tattoos.tattoodb", "Tattoo Styles"),
           "tattooColors": styles("tattoos.tattoodb", "Tattoo Colors")}
    # The images of the screen: each creation image of the game, and the proficiency images of the archetypes.
    images = sorted(p for p in source.paks().paths(IMAGE_ROOT, ".dds"))
    images += sorted({a[k] for a in doc["archetypes"] for k in a if k.startswith("ProficiencyImage_")})
    path = out_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1))
    wanted = wanted_path()
    wanted.parent.mkdir(parents=True, exist_ok=True)
    wanted.write_text(json.dumps({"textures": images}, indent=0))
    return {"archetypes": len(doc["archetypes"]), "races": {g: len(v["races"]) for g, v in genders.items()},
            "tone databases": len(tone_dbs), "hair databases": len(hair_dbs), "eye colors": len(doc["eyeColors"]),
            "face marks": len(doc["faceMarks"]), "scars": len(doc["scars"]), "tattoos": len(doc["tattoos"]),
            "tattoo colors": len(doc["tattooColors"]), "images": len(images), "output": str(path), "wanted": str(wanted)}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-convert-creation", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("write")
    parser.parse_args(argv)
    print(json.dumps(write(), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
