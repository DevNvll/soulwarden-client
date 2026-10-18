"""Characters (.cdf -> .chr skeleton + .skin attachments + .mtl) and clips (.caf) -> one glTF with a skin and
animations."""
from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

from . import anim, cgf, env, gltf, manifest, materials, source

C = np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]], dtype=np.float64)
CI = np.linalg.inv(C)


def to_y_up(m: np.ndarray) -> np.ndarray:
    return C @ m @ CI


def mat_to_quat(m: np.ndarray) -> list[float]:
    r = m[:3, :3]
    t = np.trace(r)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w, x, y, z = 0.25 * s, (r[2, 1] - r[1, 2]) / s, (r[0, 2] - r[2, 0]) / s, (r[1, 0] - r[0, 1]) / s
    elif r[0, 0] > r[1, 1] and r[0, 0] > r[2, 2]:
        s = np.sqrt(1.0 + r[0, 0] - r[1, 1] - r[2, 2]) * 2
        w, x, y, z = (r[2, 1] - r[1, 2]) / s, 0.25 * s, (r[0, 1] + r[1, 0]) / s, (r[0, 2] + r[2, 0]) / s
    elif r[1, 1] > r[2, 2]:
        s = np.sqrt(1.0 + r[1, 1] - r[0, 0] - r[2, 2]) * 2
        w, x, y, z = (r[0, 2] - r[2, 0]) / s, (r[0, 1] + r[1, 0]) / s, 0.25 * s, (r[1, 2] + r[2, 1]) / s
    else:
        s = np.sqrt(1.0 + r[2, 2] - r[0, 0] - r[1, 1]) * 2
        w, x, y, z = (r[1, 0] - r[0, 1]) / s, (r[0, 2] + r[2, 0]) / s, (r[1, 2] + r[2, 1]) / s, 0.25 * s
    q = np.array([x, y, z, w])
    q /= np.linalg.norm(q)
    return [float(v) for v in q]


def norm(path: str) -> str:
    return path.replace("\\", "/").lower()


class Character:
    """The parts of one character definition, in engine space."""

    def __init__(self, cdf_path: str) -> None:
        self.cdf_path = norm(cdf_path)
        cdf = ET.fromstring(source.read(self.cdf_path).decode("utf-8", "replace"))
        model = cdf.find("Model")
        self.skeleton_path = norm(model.get("File"))
        self.bones = cgf.read_bones(cgf.read_chunks(source.read(self.skeleton_path)))
        self.index_of = {b.name: b.index for b in self.bones}
        self.default_material = norm(model.get("Material") or "")
        self.skins = []   # (path, mesh, joints into the skeleton, material path)
        for att in cdf.iter("Attachment"):
            if att.get("Type") != "CA_SKIN" or not att.get("Binding"):
                continue
            path = norm(att.get("Binding"))
            f = cgf.read_chunks(source.read(path))
            mesh = cgf.read_mesh(f, f.of_type(cgf.T_MESH)[0])
            remap = np.array([self.index_of.get(b.name, 0) for b in cgf.read_bones(f)], dtype=np.uint16)
            self.skins.append((path, mesh, remap[mesh.joints], norm(att.get("Material") or self.default_material)))

    def pose(self, clip: anim.Clip | None, frame: float) -> list[np.ndarray]:
        if clip is None:
            return [b.b2w for b in self.bones]
        return anim.pose(clip, self.bones, frame)


