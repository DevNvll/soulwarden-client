"""Static meshes (.cgf + .cgfheap) -> glTF 2.0 with PNG textures."""
from __future__ import annotations

from pathlib import Path

from . import cgf, env, gltf, manifest, materials, source


def alpha_cutoff(value: str) -> float | None:
    """@return The glTF alpha cutoff of a CryEngine AlphaTest value, or no value for an opaque material."""
    try:
        cutoff = float(value)
    except ValueError:
        return None
    return cutoff if cutoff > 0.0 else None


def out_path(asset: str) -> Path:
    return env.content_dir() / (asset.rsplit(".", 1)[0] + ".gltf")


def load(path: str) -> tuple[cgf.ChunkFile, list[cgf.Mesh]]:
    heap = source.read(path + "heap") if source.has(path + "heap") else None
    f = cgf.read_chunks(source.read(path), heap)
    meshes = []
    for chunk in f.of_type(cgf.T_MESH):
        try:
            meshes.append(cgf.read_mesh(f, chunk))
        except ValueError as error:
            if "no geometry" not in str(error):
                raise
    if not meshes:
        raise ValueError(f"{path}: no mesh chunk with geometry")
    return f, meshes


def convert(path: str, max_texture: int = 1024, force: bool = False) -> dict:
    """Converts one static mesh. @return A report."""
    path = path.lower()
    out = out_path(path)
    if out.is_file() and not force:
        return {"mesh": path, "gltf": str(out), "skipped": "exists"}
    f, meshes = load(path)
    reference = cgf.material_guid(f) or ""
    w = gltf.Writer()
    report = {"mesh": path, "gltf": str(out), "vertices": 0, "triangles": 0, "materials": []}
    mats: dict[int, int | None] = {}
    sub_materials = []
    if reference:
        try:
            mtl_path, sub_materials = materials.load_mtl(reference, path)
            report["mtl"] = mtl_path
        except Exception as error:
            report["material error"] = str(error)
    out.parent.mkdir(parents=True, exist_ok=True)
    nodes = []
    for number, mesh in enumerate(meshes):
        prims = []
        pos = gltf.z_up_to_y_up(mesh.positions)
        nrm = gltf.z_up_to_y_up(mesh.normals) if mesh.normals is not None else None
        tan = None
        if mesh.tangents is not None:
            tan = mesh.tangents.copy()
            tan[:, :3] = gltf.z_up_to_y_up(mesh.tangents[:, :3])
        for s in mesh.subsets:
            if s.material not in mats:
                mats[s.material] = None
                if s.material < len(sub_materials):
                    m = sub_materials[s.material]
                    if materials.not_drawn(m):
                        mats[s.material] = -1
                    else:
                        info = materials.describe(m, max_texture)
                        report["materials"].append(info)
                        files = info["files"]
                        glass = materials.is_glass(m)
                        mats[s.material] = w.material(
                            f"m{s.material}_{info['name']}"[:48],   # unique in the file: Interchange shortens long names
                            materials.relative(files["base"], out) if "base" in files else None,
                            materials.relative(files["normal"], out) if "normal" in files else None,
                            None, factor=(0.8, 0.85, 0.9, 0.25) if glass else (1, 1, 1, 1),
                            alpha_mask=None if glass else alpha_cutoff(info.get("alphaTest", "")), blend=glass)
            if mats[s.material] == -1:
                continue   # collision (Nodraw) and shadow proxies are not drawn
            idx = mesh.indices[s.first_index:s.first_index + s.num_indices]
            prims.append(w.primitive(pos, idx, nrm, mesh.uvs, tan, material=mats[s.material]))
            report["triangles"] += len(idx) // 3
        report["vertices"] += len(mesh.positions)
        if prims:
            mi = w.mesh(f"{Path(path).stem}_{number}", prims)
            nodes.append(w.node(name=f"{Path(path).stem}_{number}", mesh=mi))
    w.doc["scenes"][0]["nodes"] = nodes
    w.save(out)
    manifest.add("static", path, out)
    return report
