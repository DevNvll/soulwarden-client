"""Tests of the cell rules of AST-275 (converter/swc_convert/cells.py) with no game data: a mesh with only decal subsets places its
decal companion alone; a mesh with no material of its own and no override slot map is left out (it would draw the fallback
material); the fog cards (fx_fogcard_*) stay out, the other fx_ meshes (the waterfall kit) are placed."""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import cells, materials, overrides  # noqa: E402

REGION = "r_+00_+00"
SURFACE = "objects/props/crate.cgf"
MOSS = "objects/nature/rock_cliffs/cliffs_crater/cliff_moss.cgf"
BARE = "objects/props/ironbars.cgf"
FOG_CARD = "objects/vfx/mesh/fx_fogcard_01.cgf"
WATERFALL = "objects/vfx/mesh/fx_waterfallkit01.cgf"


def entries(path: Path) -> list[tuple[str, int]]:
    """@return (Unreal folder, flags) of each entry of a cells.bin file."""
    b = path.read_bytes()
    count = struct.unpack_from("<I", b, 8)[0]
    o, out = 12, []
    for _ in range(count):
        size = struct.unpack_from("<H", b, o)[0]; o += 2
        folder = b[o:o + size].decode(); o += size
        slots = struct.unpack_from("<H", b, o)[0]; o += 2
        for _ in range(2 * slots):
            size = struct.unpack_from("<H", b, o)[0]; o += 2 + size
        out.append((folder, b[o])); o += 1
    return out


class CoverageCells(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        root = Path(self.folder.name)
        self.saved = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = str(root)
        no_material = materials.shared_material(None)[0]["name"]
        assets = {
            SURFACE: {"kind": "static", "materials": ["m_0000000001"], "version": 7},
            MOSS: {"kind": "empty", "version": 7, "decals": "decals:" + MOSS},
            "decals:" + MOSS: {"kind": "static", "materials": [], "version": 7, "decals of": MOSS},
            BARE: {"kind": "static", "materials": [no_material], "version": 6},
            FOG_CARD: {"kind": "static", "materials": [no_material], "version": 8},
            WATERFALL: {"kind": "static", "materials": ["m_0000000002"], "version": 8},
        }
        (root / "manifest.json").write_text(json.dumps({"format": 2, "assets": assets}))
        overrides.out_dir().mkdir(parents=True, exist_ok=True)
        (overrides.out_dir() / "pairs.json").write_text("{}")
        meshes = [SURFACE, MOSS, BARE, FOG_CARD, WATERFALL]
        matrix = np.zeros((len(meshes), 3, 4))
        matrix[:, :, :3] = np.eye(3)
        matrix[:, :, 3] = [[10.0 + i, 10.0, 50.0] for i in range(len(meshes))]
        region = root / "regions" / REGION
        region.mkdir(parents=True)
        np.savez(region / "placements.npz", meshes=np.array(meshes), mesh=np.arange(len(meshes), dtype=np.uint32),
                 source=np.zeros(len(meshes), np.uint8), matrix=matrix, sources=np.array(["chunk"]),
                 materials=np.array([""]), material=np.zeros(len(meshes), np.uint16), wind=np.zeros(len(meshes), bool))
        self.root = root

    def tearDown(self):
        if self.saved is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.saved
        self.folder.cleanup()

    def test_the_cell_entries(self):
        report = cells.write(REGION)   # C4 on (PLACE_DECAL_ONLY True since the C4 + C11 + C6 deploy)
        from swc_convert import manifest
        assets = manifest.load()["assets"]
        found = entries(self.root / "regions" / REGION / "cells.bin")
        self.assertIn((assets[SURFACE]["unreal"], 0), found)
        self.assertIn((assets[WATERFALL]["unreal"], 0), found)
        self.assertIn((assets["decals:" + MOSS]["unreal"], 2), found)   # the companion alone, with the decal flag
        folders = {f for f, _ in found}
        self.assertNotIn(assets[BARE]["unreal"], folders)
        self.assertNotIn(assets[FOG_CARD]["unreal"], folders)
        self.assertEqual(report["instances"], 3)
        self.assertEqual(report["decal-only meshes"], 1)


    def test_decal_only_held_out(self):
        cells.PLACE_DECAL_ONLY = False   # the switch still holds C4 out
        try:
            report = cells.write(REGION)
        finally:
            cells.PLACE_DECAL_ONLY = True
        from swc_convert import manifest
        assets = manifest.load()["assets"]
        folders = {f for f, _ in entries(self.root / "regions" / REGION / "cells.bin")}
        self.assertNotIn(assets["decals:" + MOSS]["unreal"], folders)
        self.assertEqual(report["decal-only meshes"], 0)


if __name__ == "__main__":
    unittest.main()
