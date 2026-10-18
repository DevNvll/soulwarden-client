"""Tests of materials.instance_key (the materials track, A1; docs/MATERIALS.md) with no game data: two sub-materials
with the same legacy key (the rules of stage 1.2) and different render rules (the XML Diffuse)."""
import json
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import material_records, materials  # noqa: E402


def element(diffuse: str) -> ET.Element:
    # No shader name and no texture: no read of the game copy.
    return ET.Element("Material", {"Name": "test", "Shader": "", "MtlFlags": "0", "Diffuse": diffuse,
                                   "Specular": "0.04,0.04,0.04", "Shininess": "128"})


class InstanceKey(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.previous = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = self.folder.name
        material_records.main_keys.cache_clear()
        self.a, self.b = element("0.5,0.5,0.5,1"), element("0.25,0.1,0.05,1")
        self.legacy = materials.shared_material(self.a)[0]["name"]

    def tearDown(self):
        material_records.main_keys.cache_clear()
        if self.previous is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.previous
        self.folder.cleanup()

    def render(self, e: ET.Element) -> str:
        return material_records.render_key(material_records.render_rules(material_records.source_record("", 0, e)))

    def write_keys(self, mains: dict) -> None:
        path = Path(self.folder.name) / "materials"
        path.mkdir(parents=True, exist_ok=True)
        (path / "keys.json").write_text(json.dumps({"version": 3, "main render keys": mains}))
        material_records.main_keys.cache_clear()

    def test_same_legacy_key(self):
        self.assertEqual(materials.shared_material(self.b)[0]["name"], self.legacy)
        self.assertNotEqual(self.render(self.a), self.render(self.b))

    def test_main_rules_keep_the_legacy_key(self):
        self.write_keys({self.legacy: self.render(self.a)})
        self.assertEqual(materials.instance_key(self.a), self.legacy)
        self.assertEqual(materials.instance_key(self.b), "n_" + self.render(self.b)[2:])

    def test_unknown_key(self):
        self.write_keys({})
        self.assertEqual(materials.instance_key(self.b), self.legacy)
        material_records.main_keys.cache_clear()
        (Path(self.folder.name) / "materials" / "keys.json").unlink()
        self.assertEqual(materials.instance_key(self.b), self.legacy)


if __name__ == "__main__":
    unittest.main()
