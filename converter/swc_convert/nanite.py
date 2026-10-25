"""The meshes that must not have Nanite because a slot that they draw is translucent (AST-288, class W2).

Nanite draws only opaque and masked materials. A mesh with a translucent slot gets no Nanite at its conversion (mesh.convert: the
stage 1.2 blend of its own records). Two cases escape that rule:
- a placement override gives a slot an instance with a translucent parent (the pairs of overrides.py: the fx override of the
  banana plants, the glass of jav_prp_window_a);
- the spec of an own slot has a translucent parent that the stage 1.2 record does not have (the C8 effect specs of merged chunks).
Unreal then draws the engine default material (the grey checker; "Invalid material ... used on Nanite static mesh" in the log).

write() lists these meshes in SWC_CONTENT/materials/no-nanite.json. The import (Scripts/import_content.py) imports a listed mesh
with the pipeline with no Nanite, and its import stamp gets NO_NANITE_STAMP: only the listed meshes import again. No cell rebuild
for the meshes (their slots do not change); the cells of a new pair rebuild as usual.
"""
from __future__ import annotations

import json
from pathlib import Path

from . import env, manifest

# The parents of swc_materials that are not opaque or masked (PARENT_NAMES; "legacy translucent": the stage 1.2 base parent).
TRANSLUCENT_PARENTS = frozenset({"beam", "fxmesh", "fxmesh additive", "meshparticle", "glass", "surface translucent",
                                 "legacy translucent"})
NO_NANITE_STAMP = ":no-nanite"
SPEC_FILES = ("materials/instances-candidate.json", "decals/instances.json", "materials/instances.json")   # the last one wins


def path() -> Path:
    return env.content_dir() / "materials" / "no-nanite.json"


def parent_of(key: str, specs: dict, assets: dict) -> str:
    """@return The parent of a material instance key: its spec, else the stage 1.2 base parent of its record ("legacy BLEND")."""
    spec = specs.get(key)
    if spec and spec.get("parent"):
        return spec["parent"]
    return "legacy " + assets.get("material:" + key, {}).get("blend", "opaque")


def blocked(assets: dict, pairs: dict, specs: dict) -> dict[str, list[str]]:
    """@return {mesh key: the translucent keys that it draws} for each static mesh with Nanite (assets: the manifest; pairs: the
    slot maps of overrides.py; specs: the material specs) that draws a translucent slot (own slot or a slot of a pair)."""
    out: dict[str, set[str]] = {}
    for key, entry in assets.items():
        if entry.get("kind") != "static" or not entry.get("nanite", True):
            continue
        for slot in entry.get("slots") or entry.get("materials") or []:
            if parent_of(slot, specs, assets) in TRANSLUCENT_PARENTS:
                out.setdefault(key, set()).add(slot)
    for pair, row in pairs.items():
        mesh_key = pair.split("|", 1)[0]
        entry = assets.get(mesh_key, {})
        if entry.get("kind") != "static" or not entry.get("nanite", True):
            continue
        own = set(entry.get("slots") or entry.get("materials") or [])
        for slot, key in row.get("slots", {}).items():
            if slot in own and parent_of(key, specs, assets) in TRANSLUCENT_PARENTS:
                out.setdefault(mesh_key, set()).add(key)
    return {k: sorted(v) for k, v in sorted(out.items())}


def load_specs(content: Path | None = None) -> dict:
    content = content or env.content_dir()
    specs: dict = {}
    for name in SPEC_FILES:
        file = content / name
        if file.is_file():
            specs.update(json.loads(file.read_text())["instances"])
    return specs


def write() -> dict:
    """Writes no-nanite.json from the manifest, the pairs, and the specs. @return A summary."""
    assets = manifest.load()["assets"]
    pairs_file = env.content_dir() / "overrides" / "pairs.json"
    pairs = json.loads(pairs_file.read_text()) if pairs_file.is_file() else {}
    meshes = blocked(assets, pairs, load_specs())
    path().write_text(json.dumps({"version": 1, "meshes": meshes}, indent=1, sort_keys=True))
    return {"meshes": len(meshes), "file": str(path())}


def listed(content: Path) -> set[str]:
    """@return The mesh keys of no-nanite.json (empty when the file is not there)."""
    file = content / "materials" / "no-nanite.json"
    return set(json.loads(file.read_text())["meshes"]) if file.is_file() else set()
