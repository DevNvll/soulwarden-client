"""The material overrides of the placements (P4.1): a placement can replace the material of its mesh.

A slice entity has the field Material Override Asset in its mesh render node; a mesh of the slice metadata (a variant)
has materialOverrideAssetId (region.py). Near Oxboro the fall variants of the poplars and the pawpaw bushes select their
fall materials, and the lanterns select their on and off materials. The override replaces all the sub-materials of the
mesh, by index.

The slots of an imported mesh are named by the instance key of their sub-material (materials.instance_key: the key of
its material record, or the n_ key of A1 when its render rules are not the main rules of that key; mesh rule 7 names
the slots so, and a mesh of rule 6 has only main rules). For a pair (mesh, override), the slot map gives for each slot
the instance key of the override sub-material with the same index (A2: the instance with the rules of that
sub-material); the manifest gets its material record. Two sub-materials of the mesh with the same key are one slot:
when the override gives them different keys, the first one wins ("merged slots" in the report).

The override also replaces the %DECAL sub-materials, which mesh rule 7 draws as the decal companion (AST-275 C11). "decal slots"
gives for each slot of the companion (its dm_ key) the dm_ key of the override sub-material with the same index, or "" when that
sub-material is not a mesh decal (retail then has no decal there: the client leaves the slot out). Before, the companion of every
placement drew the decals of the mesh's own .mtl: 66,837 placements of the world drew a wrong or an extra decal (the dead oaks and
the damned trees with their "full" .mtl, the dead spruces, the green moss of cliffs_mossy on the crater cliff meshes).

Output: content/overrides/pairs.json (the slot map of each pair, "MESH|MTL" -> {"slots": {slot: key}, "decal slots": {dm key:
dm key or ""}}), the material and texture records in the manifest, content/decals/mesh-materials.json (the decal sub-materials of
the overrides, for decals.specs), and content/overrides/material-keys.json (the manifest keys of the override materials, for
tools/import.sh --keys). cells.py uses the slot maps.
"""
from __future__ import annotations

import json
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from . import census, cgf, env, manifest, materials, mesh, props

VERSION = 3   # the rule of the slot maps (with materials.TEXTURE_VERSION and the material keys of materials.py;
              # 2: the instance keys of materials.instance_key for the slots and the overrides, A2 and A2b;
              # 3: the GeometryBeam slots of mesh rule 8, AST-207)
# The rule of the decal slots (AST-275 C11), apart from VERSION: a pair of an older VERSION keeps its slot map (7,117 of the 7,683
# pairs of the world were version 1 on 2026-10-24) and only gets its decal slots.
DECAL_VERSION = 1


def out_dir() -> Path:
    return env.content_dir() / "overrides"


def pair_key(mesh_key: str, material: str) -> str:
    return f"{mesh_key}|{material}"


def slot_map(pair: tuple[str, str]) -> dict:
    """@return {"pair", "slots": {slot key: override key}, "records": [manifest records], "merged slots": n} or an error."""
    mesh_key, material = pair
    out = {"pair": pair_key(mesh_key, material), "version": VERSION}
    try:
        f, meshes = mesh.load(mesh_key)
        reference = props.own_reference(f, mesh_key)
        override_path, overrides = materials.load_mtl(material)
    except Exception as error:   # a mesh or an override that does not decode: the placement keeps the mesh material
        out["error"] = str(error)[:120]
        return out
    try:
        default_path, defaults = materials.load_mtl(reference, mesh_key) if reference else ("", [])
    except Exception:
        # The material of the mesh is not in the game copy (AST-275: for example objects/vfx/mesh/structures/checker.mtl of the
        # glyph bridges): the override replaces it, so each slot is the slot of a mesh with no material (materials.instance_key
        # of no sub-material). Before, the pair had an error and the placement drew the fallback material.
        default_path, defaults = "", []
    slots: dict[str, str] = {}
    records: dict[str, dict] = {}
    merged = 0
    used = sorted({s.material for m in meshes for s in m.subsets})
    for index in used:
        default = materials.sub_material(defaults, index)
        if default is not None and materials.not_drawn(default, beams=True):
            continue   # no slot (mesh rule 6; a GeometryBeam slot of mesh rule 8 is a slot)
        slot = materials.instance_key(default)
        replacement = materials.sub_material(overrides, index)
        if replacement is None or materials.not_drawn(replacement, beams=True):
            continue   # the slot keeps its material
        record, textures = materials.shared_material(replacement)
        records[record["asset"]] = record
        for texture in textures:
            records[texture["asset"]] = texture
        key = materials.instance_key(replacement)
        if slot in slots and slots[slot] != key:
            merged += 1
            continue
        if key != slot:
            slots[slot] = key
    out.update({"slots": slots, "records": list(records.values()), "merged slots": merged})
    out.update(decal_slot_map(mesh_key, used, default_path, defaults, override_path, overrides))
    return out


