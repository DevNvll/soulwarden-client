"""Tests of the Hair rule of the bodies (converter/swc_convert/material_records.hair_spec, research hair-shader.md) with a
synthetic record (no game data): the specular map with no %SPECULAR_MAP, the gloss clamp, the direction map, the coverage
controls, the variants, and no surface F0 split."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))

from swc_convert import material_records as mr  # noqa: E402


def record(flags, shininess=255.0, detail=False):
    textures = {"Diffuse": {"product": "a/diff.dds", "role": "diffuse"},
                "Specular": {"product": "a/spec.dds", "role": None},
                "Custom": {"product": "a/mask.dds", "role": "overlay mask"}}
    if detail:
        textures["Detail"] = {"product": "a/dir.dds", "role": None}
    return {"flags": flags, "lighting": {"Diffuse": [0.5, 0.5, 0.5], "Specular": [1.0, 1.0, 1.0], "Opacity": 0.99,
                                         "AlphaTest": 0.2, "Shininess": shininess},
            "params": {"SecondaryHighlightColor": [0.7, 0.6, 0.5, 1.0], "SecondaryHighlightWidth": 2.81,
                       "SecondaryHighlightShiftAmount": 0.05, "DiffuseWrap": 0.5, "AlphaBlendMultiplier": 1.0,
                       "Mask_A_Gloss": 1.0, "Mask_R": 0.5},
            "textures": textures}


class HairSpecTest(unittest.TestCase):
    def setUp(self):
        # The texture writers need the game data: record the request only.
        self.saved = mr.color_texture, mr.data_texture
        mr.color_texture = lambda product, role: {"asset": f"texture:{product}"}
        mr.data_texture = lambda product, decode=False: {"asset": f"texture:{product}@data"}

    def tearDown(self):
        mr.color_texture, mr.data_texture = self.saved

    def spec(self, flags, **kwargs):
        rules = {"textures": {}}
        return mr.hair_spec(record(flags, **kwargs), rules, lambda t: t["asset"])

    def test_specular_map_without_flag(self):
        spec = self.spec(["%HAIR_PASS", "%ANISO_SPECULAR"])
        self.assertEqual(spec["parent"], "hair")
        self.assertEqual(spec["textures"]["Specular"], "texture:a/spec.dds")
        self.assertEqual(spec["vectors"]["SpecularColor"], [1.0, 1.0, 1.0, 1.0])
        self.assertTrue(spec["two sided"])

    def test_gloss_clamp_and_roughness(self):
        high = self.spec(["%HAIR_PASS"], shininess=255.0)["scalars"]
        low = self.spec(["%HAIR_PASS"], shininess=10.0)["scalars"]
        self.assertAlmostEqual(high["Gloss"], 0.9)
        self.assertAlmostEqual(low["Gloss"], 0.5)
        self.assertAlmostEqual(high["HairRoughness"], (4.0 / 1024.0) ** 0.25)
        self.assertAlmostEqual(low["HairRoughness"], (4.0 / 64.0) ** 0.25)

    def test_source_parameters(self):
        spec = self.spec(["%HAIR_PASS"])
        self.assertEqual(spec["vectors"]["SecondaryColor"], [0.7, 0.6, 0.5, 1.0])
        self.assertAlmostEqual(spec["scalars"]["SecondaryWidth"], 2.81)
        self.assertAlmostEqual(spec["scalars"]["SecondaryShift"], 0.05)
        self.assertAlmostEqual(spec["scalars"]["AlphaTest"], 0.2)
        self.assertAlmostEqual(spec["scalars"]["Opacity"], 0.99)
        self.assertNotIn("Gloss", spec["textures"])

    def test_direction_map(self):
        without = self.spec(["%HAIR_PASS"], detail=True)
        with_map = self.spec(["%HAIR_PASS", "%DIRECTION_MAP"], detail=True)
        self.assertNotIn("Direction", without["textures"])
        self.assertNotIn("switches", without)
        self.assertEqual(with_map["textures"]["Direction"], "texture:a/dir.dds@data")
        self.assertTrue(with_map["switches"]["DirectionMap"])

    def test_variants_and_masks(self):
        self.assertEqual(self.spec(["%HAIR_PASS"])["scalars"]["BlendEdge"], 1.0)
        self.assertEqual(self.spec(["%HAIR_PASS", "%SOLID_HAIR"])["scalars"]["BlendEdge"], 0.0)
        masked = self.spec(["%HAIR_PASS", "%OVERLAY_MASK"])
        # Mask A gloss does not change the Hair lobes: its amount is zero.
        self.assertEqual(masked["vectors"]["MaskAmount"], [0.5, 0.0, 0.0, 0.0])
        self.assertEqual(masked["scalars"]["MaskMode"], 1.0)


if __name__ == "__main__":
    unittest.main()
