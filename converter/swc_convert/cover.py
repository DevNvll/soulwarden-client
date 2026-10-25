"""The ground cover of the regions for the client (world fidelity T3; research/terrain.md, section 3).

region.vegetation of a region has a descriptor table (the mesh, the material override, AutoMerge, Bending / 100) and
128 x 128 tiles of 16 m with groups of packed records (16 bytes: the quaternion u32, the local x and y u16, the z u16,
the scale u8, a spare u8, the output ordinal u16, a spare u16). The cover stays packed: the client (USwcGroundCover)
reads the records of the tiles near the view and places them (no Landscape Grass, no new scatter).

Output: regions/NAME/cover.bin (little-endian): "SWCV", u32 version 1, u32 descriptor count; each descriptor: u16 length +
UTF-8 object path of the static mesh (FOLDER/STEM/StaticMeshes/STEM.STEM, found in the plugin on disk; empty when the mesh
is not imported: the client skips it), u16 slot count and for each slot the slot name and the material key of the
override (cover/pairs.json of cover_materials.py; each u16 length + UTF-8), f32 bending (the custom primitive data 0 "SwcWind" of the components), u8 AutoMerge; then u64 length and the
bytes of region.vegetation, unchanged (its tile offsets count from the start of these bytes). After those bytes (from
2026-10-20; a client before it reads none of it, so the version stays 1): the claimed sample slots (cover_areas.py: the
vegetation blockers of the level claim the slots, and a record draws only when its slot is free; research/ground-cover.md):
u32 tile count, then each tile: u16 tile index (tileY x 128 + tileX) and 128 bytes, bit N (byte N / 8, bit N % 8) set when
the ordinal N is claimed. The menu terrain has no claims yet (count 0).
"""
from __future__ import annotations

import json
import os
import struct
from pathlib import Path

from . import cover_areas, cover_materials, env, manifest, region, source

VERSION = 1
# The placeholders of the vegetation tables that the client does not draw (AST-288 W8, the decision of the orchestrator 2026-10-25):
# jav_fst_emptyspace_a is one Illum triangle of 24 cm with textures/defaults/grey.dds (78,200 records in 10 regions); the retail
# still at 13656 8462 (set-look1024 "empty") shows nothing. Its descriptor keeps its index and gets no mesh path: the client skips it.
LEFT_OUT = frozenset({"objects/nature/veg_grass/grass_forest/emptyspace/jav_fst_emptyspace_a.cgf"})


def left_out(mesh: str | None) -> bool:
    """@return True for a descriptor mesh that the cover does not draw (LEFT_OUT)."""
    return (mesh or "").lower() in LEFT_OUT
# The terrains with ground cover: the folder of the terrain in the game data and the content folder of its regions. The world
# is the default; the terrain of the main menu level writes to its own folder, so that no file of the world changes (stage F4b
# of docs/FRONTEND.md: the leaves and stones at the feet of the menu character).
TERRAINS = {"newworld_vitaeeterna": (region.WORLD, "regions"),
            "nw_frontend_base": ("sharedassets/coatlicue/nw_frontend_base/", "frontend/terrain/regions")}


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


def write(name: str, terrain: str = "newworld_vitaeeterna") -> dict:
    """Writes REGIONS/NAME/cover.bin (REGIONS: the content folder of the terrain, TERRAINS). @return A report (cover False for
    a region with no region.vegetation)."""
    level, regions = TERRAINS[terrain]
    path = level + "regions/" + name + "/region.vegetation"
    if not source.has(path):
        return {"region": name, "cover": False}
    data = source.read(path)
    table = cover_materials.descriptors(name, level)["descriptors"]
    assets = manifest.load()["assets"]
    pairs_path = cover_materials.out_dir() / "pairs.json"
    pairs = json.loads(pairs_path.read_text()) if pairs_path.is_file() else {}
    out = bytearray(b"SWCV" + struct.pack("<II", VERSION, len(table)))
    report = {"region": name, "cover": True, "descriptors": len(table), "records": 0, "records of imported meshes": 0,
              "descriptors with an override": 0, "overrides with no slot map": 0, "meshes not imported": 0,
              "records left out (placeholders)": 0}
    for d in table:
        entry = assets.get(d["mesh"] or "", {})
        mesh_path = object_path(entry["unreal"]) if entry.get("kind") == "static" and entry.get("unreal") else ""
        if mesh_path and left_out(d["mesh"]):
            mesh_path = ""
            report["records left out (placeholders)"] += d["records"]
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
        elif not left_out(d["mesh"]):
            report["meshes not imported"] += 1
        out += text(mesh_path) + struct.pack("<H", len(slots))
        for slot, key in sorted(slots.items()):
            out += text(slot) + text(key)
        out += struct.pack("<fB", float(d["bending"]), int(d["autoMerge"]))
    out += struct.pack("<Q", len(data)) + data
    claims: dict[int, bytes] = {}
    if terrain == "newworld_vitaeeterna":
        areas, slots = cover_areas.write(name)
        claims = cover_areas.tile_bits(slots)
        report.update({"blockers": areas["blockers"], "claimed slots": areas["claimed slots"], "tiles with claims": len(claims)})
    out += struct.pack("<I", len(claims))
    for tile, bits in sorted(claims.items()):
        out += struct.pack("<H", tile) + bits
    target = env.content_dir() / regions / name / "cover.bin"
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(".tmp")   # the client can read the file: atomic
    temporary.write_bytes(bytes(out))
    temporary.replace(target)
    report["bytes"] = len(out)
    (target.parent / "cover.json").write_text(json.dumps(report, indent=1))
    return report
