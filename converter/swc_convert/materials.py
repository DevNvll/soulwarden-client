"""Materials (.mtl, CryEngine XML) and their textures (split .dds) -> PNG files and a material description.

Proved on the game copy (research AST-135): the texture slots Diffuse, Bumpmap (the _ddna normal, BC5 signed,
with the gloss as a second texture in the .a parts), Specular, Smoothness, Emittance, Custom, Decal; the shader
names Illum, HumanSkin, Nodraw, ...
"""
from __future__ import annotations

import hashlib
import json
import os
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

from . import dds, env, source

SLOTS = {"Diffuse": "base", "Bumpmap": "normal", "Specular": "specular", "Emittance": "emissive"}


NOT_DRAWN_SHADERS = {"nodraw", "geometrybeam"}   # collision (both spellings are in the game data), light beams


def not_drawn(material: ET.Element) -> bool:
    """A sub-material that the client does not draw as a surface: Nodraw or NoDraw (collision), a shadow proxy (it only
    casts shadows), a light beam or fog volume (GeometryBeam), or a decal (%DECAL: the game draws it over another
    surface; as a surface of its own it would hide that surface)."""
    return (material.get("Shader", "").lower() in NOT_DRAWN_SHADERS or "shadow_proxy" in material.get("Name", "").lower()
            or "%DECAL" in material.get("StringGenMask", "").upper())


def is_glass(material: ET.Element) -> bool:
    """A transparent sub-material (shader Glass): the glTF output makes it blend; the previews leave it out."""
    return material.get("Shader", "") == "Glass"


def load_mtl(reference: str, mesh_path: str = "") -> tuple[str, list[ET.Element]]:
    """@return (mtl path, sub-materials) of a material reference: a GUID text, a path, or a file name of the folder
    of the mesh (merged meshes of a slice)."""
    if reference.startswith("{"):
        path = source.product_of_text(reference, ".mtl")
    else:
        path = reference.lower().replace("\\", "/")
        if not path.endswith(".mtl"):
            path += ".mtl"
        if "/" not in path and mesh_path:
            path = mesh_path.rsplit("/", 1)[0] + "/" + path
    if path is None:
        raise FileNotFoundError(f"material {reference}")
    root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
    subs = root.find("SubMaterials")
    return path, (list(subs) if subs is not None else [root])


def texture_png(product: str, max_size: int, normal: bool = False, alpha: bool = False) -> Path:
    """Writes one texture product as PNG into the content folder (once). @return The PNG path."""
    suffix = "_gloss" if alpha else ""
    out = env.content_dir() / "textures" / (product[:-4] + suffix + ".png")
    if out.is_file():
        return out
    data, w, h, fmt = dds.join(source.read, product, max_size, alpha)
    img = dds.to_image(data, w, h, fmt)
    if normal:
        arr = np.asarray(img.convert("RGB")).copy()
        arr[:, :, 1] = 255 - arr[:, :, 1]   # engine green is down in UV space (inference); glTF wants up
        img = Image.fromarray(arr)
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    return out


def texture_product(texture: ET.Element) -> str | None:
    """@return The pak path of the .dds of a texture element: its asset id, or (an older material with only a file path,
    for example a .tif name) the file path with the extension .dds when the pak has it."""
    product = source.product_of_text(texture.get("AssetId"), ".dds") if texture.get("AssetId") else None
    if product is None and texture.get("File"):
        candidate = texture.get("File").lower().replace("\\", "/").rsplit(".", 1)[0] + ".dds"
        product = candidate if source.has(candidate) else None
    return product


def describe(material: ET.Element, max_size: int = 1024) -> dict:
    """@return A description of one sub-material with the PNG paths of its main textures."""
    info = {"name": material.get("Name", ""), "shader": material.get("Shader", ""),
            "flags": material.get("MtlFlags", ""), "genMask": material.get("StringGenMask", ""),
            "diffuse": material.get("Diffuse", ""), "specular": material.get("Specular", ""),
            "opacity": material.get("Opacity", ""), "alphaTest": material.get("AlphaTest", ""),
            "emittance": material.get("Emittance", ""), "textures": {}, "files": {}}
    for t in material.iter("Texture"):
        slot = t.get("Map", "")
        info["textures"][slot] = t.get("File", "")
        if slot not in SLOTS:
            continue
        product = texture_product(t)
        if product is None:
            continue
        try:
            png = texture_png(product, max_size if slot == "Diffuse" else min(max_size, 1024), normal=(slot == "Bumpmap"))
            info["files"][SLOTS[slot]] = str(png)
            if slot == "Bumpmap" and source.has(product + ".a"):
                info["files"]["gloss"] = str(texture_png(product, min(max_size, 1024), alpha=True))
        except Exception as error:   # a texture that does not decode: the material stays with no such map
            info.setdefault("errors", []).append(f"{slot}: {error}")
    return info


