"""Static meshes (.cgf + .cgfheap) -> glTF 2.0 with PNG textures."""
from __future__ import annotations

import functools
import hashlib
import json
from pathlib import Path

import numpy as np

from . import cgf, env, gltf, manifest, materials, props, source


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
# Rule 7 (the materials track, stage M3; an option of convert() until the rollout): the vertex colors of the source
# (COLOR_0) and the slot of each sub-material named by materials.instance_key (one slot for each set of render rules:
# no collapsed slots, the n_ instances of A1).
RULE_COLORS = 7
# Rule 8 (AST-207): rule 7, and the GeometryBeam sub-materials are drawn (materials.is_beam: the fog sheets and cards, the
# light beams; their slots get the parent M_SwcBeam by their spec; no Nanite). The default rule of the meshes of
# BEAM_FOLDERS; a world mesh with a beam slot elsewhere keeps its rule until a conversion with rule 8.
RULE_BEAMS = 8
BEAM_FOLDERS = ("objects/volumetrics/", "objects/vfx/mesh/")


@functools.lru_cache(maxsize=1)
def rule7_meshes() -> frozenset:
    """@return The meshes of the M3 rebuild (SWC_CONTENT/materials/rule7-meshes.json, material_records.rebuild_set):
    convert() gives them rule 7 when the caller asks for the default rule, so that a later conversion keeps rule 7."""
    p = env.content_dir() / "materials" / "rule7-meshes.json"
    return frozenset(json.loads(p.read_text())["meshes"]) if p.is_file() else frozenset()


def is_mesh_decal(material) -> bool:
    """@return True for a decal sub-material (%DECAL) that rule 7 draws as a mesh decal: not NoDraw, not a shadow proxy,
    not a GeometryBeam (materials.not_drawn leaves all decals out of the main mesh)."""
    return (material is not None and "%DECAL" in material.get("StringGenMask", "").upper()
            and material.get("Shader", "").lower() not in materials.NOT_DRAWN_SHADERS
            and not materials.is_shadow_proxy(material))


def decal_key(mtl: str, index: int, material) -> str:
    """@return The key of the mesh decal instance of a sub-material: "dm_" and the 10 hex digits of its render key
    (material_records.render_rules). Its own prefix: a mesh decal reads the vertex alpha, a projected decal (d_) not."""
    from . import material_records
    rules = material_records.render_rules(material_records.source_record(mtl, index, material))
    return "dm_" + material_records.render_key(rules)[2:]


def companion_key(path: str) -> str:
    """@return The manifest key of the mesh decal companion of a mesh (rule 7)."""
    return "decals:" + path


def rule_for(path: str, rule: int = VERSION) -> int:
    """@return The rule of a conversion: rule 7 for a mesh of the M3 rebuild and rule 8 for a mesh of BEAM_FOLDERS when
    the caller asks for the default rule."""
    if rule == VERSION and path.lower().startswith(BEAM_FOLDERS):
        return RULE_BEAMS
    return RULE_COLORS if rule == VERSION and path.lower() in rule7_meshes() else rule


def vertex_colors(colors) -> np.ndarray | None:
    """@return COLOR_0 (n, 4) of the source colors (n, 4) uint8 B, G, R, A (the interleaved vertex of the chunk file):
    RGBA, the RGB as the linear value whose sRGB encoding is the source byte. Unreal stores a vertex color as sRGB bytes
    (StaticMeshBuilder: ToFColor(true)) and the material reads those bytes as they are: the material gets the source
    bytes / 255 (the wind and blend data of the source are not colors). The alpha is linear in both."""
    if colors is None:
        return None
    c = np.asarray(colors, dtype=np.float64)[:, [2, 1, 0, 3]] / 255.0
    rgb = c[:, :3]
    c[:, :3] = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    return c.astype(np.float32)


def compact(indices: np.ndarray, *arrays):
    """@return The indices of one primitive and the vertex arrays with only the vertices that it uses."""
    used, local = np.unique(indices, return_inverse=True)
    return local.astype(np.uint32), [a[used] if a is not None else None for a in arrays]


def file_hash(gltf_path: Path) -> str:
    digest = hashlib.sha1(gltf_path.read_bytes())
    digest.update(gltf_path.with_suffix(".bin").read_bytes())
    return digest.hexdigest()[:16]


