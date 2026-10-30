"""The station rule of the client interaction templates (swc_convert/actions.interactions, AST-244) with synthetic data (no game data):
a camp station of a world place stays in with its crafting bound and its one text key, a player camp (kind camp) stays out, and a
settlement station keeps its bounds and options as they are. C-13 step 1b: an exit portal of instanceObjects (kind dungeonExit) is in
with its card name; a gathering instance object stays out."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from swc_convert import actions


def option(name, action):
    return {"name": name, "id": len(name), "action": action, "committed": True, "delay": 0.0, "hold": 0.5}


def bound(*options):
    return {"slot": 4, "component": "c1", "entity": "e1", "position": [0.0, 0.0, 0.0], "rangeSq": 9.0, "capacity": 1,
            "options": [[index, "e2"] for index in options]}


def template(slice_path, kind, key, *bounds):
    out = {"slice": slice_path, "asset": "a1", "sub": 2, "kind": kind, "root": "r1", "slots": {"position": 2},
           "permissionRule": 1, "bounds": list(bounds)}
    if key:
        out["assembly"] = {"key": key, "fee": False}
    return out


DATA = {
    "options": [option("ApplyAllResources", 16), option("CancelStructurePlan", 2), option("OpenCraftingOption", 20),
                option("SetSecurityLevelOption", 42), option("CampOption", 21), dict(option("ExitDungeon", 74), committed=False)],
    "templates": [
        template("slices/pois/interactions/int_watchtowercampt1.dynamicslice", "craftingStation", "Camp1", bound(0, 1), bound(2)),
        template("slices/structures/camps/permanentcamp_master.dynamicslice", "camp", "Camp5", bound(4)),
        template("slices/settlement/settlement_station_cooking_t3.dynamicslice", "craftingStation", "Cooking3", bound(2, 3)),
        template("slices/interactobjects/portalstone/interactobject_portalstone.dynamicslice", "dungeonExit", "", bound(5)),
        template("slices/gatherables/master_quest_instanced.dynamicslice", "gathering", "", bound(0)),
    ],
    "instanceObjects": {"slices/interactobjects/portalstone/interactobject_portalstone.dynamicslice": 3,
                        "slices/gatherables/master_quest_instanced.dynamicslice": 4},
    "spawned": [{"template": 0}, {"template": 1}, {"template": 2}],
    "fixtures": {},
    "lootContainers": {},
}

SLICES = {
    "slices/pois/interactions/int_watchtowercampt1.dynamicslice": b"\x00@HA\x00@ui_watcherscampfire\x00@_\x00",
    "slices/settlement/settlement_station_cooking_t3.dynamicslice": b"\x00@CookingT3_MasterName\x00",
}


class StationRuleTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        root = Path(self.folder.name)
        (root / "gamedata").mkdir()
        (root / "gamedata" / "interaction-data.json").write_text(json.dumps(DATA))
        (root / "player").mkdir()
        patches = [mock.patch.object(actions, "gamedata_dir", lambda: root / "gamedata"),
                   mock.patch.object(actions.player, "player_dir", lambda: root / "player"),
                   mock.patch.object(actions.source, "read", lambda path: SLICES[path]),
                   mock.patch.object(actions.BoundShapes, "shape", lambda self, path, entity=None: None)]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        actions.interactions()
        self.templates = {t["name"]: t for t in json.loads((root / "player" / "interactions.json").read_text())["templates"]}

    def tearDown(self):
        self.folder.cleanup()

    def test_world_camp_stays_with_its_crafting_bound(self):
        camp = self.templates["station:int_watchtowercampt1"]
        self.assertEqual(camp["assembly"], {"key": "Camp1", "fee": False})
        self.assertEqual(camp["interactName"], "@ui_watcherscampfire")
        self.assertEqual([[o["name"] for o in b["options"]] for b in camp["bounds"]], [["OpenCraftingOption"]])

    def test_player_camp_stays_out(self):
        self.assertFalse(any("permanentcamp" in name for name in self.templates))

    def test_settlement_station_is_unchanged(self):
        station = self.templates["station:settlement_station_cooking_t3"]
        self.assertEqual(station["interactName"], "@CookingT3_MasterName")
        self.assertEqual([[o["name"] for o in b["options"]] for b in station["bounds"]], [["OpenCraftingOption", "SetSecurityLevelOption"]])

    def test_exit_portal_is_a_fixture_with_its_card_name(self):
        portal = self.templates["exit:interactobject_portalstone"]
        self.assertEqual(portal["interactName"], "@ui_dungeon_exit")
        self.assertEqual([[(o["name"], o["action"], o["committed"], o["hold"]) for o in b["options"]] for b in portal["bounds"]],
                         [[("ExitDungeon", 74, False, 0.5)]])
        self.assertNotIn("assembly", portal)

    def test_gathering_instance_object_stays_out(self):
        self.assertFalse(any("master_quest_instanced" in name for name in self.templates))


if __name__ == "__main__":
    unittest.main()
