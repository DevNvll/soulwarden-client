"""Writes again the PNG textures of the creature and object outputs whose source is a BC4 or BC5 texture, after a change of
the BC4/BC5 decoder of dds.py (1eaed5a: the interpolation weights): the normal maps (BC5) and the opacity masks (BC4). The
converters write a PNG only once (materials.texture_png, materials.shared_texture): they do not see a change of the decoder.

Usage: python3 -B -m swc_convert.retexture creatures|objects [--dry-run]
  creatures  the textures of the creature bodies (the PNGs of their glTF files) and of the creature weapons (shared records)
  objects    the shared textures of the depleted node meshes (content/world-objects/depleted-meshes.json)

Each PNG is written as the converter writes it, with the fixed decoder, and replaces the old file at once (a temporary file and
a rename: a reader never sees half a file). The keys of the manifest to import again go to
SWC_CONTENT/retexture/KIND-keys.json (tools/import.sh --keys FILE --force): the creature bodies whose glTF uses a new PNG, and the
shared texture records.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from . import dds, env, source
from . import manifest as shared_manifest

BC45 = (80, 81, 83, 84)


def replace(out: Path, image: Image.Image) -> None:
    temporary = out.with_name(f".{out.name}.{os.getpid()}.png")
    image.save(temporary)
    os.replace(temporary, out)


def body_texture(product: str, size: int, normal: bool) -> Image.Image | None:
    """@return The PNG of materials.texture_png for a BC4/BC5 product (a normal map: green up for glTF), or no value for another
    format."""
    data, w, h, fmt = dds.join(source.read, product, size)
    if fmt not in BC45:
        return None
    image = dds.to_image(data, w, h, fmt)
    if normal:
        array = np.asarray(image.convert("RGB")).copy()
        array[:, :, 1] = 255 - array[:, :, 1]
        image = Image.fromarray(array)
    return image


def shared_texture(key: str, record: dict) -> Image.Image | None:
    """@return The PNG of materials.shared_texture for a record whose product or opacity is BC4/BC5, or no value."""
    product, _, opacity = key[len("texture:"):].partition("+opacity:")
    cap = int(record.get("cap", 1024))
    data, w, h, fmt = dds.join(source.read, product, cap)
    opacity_data = dds.join(source.read, opacity, cap) if opacity else None
    if fmt not in BC45 and (opacity_data is None or opacity_data[3] not in BC45):
        return None
    image = dds.to_image(data, w, h, fmt)
    if opacity_data is not None:
        mask = dds.to_image(*opacity_data).convert("L").resize(image.size)
        image = image.convert("RGB")
        image.putalpha(mask)
    elif record.get("role") == "normal" or (image.mode == "RGBA" and int(np.asarray(image)[:, :, 3].min()) == 255):
        image = image.convert("RGB")
    return image


def gltf_images(path: Path) -> dict[Path, bool]:
    """@return The PNG files of a glTF file, each with True for a normal map."""
    doc = json.loads(path.read_text())
    images = doc.get("images", [])
    textures = doc.get("textures", [])
    normals = set()
    for material in doc.get("materials", []):
        normal = material.get("normalTexture")
        if normal is not None and normal.get("index", -1) < len(textures):
            normals.add(textures[normal["index"]].get("source"))
    out = {}
    for index, image in enumerate(images):
        if "uri" in image:
            out[(path.parent / image["uri"]).resolve()] = index in normals
    return out


def product_of(png: Path) -> str | None:
    """@return The pak path of the source of a PNG of materials.texture_png (content/textures/PRODUCT.png), or no value."""
    root = (env.content_dir() / "textures").resolve()
    try:
        relative = png.resolve().relative_to(root).as_posix()
    except ValueError:
        return None
    if relative.startswith("v") and "/" in relative and relative.split("/", 1)[0][1:].replace("-", "").isdigit():
        return None   # a shared texture: its record has the product
    return relative[:-len(".png")] + ".dds" if relative.endswith(".png") and not relative.endswith("_gloss.png") else None


def run(kind: str, dry_run: bool) -> dict:
    assets = shared_manifest.load()["assets"]
    keys: set[str] = set()
    counts = {"bodies": 0, "bodyPngs": 0, "bodyPngsRewritten": 0, "sharedTextures": 0, "sharedRewritten": 0, "failed": []}
    shared: set[str] = set()
    if kind == "creatures":
        rewritten: dict[Path, bool] = {}
        for key, entry in assets.items():
            if not key.startswith("creature-body:"):
                continue
            path = env.content_dir() / entry["file"]
            if not path.is_file():
                continue
            counts["bodies"] += 1
            for png, normal in gltf_images(path).items():
                if png not in rewritten:
                    rewritten[png] = False
                    product = product_of(png)
                    counts["bodyPngs"] += 1
                    if product is None or not png.is_file():
                        continue
                    try:
                        size = max(Image.open(png).size)
                        image = body_texture(product, size, normal)
                    except Exception as error:
                        counts["failed"].append(f"{png.name}: {error}")
                        continue
                    if image is not None:
                        rewritten[png] = True
                        counts["bodyPngsRewritten"] += 1
                        if not dry_run:
                            replace(png, image)
                if rewritten[png]:
                    keys.add(key)
        for key, entry in assets.items():
            if key.startswith("creature-weapon:"):
                for material in entry.get("materials", []):
                    record = assets.get("material:" + material, {})
                    shared.update(record.get(role) for role in ("base", "normal") if record.get(role))
    else:
        depleted = json.loads((env.content_dir() / "world-objects" / "depleted-meshes.json").read_text())
        for mesh in depleted:
            for material in assets.get(mesh, {}).get("materials", []):
                record = assets.get("material:" + material, {})
                shared.update(record.get(role) for role in ("base", "normal") if record.get(role))
    for key in sorted(shared):
        record = assets.get(key)
        if record is None:
            continue
        counts["sharedTextures"] += 1
        out = env.content_dir() / record["file"]
        try:
            image = shared_texture(key, record) if out.is_file() else None
        except Exception as error:
            counts["failed"].append(f"{key}: {error}")
            continue
        if image is not None:
            counts["sharedRewritten"] += 1
            keys.add(key)
            if not dry_run:
                replace(out, image)
    counts["keys"] = len(keys)
    if not dry_run:
        folder = env.content_dir() / "retexture"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"{kind}-keys.json").write_text(json.dumps(sorted(keys), indent=0))
        counts["keysFile"] = str(folder / f"{kind}-keys.json")
    counts["failedCount"] = len(counts["failed"])
    counts["failed"] = counts["failed"][:10]
    return counts


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-retexture", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kind", choices=["creatures", "objects"])
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    print(json.dumps(run(args.kind, args.dry_run), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