def convert(path: str, force: bool = False, write_manifest: bool = True, rule: int = VERSION,
            out: Path | None = None) -> dict:
    """Converts one static mesh to glTF with shared materials: each glTF material is only the key of a material
    record (materials.shared_material); the textures are their own assets. All chunks are one glTF mesh with one node
    (Interchange makes one static mesh asset for each node). @return A report with the manifest records."""
    path = path.lower()
    rule = rule_for(path, rule)
    out = out or out_path(path)
    if not force and out.is_file():
        entry = manifest.load()["assets"].get(path, {})
        if entry.get("version") == rule:
            return {"mesh": path, "skipped": "up to date"}
    f, meshes = load(path)
    reference = props.own_reference(f, path)
    sub_materials: list = []
    report: dict = {"mesh": path, "triangles": 0, "vertices": 0}
    if reference:
        try:
            report["mtl"], sub_materials = materials.load_mtl(reference, path)
        except Exception as error:
            report["material error"] = str(error)[:120]
    w = gltf.Writer()
    dw = gltf.Writer()                  # rule 7: the mesh decal companion (the %DECAL subsets)
    decal_slots: dict[int, int] = {}    # sub-material index -> glTF material index of the companion
    decal_prims = []
    decals: dict[str, dict] = {}        # dm_ key -> the source sub-material
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
        col = vertex_colors(mesh.colors) if rule >= RULE_COLORS else None
        for s in mesh.subsets:
            if s.material not in slots:
                m = materials.sub_material(sub_materials, s.material)
                if m is not None and materials.not_drawn(m, beams=rule >= RULE_BEAMS):
                    slots[s.material] = -1
                else:
                    record, used = materials.shared_material(m)
                    records[record["name"]] = record
                    textures.update({t["asset"]: t for t in used})
                    name = materials.instance_key(m) if rule >= RULE_COLORS and m is not None else record["name"]
                    if name not in by_key:
                        by_key[name] = w.material(name)
                    slots[s.material] = by_key[name]
            if slots[s.material] == -1:
                m = materials.sub_material(sub_materials, s.material)
                if rule >= RULE_COLORS and is_mesh_decal(m):
                    if s.material not in decal_slots:
                        key = decal_key(report["mtl"], s.material, m)
                        decals.setdefault(key, {"mtl": report["mtl"], "index": s.material})
                        decal_slots[s.material] = dw.material(key)
                    idx, (p, n, uv, t, c) = compact(mesh.indices[s.first_index:s.first_index + s.num_indices], pos,
                                                    nrm, mesh.uvs, tan, col)
                    decal_prims.append(dw.primitive(p, idx, n, uv, t, material=decal_slots[s.material], colors=c))
                continue   # collision (Nodraw) and shadow proxies are not drawn; decals: the companion
            idx, (p, n, uv, t, c) = compact(mesh.indices[s.first_index:s.first_index + s.num_indices], pos, nrm,
                                            mesh.uvs, tan, col)
            prims.append(w.primitive(p, idx, n, uv, t, material=slots[s.material], colors=c))
            report["triangles"] += len(idx) // 3
            report["vertices"] += len(p)
    if not prims and not decal_prims:
        raise ValueError(f"{path}: no drawn triangles")
    if prims:
        out.parent.mkdir(parents=True, exist_ok=True)
        w.doc["scenes"][0]["nodes"] = [w.node(name=Path(path).stem, mesh=w.mesh(Path(path).stem, prims))]
        w.save(out)
        translucent = any(r["blend"] == "translucent" for r in records.values()) or any(
            materials.is_blended_illum(m) for i, m in enumerate(sub_materials) if slots.get(i, -1) != -1)
        # "materials": the keys of the material records (the import pipeline makes and sets their instances); rule 7 also
        # gives "slots": the slot names (the instance keys; an n_ instance has no record: swc_materials makes it).
        # A mesh of rule 8 with no material of its own (AST-275: the energy planes of the Shattered Obelisk) takes its look from the
        # override of each placement, a GeometryBeam material (M_SwcBeam, additive): Nanite draws only opaque and masked materials.
        effect = rule >= RULE_BEAMS and not sub_materials
        entry = {"kind": "static", "file": str(out.relative_to(env.content_dir())) if out.is_relative_to(env.content_dir())
                 else str(out), "version": rule, "hash": file_hash(out), "materials": sorted(records),
                 "nanite": not translucent and not effect}
        if rule >= RULE_COLORS:
            entry["slots"] = sorted(m["name"] for m in w.doc["materials"])
    else:
        # Only decal subsets (AST-275, class C4: the moss layers of the crater cliffs, cliffs_crater/*_moss.cgf): no main asset;
        # the record "empty" names its decal companion, and the cells place the companion alone (cells.write).
        entry = {"kind": "empty", "version": rule}
    if decal_prims:
        # The mesh decal companion: its own glTF and asset (no Nanite: Nanite draws only surface materials); the
        # cells place it at the transforms of the mesh. Its slots are dm_ instances (no material record).
        out_decals = out.with_name(out.stem + "_decals.gltf")
        stem = Path(path).stem + "_decals"
        dw.doc["scenes"][0]["nodes"] = [dw.node(name=stem, mesh=dw.mesh(stem, decal_prims))]
        dw.save(out_decals)
        entry["decals"] = companion_key(path)
        report["companion"] = {
            "key": companion_key(path), "decals": decals,
            "triangles": sum(dw.doc["accessors"][p["indices"]]["count"] for p in decal_prims) // 3,
            "entry": {"kind": "static", "file": str(out_decals.relative_to(env.content_dir()))
                      if out_decals.is_relative_to(env.content_dir()) else str(out_decals),
                      "version": rule, "hash": file_hash(out_decals), "materials": [],
                      "slots": sorted(m["name"] for m in dw.doc["materials"]), "nanite": False, "decals of": path}}
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
        if "companion" in r:
            out[r["companion"]["key"]] = r["companion"]["entry"]
        for record in r["materials"] + r["textures"]:
            entry = {k: v for k, v in record.items() if k != "asset"}
            out[record["asset"]] = entry
    return out


def drawn_positions(path: str) -> np.ndarray:
    """@return The engine positions of the vertices of the drawn triangles of a mesh (the glTF has no others)."""
    f, meshes = load(path.lower())
    reference = props.own_reference(f, path.lower())
    sub_materials = materials.load_mtl(reference, path.lower())[1] if reference else []
    out = []
    for m in meshes:
        for s in m.subsets:
            sub = materials.sub_material(sub_materials, s.material)
            if sub is not None and materials.not_drawn(sub):
                continue
            out.append(m.positions[np.unique(m.indices[s.first_index:s.first_index + s.num_indices])])
    return np.concatenate(out) if out else np.zeros((0, 3), np.float32)


def convert_group(key: str, out: Path, parts: list[tuple[str, np.ndarray]], normal_maps: bool = True) -> dict:
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
        reference = props.own_reference(f, path).lstrip("/")
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
                sub = materials.sub_material(sub_materials, s.material)
                if sub is not None and materials.not_drawn(sub):
                    continue
                record, used = materials.shared_material(sub, normal_maps)
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
