"""Software previews (no Unreal): a static mesh, a character pose, or a region area, as PNG.

They prove geometry, UVs, skinning, and placement. They do not show the look of the game.
"""
from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

from . import anim, cgf, character, dds, env, materials, mesh, region, render, source


def out_dir() -> Path:
    p = env.work_dir() / "previews"
    p.mkdir(parents=True, exist_ok=True)
    return p


class TextureSet:
    def __init__(self) -> None:
        self.textures: list[np.ndarray] = []
        self.index: dict[str, int] = {}

    def of_material(self, m: ET.Element, size: int) -> int:
        for t in m.iter("Texture"):
            if t.get("Map") == "Diffuse" and t.get("AssetId"):
                product = source.product_of_text(t.get("AssetId"), ".dds")
                if product is None:
                    return -1
                if product not in self.index:
                    try:
                        data, w, h, fmt = dds.join(source.read, product, size)
                        self.textures.append(render.texture_array(dds.to_image(data, w, h, fmt)))
                        self.index[product] = len(self.textures) - 1
                    except Exception:
                        self.index[product] = -1
                return self.index[product]
        return -1


def mesh_preview(path: str) -> Path:
    f, meshes = mesh.load(path.lower())
    ref = cgf.material_guid(f) or ""
    subs = materials.load_mtl(ref, path.lower())[1] if ref else []
    ts = TextureSet()
    P, N, U, T, TT, off = [], [], [], [], [], 0
    for m in meshes:
        P.append(m.positions); N.append(m.normals if m.normals is not None else np.tile([0, 0, 1.0], (len(m.positions), 1)))
        U.append(m.uvs if m.uvs is not None else np.zeros((len(m.positions), 2)))
        for s in m.subsets:
            if s.material < len(subs) and (materials.not_drawn(subs[s.material]) or materials.is_glass(subs[s.material])):
                continue
            k = ts.of_material(subs[s.material], 1024) if s.material < len(subs) else -1
            tri = m.indices[s.first_index:s.first_index + s.num_indices].reshape(-1, 3) + off
            T.append(tri); TT += [k] * len(tri)
        off += len(m.positions)
    P = np.concatenate(P)
    lo, hi = P.min(0), P.max(0)
    c = (lo + hi) / 2
    r = float(np.linalg.norm(hi - lo)) / 2
    img = render.render(P, np.concatenate(N), np.concatenate(U), None, np.concatenate(T), np.array(TT), ts.textures,
                        c + np.array([1.6, -2.2, 0.9]) * r, c, size=(1024, 768), fov=35)
    out = out_dir() / (Path(path).stem + ".png")
    img.save(out)
    return out


