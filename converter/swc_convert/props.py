"""The .cdf props of the level slices (AST-275 C6): a Character component of a slice entity that shows a character definition with
no animation, for example the fish of the Grivaros stalls, the weapons and shields on the racks, the shovels, the glass bottles, and
the saddle bags of the stables. The census took only .cgf meshes, so these props were missing (694 references of 74 files in 15
regions of the world, 2026-10-24).

Retail draws such a prop in its bind pose: the Character component has no animation field (3 fields: the id, the character data
0xe0c81603, and 0xfed96822). The client places each part of the .cdf as a static mesh: a CA_SKIN part (.skin) in its bind pose, the
model space of its vertices; a CA_BONE part (.cgf) at the absolute model transform of the attachment (Position, Rotation). Hidden
attachments (Flags bit 0) stay out. A part with no material of its own (most .skin files) takes the .mtl of its attachment: the
registry world-objects/cdf-parts.json gives it to mesh.convert; a part whose attachment names another .mtl gets it as the material
override of the placement (overrides.py).

Only the folders of PROP_FOLDERS: the animals, the creatures, the persons, and the animated ship rigging play clips in retail and
stay out (the persons and creature systems).
"""
from __future__ import annotations

import functools
import json
import xml.etree.ElementTree as ET

import numpy as np

from . import env, source

PROP_FOLDERS = ("objects/weapons/", "objects/characters/npc/natural/fish/", "objects/characters/player/mounts/mountattach/",
                "objects/crafting/props/")
# The fields of the Character component (region.slice_meshes): the character data, its visible flag, the .cdf asset, and a material
# asset. In 220 of the 356 components of r_+01_+02 and r_+04_+02 the material asset is
# materials/seasons/s_01/jav_decal_claimdirta_s01hack.mtl (a forest trail dirt decal material) on fish, swords, and shields: the
# client does not use it until a retail picture shows it (USE_MATERIAL_FIELD).
CHARACTER_DATA = 0xE0C81603
CHARACTER_VISIBLE = 0x7AB0E859
CHARACTER_ASSET = 0x6CC7A752
CHARACTER_MATERIAL = 0xDCA75C13
USE_MATERIAL_FIELD = False


def norm(path: str | None) -> str:
    return (path or "").lower().replace("\\", "/").lstrip("/")


# The siege weapons of the forts (fire droppers, cannons, the seated turrets) are interactive objects with clips: not props.
NOT_PROPS = ("objects/weapons/siege/",)


def is_prop(cdf: str) -> bool:
    return cdf.endswith(".cdf") and cdf.startswith(PROP_FOLDERS) and not cdf.startswith(NOT_PROPS)


def _matrix(position: str | None, rotation: str | None) -> np.ndarray:
    """@return The 4x4 matrix of a CDF Position "x,y,z" and Rotation "w,x,y,z" (a quaternion)."""
    from .region import quat_matrix
    m = np.eye(4)
    if rotation:
        w, x, y, z = (float(v) for v in rotation.split(","))
        m[:3, :3] = quat_matrix(x, y, z, w)
    if position:
        m[:3, 3] = [float(v) for v in position.split(",")]
    return m


@functools.lru_cache(maxsize=None)
def parts(cdf: str) -> tuple[tuple[str, str, tuple], ...]:
    """@return The parts of a prop: (mesh path, .mtl of the attachment or "", model matrix as a tuple of 16 floats)."""
    try:
        root = ET.fromstring(source.read(norm(cdf)).decode("utf-8", "replace"))
    except Exception:
        return ()
    model = root.find("Model")
    default_material = norm(model.get("Material")) if model is not None else ""
    out = []
    for attachment in root.iter("Attachment"):
        kind = attachment.get("Type")
        binding = norm(attachment.get("Binding"))
        if (int(attachment.get("Flags") or 0) & 1) or not binding or not source.has(binding):
            continue
        material = norm(attachment.get("Material")) or default_material
        if kind == "CA_SKIN" and binding.endswith(".skin"):
            matrix = np.eye(4)
        elif kind == "CA_BONE" and binding.endswith(".cgf") and "xformattach" not in binding:
            matrix = _matrix(attachment.get("Position"), attachment.get("Rotation"))
        else:
            continue
        out.append((binding, material, tuple(float(v) for v in matrix.ravel())))
    return tuple(out)


def registry_path():
    return env.content_dir() / "world-objects" / "cdf-parts.json"


@functools.lru_cache(maxsize=1)
def registry() -> dict:
    """@return {part mesh path: the .mtl that mesh.convert uses when the part has no material of its own}."""
    path = registry_path()
    return json.loads(path.read_text())["parts"] if path.is_file() else {}


def part_material(path: str) -> str:
    return registry().get(path.lower(), "")


def own_reference(f, path: str) -> str:
    """@return The material reference of a mesh file: its material chunk, else the .mtl of a .cdf prop part (the registry)."""
    from . import cgf
    return cgf.material_guid(f) or part_material(path)


def write_registry(cdfs: list[str]) -> dict:
    """Writes world-objects/cdf-parts.json for the parts of `cdfs` with no material of their own (no material reference in the
    mesh file): the first .mtl of such a part, in the sorted order of the files, is its own material; another .mtl of the same part
    is a placement override. @return Counts."""
    from . import cgf, mesh
    path = registry_path()
    known = json.loads(path.read_text())["parts"] if path.is_file() else {}
    for cdf in sorted(set(cdfs)):
        for mesh_path, material, _ in parts(cdf):
            if not material or mesh_path in known:
                continue
            try:
                f, _ = mesh.load(mesh_path)
            except Exception:
                continue
            if not cgf.material_guid(f):
                known[mesh_path] = material
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"version": 1, "parts": dict(sorted(known.items()))}, indent=1))
    registry.cache_clear()
    return {"props": len(set(cdfs)), "parts": len(known)}


def placements(cdf: str, world: np.ndarray, material_field: str | None) -> list[tuple[str, np.ndarray, str | None]]:
    """@return (part mesh path, world matrix, material override or None) of each part of a prop at `world`."""
    out = []
    for mesh_path, material, matrix in parts(cdf):
        override = material_field if (USE_MATERIAL_FIELD and material_field) else None
        if override is None and material and material != part_material(mesh_path):
            # Another .mtl of a part in the registry, or a part with a material of its own (the attachment .mtl replaces it).
            override = material
        out.append((mesh_path, world @ np.array(matrix).reshape(4, 4), override))
    return out
