"""The world census (stage 1.2): the placements of all regions of the main world, the unique meshes, materials, and
textures, the triangle counts, and the inputs of the disk budget.

The census reads the game copy only (no conversion). Output: SWC_CONTENT/census/ (made from the game copy: not in
git): `world-census.json` (the totals), `meshes.json` and `textures.json` (one row each, a cache for later runs), and
`regions/NAME/placements.npz` for each region (the input of the world level, stage 1.3).
"""
from __future__ import annotations

import json
import os
import struct
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

from . import cgf, env, materials, region, source

SOURCES = ("chunk", "capital", "capital variant", "distribution")


def world_regions() -> list[str]:
    return [f"r_+{x:02d}_+{y:02d}" for x, y in json.loads(source.read(region.WORLD + "world.json"))]


def playable_regions() -> list[str]:
    return [f"r_+{x:02d}_+{y:02d}" for x, y in json.loads(source.read(region.WORLD + "playable.json"))]


def out_dir() -> Path:
    path = env.content_dir() / "census"
    path.mkdir(parents=True, exist_ok=True)
    return path


def placements_file(name: str) -> Path:
    return env.content_dir() / "regions" / name / "placements.npz"


def region_pass(name: str) -> dict:
    """Writes the placements of one whole region (npz: the mesh table, a mesh index, a source index, and the engine
    matrices as 3 x 4 rows). @return The counts and the instances of each mesh."""
    started = time.time()
    items, counts = region.Region(name).placements(None)
    meshes = sorted({p["mesh"] for p in items})
    index = {m: i for i, m in enumerate(meshes)}
    mesh_of = np.array([index[p["mesh"]] for p in items], dtype=np.uint32)
    source_of = np.array([SOURCES.index(p["source"]) for p in items], dtype=np.uint8)
    matrices = np.array([p["matrix"][:3] for p in items], dtype=np.float64).reshape(-1, 3, 4)
    out = placements_file(name)
    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(out, meshes=np.array(meshes), mesh=mesh_of, source=source_of, matrix=matrices,
                        sources=np.array(SOURCES))
    (out.parent / "meshes.json").write_text(json.dumps(meshes))   # for the import script (no numpy in Unreal)
    instances = np.bincount(mesh_of, minlength=len(meshes)) if len(meshes) else np.zeros(0, np.int64)
    return {"region": name, "placements": len(items), "counts": counts, "unique meshes": len(meshes),
            "instances": {m: int(c) for m, c in zip(meshes, instances)}, "seconds": round(time.time() - started, 1)}


def load_placements(name: str) -> dict:
    """@return The placements of a region (npz arrays): meshes, mesh, source, matrix (n, 3, 4), sources."""
    with np.load(placements_file(name)) as data:
        return {k: data[k] for k in data.files}


def mesh_pass(path: str) -> dict:
    """@return The facts of one mesh from its chunk file (no .cgfheap: the counts are in the chunk headers)."""
    row: dict = {"mesh": path}
    if not source.has(path):
        row["error"] = "not in the game copy"
        return row
    try:
        f = cgf.read_chunks(source.read(path))
        reference = cgf.material_guid(f) or ""
        subs: list = []
        if reference:
            try:
                row["mtl"], subs = materials.load_mtl(reference, path)
            except Exception as error:
                row["material error"] = str(error)[:120]
        vertices = triangles = drawn = 0
        used: dict[int, dict] = {}
        for chunk in f.of_type(cgf.T_MESH):
            _flags, _flags2, nverts, nindices, _nsubsets, subsets_id, _va = struct.unpack_from("<7i", chunk.data, 0)
            if nverts == 0 or subsets_id not in f.by_id:
                continue
            vertices += nverts
            triangles += nindices // 3
            sub = f.by_id[subsets_id].data
            _sflags, count = struct.unpack_from("<2i", sub, 0)
            for i in range(count):
                _fi, ni, _fv, _nv, mat = struct.unpack_from("<5i", sub, 16 + i * 36)
                m = subs[mat] if 0 <= mat < len(subs) else None
                if m is not None and materials.not_drawn(m):
                    continue
                drawn += ni // 3
                if mat not in used:
                    used[mat] = slot_textures(m) if m is not None else {}
                    if m is not None:
                        used[mat]["shader"] = m.get("Shader", "")
        row.update({"vertices": vertices, "triangles": triangles, "drawn triangles": drawn,
                    "materials": {str(k): v for k, v in sorted(used.items())},
                    "lods": sum(1 for n in range(1, 6) if source.has(path.replace(".cgf", f"_lod{n}.cgf")))})
    except Exception as error:
        row["error"] = str(error)[:120]
    return row


def slot_textures(material) -> dict:
    """@return The texture products of the slots that the import uses (base color, normal, and the gloss part)."""
    out = {}
    for t in material.iter("Texture"):
        slot = t.get("Map", "")
        if slot in materials.SLOTS and t.get("AssetId"):
            product = source.product_of_text(t.get("AssetId"), ".dds")
            if product:
                out[materials.SLOTS[slot]] = product
    return out


