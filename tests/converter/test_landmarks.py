"""Tests of the TexMod rule of the landmarks (AST-209; converter/swc_convert/landmarks.py texmod) with no game data: the rotation of type
2 as degrees per second (1000 x the amplitude), the centre, the tile and the offset of the TexMod element or of the Texture element, and
a slot with no TexMod."""
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import landmarks  # noqa: E402


class TexMod(unittest.TestCase):
    def test_layer_03(self):
        # pm_vortex_l03.mtl, the Diffuse slot: amplitude 0.0025, tile 0.7, offset 0.15 (research/maelstrom.md: 2.5 deg/s).
        t = ET.fromstring('<Texture Map="Diffuse"><TexMod TexMod_RotateType="2" TexMod_URotateCenter="0.5" TexMod_VRotateCenter="0.5" '
                          'TexMod_WRotateAmplitude="0.0025" TileU="0.7" TileV="0.7" OffsetU="0.15" OffsetV="0.15"/></Texture>')
        rule = landmarks.texmod(t)
        self.assertAlmostEqual(rule["degrees per second"], 2.5)
        self.assertEqual(rule["tile"], [0.7, 0.7])
        self.assertEqual(rule["offset"], [0.15, 0.15])
        self.assertEqual(rule["center"], [0.5, 0.5])

    def test_tile_on_the_texture_element_and_no_amplitude(self):
        # The core Diffuse slot: TileU and OffsetU on the TexMod element, rotation type 2 with no amplitude: no rotation.
        t = ET.fromstring('<Texture Map="Diffuse"><TexMod TexMod_RotateType="2" TileU="1.3" OffsetU="0.85" TileV="1.3" '
                          'OffsetV="0.85"/></Texture>')
        rule = landmarks.texmod(t)
        self.assertEqual(rule["degrees per second"], 0.0)
        self.assertEqual(rule["tile"], [1.3, 1.3])
        self.assertEqual(rule["offset"], [0.85, 0.85])

    def test_no_texmod(self):
        rule = landmarks.texmod(ET.fromstring('<Texture Map="Bumpmap"/>'))
        self.assertEqual(rule, {"tile": [1.0, 1.0], "offset": [0.0, 0.0], "center": [0.5, 0.5], "degrees per second": 0.0})

    def test_the_rotation_step(self):
        # 0.0025 rad each 0.05 s: 0.143239 degrees a step, one turn in 125.664 s.
        self.assertAlmostEqual(landmarks.ROTATION["step degrees"], 0.143239, places=6)
        self.assertAlmostEqual(360.0 / landmarks.ROTATION["step degrees"] * landmarks.ROTATION["step seconds"], 125.664, places=2)


if __name__ == "__main__":
    unittest.main()
