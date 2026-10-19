#!/usr/bin/env python3
"""The color report of the water part of matched views (docs/WATER.md, "The water color check").

Usage: python3 -B tools/lib/water_part.py LABEL=FOLDER[,FOLDER] ... [--views V1,V2] [--save DIR]

FOLDER: an output folder of tools/match-view.sh (plan.json, client/VIEW.png, colors.json). A pixel is water when its view ray
(the camera, the forward vector, and the vertical field of view of the view JSON) meets the water surface before the terrain:
the surface of the converted quadtree (SWC_CONTENT/water/REGION.waterq; where the quadtree has no water, the ocean at 40 m, the
rule of the client) and the heights of the Landscape (SWC_CONTENT/regions/REGION/landscape.r16). The ray steps along 1,200
distances from 0.3 m to 3 km. The character box of tools/lib/color_report.py is masked out of both pictures; objects in front of
the water (a bridge, a rock) stay in the mask of both. For each view and label: the client and retail statistics of the water
pixels, the CIEDE2000 distance of the two mean colors, and the difference of L* of the water minus the difference of L* of the
whole picture (colors.json "all"): the water against the light of the whole picture. Then the means of each label.
--save DIR writes each mask as a PNG picture (white: water).
"""
from __future__ import annotations

import json
import math
import os
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import color_report  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "converter"))
from swc_convert import region, water  # noqa: E402

CONTENT = Path(os.environ.get("SWC_CONTENT") or Path.home() / "Games/soulwarden-client/content")
REFERENCES = Path.home() / "Games/soulwarden/work/evidence/unreal-references"
WIDTH, HEIGHT = 1280, 720
OCEAN_LEVEL = 40.0
DISTANCES = np.geomspace(0.3, 3000.0, 1200)


def water_surface(name: str) -> np.ndarray | None:
    """@return The water surface of a region (2048 x 2048, 1 m; row = local y, column = local x; NaN: no water)."""
    path = CONTENT / "water" / f"{name}.waterq"
    if not path.is_file():
        return None
    data = path.read_bytes()
    if data[:4] != b"SWQT":
        raise ValueError(f"{path}: not a quadtree file")
    _, size, count = struct.unpack_from("<III", data, 4)
    nodes = np.frombuffer(data, dtype=[("h", "<f4"), ("f", "<f4"), ("g", "<u4")], count=count, offset=16)
    split = nodes["h"] == np.float32(water.SUBDIVIDED)
    splits = np.cumsum(split)
    out = np.full((size, size), np.nan, dtype=np.float32)
    stack = [(0, 0, 0, size)]
    while stack:
        position, x0, y0, edge = stack.pop()
        h, f, _ = nodes[position]
        if split[position]:
            # The level order: the children of the k-th split node are 1 + 4k to 4 + 4k.
            k, half = int(splits[position]) - 1, edge // 2
            for child in range(4):
                stack.append((1 + 4 * k + child, x0 + half * (child & 1), y0 + half * (child >> 1), half))
        elif h != 0.0 or f != 0.0:
            out[y0:y0 + max(edge, 1), x0:x0 + max(edge, 1)] = h
    return out