def decal_slot_map(mesh_key: str, used: list[int], default_path: str, defaults: list, override_path: str, overrides: list) -> dict:
    """C11: the slots of the decal companion (mesh rule 7 names them mesh.decal_key of the own sub-material; mesh.convert).
    @return {"decal slots": {own dm key: the override's dm key, or ""}, "decals": {dm key: {"mtl", "index"}}, "decal version"}."""
    decal_slots: dict[str, str] = {}
    decals: dict[str, dict] = {}
    if default_path and mesh.rule_for(mesh_key) >= mesh.RULE_COLORS:
        for index in used:
            default = materials.sub_material(defaults, index)
            if not mesh.is_mesh_decal(default):
                continue
            slot = mesh.decal_key(default_path, index, default)
            replacement = materials.sub_material(overrides, index)
            key = mesh.decal_key(override_path, index, replacement) if mesh.is_mesh_decal(replacement) else ""
            if key:
                decals.setdefault(key, {"mtl": override_path, "index": index})
            if slot not in decal_slots and key != slot:
                decal_slots[slot] = key   # two subsets with one slot: the first one wins (as the slot map)
    return {"decal slots": decal_slots, "decals": decals, "decal version": DECAL_VERSION}


def decal_slots_only(pair: tuple[str, str]) -> dict:
    """@return {"pair", "decal slots", "decals", "decal version"} of a pair whose slot map stays (an older VERSION), or an error."""
    mesh_key, material = pair
    out = {"pair": pair_key(mesh_key, material)}
    try:
        f, meshes = mesh.load(mesh_key)
        reference = props.own_reference(f, mesh_key)
        override_path, overrides = materials.load_mtl(material)
    except Exception as error:
        out["decal error"] = str(error)[:120]
        return out
    try:
        default_path, defaults = materials.load_mtl(reference, mesh_key) if reference else ("", [])
    except Exception:
        default_path, defaults = "", []   # no material of its own in the game copy (as slot_map): no decal companion
    used = sorted({s.material for m in meshes for s in m.subsets})
    out.update(decal_slot_map(mesh_key, used, default_path, defaults, override_path, overrides))
    return out


def convert(names: list[str], workers: int = 4) -> dict:
    """Makes the slot maps of the override pairs of the regions (only the new ones), writes the records to the manifest,
    and writes the key list. @return A report."""
    wanted: set[tuple[str, str]] = set()
    for name in names:
        data = census.load_placements(name)
        if "materials" not in data:
            continue
        meshes, overrides = data["meshes"], data["materials"]
        for m, o in set(zip(data["mesh"].tolist(), data["material"].tolist())):
            if o:
                wanted.add((str(meshes[m]), str(overrides[o])))
    return {"regions": len(names), **add_pairs(wanted, workers)}


