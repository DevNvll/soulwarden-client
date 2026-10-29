"""Tests of the color opacity of the decals (C-3, converter/swc_convert/decals.py) with no game data: a projected decal with a
DecalDiffuseOpacity below 1 is split into a color pass (dpc_, above 0 only) and a normal, gloss, and reflectance pass (dps_)."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import decals  # noqa: E402


def entry(key):
    return {"entity": "1", "material": key, "location": [0.0, 0.0, 0.0], "size": [100.0, 100.0, 100.0], "opacity": 1.0}


MATERIALS = {"d_puddle": {"rules": {"params": {"DecalDiffuseOpacity": 0.3}}},
             "d_glowy": {"rules": {"params": {"DecalDiffuseOpacity": 0.0}}},
             "d_road": {"rules": {"params": {"DecalDiffuseOpacity": 1.0}}},
             "d_plain": {"rules": {"params": {}}}}


class Split(unittest.TestCase):
    def test_split(self):
        out = decals.split_entries([entry("dp_puddle"), entry("dp_glowy"), entry("dp_road"), entry("dp_plain"), entry("d_puddle")],
                                   MATERIALS)
        self.assertEqual([d["material"] for d in out], ["dpc_puddle", "dps_puddle", "dps_glowy", "dp_road", "dp_plain", "d_puddle"])
        self.assertEqual(out[0]["location"], out[1]["location"])   # the two passes at one place

    def test_switch(self):
        saved = decals.DIFFUSE_OPACITY_SPLIT
        decals.DIFFUSE_OPACITY_SPLIT = False
        try:
            self.assertEqual([d["material"] for d in decals.split_entries([entry("dp_puddle")], MATERIALS)], ["dp_puddle"])
        finally:
            decals.DIFFUSE_OPACITY_SPLIT = saved

    def test_value(self):
        self.assertEqual(decals.diffuse_opacity({"params": {"DecalDiffuseOpacity": [0.3, 0.3, 0.3]}}), 0.3)
        self.assertEqual(decals.diffuse_opacity({}), 1.0)


if __name__ == "__main__":
    unittest.main()
