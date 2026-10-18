#!/usr/bin/env python3
"""Scratch: a minimal glTF 2.0 writer (JSON + one .bin, PNG textures by URI)."""
from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np

FLOAT, USHORT, UINT = 5126, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


def z_up_to_y_up(v: np.ndarray) -> np.ndarray:
    """Engine space (X right, Y forward, Z up) to glTF space (Y up): (x, y, z) -> (x, z, -y)."""
    out = v.copy()
    out[..., 1] = v[..., 2]
    out[..., 2] = -v[..., 1]
    return out


def matrix_z_up_to_y_up(m: np.ndarray) -> np.ndarray:
    c = np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]], dtype=np.float64)
    return c @ m @ np.linalg.inv(c)


class Writer:
    def __init__(self) -> None:
        self.doc = {"asset": {"version": "2.0", "generator": "soulwarden scratch reader (research)"},
                    "buffers": [], "bufferViews": [], "accessors": [], "meshes": [], "nodes": [],
                    "scenes": [{"nodes": []}], "scene": 0, "materials": [], "textures": [], "images": [],
                    "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]}
        self.bin = bytearray()
        self.image_index: dict[str, int] = {}

    def _view(self, data: bytes, target: int | None) -> int:
        while len(self.bin) % 4:
            self.bin.append(0)
        view = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.bin += data
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, array: np.ndarray, kind: str, target: int | None = ARRAY_BUFFER, minmax: bool = False) -> int:
        if array.dtype == np.float32 or array.dtype == np.float64:
            array = array.astype(np.float32); comp = FLOAT
        elif array.dtype == np.uint16:
            comp = USHORT
        else:
            array = array.astype(np.uint32); comp = UINT
        view = self._view(np.ascontiguousarray(array).tobytes(), target)
        acc = {"bufferView": view, "componentType": comp, "count": int(array.shape[0]), "type": kind}
        if minmax:
            flat = array.reshape(array.shape[0], -1)
            acc["min"] = [float(x) for x in flat.min(0)]
            acc["max"] = [float(x) for x in flat.max(0)]
        self.doc["accessors"].append(acc)
        return len(self.doc["accessors"]) - 1

    def image(self, uri: str) -> int:
        if uri not in self.image_index:
            self.doc["images"].append({"uri": uri})
            self.doc["textures"].append({"sampler": 0, "source": len(self.doc["images"]) - 1})
            self.image_index[uri] = len(self.doc["textures"]) - 1
        return self.image_index[uri]

    def material(self, name: str, base: str | None = None, normal: str | None = None, mr: str | None = None,
                 factor=(1, 1, 1, 1), double: bool = False, alpha_mask: float | None = None, blend: bool = False) -> int:
        pbr = {"baseColorFactor": list(factor), "metallicFactor": 0.0, "roughnessFactor": 0.8}
        if base:
            pbr["baseColorTexture"] = {"index": self.image(base)}
        if mr:
            pbr["metallicRoughnessTexture"] = {"index": self.image(mr)}
            pbr["roughnessFactor"] = 1.0
        mat = {"name": name, "pbrMetallicRoughness": pbr, "doubleSided": double}
        if normal:
            mat["normalTexture"] = {"index": self.image(normal)}
        if alpha_mask is not None:
            mat["alphaMode"] = "MASK"; mat["alphaCutoff"] = alpha_mask
        if blend:
            mat["alphaMode"] = "BLEND"
        self.doc["materials"].append(mat)
        return len(self.doc["materials"]) - 1

    def mesh(self, name: str, primitives: list[dict]) -> int:
        self.doc["meshes"].append({"name": name, "primitives": primitives})
        return len(self.doc["meshes"]) - 1

    def primitive(self, positions, indices, normals=None, uvs=None, tangents=None, joints=None, weights=None,
                  material: int | None = None, colors=None) -> dict:
        attrs = {"POSITION": self.accessor(positions.astype(np.float32), "VEC3", minmax=True)}
        if normals is not None:
            attrs["NORMAL"] = self.accessor(normals.astype(np.float32), "VEC3")
        if tangents is not None:
            attrs["TANGENT"] = self.accessor(tangents.astype(np.float32), "VEC4")
        if uvs is not None:
            attrs["TEXCOORD_0"] = self.accessor(uvs.astype(np.float32), "VEC2")
        if colors is not None:   # (n, 4) float RGBA, linear (glTF COLOR_0)
            attrs["COLOR_0"] = self.accessor(colors.astype(np.float32), "VEC4")
        if joints is not None:
            attrs["JOINTS_0"] = self.accessor(joints.astype(np.uint16), "VEC4")
            attrs["WEIGHTS_0"] = self.accessor(weights.astype(np.float32), "VEC4")
        p = {"attributes": attrs, "indices": self.accessor(indices.astype(np.uint32), "SCALAR", ELEMENT_ARRAY_BUFFER)}
        if material is not None:
            p["material"] = material
        return p

    def node(self, **fields) -> int:
        self.doc["nodes"].append({k: v for k, v in fields.items() if v is not None})
        return len(self.doc["nodes"]) - 1

    def save(self, path: Path) -> None:
        path = Path(path)
        bin_name = path.with_suffix(".bin").name
        self.doc["buffers"] = [{"uri": bin_name, "byteLength": len(self.bin)}]
        for key in ("materials", "textures", "images"):
            if not self.doc[key]:
                del self.doc[key]
        if "textures" not in self.doc:
            self.doc.pop("samplers", None)
        path.with_suffix(".bin").write_bytes(bytes(self.bin))
        path.write_text(json.dumps(self.doc, indent=1))
