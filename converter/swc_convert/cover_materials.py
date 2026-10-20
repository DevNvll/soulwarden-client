"""The materials of the ground cover (region.vegetation; the materials track for T3 of the world track, docs/MATERIALS.md).

The world track places the cover (the tile records); this module makes its meshes and materials ready:
- The descriptor table of each region.vegetation (research/terrain.md, section 3): the mesh (GUID and sub id), AutoMerge,
  Bending / 100, and the material override text. A descriptor with an override replaces all sub-materials of its mesh,
  by index (the fall01 and fall02 variants of the grass).
- The cover meshes convert with mesh rule 7 (the vertex colors: the bend data of the wind) when the manifest does not
  have them yet. A mesh that the manifest has (for example a bush of the distribution) gets rule 7 in the M3 rebuild.
- The slot map of each pair (mesh, override), in the format of overrides.py (content/overrides/pairs.json): each slot
  (materials.instance_key of the sub-material of the mesh) -> the instance key of the override sub-material with the
  same index. The manifest gets the material records of the overrides; material-keys.json lists their keys.

Output: content/cover/descriptors.json (each region: its descriptors with their record counts), content/cover/pairs.json,
content/cover/material-keys.json. material_records.audit reads the override files (cover_mtls) as the use "cover".
"""
from __future__ import annotations

import json
import struct
import time
from pathlib import Path

from . import cgf, env, manifest, materials, mesh, source

VERSION = 1   # the rule of the descriptor tables and of the slot maps (the A2 and A2b keys of materials.instance_key)


def out_dir() -> Path:
    return env.content_dir() / "cover"


def region_names() -> list[str]:
    from . import region as region_module
    prefix = region_module.WORLD + "regions/"
    return sorted({p[len(prefix):].split("/", 1)[0] for p in source.paks().entries
                   if p.startswith(prefix) and p.endswith("/region.vegetation")})


def descriptors(name: str, level: str | None = None) -> dict:
    """@return The descriptor table of one region.vegetation and the record count of each descriptor (the group headers
    of all tiles; the 16-byte records are skipped). Layout: research/terrain.md, section 3. level: the folder of the
    terrain in the game data (the world when None; cover.TERRAINS)."""
    from . import region as region_module
    data = source.read((level or region_module.WORLD) + "regions/" + name + "/region.vegetation")
    tile, size, _opaque = struct.unpack_from("<HHI", data, 0)
    tiles = (size // tile) ** 2
    offsets = struct.unpack_from(f"<{tiles}I", data, 8)
    pos = 8 + tiles * 4
    count = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    out = []
    for index in range(count):
        guid = data[pos:pos + 16]
        sub, merge, bend, length = struct.unpack_from("<IBBB", data, pos + 16)
        text = data[pos + 23:pos + 23 + length].decode("utf-8", "replace")
        pos += 23 + length
        override = text.lower().replace("\\", "/") if text else None
        if override and override.startswith("{"):
            override = source.product_of_text(override, ".mtl")
        out.append({"index": index, "mesh": source.product(guid, sub, ".cgf"), "override": override or None,
                    "bending": bend / 100, "autoMerge": merge, "records": 0})
    if count and pos != offsets[0]:
        raise ValueError(f"{name}: the descriptor table ends at {pos}, the first tile at {offsets[0]}")
    for offset in offsets:
        p = int(offset)
        groups = struct.unpack_from("<H", data, p)[0]
        p += 2
        for _ in range(groups):
            d, n = struct.unpack_from("<HH", data, p)
            out[d]["records"] += n
            p += 4 + 16 * n
    return {"region": name, "bytes": len(data), "descriptors": out}


def slot_map(pair: tuple[str, str]) -> dict:
    """@return {"pair", "version", "slots": {slot: key}, "records": [manifest records], "merged slots"} or an error: the
    rule of overrides.py with the instance keys (A2, A2b)."""
    mesh_key, material = pair
    out = {"pair": f"{mesh_key}|{material}", "version": VERSION}
    try:
        f, meshes = mesh.load(mesh_key)
        reference = cgf.material_guid(f) or ""
        defaults = materials.load_mtl(reference, mesh_key)[1] if reference else []
        overrides = materials.load_mtl(material)[1]
        used = sorted({s.material for m in meshes for s in m.subsets})
    except Exception as error:
        out["error"] = str(error)[:120]
        return out
    slots: dict[str, str] = {}
    records: dict[str, dict] = {}
    merged = 0
    for index in used:
        default = defaults[index] if 0 <= index < len(defaults) else None
        if default is not None and materials.not_drawn(default):
            continue
        slot = materials.instance_key(default)
        replacement = overrides[index] if 0 <= index < len(overrides) else None
        if replacement is None or materials.not_drawn(replacement):
            continue
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


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=1, sort_keys=True))
    temporary.replace(path)


