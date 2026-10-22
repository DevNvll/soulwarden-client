"""The materials of the skinned bodies (AST-227, AST-233; docs/MATERIALS.md "The skinned bodies").

The converters of the creatures (creature.py), the player parts (player.py; the persons wear the same parts), the weapon
looks and their accessories (player.py weapons.json: the weapons, shields, quivers, and tools), and the characters write a glTF material for each drawn sub-material with only the base color, the normal map, and some of the
alpha rules. The source shades these sub-materials (Illum, HumanSkin, Hair) with the same reflectance and overlay masks as
the world (charactermasking.cfib, research world-materials.md section 3). This module gives each glTF material of an
imported body the spec of the world rule (material_records.surface_spec: the XML Diffuse, the F0 split of the specular map
and the XML Specular, the gloss and Shininess, the overlay masks, two sided, the cutoff). Scripts/swc_materials.py
apply_bodies sets the glTF material instances of the body folders from these specs: the meshes and their slots do not
change, and no body is imported again.

Usage (tools/convert-materials.sh, through material_records.main): bodies [FILTER]
Output: SWC_CONTENT/bodies/instances.json (the spec of each render key), textures.json (the texture records), and
slots.json (the Unreal folder of each body -> the glTF material name -> the render key).
"""
from __future__ import annotations

import hashlib
import json
import time
import xml.etree.ElementTree as ET
from pathlib import Path

from . import env, manifest, materials, source
from . import material_records as mr

VERSION = 1
# The shader families of the bodies that take the surface parents. The anisotropic highlight of Hair and the subsurface
# light of HumanSkin are not in the parents (a limit): their diffuse, F0, gloss, cutoff, and overlay masks are.
SHADERS = ("illum", "humanskin", "hair")
HAIR_F0 = 0.08   # the largest dielectric F0 of the surface parents (Unreal: 0.08 x Specular)


def out_dir() -> Path:
    path = env.content_dir() / "bodies"
    path.mkdir(parents=True, exist_ok=True)
    return path


def body_spec(mtl: str, index: int, element: ET.Element, use) -> tuple[str, dict] | None:
    """@return (the key: a hash of the spec, the spec) of one drawn sub-material of a body, or None: the glTF rule stays (glass, a
    shader that is not in SHADERS). A transparent head layer (player.HEAD_LAYER: the lashes, the eye shadow) takes the
    translucent surface parent: opacity = the diffuse alpha x the XML Opacity."""
    from .player import head_layer
    if materials.not_drawn(element) or element.get("Shader", "").lower() not in SHADERS:
        return None
    record = mr.source_record(mtl, index, element)
    rules = mr.render_rules(record)
    if rules.get("missing"):
        return None
    layer = head_layer(element)
    if layer:
        rules = {**rules, "blend": "translucent", "opacity": record["lighting"].get("Opacity", 1.0)}
        rules.pop("cutoff", None)
    spec = mr.surface_spec(rules, use)
    if rules["shader"] == "hair":
        # The Hair shader has no metal: its Specular is the color of the anisotropic highlight [I]. The F0 split of the parent
        # made a beard of Specular 1.0 (no specular map) a white metal. The F0 stays at the dielectric limit of the parent.
        color = spec["vectors"]["SpecularColor"][:3]
        light = mr.luminance(color)
        if light > HAIR_F0:
            spec["vectors"]["SpecularColor"] = [v * HAIR_F0 / light for v in color] + [1.0]
    if layer:
        # A head layer is a decal layer of the head (the head material has %HAS_DECAL_LAYERS): it changes the albedo only
        # [I]. With its XML Specular 1.0 and no specular map the F0 split made the eye shadow a white mirror.
        spec["vectors"]["SpecularColor"] = [0.0, 0.0, 0.0, 1.0]
        spec["textures"].pop("Specular", None)
    if rules["blend"] == "translucent":
        spec["parent"] = "surface translucent"
        spec["scalars"]["Opacity"] = float(rules.get("opacity", 1.0))
    key = "b_" + hashlib.sha1(json.dumps(spec, sort_keys=True).encode()).hexdigest()[:10]
    spec["source"] = f"{mtl}#{index}"   # the first source of the key (not in the key)
    return key, spec


def gltf_material_names(path: Path) -> list[str]:
    return [m.get("name", "") for m in json.loads(path.read_text()).get("materials", [])]


def skin_materials(skins: list[tuple[str, str]]) -> dict[str, tuple[str, int, ET.Element]]:
    """@return The glTF material name of each sub-material of the skins (skin path, .mtl reference) -> (mtl path, index,
    element): the name that the converters give a glTF material (the Name of the sub-material). The first skin wins a
    name that two skins share."""
    out: dict[str, tuple[str, int, ET.Element]] = {}
    for skin, reference in skins:
        if not reference:
            continue
        try:
            mtl, subs = materials.load_mtl(reference, skin)
        except Exception:
            continue
        for index, element in enumerate(subs):
            out.setdefault(element.get("Name", ""), (mtl.lower(), index, element))
    return out


