"""Tests of the color report of the matched views (tools/lib/color_report.py) with no game data: CIEDE2000 against the test pairs of
Sharma, Wu, and Dalal (2005), the CIELAB conversion, the projection of the character box, and the statistics of a picture."""
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "lib"))
import color_report  # noqa: E402


class DeltaE(unittest.TestCase):
    PAIRS = [((50.0, 2.6772, -79.7751), (50.0, 0.0, -82.7485), 2.0425),
             ((50.0, 0.0, 0.0), (50.0, -1.0, 2.0), 2.3669),
             ((50.0, 2.5, 0.0), (73.0, 25.0, -18.0), 27.1492),
             ((60.2574, -34.0099, 36.2677), (60.4626, -34.1751, 39.4387), 1.2644),
             ((22.7233, 20.0904, -46.6940), (23.0331, 14.9730, -42.5619), 2.0373),
             ((2.0776, 0.0795, -1.1350), (0.9033, -0.0636, -0.5514), 0.9082)]

    def test_sharma_pairs(self):
        for a, b, expected in self.PAIRS:
            self.assertAlmostEqual(color_report.ciede2000(a, b), expected, places=4, msg=f"{a} {b}")
            self.assertAlmostEqual(color_report.ciede2000(b, a), expected, places=4, msg="symmetric")

    def test_lab_of_white_black_and_gray(self):
        lab = color_report.rgb_to_lab(np.array([[255, 255, 255], [0, 0, 0], [119, 119, 119]]))
        self.assertAlmostEqual(lab[0, 0], 100.0, places=2)
        self.assertAlmostEqual(lab[1, 0], 0.0, places=6)
        self.assertAlmostEqual(lab[2, 0], 50.0, delta=0.3)
        self.assertTrue(np.allclose(lab[:, 1:], 0.0, atol=0.01), "a gray has no a and b")


class Mask(unittest.TestCase):
    VIEW = {"feet": {"x": 0.0, "y": 4.0, "z": 0.0},
            "camera": {"x": 0.0, "y": 0.0, "z": 1.7, "forward_vector": [0.0, 1.0, 0.0], "fov": {"vertical_deg": 57.0}}}

    def test_a_point_ahead_is_at_the_center(self):
        x, y = color_report.project([0.0, 10.0, 1.7], [0.0, 0.0, 1.7], [0.0, 1.0, 0.0], 57.0, 1280, 720)
        self.assertAlmostEqual(x, 640.0)
        self.assertAlmostEqual(y, 360.0)
        x, _ = color_report.project([1.0, 10.0, 1.7], [0.0, 0.0, 1.7], [0.0, 1.0, 0.0], 57.0, 1280, 720)
        self.assertGreater(x, 640.0, "east is to the right for a camera to the north")
        self.assertIsNone(color_report.project([0.0, -1.0, 1.7], [0.0, 0.0, 1.7], [0.0, 1.0, 0.0], 57.0, 1280, 720))

    def test_the_character_box_is_masked(self):
        keep = color_report.character_mask(self.VIEW, 1280, 720)
        self.assertFalse(keep[400, 640], "the character at the center bottom")
        self.assertTrue(keep[100, 100], "the corner stays")
        self.assertTrue(color_report.character_mask({}, 1280, 720).all(), "no feet: no mask")


class Statistics(unittest.TestCase):
    def test_the_same_picture_has_no_difference(self):
        with tempfile.TemporaryDirectory() as folder:
            data = np.zeros((720, 1280, 3), dtype=np.uint8)
            data[:240] = (90, 120, 200)
            data[240:480] = (40, 140, 40)
            data[480:] = (120, 90, 60)
            path = Path(folder) / "a.png"
            Image.fromarray(data).save(path)
            colors = color_report.compare(path, path, {})
            for band in color_report.BANDS:
                self.assertAlmostEqual(colors[band]["difference"]["delta E 2000"], 0.0)
            self.assertAlmostEqual(colors["top"]["client"]["hue mean"], 223.6, delta=0.1)
            self.assertLess(colors["top"]["client"]["L spread"], 1e-6)

    def test_a_washed_picture_has_less_saturation_and_contrast(self):
        with tempfile.TemporaryDirectory() as folder:
            data = np.zeros((720, 1280, 3), dtype=np.uint8)
            data[:, ::2] = (20, 120, 30)
            data[:, 1::2] = (2, 12, 3)
            washed = (data.astype(np.float64) * 0.4 + 140).astype(np.uint8)
            Image.fromarray(data).save(Path(folder) / "retail.png")
            Image.fromarray(washed).save(Path(folder) / "client.png")
            d = color_report.compare(Path(folder) / "client.png", Path(folder) / "retail.png", {})["all"]["difference"]
            self.assertLess(d["saturation mean"], -0.3)
            self.assertLess(d["L spread"], 0.0)
            self.assertGreater(d["L mean"], 0.0)
            self.assertLess(d["dark part"], 0.0)
            line = color_report.summary("fen", color_report.compare(Path(folder) / "client.png", Path(folder) / "retail.png", {}))
            self.assertTrue(line.startswith("fen: all dE"))


if __name__ == "__main__":
    unittest.main()
