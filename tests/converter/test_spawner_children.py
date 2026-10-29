"""The objects of the interaction data that a spawner makes (swc_convert/objects.spawner_child, C-7, ut567) with synthetic data (no game
data): the key SPAWNER/ENTITY of format 4 (a quest child, a mission board, a person child) and a person with clientChild skip the
placement actor rule; a placement UUID key keeps it (placement_actor gives its actor)."""
import unittest

from swc_convert.objects import placement_actor, spawner_child


class SpawnerChildTest(unittest.TestCase):
    def test_children(self):
        quest = {"key": "70c81ed1-3815-480f-524c-636e9b381acf/4b29560e87692396", "kind": "gathering",
                 "questChild": {"spawner": "70c81ed1-3815-480f-524c-636e9b381acf", "variant": "17_ZQUS3_Sarchophagus01"}}
        self.assertTrue(spawner_child(quest))
        self.assertTrue(spawner_child({"key": "fdfdda75-cfe3-00d1-0a35-2803625a06c6/155ddb9c53e98755~1", "kind": "missionBoard"}))
        self.assertTrue(spawner_child({"key": "43be6fe0-03d9-39eb-2c93-b43f98e6dffa", "kind": "npc", "person": {"clientChild": True}}))

    def test_placement(self):
        entry = {"key": "70c81ed1-3815-480f-524c-636e9b381acf", "kind": "gathering", "person": {"clientChild": False}}
        self.assertFalse(spawner_child(entry))
        # The rule of a placement: XOR the two halves, reverse, the low 3 bits of the first byte 3, then 8 zero bytes.
        self.assertEqual(placement_actor(entry["key"]).hex(), "c3522da3bf7d84220000000000000000")
        with self.assertRaises(ValueError):
            placement_actor(entry["key"] + "/4b29560e87692396")


if __name__ == "__main__":
    unittest.main()
