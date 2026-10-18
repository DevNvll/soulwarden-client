"""The terrain of the world for the Landscape actors (stage 1.3).

The heights: `region.heightmap` (2048 x 2048 samples of 16 bits at 1 m, height = sample x 2048 / 65535 m), then the
terrain cookies of the capitals of the region (the rule of the client: the vendored `navcore cookies`, see
vendor/soulwarden/nav_terrain.py and the Soulwarden protocol notes 4.19).

The Landscape grid: one Landscape for each region, 8 x 8 components of 255 quads (one section), thus 2,041 x 2,041
vertices over 2,048 m (a quad of 2048 / 2040 m = 100.39 cm). A region with its shared edge has 2,049 x 2,049 samples
(the last row and column are the first of the next region); a bilinear resample gives the 2,041 x 2,041 vertices.
Unreal Landscape height: (value - 32768) / 128 x Z scale. The Z scale 128 x 204800 / 65535 = 400.006 cm gives one step
of 3.125048 cm, the step of the source (2048 / 65535 m): the value is the source sample, and the actor is at
Z = 32768 steps (1,024.02 m). (A Z scale of 400 would give an error of 3.1 cm at 2,048 m.)
The raw file has row 0 = north (the smallest Unreal Y) and column 0 = west (space map of stage 1.1).
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
from array import array
from pathlib import Path

import numpy as np

from . import dds, env, region, source
from nav_terrain import apply_cookies, read_cookies

VENDOR = Path(__file__).resolve().parent.parent / "vendor" / "soulwarden"
COMPONENTS = 8
QUADS = 255
VERTICES = COMPONENTS * QUADS + 1      # 2041
QUAD_CM = 204800.0 / (VERTICES - 1)   # 100.392 cm
Z_SCALE_CM = 128.0 * 204800.0 / 65535.0    # 400.006 cm: one step = 2048 / 65535 m
Z_ORIGIN_CM = 32768 * Z_SCALE_CM / 128.0    # 102,401.6 cm


def compiler(names: tuple[str, ...]) -> str:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no compiler: install one of " + ", ".join(names))


def navcore() -> tuple[Path, Path]:
    """Compiles the vendored navcore (when its sources are newer) and makes its noise texture. @return (program, noise)."""
    folder = env.cache_dir() / "navcore"
    program, noise = folder / "navcore", folder / "noise.f32"
    noise_source = VENDOR / "vendor/open-simplex-noise/open-simplex-noise.c"
    sources = [VENDOR / "navcore.cpp", noise_source, VENDOR / "vendor/open-simplex-noise/open-simplex-noise.h"]
    if not program.is_file() or any(s.stat().st_mtime > program.stat().st_mtime for s in sources):
        folder.mkdir(parents=True, exist_ok=True)
        obj = folder / "open-simplex-noise.o"
        # No joined float operations: the terrain rule needs the numbers of the client (navcore.cpp).
        steps = [[compiler(("clang", "gcc")), "-O2", "-ffp-contract=off", "-c", str(noise_source), "-o", str(obj)],
                 [compiler(("clang++", "g++")), "-std=c++17", "-O2", "-ffp-contract=off", "-pthread", f"-I{VENDOR}",
                  "-o", str(program) + ".part", str(VENDOR / "navcore.cpp"), str(obj)]]
        for step in steps:
            result = subprocess.run(step, capture_output=True, text=True, check=False)
            if result.returncode != 0:
                raise RuntimeError(f"the compiler failed: {result.stderr.strip()[:2000]}")
        os.replace(str(program) + ".part", program)
        noise.unlink(missing_ok=True)
    if not noise.is_file():
        result = subprocess.run([str(program), "noise", str(noise)], capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"navcore noise failed: {result.stderr.strip()}")
    return program, noise


def heights(name: str) -> tuple[np.ndarray, dict]:
    """@return (the 2048 x 2048 samples of a region after its cookies, row 0 = local y 0, the counts of the rule)."""
    reg = region.Region(name)
    raw = array("H", reg.heights().tobytes())
    cookies = read_cookies(source.paks(), reg.folder)
    program, noise = navcore()
    changed, counts = apply_cookies(raw, reg.origin, cookies, program, noise)
    return np.frombuffer(changed.tobytes(), dtype=np.uint16).reshape(2048, 2048).copy(), counts


def neighbor(name: str, dx: int, dy: int) -> str | None:
    rx, ry = (int(v) for v in name[2:].split("_"))
    other = f"r_+{rx + dx:02d}_+{ry + dy:02d}"
    return other if source.has(region.WORLD + "regions/" + other + "/region.heightmap") else None


def landscape(name: str, cache: dict | None = None) -> dict:
    """Writes `regions/NAME/landscape.r16` (2,041 x 2,041, u16 little-endian, row 0 = north) and `landscape.json`.
    @return The layout of the Landscape of the region (Unreal units)."""
    cache = cache if cache is not None else {}

    def samples(n: str) -> np.ndarray:
        if n not in cache:
            cache[n] = heights(n)
        return cache[n][0]

    h = samples(name).astype(np.float64)
    grid = np.empty((2049, 2049))
    grid[:2048, :2048] = h
    east, north, corner = neighbor(name, 1, 0), neighbor(name, 0, 1), neighbor(name, 1, 1)
    grid[:2048, 2048] = samples(east)[:, 0] if east else h[:, 2047]
    grid[2048, :2048] = samples(north)[0, :] if north else h[2047, :]
    grid[2048, 2048] = samples(corner)[0, 0] if corner else grid[2047, 2047]
    # Bilinear resample of the 2,049 samples (0..2048 m) to the 2,041 vertices (0..2048 m).
    t = np.arange(VERTICES) * (2048.0 / (VERTICES - 1))
    i0 = np.minimum(np.floor(t).astype(int), 2047)
    f = t - i0
    rows = grid[i0, :] * (1 - f)[:, None] + grid[i0 + 1, :] * f[:, None]
    out = rows[:, i0] * (1 - f)[None, :] + rows[:, i0 + 1] * f[None, :]
    values = np.clip(np.rint(out), 0, 65535).astype("<u2")[::-1]   # row 0 = north
    folder = env.content_dir() / "regions" / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "landscape.r16").write_bytes(values.tobytes())
    rx, ry = (int(v) for v in name[2:].split("_"))
    layout = {"region": name, "vertices": VERTICES, "components": COMPONENTS, "quads per component": QUADS,
              "sections per component": 1, "scale cm": [QUAD_CM, QUAD_CM, Z_SCALE_CM],
              # The Landscape origin is its corner with the smallest Unreal X and Y: west, north.
              "location cm": [rx * 204800.0, -(ry + 1) * 204800.0, Z_ORIGIN_CM],
              "height min m": round(float(values.min()) * region.HEIGHT_SCALE, 3),
              "height max m": round(float(values.max()) * region.HEIGHT_SCALE, 3),
              "cookies": cache[name][1], "file": "landscape.r16", "row 0": "north"}
    (folder / "landscape.json").write_text(json.dumps(layout, indent=1))
    return layout


def macro(name: str) -> dict | None:
    """Writes the macro color map of a region (`regions/NAME/macro_diff.png`, 2,048 x 2,048, row 0 = north) and
    @return its manifest record (a texture of the role "macro"), or None when the region has no macro map."""
    rx, ry = (int(v) for v in name[2:].split("_"))
    product = (f"materials/terrain/newworld_vitaeeterna/jav_newworld_vitaeeterna_{rx}_{ry}/"
               f"jav_newworld_vitaeeterna_{rx}.{ry}.macro_diff.dds")
    out = env.content_dir() / "regions" / name / "macro_diff.png"
    if not out.is_file():
        if not source.has(product):
            return None
        data, w, h, fmt = dds.join(source.read, product, 2048)
        out.parent.mkdir(parents=True, exist_ok=True)
        dds.to_image(data, w, h, fmt).convert("RGB").save(out)
    return {"kind": "texture", "file": str(out.relative_to(env.content_dir())), "role": "macro", "cap": 2048}


def level_data(names: list[str]) -> dict:
    """The data of the world level for regions: the Landscape heights, the macro color maps (manifest records), and
    the cell files of the instances. @return A report."""
    from . import cells, manifest
    cache: dict = {}   # the heights of each region (8 MB): the neighbors give the shared edges
    report, records = {}, {}
    for name in names:
        layout = landscape(name, cache)
        record = macro(name)
        if record:
            records["texture:macro:" + name] = record
        report[name] = {"heights m": [layout["height min m"], layout["height max m"]], "cookies": layout["cookies"],
                        "macro": record is not None, "cells": cells.write(name), "impostors": impostors(name)}
    if records:
        manifest.add_many(records)
    return report


def height_m(x: float, y: float, cache: dict) -> float | None:
    """@return The terrain height (m, with the cookies) at an engine position, from regions/NAME/landscape.r16."""
    rx, ry = int(x // 2048), int(y // 2048)
    name = f"r_+{rx:02d}_+{ry:02d}"
    if name not in cache:
        path = env.content_dir() / "regions" / name / "landscape.r16"
        cache[name] = np.frombuffer(path.read_bytes(), dtype="<u2").reshape(VERTICES, VERTICES) if path.is_file() else None
    grid = cache[name]
    if grid is None:
        return None
    column = (x - rx * 2048) / 2048 * (VERTICES - 1)
    row = (VERTICES - 1) - (y - ry * 2048) / 2048 * (VERTICES - 1)   # row 0 = north
    return float(grid[int(round(row)), int(round(column))]) * region.HEIGHT_SCALE


def flythrough_plan(route: list[tuple[float, float]], altitude: float, speed: float, out: Path, frames: str | None = None,
                    every: int = 2) -> dict:
    """Writes the plan of ASwcFlythrough: the route (engine x, y in m) every 25 m, at `altitude` m above the highest
    terrain within 100 m (smoothed), at `speed` m/s. @return The plan."""
    cache: dict = {}
    points = []
    for (x0, y0), (x1, y1) in zip(route, route[1:]):
        steps = max(1, int(np.hypot(x1 - x0, y1 - y0) // 25))
        for i in range(steps):
            points.append([x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps])
    points.append(list(route[-1]))
    ground = []
    for x, y in points:
        around = [height_m(x + dx, y + dy, cache) for dx in (-100, 0, 100) for dy in (-100, 0, 100)]
        ground.append(max([h for h in around if h is not None] or [40.0]))
    z = np.convolve(np.pad(ground, 4, mode="edge"), np.ones(9) / 9, mode="valid") + altitude
    plan = {"points": [[round(x, 1), round(y, 1), round(float(h), 1)] for (x, y), h in zip(points, z)],
            "speed": speed, "look ahead": 80.0, "pitch": 10.0}
    if frames:
        plan["frames"] = {"folder": frames, "every": every}
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(plan))
    return {"plan": str(out), "points": len(points), "length m": round(float(sum(np.hypot(*np.diff(np.array(points), axis=0).T))), 0),
            "seconds": round(float(sum(np.hypot(*np.diff(np.array(points), axis=0).T))) / speed, 0)}


IMPOSTOR_GROUP_M = 512.0


def impostors(name: str) -> dict:
    """Converts the far meshes of the game of a region (`impostors/` and `poi_impostors/`: one mesh for each cell of
    128 m, the vertices local to the cell, the heights absolute) into groups of 512 m (one glTF each; key
    `impostor:NAME:KIND:GX_GY`, origin = the south-west corner of the group at height 0). @return Counts; the
    manifest records are written."""
    from . import manifest, mesh
    folder = region.WORLD + "regions/" + name + "/"
    rx, ry = (int(v) for v in name[2:].split("_"))
    ox, oy = rx * 2048.0, ry * 2048.0
    reports, groups = [], {}
    for kind in ("impostors", "poi_impostors"):
        if not source.has(folder + kind + ".json"):
            continue
        for cell in json.loads(source.read(folder + kind + ".json")).get("impostors", []):
            path = source.product_of_text(cell.get("meshAssetID", ""), ".cgf")
            if path is None:
                continue
            x, y = cell["worldPosition"]["x"], cell["worldPosition"]["y"]
            gx, gy = int((x - ox) // IMPOSTOR_GROUP_M), int((y - oy) // IMPOSTOR_GROUP_M)
            groups.setdefault((kind, gx, gy), []).append((path, (x, y)))
    for (kind, gx, gy), cells in sorted(groups.items()):
        origin = (ox + gx * IMPOSTOR_GROUP_M, oy + gy * IMPOSTOR_GROUP_M)
        parts = [(path, np.array([x - origin[0], y - origin[1], 0.0])) for path, (x, y) in cells]
        key = f"impostor:{name}:{kind}:{gx}_{gy}"
        out = env.content_dir() / "regions" / name / "impostors" / f"{kind}_{gx}_{gy}.gltf"
        try:
            report = mesh.convert_group(key, out, parts)
        except Exception as error:
            reports.append({"mesh": key, "error": str(error)[:120]})
            continue
        report["entry"]["origin m"] = [origin[0], origin[1], 0.0]
        reports.append(report)
    records = mesh.records_of(reports)
    if records:
        manifest.add_many(records)
    return {"region": name, "groups": len(groups), "converted": sum(1 for r in reports if "entry" in r),
            "triangles": sum(r.get("triangles", 0) for r in reports), "errors": [r["error"] for r in reports if "error" in r][:5]}
