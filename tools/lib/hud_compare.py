#!/usr/bin/env python3
"""The place and the size of the HUD parts, client against retail (stage P3.H, docs/UI.md "The fight HUD"): two pictures of the
same window size (a retail picture and a client picture, for example 1256 x 660). For each region of the HUD (a box in the retail
picture), the edges of the retail region are the pattern; the tool finds the shift (and the scale, 0.94 to 1.06) of the client
picture where the edges agree best (the normalized correlation of the edge strength). The world behind the HUD is different in
the two pictures: the edges of the HUD frames carry the match, and the score tells how sure it is (1.0: the same edges).

Output: FOLDER/hud-compare.json (each region: dx, dy in pixels of the client against retail, the scale, the score), and for each
region FOLDER/hud-REGION.png: the retail region (top), the client region at the same place (middle), and the two edge maps in one
picture (bottom: retail red, client green, the same edges yellow).

Usage: python3 -B tools/lib/hud_compare.py RETAIL.png CLIENT.png FOLDER [REGION=x0,y0,x1,y1 ...]
  (the default regions are those of a 1256 x 660 window: compass, consumables, vitals, level, weapons)
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# The regions of the fight HUD in a window of 1256 x 660 (the retail pictures of the Soulwarden evidence).
DEFAULT_REGIONS = {
    "compass": (330, 8, 926, 52),
    "consumables": (338, 582, 506, 646),
    "vitals": (522, 582, 734, 620),
    "level": (600, 618, 660, 646),
    "weapons": (744, 552, 1000, 630),
}
SEARCH = 24            # pixels in each direction
SCALES = (0.94, 0.97, 1.0, 1.03, 1.06)


def edges(rgb: np.ndarray) -> np.ndarray:
    """@return The edge strength (the gradient length of the luminance), 0 to 1."""
    y = rgb[..., :3].astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    gx = np.zeros_like(y)
    gy = np.zeros_like(y)
    gx[:, 1:-1] = y[:, 2:] - y[:, :-2]
    gy[1:-1, :] = y[2:, :] - y[:-2, :]
    e = np.sqrt(gx * gx + gy * gy)
    return e / max(float(e.max()), 1e-6)


def ncc(a: np.ndarray, b: np.ndarray) -> float:
    a = a - a.mean()
    b = b - b.mean()
    d = float(np.sqrt((a * a).sum() * (b * b).sum()))
    return float((a * b).sum() / d) if d > 0 else 0.0


def match(retail: Image.Image, client: Image.Image, box: tuple[int, int, int, int]) -> dict:
    """@return The best shift and scale of the client picture for one retail region."""
    x0, y0, x1, y1 = box
    pattern = edges(np.asarray(retail.crop(box)))
    best = {"dx": 0, "dy": 0, "scale": 1.0, "score": -1.0}
    cw, ch = client.size
    for scale in SCALES:
        scaled = client if scale == 1.0 else client.resize((round(cw * scale), round(ch * scale)), Image.BILINEAR)
        e = edges(np.asarray(scaled))
        # The region in the scaled picture: the HUD scales about the center of the region.
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        w, h = x1 - x0, y1 - y0
        bx = round(cx * scale - w / 2)
        by = round(cy * scale - h / 2)
        for dy in range(-SEARCH, SEARCH + 1):
            for dx in range(-SEARCH, SEARCH + 1):
                sx, sy = bx + dx, by + dy
                if sx < 0 or sy < 0 or sx + w > e.shape[1] or sy + h > e.shape[0]:
                    continue
                s = ncc(pattern, e[sy:sy + h, sx:sx + w])
                if s > best["score"]:
                    # The shift in window pixels: the place of the region center in the client picture less the retail one.
                    best = {"dx": round((sx + w / 2) / scale - cx, 1), "dy": round((sy + h / 2) / scale - cy, 1), "scale": scale,
                            "score": round(s, 3)}
    return best


def panel(retail: Image.Image, client: Image.Image, box: tuple[int, int, int, int], zoom: int = 3) -> Image.Image:
    r = retail.crop(box)
    c = client.crop(box)
    er = edges(np.asarray(r))
    ec = edges(np.asarray(c))
    both = np.zeros(er.shape + (3,), np.uint8)
    both[..., 0] = np.clip(er * 400, 0, 255)
    both[..., 1] = np.clip(ec * 400, 0, 255)
    w, h = r.size
    out = Image.new("RGB", (w * zoom, h * zoom * 3 + 8), (0, 0, 0))
    for i, im in enumerate((r.convert("RGB"), c.convert("RGB"), Image.fromarray(both))):
        out.paste(im.resize((w * zoom, h * zoom), Image.NEAREST), (0, i * (h * zoom + 4)))
    return out


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    retail = Image.open(argv[0]).convert("RGB")
    client = Image.open(argv[1]).convert("RGB")
    if retail.size != client.size:
        print(f"hud_compare: the sizes differ: {retail.size} and {client.size}", file=sys.stderr)
        return 1
    folder = Path(argv[2])
    folder.mkdir(parents=True, exist_ok=True)
    regions = dict(DEFAULT_REGIONS)
    for arg in argv[3:]:
        name, _, value = arg.partition("=")
        regions[name] = tuple(int(v) for v in value.split(","))
    report = {"retail": str(argv[0]), "client": str(argv[1]), "size": list(retail.size), "regions": {}}
    for name, box in regions.items():
        result = match(retail, client, box)
        result["box"] = list(box)
        report["regions"][name] = result
        panel(retail, client, box).save(folder / f"hud-{name}.png")
        print(f"{name:12s} dx {result['dx']:6.1f}  dy {result['dy']:6.1f}  scale {result['scale']:.2f}  score {result['score']:.3f}")
    (folder / "hud-compare.json").write_text(json.dumps(report, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
