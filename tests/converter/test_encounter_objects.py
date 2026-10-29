"""The start objects of the encounters (swc_convert/objects.encounter_doc, E4 of QG5, ut567) with synthetic data (no game data): each slice
of the backend link gathering-data.json encounterObjects gives one template and one row with local indices, two slices of one template
share it, the key is the slice key of the client entity table, and the name of the row goes into the texts."""
import unittest

from swc_convert.objects import encounter_doc

BOUND = {"entity": "00007c1f4e43326f", "component": "2534411c0f902468", "option": "00007c244e43326f", "slot": 4, "rangeSq": 2500.0,
         "translation": [0.0, 0.0, 0.0], "lineOfSight": False, "capacity": 1, "arcRequired": False, "requires": []}


def template(asset, sub=2):
    return {"slice": f"slices/test/{asset}.dynamicslice", "asset": asset, "sub": sub, "root": "00007c244e43326f", "controllerSlot": 3,
            "table": "Gatherable_Quest", "entry": "Start", "bound": dict(BOUND)}


def row(ident, time=3.0):
    return {"id": ident, "type": "None", "restriction": "NoToolNecessary", "tradeskill": "", "level": 0, "time": time, "unsupported": []}


class EncounterDocTest(unittest.TestCase):
    def setUp(self):
        self.gathering = {
            "templates": [template("00000000000000000000000000000000"), template("a28f5f64c3e15e9f9cbe6588b06b71c5"),
                          template("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 7)],
            "rows": [row("Other"), row("04A_Side02_02_EncounterStart_01"), row("Altar", 5.0)],
            # slice -> [template, row, interactTag, onlyOnQuest] (backend gathering-data.json)
            "encounterObjects": {"slices/a.dynamicslice": [1, 1, "IO_04A_Side02_02_EncounterStart_01", False],
                                 "slices/b.dynamicslice": [2, 2, "", True]},
        }
        self.columns = {"04a_side02_02_encounterstart_01": {"name": "@04A_Side02_02_EncounterStart_01_DisplayName", "action": "Handcrafting"}}
        self.texts = {"04a_side02_02_encounterstart_01_displayname": "Ancient Device"}

    def test_local_indices_and_keys(self):
        doc = encounter_doc(self.gathering, self.columns, self.texts, lambda path, entity: {"type": "sphere", "radius": 1.85})
        self.assertEqual(len(doc["templates"]), 2)
        self.assertEqual(len(doc["rows"]), 2)
        self.assertEqual(doc["objects"]["A28F5F64-C3E1-5E9F-9CBE-6588B06B71C5:2"], [0, 0])
        self.assertEqual(doc["objects"]["BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB:7"], [1, 1])
        self.assertEqual(doc["templates"][0]["bound"]["rangeSq"], 2500.0)
        self.assertEqual(doc["templates"][0]["shape"], {"type": "sphere", "radius": 1.85})
        self.assertNotIn("capacity", doc["templates"][0]["bound"])

    def test_row_and_name(self):
        doc = encounter_doc(self.gathering, self.columns, self.texts, lambda path, entity: None)
        device = doc["rows"][0]
        self.assertEqual(device[0], "04A_Side02_02_EncounterStart_01")
        self.assertEqual(device[2], "Handcrafting")
        self.assertEqual(device[6], 3.0)
        self.assertEqual(device[7], "@04A_Side02_02_EncounterStart_01_DisplayName")
        self.assertEqual(doc["texts"], {"04a_side02_02_encounterstart_01_displayname": "Ancient Device"})
        self.assertEqual(doc["rows"][1][2], "None")

    def test_shared_template(self):
        self.gathering["encounterObjects"]["slices/c.dynamicslice"] = [1, 2, "", False]
        doc = encounter_doc(self.gathering, self.columns, self.texts, lambda path, entity: None)
        self.assertEqual(len(doc["templates"]), 2)
        self.assertEqual(len(doc["rows"]), 2)


if __name__ == "__main__":
    unittest.main()
