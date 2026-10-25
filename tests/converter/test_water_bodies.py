"""Tests of the water body specs of the Unreal Water plugin with no game data (water_bodies.py): the Hermite spline of a river is
the source Bezier curve, the width is linear by the key, a lake has a flat simple outline, a tilted volume is a custom body."""
import math
import sys
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import water as w  # noqa: E402
from swc_convert import water_bodies as wb  # noqa: E402


def river_record(points, controls, widths, spline_type, matrix):
    bezier = [{"Forward": list(f), "crc:6dcec137": list(b)} for f, b in controls]
    return {"kind": "river", "entity": "TestRiver", "matrix": matrix,
            "shape": {"type": "spline", "fields": {"Configuration": {"Spline Type": spline_type, "Spline": {"element": [
                {"BaseClass1": {"Vertices": {"Vertices": {"element": [list(p) for p in points]}}}, "Bezier Data": {"element": bezier}}]}}}},
            "fields": {"River": {"BaseClass1": {"Width": {"DefaultWidth": 4.0, "WidthAttribute": {"Elements": {"element": widths}}},
                                                "SegmentLength": 2.0, "TileLength": 1.0}, "TileWidth": 1.0}}}


def turned(angle_deg, scale, place):
    a = math.radians(angle_deg)
    c, s = math.cos(a) * scale, math.sin(a) * scale
    return [[c, -s, 0.0, place[0]], [s, c, 0.0, place[1]], [0.0, 0.0, scale, place[2]], [0.0, 0.0, 0.0, 1.0]]


def surface_body(record):
    world, _uv, _triangles, _shape = w.river_surface(record)
    return {"points": world}


class River(unittest.TestCase):
    points = [(0.0, 0.0, 1.0), (20.0, 5.0, 1.5), (35.0, -10.0, 1.0)]
    controls = [((6.0, 4.0, 1.0), (-6.0, -4.0, 1.0)), ((26.0, 8.0, 1.5), (14.0, 2.0, 1.5)), ((40.0, -16.0, 1.0), (31.0, -4.0, 1.0))]

    def test_bezier_matches_the_surface(self):
        record = river_record(self.points, self.controls, [0.0, 2.0, -1.0], w.BEZIER, turned(30.0, 1.5, (100.0, 200.0, 40.0)))
        spec = wb.river_spec(record, None)
        self.assertFalse(spec["linear"])
        result = wb.check_river(spec, surface_body(record))
        self.assertLess(result["center"], 2e-4)  # the specs round to 0.1 mm
        self.assertLess(result["width"], 2e-4)  # the specs round to 0.1 mm
        self.assertAlmostEqual(spec["widths"][1], 6.0 * 1.5, places=6)

    def test_linear_river(self):
        record = river_record(self.points, [], [1.0, 0.0, 3.0], w.LINEAR, turned(-10.0, 1.0, (0.0, 0.0, 0.0)))
        spec = wb.river_spec(record, None)
        self.assertTrue(spec["linear"])
        np.testing.assert_allclose(spec["leave"][0], np.subtract(spec["points"][1], spec["points"][0]), atol=1e-4)
        result = wb.check_river(spec, surface_body(record))
        self.assertLess(result["center"], 2e-4)  # the specs round to 0.1 mm
        self.assertLess(result["width"], 2e-4)  # the specs round to 0.1 mm

    def test_hermite_end_points(self):
        p0, p1 = np.array([0.0, 0.0, 0.0]), np.array([1.0, 2.0, 3.0])
        out = wb.hermite(p0, np.ones(3), p1, np.ones(3), np.array([0.0, 1.0]))
        np.testing.assert_allclose(out, [p0, p1])


class Volume(unittest.TestCase):
    def test_flat_prism_is_a_lake(self):
        world = np.array([[0, 0, 5], [10, 0, 5], [10, 10, 5], [5, 3, 5], [0, 10, 5]], dtype=float)
        spec = wb.volume_spec(world, w.SHAPE_PRISM)
        self.assertEqual(spec["type"], "lake")
        self.assertEqual(len(spec["outline"]), 5)

    def test_cylinder_outline_has_no_center(self):
        rim = [[math.cos(a), math.sin(a), 2.0] for a in np.linspace(0, 2 * math.pi, 8, endpoint=False)]
        spec = wb.volume_spec(np.array([[0.0, 0.0, 2.0]] + rim), w.SHAPE_CYLINDER)
        self.assertEqual(len(spec["outline"]), 8)

    def test_tilted_is_custom(self):
        world = np.array([[0, 0, 5], [10, 0, 5.2], [10, 10, 5.2], [0, 10, 5]], dtype=float)
        self.assertEqual(wb.volume_spec(world, w.SHAPE_BOX)["type"], "custom")

    def test_crossing_outline_is_custom(self):
        world = np.array([[0, 0, 1], [10, 10, 1], [10, 0, 1], [0, 10, 1]], dtype=float)
        spec = wb.volume_spec(world, w.SHAPE_PRISM)
        self.assertEqual(spec["reason"], "outline crosses itself")


if __name__ == "__main__":
    unittest.main()
