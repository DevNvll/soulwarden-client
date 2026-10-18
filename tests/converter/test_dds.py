"""Tests of the DDS decoders of the converter (no game data): BC4 and BC5 blocks with known values."""
import json
import sys
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import dds  # noqa: E402

# The four synthetic blocks of the terrain research (research/terrain/codec-check.json): end values, one index for all
# 16 texels, and the value of the Microsoft rule.
BLOCKS = [((140, 70), 2, 130.0), ((10, 110), 2, 30.0), ((140, 70), 7, 80.0), ((10, 110), 5, 90.0)]


def block(a0: int, a1: int, index: int) -> np.ndarray:
    bits = sum(index << (i * 3) for i in range(16))
    return np.array([[a0, a1, *bits.to_bytes(6, "little")]], np.uint8)


class Bc4(unittest.TestCase):
    def test_research_blocks(self):
        for (a0, a1), index, expected in BLOCKS:
            with self.subTest(a0=a0, a1=a1, index=index):
                self.assertAlmostEqual(float(dds.bc4_blocks(block(a0, a1, index), False)[0, 0]), expected, places=4)

    def test_all_indices_eight_values(self):
        a0, a1 = 200, 60
        values = [float(dds.bc4_blocks(block(a0, a1, i), False)[0, 0]) for i in range(8)]
        expected = [a0, a1] + [((7 - i) * a0 + i * a1) / 7 for i in range(1, 7)]
        np.testing.assert_allclose(values, expected, atol=1e-4)

    def test_all_indices_six_values(self):
        a0, a1 = 60, 200
        values = [float(dds.bc4_blocks(block(a0, a1, i), False)[0, 0]) for i in range(8)]
        expected = [a0, a1] + [((5 - i) * a0 + i * a1) / 5 for i in range(1, 5)] + [0.0, 255.0]
        np.testing.assert_allclose(values, expected, atol=1e-4)

    def test_signed(self):
        # BC5 SNORM normals: signed end values, the same rule; the 6-value mode ends at -127 and 127.
        a = block(100, 0, 3)
        a[0, 1] = np.uint8(256 - 100)   # a1 = -100
        self.assertAlmostEqual(float(dds.bc4_blocks(a, True)[0, 0]), (5 * 100 + 2 * -100) / 7, places=4)

    def test_research_file(self):
        path = Path.home() / "Games/soulwarden-client/research/terrain/codec-check.json"
        if not path.is_file():
            self.skipTest("no research file")
        for row in json.loads(path.read_text()):
            (a0, a1), index = row["endpoints"], row["index"]
            self.assertAlmostEqual(float(dds.bc4_blocks(block(a0, a1, index), False)[0, 0]), row["expected"], places=4)


if __name__ == "__main__":
    unittest.main()
