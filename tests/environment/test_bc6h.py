"""Tests of the BC6H decoder of the world probes (converter/swc_convert/bc6h.py) with no game data: blocks made by hand from the
bit layout of the format (mode 11: the raw 10-bit endpoints; mode 1: a base and its deltas; a reserved mode), and the half floats of
the unquantization. The decoder was also checked against the 8-bit decoder of Pillow on all ten modes of the world probe (stage E5)."""
import sys
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import bc6h  # noqa: E402


def block(fields: list[tuple[int, int]]) -> bytes:
    """@return 16 bytes from (value, bit count) fields in the stream order (the first field in the lowest bits)."""
    bits, pos = 0, 0
    for value, count in fields:
        bits |= (value & ((1 << count) - 1)) << pos
        pos += count
    assert pos <= 128
    return bits.to_bytes(16, "little")


def half(bits: int) -> float:
    return float(np.array([bits], np.uint16).view(np.float16)[0])


class Bc6h(unittest.TestCase):
    def test_mode_11_endpoints(self):
        # Mode 11 (0b00011): 10-bit endpoints w and x, no transform; then 63 index bits (the anchor 3 bits, the others 4).
        indices = [(0, 3)] + [(15, 4)] * 15
        data = block([(0b00011, 5), (0x200, 10), (0x100, 10), (0x080, 10), (0x3FF, 10), (0x3FF, 10), (0x3FF, 10)] + indices)
        texels = bc6h.decode_block(data)
        # Texel 0: the endpoint w; unquantize(v, 10) = ((v << 16) + 0x8000) >> 10, then (v * 31) >> 6 is the half.
        for channel, value in enumerate((0x200, 0x100, 0x080)):
            expected = half((((value << 16) + 0x8000) >> 10) * 31 >> 6)
            self.assertAlmostEqual(float(texels[0, 0, channel]), expected, places=5)
        # The other texels: index 15, the endpoint x (0x3FF: the largest value, 0xFFFF before the finish).
        self.assertAlmostEqual(float(texels[3, 3, 0]), half(0xFFFF * 31 >> 6), places=2)

    def test_mode_1_deltas(self):
        # Mode 1 (0b00): the base w (10 bits) and the signed 5-bit deltas of x, y, z; partition 0 (the left two columns: region 0).
        fields = [(0b00, 2), (0, 1), (0, 1), (0, 1),            # gy[4], by[4], bz[4]
                  (400, 10), (400, 10), (400, 10),              # rw, gw, bw
                  (0b11111, 5), (0, 1), (0, 4), (0b00001, 5),   # rx = -1, gz[4], gy[3:0], gx = +1
                  (0, 1), (0, 4), (0, 5), (0, 1), (0, 4),       # bz[0], gz[3:0], bx = 0, bz[1], by[3:0]
                  (0, 5), (0, 1), (0, 5), (0, 1), (0, 5)]       # ry, bz[2], rz, bz[3], the partition 0
        data = block(fields + [(0, 2)] + [(0, 3)] * 14 + [(0, 2)])
        texels = bc6h.decode_block(data)
        expected = half((((400 << 16) + 0x8000) >> 10) * 31 >> 6)
        self.assertAlmostEqual(float(texels[0, 0, 1]), expected, places=5)

    def test_reserved_mode(self):
        self.assertTrue((bc6h.decode_block(block([(0b10011, 5)])) == 0).all())

    def test_surface(self):
        data = block([(0b00011, 5)] + [(0x200, 10)] * 6) * 4
        self.assertEqual(bc6h.decode(data, 8, 8).shape, (8, 8, 3))


if __name__ == "__main__":
    unittest.main()
