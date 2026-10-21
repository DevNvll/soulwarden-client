"""Tests of the Illum resource flag (research/lights-emission.md "Illum emission"; material_records.resource_flags) with no game
data: an Emittance or a Decal texture enables %EMITTANCE_MAP, and the record keeps the render key of the record without it (the
instance keys n_RENDER are the names of the converted mesh slots)."""
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import material_records  # noqa: E402


def element(shader: str, maps: dict[str, str]) -> ET.Element:
    e = ET.Element("Material", {"Name": "coals", "Shader": shader, "MtlFlags": "0", "StringGenMask": "%NORMAL_MAP"})
    textures = ET.SubElement(e, "Textures")
    for slot, file in maps.items():
        ET.SubElement(textures, "Texture", {"Map": slot, "File": file})
    return e


def record(flags: list[str], resource: list[str]) -> dict:
    # A rich record as source_record gives it, with no read of the game copy.
    out = {"mtl": "m", "index": 0, "name": "coals", "shader": "Illum", "family": "illum", "flags": flags, "mtlFlags": 0,
           "lighting": {"Emittance": [0.98, 0.46, 0.18, 600.0]}, "params": {}, "textures": {}}
    for slot, product in (("Diffuse", "texture:a.dds"), ("Emittance", "texture:b.dds"), ("Decal", "texture:c.dds@data")):
        out["textures"][slot] = {"file": product, "assetId": "", "product": product,
                                 **material_records.role_fields("Illum", slot, set(flags))}
    if resource:
        out["resource flags"] = resource
    return out


class ResourceFlags(unittest.TestCase):
    def test_emittance_or_decal_texture(self):
        for maps in ({"Emittance": "b.dds"}, {"Decal": "c.dds"}, {"Emittance": "b.dds", "Decal": "c.dds"}):
            self.assertEqual(material_records.resource_flags(element("Illum", maps)), {"%EMITTANCE_MAP"}, maps)
        self.assertEqual(material_records.resource_flags(element("Illum", {"Diffuse": "a.dds", "Decal": " "})), set())
        self.assertEqual(material_records.resource_flags(element("Vegetation", {"Emittance": "b.dds"})), set())

    def test_flags_of(self):
        e = element("Illum", {"Emittance": "b.dds"})
        self.assertIn("%EMITTANCE_MAP", material_records.flags_of(e)[0])
        self.assertNotIn("%EMITTANCE_MAP", material_records.flags_of(e, resources=False)[0])

    def test_key_stays(self):
        plain = record(["%NORMAL_MAP"], [])
        flagged = record(["%EMITTANCE_MAP", "%NORMAL_MAP"], ["%EMITTANCE_MAP"])
        self.assertEqual(flagged["textures"]["Emittance"]["role"], "emittance")
        self.assertIsNone(plain["textures"]["Emittance"]["role"])
        rules = material_records.render_rules(flagged)
        self.assertIn("emittance", rules["textures"])   # the map branch
        self.assertEqual(material_records.render_key(rules),
                         material_records.render_key(material_records.render_rules(plain)))
        self.assertEqual(material_records.without_resource_flags(flagged)["textures"], plain["textures"])


if __name__ == "__main__":
    unittest.main()