def convert(cdf_path: str, clip_paths: list[str], max_texture: int = 1024, force: bool = False) -> dict:
    out = env.content_dir() / (norm(cdf_path).rsplit(".", 1)[0] + ".gltf")
    if out.is_file() and not force:
        return {"cdf": cdf_path, "gltf": str(out), "skipped": "exists"}
    ch = Character(cdf_path)
    w = gltf.Writer()
    report = {"cdf": ch.cdf_path, "skeleton": ch.skeleton_path, "bones": len(ch.bones), "skins": [], "clips": [],
              "gltf": str(out)}
    first = len(w.doc["nodes"])
    for b in ch.bones:
        local = (np.linalg.inv(ch.bones[b.parent].b2w) @ b.b2w) if b.parent >= 0 else b.b2w
        m = to_y_up(local)
        w.node(name=b.name, translation=[float(v) for v in m[:3, 3]], rotation=mat_to_quat(m))
    for b in ch.bones:
        kids = [first + c.index for c in ch.bones if c.parent == b.index]
        if kids:
            w.doc["nodes"][first + b.index]["children"] = kids
    roots = [first + b.index for b in ch.bones if b.parent < 0]
    ibm = np.stack([to_y_up(b.w2b).T for b in ch.bones]).astype(np.float32)
    w.doc["skins"] = [{"joints": [first + b.index for b in ch.bones], "skeleton": roots[0],
                       "inverseBindMatrices": w.accessor(ibm.reshape(len(ch.bones), 16), "MAT4", target=None),
                       "name": Path(ch.skeleton_path).stem}]
    out.parent.mkdir(parents=True, exist_ok=True)
    mtl_cache = {}
    for path, mesh, joints, mtl in ch.skins:
        if mtl not in mtl_cache:
            try:
                mtl_cache[mtl] = materials.load_mtl(mtl)[1]
            except Exception:
                mtl_cache[mtl] = []
        subs = mtl_cache[mtl]
        prims = []
        pos = gltf.z_up_to_y_up(mesh.positions)
        nrm = gltf.z_up_to_y_up(mesh.normals) if mesh.normals is not None else None
        for s in mesh.subsets:
            mi = None
            if s.material < len(subs):
                if materials.not_drawn(subs[s.material]):
                    continue
                info = materials.describe(subs[s.material], max_texture)
                files = info["files"]
                mi = w.material(info["name"], materials.relative(files["base"], out) if "base" in files else None,
                                materials.relative(files["normal"], out) if "normal" in files else None)
            idx = mesh.indices[s.first_index:s.first_index + s.num_indices]
            prims.append(w.primitive(pos, idx, nrm, mesh.uvs, None, joints, mesh.weights, mi))
        if prims:
            node = w.node(name=Path(path).stem, mesh=w.mesh(Path(path).stem, prims), skin=0)
            w.doc["scenes"][0]["nodes"].append(node)
        report["skins"].append({"skin": path, "vertices": len(mesh.positions), "triangles": len(mesh.indices) // 3})
    w.doc["scenes"][0]["nodes"] += roots
    w.doc["animations"] = []
    by_id = {b.controller_id: b for b in ch.bones}
    for clip_path in clip_paths:
        clip = anim.read_clip(cgf.read_chunks(source.read(norm(clip_path))))
        samplers, channels = [], []
        for cid, c in clip.controllers.items():
            b = by_id.get(cid)
            if b is None:
                continue
            node = first + b.index
            if len(c.rotations):
                t = ((c.rot_times - clip.start) * clip.secs_per_frame).astype(np.float32)
                q = np.array([mat_to_quat(to_y_up(np.block([[anim.quat_to_matrix(r), np.zeros((3, 1))],
                                                              [np.zeros((1, 3)), np.ones((1, 1))]])))
                              for r in c.rotations], dtype=np.float32)
                samplers.append({"input": w.accessor(t, "SCALAR", None, minmax=True),
                                 "output": w.accessor(q, "VEC4", None), "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": "rotation"}})
            if len(c.positions):
                t = ((c.pos_times - clip.start) * clip.secs_per_frame).astype(np.float32)
                p = gltf.z_up_to_y_up(c.positions).astype(np.float32)
                samplers.append({"input": w.accessor(t, "SCALAR", None, minmax=True),
                                 "output": w.accessor(p, "VEC3", None), "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": "translation"}})
        w.doc["animations"].append({"name": Path(clip_path).stem, "samplers": samplers, "channels": channels})
        report["clips"].append({"clip": norm(clip_path), "frames": clip.end - clip.start,
                                "fps": round(1 / clip.secs_per_frame, 3), "controllers": len(clip.controllers),
                                "move speed m/s": round(clip.move_speed, 3)})
    if not w.doc["animations"]:
        del w.doc["animations"]
    w.save(out)
    manifest.add("character", ch.cdf_path, out)
    return report
