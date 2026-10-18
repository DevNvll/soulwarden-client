"""Static meshes (.cgf + .cgfheap) -> glTF 2.0 with PNG textures."""
from __future__ import annotations

import hashlib
from pathlib import Path

import numpy as np

from . import cgf, env, gltf, manifest, materials, source


def out_path(asset: str) -> Path:
    return env.content_dir() / (asset.rsplit(".", 1)[0] + ".gltf")


def load(path: str) -> tuple[cgf.ChunkFile, list[cgf.Mesh]]:
    """@return The chunk file and its meshes, each one in the space of the file (the node matrices are applied)."""
    heap = source.read(path + "heap") if source.has(path + "heap") else None
    f = cgf.read_chunks(source.read(path), heap)
    nodes = cgf.node_matrices(f)
    meshes = []
    for chunk in f.of_type(cgf.T_MESH):
        try:
            m = cgf.read_mesh(f, chunk)
        except ValueError as error:
            if "no geometry" not in str(error):
                raise
            continue
        if chunk.id in nodes and not np.allclose(nodes[chunk.id], np.eye(4), atol=1e-6):
            m = cgf.transform_mesh(m, nodes[chunk.id])
        meshes.append(m)
    if not meshes:
        raise ValueError(f"{path}: no mesh chunk with geometry")
    return f, meshes


# The rule of the static meshes: 1 = stage P0 (textures in each glTF); 2 = stage 1.2 (shared materials);
# 3 = the shared PNGs of materials.TEXTURE_VERSION 2; 4 = a texture with only a file path (materials.texture_product);
# 5 = the Opacity map of a masked material is the alpha of its base color; 6 = NoDraw, GeometryBeam, and decal
# sub-materials are not drawn (materials.not_drawn).
VERSION = 6


def compact(indices: np.ndarray, *arrays):
    """@return The indices of one primitive and the vertex arrays with only the vertices that it uses."""
    used, local = np.unique(indices, return_inverse=True)
    return local.astype(np.uint32), [a[used] if a is not None else None for a in arrays]


def file_hash(gltf_path: Path) -> str:
    digest = hashlib.sha1(gltf_path.read_bytes())
    digest.update(gltf_path.with_suffix(".bin").read_bytes())
    return digest.hexdigest()[:16]


def convert(path: str, force: bool = False, write_manifest: bool = True) -> dict:
    """Converts one static mesh to glTF with shared materials: each glTF material is only the key of a material
    record (materials.shared_material); the textures are their own assets. All chunks are one glTF mesh with one node
    (Interchange makes one static mesh asset for each node). @return A report with the manifest records."""
    path = path.lower()
    out = out_path(path)
    if not force and out.is_file():
        entry = manifest.load()["assets"].get(path, {})
        if entry.get("version") == VERSION:
            return {"mesh": path, "skipped": "up to date"}
    f, meshes = load(path)
    reference = cgf.material_guid(f) or ""
    sub_materials: list = []
    report: dict = {"mesh": path, "triangles": 0, "vertices": 0}
    if reference:
        try:
            report["mtl"], sub_materials = materials.load_mtl(reference, path)
        except Exception as error:
            report["material error"] = str(error)[:120]
    w = gltf.Writer()
    slots: dict[int, int] = {}          # sub-material index -> glTF material index (-1: not drawn)
    by_key: dict[str, int] = {}         # material key -> glTF material index
    records: dict[str, dict] = {}
    textures: dict[str, dict] = {}
    prims = []
    for mesh in meshes:
        pos = gltf.z_up_to_y_up(mesh.positions)
        nrm = gltf.z_up_to_y_up(mesh.normals) if mesh.normals is not None else None
        tan = None
        if mesh.tangents is not None:
            tan = mesh.tangents.copy()
            tan[:, :3] = gltf.z_up_to_y_up(mesh.tangents[:, :3])
        for s in mesh.subsets:
            if s.material not in slots:
                m = sub_materials[s.material] if 0 <= s.material < len(sub_materials) else None
                if m is not None and materials.not_drawn(m):
                    slots[s.material] = -1
                else:
                    record, used = materials.shared_material(m)
                    records[record["name"]] = record
                    textures.update({t["asset"]: t for t in used})
                    if record["name"] not in by_key:
                        by_key[record["name"]] = w.material(record["name"])
                    slots[s.material] = by_key[record["name"]]
            if slots[s.material] == -1:
                continue   # collision (Nodraw) and shadow proxies are not drawn
            idx, (p, n, uv, t) = compact(mesh.indices[s.first_index:s.first_index + s.num_indices], pos, nrm, mesh.uvs, tan)
            prims.append(w.primitive(p, idx, n, uv, t, material=slots[s.material]))
            report["triangles"] += len(idx) // 3
            report["vertices"] += len(p)
    if not prims:
        raise ValueError(f"{path}: no drawn triangles")
    out.parent.mkdir(parents=True, exist_ok=True)
    w.doc["scenes"][0]["nodes"] = [w.node(name=Path(path).stem, mesh=w.mesh(Path(path).stem, prims))]
    w.save(out)
    translucent = any(r["blend"] == "translucent" for r in records.values())
    entry = {"kind": "static", "file": str(out.relative_to(env.content_dir())), "version": VERSION,
             "hash": file_hash(out), "materials": sorted(records), "nanite": not translucent}
    report.update({"entry": entry, "materials": list(records.values()), "textures": list(textures.values())})
    if write_manifest:
        manifest.add_many(records_of([report]))
    return report


