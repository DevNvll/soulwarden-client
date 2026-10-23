"""Tests of the marker sources of the quest tasks (stage Q2; converter/swc_convert/questpins.py) with synthetic data: the parse of
OverrideLocation (a shifted cell is not a position), the POI join by tag (any case), the flags, the hand-in NPC positions, and the
separate area records."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import questpins  # noqa: E402


QUESTS = {
    "pois": [{"id": 40142, "tags": ["Drowned_Shipwreck_2_40142"], "center": [9696.8, 2258.3, 40.3], "radius": 60.0,
              "shape": [[1.0, 2.0], [3.0, 4.0], [5.0, 1.0]], "territory": 6}],
    "tasks": {
        "trigger": {"cells": {"OverrideLocation": "9621.61 2309.48 40.24", "ShowDiegeticTaskPin": "1", "TriggerAreaType": "Enter",
                              "TP_TargetTag": "Aelstrom_IGC_Trigger"}},
        "shifted": {"cells": {"OverrideLocation": "1"}},
        "kill": {"cells": {"OverrideLocation": "9694.9473 2262.7566 43.6366", "POITag": "drowned_shipwreck_2_40142",
                           "ShowDiegeticTaskPin": "0", "DiegeticPinZOverride": "2.5", "TP_HideOnCompassOnEnterPOI": "1"}},
        "unknown poi": {"cells": {"POITag": "no_such_tag", "TerritoryID": "6"}},
        "plain": {"cells": {"ItemName": "Something"}},
        "map poi": {"cells": {"POITag": "Fort_Tag, other_tag"}},
    },
    "objectives": {"06_getoffbeach": {"cells": {"NpcDestinationId": "01_AelstromWW_01", "HideIncompleteTurninMarker": "1"}},
                   "no npc": {"cells": {"Title": "x"}}},
}
WORLDMAP = {"definitions": {"40142": {"pOITag": "drowned_shipwreck_2_40142"}, "500": {"pOITag": "fort_tag,second"}},
            "shapes": [{"id": 500, "position": [10.0, 20.0, 30.0], "min": [0.0, 0.0], "max": [100.0, 50.0],
                        "polygon": "[[0, 0], [100, 0], [100, 50]]"}, {"id": 40142, "position": [0, 0, 0], "min": [0, 0], "max": [1, 1]}]}
PERSONS = [{"npcId": "01_AelstromWW_01", "position": [9613.356, 2329.267, 42.432]}, {"npcId": "other", "position": [1, 2, 3]}]


class QuestPins(unittest.TestCase):
    def setUp(self):
        self.doc = questpins.build(QUESTS, PERSONS, WORLDMAP)

    def test_location_parse(self):
        self.assertEqual(questpins.parse_location("9621.61 2309.48 40.24"), [9621.61, 2309.48, 40.24])
        self.assertIsNone(questpins.parse_location("1"))
        self.assertIsNone(questpins.parse_location(""))
        self.assertIsNone(questpins.parse_location("a b c"))

    def test_beach_trigger(self):
        t = self.doc["tasks"]["trigger"]
        self.assertEqual(t["override"], [9621.61, 2309.48, 40.24])
        self.assertTrue(t["showPin"])
        self.assertEqual(t["triggerArea"], "Enter")

    def test_shifted_cell_is_not_a_position(self):
        self.assertNotIn("override", self.doc["tasks"]["shifted"])
        self.assertEqual(self.doc["tasks"]["shifted"]["overrideRaw"], "1")

    def test_poi_join_and_area(self):
        t = self.doc["tasks"]["kill"]
        self.assertEqual(t["poi"], 40142)
        self.assertEqual(t["poiTerritory"], 6)
        self.assertNotIn("showPin", t)
        self.assertEqual(t["pinZ"], 2.5)
        self.assertTrue(t["hideOnEnterPoi"])
        self.assertEqual(self.doc["areas"]["kill"]["poi"], 40142)

    def test_poi_of_the_map(self):
        t = self.doc["tasks"]["map poi"]
        self.assertEqual(t["poi"], 500)
        self.assertEqual(t["poiCenter"], [50.0, 25.0, 30.0])
        self.assertNotIn("poiTerritory", t)
        self.assertEqual(self.doc["areas"]["map poi"]["shape"], [[0, 0], [100, 0], [100, 50]])
        # The backend point wins over the map definition with the same tag.
        self.assertEqual(self.doc["tasks"]["kill"]["poiCenter"], [9696.8, 2258.3, 40.3])

    def test_unknown_poi_and_territory(self):
        t = self.doc["tasks"]["unknown poi"]
        self.assertNotIn("poi", t)
        self.assertEqual(t["territory"], 6)
        self.assertNotIn("unknown poi", self.doc["areas"])

    def test_task_with_no_marker_field(self):
        self.assertNotIn("plain", self.doc["tasks"])

    def test_hand_in_npc(self):
        self.assertEqual(self.doc["objectives"]["06_getoffbeach"], {"npc": "01_AelstromWW_01", "hideTurninMarker": True})
        self.assertEqual(self.doc["npcs"], {"01_aelstromww_01": [[9613.356, 2329.267, 42.432]]})
        self.assertNotIn("no npc", self.doc["objectives"])


if __name__ == "__main__":
    unittest.main()
