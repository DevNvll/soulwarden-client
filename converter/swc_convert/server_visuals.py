"""The visuals of the objects that the backend spawns (AST-275, class C2): the crafting stations of the settlements and their bodies.

A settlement places its stations with PrefabSpawners that are not "on start" (m_spawnOnInitialize false): the server makes each
station entity and its children (the body `craftingstation_t5_*_complete`, the torches). The client gets a record of each one
(the metadata state: the slice asset; the PositionInTheWorld state: the place and the rotation) within 150 m, and the stop of the
record at 200 m (Soulwarden protocol notes 4.36). The census does not follow these spawners (the cells have no station), and
before AST-275 no client system drew such a record.

Input: the backend copy `gamedata/interaction-data.json`: the list `spawned` (the station objects of the backend with their place)
and its `templates` (the slice of each object and its children with their places relative to it). Only the objects with
spawnOnInitialize false: the stations "on start" are census placements already (no double draw).
Output: SWC_CONTENT/world-objects/server-visuals.json:
  "slices": {KEY: [{"mesh", "collision", "slots", "decal", "transforms"}]}: the render nodes of the slice of a record key (the
      text of Entity::slice_key: the GUID in upper case and ":SUB"). "mesh" and "collision" are object paths of the plugin ("" when
      the plugin has none); "slots" the slot name and the material key of an override (overrides.py pairs, as the cells); "decal":
      the decal companion of a mesh (no shadow, no collision); "transforms": [tx ty tz (cm), qx qy qz qw, sx sy sz] in the slice,
      in Unreal space (cells.unreal_transforms).
  "places": [{"key", "place" (engine m), "rotation" (engine quaternion x y z w), "slice"}]: the places of the backend objects;
      the client draws a record only when a place of its key is within PLACE_M of the record (USwcServerVisualSubsystem).
"""
from __future__ import annotations

import json
import os
import sys
import uuid
from pathlib import Path

import numpy as np

from . import cells, cover, env, manifest, overrides, region, source
from .creature import gamedata_dir

FORMAT = 1


def record_key(asset: str, sub: int) -> str:
    """@return The key of a record slice as the client writes it (Entity::slice_key): the GUID text in upper case and ":SUB"."""
    return f"{str(uuid.UUID(asset)).upper()}:{sub}"


def quat_rotate(q, v) -> np.ndarray:
    return region.quat_matrix(*q) @ np.asarray(v, dtype=np.float64)


def quat_multiply(a, b) -> list[float]:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def objects() -> list[dict]:
    """@return The backend objects with spawnOnInitialize false and their children (the template tree): key, slice, place,
    rotation."""
    data = json.loads((gamedata_dir() / "interaction-data.json").read_text())
    templates = data["templates"]
    out: list[dict] = []

    def add(index: int, place, rotation, depth: int) -> None:
        template = templates[index]
        out.append({"key": record_key(template["asset"], template["sub"]), "slice": template["slice"],
                    "place": [round(float(v), 4) for v in place], "rotation": [round(float(v), 6) for v in rotation]})
        if depth < 8:
            for child in template["children"]:
                add(child["template"], np.asarray(place) + quat_rotate(rotation, child["position"]),
                    quat_multiply(rotation, child["rotation"]), depth + 1)

    for spawned in data["spawned"]:
        if not spawned["spawnOnInitialize"]:
            add(spawned["template"], spawned["position"], spawned["rotation"], 0)
    return out