def records_of(reports: list[dict]) -> dict[str, dict]:
    """@return The manifest entries of mesh reports: the meshes, their materials, and their textures."""
    out: dict[str, dict] = {}
    for r in reports:
        if "entry" not in r:
            continue
        out[r["mesh"]] = r["entry"]
        for record in r["materials"] + r["textures"]:
            entry = {k: v for k, v in record.items() if k != "asset"}
            out[record["asset"]] = entry
    return out


def drawn_positions(path: str) -> np.ndarray:
    """@return The engine positions of the vertices of the drawn triangles of a mesh (the glTF has no others)."""
    f, meshes = load(path.lower())
    reference = cgf.material_guid(f) or ""
    sub_materials = materials.load_mtl(reference, path.lower())[1] if reference else []
    out = []
    for m in meshes:
        for s in m.subsets:
            if s.material < len(sub_materials) and materials.not_drawn(sub_materials[s.material]):
                continue
            out.append(m.positions[np.unique(m.indices[s.first_index:s.first_index + s.num_indices])])
    return np.concatenate(out) if out else np.zeros((0, 3), np.float32)


def convert_group(key: str, out: Path, parts: list[tuple[str, np.ndarray]]) -> dict:
    """Converts several static meshes into one glTF mesh (the same rule as convert): `parts` are (path, offset in
    engine m) pairs; the offset moves the vertices of that mesh. @return A report with the manifest records (the mesh
    record has the key `key`)."""
    w = gltf.Writer()
    by_key: dict[str, int] = {}
    records: dict[str, dict] = {}
    textures: dict[str, dict] = {}
    prims = []
    report: dict = {"mesh": key, "triangles": 0, "vertices": 0, "parts": 0}
    for path, offset in parts:
        try:
            f, meshes = load(path)
        except Exception as error:
            report.setdefault("part errors", []).append(f"{path}: {str(error)[:80]}")
            continue
        reference = (cgf.material_guid(f) or "").lstrip("/")
        sub_materials: list = []
        if reference:
            try:
                sub_materials = materials.load_mtl(reference, path)[1]
            except Exception:
                sub_materials = []
        report["parts"] += 1
        for m in meshes:
            pos = gltf.z_up_to_y_up(m.positions + np.asarray(offset, dtype=np.float32))
            nrm = gltf.z_up_to_y_up(m.normals) if m.normals is not None else None
            tan = None
            if m.tangents is not None:
                tan = m.tangents.copy()
                tan[:, :3] = gltf.z_up_to_y_up(m.tangents[:, :3])
            for s in m.subsets:
                sub = sub_materials[s.material] if 0 <= s.material < len(sub_materials) else None
                if sub is not None and materials.not_drawn(sub):
                    continue
                record, used = materials.shared_material(sub)
                records[record["name"]] = record
                textures.update({t["asset"]: t for t in used})
                if record["name"] not in by_key:
                    by_key[record["name"]] = w.material(record["name"])
                idx, (p, n, uv, t) = compact(m.indices[s.first_index:s.first_index + s.num_indices], pos, nrm, m.uvs, tan)
                prims.append(w.primitive(p, idx, n, uv, t, material=by_key[record["name"]]))
                report["triangles"] += len(idx) // 3
                report["vertices"] += len(p)
    if not prims:
        raise ValueError(f"{key}: no drawn triangles")
    out.parent.mkdir(parents=True, exist_ok=True)
    w.doc["scenes"][0]["nodes"] = [w.node(name=out.stem, mesh=w.mesh(out.stem, prims))]
    w.save(out)
    translucent = any(r["blend"] == "translucent" for r in records.values())
    entry = {"kind": "static", "file": str(out.relative_to(env.content_dir())), "version": VERSION,
             "hash": file_hash(out), "materials": sorted(records), "nanite": not translucent}
    report.update({"entry": entry, "materials": list(records.values()), "textures": list(textures.values())})
    return report