def creature_bodies() -> list[tuple[str, Path, list[tuple[str, str]]]]:
    """@return (Unreal folder, glTF file, skins) of each imported creature body: the base body of a skeleton (its glTF is
    the skeleton file) and each other body or material variant (creature.py: a variant puts its .mtl on all skins)."""
    census = json.loads((env.content_dir() / "creatures" / "census.json").read_text())["bodies"]
    out = []
    for key, entry in manifest.load()["assets"].items():
        if entry.get("kind") != "character":
            continue
        if key.startswith("creature-skeleton:"):
            body_key = entry.get("body", "")
        elif key.startswith("creature-body:"):
            body_key = key.split(":", 1)[1]
        else:
            continue
        path, _, variant = body_key.partition("|")
        body = census.get(path)
        if body is None:
            continue   # a weapon body (creature.py weapon_bodies): not in the census
        skins = [(s["skin"], variant or s.get("material") or body.get("material", "")) for s in body["skins"]]
        out.append((entry["unreal"], env.content_dir() / entry["file"], skins))
    return out


def player_parts() -> list[tuple[str, Path, list[tuple[str, str]]]]:
    """@return (Unreal folder, glTF file, skins) of each converted player part (the key SKIN|MTL; the persons wear them)."""
    path = env.content_dir() / "player" / "manifest.json"
    if not path.is_file():
        return []
    out = []
    for key, entry in json.loads(path.read_text())["assets"].items():
        if entry.get("kind") != "part":
            continue
        skin, _, mtl = key.partition("|")
        if skin == "person":   # a person body of its own character definition (character.py): the skins of the CDF
            from .creature import read_cdf
            try:
                cdf = read_cdf(mtl)
            except Exception:
                continue
            skins = [(s["skin"], s.get("material") or cdf.get("material", "")) for s in cdf["skins"]]
        else:
            skins = [(skin, mtl)]
        out.append((entry["unreal"], env.content_dir() / entry["file"], skins))
    return out


def weapon_looks() -> list[tuple[str, Path, list[tuple[str, str]]]]:
    """@return (Unreal folder, glTF file, skins) of each converted weapon look and accessory of weapons.json (the weapons,
    shields, quivers and their arrows, and tools of the player and the persons; research weapon-materials.md section 1). One
    look serves each item with its mesh and skins: the mask values of the appearance row of an item go on at run time
    (SwcPlayerLooks: the dynamic instances of the item), not into these specs."""
    path = env.content_dir() / "player" / "weapons.json"
    known_path = env.content_dir() / "player" / "manifest.json"
    if not path.is_file() or not known_path.is_file():
        return []
    known = json.loads(known_path.read_text())["assets"]
    looks: dict[str, list] = {}
    for item in json.loads(path.read_text())["items"].values():
        for look in (item, item.get("accessory")):
            if look and look.get("key") in known and not look.get("playerModel"):
                looks.setdefault(look["key"], [tuple(pair) for pair in look["skins"]])
    return [(known[key]["unreal"], env.content_dir() / known[key]["file"], skins) for key, skins in sorted(looks.items())]


def write(wanted: str = "") -> dict:
    """Writes instances.json, textures.json, and slots.json: all bodies, or (`wanted`) the bodies whose Unreal folder
    contains this text, merged into the files of an earlier call."""
    started = time.time()
    textures: dict = {}

    def use(record: dict) -> str:
        textures[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
        return record["asset"]

    specs: dict = {}
    slots: dict = {}
    out = out_dir()
    if wanted and (out / "slots.json").is_file():
        # A filtered call updates the bodies of the filter and keeps the others.
        specs = json.loads((out / "instances.json").read_text())["instances"]
        textures.update(json.loads((out / "textures.json").read_text())["textures"])
        slots = {k: v for k, v in json.loads((out / "slots.json").read_text())["bodies"].items() if wanted not in k}
    counts = {"bodies": 0, "slots": 0, "with a spec": 0, "no source": 0, "glTF rule": 0, "errors": 0}
    errors: dict = {}
    for folder, gltf_file, skins in creature_bodies() + player_parts() + weapon_looks():
        if wanted and wanted not in folder or not gltf_file.is_file():
            continue
        counts["bodies"] += 1
        names = skin_materials(skins)
        table = {}
        for name in gltf_material_names(gltf_file):
            counts["slots"] += 1
            if name in table:
                continue
            found = names.get(name)
            if found is None:
                counts["no source"] += 1
                continue
            try:
                result = body_spec(*found, use)
            except Exception as error:
                counts["errors"] += 1
                errors.setdefault(f"{found[0]}#{found[1]}", str(error)[:160])
                continue
            if result is None:
                counts["glTF rule"] += 1
                continue
            key, spec = result
            specs[key] = spec
            table[name] = key
            counts["with a spec"] += 1
        if table:
            slots[folder] = table
    for key, record in textures.items():
        record["unreal"] = manifest.unreal_folder("texture", key)
    mr.write_json(out / "instances.json", {"version": VERSION, "instances": specs})
    mr.write_json(out / "textures.json", {"version": VERSION, "textures": textures})
    mr.write_json(out / "slots.json", {"version": VERSION, "bodies": slots})
    known = manifest.load()["assets"]
    by_parent: dict = {}
    for spec in specs.values():
        by_parent[spec["parent"]] = by_parent.get(spec["parent"], 0) + 1
    return {**counts, "specs": len(specs), "by parent": by_parent, "textures": len(textures),
            "new textures (not in the manifest)": sum(1 for k in textures if k not in known),
            "first errors": dict(list(errors.items())[:5]), "seconds": round(time.time() - started, 1)}


def main(argv: list[str]) -> dict:
    return write(argv[0] if argv else "")
