"""Tests of the DDS decoders of the converter (no game data): BC4 and BC5 blocks with known values."""
import json
import struct
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


def dds_file(fourcc: bytes, w: int, h: int, mips: int, data: bytes, fmt: int | None = None) -> bytes:
    """@return A DDS file: a header (with a DX10 part when fourcc is DX10) and the data of all mips."""
    head = bytearray(128)
    head[:4] = b"DDS "
    struct.pack_into("<II", head, 4, 124, 0x1007)   # the header size; caps, height, width, pixel format
    struct.pack_into("<II", head, 12, h, w)
    struct.pack_into("<II", head, 76, 32, 0x4)      # the pixel format size; DDPF_FOURCC
    struct.pack_into("<I", head, 28, mips)
    head[84:88] = fourcc
    if fourcc == b"DX10":
        head += struct.pack("<5I", fmt, 3, 0, 1, 0)
    return bytes(head) + data


class Headers(unittest.TestCase):
    """The legacy header (DXT1, 128 bytes) and the A8 alpha image: two cases of the material audit (stage M0)."""

    def test_legacy_dxt1(self):
        # One 4 x 4 block: color0 = white (0xFFFF), color1 = black, all indices 0: a white block; then 2 x 2 and 1 x 1.
        white = struct.pack("<HHI", 0xFFFF, 0x0000, 0)
        files = {"t.dds": dds_file(b"DXT1", 4, 4, 3, white * 3)}
        data, w, h, fmt = dds.join(files.__getitem__, "t.dds", 1024)
        self.assertEqual((w, h, fmt), (4, 4, 71))
        image = np.asarray(dds.to_image(data, w, h, fmt))
        self.assertEqual(image.shape, (4, 4, 4))
        self.assertTrue((image[:, :, :3] == 255).all())

    def test_legacy_split_mips(self):
        # 8 x 8 with mip 0 in t.dds.1: the base file has only the smaller mips (as the split files of the game).
        block_white, block_black = struct.pack("<HHI", 0xFFFF, 0, 0), struct.pack("<HHI", 0, 0xFFFF, 0)
        files = {"t.dds": dds_file(b"DXT1", 8, 8, 4, block_white * 3), "t.dds.1": block_black * 4}
        data, w, h, fmt = dds.join(files.__getitem__, "t.dds", 1024)
        self.assertEqual((w, h), (8, 8))
        self.assertTrue((np.asarray(dds.to_image(data, w, h, fmt))[:, :, :3] == 0).all())

    def test_legacy_bgra8(self):
        # No four-character code: RGB and alpha flags, 32 bits, the B8G8R8A8 masks (engineassets/textures/grey.dds).
        head = bytearray(dds_file(b"\0\0\0\0", 2, 2, 1, b""))
        struct.pack_into("<7I", head, 80, 0x41, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000)
        pixels = bytes([10, 20, 30, 255]) * 4   # B, G, R, A
        data, w, h, fmt = dds.join({"g.dds": bytes(head) + pixels}.__getitem__, "g.dds", 1024)
        image = np.asarray(dds.to_image(data, w, h, fmt))
        self.assertEqual(fmt, 87)
        self.assertEqual(image[0, 0].tolist(), [30, 20, 10, 255])

    def test_a8_alpha_image(self):
        pixels = bytes(range(16)) + bytes([7, 9, 11, 13]) + bytes([42])
        files = {"n.dds.a": dds_file(b"DX10", 4, 4, 3, pixels, fmt=dds.A8)}
        data, w, h, fmt = dds.join(files.__getitem__, "n.dds", 1024, alpha=True)
        image = np.asarray(dds.to_image(data, w, h, fmt))
        self.assertEqual(image.shape, (4, 4))
        self.assertEqual(image.ravel().tolist(), list(range(16)))


if __name__ == "__main__":
    unittest.main()