class World:
    """The water surfaces and the terrain heights of the regions, read when a ray reaches them."""

    def __init__(self):
        self.surfaces: dict[str, np.ndarray | None] = {}
        self.terrain: dict[str, np.ndarray | None] = {}

    def load(self, name: str):
        if name not in self.surfaces:
            self.surfaces[name] = water_surface(name)
            path = CONTENT / "regions" / name / "landscape.r16"
            layout = CONTENT / "regions" / name / "landscape.json"
            if path.is_file() and layout.is_file():
                vertices = json.loads(layout.read_text())["vertices"]
                grid = np.frombuffer(path.read_bytes(), dtype="<u2").reshape(vertices, vertices)
                self.terrain[name] = grid.astype(np.float32) * np.float32(region.HEIGHT_SCALE)   # row 0 = north
            else:
                self.terrain[name] = None
        return self.surfaces[name], self.terrain[name]

    def sample(self, x: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        """@return (the water surface, the terrain height) at engine points (m; the terrain is NaN outside the regions)."""
        surface = np.full(x.shape, np.nan, dtype=np.float32)
        ground = np.full(x.shape, np.nan, dtype=np.float32)
        rx, ry = np.floor(x / 2048).astype(int), np.floor(y / 2048).astype(int)
        for key in set(zip(rx.tolist(), ry.tolist())):
            chosen = (rx == key[0]) & (ry == key[1])
            water_grid, terrain_grid = self.load(f"r_+{key[0]:02d}_+{key[1]:02d}")
            lx, ly = x[chosen] - key[0] * 2048, y[chosen] - key[1] * 2048
            if water_grid is not None:
                surface[chosen] = water_grid[np.clip(ly.astype(int), 0, 2047), np.clip(lx.astype(int), 0, 2047)]
            if terrain_grid is not None:
                last = terrain_grid.shape[0] - 1
                column = np.clip(np.rint(lx / 2048 * last).astype(int), 0, last)
                row = np.clip(np.rint(last - ly / 2048 * last).astype(int), 0, last)
                ground[chosen] = terrain_grid[row, column]
        # The ocean rule of the client (swc::water::with_ocean): where the quadtree has no water, the ocean level counts.
        surface = np.where(np.isnan(surface), np.float32(OCEAN_LEVEL), surface)
        return surface, ground


def water_mask(view: dict, world: World, step: int = 2) -> np.ndarray:
    """@return The water pixels (HEIGHT x WIDTH) of a view JSON; one ray for each STEP x STEP pixels."""
    camera = view["camera"]
    eye = np.array([camera["x"], camera["y"], camera["z"]], dtype=np.float64)
    forward = np.array(camera.get("forward_vector") or [0.0, 1.0, 0.0], dtype=np.float64)
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, [0.0, 0.0, 1.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    focal = (HEIGHT / 2) / math.tan(math.radians((camera.get("fov") or {}).get("vertical_deg", 57.0)) / 2)
    ys, xs = np.mgrid[0:HEIGHT:step, 0:WIDTH:step]
    rays = (forward[None, None, :] + right[None, None, :] * ((xs - WIDTH / 2) / focal)[..., None]
            + up[None, None, :] * ((HEIGHT / 2 - ys) / focal)[..., None])
    rays = (rays / np.linalg.norm(rays, axis=2, keepdims=True)).reshape(-1, 3)
    state = np.zeros(len(rays), dtype=np.int8)   # 0 open, 1 water, 2 ground
    for distance in DISTANCES:
        open_rays = np.flatnonzero(state == 0)
        if not len(open_rays):
            break
        points = eye[None, :] + rays[open_rays] * distance
        surface, ground = world.sample(points[:, 0], points[:, 1])
        below_ground = ~np.isnan(ground) & (points[:, 2] <= ground)
        below_water = (points[:, 2] <= surface) & ~(below_ground & (ground >= surface))
        state[open_rays[below_water]] = 1
        state[open_rays[below_ground & ~below_water]] = 2
    mask = (state == 1).reshape(ys.shape)
    return np.repeat(np.repeat(mask, step, axis=0), step, axis=1)[:HEIGHT, :WIDTH]


def retail_picture(view_path: Path, view: dict) -> Path | None:
    near = view_path.parent / view.get("file", f"{view_path.stem}.png")
    if near.is_file():
        return near
    found = sorted(REFERENCES.glob(f"*/{view.get('file', view_path.stem + '.png')}"))
    return found[0] if found else None


def main(argv: list[str]) -> int:
    args = list(argv[1:])
    only, save = None, None
    if "--views" in args:
        i = args.index("--views")
        only = set(args[i + 1].split(","))
        del args[i:i + 2]
    if "--save" in args:
        i = args.index("--save")
        save = Path(args[i + 1])
        del args[i:i + 2]
        save.mkdir(parents=True, exist_ok=True)
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    world, masks, totals = World(), {}, {}
    for argument in args:
        label, folders = argument.split("=", 1)
        totals[label] = {"dE": [], "relative": []}
        for folder in (Path(os.path.expanduser(f)) for f in folders.split(",")):
            plan_path = folder / "plan.json"
            if not plan_path.is_file():
                print(f"{label}: no plan.json in {folder}", file=sys.stderr)
                continue
            whole = json.loads((folder / "colors.json").read_text()) if (folder / "colors.json").is_file() else {}
            for entry in json.loads(plan_path.read_text())["views"]:
                name = entry["name"]
                client = folder / "client" / f"{name}.png"
                if (only and name not in only) or not client.is_file():
                    continue
                view_path = Path(entry["json"])
                view = json.loads(view_path.read_text())
                retail = retail_picture(view_path, view)
                if retail is None:
                    print(f"{label} {name}: no retail picture", file=sys.stderr)
                    continue
                if name not in masks:
                    masks[name] = water_mask(view, world) & color_report.character_mask(view, WIDTH, HEIGHT)
                    if save:
                        Image.fromarray((masks[name] * 255).astype(np.uint8)).save(save / f"{name}-water.png")
                a = color_report.pixel_values(np.asarray(Image.open(client).convert("RGB").resize((WIDTH, HEIGHT))))
                b = color_report.pixel_values(np.asarray(Image.open(retail).convert("RGB").resize((WIDTH, HEIGHT))))
                c, r = color_report.region_stats(a, masks[name]), color_report.region_stats(b, masks[name])
                if not c.get("pixels"):
                    print(f"{label:12s} {name:22s} no water pixels")
                    continue
                distance = color_report.ciede2000(c["lab mean"], r["lab mean"])
                all_l = ((whole.get(name, {}).get("all") or {}).get("difference") or {}).get("L mean")
                relative = (c["L mean"] - r["L mean"]) - all_l if all_l is not None else float("nan")
                totals[label]["dE"].append(distance)
                totals[label]["relative"].append(relative)
                print(f"{label:12s} {name:22s} px {c['pixels']:6d} | client L {c['L mean']:5.1f} a {c['lab mean'][1]:+5.1f} "
                      f"b {c['lab mean'][2]:+5.1f} S {c['saturation mean']:.3f} | retail L {r['L mean']:5.1f} a {r['lab mean'][1]:+5.1f} "
                      f"b {r['lab mean'][2]:+5.1f} S {r['saturation mean']:.3f} | dE {distance:5.1f} | water dL - all dL {relative:+5.1f}")
    for label, values in totals.items():
        if values["dE"]:
            relative = [v for v in values["relative"] if not math.isnan(v)]
            print(f"{label}: mean water dE {sum(values['dE']) / len(values['dE']):.1f} over {len(values['dE'])} views"
                  + (f", mean water dL - all dL {sum(relative) / len(relative):+.1f}" if relative else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
