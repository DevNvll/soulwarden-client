"""Tests of the C8 specs (AST-275, converter/swc_convert/material_records.py) with synthetic records, no game data: the rules of the
effect shaders go to rules["fx"], and the render key (the name of the n_ instances and of the mesh slots) does not change; the
Fxmeshadvancedtransp spec chooses the blend by Opacity and the additive bit; the Meshparticle spec gives the type-2 movement of the
Custom slot (Tile x (UV0 + t x Rate x Amplitude) + Offset)."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import material_records as mr  # noqa: E402


def record(shader, flags, params, textures, opacity=0.99, mtl_flags=0x88022):
    return {"mtl": "test.mtl", "index": 0, "name": "test", "surface": "", "shader": shader, "family": mr.family_of(shader),
            "flags": flags, "genMask": "", "mtlFlags": mtl_flags,
            "lighting": {"Diffuse": [0.1, 0.2, 0.3], "Opacity": opacity, "Emittance": [0.0015, 0.22, 1.0, 10.06]},
            "params": params, "textures": textures}


def texture(product, uv=None):
    return {"file": product, "assetId": "", "product": product, "role": None, "rule": "test", **({"uv": uv} if uv else {})}


class C8Specs(unittest.TestCase):
    def setUp(self):
        self.saved = mr.color_texture
        mr.color_texture = lambda product, role: {"asset": "texture:" + product, "file": product, "color": "srgb", "cap": 1024}
        self.stages = mr.STAGES
        mr.STAGES = ("c8",)

    def tearDown(self):
        mr.color_texture = self.saved
        mr.STAGES = self.stages

    def test_render_key_unchanged(self):
        rec = record("Fxmeshadvancedtransp", ["%USE_SDF_2D", "%VERTCOLORS"], {"GlobalAlphaFill": 1.62},
                     {"Custom": texture("fx/fire.dds"), "Heightmap": texture("fx/sdf.dds")})
        rules = mr.render_rules(rec)
        self.assertIn("fx", rules)
        without = {k: v for k, v in rules.items() if k != "fx"}
        self.assertEqual(mr.render_key(rules), mr.render_key(without))

    def test_fxmesh_spec(self):
        rec = record("Fxmeshadvancedtransp", ["%USE_SDF_2D", "%USE_GLOW_FRESNEL", "%USE_INTERSECTION_FADE", "%VERTCOLORS",
                                              "%USE_COMPLEX_COL", "%USE_COMPLEX_COL_OVERLAY"],
                     {"GlobalAlphaFill": 1.62, "GlobalAlphaStrength": 0.06, "INTERSECT_FADE_DIST": 1.0, "SDF_2D_TILE_X": 5.0},
                     {"Custom": texture("fx/fire.dds"), "Heightmap": texture("fx/sdf.dds"), "Specular": texture("fx/complex.dds")})
        spec = mr.spec_of(mr.render_rules(rec), {}, {})
        self.assertEqual(spec["parent"], "fxmesh")   # Opacity 0.99, no additive bit: alpha over
        self.assertEqual(spec["textures"]["SourceMap"], "texture:fx/fire.dds")
        self.assertEqual(spec["vectors"]["Global"], [1.0, 0.06, 1.62, 1.0])
        self.assertEqual(spec["vectors"]["SdfTS"][:2], [5.0, 1.0])
        self.assertEqual(spec["vectors"]["IntersectFade"][0], 1.0)
        self.assertTrue(spec["switches"]["UseSdf2D"] and spec["switches"]["UseVertexColors"] and spec["switches"]["UseComplexOverlay"])
        self.assertFalse(spec["switches"]["UseOutsideFresnelAlpha"] or spec["switches"]["UseComplexDodge"])
        self.assertNotIn("limits", spec)
        additive = mr.spec_of(mr.render_rules(record("Fxmeshadvancedtransp", [], {}, {}, opacity=1.0, mtl_flags=524320)), {}, {})
        self.assertEqual(additive["parent"], "fxmesh additive")   # Opacity 1 (the crown chamber door): the declared pass

    def test_meshparticle_spec(self):
        uv = {"tile": [0.05, 2.0], "mod": {"TexMod_UOscillatorType": 2, "TexMod_UOscillatorRate": 0.75,
                                           "TexMod_UOscillatorAmplitude": 1.0, "TexMod_VOscillatorType": 2,
                                           "TexMod_VOscillatorRate": 0.01, "TexMod_VOscillatorAmplitude": 1.0}}
        rec = record("Meshparticle", ["%DIFFUSE_MAP_2", "%ENABLE_FADEOUT", "%APPLY_SUN_COLOR"],
                     {"Brightness": 0.75, "AmbStrength": 0.5, "viewDependencyFactor": 10.0, "FadeOutDistance": 0.05},
                     {"Diffuse": texture("fx/ray.dds"), "Custom": texture("fx/noise.dds", uv)}, opacity=0.69)
        spec = mr.spec_of(mr.render_rules(rec), {}, {})
        self.assertEqual(spec["parent"], "meshparticle")
        self.assertEqual(spec["vectors"]["CustomUV"], [0.05, 2.0, 0.0, 0.0])
        self.assertAlmostEqual(spec["vectors"]["CustomMove"][0], 0.75)
        self.assertAlmostEqual(spec["vectors"]["CustomMove"][1], 0.01)
        self.assertEqual(spec["vectors"]["Light"], [0.5, 0.75, 0.5, 1.0])
        self.assertEqual(spec["vectors"]["Fade"], [0.55, 10.0, 0.05, 0.0])
        self.assertEqual(spec["vectors"]["DiffuseColor"][3], 0.69)
        self.assertNotIn("LayerMask", spec["textures"])   # no Emittance map: the parent default (white)
        self.assertTrue(spec["switches"]["UseDiffuseMap2"] and spec["switches"]["EnableFadeout"] and spec["switches"]["ApplySunColor"])

    def test_gate(self):
        mr.STAGES = ("beam",)
        rec = record("Meshparticle", [], {}, {"Diffuse": texture("fx/ray.dds")})
        spec = mr.spec_of(mr.render_rules(rec), {}, {})
        self.assertTrue(spec is None or spec.get("parent") not in ("meshparticle", "fxmesh", "fxmesh additive"))


if __name__ == "__main__":
    unittest.main()
