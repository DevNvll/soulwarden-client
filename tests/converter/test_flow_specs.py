"""Tests of the flow rule (AST-290, converter/swc_convert/material_records.py) with synthetic records, no game data: the UV speed of
the type 2 oscillators (amplitude x rate x tile), the flowing liquids (Illum, Opacity < 1, no AlphaTest, a type 2 scroll on the
Diffuse slot) on M_SwcSurfaceTranslucent with UVPan, the scroll of the emission maps (EmissionPan), the gate of the stage "flow",
and the render key, which does not change (the slot names of the converted meshes)."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import material_records as mr  # noqa: E402

KIT_UV = {"tile": [0.75, 0.25], "mod": {"TexMod_RotateType": 2.0, "TexMod_UOscillatorAmplitude": 1.0,
                                        "TexMod_UOscillatorRate": 0.05, "TexMod_UOscillatorType": 2.0,
                                        "TexMod_VOscillatorAmplitude": 1.0, "TexMod_VOscillatorRate": -0.75,
                                        "TexMod_VOscillatorType": 2.0}}


def texture(product, role, uv=None):
    return {"file": product, "assetId": "", "product": product, "role": role, "rule": "test", **({"uv": uv} if uv else {})}


def record(opacity=0.3, alpha_test=0.0, uv=KIT_UV, emission=False):
    textures = {"Diffuse": texture("waterfall/water_diff.dds", "diffuse", uv)}
    lighting = {"Diffuse": [0.5, 0.5, 0.5], "Specular": [0.04, 0.04, 0.04], "Opacity": opacity, "AlphaTest": alpha_test,
                "Shininess": 128.0}
    if emission:
        lighting["Emittance"] = [0.66, 0.49, 0.68, 75.0]
        textures["Emittance"] = texture("ghost/light2.dds", "emittance")
        textures["Decal"] = texture("vfx/noise_03.dds", "emittance intensity",
                                    {"tile": [3.0, 3.0], "mod": {"TexMod_VOscillatorAmplitude": 1.0,
                                                                 "TexMod_VOscillatorRate": 0.25, "TexMod_VOscillatorType": 2.0}})
    return {"mtl": "test.mtl", "index": 0, "name": "water", "surface": "", "shader": "Illum", "family": "illum",
            "flags": ["%TRANSMITTANCE", "%VERTCOLORS"] + (["%EMITTANCE_MAP"] if emission else []), "genMask": "",
            "mtlFlags": 0, "lighting": lighting, "params": {}, "textures": textures}


class FlowSpecs(unittest.TestCase):
    def setUp(self):
        self.saved = (mr.color_texture, mr.data_texture, mr.file_srgb)
        mr.color_texture = lambda product, role: {"asset": "texture:" + product, "file": product, "color": "srgb", "cap": 1024}
        mr.data_texture = lambda product: {"asset": "texture:" + product + "@data", "file": product, "color": "linear", "cap": 1024}
        mr.file_srgb = lambda product: True
        self.stages = mr.STAGES

    def tearDown(self):
        mr.color_texture, mr.data_texture, mr.file_srgb = self.saved
        mr.STAGES = self.stages

    def spec(self, rec):
        rules = mr.render_rules(rec)
        return mr.spec_of(rules, {}, {})

    def test_uv_pan(self):
        self.assertEqual(mr.uv_pan(KIT_UV), [0.0375, -0.1875])   # (0.05 x 0.75, -0.75 x 0.25)
        self.assertEqual(mr.uv_pan({"mod": {"TexMod_VOscillatorType": 4.0, "TexMod_VOscillatorRate": 1.0}}), [0.0, 0.0])
        self.assertEqual(mr.uv_pan({"mod": {"TexMod_VOscillatorType": 2.0, "TexMod_VOscillatorRate": 0.5}}), [0.0, 0.5])
        self.assertEqual(mr.uv_pan(None), [0.0, 0.0])

    def test_flow_of(self):
        self.assertEqual(mr.flow_of(record()), {"opacity": 0.3})
        self.assertEqual(mr.flow_of(record(opacity=1.0)), {})          # opaque
        self.assertEqual(mr.flow_of(record(alpha_test=0.3)), {})       # a cutout
        self.assertEqual(mr.flow_of(record(uv={"tile": [1.0, 1.0]})), {})   # no scroll

    def test_render_key_unchanged(self):
        rules = mr.render_rules(record())
        self.assertIn("flow", rules)
        self.assertEqual(mr.render_key(rules), mr.render_key({k: v for k, v in rules.items() if k != "flow"}))

    def test_flow_spec(self):
        mr.STAGES = ("m2", "m4", "flow")
        spec = self.spec(record())
        self.assertEqual(spec["parent"], "surface translucent")
        self.assertEqual(spec["scalars"]["Opacity"], 0.3)
        self.assertEqual(spec["vectors"]["UVPan"], [0.0375, -0.1875, 0.0, 0.0])
        self.assertEqual(spec["vectors"]["UVTransform"], [0.75, 0.25, 0.0, 0.0])

    def test_emission_pan(self):
        mr.STAGES = ("m2", "m4", "flow")
        spec = self.spec(record(opacity=1.0, emission=True))   # an opaque body with a moving glow (the ghost intensity map)
        self.assertEqual(spec["parent"], "surface")
        self.assertEqual(spec["vectors"]["EmissionPan"], [0.0, 0.0, 0.0, 0.75])   # 0.25 x tile 3
        self.assertEqual(spec["vectors"]["UVPan"], [0.0375, -0.1875, 0.0, 0.0])

    def test_gate(self):
        mr.STAGES = ("m2", "m4")
        spec = self.spec(record())
        self.assertNotEqual(spec["parent"], "surface translucent")
        self.assertNotIn("UVPan", spec["vectors"])
        spec = self.spec(record(opacity=1.0, emission=True))
        self.assertNotIn("EmissionPan", spec["vectors"])


if __name__ == "__main__":
    unittest.main()
