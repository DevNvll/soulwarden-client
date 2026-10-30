"""The scenes of the quest encounter stages (swc_convert/scenes.encounter_triggers, C-13 step 1b D) with synthetic data (no game data): a
stage with a cutscene and an activate action on a trigger entity of the encounter slice gives one entry with the stage slot, the CRC, the
scene slice, and the world matrix of the trigger (the placement of the recipe times the matrix of the entity); a trigger with no
m_triggerOnActivate, an other cutscene name, or a deactivate action gives none."""
import unittest

from swc_convert import scenes

NAME = "06A_MSQ_21_MedeaShowdownIntro_IGC"


def trigger(entity_id, cutscene, on_activate=True):
    facet = {"m_preloadCutsceneName": cutscene, "m_sliceAsset": {"path": "Slices/IGC/Test/" + cutscene + ".dynamicslice"},
             "m_triggerOnActivate": on_activate}
    world = [[1.0, 0.0, 0.0, 2.0], [0.0, 1.0, 0.0, -3.0], [0.0, 0.0, 1.0, 0.5], [0.0, 0.0, 0.0, 1.0]]
    return {"id": entity_id, "name": "IGC_Spawner_" + entity_id[-2:], "world": world,
            "components": [{"class": scenes.TRIGGER_CUTSCENE, "BaseClass1": {"m_clientFacetPtr": facet}}]}


def recipe(stages):
    # The rotation: 90 degrees about Z (x y z w).
    return {"key": "root/1", "asset": "b34454f99a5850e5b6cc83ac43f43ee6", "mode": "QuestMedea", "map": "QuestMedea", "slice": "s.dynamicslice",
            "position": [100.0, 200.0, 50.0], "rotation": [0.0, 0.0, 0.7071068, 0.7071068], "stages": stages}


def stage(slot, cutscene, entity, event=0):
    return {"slot": slot, "name": f"Phase {slot}", "objective": {"type": "CutsceneFinishedObjective", "cutscene": cutscene},
            "startActions": [{"type": "EntityEventAction", "entity": entity, "event": event, "localPlayerOnly": True}]}


ENTITIES = [trigger("00000000000000aa", NAME), trigger("00000000000000bb", NAME, on_activate=False), trigger("00000000000000cc", "Other_IGC")]


class EncounterTriggerTest(unittest.TestCase):
    def run_triggers(self, stages):
        return scenes.encounter_triggers([recipe(stages)], read=lambda path: ENTITIES)

    def test_activate_stage_gives_its_scene(self):
        out = self.run_triggers([stage(5, NAME, 0xAA)])
        self.assertEqual(len(out), 1)
        entry = out[0]
        self.assertEqual((entry["slot"], entry["crc"], entry["asset"]), (5, "a921416f", "B34454F9-9A58-50E5-B6CC-83AC43F43EE6"))
        self.assertEqual(entry["slice"], "slices/igc/test/06a_msq_21_medeashowdownintro_igc.dynamicslice")
        # The entity at (2, -3, 0.5) in the slice, turned 90 degrees: (3, 2, 0.5) from the placement.
        self.assertAlmostEqual(entry["world"][0][3], 103.0, places=4)
        self.assertAlmostEqual(entry["world"][1][3], 202.0, places=4)
        self.assertAlmostEqual(entry["world"][2][3], 50.5, places=4)

    def test_no_entry_for_other_triggers(self):
        self.assertEqual(self.run_triggers([stage(5, NAME, 0xBB)]), [])     # no m_triggerOnActivate
        self.assertEqual(self.run_triggers([stage(5, NAME, 0xCC)]), [])     # another cutscene
        self.assertEqual(self.run_triggers([stage(5, NAME, 0xAA, event=1)]), [])   # a deactivate action
        self.assertEqual(self.run_triggers([stage(5, NAME, 0xDD)]), [])     # no entity


if __name__ == "__main__":
    unittest.main()