def relative(target: str, gltf_path: Path) -> str:
    return os.path.relpath(target, gltf_path.parent)


# Shared assets (stage 1.2): each texture is one Unreal asset, and each material is one material instance of a base
# material, for all meshes. A material is the same for each mesh with the same textures and rules (its key is a hash).

TEXTURE_CAPS = {"base": 1024, "normal": 1024}   # the largest size of each role (the look is not a goal now)
TEXTURE_VERSION = 2   # the rule of the shared PNGs (2: a base color with an alpha of 255 everywhere is RGB)
TWO_SIDED = 0x2                                  # MtlFlags: MTL_FLAG_2SIDED (CryEngine)


def shared_texture(product: str, role: str, opacity: str | None = None) -> dict:
    """Writes a texture product as PNG for its own Unreal asset (once for each size cap) and @return its record.
    A normal map keeps the green channel of the engine (Unreal uses the same direction; the glTF PNGs flip it).
    `opacity`: the product of the Opacity map of a masked material: it becomes the alpha of the base color (the masked
    base material takes its mask from the alpha of the base color)."""
    cap = TEXTURE_CAPS[role]
    name = product[:-4] + ("+" + opacity.rsplit("/", 1)[-1][:-4] if opacity else "") + ".png"
    out = env.content_dir() / "textures" / f"v{TEXTURE_VERSION}-{cap}" / name
    if not out.is_file():
        data, w, h, fmt = dds.join(source.read, product, cap)
        img = dds.to_image(data, w, h, fmt)
        if opacity:
            data, ow, oh, ofmt = dds.join(source.read, opacity, cap)
            mask = dds.to_image(data, ow, oh, ofmt).convert("L").resize(img.size)
            img = img.convert("RGB")
            img.putalpha(mask)
        elif role == "normal" or (img.mode == "RGBA" and int(np.asarray(img)[:, :, 3].min()) == 255):
            img = img.convert("RGB")   # no alpha: Unreal compresses it as DXT1 (a 1-bit alpha of BC1 stays)
        out.parent.mkdir(parents=True, exist_ok=True)
        temporary = out.with_name(f".{out.name}.{os.getpid()}.png")   # workers can write the same texture: atomic
        img.save(temporary)
        os.replace(temporary, out)
    key = "texture:" + product + ("+opacity:" + opacity if opacity else "")
    return {"kind": "texture", "asset": key, "file": str(out.relative_to(env.content_dir())), "role": role, "cap": cap}


def shared_material(material: ET.Element | None, normal_map: bool = True) -> tuple[dict, list[dict]]:
    """@return (the material record, the texture records) of one sub-material. The record name is the key of the
    material: "m_" + 10 hex digits of the hash of its rules. A missing sub-material is the default (opaque, white)."""
    rules: dict = {"blend": "opaque", "two sided": False, "factor": [1.0, 1.0, 1.0, 1.0]}
    textures = []
    if material is not None:
        if is_glass(material):
            rules["blend"] = "translucent"
            rules["factor"] = [0.8, 0.85, 0.9, 0.25]
        else:
            try:
                cutoff = float(material.get("AlphaTest", "0") or 0)
            except ValueError:
                cutoff = 0.0
            if cutoff > 0:
                rules["blend"] = "masked"
                rules["cutoff"] = round(cutoff, 3)
        try:
            rules["two sided"] = bool(int(material.get("MtlFlags", "0") or 0) & TWO_SIDED)
        except ValueError:
            pass
        products: dict[str, str] = {}
        for t in material.iter("Texture"):
            role = {"Diffuse": "base", "Bumpmap": "normal", "Opacity": "opacity"}.get(t.get("Map", ""))
            if role is not None and role not in products:
                product = texture_product(t)
                if product is not None:
                    products[role] = product
        opacity = products.get("opacity") if rules["blend"] == "masked" else None
        for role in ("base", "normal"):
            if role not in products or (role == "normal" and not normal_map):
                continue
            try:
                record = shared_texture(products[role], role, opacity if role == "base" else None)
                textures.append(record)
                rules[role] = record["asset"]
            except Exception as error:   # a texture that does not decode: the material has no such map
                rules.setdefault("errors", []).append(f"{role}: {str(error)[:80]}")
    errors = rules.pop("errors", None)
    key = "m_" + hashlib.sha1(json.dumps(rules, sort_keys=True).encode()).hexdigest()[:10]
    record = {"kind": "material", "asset": "material:" + key, "name": key, **rules}
    if errors:
        record["errors"] = errors
    return record, textures
