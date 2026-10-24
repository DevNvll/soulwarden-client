"""Tests of the material rule of the skins of the persons (converter/swc_convert/persons.choose_skin_material, AST-277) with
synthetic readers (no game data): the .cdf Material, the tone body of a race body that the game copy does not have, the material
of the skin (MTLNAME), the player appearance row, and never the Model Material."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))

from swc_convert import persons  # noqa: E402

GAME = {
    "p/male/ancientmedium/male_ancientmedium_00_chest_matgroup.mtl",
    "p/female/naked_shared/female_naked_body_shared_l1_matgroup.mtl",
    "p/female/naked_caucasian/female_naked_body_caucasian_d1_matgroup.mtl",
    "p/female/basicdress/female_basicdress_thighs_matgroup.mtl",
    "p/male/harbinger/male_harbingerdark_thighs_matgroup.mtl",
    "p/npc/cdf_given.mtl",
}
OWN = {
    "chest.skin": "{GUID-CHEST}",
    "body.skin": "{GUID-BODY}",
    "harbinger_thighs.skin": "male_harbingerdark_thighs_matgroup",   # a bare name of the male folder: not in the folder of the skin
}
GUIDS = {"{GUID-CHEST}": "p/male/ancientmedium/male_ancientmedium_00_chest_matgroup.mtl",
         "{GUID-BODY}": "p/female/naked_caucasian/female_naked_body_caucasian_d1_matgroup.mtl"}
TONES = {"p/female/naked_mix/female_naked_body_mix_l1_matgroup.mtl": "p/female/naked_shared/female_naked_body_shared_l1_matgroup.mtl"}
ROWS = {"basicdress_thighs.skin": "p/female/basicdress/female_basicdress_thighs_matgroup.mtl",
        "harbinger_thighs.skin": "p/male/harbinger/male_harbingerdark_thighs_matgroup.mtl"}


def loads(reference, skin):
    path = GUIDS.get(reference, reference)
    return path if path in GAME else None


def choose(skin, material=""):
    return persons.choose_skin_material(skin, material, loads, lambda s: OWN.get(s, ""), lambda m: TONES.get(m, ""),
                                        lambda s: ROWS.get(s, ""))


class SkinMaterialTest(unittest.TestCase):
    def test_cdf_material_first(self):
        self.assertEqual(choose("chest.skin", "p/npc/cdf_given.mtl"), "p/npc/cdf_given.mtl")

    def test_no_cdf_material_takes_the_skin_material(self):
        # The mason: the armor attachments have no Material; before, the part had no material (white).
        self.assertEqual(choose("chest.skin"), "p/male/ancientmedium/male_ancientmedium_00_chest_matgroup.mtl")

    def test_missing_race_body_takes_the_tone_body(self):
        # Not the material of the skin (the caucasian d1 body): the tone of the .cdf is kept.
        self.assertEqual(choose("body.skin", "p/female/naked_mix/female_naked_body_mix_l1_matgroup.mtl"),
                         "p/female/naked_shared/female_naked_body_shared_l1_matgroup.mtl")

    def test_missing_material_with_no_tone_takes_the_skin_material(self):
        self.assertEqual(choose("body.skin", "p/female/naked_x/gone.mtl"),
                         "p/female/naked_caucasian/female_naked_body_caucasian_d1_matgroup.mtl")

    def test_appearance_row_last(self):
        self.assertEqual(choose("basicdress_thighs.skin"), "p/female/basicdress/female_basicdress_thighs_matgroup.mtl")
        self.assertEqual(choose("harbinger_thighs.skin"), "p/male/harbinger/male_harbingerdark_thighs_matgroup.mtl")

    def test_nothing_gives_no_material(self):
        self.assertEqual(choose("unknown.skin"), "")


if __name__ == "__main__":
    unittest.main()
