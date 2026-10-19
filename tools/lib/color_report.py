#!/usr/bin/env python3
"""The color report of the matched views (docs/ENVIRONMENT.md, "The color report"; tools/match-view.sh): the statistics of the colors of
a client picture against its retail picture, for the whole picture and for 3 bands (the top third: the sky or the high walls; the middle
third; the bottom third: the ground), with the character masked out of both pictures (the retail character is different).

For each region: the mean and the spread (the standard deviation) of R, G, and B (sRGB, 0 to 255), of the luminance (CIE L*, 0 to
100), of the saturation (HSV, 0 to 1), and of the hue (HSV, degrees: the circular mean weighted by the saturation, the circular
spread); the mean CIELAB color; the dark and the bright parts (the fraction of the pixels with L* under 10 and over 90); and, client
against retail, the differences and the CIEDE2000 distance of the two mean colors. The mask of the character: the box of the feet and
the head of the character of the JSON of the view (its feet, the camera, and the vertical field of view), projected in the picture,
0.55 m to each side, with a margin of 8 pixels.

Usage: python3 -B tools/lib/color_report.py FOLDER   (a folder of tools/match-view.sh: report.json and the pictures; writes
FOLDER/colors.json and FOLDER/colors.txt, and prints one line for each view)
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

BANDS = ("all", "top", "middle", "bottom")
CHARACTER_HALF_WIDTH = 0.55   # m
CHARACTER_HEIGHT = 2.0        # m, from the feet
MARGIN = 8                    # pixels


def srgb_to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def rgb_to_lab(rgb8: np.ndarray) -> np.ndarray:
    """@return The CIELAB colors (D65) of sRGB colors (..., 3) of 0 to 255."""
    linear = srgb_to_linear(rgb8.astype(np.float64) / 255.0)
    m = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
    xyz = linear @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > (6 / 29) ** 3, np.cbrt(xyz), xyz / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], axis=-1)


def ciede2000(lab1, lab2) -> float:
    """@return The CIEDE2000 color difference of two CIELAB colors (kL = kC = kH = 1; Sharma, Wu, and Dalal 2005)."""
    L1, a1, b1 = (float(x) for x in lab1)
    L2, a2, b2 = (float(x) for x in lab2)
    C1, C2 = math.hypot(a1, b1), math.hypot(a2, b2)
    Cm = (C1 + C2) / 2
    G = 0.5 * (1 - math.sqrt(Cm ** 7 / (Cm ** 7 + 25 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360 if C1p else 0.0
    h2p = math.degrees(math.atan2(b2, a2p)) % 360 if C2p else 0.0
    dLp = L2 - L1
    dCp = C2p - C1p
    if C1p * C2p == 0:
        dhp = 0.0
    elif abs(h2p - h1p) <= 180:
        dhp = h2p - h1p
    elif h2p - h1p > 180:
        dhp = h2p - h1p - 360
    else:
        dhp = h2p - h1p + 360
    dHp = 2 * math.sqrt(C1p * C2p) * math.sin(math.radians(dhp / 2))
    Lpm = (L1 + L2) / 2
    Cpm = (C1p + C2p) / 2
    if C1p * C2p == 0:
        hpm = h1p + h2p
    elif abs(h1p - h2p) <= 180:
        hpm = (h1p + h2p) / 2
    elif h1p + h2p < 360:
        hpm = (h1p + h2p + 360) / 2
    else:
        hpm = (h1p + h2p - 360) / 2
    T = (1 - 0.17 * math.cos(math.radians(hpm - 30)) + 0.24 * math.cos(math.radians(2 * hpm))
         + 0.32 * math.cos(math.radians(3 * hpm + 6)) - 0.20 * math.cos(math.radians(4 * hpm - 63)))
    dtheta = 30 * math.exp(-(((hpm - 275) / 25) ** 2))
    Rc = 2 * math.sqrt(Cpm ** 7 / (Cpm ** 7 + 25 ** 7))
    Sl = 1 + 0.015 * (Lpm - 50) ** 2 / math.sqrt(20 + (Lpm - 50) ** 2)
    Sc = 1 + 0.045 * Cpm
    Sh = 1 + 0.015 * Cpm * T
    Rt = -math.sin(math.radians(2 * dtheta)) * Rc
    return math.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2 + Rt * (dCp / Sc) * (dHp / Sh))


def project(point, camera, forward, vertical_fov_deg: float, width: int, height: int):
    """@return The pixel (x, y) of an engine point (m; X east, Y north, Z up) for a camera place and forward vector, or no value
    behind the camera."""
    f = np.array(forward, dtype=np.float64)
    f /= np.linalg.norm(f) or 1.0
    right = np.cross(f, [0.0, 0.0, 1.0])
    right = right / (np.linalg.norm(right) or 1.0) if np.linalg.norm(right) > 1e-6 else np.array([1.0, 0.0, 0.0])
    up = np.cross(right, f)
    d = np.array(point, dtype=np.float64) - np.array(camera, dtype=np.float64)
    z = d @ f
    if z <= 0.05:
        return None
    focal = (height / 2) / math.tan(math.radians(vertical_fov_deg) / 2)
    return width / 2 + focal * (d @ right) / z, height / 2 - focal * (d @ up) / z


def character_mask(view: dict, width: int, height: int) -> np.ndarray:
    """@return A mask (height, width) that is True outside the box of the character of a view JSON (feet, camera, fov)."""
    mask = np.ones((height, width), dtype=bool)
    feet = view.get("feet")
    camera = view.get("camera")
    if not feet or not camera:
        return mask
    place = [feet["x"], feet["y"], feet["z"]]
    eye = [camera["x"], camera["y"], camera["z"]]
    forward = camera.get("forward_vector") or [0.0, 1.0, 0.0]
    fov = (camera.get("fov") or {}).get("vertical_deg", 57.0)
    f = np.array(forward, dtype=np.float64)
    side = np.cross(f, [0.0, 0.0, 1.0])
    side = side / (np.linalg.norm(side) or 1.0)
    points = []
    for dz in (-0.1, CHARACTER_HEIGHT):
        for s in (-CHARACTER_HALF_WIDTH, CHARACTER_HALF_WIDTH):
            for along in (-CHARACTER_HALF_WIDTH, CHARACTER_HALF_WIDTH):
                p = np.array(place) + side * s + f * along + np.array([0.0, 0.0, dz])
                pixel = project(p, eye, forward, fov, width, height)
                if pixel is not None:
                    points.append(pixel)
    if not points:
        return mask
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    x0, x1 = max(int(min(xs)) - MARGIN, 0), min(int(max(xs)) + MARGIN, width)
    y0, y1 = max(int(min(ys)) - MARGIN, 0), min(int(max(ys)) + MARGIN, height)
    if x0 < x1 and y0 < y1:
        mask[y0:y1, x0:x1] = False
    return mask


def pixel_values(rgb8: np.ndarray) -> dict:
    """@return The values of each pixel of a picture: the color (float), CIELAB, the HSV saturation and hue (degrees)."""
    pixels = rgb8.reshape(-1, 3).astype(np.float64)
    mx, mn = pixels.max(axis=1), pixels.min(axis=1)
    saturation = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-9), 0.0)
    r, g, b = pixels[:, 0], pixels[:, 1], pixels[:, 2]
    delta = np.maximum(mx - mn, 1e-9)
    hue = np.where(mx == r, ((g - b) / delta) % 6, np.where(mx == g, (b - r) / delta + 2, (r - g) / delta + 4)) * 60.0
    return {"rgb": pixels, "lab": rgb_to_lab(pixels), "saturation": saturation, "hue": hue}


def region_stats(values: dict, valid: np.ndarray) -> dict:
    """@return The statistics of the valid pixels (a flat mask) of a picture (the values of pixel_values)."""
    index = valid.reshape(-1)
    pixels, lab = values["rgb"][index], values["lab"][index]
    if len(pixels) == 0:
        return {"pixels": 0}
    saturation, hue = values["saturation"][index], values["hue"][index]
    sx, sy = (saturation * np.cos(np.radians(hue))).sum(), (saturation * np.sin(np.radians(hue))).sum()
    strength = math.hypot(sx, sy) / max(saturation.sum(), 1e-9)
    return {"pixels": int(len(pixels)),
            "rgb mean": [round(float(x), 2) for x in pixels.mean(axis=0)], "rgb spread": [round(float(x), 2) for x in pixels.std(axis=0)],
            "L mean": round(float(lab[:, 0].mean()), 2), "L spread": round(float(lab[:, 0].std()), 2),
            "saturation mean": round(float(saturation.mean()), 4), "saturation spread": round(float(saturation.std()), 4),
            "hue mean": round(math.degrees(math.atan2(sy, sx)) % 360, 1),
            "hue spread": round(math.degrees(math.sqrt(max(-2 * math.log(max(strength, 1e-9)), 0.0))), 1),
            "lab mean": [round(float(x), 3) for x in lab.mean(axis=0)],
            "dark part": round(float((lab[:, 0] < 10).mean()), 4), "bright part": round(float((lab[:, 0] > 90).mean()), 4)}


def band_masks(height: int, width: int) -> dict:
    rows = np.arange(height)[:, None] * np.ones((1, width))
    return {"all": np.ones((height, width), dtype=bool), "top": rows < height / 3,
            "middle": (rows >= height / 3) & (rows < 2 * height / 3), "bottom": rows >= 2 * height / 3}


def hue_difference(a: float, b: float) -> float:
    return (a - b + 180.0) % 360.0 - 180.0


def compare(client: Path, retail: Path, view_json: dict) -> dict:
    """@return The color statistics of a client picture against its retail picture (both 1280 x 720 after a resize)."""
    a = pixel_values(np.asarray(Image.open(client).convert("RGB").resize((1280, 720))))
    b = pixel_values(np.asarray(Image.open(retail).convert("RGB").resize((1280, 720))))
    keep = character_mask(view_json, 1280, 720)
    out = {"mask": {"pixels masked": int((~keep).sum())}}
    for name, band in band_masks(720, 1280).items():
        c, r = region_stats(a, band & keep), region_stats(b, band & keep)
        if not c.get("pixels") or not r.get("pixels"):
            continue
        out[name] = {"client": c, "retail": r, "difference": {
            "rgb mean": [round(x - y, 2) for x, y in zip(c["rgb mean"], r["rgb mean"])],
            "L mean": round(c["L mean"] - r["L mean"], 2), "L spread": round(c["L spread"] - r["L spread"], 2),
            "saturation mean": round(c["saturation mean"] - r["saturation mean"], 4),
            "hue mean": round(hue_difference(c["hue mean"], r["hue mean"]), 1),
            "dark part": round(c["dark part"] - r["dark part"], 4), "bright part": round(c["bright part"] - r["bright part"], 4),
            "delta E 2000": round(ciede2000(c["lab mean"], r["lab mean"]), 2)}}
    return out


def summary(name: str, colors: dict) -> str:
    """@return One line for a view: the CIEDE2000 distance of each band, and the differences (client - retail) of L*, of its spread
    (the contrast), of the saturation, and of the hue, and the dark parts."""
    parts = []
    for band in BANDS:
        d = (colors.get(band) or {}).get("difference")
        if d:
            parts.append(f"{band} dE {d['delta E 2000']:5.1f} L {d['L mean']:+5.1f} Lsd {d['L spread']:+5.1f} "
                         f"S {d['saturation mean']:+.3f} h {d['hue mean']:+5.0f}")
    dark = (colors.get("all") or {})
    dark_text = (f" | dark {dark['client']['dark part']:.2f}/{dark['retail']['dark part']:.2f}" if dark else "")
    return f"{name}: " + " | ".join(parts) + dark_text


def panel_lines(name: str, colors: dict) -> list[str]:
    """@return The lines of the number panel of a view in the contact sheet."""
    lines = [name, "band     dE00   L* c/r     Lsd c/r    sat c/r      hue c/r"]
    for band in BANDS:
        entry = colors.get(band)
        if not entry:
            continue
        c, r, d = entry["client"], entry["retail"], entry["difference"]
        lines.append(f"{band:7s} {d['delta E 2000']:5.1f}  {c['L mean']:4.0f}/{r['L mean']:<4.0f}  {c['L spread']:4.0f}/{r['L spread']:<4.0f}  "
                     f"{c['saturation mean']:.2f}/{r['saturation mean']:.2f}  {c['hue mean']:4.0f}/{r['hue mean']:<4.0f}")
    if colors.get("all"):
        a = colors["all"]
        lines.append(f"dark (L*<10) {a['client']['dark part']:.2f}/{a['retail']['dark part']:.2f}   "
                     f"bright (L*>90) {a['client']['bright part']:.2f}/{a['retail']['bright part']:.2f}")
    return lines


def view_json_of(view: dict) -> dict:
    try:
        return json.loads(Path(view["json"]).read_text())
    except (KeyError, OSError, ValueError):
        return {}


def folder_report(folder: Path) -> dict:
    """Writes FOLDER/colors.json and FOLDER/colors.txt for the views of FOLDER/report.json. @return The colors by view."""
    report = json.loads((folder / "report.json").read_text())
    plan = {v["name"]: v for v in json.loads((folder / "plan.json").read_text())["views"]} if (folder / "plan.json").is_file() else {}
    colors, lines = {}, []
    for view in report["views"]:
        client, retail = Path(view["out"]), Path(view["reference"])
        if not client.is_file() or not retail.is_file():
            continue
        source = dict(view)
        source.setdefault("json", plan.get(view["name"], {}).get("json", ""))
        colors[view["name"]] = compare(client, retail, view_json_of(source))
        lines.append(summary(view["name"], colors[view["name"]]))
    (folder / "colors.json").write_text(json.dumps(colors, indent=1))
    (folder / "colors.txt").write_text("\n".join(lines) + "\n")
    return colors


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    folder = Path(sys.argv[1])
    colors = folder_report(folder)
    print((folder / "colors.txt").read_text(), end="")
    return 0 if colors else 1


if __name__ == "__main__":
    sys.exit(main())
