"""The game modes of the UI (swc_convert/gamemodes.build, C-13 step 1b part B of ut567) with synthetic rows (no game data): the key is the
CRC-32 of the lower-case GameModeId (QuestMedea 95B632E9), the columns are camel case, empty cells and rows with no id are left out;
and the entrance member of an object record (swc_convert/objects.entrance_record)."""
import unittest

from swc_convert.gamemodes import build
from swc_convert.objects import entrance_record


class GameModesTest(unittest.TestCase):
    def test_build(self):
        doc = build([{"GameModeId": "QuestMedea", "DisplayName": "@Quest_Medea_title", "QuitButtonTextId": "@ui_solo_arena_leave",
                      "IsSoloTrial": True, "RecommendedLevel": 17.0, "Description": ""},
                     {"GameModeId": "", "DisplayName": "@x"}])
        self.assertEqual(list(doc["modes"]), [str(0x95B632E9)])
        mode = doc["modes"][str(0x95B632E9)]
        self.assertEqual(mode, {"id": "QuestMedea", "displayName": "@Quest_Medea_title", "quitButtonTextId": "@ui_solo_arena_leave",
                                "isSoloTrial": True, "recommendedLevel": 17.0})

    def test_entrance_record(self):
        # The member entrance of the backend interaction data (QuestMedea entrance 9f061218-...): the mode is the CRC of the lower-case id.
        self.assertEqual(entrance_record({"mode": "QuestMedea", "solo": True, "slot": 3}),
                         {"mode": 0x95B632E9, "modeId": "QuestMedea", "solo": True})
        self.assertEqual(entrance_record({"mode": "QUESTMEDEA"})["mode"], 0x95B632E9)


if __name__ == "__main__":
    unittest.main()
