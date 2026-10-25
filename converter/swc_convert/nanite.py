"""The meshes that must not have Nanite because a slot that they draw is translucent (AST-288, class W2).

Nanite draws only opaque and masked materials. A mesh with a translucent slot gets no Nanite at its conversion (mesh.convert: the
stage 1.2 blend of its own records). Two cases escape that rule:
- a placement override gives a slot an instance with a translucent parent (the pairs of overrides.py: the fx override of the
  banana plants, the glass of jav_prp_window_a);
- the spec of an own slot has a translucent parent that the stage 1.2 record does not have (the C8 effect specs of merged chunks).
Unreal then draws the engine default material (the grey checker; "Invalid material ... used on Nanite static mesh" in the log).

A mesh loses Nanite for all its placements. Thus a mesh is listed only when an own slot is translucent or when at least half of
its placements (MAJORITY) draw a translucent pair: one fx override on 1 of the 19,881 placements of jav_struc_ruins_wall_bb must not
take Nanite from the other 19,880. The other pairs are "minority" pairs: they keep the engine default material on that slot until a
cell flag gives their entries no Nanite (bDisallowNanite on the component of the entry: a C++ step).

write() lists these meshes in SWC_CONTENT/materials/no-nanite.json. The import (Scripts/import_content.py) imports a listed mesh
with the pipeline with no Nanite, and its import stamp gets NO_NANITE_STAMP: only the listed meshes import again. No cell rebuild
for the meshes (their slots do not change); the cells of a new pair rebuild as usual.
"""
from __future__ import annotations

import json
from pathlib import Path

from . import census, env, manifest

# The parents of swc_materials that are not opaque or masked (PARENT_NAMES; "legacy translucent": the stage 1.2 base parent).
TRANSLUCENT_PARENTS = frozenset({"beam", "beam simple", "fxmesh", "fxmesh additive", "meshparticle", "glass",
                                 "surface translucent", "legacy translucent"})
NO_NANITE_STAMP = ":no-nanite"
SPEC_FILES = ("materials/instances-candidate.json", "decals/instances.json", "materials/instances.json")   # the last one wins
MAJORITY = 0.5


def path() -> Path:
    return env.content_dir() / "materials" / "no-nanite.json"


def parent_of(key: str, specs: dict, assets: dict) -> str:
    """@return The parent of a material instance key: its spec, else the stage 1.2 base parent of its record ("legacy BLEND")."""
    spec = specs.get(key)
    if spec and spec.get("parent"):
        return spec["parent"]
    return "legacy " + assets.get("material:" + key, {}).get("blend", "opaque")


def blocked(assets: dict, pairs: dict, specs: dict, counts: dict | None = None) -> dict[str, list[str]]:
    """@return {mesh key: the translucent keys that it draws} for each static mesh with Nanite (assets: the manifest; pairs: the
    slot maps of overrides.py; specs: the material specs) that draws a translucent slot (own slot or a slot of a pair). With
    `counts` ({mesh key: {override material or "": placements}}) a mesh with only pairs is listed when the placements of its
    translucent pairs are at least MAJORITY of its placements (minority() gives the others)."""
    listed_ = translucent_slots(assets, pairs, specs)
    if counts is None:
        return {k: sorted(v) for k, v in sorted(listed_.items())}
    return {k: sorted(v) for k, v in sorted(listed_.items()) if share(k, assets, pairs, specs, counts) >= MAJORITY}


def minority(assets: dict, pairs: dict, specs: dict, counts: dict) -> dict[str, dict]:
    """@return {mesh key: {"translucent": placements, "all": placements}} of the meshes with translucent pairs below MAJORITY."""
    out = {}
    for k in translucent_slots(assets, pairs, specs):
        s = share(k, assets, pairs, specs, counts)
        if s < MAJORITY:
            total = sum(counts.get(k, {}).values())
            out[k] = {"translucent": round(s * total), "all": total}
    return out


def share(mesh_key: str, assets: dict, pairs: dict, specs: dict, counts: dict) -> float:
    """@return The part of the placements of a mesh that draw a translucent slot (1 for a translucent own slot)."""
    entry = assets[mesh_key]
    own = entry.get("slots") or entry.get("materials") or []
    if any(parent_of(s, specs, assets) in TRANSLUCENT_PARENTS for s in own):
        return 1.0
    by_override = counts.get(mesh_key, {})
    total = sum(by_override.values())
    if not total:
        return 1.0   # no placement in the census (the menu scene, the server visuals): list it
    hit = sum(n for material, n in by_override.items() if material and translucent_pair(mesh_key, material, assets, pairs, specs))
    return hit / total


def translucent_pair(mesh_key: str, material: str, assets: dict, pairs: dict, specs: dict) -> bool:
    own = set(assets[mesh_key].get("slots") or assets[mesh_key].get("materials") or [])
    row = pairs.get(f"{mesh_key}|{material}", {})
    return any(s in own and parent_of(k, specs, assets) in TRANSLUCENT_PARENTS for s, k in row.get("slots", {}).items())


def translucent_slots(assets: dict, pairs: dict, specs: dict) -> dict[str, set[str]]:
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
    return out


def placement_counts(regions: list[str]) -> dict:
    """@return {mesh key: {override material or "": placements}} of the census placements of the regions."""
    out: dict = {}
    for name in regions:
        data = census.load_placements(name)
        meshes = [str(m) for m in data["meshes"]]
        mats = [str(m) for m in data["materials"]] if "materials" in data else [""]
        material = data["material"].tolist() if "material" in data else [0] * len(data["mesh"])
        for m, o in zip(data["mesh"].tolist(), material):
            row = out.setdefault(meshes[m], {})
            row[mats[o]] = row.get(mats[o], 0) + 1
    return out


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
    specs = load_specs()
    regions = sorted(p.parent.name for p in (env.content_dir() / "regions").glob("*/placements.npz"))
    counts = placement_counts(regions)
    meshes = blocked(assets, pairs, specs, counts)
    few = minority(assets, pairs, specs, counts)
    path().write_text(json.dumps({"version": 1, "majority": MAJORITY, "meshes": meshes, "minority": few}, indent=1, sort_keys=True))
    return {"meshes": len(meshes), "minority meshes": len(few),
            "minority placements": sum(v["translucent"] for v in few.values()), "file": str(path())}


def listed(content: Path) -> set[str]:
    """@return The mesh keys of no-nanite.json (empty when the file is not there)."""
    file = content / "materials" / "no-nanite.json"
    return set(json.loads(file.read_text())["meshes"]) if file.is_file() else set()
