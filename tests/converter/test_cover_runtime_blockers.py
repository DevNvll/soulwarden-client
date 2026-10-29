"""Tests of the run-time blockers of the ground cover (C-6, converter/swc_convert/cover_areas.py) with no game data: only a blocker
whose area has bit 2 of PlacementModeTypeFlag claims a slot; a flag-1 blocker was applied in the bake of region.vegetation."""
import sys
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import cover_areas  # noqa: E402


def box(flag):
    return {"kind": "box", "matrix": np.eye(4).tolist(), "dimensions": [20.0, 20.0, 256.0],
            "flags": {"Priority": 1, "AreaType": 0, "PlacementModeTypeFlag": flag, "InheritBehavior": 1, "UseRelativeUVW": 0}}


class Runtime(unittest.TestCase):
    def test_flags(self):
        self.assertFalse(cover_areas.runtime(box(1)))    # the five road-dressing boxes at 6833 4540
        self.assertTrue(cover_areas.runtime(box(2)))     # the roads and houses of the study
        self.assertTrue(cover_areas.runtime(box(3)))
        self.assertFalse(cover_areas.runtime(box(None)))  # no area component

    def test_switch(self):
        saved = cover_areas.RUNTIME_ONLY
        cover_areas.RUNTIME_ONLY = False
        try:
            self.assertTrue(cover_areas.runtime(box(1)))
        finally:
            cover_areas.RUNTIME_ONLY = saved

    def test_box_test(self):
        # the box of the blocker: the dimensions are the full size (the half size each side of the origin)
        points = np.array([[9.9, 0.0, 0.0], [10.1, 0.0, 0.0], [0.0, -9.9, 100.0]])
        self.assertEqual(cover_areas.inside(box(2), points).tolist(), [True, False, True])


if __name__ == "__main__":
    unittest.main()
