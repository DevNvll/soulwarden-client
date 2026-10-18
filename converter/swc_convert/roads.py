"""The road maps of the world (stage P3.3): the road weight of the terrain at 1 m for each region.

The road boost of the data (the action Traversal_Road_Sprint_Speed_Boost: IfOnRoad >= 0.1, then the status
Traversal_RoadSprintBoost_Haste, SprintSpeedMod +0.15) needs the road weight at the feet. The status is not client predicted
(IsClientPredicted 0): the server applies it and sends the speed table (key 0x35). The client does not predict it. These maps
are for a backend that applies the boost (decision of the orchestrator for P3.3), and for the debug view of the client.

The source (research p33-movement.md Q3):
  materials/terrain/newworld_vitaeeterna/jav_newworld_vitaeeterna_X_Y/jav_newworld_vitaeeterna_X_Y.regionmat
    Layers: 33 TerrainMaterialLayerData (Material, SplatMap, AffectedTiles, Priority). A road layer has a material in
    materials/terrain/roads/ (roads_splat, mountainroads_splat, coastroads_splat, damnedroads_splat).
  jav_newworld_vitaeeterna_X.Y.LAYER.dds: one channel (DXGI 80, BC4 UNORM), 2048 x 2048 at 1 m, row 0 = north, column 0 = west
    (the orientation of the macro map: the correlation of the road splat of region 4_2 with the red-green value of its macro
    map is +0.32, and 0.04 or less for each flip or turn).

The rule (an inference: the data does not say if IfOnRoad reads the terrain weight or the cover of a road spline):
  weight = min(1, the sum of the splat values of the road layers). The layer priorities and AffectedTiles (the render tiles of
  a layer) are not applied. A layer with AffectedTiles 0 adds nothing (its splat is a 16 x 16 placeholder).

Output (SWC_CONTENT/roads, never in git):
  NAME.r8       2048 x 2048 bytes, row 0 = north (the smallest Unreal Y), column 0 = west; byte = round(255 x weight).
                IfOnRoad >= 0.1 is byte >= 26. The texel of the world point (x, y) m is column floor(x) - 2048 rx,
                row 2047 - (floor(y) - 2048 ry) (the region r_+rx_+ry has x in [2048 rx, 2048 (rx + 1)) m).
  roads.json    the format, the layers, and for each region its file, its layers, and its texel counts.
A region with no road texel has no file.
"""
from __future__ import annotations

import json
import re
import struct

import numpy as np

from . import dds, env, source

SIZE = 2048
ROAD_MATERIALS = "materials/terrain/roads/"
# IfOnRoad >= 0.1 of the action Traversal_Road_Sprint_Speed_Boost: the smallest byte with byte / 255 >= 0.1.
ON_ROAD = 26
LAYER = re.compile(r'field="Material" value="[^"]*hint=\{([^}]*)\}".*?field="SplatMap" value="[^"]*hint=\{([^}]*)\}"'
                   r'.*?field="AffectedTiles" value="(\d+)".*?field="Priority" value="(\d+)"', re.S)


def folder(name: str) -> str:
    rx, ry = (int(v) for v in name[2:].split("_"))
    return f"materials/terrain/newworld_vitaeeterna/jav_newworld_vitaeeterna_{rx}_{ry}/"


def road_layers(name: str) -> list[dict]:
    """@return The road layers of the region material of a region (empty when it has no region material)."""
    rx, ry = (int(v) for v in name[2:].split("_"))
    path = folder(name) + f"jav_newworld_vitaeeterna_{rx}_{ry}.regionmat"
    if not source.has(path):
        return []
    text = source.read(path).decode("utf-8", "replace")
    layers = []
    for material, splat, tiles, priority in LAYER.findall(text):
        if material.lower().startswith(ROAD_MATERIALS):
            layers.append({"material": material.lower(), "splat": splat.lower(), "tiles": int(tiles), "priority": int(priority)})
    return layers


def splat(path: str) -> np.ndarray:
    """@return The values 0..1 of a splat map at SIZE x SIZE (a smaller map is repeated to SIZE)."""
    head = source.read(path)
    fmt = struct.unpack_from("<I", head, 128)[0]
    if fmt not in (80, 81):
        raise ValueError(f"{path}: DXGI format {fmt}, not BC4")
    data, w, h, fmt = dds.join(source.read, path, SIZE)
    values = dds.decode_bc45(data[148:], w, h, fmt)[:, :, 0]
    if (w, h) != (SIZE, SIZE):
        values = np.kron(values, np.ones((SIZE // h, SIZE // w), dtype=np.float32))
    return values


def region_map(name: str) -> tuple[np.ndarray | None, dict]:
    """@return (the road bytes of a region or None, its record for roads.json)."""
    weight = np.zeros((SIZE, SIZE), dtype=np.float32)
    used = []
    for layer in road_layers(name):
        if layer["tiles"] == 0 or not source.has(layer["splat"]):
            continue
        values = splat(layer["splat"])
        used.append({"layer": layer["splat"].rsplit(".", 2)[-2], "priority": layer["priority"],
                     "texels": int((values > 0).sum())})
        weight += values
    data = np.rint(np.clip(weight, 0.0, 1.0) * 255.0).astype(np.uint8)
    on_road = int((data >= ON_ROAD).sum())
    record = {"layers": used, "texels": int((data > 0).sum()), "on road": on_road}
    return (data if record["texels"] else None), record


def convert(names: list[str]) -> dict:
    out = env.content_dir() / "roads"
    out.mkdir(parents=True, exist_ok=True)
    regions = {}
    for name in names:
        data, record = region_map(name)
        if data is not None:
            (out / f"{name}.r8").write_bytes(data.tobytes())
            record["file"] = f"{name}.r8"
        regions[name] = record
    index = {"size": SIZE, "meters per texel": 1, "row 0": "north", "column 0": "west", "value": "round(255 x weight)",
             "weight": "min(1, the sum of the road splat layers)", "on road": f"byte >= {ON_ROAD} (IfOnRoad >= 0.1)",
             "regions": regions}
    (out / "roads.json").write_text(json.dumps(index, indent=1))
    return {"regions": len(regions), "with roads": sum(1 for r in regions.values() if "file" in r),
            "on road km2": round(sum(r["on road"] for r in regions.values()) / 1e6, 2), "folder": str(out)}
