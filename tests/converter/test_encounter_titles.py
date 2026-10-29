"""The head title of the tracker row of an encounter (swc_convert/worldmap.encounter_titles, E2b of QG5, ut567) with synthetic data (no
game data): the name key of the territory definition of the first "Territory Id" of the root slice; no id or no row gives an empty
title (retail has no fallback: not the stage title, not the success event). The values are the ones of the static read E2b."""
import unittest

from swc_convert.worldmap import encounter_titles

RECIPES = [{"asset": "7b52b1b2898d5d2684bd2dced94f3321", "slice": "obelisk01"},
           {"asset": "aa", "slice": "thorpe"},
           {"asset": "bb", "slice": "goldenstash"},
           {"asset": "cc", "slice": "unknown-row"}]
IDS = {"obelisk01": [8058], "thorpe": [8048, 9000], "goldenstash": [], "unknown-row": [1]}
NAMES = {8058: "@04A_Side02_02_Encounter_01", 8048: "@9901_0036_masterofthetempest_objective_solo_title", 9000: "@other"}


class EncounterTitlesTest(unittest.TestCase):
    def test_titles(self):
        titles = encounter_titles(RECIPES, lambda path: IDS[path], NAMES)
        self.assertEqual(titles["7B52B1B2898D5D2684BD2DCED94F3321"], {"territory": 8058, "title": "@04A_Side02_02_Encounter_01"})
        self.assertEqual(titles["AA"]["title"], "@9901_0036_masterofthetempest_objective_solo_title")   # the first id of the slice
        self.assertEqual(titles["BB"], {"territory": 0, "title": ""})
        self.assertEqual(titles["CC"], {"territory": 1, "title": ""})


if __name__ == "__main__":
    unittest.main()
