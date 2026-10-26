"""Tests of two body rules (converter/swc_convert/bodies.py; item 3 of the user test, the ghosts) with no game data: a repeated glTF
material name (the follower skin of a ghost) gets the Unreal name NAME1 and the sub-material of the second skin, and an Illum body
sub-material with Opacity below BLENDED_BODY_MAX and no AlphaTest is blended."""
import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import bodies  # noqa: E402


class RepeatedNames(unittest.TestCase):
    def test_second_skin(self):
        main = ET.fromstring('<Material Name="ghostBasic_mainBody" Shader="Illum"/>')
        follower = ET.fromstring('<Material Name="ghostBasic_mainBody" Shader="Illum"/>')
        saved = bodies.materials.load_mtl
        bodies.materials.load_mtl = lambda reference, skin: (reference, [main] if "main" in reference else [follower])
        try:
            names = bodies.skin_materials([("body.skin", "main.mtl"), ("follow.skin", "follower.mtl")])
        finally:
            bodies.materials.load_mtl = saved
        self.assertEqual([m for m, _i, _e in names["ghostBasic_mainBody"]], ["main.mtl", "follower.mtl"])

    def test_gltf_names(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "body.gltf"
            path.write_text(json.dumps({"materials": [{"name": "ghostBasic_mainBody"}, {"name": "ghostBasic_mainBody"}]}))
            self.assertEqual(bodies.gltf_material_names(path), ["ghostBasic_mainBody", "ghostBasic_mainBody"])


class BlendedBody(unittest.TestCase):
    def rec(self, opacity, cutoff=0.0):
        return {"lighting": {"Opacity": opacity, "AlphaTest": cutoff}}

    def test_threshold(self):
        rules = {"shader": "illum", "blend": "opaque"}
        self.assertTrue(bodies.blended_body(self.rec(0.9), rules))       # the Gillgut ghost body
        self.assertFalse(bodies.blended_body(self.rec(0.99), rules))     # near opaque: stays opaque
        self.assertFalse(bodies.blended_body(self.rec(0.5, 0.3), rules))  # a cutout
        self.assertFalse(bodies.blended_body(self.rec(0.5), {"shader": "hair", "blend": "opaque"}))


if __name__ == "__main__":
    unittest.main()
