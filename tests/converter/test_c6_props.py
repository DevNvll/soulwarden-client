"""Tests of C6 (AST-275, converter/swc_convert/props.py) with a synthetic character definition, no game data: the parts of a prop
(a CA_SKIN part in its bind pose, a CA_BONE .cgf part at its absolute model transform; a hidden attachment and a missing file stay
out), the folders of the props (the siege weapons and the animals are not props), and the material override of a placement (a
part of the registry with another .mtl; the material field of the component only with USE_MATERIAL_FIELD)."""
import sys
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import props  # noqa: E402

CDF = "objects/weapons/melee/swords/test_sword.cdf"
FILES = {
    CDF: b"""<CharacterDefinition><Model File="objects/weapons/test.chr" Material="objects/weapons/default_mat.mtl"/>
<AttachmentList>
 <Attachment Type="CA_SKIN" AName="blade" Binding="Objects/Weapons/Test_Blade.skin" Material="objects/weapons/blade_mat.mtl" Flags="0"/>
 <Attachment Type="CA_SKIN" AName="hidden" Binding="objects/weapons/test_hidden.skin" Flags="1"/>
 <Attachment Type="CA_SKIN" AName="missing" Binding="objects/weapons/not_in_the_pak.skin"/>
 <Attachment Type="CA_BONE" AName="gem" Binding="objects/weapons/test_gem.cgf" BoneName="hand" Position="1,2,3"
  Rotation="0.70710677,0.70710677,0,0" Flags="0"/>
</AttachmentList></CharacterDefinition>""",
    "objects/weapons/test_blade.skin": b"",
    "objects/weapons/test_hidden.skin": b"",
    "objects/weapons/test_gem.cgf": b"",
}


class Props(unittest.TestCase):
    def setUp(self):
        self.saved = (props.source.read, props.source.has, props.registry, props.USE_MATERIAL_FIELD)
        props.source.read = lambda path: FILES[path]
        props.source.has = lambda path: path in FILES
        props.parts.cache_clear()
        props.registry = lambda: {"objects/weapons/test_blade.skin": "objects/weapons/blade_other_mat.mtl"}

    def tearDown(self):
        props.source.read, props.source.has, props.registry, props.USE_MATERIAL_FIELD = self.saved
        props.parts.cache_clear()

    def test_folders(self):
        self.assertTrue(props.is_prop("objects/characters/npc/natural/fish/fish_a_pike.cdf"))
        self.assertTrue(props.is_prop(CDF))
        self.assertFalse(props.is_prop("objects/weapons/siege/fire_dropper/fire_dropper_t1/fire_dropper_t1.cdf"))
        self.assertFalse(props.is_prop("objects/characters/npc/natural/cow/cowdevon.cdf"))
        self.assertFalse(props.is_prop("objects/crafting/props/tools/shovel/shovel.cgf"))

    def test_parts(self):
        found = props.parts(CDF)
        self.assertEqual([p[0] for p in found], ["objects/weapons/test_blade.skin", "objects/weapons/test_gem.cgf"])
        blade, gem = found
        self.assertEqual(blade[1], "objects/weapons/blade_mat.mtl")
        self.assertEqual(gem[1], "objects/weapons/default_mat.mtl")   # no Material: the model material
        np.testing.assert_allclose(np.array(blade[2]).reshape(4, 4), np.eye(4))
        m = np.array(gem[2]).reshape(4, 4)
        np.testing.assert_allclose(m[:3, 3], [1, 2, 3])
        np.testing.assert_allclose(m[:3, :3] @ [0, 1, 0], [0, 0, 1], atol=1e-6)   # 90 degrees about X (w, x, y, z)

    def test_placements(self):
        world = np.eye(4)
        world[:3, 3] = [100, 200, 30]
        rows = props.placements(CDF, world, "materials/seasons/s_01/hack.mtl")
        self.assertEqual(rows[0][2], "objects/weapons/blade_mat.mtl")   # the registry has another .mtl for the blade
        self.assertEqual(rows[1][2], "objects/weapons/default_mat.mtl")  # not in the registry: a material of its own, replaced
        np.testing.assert_allclose(rows[1][1][:3, 3], [101, 202, 33])
        props.USE_MATERIAL_FIELD = True
        self.assertEqual(props.placements(CDF, world, "materials/seasons/s_01/hack.mtl")[0][2], "materials/seasons/s_01/hack.mtl")


if __name__ == "__main__":
    unittest.main()
