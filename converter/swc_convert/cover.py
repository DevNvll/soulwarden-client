"""The ground cover of the regions for the client (world fidelity T3; research/terrain.md, section 3).

region.vegetation of a region has a descriptor table (the mesh, the material override, AutoMerge, Bending / 100) and
128 x 128 tiles of 16 m with groups of packed records (16 bytes: the quaternion u32, the local x and y u16, the z u16,
the scale u8, a spare u8, the output ordinal u16, a spare u16). The cover stays packed: the client (USwcGroundCover)
reads the records of the tiles near the view and places them (no Landscape Grass, no new scatter).

Output: regions/NAME/cover.bin (little-endian): "SWCV", u32 version 1, u32 descriptor count; each descriptor: u16 length +
UTF-8 object path of the static mesh (FOLDER/STEM/StaticMeshes/STEM.STEM, found in the plugin on disk; empty when the mesh
is not imported: the client skips it), u16 slot count and for each slot the slot name and the material key of the
override (cover/pairs.json of cover_materials.py; each u16 length + UTF-8), f32 bending (the custom primitive data 0 "SwcWind" of the components), u8 AutoMerge; then u64 length and the
bytes of region.vegetation, unchanged (its tile offsets count from the start of these bytes).
"""
from __future__ import annotations

import json
import os
import struct
from pathlib import Path

from . import cover_materials, env, manifest, region, source

VERSION = 1


def text(value: str) -> bytes:
    data = value.encode()
    return struct.pack("<H", len(data)) + data


def object_path(folder: str) -> str:
    """@return The object path of the static mesh of an imported mesh folder (/SwcGameContent/...), or "" when the plugin
    has none (the glTF import makes FOLDER/STEM/StaticMeshes/STEM)."""
    root = Path(os.environ.get("SWC_GAME_CONTENT") or env.work_dir() / "unreal" / "SwcGameContent") / "Content"
    found = sorted((root / folder[len("/SwcGameContent/"):]).glob("*/StaticMeshes/*.uasset"))
    if not found:
        return ""
    asset = found[0]
    package = "/SwcGameContent/" + str(asset.relative_to(root).with_suffix(""))
    return f"{package}.{asset.stem}"


def write(name: str) -> dict:
    """Writes regions/NAME/cover.bin. @return A report (None for a region with no region.vegetation)."""
    path = region.WORLD + "regions/" + name + "/region.vegetation"
    if not source.has(path):
        return {"region": name, "cover": False}
    data = source.read(path)
    table = cover_materials.descriptors(name)["descriptors"]
    assets = manifest.load()["assets"]
    pairs_path = cover_materials.out_dir() / "pairs.json"
    pairs = json.loads(pairs_path.read_text()) if pairs_path.is_file() else {}
    out = bytearray(b"SWCV" + struct.pack("<II", VERSION, len(table)))
    report = {"region": name, "cover": True, "descriptors": len(table), "records": 0, "records of imported meshes": 0,
              "descriptors with an override": 0, "overrides with no slot map": 0, "meshes not imported": 0}
    for d in table:
        entry = assets.get(d["mesh"] or "", {})
        mesh_path = object_path(entry["unreal"]) if entry.get("kind") == "static" and entry.get("unreal") else ""
        slots: dict = {}
        if d["override"]:
            report["descriptors with an override"] += 1
            pair = pairs.get(f"{d['mesh']}|{d['override']}")
            if pair is None:
                report["overrides with no slot map"] += 1
            else:
                slots = pair.get("slots", {})
        report["records"] += d["records"]
        if mesh_path:
            report["records of imported meshes"] += d["records"]
        else:
            report["meshes not imported"] += 1
        out += text(mesh_path) + struct.pack("<H", len(slots))
        for slot, key in sorted(slots.items()):
            out += text(slot) + text(key)
        out += struct.pack("<fB", float(d["bending"]), int(d["autoMerge"]))
    out += struct.pack("<Q", len(data)) + data
    target = env.content_dir() / "regions" / name / "cover.bin"
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(".tmp")   # the client can read the file: atomic
    temporary.write_bytes(bytes(out))
    temporary.replace(target)
    report["bytes"] = len(out)
    (target.parent / "cover.json").write_text(json.dumps(report, indent=1))
    return report
