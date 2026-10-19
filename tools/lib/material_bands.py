"""The material colors of matched views, sunlit and shadowed apart (the materials track, docs/MATERIALS.md "Material colors").

For each box of a view (one material, the same place in the retail picture and in the client picture; fractions of the
1280 x 720 picture), the pixels of each picture are split into a sunlit and a shadowed class by the Otsu threshold of their
L* (each picture alone: the shadow fill of the client must not move the material values). A box with less than 12 L* between
its two classes has no shadow: all its pixels are sunlit. For each class: the share of the pixels, the mean color (from the
mean linear RGB) as L*, a*, b*, chroma, hue, and the CIEDE2000 distance client against retail. A box marked "ref" is a light
reference (a neutral material): the client / retail ratio of its sunlit linear RGB is the cast of the light; each material
also gets its sunlit ratio divided by that cast (a value near 1 in each channel: the material follows the light).

Usage: python3 -B tools/lib/material_bands.py BOXES.json FOLDER [VIEW...]
BOXES.json: {"VIEW": {"reference": "SET/VIEW.png" (under the reference folder), "boxes": {"NAME": [x0, x1, y0, y1, ref]}}}
FOLDER: a tools/match-view.sh folder (FOLDER/client/VIEW.png). Writes FOLDER/material-bands.json and material-bands.txt.
"""
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REFERENCES = Path.home() / "Games/soulwarden/work/evidence/unreal-references"
SHADOW_SPLIT_L = 12.0


def linear(srgb: np.ndarray) -> np.ndarray:
    a = srgb / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def lab(rgb_linear: np.ndarray) -> np.ndarray:
    m = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
    xyz = rgb_linear @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def ciede2000(a: np.ndarray, b: np.ndarray) -> float:
    l1, a1, b1 = a
    l2, a2, b2 = b
    c1, c2 = math.hypot(a1, b1), math.hypot(a2, b2)
    g = 0.5 * (1 - math.sqrt(((c1 + c2) / 2) ** 7 / (((c1 + c2) / 2) ** 7 + 25 ** 7)))
    a1p, a2p = (1 + g) * a1, (1 + g) * a2
    c1p, c2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p, h2p = math.degrees(math.atan2(b1, a1p)) % 360, math.degrees(math.atan2(b2, a2p)) % 360
    dl, dc = l2 - l1, c2p - c1p
    dh = 0.0 if c1p * c2p == 0 else (h2p - h1p if abs(h2p - h1p) <= 180 else h2p - h1p - 360 if h2p > h1p else h2p - h1p + 360)
    dhh = 2 * math.sqrt(c1p * c2p) * math.sin(math.radians(dh / 2))
    lm, cm = (l1 + l2) / 2, (c1p + c2p) / 2
    hm = h1p + h2p if c1p * c2p == 0 else ((h1p + h2p) / 2 if abs(h1p - h2p) <= 180 else (h1p + h2p + 360) / 2 if h1p + h2p < 360 else (h1p + h2p - 360) / 2)
    t = 1 - 0.17 * math.cos(math.radians(hm - 30)) + 0.24 * math.cos(math.radians(2 * hm)) + 0.32 * math.cos(math.radians(3 * hm + 6)) - 0.20 * math.cos(math.radians(4 * hm - 63))
    sl = 1 + 0.015 * (lm - 50) ** 2 / math.sqrt(20 + (lm - 50) ** 2)
    sc, sh = 1 + 0.045 * cm, 1 + 0.015 * cm * t
    rt = -2 * math.sqrt(cm ** 7 / (cm ** 7 + 25 ** 7)) * math.sin(math.radians(60 * math.exp(-(((hm - 275) / 25) ** 2))))
    return math.sqrt((dl / sl) ** 2 + (dc / sc) ** 2 + (dhh / sh) ** 2 + rt * (dc / sc) * (dhh / sh))


def otsu(values: np.ndarray) -> float:
    hist, edges = np.histogram(values, bins=100, range=(0.0, 100.0))
    p = hist / max(hist.sum(), 1)
    w = np.cumsum(p)
    mu = np.cumsum(p * np.arange(100))
    between = (mu[-1] * w - mu) ** 2 / np.maximum(w * (1 - w), 1e-12)
    return float(edges[int(np.argmax(between)) + 1])