def character_preview(cdf_path: str, clip_path: str | None, frames: list[float]) -> Path:
    ch = character.Character(cdf_path)
    clip = anim.read_clip(cgf.read_chunks(source.read(clip_path.lower()))) if clip_path else None
    ts = TextureSet()
    parts = []
    for path, m, joints, mtl in ch.skins:
        try:
            subs = materials.load_mtl(mtl)[1]
        except Exception:
            subs = []
        tex = [ts.of_material(subs[s.material], 1024) if s.material < len(subs) else -1 for s in m.subsets]
        parts.append((m, joints, tex))
    images = []
    for frame in (frames if clip else [0.0]):
        mats = ch.pose(clip, frame)
        P, N, U, T, TT, off = [], [], [], [], [], 0
        for m, joints, tex in parts:
            skin = np.stack([mats[j] @ ch.bones[j].w2b for j in range(len(ch.bones))])
            M = np.einsum("vk,vkij->vij", m.weights, skin[joints])
            P.append(np.einsum("vij,vj->vi", M[:, :3, :3], m.positions) + M[:, :3, 3])
            N.append(np.einsum("vij,vj->vi", M[:, :3, :3], m.normals))
            U.append(m.uvs)
            for s, k in zip(m.subsets, tex):
                tri = m.indices[s.first_index:s.first_index + s.num_indices].reshape(-1, 3) + off
                T.append(tri); TT += [k] * len(tri)
            off += len(m.positions)
        P = np.concatenate(P)
        lo, hi = P.min(0), P.max(0)
        c = (lo + hi) / 2
        r = float(np.linalg.norm(hi - lo)) / 2
        images.append(render.render(P, np.concatenate(N), np.concatenate(U), None, np.concatenate(T), np.array(TT),
                                    ts.textures, c + np.array([2.2, -1.0, 0.6]) * r, c, size=(640, 480), fov=40))
    sheet = Image.new("RGB", (640 * min(3, len(images)), 480 * ((len(images) + 2) // 3)), (255, 255, 255))
    for i, im in enumerate(images):
        sheet.paste(im, (640 * (i % 3), 480 * (i // 3)))
    out = out_dir() / (Path(cdf_path).stem + (("_" + Path(clip_path).stem) if clip_path else "") + ".png")
    sheet.save(out)
    return out


def region_preview(name: str, area: tuple[float, float, float]) -> Path:
    reg = region.Region(name)
    pfile = region.placement_file(name, area)
    if not pfile.is_file():
        reg.write_placements(area)
    placements = json.loads(pfile.read_text())["placements"]
    ox, oy = reg.origin
    x0, y0, size = area
    heights = reg.heights().astype(np.float64) * region.HEIGHT_SCALE
    rx, ry = reg.index
    macro = (f"materials/terrain/newworld_vitaeeterna/jav_newworld_vitaeeterna_{rx}_{ry}/"
             f"jav_newworld_vitaeeterna_{rx}.{ry}.macro_diff.dds")
    data, w, h, fmt = dds.join(source.read, macro, 2048)
    ts = TextureSet()
    ts.textures.append(render.texture_array(dds.to_image(data, w, h, fmt)))
    step = 2
    xs = np.arange(int(x0 - ox), int(x0 - ox + size) + 1, step)
    ys = np.arange(int(y0 - oy), int(y0 - oy + size) + 1, step)
    gx, gy = np.meshgrid(xs, ys)
    gz = heights[np.clip(gy, 0, 2047), np.clip(gx, 0, 2047)]
    P = [np.stack([gx + ox, gy + oy, gz], -1).reshape(-1, 3)]
    dzdx, dzdy = np.gradient(gz, step, axis=1), np.gradient(gz, step, axis=0)
    n = np.stack([-dzdx, -dzdy, np.ones_like(gz)], -1).reshape(-1, 3)
    N = [n / np.linalg.norm(n, axis=1, keepdims=True)]
    U = [np.stack([gx / 2048.0, 1 - gy / 2048.0], -1).reshape(-1, 2)]
    nx, ny = len(xs), len(ys)
    a = (np.arange(ny - 1)[:, None] * nx + np.arange(nx - 1)[None, :]).reshape(-1)
    T = [np.concatenate([np.stack([a, a + 1, a + nx + 1], 1), np.stack([a, a + nx + 1, a + nx], 1)])]
    TT = [np.zeros(len(T[0]), np.int64)]
    off = len(P[0])
    cache = {}
    for pl in placements:
        mp = pl["mesh"]
        if mp.rsplit("/", 1)[-1].startswith("fx_"):
            continue   # effect cards
        if mp not in cache:
            cache[mp] = None
            for cand in (mp.replace(".cgf", "_lod2.cgf"), mp.replace(".cgf", "_lod1.cgf"), mp):
                if not source.has(cand):
                    continue
                try:
                    f, meshes = mesh.load(cand)
                    ref = cgf.material_guid(f) or ""
                    subs = materials.load_mtl(ref, cand)[1] if ref else []
                    tex = []
                    for m in meshes:
                        tex.append([(-2 if s.material < len(subs) and (materials.not_drawn(subs[s.material]) or materials.is_glass(subs[s.material]))
                                     else (ts.of_material(subs[s.material], 128) if s.material < len(subs) else -1))
                                    for s in m.subsets])
                    cache[mp] = (meshes, tex)
                except Exception:
                    cache[mp] = None
                break
        item = cache[mp]
        if item is None:
            continue
        m4 = np.array(pl["matrix"])
        for m, tex in zip(*item):
            p = (m4[:3, :3] @ m.positions.T).T + m4[:3, 3]
            nn = m.normals if m.normals is not None else np.tile([0, 0, 1.0], (len(p), 1))
            P.append(p); N.append((np.linalg.inv(m4[:3, :3]).T @ nn.T).T)
            U.append(m.uvs if m.uvs is not None else np.zeros((len(p), 2)))
            for s, k in zip(m.subsets, tex):
                if k == -2:
                    continue
                tri = m.indices[s.first_index:s.first_index + s.num_indices].reshape(-1, 3) + off
                T.append(tri); TT.append(np.full(len(tri), k, np.int64))
            off += len(p)
    cx, cy = x0 + size / 2, y0 + size / 2
    cz = reg.height_at(cx - ox, cy - oy)
    img = render.render(np.concatenate(P), np.concatenate(N), np.concatenate(U), None, np.concatenate(T),
                        np.concatenate(TT), ts.textures, (cx - size * 0.55, cy - size * 0.75, cz + size * 0.42),
                        (cx, cy + size * 0.05, cz), size=(1280, 800), fov=50, light=(0.3, -0.4, 0.85),
                        background=(0.62, 0.74, 0.88), ambient=0.4)
    out = out_dir() / ("%s_%d_%d_%d.png" % (name, *[int(v) for v in area]))
    img.save(out)
    return out
