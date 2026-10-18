"""Tests of the slot maps of the material overrides with the instance keys (A2 and A2b, docs/MATERIALS.md) with no game
data: the mesh, its material file, and the override material file are stubs."""
import json
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import material_records, materials, overrides  # noqa: E402


def element(diffuse: str) -> ET.Element:
    # No shader name and no texture: no read of the game copy.
    return ET.Element("Material", {"Name": "test", "Shader": "", "MtlFlags": "0", "Diffuse": diffuse,
                                   "Specular": "0.04,0.04,0.04", "Shininess": "128"})


def render(e: ET.Element) -> str:
    return material_records.render_key(material_records.render_rules(material_records.source_record("", 0, e)))


class OverrideKeys(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.previous = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = self.folder.name
        # a and b: one legacy key, two render keys (the XML Diffuse); c: another legacy key.
        self.a, self.b, self.c = element("0.5,0.5,0.5,1"), element("0.25,0.1,0.05,1"), element("0.5,0.5,0.5,1")
        self.c.set("Specular", "0.5,0.5,0.5")
        self.legacy = materials.shared_material(self.a)[0]["name"]
        self.other = materials.shared_material(self.c)[0]["name"]
        path = Path(self.folder.name) / "materials"
        path.mkdir()
        (path / "keys.json").write_text(json.dumps({"version": 3, "main render keys": {
            self.legacy: render(self.a), self.other: render(self.c)}}))
        material_records.main_keys.cache_clear()

    def tearDown(self):
        material_records.main_keys.cache_clear()
        if self.previous is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.previous
        self.folder.cleanup()

    def slot_map(self, defaults, replacements):
        mesh = SimpleNamespace(subsets=[SimpleNamespace(material=i) for i in range(len(defaults))])

        def load_mtl(reference, mesh_key=None):
            return (None, defaults) if mesh_key else (None, replacements)

        with mock.patch.object(overrides.mesh, "load", return_value=(None, [mesh])), \
                mock.patch.object(overrides.cgf, "material_guid", return_value="ref"), \
                mock.patch.object(overrides.materials, "load_mtl", side_effect=load_mtl):
            return overrides.slot_map(("objects/test.cgf", "objects/test_override.mtl"))

    def test_override_with_other_rules(self):
        # A2: the override sub-material b has the legacy key of the slot, but its own rules: its n_ instance.
        out = self.slot_map([self.c], [self.b])
        self.assertEqual(out["slots"], {self.other: "n_" + render(self.b)[2:]})
        self.assertIn("material:" + self.legacy, {r["asset"] for r in out["records"]})   # the record for the import

    def test_slot_with_other_rules(self):
        # A2b: the mesh slot of b is named by its n_ key (mesh rule 7); the override c replaces it.
        out = self.slot_map([self.b], [self.c])
        self.assertEqual(out["slots"], {"n_" + render(self.b)[2:]: self.other})

    def test_same_instance(self):
        # The override gives the same instance: no change of the slot.
        out = self.slot_map([self.a], [self.a])
        self.assertEqual(out["slots"], {})


if __name__ == "__main__":
    unittest.main()
