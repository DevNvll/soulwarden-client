"""The collision of the merged structures (AST-201): the colliders of the collision chunks of a region.

The game merges the render meshes of a part of a region (CoATGen) into mesh chunks: `coatgen/HASH/NAME_mesh_chunk_X_Y_Z`
(merged meshes and instanced meshes). Their collision is not in these meshes (the merged .cgf has no physics chunk and no .rnr):
the collision chunk of the same part (`NAME_collision_chunk_X_Y_Z`, chunkType 2 of region.chunks) has one collider for each
piece of the part, the physics file (.rnr) of its mesh at its place (the deck and the stairs of the supply stockpile north of
Oxboro: jav_prp_platform_b_long, jav_prp_stairs_b_md). region.py skips these chunks (they have no render mesh), so the level
had no collision for the merged structures: the character walked under the planks of a deck (AST-201).

This step writes the colliders of the collision chunks of a region whose physics mesh has an imported collision mesh (import
section 6: /SwcGameContent/Collision/FOLDER/C_NAME, the folder of the render mesh of the manifest), less the colliders that are
also a placement of the level with the same mesh at the same place (the instanced pieces of a mesh chunk: the cells already
have their collision). The client places them at run time (USwcCollisionChunks): hidden instances with the collision profile
of the cells. Also (AST-216): the simple shapes of the chunks (boxes, spheres, cylinders, capsules as cylinders) as scaled
instances of the unit shapes of collision.UNIT_SHAPES, the colliders of the collision-only meshes (collision.collision_only:
the meshes that are only in the chunks), and the placements of the level whose mesh is collision-only (the cells leave them
out).

Output: regions/NAME/collision-chunks.bin (little-endian): "SWCK", u32 version 1, u32 entry count, the entries (u16 length +
UTF-8 object path of the collision mesh), u32 instance count, then each instance: u32 entry index, f64 x 3 translation (cm), f32
x 4 rotation (x y z w), f32 x 3 scale (cells.unreal_transforms: the space map of stage 1.1).
"""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

from . import census, env, manifest, source
from .cells import INSTANCE, unreal_transforms
from .region import WORLD, F
from nav_slices import MARK_CUT, read_slice  # noqa: E402  (vendor/soulwarden, on the path of source)

MAGIC = b"SWCK"
VERSION = 1
# A collider within this distance of a placement of the same mesh is that placement (the same piece; float noise of the slices).
SAME_PLACE_M = 0.05


def collision_assets() -> dict[str, str]:
    """@return The folder name -> the object path of its collision mesh (the imported collision meshes on disk)."""
    root = env.work_dir() / "unreal/SwcGameContent/Content/Collision"
    return {f.parent.name: f"/SwcGameContent/Collision/{f.parent.name}/{f.stem}.{f.stem}" for f in root.glob("*/C_*.uasset")}


def colliders(name: str) -> tuple[list[str], np.ndarray, dict]:
    """@return (the mesh keys, the engine matrices (n, 3, 4), counts) of the rnr colliders of the collision chunks of a region."""
    keys: list[str] = []
    matrices: list[np.ndarray] = []
    counts = {"chunks": 0, "colliders": 0, "shapes": 0, "shapes left out": 0, "other colliders": 0, "unresolved": 0}
    root = source.parse(source.read(WORLD + "regions/" + name + "/region.chunks"))[0]
    for chunk in root.child(F("chunks")).children:
        if source.number(chunk.child(F("chunkType"))) != 2:
            continue
        a = source.asset_id(chunk.child(F("AssetId")))
        path = source.product(a[0], a[1], ".dynamicslice") if a else None
        if path is None:
            continue
        counts["chunks"] += 1
        x, y, z = struct.unpack(">3f", chunk.child(F("WorldPosition")).value)
        for collider in read_slice(source.read(path)).colliders:
            if collider.kind == "shape":
                # A simple shape (AST-216): an instance of a unit shape of collision.UNIT_SHAPES, scaled, at the place of the
                # collider and the offset of the shape (the rule of the Soulwarden collision data: no scale flag).
                values, offset, _type, _scale = collider.reference
                if values is None or collider.mark == MARK_CUT:
                    counts["shapes left out"] += 1
                    continue
                m = np.asarray(collider.matrix, dtype=np.float64).reshape(3, 4).copy()
                m[:, 3] += m[:, :3] @ np.asarray(offset, dtype=np.float64)
                m[:, 3] += (x, y, z)
                if values[0] == "box":
                    size = (values[1], values[2], values[3])
                elif values[0] == "sphere":
                    size = (values[1],) * 3
                else:   # a cylinder (or a capsule: the cylinder of its height): (height, radius)
                    size = (values[2], values[2], values[1] / 2)
                m[:, :3] = m[:, :3] @ np.diag(size)
                keys.append("swc:unit_" + values[0])
                matrices.append(m)
                counts["shapes"] += 1
                continue
            if collider.kind != "rnr":
                counts["other colliders"] += 1
                continue
            rnr = source.product(collider.reference[0], collider.reference[1], ".rnr") if collider.reference else None
            if rnr is None:
                counts["unresolved"] += 1
                continue
            m = np.asarray(collider.matrix, dtype=np.float64).reshape(3, 4).copy()
            m[:, 3] += (x, y, z)   # a chunk slice is placed with no rotation (region.py)
            keys.append(rnr[:-4] + ".cgf")
            matrices.append(m)
            counts["colliders"] += 1
    return keys, np.asarray(matrices).reshape(-1, 3, 4), counts


