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

Output: content/overrides/pairs.json (the slot map of each pair, "MESH|MTL" -> {"slots": {slot: key}}), the material
and texture records in the manifest, and content/overrides/material-keys.json (the manifest keys of the override
materials, for tools/import.sh --keys). cells.py uses the slot maps.
"""
from __future__ import annotations

import json
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from . import census, cgf, env, manifest, materials, mesh

VERSION = 2   # the rule of the slot maps (with materials.TEXTURE_VERSION and the material keys of materials.py;
              # 2: the instance keys of materials.instance_key for the slots and the overrides, A2 and A2b)


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
        reference = cgf.material_guid(f) or ""
        defaults = materials.load_mtl(reference, mesh_key)[1] if reference else []
        overrides = materials.load_mtl(material)[1]
    except Exception as error:   # a mesh or a material that does not decode: the placement keeps the mesh material
        out["error"] = str(error)[:120]
        return out
    slots: dict[str, str] = {}
    records: dict[str, dict] = {}
    merged = 0
    used = sorted({s.material for m in meshes for s in m.subsets})
    for index in used:
        default = defaults[index] if 0 <= index < len(defaults) else None
        if default is not None and materials.not_drawn(default):
            continue   # no slot (mesh rule 6)
        slot = materials.instance_key(default)
        replacement = overrides[index] if 0 <= index < len(overrides) else None
        if replacement is None or materials.not_drawn(replacement):
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
    todo = sorted(p for p in wanted if known.get(pair_key(*p), {}).get("version") != VERSION)
    if workers > 1 and len(todo) > 1:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            rows = list(pool.map(slot_map, todo, chunksize=8))
    else:
        rows = [slot_map(p) for p in todo]
    records: dict[str, dict] = {}
    for row in rows:
        for record in row.pop("records", []):
            records[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
        known[row["pair"]] = row
    if records:
        manifest.add_many(records)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(known, indent=0, sort_keys=True))
    temporary.replace(path)
    keys = sorted({"material:" + key for row in known.values() for key in row.get("slots", {}).values()})
    (out_dir() / "material-keys.json").write_text(json.dumps(keys, indent=0))
    errors = [row for row in rows if "error" in row]
    return {"pairs": len(wanted), "new pairs": len(todo), "errors": len(errors),
            "error examples": [(r["pair"], r["error"]) for r in errors[:5]],
            "pairs with a slot change": sum(1 for p in wanted if known.get(pair_key(*p), {}).get("slots")),
            "merged slots": sum(r.get("merged slots", 0) for r in rows), "material keys": len(keys),
            "records": len(records)}