def slice_parts(path: str, assets: dict, pairs: dict) -> tuple[list[dict], list[str]]:
    """@return The parts of a slice (its render nodes, grouped by mesh and override) and the meshes that are not imported."""
    groups: dict[tuple[str, str], list[np.ndarray]] = {}
    for mesh, matrix, material, _ident, _instance, _wind in region.slice_meshes(source.read(path)):
        groups.setdefault((mesh, material or ""), []).append(matrix[:3])
    parts, missing = [], []
    for (mesh, material), matrices in sorted(groups.items()):
        entry = assets.get(mesh, {})
        if entry.get("kind") != "static" or mesh.rsplit("/", 1)[-1].startswith("fx_"):
            continue   # the rule of the cells (cells.write): an effect card or a mesh with no drawn part
        mesh_path = cover.object_path(entry["unreal"])
        if not mesh_path:
            missing.append(mesh)
            continue
        folder = entry["unreal"].rsplit("/", 1)[-1]
        collision = collision_path(folder)
        slots = pairs.get(overrides.pair_key(mesh, material), {}).get("slots", {}) if material else {}
        transforms = [[*t["t"].tolist(), *t["q"].tolist(), *t["s"].tolist()] for t in cells.unreal_transforms(np.asarray(matrices))]
        parts.append({"mesh": mesh_path, "collision": collision, "slots": slots, "decal": False, "transforms": transforms})
        companion = assets.get(entry.get("decals", ""), {})
        if companion.get("unreal"):
            companion_path = cover.object_path(companion["unreal"])
            if companion_path:
                parts.append({"mesh": companion_path, "collision": "", "slots": {}, "decal": True, "transforms": transforms})
    return parts, missing


def collision_path(folder: str) -> str:
    """@return The object path of the collision mesh of an imported mesh folder (import_content.py section 6:
    /SwcGameContent/Collision/FOLDER/C_NAME), or ""."""
    root = Path(os.environ.get("SWC_GAME_CONTENT") or env.work_dir() / "unreal" / "SwcGameContent") / "Content"
    found = sorted((root / "Collision" / folder).glob("*.uasset"))
    return f"/SwcGameContent/Collision/{folder}/{found[0].stem}.{found[0].stem}" if found else ""


def write() -> dict:
    assets = manifest.load()["assets"]
    pairs_path = overrides.out_dir() / "pairs.json"
    pairs = json.loads(pairs_path.read_text()) if pairs_path.is_file() else {}
    places = objects()
    slices: dict[str, list[dict]] = {}
    missing: set[str] = set()
    for place in places:
        if place["key"] not in slices:
            parts, gone = slice_parts(place["slice"], assets, pairs)
            slices[place["key"]] = parts
            missing.update(gone)
    drawn = [p for p in places if slices[p["key"]]]
    out = {"format": FORMAT, "source": "interaction-data.json (spawned, spawnOnInitialize false)",
           "slices": {k: v for k, v in sorted(slices.items()) if v}, "places": drawn}
    path = env.content_dir() / "world-objects" / "server-visuals.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(out, indent=0))
    temporary.replace(path)
    report = {"file": str(path), "backend objects": len(places), "places with meshes": len(drawn),
              "slices with meshes": len(out["slices"]),
              "instances": sum(len(part["transforms"]) for p in drawn for part in out["slices"][p["key"]] if not part["decal"]),
              "meshes not imported": sorted(missing)}
    return report


def wanted_pairs() -> set[tuple[str, str]]:
    """@return The (mesh, override material) pairs of the slices of the backend objects (for overrides.add_pairs)."""
    wanted = set()
    for place in objects():
        for mesh, _m, material, *_ in region.slice_meshes(source.read(place["slice"])):
            if material:
                wanted.add((mesh, material))
    return wanted


def wanted_meshes() -> list[str]:
    """@return The meshes of the slices of the backend objects (for the conversion and the import)."""
    meshes = set()
    for place in objects():
        meshes.update(m for m, *_ in region.slice_meshes(source.read(place["slice"])))
    return sorted(meshes)


if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else "write"
    if command == "meshes":
        print(json.dumps(wanted_meshes(), indent=0))
    elif command == "pairs":
        print(json.dumps(overrides.add_pairs(wanted_pairs()), indent=1))
    else:
        print(json.dumps(write(), indent=1))
