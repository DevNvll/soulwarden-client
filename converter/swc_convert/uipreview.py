"""A software picture of a draw list of the UI runtime (tests/ui: the offline run writes draw.json).

Usage: python3 -B -m swc_convert.uipreview DRAW.json OUT.png [--background PNG]

The draw list has triangle meshes (texture, vertex positions, UVs, colors; normal or additive blend), text lines (font
file, size, a 2D transform), and clip quads. This picture is a check of the runtime with no engine: the client draws
the same list with Slate. The text uses the original fonts with Pillow; a rotated text is drawn with no rotation.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

_textures: dict[str, np.ndarray | None] = {}
_fonts: dict[tuple[str, int], ImageFont.FreeTypeFont] = {}


def texture(path: str) -> np.ndarray | None:
    if path not in _textures:
        try:
            _textures[path] = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32) / 255.0
        except OSError:
            _textures[path] = None
    return _textures[path]


def font(path: str, size: float) -> ImageFont.FreeTypeFont:
    key = (path, max(4, int(round(size))))
    if key not in _fonts:
        try:
            _fonts[key] = ImageFont.truetype(path, key[1])
        except OSError:
            _fonts[key] = ImageFont.load_default()
    return _fonts[key]


def unpack_color(rgba: int) -> np.ndarray:
    return np.array([(rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255], dtype=np.float32) / 255.0


class Canvas:
    def __init__(self, width: int, height: int, background: Image.Image | None):
        self.w, self.h = width, height
        if background is not None:
            self.rgb = np.asarray(background.convert("RGB").resize((width, height)), dtype=np.float32) / 255.0
        else:
            self.rgb = np.full((height, width, 3), 0.18, dtype=np.float32)
        self.clips: list[np.ndarray] = []

    def clip_mask(self) -> np.ndarray | None:
        return self.clips[-1] if self.clips else None

    def push_clip(self, quad: list[float]):
        mask_image = Image.new("L", (self.w, self.h), 0)
        ImageDraw.Draw(mask_image).polygon([(quad[i], quad[i + 1]) for i in range(0, 8, 2)], fill=255)
        mask = np.asarray(mask_image, dtype=np.float32) / 255.0
        if self.clips:
            mask = np.minimum(mask, self.clips[-1])
        self.clips.append(mask)

    def pop_clip(self):
        if self.clips:
            self.clips.pop()

    def triangle(self, p, uv, col, tex, additive: bool):
        xs = [p[0][0], p[1][0], p[2][0]]
        ys = [p[0][1], p[1][1], p[2][1]]
        x0, x1 = max(int(np.floor(min(xs))), 0), min(int(np.ceil(max(xs))), self.w)
        y0, y1 = max(int(np.floor(min(ys))), 0), min(int(np.ceil(max(ys))), self.h)
        if x1 <= x0 or y1 <= y0:
            return
        gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        (ax, ay), (bx, by), (cx, cy) = p
        det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(det) < 1e-9:
            return
        w0 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / det
        w1 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / det
        w2 = 1 - w0 - w1
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any():
            return
        color = w0[..., None] * col[0] + w1[..., None] * col[1] + w2[..., None] * col[2]
        if tex is not None:
            u = w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0]
            v = w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1]
            th, tw = tex.shape[:2]
            tx = np.clip((u * tw).astype(np.int32), 0, tw - 1)
            ty = np.clip((v * th).astype(np.int32), 0, th - 1)
            color = color * tex[ty, tx]
        alpha = color[..., 3] * inside
        clip = self.clip_mask()
        if clip is not None:
            alpha = alpha * clip[y0:y1, x0:x1]
        target = self.rgb[y0:y1, x0:x1]
        if additive:
            target += color[..., :3] * alpha[..., None]
            np.clip(target, 0, 1, out=target)
        else:
            target *= (1 - alpha[..., None])
            target += color[..., :3] * alpha[..., None]

    def mesh(self, item):
        tex = texture(item["tex"]) if item["tex"] else None
        if item["tex"] and tex is None:
            return
        v = item["v"]
        verts = [(v[i], v[i + 1], v[i + 2], v[i + 3], unpack_color(int(v[i + 4]))) for i in range(0, len(v), 5)]
        idx = item["i"]
        for t in range(0, len(idx), 3):
            a, b, c = verts[idx[t]], verts[idx[t + 1]], verts[idx[t + 2]]
            self.triangle([(a[0], a[1]), (b[0], b[1]), (c[0], c[1])], [(a[2], a[3]), (b[2], b[3]), (c[2], c[3])],
                          [a[4], b[4], c[4]], tex, item.get("blend", 0) == 1)

    def text(self, item):
        m = item["m"]
        scale = (m[0] ** 2 + m[1] ** 2) ** 0.5
        f = font(item["font"], item["size"] * scale)
        layer = Image.new("L", (self.w, self.h), 0)
        draw = ImageDraw.Draw(layer)
        spacing = item.get("ls", 0.0) / 1000.0 * item["size"] * scale
        if spacing == 0.0:
            draw.text((m[4], m[5]), item["text"], font=f, fill=255)
        else:
            # The character spacing (1/1000 em) after each character, as the runtime measures it.
            x = m[4]
            for i, ch in enumerate(item["text"]):
                draw.text((x, m[5]), ch, font=f, fill=255)
                pair = item["text"][i:i + 2]
                x += (f.getlength(pair) - f.getlength(pair[1:]) if len(pair) == 2 else f.getlength(ch)) + spacing
        coverage = np.asarray(layer, dtype=np.float32) / 255.0
        r, g, b, a = item["color"]
        alpha = coverage * a
        clip = self.clip_mask()
        if clip is not None:
            alpha = alpha * clip
        self.rgb *= (1 - alpha[..., None])
        self.rgb += np.array([r, g, b], dtype=np.float32) * alpha[..., None]

    def image(self) -> Image.Image:
        return Image.fromarray((np.clip(self.rgb, 0, 1) * 255).astype(np.uint8), "RGB")


def render(draw: dict, background: Image.Image | None = None) -> Image.Image:
    w, h = int(draw["viewport"][0]), int(draw["viewport"][1])
    canvas = Canvas(w, h, background)
    for item in draw["items"]:
        kind = item["k"]
        if kind == "mesh":
            canvas.mesh(item)
        elif kind == "text":
            canvas.text(item)
        elif kind == "push":
            canvas.push_clip(item["q"])
        elif kind == "pop":
            canvas.pop_clip()
    return canvas.image()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("draw")
    parser.add_argument("out")
    parser.add_argument("--background")
    args = parser.parse_args(argv)
    draw = json.loads(Path(args.draw).read_text())
    background = Image.open(args.background) if args.background else None
    render(draw, background).save(args.out)
    print(json.dumps({"picture": args.out, "items": len(draw["items"])}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
