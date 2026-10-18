"""The check of stage E0 (docs/ENVIRONMENT.md): the data of the environment reader at the Oxboro reference place, beside the
retail reference pictures. It does not claim a render match: the client does not draw these values yet.

Usage: python3 -B -m swc_convert.environment_check [--research FOLDER] [--references FOLDER] [--out FOLDER]

1. The time-of-day volumes that hold the Oxboro point of the research (8880, 4250, 140.5): the Everfall cylinders.
2. The preset of these volumes at 12:00 and 00:00 (the hour / 24 of the retail curves), against the values of the research
   (world-environment/everfall-noon-midnight.json; exact keys and its compatible spline evaluation).
3. The main-world mission (sun rotation, longitude, moon) of levels/newworld_vitaeeterna/level.pak.
4. The two diagrams of the research report, made from the output of this reader, beside l1-noon.png and l1-midnight.png of the
   Soulwarden evidence (world-time/implement), and the curves of the sun, the fog, the probes, and the exposure.
Output: OUT/e0-check.json, OUT/oxboro-noon-data.png, OUT/oxboro-midnight-data.png, OUT/everfall-curves.png.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from . import environment, env

OXBORO = (8880.0, 4250.0, 140.5)
FONT = "/usr/share/fonts/TTF/DejaVuSans.ttf"
BACKGROUND, TEXT, LABEL, NOTE = (19, 25, 34), (238, 242, 249), (165, 180, 200), (175, 190, 209)


def font(size: int):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def srgb(color) -> tuple:
    a = np.clip(np.array(color, dtype=np.float64), 0, 1)
    s = np.where(a <= 0.0031308, 12.92 * a, 1.055 * a ** (1 / 2.4) - 0.055)
    return tuple(int(v) for v in (s * 255).round())


def same(a, b, tolerance: float) -> bool:
    if isinstance(a, list) or isinstance(b, list):
        return isinstance(a, list) and isinstance(b, list) and len(a) == len(b) and all(abs(x - y) <= tolerance * max(1.0, abs(y)) for x, y in zip(a, b))
    return a is not None and b is not None and abs(a - b) <= tolerance * max(1.0, abs(b))


def diagram(values: dict, label: str, reference: Path, out: Path) -> None:
    """The research layout: the retail picture, the main authored values, and three color swatches (linear values shown as sRGB)."""
    image = Image.new("RGB", (1680, 660), BACKGROUND)
    draw = ImageDraw.Draw(image)
    picture = Image.open(reference).convert("RGB")
    picture.thumbnail((1050, 552))
    image.paste(picture, (0, 63))
    draw.text((18, 14), f"Oxboro at {label}  |  retail reference and the data of the environment reader", font=font(26), fill=TEXT)
    x, y = 1080, 73
    rows = [("Sun intensity", f"{values['Sun intensity']:,.0f}"), ("Legacy fog density", f"{values['Volumetric fog: Global density']:g}"),
            ("Fog ramp end", f"{values['Volumetric fog: Ramp end']:g} m"),
            ("Fog 2 global density", f"{values['Volumetric fog 2: Global fog density']:.6f}"),
            ("Fog 2 extinction", f"{values['Volumetric fog 2: Extinction factor']:g}"),
            ("Probe diffuse multiplier", f"{values['Environment Probe Diffuse Multiplier']:.2f}"),
            ("Film shoulder", f"{values['Film curve shoulder scale']:g}"), ("EV min / max", f"{values['EV Min']:g} / {values['EV Max']:g}")]
    for name, value in rows:
        draw.text((x, y), name, font=font(20), fill=LABEL)
        draw.text((x + 350, y), value, font=font(22), fill=TEXT)
        y += 38
    for name, multiplier, title in (("Sun color", None, "Directional RGB"), ("Fog color", "Fog color multiplier", "Bottom fog RGB x multiplier"),
                                    ("Color balance", None, "Color balance RGB")):
        color = np.array(values[name]) * (values[multiplier] if multiplier else 1.0)
        draw.rectangle((x, y + 3, x + 45, y + 33), fill=srgb(color))
        draw.text((x + 60, y), title, font=font(17), fill=LABEL)
        y += 27
        draw.text((x + 60, y), ", ".join(f"{v:.6g}" for v in color), font=font(17), fill=TEXT)
        y += 35
    draw.text((18, 617), "Source: libs/timeofday/zones/everfall_a.xml (SWC_CONTENT/environment/presets.json). RGB swatches: linear values shown as sRGB.",
              font=font(17), fill=NOTE)
    draw.text((18, 642), "A data proof of the reader: the client does not draw these values yet (stage E1 and later).", font=font(16), fill=NOTE)
    image.save(out)


def curves(preset: dict, out: Path) -> None:
    """Four plots of the curves over 24 hours (Pillow; the endpoints at 00:00 and 12:00 are marked)."""
    groups = [(["Sun intensity"], "Directional intensity"), (["Volumetric fog 2: Global fog density", "Volumetric fog 2: Extinction factor"], "Fog 2 density and extinction"),
              (["Environment Probe Diffuse Multiplier", "Environment Probe Specular Multiplier"], "Ambient probe multipliers"), (["EV Min", "EV Max"], "Exposure bounds (source units)")]
    by_name = {}
    for variable in preset["variables"]:
        by_name.setdefault(variable["name"], variable)
    image = Image.new("RGB", (1600, 900), BACKGROUND)
    draw = ImageDraw.Draw(image)
    draw.text((20, 12), "Everfall curves of the environment reader (hour / 24; between keys: the compatible Bezier rule)", font=font(22), fill=TEXT)
    colors = [(239, 185, 89), (123, 174, 244)]
    hours = np.linspace(0, 24, 481)
    for index, (names, title) in enumerate(groups):
        left, top = 60 + (index % 2) * 790, 70 + (index // 2) * 415
        width, height = 700, 320
        series = [[environment.evaluate(by_name[n]["keys"], environment.curve_time(h)) for h in hours] for n in names]
        low = min(min(s) for s in series)
        high = max(max(s) for s in series)
        span = (high - low) or 1.0
        draw.rectangle((left, top, left + width, top + height), outline=(98, 115, 138), fill=(25, 35, 50))
        draw.text((left, top - 28), title, font=font(18), fill=TEXT)
        for hour in (0, 6, 12, 18, 24):
            gx = left + hour / 24 * width
            draw.line((gx, top, gx, top + height), fill=(53, 70, 95))
            draw.text((gx - 8, top + height + 6), str(hour), font=font(14), fill=LABEL)
        for k, (name, values) in enumerate(zip(names, series)):
            points = [(left + h / 24 * width, top + height - (v - low) / span * height) for h, v in zip(hours, values)]
            draw.line(points, fill=colors[k], width=2)
            for h in (0, 12):
                v = environment.evaluate(by_name[name]["keys"], environment.curve_time(h))
                px, py = left + h / 24 * width, top + height - (v - low) / span * height
                draw.ellipse((px - 4, py - 4, px + 4, py + 4), fill=colors[k])
            draw.text((left + 10, top + 8 + k * 20), name.replace("Volumetric fog 2: ", "").replace("Environment Probe ", ""), font=font(14), fill=colors[k])
        draw.text((left + width - 120, top + 8), f"{low:g} .. {high:g}", font=font(13), fill=LABEL)
    image.save(out)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-environment-check", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--research", default=str(Path.home() / "Games/soulwarden-client/research/world-environment"))
    parser.add_argument("--references", default=str(Path.home() / "Games/soulwarden/work/evidence/world-time/implement"))
    parser.add_argument("--out", default=str(env.work_dir() / "evidence" / "E0"))
    args = parser.parse_args(argv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report: dict = {"point": OXBORO}
    # 1. The volumes.
    volumes = environment.time_volumes_at(OXBORO)
    report["volumes"] = volumes
    names = sorted({v["preset"] for v in volumes})
    report["presetsAtPoint"] = names
    # 2. The samples against the research.
    presets = json.loads((environment.out_dir() / "presets.json").read_text())
    preset = presets["libs/timeofday/zones/everfall_a.xml"]
    noon, midnight = environment.sample(preset, 12.0), environment.sample(preset, 0.0)
    research_file = Path(args.research) / "everfall-noon-midnight.json"
    compared, mismatches = 0, []
    if research_file.is_file():
        research = json.loads(research_file.read_text())
        for row in research:
            variable = preset["variables"][row["index"]]
            if variable["name"] != row["name"]:
                mismatches.append(f"variable {row['index']}: {variable['name']} != {row['name']}")
                continue
            if variable["keys"] != [list(k) for k in row["keys"]]:
                mismatches.append(f"{row['name']}: the keys differ")
            for hour, key in ((12.0, "noon"), (0.0, "midnight")):
                mine = environment.evaluate(variable["keys"], environment.curve_time(hour)) if variable["keys"] else variable["saved"]
                theirs = row[key]["value"]
                compared += 1
                if not same(mine, theirs, 1e-6):
                    mismatches.append(f"{row['name']} at {key}: {mine} != {theirs} ({row[key]['basis']})")
    report["research"] = {"file": str(research_file), "variables": len(preset["variables"]), "compared": compared, "mismatches": mismatches}
    # 3. The mission.
    mission = json.loads((environment.out_dir() / "mission.json").read_text())
    lighting, moon = mission["environment"].get("Lighting", {}), mission["environment"].get("Moon", {})
    report["mission"] = {"source": mission["source"], "SunRotation": lighting.get("SunRotation"), "Longitude": lighting.get("Longitude"),
                         "SunLinkedToTOD": mission["environment"].get("EnvState", {}).get("SunLinkedToTOD"),
                         "Moon": {k: moon.get(k) for k in ("Latitude", "Longitude", "Size", "Texture")}}
    # 4. The pictures.
    references = Path(args.references)
    pictures = []
    for values, label, name, file in ((noon, "12:00", "noon", "l1-noon.png"), (midnight, "00:00", "midnight", "l1-midnight.png")):
        reference = references / file
        if reference.is_file():
            diagram(values, label, reference, out / f"oxboro-{name}-data.png")
            pictures.append({"reference": str(reference), "sha256": hashlib.sha256(reference.read_bytes()).hexdigest(),
                             "diagram": str(out / f"oxboro-{name}-data.png")})
    curves(preset, out / "everfall-curves.png")
    report["pictures"] = pictures
    report["values"] = {"12:00": {k: noon[k] for k in ("Sun color", "Sun intensity", "Fog color", "Fog color multiplier", "Color balance", "EV Min", "EV Max")},
                        "00:00": {k: midnight[k] for k in ("Sun color", "Sun intensity", "Fog color", "Fog color multiplier", "Color balance", "EV Min", "EV Max")}}
    (out / "e0-check.json").write_text(json.dumps(report, indent=1))
    ok = len(volumes) == 4 and names == ["libs/timeofday/zones/everfall_a.xml"] and not mismatches and compared > 0
    print(json.dumps({"volumes": len(volumes), "presets": names, "compared": compared, "mismatches": len(mismatches), "mission": report["mission"],
                      "pictures": [p["diagram"] for p in pictures], "ok": ok}, indent=1))
    for line in mismatches[:20]:
        print("MISMATCH", line)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
