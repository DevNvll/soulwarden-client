"""The instances of the world level (stage 1.3): the placements of a region in Unreal space, by streaming cell.

Input: the census placements of a region (census.load_placements) and the manifest (the Unreal folder of each mesh).
Output: `regions/NAME/cells.bin`, the file of SwcEditorLibrary.SpawnInstanceActors: one actor for each cell of
CELL_M x CELL_M meters with one instanced static mesh component for each mesh. World Partition streams the actors by
their bounds.

File (little-endian): "SWCI", u32 version 3, u32 entry count, the entries (u16 length + UTF-8 Unreal folder of the
mesh, u16 slot count, and for each slot: the slot name and the material key that replaces its material, each u16
length + UTF-8, then u8 flags: bit 0 the wind, bit 1 a decal companion), u32 actor count, then each actor: u16
length + UTF-8 label, f64 x 3 location (cm), u32 group count, then each group: u32 entry index, u32 instance count, and
each instance: f64 x 3 translation (cm), f32 x 4 rotation (Unreal quaternion x y z w), f32 x 3 scale. The transforms
follow the space map of stage 1.1 (space.py).
An entry is a group (mesh, material override, wind) (P4.1, overrides.py): the slots of a placement with an override get
the material instances of the override (version 1 had only the mesh). The wind is the receiveWind flag of the render
node of the placement (region.py): the component of an entry with wind gets the custom primitive data 0 "SwcWind" = 1,
for the wind of the vegetation material (E1; version 2 had no flags).
A decal companion (the materials track, step c): Nanite does not draw deferred decal materials, so the %DECAL subsets of
a mesh are a small non-Nanite mesh of their own (manifest: the main mesh has "decals": the key of the companion). Each
group of such a mesh is followed by a group of its companion with the same transforms: no slot map, no wind, no shadow,
no collision.
"""
from __future__ import annotations

import json
import struct

import numpy as np

from . import census, env, manifest, overrides

CELL_M = 256.0
SIGNS = np.array([[1.0, -1.0, 1.0], [-1.0, 1.0, -1.0], [1.0, -1.0, 1.0]])   # S L S with S = diag(1, -1, 1)
INSTANCE = np.dtype([("t", "<f8", 3), ("q", "<f4", 4), ("s", "<f4", 3)])


def quaternions(r: np.ndarray) -> np.ndarray:
    """@return The unit quaternions (n, 4: x y z w, w >= 0) of rotation matrices (n, 3, 3), column vectors: the four
    cases of space.quaternion (the largest of w, x, y, z first), for many matrices."""
    trace = r[:, 0, 0] + r[:, 1, 1] + r[:, 2, 2]
    case = np.argmax(np.stack([trace, r[:, 0, 0], r[:, 1, 1], r[:, 2, 2]], axis=1), axis=1)
    q = np.zeros((len(r), 4))
    for k in range(4):
        m = r[case == k]
        if not len(m):
            continue
        if k == 0:
            s = np.sqrt(1.0 + m[:, 0, 0] + m[:, 1, 1] + m[:, 2, 2]) * 2
            v = [(m[:, 2, 1] - m[:, 1, 2]) / s, (m[:, 0, 2] - m[:, 2, 0]) / s, (m[:, 1, 0] - m[:, 0, 1]) / s, 0.25 * s]
        elif k == 1:
            s = np.sqrt(1.0 + m[:, 0, 0] - m[:, 1, 1] - m[:, 2, 2]) * 2
            v = [0.25 * s, (m[:, 0, 1] + m[:, 1, 0]) / s, (m[:, 0, 2] + m[:, 2, 0]) / s, (m[:, 2, 1] - m[:, 1, 2]) / s]
        elif k == 2:
            s = np.sqrt(1.0 + m[:, 1, 1] - m[:, 0, 0] - m[:, 2, 2]) * 2
            v = [(m[:, 0, 1] + m[:, 1, 0]) / s, 0.25 * s, (m[:, 1, 2] + m[:, 2, 1]) / s, (m[:, 0, 2] - m[:, 2, 0]) / s]
        else:
            s = np.sqrt(1.0 + m[:, 2, 2] - m[:, 0, 0] - m[:, 1, 1]) * 2
            v = [(m[:, 0, 2] + m[:, 2, 0]) / s, (m[:, 1, 2] + m[:, 2, 1]) / s, 0.25 * s, (m[:, 1, 0] - m[:, 0, 1]) / s]
        q[case == k] = np.stack(v, axis=1)
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    return np.where(q[:, 3:4] < 0, -q, q)


def unreal_transforms(matrices: np.ndarray) -> np.ndarray:
    """@return The Unreal transforms (INSTANCE records) of engine placement matrices (n, 3, 4): space.unreal_transform
    for many placements (scale of each axis, the nearest rotation, a mirror as a negative X scale)."""
    linear = matrices[:, :, :3] * SIGNS
    scale = np.linalg.norm(linear, axis=1)
    mirror = np.linalg.det(linear) < 0
    scale[mirror, 0] *= -1
    unscaled = linear / np.where(scale == 0, 1.0, scale)[:, None, :]
    left, _values, right = np.linalg.svd(unscaled)
    out = np.empty(len(matrices), dtype=INSTANCE)
    out["t"] = matrices[:, :, 3] * np.array([100.0, -100.0, 100.0])
    out["q"] = quaternions(left @ right)
    out["s"] = scale
    return out


def text(value: str) -> bytes:
    data = value.encode()
    return struct.pack("<H", len(data)) + data


