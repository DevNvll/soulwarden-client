"""Tests of the marker sources of the quest tasks (stage Q2; converter/swc_convert/questpins.py; research/quest-pins.md) with synthetic
data: the native OverrideLocation parser (comma entries, a missing number is 0), the POI ids of the tags (map definitions and backend
points; a point only from the backend), TerritoryID as a filter value, the GameModeID gate value, the conversation NPC, the pin height
sentinel, and the hand-in NPC positions."""
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
        "trigger": {"code": 3, "cells": {"OverrideLocation": "9621.61 2309.48 40.24", "ShowDiegeticTaskPin": "1", "TriggerAreaType": "Enter",
                                         "TP_TargetTag": "Aelstrom_IGC_Trigger", "DiegeticPinZOverride": "0"}},
        "shifted": {"code": 13, "cells": {"OverrideLocation": "1"}},
        "three": {"code": 13, "cells": {"OverrideLocation": "1295,1424,930"}},
        "kill": {"code": 13, "cells": {"OverrideLocation": "9694.9473 2262.7566 43.6366", "POITag": "drowned_shipwreck_2_40142",
                                       "ShowDiegeticTaskPin": "0", "DiegeticPinZOverride": "2.5", "TP_HideOnCompassOnEnterPOI": "1"}},
        "unknown poi": {"code": 6, "cells": {"POITag": "no_such_tag", "TerritoryID": "6"}},
        "plain": {"code": 25, "cells": {"ItemName": "Something"}},
        "map poi": {"code": 6, "cells": {"POITag": "Fort_Tag, drowned_shipwreck_2_40142", "GameModeID": "3"}},
        "talk": {"code": 16, "cells": {"ItemName": "01_AelstromWW_01"}},
    },
    "objectives": {"06_getoffbeach": {"cells": {"NpcDestinationId": "01_AelstromWW_01", "HideIncompleteTurninMarker": "1"}},
                   "no npc": {"cells": {"Title": "x"}}},
}
WORLDMAP = {"definitions": {"40142": {"pOITag": "drowned_shipwreck_2_40142"}, "500": {"pOITag": "fort_tag,second"}},
            "shapes": [{"id": 500, "position": [10.0, 20.0, 30.0], "min": [0.0, 0.0], "max": [100.0, 50.0]}]}
PERSONS = [{"npcId": "01_AelstromWW_01", "position": [9613.356, 2329.267, 42.432]}, {"npcId": "other", "position": [1, 2, 3]}]


class QuestPins(unittest.TestCase):
    def setUp(self):
        self.doc = questpins.build(QUESTS, PERSONS, WORLDMAP)

    def test_native_parser(self):
        self.assertEqual(questpins.parse_locations("9621.61 2309.48 40.24"), [[9621.61, 2309.48, 40.24]])
        self.assertEqual(questpins.parse_locations("1"), [[1.0, 0.0, 0.0]])
        self.assertEqual(questpins.parse_locations("1295,1424,930"), [[1295.0, 0.0, 0.0], [1424.0, 0.0, 0.0], [930.0, 0.0, 0.0]])
        self.assertEqual(questpins.parse_locations("876 663,853 724"), [[876.0, 663.0, 0.0], [853.0, 724.0, 0.0]])
        self.assertEqual(questpins.parse_locations(""), [])
        self.assertEqual(questpins.parse_locations("a b c"), [[0.0, 0.0, 0.0]])

    def test_beach_trigger(self):
        t = self.doc["tasks"]["trigger"]
        self.assertEqual(t["override"], [[9621.61, 2309.48, 40.24]])
        self.assertTrue(t["showPin"])
        self.assertNotIn("pinZ", t)   # 0 is the sentinel: the client adds 1.75 m
        self.assertEqual(t["triggerArea"], "Enter")

    def test_short_cells_are_points_and_reported(self):
        self.assertEqual(self.doc["tasks"]["shifted"]["override"], [[1.0, 0.0, 0.0]])
        self.assertEqual(len(self.doc["tasks"]["three"]["override"]), 3)
        self.assertEqual(self.doc["report"]["override short entries"], 2)

    def test_poi_with_a_backend_point(self):
        t = self.doc["tasks"]["kill"]
        self.assertEqual(t["pois"], [[40142, 9696.8, 2258.3, 40.3]])
        self.assertEqual(t["poiTerritory"], 6)
        self.assertNotIn("showPin", t)
        self.assertEqual(t["pinZ"], 2.5)
        self.assertTrue(t["hideOnEnterPoi"])

    def test_map_poi_has_an_id_and_no_guessed_point(self):
        t = self.doc["tasks"]["map poi"]
        self.assertEqual(t["poiIds"], [500, 40142])
        self.assertEqual(t["pois"], [[40142, 9696.8, 2258.3, 40.3]])   # only the backend point; 500 has none
        self.assertEqual(t["gameMode"], "3")

    def test_unknown_poi_and_territory(self):
        t = self.doc["tasks"]["unknown poi"]
        self.assertNotIn("poiIds", t)
        self.assertEqual(t["territory"], 6)

    def test_task_with_no_marker_field(self):
        self.assertNotIn("plain", self.doc["tasks"])

    def test_conversation_npc(self):
        self.assertEqual(self.doc["tasks"]["talk"]["npc"], "01_AelstromWW_01")

    def test_hand_in_npc(self):
        self.assertEqual(self.doc["objectives"]["06_getoffbeach"], {"npc": "01_AelstromWW_01", "hideTurninMarker": True})
        self.assertEqual(self.doc["npcs"], {"01_aelstromww_01": [[9613.356, 2329.267, 42.432]]})
        self.assertNotIn("no npc", self.doc["objectives"])
        self.assertNotIn("areas", self.doc)


if __name__ == "__main__":
    unittest.main()
