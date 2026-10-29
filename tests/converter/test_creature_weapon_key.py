"""Tests of the weapon body of a model key (C-8, converter/swc_convert/creature.py) with no game data: a conversion of some slices
writes a weapon skeleton again with the animated weapons of the slices of earlier conversions; their bodies come from the model key
(MeshOverride|SkinOverride1|MaterialOverride1|SkinOverride2|MaterialOverride2), as weapon_body makes them from the weapon row."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import creature  # noqa: E402


class WeaponKey(unittest.TestCase):
    def setUp(self):
        self.saved = creature.read_cdf
        creature.read_cdf = lambda path: {"skeleton": "bow.chr", "skins": [{"name": "a", "skin": "bow.skin", "material": "bow.mtl"},
                                                                          {"name": "b", "skin": "string.skin", "material": "s.mtl"}]}

    def tearDown(self):
        creature.read_cdf = self.saved

    def test_same_as_row(self):
        key = "objects/weapons/bow.cdf|objects/weapons/bow_b.skin|objects/weapons/bow_b.mtl||"
        row = {"SkinOverride1": "objects/weapons/bow_b.skin", "MaterialOverride1": "objects/weapons/bow_b.mtl", "SkinOverride2": "",
               "MaterialOverride2": ""}
        self.assertEqual(creature.weapon_body_of_key(key), creature.weapon_body("objects/weapons/bow.cdf", row))
        self.assertEqual(creature.weapon_body_of_key(key)["skins"][0]["skin"], "objects/weapons/bow_b.skin")

    def test_creature_body_key(self):
        self.assertIsNone(creature.weapon_body_of_key("objects/characters/skeleton.cdf"))
        self.assertIsNone(creature.weapon_body_of_key("objects/characters/skeleton.cdf|objects/characters/red.mtl"))


class SetClips(unittest.TestCase):
    def test_keeps_earlier_clips(self):
        class Resolver:
            clips = {"new_attack": {}}
            spaces = {"new_turn": {"examples": []}}
            combs = {}
            missing = {"gone_clip"}
        known = {"idle": {"asset": "/A/idle"}, "walk": {"asset": "/A/walk"}, "new_attack": {"asset": "/A/new"}}
        old = {"clips": {"idle": {"asset": "/old"}, "walk": {}}, "blendSpaces": {"turn": {"examples": [1]}},
               "combs": {"turn_comb": {"names": ["turn"]}}, "missingClips": ["walk"]}
        animations = {}
        creature.set_clips(animations, old, known, Resolver())
        self.assertEqual(sorted(animations["clips"]), ["idle", "new_attack", "walk"])
        self.assertEqual(animations["clips"]["idle"], {"asset": "/A/idle"})   # the object path of the skeleton of this run
        self.assertEqual(sorted(animations["blendSpaces"]), ["new_turn", "turn"])
        self.assertEqual(animations["combs"], {"turn_comb": {"names": ["turn"]}})
        self.assertEqual(animations["missingClips"], ["gone_clip"])   # "walk" has a clip now

    def test_full_conversion(self):
        class Resolver:
            clips = {"idle": {}}
            spaces, combs, missing = {}, {}, set()
        animations = {}
        creature.set_clips(animations, {}, {"idle": {"asset": "/A/idle"}}, Resolver())
        self.assertEqual(animations["clips"], {"idle": {"asset": "/A/idle"}})


if __name__ == "__main__":
    unittest.main()
