"""The joins of the quest coverage report (swc_convert/questsupport.py, stage Q9) with a small client stand-in (no game data)."""
import types
import unittest

from swc_convert import questsupport


def client(**values):
    base = dict(objectives={}, tasks={}, events={}, poi_tags=set(), npcs={}, states={}, placed=set(), scenes=set(), cinematics=set(),
                recipes=set(), items={}, texts=set(), quest_objects=set(), event_tags={}, destructible_tags=set(), creature_slices=1,
                creature_failures=0)
    base.update(values)
    return types.SimpleNamespace(**base)


class SupplierTest(unittest.TestCase):
    def test_person(self):
        c = client(npcs={"01_urdaww_01": []}, placed={"01_urdaww_01"})
        self.assertTrue(questsupport.supplier_check(c, {"supplier": "the person 01_UrdaWW_01"})[1])
        self.assertFalse(questsupport.supplier_check(client(), {"supplier": "the person 01_UrdaWW_01"})[1])

    def test_destructible_event(self):
        c = client(event_tags={"msq_corruptednodule_1_interact": {"io_msq_grace_corruptednodule_1"}},
                   destructible_tags={"io_msq_grace_corruptednodule_1"})
        task = {"supplier": "a destructible quest object (MSQ_CorruptedNodule_1_interact)"}
        text, ok = questsupport.supplier_check(c, task)
        self.assertFalse(ok)
        self.assertIn("a destructible quest object", text)
        c.quest_objects = {"io_msq_grace_corruptednodule_1"}
        self.assertTrue(questsupport.supplier_check(c, task)[1])

    def test_scene_and_poi(self):
        c = client(scenes={"06_raisealarm_msq_ancientstatueleadin_ww_igc"}, poi_tags={"watchtower"})
        scene = "messages 6703, 6706, 6705 (the client asks for 06_RaiseAlarm_MSQ_AncientStatueLeadIn_WW_IGC)"
        self.assertTrue(questsupport.supplier_check(c, {"supplier": scene})[1])
        self.assertTrue(questsupport.supplier_check(c, {"supplier": "the point of interest watchtower"})[1])
        self.assertFalse(questsupport.supplier_check(c, {"supplier": "the point of interest nowhere"})[1])


class RowTest(unittest.TestCase):
    def row(self, **values):
        base = {"id": "q1", "kind": "objective", "supported": True, "blocked": [], "grants": [], "rewards": {},
                "tasks": [{"id": "T1", "type": "taskhaveitems", "supported": True, "supplier": "the recipe X"}]}
        base.update(values)
        return base

    def test_classes(self):
        c = client(objectives={"q1": {"cells": {"Title": "@t"}, "tree": [[0, "t1", []]]}}, tasks={"t1": {}}, texts={"t"},
                   recipes={"x"})
        self.assertEqual(questsupport.row_report(c, self.row())["class"], "untested")
        self.assertEqual(questsupport.row_report(c, self.row(supported=False, blocked=["a reason"]))["class"], "unavailable")
        timer = self.row(tasks=[{"id": "T1", "type": "tasktimer", "supported": True, "supplier": "the world clock"}])
        self.assertEqual(questsupport.row_report(c, timer)["class"], "client stage")
        self.assertEqual(questsupport.row_report(client(), self.row())["class"], "missing data")

    def test_offer_grant(self):
        c = client(objectives={"q1": {"cells": {}, "tree": [[0, "t1", []]]}}, tasks={"t1": {}}, recipes={"x"},
                   npcs={"npc": ["state"]}, states={"state": {"objectives": ["Q1"]}}, placed={"npc"})
        grant = [{"source": "offer", "supported": True, "detail": "NPC STATE"}]
        report = questsupport.row_report(c, self.row(grants=grant))
        self.assertEqual(report["grants"][0]["offer"], True)
        self.assertEqual(report["class"], "untested")
        c.placed = set()
        self.assertEqual(questsupport.row_report(c, self.row(grants=grant))["class"], "missing data")

    def test_reward_items(self):
        c = client(objectives={"q1": {"cells": {}, "tree": [[0, "t1", []]]}}, tasks={"t1": {}}, recipes={"x"},
                   items={"questreward_mb_meat_pouch": "resource+lootcontainer"})
        report = questsupport.row_report(c, self.row(rewards={"items": ["QuestReward_MB_Meat_Pouch x1"], "experience": 275}))
        self.assertTrue(report["rewards"]["items"]["value"][0]["lootContainer"])
        self.assertEqual(report["rewards"]["experience"]["value"], 275)


if __name__ == "__main__":
    unittest.main()
