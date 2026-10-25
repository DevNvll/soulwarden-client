"""Tests of the parent inputs of research/foliage-specular-blend.md with synthetic records, no game data: the layer 2 transform of the
Illum blend layer (the Custom slot on UV0, apart from the layer-1 UVTransform; Part B), and the leaf inputs of M_SwcFoliage
(NormalViewDependency for %LEAVES or %GRASS, the specular map, the TransmittanceColor default; Part A); and the spec of
GeometryBeamSimple (research/beamsimple.md: the jump pad disks; stage "beamsimple")."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import material_records as mr  # noqa: E402


def texture(product, role, uv=None):
    return {"file": product, "assetId": "", "product": product, "role": role, "rule": "test", **({"uv": uv} if uv else {})}


def rock():
    """The default material of wld_epicboulder_jagged_flat_a (the values of the study): layer 1 at tile 8, layer 2 (Custom) at 12."""
    textures = {"Diffuse": texture("rock/rock_02_dif.dds", "diffuse", {"tile": [8.0, 8.0]}),
                "Custom": texture("rock/rock_04_diff.dds", "diffuse2", {"tile": [12.0, 12.0]}),
                "Opacity": texture("rock/cloud_mask.dds", "blend mask")}
    lighting = {"Diffuse": [0.491, 0.491, 0.491], "Specular": [0.04, 0.04, 0.04], "Opacity": 1.0, "AlphaTest": 0.0,
                "Shininess": 128.0}
    params = {"BlendFactor": 1.05, "BlendFalloff": 11.34, "BlendMaskTiling": 2.25, "BlendLayer2Tiling": 1.0,
              "BlendLayer2Diffuse": [0.4019778, 0.4452012, 0.4910209]}
    return {"mtl": "rock.mtl", "index": 0, "name": "rock_d_shd", "surface": "", "shader": "Illum", "family": "illum",
            "flags": ["%BLENDLAYER", "%NORMAL_MAP"], "genMask": "", "mtlFlags": 0, "lighting": lighting, "params": params,
            "textures": textures}


def leaf(flags=("%LEAVES", "%SPECULAR_MAP"), params=None):
    """A Vegetation record as the creeping-thyme cover material (sup_inv_grasses_b_mat.mtl#0 of the study)."""
    textures = {"Diffuse": texture("veg/grass_diff.dds", "diffuse"), "Specular": texture("veg/grass_spec.dds", "specular")}
    lighting = {"Diffuse": [1.0, 1.0, 1.0], "Specular": [0.031896, 0.031896, 0.031896], "Opacity": 1.0, "AlphaTest": 0.5,
                "Shininess": 200.0}
    return {"mtl": "grass.mtl", "index": 0, "name": "grass", "surface": "", "shader": "Vegetation", "family": "vegetation",
            "flags": list(flags), "genMask": "", "mtlFlags": 0, "lighting": lighting, "params": params or {}, "textures": textures}


def pad(name, flags, mtl_flags, opacity, params, mod=None):
    """A jump pad disk material (the values of research/beamsimple.md)."""
    textures = {"Diffuse": texture(f"jumppads/{name}_diff.dds", "diffuse", {"mod": mod} if mod else None),
                "Bumpmap": texture(f"jumppads/{name}_ddna.dds", None)}
    lighting = {"Diffuse": [0.0, 0.13286835, 1.0], "Specular": [0.04, 0.04, 0.04], "Opacity": opacity, "Shininess": 255.0}
    return {"mtl": name + ".mtl", "index": 0, "name": name, "surface": "", "shader": "Geometrybeamsimple", "family": "other",
            "flags": list(flags), "genMask": "", "mtlFlags": mtl_flags, "lighting": lighting, "params": params,
            "textures": textures}


class BlendLayer(unittest.TestCase):
    def setUp(self):
        self.saved = (mr.color_texture, mr.data_texture, mr.file_srgb, mr.linear_texture)
        mr.color_texture = lambda product, role: {"asset": "texture:" + product, "file": product, "color": "srgb", "cap": 1024}
        mr.data_texture = lambda product, **kw: {"asset": "texture:" + product + "@data", "file": product, "color": "linear", "cap": 1024}
        mr.linear_texture = lambda product, **kw: {"asset": "texture:" + product + "@linear", "file": product, "color": "linear",
                                                   "cap": 1024}
        mr.file_srgb = lambda product: True

    def tearDown(self):
        mr.color_texture, mr.data_texture, mr.file_srgb, mr.linear_texture = self.saved

    def test_layer2_transform(self):
        spec = mr.spec_of(mr.render_rules(rock()), {}, {})
        self.assertTrue(spec["switches"]["Blend"])
        self.assertEqual(spec["vectors"]["UVTransform"], [8.0, 8.0, 0.0, 0.0])
        self.assertEqual(spec["vectors"]["Layer2Transform"], [12.0, 12.0, 0.0, 0.0])
        self.assertEqual(spec["scalars"]["BlendMaskTiling"], 2.25)
        self.assertEqual(spec["vectors"]["BlendParams"][:2], [1.05, 11.34])


class Foliage(BlendLayer):
    def spec(self, rec):
        return mr.spec_of(mr.render_rules(rec), {}, {})

    def test_leaf_inputs(self):
        spec = self.spec(leaf())
        self.assertEqual(spec["parent"], "foliage")
        self.assertEqual(spec["scalars"]["NormalViewDependency"], 0.5)   # the shader default
        self.assertIn("veg/grass_spec.dds", spec["textures"]["SpecularMap"])
        self.assertAlmostEqual(spec["scalars"]["Specular"], 0.031896 / 0.08, places=5)
        self.assertEqual(spec["vectors"]["TransmittanceColor"], [1.0, 1.0, 0.6, 1.0])

    def test_no_leaf_branch(self):
        spec = self.spec(leaf(flags=(), params={"NormalViewDependency": 0.8, "TransmittanceColor": [0.8, 0.7, 0.4]}))
        self.assertNotIn("NormalViewDependency", spec["scalars"])   # no %LEAVES or %GRASS
        self.assertNotIn("SpecularMap", spec["textures"])           # no %SPECULAR_MAP
        self.assertEqual(spec["vectors"]["TransmittanceColor"], [0.8, 0.7, 0.4, 1.0])
        self.assertEqual(self.spec(leaf(flags=("%GRASS",), params={"NormalViewDependency": 0.8}))["scalars"]["NormalViewDependency"],
                         0.8)



class BeamSimple(BlendLayer):
    def setUp(self):
        super().setUp()
        self.stages = mr.STAGES
        mr.STAGES = tuple(mr.STAGES) + ("beamsimple",)

    def tearDown(self):
        mr.STAGES = self.stages
        super().tearDown()

    def test_active_up(self):
        params = {"FinalMultiplier": 4.0, "SoftIntersectionFactor": 10.0, "StartColor": [0.0, 0.7912981, 1.0, 1.0],
                  "EndColor": [0.0, 1.0, 0.3005438, 0.0], "fadingFeaturing": 5.0, "AmbStrength": 1.0, "dustUVScale": 5.0,
                  "dustTimeScale": 2.0, "turbStrength": 5.0, "turbRatio": 0.5, "BackgroudAlphaNoiseSpeedX": 0.2,
                  "BackgroudAlphaNoiseSpeedY": 0.2, "BackgroudAlphaNoiseSpeedZ": 0.2}
        rules = mr.render_rules(pad("up_01", ("%NOISE", "%RECEIVE_SHADOWS"), 557094, 0.99, params))
        spec = mr.spec_of(rules, {}, {})
        self.assertEqual(spec["parent"], "beam simple")
        self.assertEqual(spec["scalars"]["RawAdditive"], 1.0)   # the additive bit with Opacity 0.99: ONE, ONE
        self.assertEqual(spec["scalars"]["Noise"], 1.0)
        self.assertIn("jumppads/up_01_ddna.dds", spec["textures"]["Turbulence"])
        self.assertEqual(spec["vectors"]["DiffuseColor"], [0.0, 0.13286835, 1.0, 0.99])
        self.assertEqual(spec["vectors"]["NoiseSpeed"], [0.2, 0.2, 0.2, 0.0])
        self.assertEqual(spec["scalars"]["dustUVScale"], 5.0)
        self.assertEqual(spec["limits"], ["%RECEIVE_SHADOWS: the shadow term is 1"])
        self.assertEqual(mr.render_key(rules), mr.render_key({k: v for k, v in rules.items() if k != "fx"}))   # no new key

    def test_ring(self):
        mod = {"TexMod_RotateType": 2.0, "TexMod_URotateCenter": 0.5, "TexMod_VRotateCenter": 0.5,
               "TexMod_WRotateAmplitude": -0.02, "TexMod_WRotateRate": 0.1}
        spec = mr.spec_of(mr.render_rules(pad("ring_01", (), 524326, 1.0, {"FinalMultiplier": 1.0}, mod)), {}, {})
        self.assertEqual(spec["scalars"]["RawAdditive"], 0.0)   # Opacity 1: SRC_ALPHA, ONE
        self.assertEqual(spec["vectors"]["Rotate"], [-0.02, 0.5, 0.5, 0.0])   # the amplitude is the speed; the rate is not used
        self.assertNotIn("Turbulence", spec["textures"])   # no %NOISE: the Bumpmap is not read
        self.assertEqual(spec["scalars"]["AmbStrength"], 0.12)   # the shader default

    def test_stage_gate(self):
        mr.STAGES = self.stages
        spec = mr.spec_of(mr.render_rules(pad("ring_01", (), 524326, 1.0, {})), {}, {})
        self.assertTrue(spec is None or spec.get("parent") != "beam simple")


if __name__ == "__main__":
    unittest.main()