def add_pairs(wanted: set[tuple[str, str]], workers: int = 4) -> dict:
    """Makes the slot maps of the pairs (mesh, override material) that pairs.json does not have in this version, writes their
    records to the manifest, and writes the key list (also for the menu scene: frontend.py). @return A report."""
    out_dir().mkdir(parents=True, exist_ok=True)
    path = out_dir() / "pairs.json"
    known = json.loads(path.read_text()) if path.is_file() else {}
    todo = sorted(p for p in wanted if known.get(pair_key(*p), {}).get("version") != VERSION or "error" in known.get(pair_key(*p), {}))
    if workers > 1 and len(todo) > 1:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            rows = list(pool.map(slot_map, todo, chunksize=8))
    else:
        rows = [slot_map(p) for p in todo]
    records: dict[str, dict] = {}
    decals: dict[str, dict] = {}
    for row in rows:
        for record in row.pop("records", []):
            records[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
        for key, m in row.pop("decals", {}).items():
            decals.setdefault(key, m)
        known[row["pair"]] = row
    if records:
        manifest.add_many(records)
    new_decals = add_decal_materials(decals)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(known, indent=0, sort_keys=True))
    temporary.replace(path)
    keys = sorted({"material:" + key for row in known.values() for key in row.get("slots", {}).values()})
    (out_dir() / "material-keys.json").write_text(json.dumps(keys, indent=0))
    errors = [row for row in rows if "error" in row]
    return {"pairs": len(wanted), "new pairs": len(todo), "errors": len(errors),
            "error examples": [(r["pair"], r["error"]) for r in errors[:5]],
            "pairs with a slot change": sum(1 for p in wanted if known.get(pair_key(*p), {}).get("slots")),
            "pairs with a decal slot change": sum(1 for p in wanted if known.get(pair_key(*p), {}).get("decal slots")),
            "new decal materials": new_decals,
            "merged slots": sum(r.get("merged slots", 0) for r in rows), "material keys": len(keys),
            "records": len(records)}


def add_decal_materials(decals: dict[str, dict]) -> int:
    """Adds the decal sub-materials of the overrides (C11) that decals/mesh-materials.json does not have: decals.specs makes their
    dm_ specs from that file. @return The count of new keys."""
    path = env.content_dir() / "decals" / "mesh-materials.json"
    doc = json.loads(path.read_text()) if path.is_file() else {"version": 1, "materials": {}}
    new = [key for key in decals if key not in doc["materials"]]
    if new:
        for key in new:
            doc["materials"][key] = {**decals[key], "uses": 0}
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(doc, indent=1, sort_keys=True))
        temporary.replace(path)
    return len(new)


def add_decal_slots(names: list[str], workers: int = 4) -> dict:
    """C11: the decal slots of the override pairs of the regions that pairs.json has with no "decal version" DECAL_VERSION. The slot
    map and the version of each pair stay (add_pairs would make an older pair again with the newer slot rules). A pair that
    pairs.json does not have is not added (add_pairs makes it, with its decal slots). @return A report."""
    path = out_dir() / "pairs.json"
    known = json.loads(path.read_text()) if path.is_file() else {}
    wanted: set[tuple[str, str]] = set()
    for name in names:
        data = census.load_placements(name)
        if "materials" not in data:
            continue
        meshes, materials_ = data["meshes"], data["materials"]
        for m, o in set(zip(data["mesh"].tolist(), data["material"].tolist())):
            if o:
                wanted.add((str(meshes[m]), str(materials_[o])))
    todo = sorted(p for p in wanted if pair_key(*p) in known and "error" not in known[pair_key(*p)]
                  and known[pair_key(*p)].get("decal version") != DECAL_VERSION)
    if workers > 1 and len(todo) > 1:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            rows = list(pool.map(decal_slots_only, todo, chunksize=8))
    else:
        rows = [decal_slots_only(p) for p in todo]
    decals: dict[str, dict] = {}
    errors = []
    for row in rows:
        if "decal error" in row:
            errors.append((row["pair"], row["decal error"]))
            continue
        for key, m in row.pop("decals").items():
            decals.setdefault(key, m)
        entry = known[row["pair"]]
        entry["decal version"] = row["decal version"]
        if row["decal slots"]:
            entry["decal slots"] = row["decal slots"]
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(known, indent=0, sort_keys=True))
    temporary.replace(path)
    return {"regions": len(names), "pairs": len(wanted), "pairs not known": sum(1 for p in wanted if pair_key(*p) not in known),
            "decal pairs done": len(rows), "errors": len(errors), "error examples": errors[:5],
            "pairs with a decal slot change": sum(1 for row in rows if row.get("decal slots")),
            "decal slots with no decal": sum(1 for row in rows for k in row.get("decal slots", {}).values() if not k),
            "new decal materials": add_decal_materials(decals)}
