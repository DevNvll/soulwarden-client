#!/usr/bin/env python3
"""Scratch: a small software renderer (numba) for the proof pictures. Z is up (engine space)."""
from __future__ import annotations

import math

import numba
import numpy as np
from PIL import Image


def look_at(eye, target, up=(0, 0, 1)):
    eye, target, up = (np.asarray(x, dtype=np.float64) for x in (eye, target, up))
    f = target - eye; f /= np.linalg.norm(f)
    r = np.cross(f, up); r /= np.linalg.norm(r)
    u = np.cross(r, f)
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3] = r, u, -f
    m[:3, 3] = -m[:3, :3] @ eye
    return m


def perspective(fov_deg, aspect, near, far):
    f = 1 / math.tan(math.radians(fov_deg) / 2)
    m = np.zeros((4, 4))
    m[0, 0], m[1, 1] = f / aspect, f
    m[2, 2], m[2, 3] = (far + near) / (near - far), 2 * far * near / (near - far)
    m[3, 2] = -1
    return m


@numba.njit(cache=False)
def _raster(sx, sy, sz, nrm, uv, col, tri, tex_id, textures, tex_off, tex_w, tex_h, light, out, zbuf, ambient):
    H, W = zbuf.shape
    for t in range(tri.shape[0]):
        a, b, c = tri[t, 0], tri[t, 1], tri[t, 2]
        if sz[a] <= -1 or sz[b] <= -1 or sz[c] <= -1:
            continue
        x0, y0, x1, y1, x2, y2 = sx[a], sy[a], sx[b], sy[b], sx[c], sy[c]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-12:
            continue
        minx = max(int(math.floor(min(x0, min(x1, x2)))), 0)
        maxx = min(int(math.ceil(max(x0, max(x1, x2)))), W - 1)
        miny = max(int(math.floor(min(y0, min(y1, y2)))), 0)
        maxy = min(int(math.ceil(max(y0, max(y1, y2)))), H - 1)
        if minx > maxx or miny > maxy:
            continue
        k = tex_id[t]
        for py in range(miny, maxy + 1):
            for px in range(minx, maxx + 1):
                fx, fy = px + 0.5, py + 0.5
                w0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) / area
                w1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) / area
                w2 = 1.0 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = w0 * sz[a] + w1 * sz[b] + w2 * sz[c]
                if z >= zbuf[py, px]:
                    continue
                nx = w0 * nrm[a, 0] + w1 * nrm[b, 0] + w2 * nrm[c, 0]
                ny = w0 * nrm[a, 1] + w1 * nrm[b, 1] + w2 * nrm[c, 1]
                nz = w0 * nrm[a, 2] + w1 * nrm[b, 2] + w2 * nrm[c, 2]
                ln = math.sqrt(nx * nx + ny * ny + nz * nz) + 1e-9
                d = abs((nx * light[0] + ny * light[1] + nz * light[2]) / ln)
                r = col[a, 0] * w0 + col[b, 0] * w1 + col[c, 0] * w2
                g = col[a, 1] * w0 + col[b, 1] * w1 + col[c, 1] * w2
                bl = col[a, 2] * w0 + col[b, 2] * w1 + col[c, 2] * w2
                if k >= 0:
                    u = w0 * uv[a, 0] + w1 * uv[b, 0] + w2 * uv[c, 0]
                    v = w0 * uv[a, 1] + w1 * uv[b, 1] + w2 * uv[c, 1]
                    u = u - math.floor(u)
                    v = v - math.floor(v)
                    tx = min(int(u * tex_w[k]), tex_w[k] - 1)
                    ty = min(int(v * tex_h[k]), tex_h[k] - 1)
                    o = tex_off[k] + (ty * tex_w[k] + tx) * 4
                    if textures[o + 3] < 77:
                        continue
                    r *= textures[o] / 255.0
                    g *= textures[o + 1] / 255.0
                    bl *= textures[o + 2] / 255.0
                s = ambient + (1 - ambient) * d
                zbuf[py, px] = z
                out[py, px, 0] = r * s
                out[py, px, 1] = g * s
                out[py, px, 2] = bl * s


def render(positions, normals, uvs, colors, triangles, tri_tex, textures, eye, target, size=(1024, 768), fov=40,
           light=(0.4, -0.5, 0.75), background=(0.80, 0.85, 0.90), ambient=0.35, far=5000.0):
    W, H = size
    view = look_at(eye, target)
    proj = perspective(fov, W / H, 0.1, far)
    p = np.concatenate([positions.astype(np.float64), np.ones((len(positions), 1))], 1)
    clip = (proj @ view @ p.T).T
    wv = clip[:, 3]
    behind = wv <= 1e-6
    wv = np.where(behind, 1e-6, wv)
    ndc = clip[:, :3] / wv[:, None]
    sx = (ndc[:, 0] + 1) * 0.5 * W
    sy = (1 - ndc[:, 1]) * 0.5 * H
    sz = np.where(behind, -2.0, ndc[:, 2])
    out = np.zeros((H, W, 3)); out[:] = background
    zbuf = np.full((H, W), 1e9)
    if textures:
        parts = [np.ascontiguousarray(t, dtype=np.uint8).reshape(-1) for t in textures]
        tex_off = np.cumsum([0] + [len(x) for x in parts[:-1]]).astype(np.int64)
        stack = np.concatenate(parts)
        tex_w = np.array([t.shape[1] for t in textures], dtype=np.int64)
        tex_h = np.array([t.shape[0] for t in textures], dtype=np.int64)
    else:
        stack = np.full(4, 255, np.uint8); tex_off = np.zeros(1, np.int64)
        tex_w = np.ones(1, np.int64); tex_h = np.ones(1, np.int64)
    if colors is None:
        colors = np.full((len(positions), 3), 0.8)
    if uvs is None:
        uvs = np.zeros((len(positions), 2))
    light = np.asarray(light, dtype=np.float64); light /= np.linalg.norm(light)
    _raster(sx, sy, sz, normals.astype(np.float64), uvs.astype(np.float64), colors.astype(np.float64),
            triangles.astype(np.int64), tri_tex.astype(np.int64), stack, tex_off, tex_w, tex_h, light, out, zbuf, ambient)
    return Image.fromarray((np.clip(out, 0, 1) ** (1 / 1.0) * 255).astype(np.uint8))


def texture_array(img: Image.Image, srgb_to_linear: bool = False) -> np.ndarray:
    """@return The texture as uint8 RGBA (h, w, 4)."""
    return np.asarray(img.convert("RGBA"), dtype=np.uint8)