def classes(rgb: np.ndarray) -> dict:
    """@return {"sunlit": stats, "shadow": stats or None} of the pixels of a box (sRGB bytes, n x 3)."""
    lin = linear(rgb.astype(np.float64))
    lstar = lab(lin)[:, 0]
    split = otsu(lstar)
    sun, shade = lstar >= split, lstar < split
    if not shade.any() or not sun.any() or lstar[sun].mean() - lstar[shade].mean() < SHADOW_SPLIT_L:
        sun, shade = np.ones_like(lstar, bool), np.zeros_like(lstar, bool)
    out = {}
    for name, mask in (("sunlit", sun), ("shadow", shade)):
        if not mask.any():
            out[name] = None
            continue
        mean = lin[mask].mean(0)
        l, a, b = lab(mean)
        out[name] = {"share": round(float(mask.mean()), 2), "linear": [round(float(v), 4) for v in mean],
                     "lab": [round(float(l), 1), round(float(a), 1), round(float(b), 1)],
                     "chroma": round(math.hypot(a, b), 1), "hue": round(math.degrees(math.atan2(b, a)) % 360)}
    return out


def main(argv: list[str]) -> int:
    boxes_file, folder = Path(argv[0]), Path(argv[1])
    wanted = set(argv[2:])
    config = json.loads(boxes_file.read_text())
    result, lines = {}, []
    for view, spec in config.items():
        client_path = folder / "client" / f"{view}.png"
        if (wanted and view not in wanted) or not client_path.is_file():
            continue
        retail = np.asarray(Image.open(REFERENCES / spec["reference"]).convert("RGB").resize((1280, 720)))
        client = np.asarray(Image.open(client_path).convert("RGB").resize((1280, 720)))
        rows, casts = {}, []
        for name, (x0, x1, y0, y1, ref) in spec["boxes"].items():
            box = (slice(int(y0 * 720), int(y1 * 720)), slice(int(x0 * 1280), int(x1 * 1280)))
            r, c = classes(retail[box].reshape(-1, 3)), classes(client[box].reshape(-1, 3))
            row = {"reference": bool(ref), "retail": r, "client": c}
            for cls in ("sunlit", "shadow"):
                if r[cls] and c[cls]:
                    row[f"{cls} dE"] = round(ciede2000(np.array(r[cls]["lab"]), np.array(c[cls]["lab"])), 1)
            if r["sunlit"] and c["sunlit"]:
                row["sunlit ratio"] = [round(cv / max(rv, 1e-6), 2) for cv, rv in zip(c["sunlit"]["linear"], r["sunlit"]["linear"])]
                if ref:
                    casts.append(np.array(row["sunlit ratio"]))
            rows[name] = row
        cast = np.exp(np.mean(np.log(np.maximum(casts, 1e-6)), 0)) if casts else None
        lines.append(f"{view}: light cast (client / retail, sunlit references) {np.round(cast, 2).tolist() if cast is not None else 'none'}")
        for name, row in rows.items():
            if cast is not None and "sunlit ratio" in row:
                row["sunlit ratio / cast"] = [round(v, 2) for v in (np.array(row["sunlit ratio"]) / cast).tolist()]

            def fmt(s):
                return "-" if not s else f"L {s['lab'][0]:5.1f} C {s['chroma']:4.1f} h {s['hue']:3d} ({int(s['share'] * 100):3d}%)"

            lines.append(f"  {name:26} sunlit: retail {fmt(row['retail']['sunlit'])} client {fmt(row['client']['sunlit'])} "
                         f"dE {row.get('sunlit dE', '-')} ratio/cast {row.get('sunlit ratio / cast', '-')}"
                         f"{'  [ref]' if row['reference'] else ''}")
            lines.append(f"  {'':26} shadow: retail {fmt(row['retail']['shadow'])} client {fmt(row['client']['shadow'])} "
                         f"dE {row.get('shadow dE', '-')}")
        result[view] = {"cast": cast.tolist() if cast is not None else None, "boxes": rows}
    (folder / "material-bands.json").write_text(json.dumps(result, indent=1))
    (folder / "material-bands.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
