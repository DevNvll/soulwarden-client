"""The instances of the world level (stage 1.3): the placements of a region in Unreal space, by streaming cell.

Input: the census placements of a region (census.load_placements) and the manifest (the Unreal folder of each mesh).
Output: `regions/NAME/cells.bin`, the file of SwcEditorLibrary.SpawnInstanceActors: one actor for each cell of
CELL_M x CELL_M meters with one instanced static mesh component for each mesh. World Partition streams the actors by
their bounds.

File (little-endian): "SWCI", u32 version 1, u32 mesh count, the meshes (u16 length + UTF-8 Unreal folder), u32 actor
count, then each actor: u16 length + UTF-8 label, f64 x 3 location (cm), u32 group count, then each group: u32 mesh
index, u32 instance count, and each instance: f64 x 3 translation (cm), f32 x 4 rotation (Unreal quaternion x y z w),
f32 x 3 scale. The transforms follow the space map of stage 1.1 (space.py).
"""
from __future__ import annotations

import json
import struct

import numpy as np

from . import census, env, manifest

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
    mesh_of = data["mesh"][keep]
    matrices = data["matrix"][keep]
    rx, ry = (int(v) for v in name[2:].split("_"))
    ox, oy = rx * 2048.0, ry * 2048.0
    per_side = int(2048 / CELL_M)
    cx = np.clip(((matrices[:, 0, 3] - ox) // CELL_M).astype(int), 0, per_side - 1)
    cy = np.clip(((matrices[:, 1, 3] - oy) // CELL_M).astype(int), 0, per_side - 1)
    transforms = unreal_transforms(matrices)
    used = sorted(set(mesh_of.tolist()))
    table = {m: i for i, m in enumerate(used)}
    out = bytearray(b"SWCI" + struct.pack("<II", 1, len(used)))
    for m in used:
        out += text(assets[names[m]]["unreal"])
    cells = sorted(set(zip(cx.tolist(), cy.tolist())))
    out += struct.pack("<I", len(cells))
    groups = 0
    for x, y in cells:
        rows = np.nonzero((cx == x) & (cy == y))[0]
        z = float(np.median(transforms["t"][rows, 2]))
        center = ((ox + (x + 0.5) * CELL_M) * 100.0, -(oy + (y + 0.5) * CELL_M) * 100.0, z)
        meshes_here = mesh_of[rows]
        order = sorted(set(meshes_here.tolist()))
        out += text(f"{name} cell {x} {y}") + struct.pack("<3d", *center) + struct.pack("<I", len(order))
        for m in order:
            picked = rows[meshes_here == m]
            out += struct.pack("<II", table[m], len(picked)) + transforms[picked].tobytes()
        groups += len(order)
    path = env.content_dir() / "regions" / name / "cells.bin"
    temporary = path.with_suffix(".tmp")   # a level build can read the file: atomic
    temporary.write_bytes(bytes(out))
    temporary.replace(path)
    report = {"region": name, "placements": int(len(data["mesh"])), "instances": int(keep.sum()), "meshes": len(used),
              "cells": len(cells), "components": groups, "file": str(path), "bytes": len(out),
              "left out (not imported or effect cards)": int((~keep).sum())}
    (path.parent / "cells.json").write_text(json.dumps(report, indent=1))
    return report