def write(name: str) -> dict:
    """Writes the cell file of one region. @return Counts."""
    data = census.load_placements(name)
    assets = manifest.load()["assets"]
    names = [str(m) for m in data["meshes"]]
    usable = np.array([assets.get(m, {}).get("kind") == "static" and not m.rsplit("/", 1)[-1].startswith("fx_")
                       for m in names], dtype=bool)
    keep = usable[data["mesh"]] if len(names) else np.zeros(0, bool)
    material_names = [str(m) for m in data.get("materials", np.array([""]))]
    material_all = data["material"] if "material" in data else np.zeros(len(data["mesh"]), np.uint16)
    pairs_path = overrides.out_dir() / "pairs.json"
    pairs = json.loads(pairs_path.read_text()) if pairs_path.is_file() else {}

    def slots_of(mesh_index: int, material_index: int) -> dict:
        if not material_index:
            return {}
        return pairs.get(overrides.pair_key(names[mesh_index], material_names[material_index]), {}).get("slots", {})

    # The entry of a placement: (mesh, override) when the override changes a slot, else the mesh alone.
    entry_of_pair: dict[tuple[int, int], tuple[int, int]] = {}
    for m, o in set(zip(data["mesh"][keep].tolist(), material_all[keep].tolist())):
        entry_of_pair[(m, o)] = (m, o) if slots_of(m, o) else (m, 0)
    pair_rows = np.array([entry_of_pair[(m, o)] for m, o in zip(data["mesh"][keep].tolist(), material_all[keep].tolist())],
                         dtype=np.int64).reshape(-1, 2)
    wind_all = data["wind"] if "wind" in data else np.zeros(len(data["mesh"]), bool)
    # One number for each entry: (mesh * 65536 + override) * 2 + wind.
    mesh_of = (pair_rows[:, 0] * 65536 + pair_rows[:, 1]) * 2 + wind_all[keep].astype(np.int64)
    matrices = data["matrix"][keep]
    rx, ry = (int(v) for v in name[2:].split("_"))
    ox, oy = rx * 2048.0, ry * 2048.0
    # The cell of the place of each instance (no clip: 0.11 % of the placements are more than 200 m outside the square of
    # their region, up to 970 m; a cell index can be below 0 or above 7). The actor stands at the center of its cell:
    # floor(X / 25600) and floor(-Y / 25600) give the global cell (the creature track's SwcLevelInstances).
    cx = np.floor((matrices[:, 0, 3] - ox) / CELL_M).astype(int)
    cy = np.floor((matrices[:, 1, 3] - oy) / CELL_M).astype(int)
    transforms = unreal_transforms(matrices)
    used = sorted(set(mesh_of.tolist()))
    table = {m: i for i, m in enumerate(used)}
    # The decal companion of a mesh: one entry for each mesh, after the entries of the groups.
    companions = sorted({m // 131072 for m in used if assets[names[m // 131072]].get("decals") in assets})
    companion_entry = {m: len(used) + i for i, m in enumerate(companions)}
    out = bytearray(b"SWCI" + struct.pack("<II", 3, len(used) + len(companions)))
    overridden = 0
    for m in used:
        pair, wind = divmod(m, 2)
        slots = slots_of(pair // 65536, pair % 65536)
        overridden += bool(slots)
        out += text(assets[names[pair // 65536]]["unreal"]) + struct.pack("<H", len(slots))
        for slot, key in sorted(slots.items()):
            out += text(slot) + text(key)
        out += struct.pack("<B", wind)
    for m in companions:
        out += text(assets[assets[names[m]]["decals"]]["unreal"]) + struct.pack("<HB", 0, 2)
    cells = sorted(set(zip(cx.tolist(), cy.tolist())))
    out += struct.pack("<I", len(cells))
    groups = decal_groups = 0
    for x, y in cells:
        rows = np.nonzero((cx == x) & (cy == y))[0]
        z = float(np.median(transforms["t"][rows, 2]))
        center = ((ox + (x + 0.5) * CELL_M) * 100.0, -(oy + (y + 0.5) * CELL_M) * 100.0, z)
        meshes_here = mesh_of[rows]
        order = sorted(set(meshes_here.tolist()))
        with_decals = [m for m in order if m // 131072 in companion_entry]
        out += text(f"{name} cell {x} {y}") + struct.pack("<3d", *center) + struct.pack("<I", len(order) + len(with_decals))
        for m in order:
            picked = rows[meshes_here == m]
            out += struct.pack("<II", table[m], len(picked)) + transforms[picked].tobytes()
            if m // 131072 in companion_entry:
                out += struct.pack("<II", companion_entry[m // 131072], len(picked)) + transforms[picked].tobytes()
        groups += len(order) + len(with_decals)
        decal_groups += len(with_decals)
    path = env.content_dir() / "regions" / name / "cells.bin"
    temporary = path.with_suffix(".tmp")   # a level build can read the file: atomic
    temporary.write_bytes(bytes(out))
    temporary.replace(path)
    report = {"region": name, "placements": int(len(data["mesh"])), "instances": int(keep.sum()), "entries": len(used),
              "entries with an override": overridden,
              "instances with an override": int(sum(1 for m in mesh_of.tolist() if (m // 2) % 65536)),
              "entries with wind": sum(m % 2 for m in used), "instances with wind": int((mesh_of % 2).sum()),
              "decal companions": len(companions), "decal components": decal_groups,
              "cells": len(cells), "components": groups, "file": str(path), "bytes": len(out),
              "left out (not imported or effect cards)": int((~keep).sum())}
    (path.parent / "cells.json").write_text(json.dumps(report, indent=1))
    return report