def scan(names: list[str] | None = None, workers: int = 4) -> dict:
    """Writes content/cover/descriptors.json for the regions (all with a region.vegetation by default). @return The counts."""
    from concurrent.futures import ProcessPoolExecutor
    started = time.time()
    names = names or region_names()
    with ProcessPoolExecutor(max_workers=workers) as pool:
        rows = list(pool.map(descriptors, names))
    regions = {r["region"]: r["descriptors"] for r in rows}
    write_json(out_dir() / "descriptors.json", {"version": VERSION, "regions": regions})
    meshes = {d["mesh"] for ds in regions.values() for d in ds if d["mesh"]}
    pairs = {(d["mesh"], d["override"]) for ds in regions.values() for d in ds if d["mesh"] and d["override"]}
    return {"regions": len(regions), "descriptors": sum(len(ds) for ds in regions.values()), "meshes": len(meshes),
            "overrides": len({o for _m, o in pairs}), "pairs": len(pairs),
            "records": sum(d["records"] for ds in regions.values() for d in ds),
            "no mesh": sum(1 for ds in regions.values() for d in ds if not d["mesh"]),
            "bending": sorted({d["bending"] for ds in regions.values() for d in ds}),
            "seconds": round(time.time() - started, 1)}


def table() -> dict:
    return json.loads((out_dir() / "descriptors.json").read_text())["regions"]


def cover_meshes() -> list[str]:
    return sorted({d["mesh"] for ds in table().values() for d in ds if d["mesh"]})


def cover_mtls() -> dict[str, list[str]]:
    """@return Each override material file of the cover -> its meshes (the audit: the use "cover")."""
    out: dict[str, set] = {}
    for ds in table().values():
        for d in ds:
            if d["mesh"] and d["override"]:
                out.setdefault(d["override"], set()).add(d["mesh"])
    return {k: sorted(v) for k, v in out.items()}


def convert_one(path: str) -> dict:
    try:
        return mesh.convert(path, force=True, write_manifest=False, rule=mesh.RULE_COLORS)
    except Exception as error:
        return {"mesh": path, "error": str(error)[:160]}


def convert_meshes(workers: int = 4, again: bool = False) -> dict:
    """Converts the cover meshes that the manifest does not have (`again`: also the cover meshes of rule 7 that this
    module converted, after the material keys changed) with rule 7, and writes their manifest records one time."""
    from concurrent.futures import ProcessPoolExecutor
    started = time.time()
    assets = manifest.load()["assets"]
    todo = [m for m in cover_meshes() if m not in assets or (again and assets[m].get("cover"))]
    with ProcessPoolExecutor(max_workers=workers) as pool:
        reports = list(pool.map(convert_one, todo, chunksize=4))
    converted = [r for r in reports if "entry" in r]
    for r in converted:
        r["entry"]["cover"] = True   # converted for the cover (the M3 rebuild set has the other cover meshes)
    if converted:
        manifest.add_many(mesh.records_of(converted))
    errors = [r for r in reports if "error" in r]
    return {"cover meshes": len(cover_meshes()), "in the manifest before": len(cover_meshes()) - len(todo),
            "converted": len(converted), "triangles": sum(r.get("triangles", 0) for r in converted),
            "companions": sum(1 for r in converted if "companion" in r), "errors": len(errors),
            "first errors": [(r["mesh"], r["error"]) for r in errors[:5]], "seconds": round(time.time() - started, 1)}


def pairs(workers: int = 4) -> dict:
    """Writes content/cover/pairs.json (the slot maps), the material records of the overrides to the manifest, and
    content/cover/material-keys.json (the manifest keys: tools/import.sh --keys makes their instances; the n_ keys
    have no record: the apply step makes them). @return The counts."""
    from concurrent.futures import ProcessPoolExecutor
    wanted = sorted({(d["mesh"], d["override"]) for ds in table().values() for d in ds if d["mesh"] and d["override"]})
    with ProcessPoolExecutor(max_workers=workers) as pool:
        rows = list(pool.map(slot_map, wanted, chunksize=4))
    records: dict[str, dict] = {}
    known = {}
    for row in rows:
        for record in row.pop("records", []):
            records[record["asset"]] = {k: v for k, v in record.items() if k != "asset"}
        known[row["pair"]] = row
    if records:
        manifest.add_many(records)
    write_json(out_dir() / "pairs.json", known)
    # The keys for tools/import.sh --keys: the override materials, and the material records of the cover meshes (the
    # meshes that this module converted have new records: their instances must exist before the mesh import).
    assets = manifest.load()["assets"]
    keys = {"material:" + key for row in known.values() for key in row.get("slots", {}).values()}
    keys |= {"material:" + key for m in cover_meshes() for key in assets.get(m, {}).get("materials", [])}
    keys = sorted(k for k in keys if k in assets)
    (out_dir() / "material-keys.json").write_text(json.dumps(keys, indent=0))
    errors = [row for row in rows if "error" in row]
    return {"pairs": len(wanted), "errors": len(errors), "error examples": [(r["pair"], r["error"]) for r in errors[:5]],
            "pairs with a slot change": sum(1 for r in rows if r.get("slots")), "manifest material keys": len(keys),
            "n_ keys": len({k for row in known.values() for k in row.get("slots", {}).values() if k.startswith("n_")}),
            "records": len(records),
            "merged slots": sum(r.get("merged slots", 0) for r in rows)}


def main(argv: list[str]) -> dict:
    if not argv or argv[0] not in ("scan", "convert", "pairs"):
        raise SystemExit("usage: cover scan [REGION...] | convert [--again] | pairs")
    if argv[0] == "scan":
        return scan(argv[1:] or None)
    if argv[0] == "convert":
        return convert_meshes(again="--again" in argv)
    return pairs()