def texture_pass(product: str) -> dict:
    """@return The size and format of one texture product (from the DDS header of its base file)."""
    row: dict = {"texture": product}
    try:
        head = source.read(product)
        height, width = struct.unpack_from("<II", head, 12)
        mips = struct.unpack_from("<I", head, 28)[0] or 1
        fmt = struct.unpack_from("<I", head, 128)[0] if head[84:88] == b"DX10" else -1
        row.update({"width": width, "height": height, "mips": mips, "format": fmt, "alpha part": source.has(product + ".a")})
    except Exception as error:
        row["error"] = str(error)[:120]
    return row


def run_pool(function, items: list, workers: int) -> list:
    if workers <= 1:
        return [function(x) for x in items]
    with ProcessPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(function, items, chunksize=max(1, len(items) // (workers * 16))))


def cached(name: str, key: str, function, items: list, workers: int) -> dict[str, dict]:
    """Runs `function` for the items that are not in the cache file `name` (incremental)."""
    path = out_dir() / name
    rows = {r[key]: r for r in json.loads(path.read_text())} if path.is_file() else {}
    todo = [x for x in items if x not in rows]
    for row in run_pool(function, todo, workers):
        rows[row[key]] = row
    path.write_text(json.dumps(sorted(rows.values(), key=lambda r: r[key]), indent=0))
    return rows


def run(names: list[str] | None = None, workers: int = 4, force: bool = False) -> dict:
    """The census of the regions (default: all 49 of the main world). @return The totals."""
    started = time.time()
    names = names or world_regions()
    playable = set(playable_regions())
    region_rows = {}
    regions_path = out_dir() / "regions.json"
    if regions_path.is_file() and not force:
        region_rows = {r["region"]: r for r in json.loads(regions_path.read_text())}
    todo = [n for n in names if n not in region_rows or not placements_file(n).is_file()]
    for row in run_pool(region_pass, todo, workers):
        region_rows[row["region"]] = row
    regions_path.write_text(json.dumps(sorted(region_rows.values(), key=lambda r: r["region"])))
    for n in names:
        if not (placements_file(n).parent / "meshes.json").is_file():
            (placements_file(n).parent / "meshes.json").write_text(json.dumps(sorted(region_rows[n]["instances"])))
    instances: dict[str, int] = {}
    for n in names:
        for m, c in region_rows[n]["instances"].items():
            instances[m] = instances.get(m, 0) + c
    mesh_rows = cached("meshes.json", "mesh", mesh_pass, sorted(instances), workers)
    textures: dict[str, set] = {}
    material_keys = set()
    for m in instances:
        row = mesh_rows[m]
        for mat, info in row.get("materials", {}).items():
            material_keys.add((row.get("mtl", ""), mat))
            for slot, product in info.items():
                if slot != "shader":
                    textures.setdefault(product, set()).add(slot)
    texture_rows = cached("textures.json", "texture", texture_pass, sorted(textures), workers)
    ok = [mesh_rows[m] for m in instances if "error" not in mesh_rows[m]]
    totals = {
        "regions": len(names), "playable regions": len([n for n in names if n in playable]),
        "placements": sum(region_rows[n]["placements"] for n in names),
        "placements by source": {s: sum(region_rows[n]["counts"].get(s, 0) for n in names) for s in SOURCES},
        "unique meshes": len(instances), "meshes with an error": len(instances) - len(ok),
        "mesh errors": sorted({mesh_rows[m]["error"] for m in instances if "error" in mesh_rows[m]}),
        "unique materials": len(material_keys), "unique textures": len(textures),
        "unique mesh triangles (LOD 0, drawn)": sum(r["drawn triangles"] for r in ok),
        "unique mesh vertices (LOD 0)": sum(r["vertices"] for r in ok),
        "instance triangles (LOD 0, drawn)": sum(mesh_rows[m].get("drawn triangles", 0) * c for m, c in instances.items()),
        "texture texels (full size)": sum(r["width"] * r["height"] for r in texture_rows.values() if "width" in r),
        "texture texels at 1024": sum(min(r["width"], 1024) * min(r["height"], 1024) for r in texture_rows.values() if "width" in r),
        "texture texels at 512": sum(min(r["width"], 512) * min(r["height"], 512) for r in texture_rows.values() if "width" in r),
        "textures by slot": {s: sum(1 for v in textures.values() if s in v) for s in ("base", "normal", "specular", "emissive")},
        "seconds": round(time.time() - started, 1),
    }
    roles: dict[str, set] = {}   # the textures that the drawn materials use, by role (the import uses base and normal)
    for m in instances:
        for info in mesh_rows[m].get("materials", {}).values():
            for role in ("base", "normal"):
                if role in info:
                    roles.setdefault(role, set()).add(info[role])
    texels = {}
    for role, products in sorted(roles.items()):
        rows = [texture_rows[t] for t in products if "width" in texture_rows.get(t, {})]
        texels[f"{role} textures"] = len(rows)
        for cap in (512, 1024, 2048):
            texels[f"{role} {cap}"] = sum(min(r["width"], cap) * min(r["height"], cap) for r in rows)
    (out_dir() / "texels.json").write_text(json.dumps(texels, indent=1))
    totals["texels of the base and normal textures"] = texels
    playable_names = [n for n in names if n in playable]
    if playable_names:
        meshes_playable = {m for n in playable_names for m in region_rows[n]["instances"]}
        totals["playable: placements"] = sum(region_rows[n]["placements"] for n in playable_names)
        totals["playable: unique meshes"] = len(meshes_playable)
    (out_dir() / "world-census.json").write_text(json.dumps(totals, indent=1))
    return totals
