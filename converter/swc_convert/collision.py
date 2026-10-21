"""The game collision of the static meshes (P4.1 part 1): the physics file of a mesh -> one collision file.

The game has the collision of a mesh in its physics file: the path of the `.cgf` with `.rnr` (the product of the same
source). It has boxes, spheres, cylinders, capsules, convex hulls, triangle meshes, and compounds of them; the reader
is vendor/soulwarden/nav_shapes.py (unchanged copy, the reader of the Soulwarden navigation tools). A mesh with no
physics file has no collision in the game (plants, cloth, books, small props): the import gives it no collision.

The converter writes the triangles of all the shapes of a mesh, in the space of the mesh (engine meters, the same
space as the glTF of the mesh: the bounds of the inn mesh of Oxboro match), to content/collision/PATH.swccol. The
import makes them the complex collision of the static mesh (USwcEditorLibrary::SetGameCollision). The record of the
mesh in the manifest gets "collision": {"file", "hash"}; its "hash" does not change (no new import of the mesh).

File: magic b"SWCC", version u32 (1), vertexCount u32, triangleCount u32, then vertexCount x 3 f32 (engine m: X east,
Y north, Z up), then triangleCount x 3 u32 (the front of a closed part to the outside, counterclockwise in the engine
space).
"""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

import numpy as np

from . import env, manifest, source
from nav_shapes import box_part, cylinder_part, outward, read_physics, sphere_part  # noqa: E402  (vendor/soulwarden)

MAGIC = b"SWCC"
VERSION = 1


def physics_path(mesh_key: str) -> str | None:
    """@return The physics file of a mesh of the game (`.cgf` key), or None when the game has none."""
    if not mesh_key.endswith(".cgf"):
        return None
    path = mesh_key[:-4] + ".rnr"
    return path if source.has(path) else None


def triangles(mesh_key: str) -> tuple[np.ndarray, np.ndarray] | None:
    """@return (vertices (n, 3) float32 engine m, triangles (m, 3) uint32) of the physics file of a mesh, or None."""
    if mesh_key in UNIT_SHAPES:
        parts = UNIT_SHAPES[mesh_key]()
    else:
        path = physics_path(mesh_key)
        if path is None:
            return None
        parts = read_physics(source.read(path))
    vertices, indices, base = [], [], 0
    for _kind, part_vertices, part_triangles in parts:
        v = np.asarray(part_vertices, np.float32).reshape(-1, 3)
        t = np.asarray(part_triangles, np.uint32).reshape(-1, 3)
        if len(v) == 0 or len(t) == 0:
            continue
        vertices.append(v)
        indices.append(t + base)
        base += len(v)
    if not vertices:
        return None
    return np.concatenate(vertices), np.concatenate(indices)


# The unit shapes of the simple colliders of the collision chunks (AST-216; collision_chunks.py places them with a scale): a
# box of half size 1, a sphere of radius 1, a cylinder of radius 1 from z -1 to 1 (the parts of the Soulwarden tools).
UNIT_SHAPES = {"swc:unit_box": lambda: [outward(box_part((1.0, 1.0, 1.0)))],
               "swc:unit_sphere": lambda: [outward(sphere_part(1.0))],
               "swc:unit_cylinder": lambda: [outward(cylinder_part((0.0, 0.0, -1.0), (0.0, 0.0, 1.0), 1.0))]}


def out_path(mesh_key: str) -> Path:
    if mesh_key.startswith("swc:"):
        return env.content_dir() / "collision" / "swc" / (mesh_key[4:] + ".swccol")
    return env.content_dir() / "collision" / (mesh_key[:-4] + ".swccol")


def write(mesh_key: str) -> dict | None:
    """Writes the collision file of one mesh. @return Its record ({"file", "hash", "triangles"}) or None (no physics
    file, or no triangles)."""
    result = triangles(mesh_key)
    if result is None:
        return None
    vertices, indices = result
    data = MAGIC + struct.pack("<3I", VERSION, len(vertices), len(indices)) + vertices.tobytes() + indices.tobytes()
    out = out_path(mesh_key)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.is_file() or out.read_bytes() != data:
        temporary = out.with_suffix(".tmp")
        temporary.write_bytes(data)
        temporary.replace(out)
    return {"file": str(out.relative_to(env.content_dir())), "hash": hashlib.sha1(data).hexdigest()[:16],
            "triangles": int(len(indices))}


def collision_only(keys: list[str]) -> dict:
    """The collision-only meshes (AST-216): meshes with a physics file and no static entry in the manifest (the pieces that
    are only in the collision chunks, for example ancient_sphere_top or the collision ramps; the collision-only meshes of
    the level, for example worldbound_col; a mesh of kind "empty"), and the unit shapes of UNIT_SHAPES. Each gets its
    collision file and a record: a key with no entry gets an entry of kind "collision" (no render mesh), a key of kind
    "empty" keeps its kind. Import section 6b makes the collision mesh (no render mesh). @return A report."""
    assets = manifest.load()["assets"]
    report = {"keys": 0, "with collision": 0, "new entries": 0, "failed": []}
    records = {}
    for key in sorted(set(keys) | set(UNIT_SHAPES)):
        kind = assets.get(key, {}).get("kind")
        if kind not in (None, "empty", "collision"):
            continue   # a static mesh: convert_all
        report["keys"] += 1
        try:
            records[key] = write(key)
        except Exception as error:
            report["failed"].append(f"{key}: {str(error)[:100]}")
            continue
        if records[key] is not None:
            report["with collision"] += 1
    doc = manifest.load()
    for key, record in records.items():
        if record is None:
            continue
        entry = doc["assets"].get(key)
        if entry is None:
            entry = doc["assets"][key] = {"kind": "collision", "unreal": manifest.unreal_folder("collision", key), "version": 1}
            report["new entries"] += 1
        entry["collision"] = {"file": record["file"], "hash": record["hash"]}
    manifest.save(doc)
    return report


def convert_all(keys: list[str] | None = None) -> dict:
    """Writes the collision files of the static meshes of the manifest (or of `keys`), then puts their records in the
    manifest (a short load, change, and save: other tools write the manifest too). A mesh of kind "empty" (nothing to
    draw, mesh rule 6) has no asset: it is not here (a collision-only mesh is a later part of P4.1). @return A report."""
    assets = manifest.load()["assets"]
    wanted = keys if keys is not None else [k for k, e in assets.items() if e["kind"] == "static"]
    report = {"meshes": 0, "with collision": 0, "triangles": 0, "failed": []}
    records = {}
    for key in wanted:
        if assets.get(key, {}).get("kind") != "static":
            continue
        report["meshes"] += 1
        try:
            records[key] = write(key)
        except Exception as error:   # a physics file that does not decode: the mesh gets no collision
            report["failed"].append(f"{key}: {str(error)[:100]}")
            records[key] = None
        if records[key] is not None:
            report["with collision"] += 1
            report["triangles"] += records[key]["triangles"]
    doc = manifest.load()
    for key, record in records.items():
        entry = doc["assets"].get(key)
        if entry is None:
            continue
        if record is None:
            entry.pop("collision", None)
        else:
            entry["collision"] = {"file": record["file"], "hash": record["hash"]}
    manifest.save(doc)
    report["failed count"] = len(report["failed"])
    report["failed"] = report["failed"][:20]
    return report
