"""Tests of C11 (AST-275, converter/swc_convert/cells.py) with no game data: the slot map of a decal companion is the "decal slots"
of the override pair. An override with another decal key gives the companion a slot map; an override with no decal at the companion
slots gives the group no companion; a decal-only mesh (C4) with such an override is not placed; a placement with no override keeps
the companion with no slot map."""
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
from swc_convert import cells, manifest, overrides  # noqa: E402

REGION = "r_+00_+00"
TREE = "objects/nature/veg_trees/oak_dead.cgf"
MOSS = "objects/nature/rock_cliffs/cliffs_crater/cliff_moss.cgf"
FALL = "objects/nature/veg_trees/oak_fall_mat.mtl"        # another decal at the companion slot
FULL = "objects/nature/veg_trees/oak_full_mat.mtl"        # no decal at the companion slot
GREEN = "objects/nature/rock_cliffs/cliffs_mossy/cliff_mossy_mat.mtl"
BARE_ROCK = "objects/nature/rock_cliffs/cliffs_bare_mat.mtl"


def entries(path: Path) -> list[tuple[str, dict, int]]:
    """@return (Unreal folder, slot map, flags) of each entry of a cells.bin file."""
    b = path.read_bytes()
    count = struct.unpack_from("<I", b, 8)[0]
    o, out = 12, []

    def text():
        nonlocal o
        size = struct.unpack_from("<H", b, o)[0]
        o += 2 + size
        return b[o - size:o].decode()
    for _ in range(count):
        folder = text()
        slots = struct.unpack_from("<H", b, o)[0]
        o += 2
        pairs = {}
        for _ in range(slots):
            slot = text()
            pairs[slot] = text()
        out.append((folder, pairs, b[o]))
        o += 1
    return out


class CompanionOverrides(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        root = Path(self.folder.name)
        self.saved = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = str(root)
        assets = {
            TREE: {"kind": "static", "materials": ["m_0000000001"], "version": 7, "decals": "decals:" + TREE},
            "decals:" + TREE: {"kind": "static", "materials": [], "slots": ["dm_00000000aa"], "version": 7, "decals of": TREE},
            MOSS: {"kind": "empty", "version": 7, "decals": "decals:" + MOSS},
            "decals:" + MOSS: {"kind": "static", "materials": [], "slots": ["dm_00000000bb"], "version": 7, "decals of": MOSS},
        }
        (root / "manifest.json").write_text(json.dumps({"format": 2, "assets": assets}))
        overrides.out_dir().mkdir(parents=True, exist_ok=True)
        pairs = {
            overrides.pair_key(TREE, FALL): {"slots": {}, "decal slots": {"dm_00000000aa": "dm_00000000cc"}, "version": 4},
            overrides.pair_key(TREE, FULL): {"slots": {}, "decal slots": {"dm_00000000aa": ""}, "version": 4},
            overrides.pair_key(MOSS, GREEN): {"slots": {}, "decal slots": {"dm_00000000bb": "dm_00000000dd"}, "version": 4},
            overrides.pair_key(MOSS, BARE_ROCK): {"slots": {}, "decal slots": {"dm_00000000bb": ""}, "version": 4},
        }
        (overrides.out_dir() / "pairs.json").write_text(json.dumps(pairs))
        # Placements: the tree with no override, FALL, FULL; the moss with no override, GREEN, BARE_ROCK.
        meshes = [TREE, MOSS]
        rows = [(0, 0), (0, 1), (0, 2), (1, 0), (1, 3), (1, 4)]
        matrix = np.zeros((len(rows), 3, 4))
        matrix[:, :, :3] = np.eye(3)
        matrix[:, :, 3] = [[10.0 + i, 10.0, 50.0] for i in range(len(rows))]
        region = root / "regions" / REGION
        region.mkdir(parents=True)
        np.savez(region / "placements.npz", meshes=np.array(meshes), mesh=np.array([r[0] for r in rows], dtype=np.uint32),
                 source=np.zeros(len(rows), np.uint8), matrix=matrix, sources=np.array(["chunk"]),
                 materials=np.array(["", FALL, FULL, GREEN, BARE_ROCK]),
                 material=np.array([r[1] for r in rows], dtype=np.uint16), wind=np.zeros(len(rows), bool))
        self.root = root

    def tearDown(self):
        if self.saved is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.saved
        self.folder.cleanup()

    def test_companion_slot_maps(self):
        report = cells.write(REGION)
        assets = manifest.load()["assets"]
        found = entries(self.root / "regions" / REGION / "cells.bin")
        tree_companion = assets["decals:" + TREE]["unreal"]
        moss_companion = assets["decals:" + MOSS]["unreal"]
        companions = [(f, s) for f, s, flags in found if flags == 2]
        # The tree: the companion with no slot map (no override) and the companion with the FALL decal; none for FULL.
        self.assertIn((tree_companion, {}), companions)
        self.assertIn((tree_companion, {"dm_00000000aa": "dm_00000000cc"}), companions)
        self.assertEqual(sum(1 for f, _ in companions if f == tree_companion), 2)
        # The moss (C4, the companion alone): no override, GREEN; the BARE_ROCK placement is not placed.
        self.assertIn((moss_companion, {}), companions)
        self.assertIn((moss_companion, {"dm_00000000bb": "dm_00000000dd"}), companions)
        self.assertEqual(sum(1 for f, _ in companions if f == moss_companion), 2)
        self.assertEqual(report["decal-only placements left out: no decal in the override (C11)"], 1)
        self.assertEqual(report["instances with no companion: no decal in the override (C11)"], 1)
        self.assertEqual(report["instances"], 5)
        # The tree groups: one entry for each override with a decal slot change (no override, FALL, FULL).
        self.assertEqual(sum(1 for f, _, flags in found if f == assets[TREE]["unreal"] and flags == 0), 3)


if __name__ == "__main__":
    unittest.main()