def write(name: str, assets: dict | None = None, folders: dict | None = None) -> dict:
    assets = assets if assets is not None else manifest.load()["assets"]
    folders = folders if folders is not None else collision_assets()
    keys, matrices, counts = colliders(name)
    # The placements of the level by mesh: a collider at the place of a placement of its mesh is that placement.
    placed: dict[str, np.ndarray] = {}
    try:
        data = census.load_placements(name)
        mesh_names = [str(m) for m in data["meshes"]]
        all_matrices = data["matrix"].reshape(-1, 3, 4)
        translations = all_matrices[:, :, 3]
        order = np.argsort(data["mesh"], kind="stable")
        bounds = np.searchsorted(data["mesh"][order], np.arange(len(mesh_names) + 1))
        extra_keys, extra_matrices = [], []
        for index, key in enumerate(mesh_names):
            if bounds[index] < bounds[index + 1]:
                rows = order[bounds[index]:bounds[index + 1]]
                entry = assets.get(key) or {}
                if entry.get("kind") in ("empty", "collision") and entry.get("collision"):
                    # A placement of a collision-only mesh (AST-216: worldbound_col, the barricade collisions): the cells leave
                    # it out (no render mesh), so its collision comes from here.
                    extra_keys += [key] * len(rows)
                    extra_matrices.append(all_matrices[rows])
                else:
                    placed[key.lower()] = translations[rows]
        if extra_keys:
            keys = keys + extra_keys
            matrices = np.concatenate([matrices] + extra_matrices)
        counts["collision-only placements"] = len(extra_keys)
    except FileNotFoundError:
        pass
    entries: list[str] = []
    entry_of: dict[str, int] = {}
    rows: list[int] = []
    keep: list[int] = []
    counts.update({"placed already": 0, "no collision mesh": 0})
    for i, key in enumerate(keys):
        same = placed.get(key.lower())
        if same is not None and np.min(np.linalg.norm(same - matrices[i, :, 3], axis=1)) <= SAME_PLACE_M:
            counts["placed already"] += 1
            continue
        record = assets.get(key) or assets.get(key.lower())
        folder = record.get("unreal", "").rsplit("/", 1)[-1] if record else ""
        path = folders.get(folder)
        if path is None:
            counts["no collision mesh"] += 1
            continue
        if path not in entry_of:
            entry_of[path] = len(entries)
            entries.append(path)
        rows.append(entry_of[path])
        keep.append(i)
    transforms = unreal_transforms(matrices[keep]) if keep else np.zeros(0, dtype=INSTANCE)
    blob = bytearray(MAGIC + struct.pack("<II", VERSION, len(entries)))
    for path in entries:
        encoded = path.encode()
        blob += struct.pack("<H", len(encoded)) + encoded
    blob += struct.pack("<I", len(keep))
    record = np.dtype([("entry", "<u4"), ("t", "<f8", 3), ("q", "<f4", 4), ("s", "<f4", 3)])
    out = np.zeros(len(keep), dtype=record)
    out["entry"] = rows
    for field in ("t", "q", "s"):
        out[field] = transforms[field]
    blob += out.tobytes()
    target = env.content_dir() / "regions" / name / "collision-chunks.bin"
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_suffix(".tmp")
    tmp.write_bytes(bytes(blob))
    tmp.replace(target)
    counts.update({"instances": len(keep), "meshes": len(entries), "file": str(target)})
    return counts


def collision_only_keys(names: list[str] | None = None) -> list[str]:
    """@return The meshes for collision.collision_only (AST-216): the physics meshes of the collision chunks with no static
    entry in the manifest, and the meshes of the level placements with no static entry that have a physics file."""
    from . import collision
    assets = manifest.load()["assets"]
    wanted: set[str] = set()
    for name in names or census.world_regions():
        try:
            keys, _matrices, _counts = colliders(name)
        except Exception:
            continue
        wanted.update(k for k in set(keys) if not k.startswith("swc:") and (assets.get(k) or {}).get("kind") != "static")
        try:
            meshes = [str(m) for m in census.load_placements(name)["meshes"]]
        except FileNotFoundError:
            continue
        wanted.update(m for m in meshes if (assets.get(m) or {}).get("kind") != "static"
                      and not m.rsplit("/", 1)[-1].startswith("fx_") and collision.physics_path(m) is not None)
    return sorted(wanted)


def write_all(names: list[str] | None = None) -> dict:
    regions = names or sorted(p.name for p in (env.content_dir() / "regions").iterdir() if (p / "placements.npz").exists())
    assets = manifest.load()["assets"]
    folders = collision_assets()
    report = {}
    for name in regions:
        try:
            report[name] = write(name, assets, folders)
        except FileNotFoundError as error:
            report[name] = {"error": str(error)}
    return report
