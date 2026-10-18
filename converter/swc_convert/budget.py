"""The disk budget of the world (stage 1.2): unit costs measured on imported regions, scaled to the census.

Measured on the files: the plugin assets of the imported meshes, textures, and materials (SWC_GAME_CONTENT and its
import-state.json), their converted files (glTF + bin, PNG), and the triangles and texels of the census. The derived
data cache of the engine is shared with other projects of the user: the growth during the import is an argument
(`--ddc-bytes`). The textures build into the cache only when they render (or cook): their cache size is computed
from the block formats (DXT1 0.5 byte, DXT5 and BC5 1 byte for each texel, 4/3 for the mips).
"""
from __future__ import annotations

import json
import os
import struct
from pathlib import Path

from . import env, manifest

MIPS = 4.0 / 3.0


def folder_bytes(path: Path) -> int:
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file()) if path.is_dir() else 0


def png_size(path: Path) -> tuple[int, int, bool]:
    """@return (width, height, alpha) from the header of a PNG."""
    with open(path, "rb") as handle:
        head = handle.read(26)
    width, height = struct.unpack(">II", head[16:24])
    return width, height, head[25] in (4, 6)


def plugin_folder(unreal_path: str) -> Path:
    return Path(os.environ.get("SWC_GAME_CONTENT") or env.work_dir() / "unreal" / "SwcGameContent") / "Content" / \
        unreal_path.removeprefix(manifest.ROOT + "/")


def run(ddc_bytes: int | None = None) -> dict:
    content = env.content_dir()
    doc = manifest.load()["assets"]
    plugin = Path(os.environ.get("SWC_GAME_CONTENT") or env.work_dir() / "unreal" / "SwcGameContent")
    state = json.loads((plugin / "import-state.json").read_text())
    census_meshes = {r["mesh"]: r for r in json.loads((content / "census" / "meshes.json").read_text())}
    world = json.loads((content / "census" / "world-census.json").read_text())
    m = {"meshes": 0, "triangles": 0, "mesh bytes": 0, "gltf bytes": 0, "textures": 0, "texels": 0, "texture bytes": 0,
         "png bytes": 0, "texture cache bytes": 0.0, "materials": 0, "material bytes": 0}
    for key in state:
        entry = doc.get(key)
        if entry is None:
            continue
        if entry["kind"] == "static":
            m["meshes"] += 1
            m["triangles"] += census_meshes.get(key, {}).get("drawn triangles", 0)
            m["mesh bytes"] += folder_bytes(plugin_folder(entry["unreal"]))
            gltf = content / entry["file"]
            m["gltf bytes"] += gltf.stat().st_size + gltf.with_suffix(".bin").stat().st_size
        elif entry["kind"] == "texture":
            png = content / entry["file"]
            width, height, alpha = png_size(png)
            m["textures"] += 1
            m["texels"] += width * height
            m["texture bytes"] += folder_bytes(plugin_folder(entry["unreal"]))
            m["png bytes"] += png.stat().st_size
            m["texture cache bytes"] += width * height * MIPS * (1.0 if alpha or entry["role"] == "normal" else 0.5)
        elif entry["kind"] == "material":
            m["materials"] += 1
            asset = plugin / "Content" / "Materials" / (entry["name"] + ".uasset")
            m["material bytes"] += asset.stat().st_size if asset.is_file() else 0
    unit = {"mesh asset bytes / triangle": m["mesh bytes"] / max(m["triangles"], 1),
            "glTF bytes / triangle": m["gltf bytes"] / max(m["triangles"], 1),
            "texture asset bytes / texel": m["texture bytes"] / max(m["texels"], 1),
            "PNG bytes / texel": m["png bytes"] / max(m["texels"], 1),
            "texture cache bytes / texel": m["texture cache bytes"] / max(m["texels"], 1),
            "material asset bytes": m["material bytes"] / max(m["materials"], 1)}
    if ddc_bytes is not None:
        unit["mesh cache bytes / triangle"] = ddc_bytes / max(m["triangles"], 1)
    triangles = world["unique mesh triangles (LOD 0, drawn)"]
    texels = json.loads((content / "census" / "texels.json").read_text()) if (content / "census" / "texels.json").is_file() else {}
    world_texels = texels.get("base 1024", 0) + texels.get("normal 1024", 0)
    materials = round(m["materials"] * world["unique meshes"] / max(m["meshes"], 1))   # an upper bound
    gb = 1e9
    estimate = {
        "converted content: glTF": triangles * unit["glTF bytes / triangle"] / gb,
        "converted content: PNG": world_texels * unit["PNG bytes / texel"] / gb,
        "converted content: placements (census)": folder_bytes(content / "regions") / gb,
        "plugin: meshes": triangles * unit["mesh asset bytes / triangle"] / gb,
        "plugin: textures": world_texels * unit["texture asset bytes / texel"] / gb,
        "plugin: materials": materials * unit["material asset bytes"] / gb,
        "cache: meshes": triangles * unit.get("mesh cache bytes / triangle", 0.0) / gb,
        "cache: textures": world_texels * unit["texture cache bytes / texel"] / gb,
    }
    estimate = {k: round(v, 1) for k, v in estimate.items()}
    estimate["total (GB)"] = round(sum(estimate.values()), 1)
    out = {"measured": m, "unit costs": {k: round(v, 3) for k, v in unit.items()},
           "world": {"triangles": triangles, "texels at 1024 (base + normal)": world_texels, "materials (upper bound)": materials},
           "estimate GB": estimate}
    (content / "census" / "budget.json").write_text(json.dumps(out, indent=1))
    return out
