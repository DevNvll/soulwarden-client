"""Materials (.mtl, CryEngine XML) and their textures (split .dds) -> PNG files and a material description.

Proved on the game copy (research AST-135): the texture slots Diffuse, Bumpmap (the _ddna normal, BC5 signed,
with the gloss as a second texture in the .a parts), Specular, Smoothness, Emittance, Custom, Decal; the shader
names Illum, HumanSkin, Nodraw, ...
"""
from __future__ import annotations

import os
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

from . import dds, env, source

SLOTS = {"Diffuse": "base", "Bumpmap": "normal", "Specular": "specular", "Emittance": "emissive"}


def not_drawn(material: ET.Element) -> bool:
    """A sub-material that the game does not draw: Nodraw (collision) or a shadow proxy (it only casts shadows)."""
    return material.get("Shader", "") == "Nodraw" or "shadow_proxy" in material.get("Name", "").lower()


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
        if slot not in SLOTS or not t.get("AssetId"):
            continue
        product = source.product_of_text(t.get("AssetId"), ".dds")
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
