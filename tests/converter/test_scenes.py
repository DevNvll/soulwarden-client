"""Tests of the scene reader (stage Q5, AST-240; no game data): the JSON form of an object stream element (the names of the fields, the
classes, the values, the unnamed list items), the layer assignments of a TimelineEntry, the animation layers of a timeline, and the
AchievementEvents of a spawner script."""
import struct
import sys
import unittest
import uuid
import zlib
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import scenes  # noqa: E402
from nav_assets import Element  # noqa: E402


def crc(name: str) -> int:
    return zlib.crc32(name.lower().encode()) & 0xFFFFFFFF


def leaf(name: str | None, type_id: bytes, value: bytes) -> Element:
    e = Element()
    e.name = crc(name) if name else None
    e.type_id = type_id
    e.value = value
    return e


def node(name: str | None, type_text: str, *children: Element) -> Element:
    e = Element()
    e.name = crc(name) if name else None
    e.type_id = uuid.UUID(type_text).bytes
    e.children = list(children)
    return e


def string(name: str, text: str) -> Element:
    return leaf(name, scenes.T_STRING, text.encode())


def real(name: str, value: float) -> Element:
    return leaf(name, scenes.T_FLOAT, struct.pack(">f", value))


def flag(name: str, value: bool) -> Element:
    return leaf(name, scenes.T_BOOL, bytes([1 if value else 0]))


LAYER = "f99a21ab-f403-4b90-ad52-25ee26c36f25"
CLIP_HOLDER = "15c790a0-1277-5005-9524-2f3836a526dd"
ANIM_CLIP = "b8d39822-6cab-42c0-af6e-5b53c3e243fc"
TIMELINE_CLIP = "d8d4045d-ffa2-40df-9454-476ca09c901d"
ENTITY_ID = "6383f1d3-bb27-4e6b-a49a-6409b2059eaa"


class SceneReader(unittest.TestCase):
    def test_json_names_classes_and_items(self):
        clip = node(None, ANIM_CLIP, node("BaseClass1", TIMELINE_CLIP, real("StartTime", 0.0), real("EndTime", 29.1)),
                    string("AnimationAlias", "IGC_S06_Test_Player"), flag("loop", False), real("playbackSpeed", 1.0),
                    leaf("NotInTheTable", scenes.T_FLOAT, struct.pack(">f", 2.5)),
                    node("EditorMetdata", "32502b25-0f43-4cd3-affa-9e6a541e83a7", flag("Selected", False)))
        out = scenes.tojson(clip)
        self.assertEqual(out["type"], ANIM_CLIP)
        self.assertEqual(out["BaseClass1"]["class"], "TimelineClip")
        self.assertAlmostEqual(out["BaseClass1"]["EndTime"], 29.1, places=4)
        self.assertEqual(out["AnimationAlias"], "IGC_S06_Test_Player")
        self.assertIs(out["loop"], False)
        self.assertEqual(out["#%08X" % crc("NotInTheTable")], 2.5)
        self.assertNotIn("EditorMetdata", out)

    def layer(self, layer_id: str, name: str, alias: str | None) -> Element:
        clips = []
        if alias:
            clips.append(node(None, CLIP_HOLDER, node(None, ANIM_CLIP, string("AnimationAlias", alias))))
        return node(None, LAYER, leaf("id", scenes.T_UUID, uuid.UUID(layer_id).bytes), string("name", name),
                    node("Clips", "6c2704d3-5d34-51ed-88cd-baaa248f3e30", *clips))

    def test_layers_go_to_the_assigned_entity(self):
        knight, camera = "11111111-0000-0000-0000-000000000001", "11111111-0000-0000-0000-000000000002"
        tl = scenes.tojson(node(None, "ff0b0821-a56a-4c61-bf82-f8f826bb129f", node("Layers", "8bd98c9a-ae86-5cbd-8697-f0eb31c6a44b",
                                self.layer(knight, "NPC_Knight_Anim", "IGC_Knight"), self.layer(camera, "Camera_Trans", None))))

        def assignment(layer_id: str, entity: int, name: str) -> Element:
            return node(None, "2acd7f8c-9f1a-41f3-b8b9-a64d2fc3bb09",
                        node("EntityId", ENTITY_ID, leaf("id", uuid.UUID("d6597933-47cd-4fc8-b911-63f3e2b0993a").bytes, entity.to_bytes(8, "big"))),
                        leaf("LayerId", scenes.T_UUID, uuid.UUID(layer_id).bytes), string("LayerName", name))
        entry = scenes.tojson(node(None, "e14cf7d7-9f3b-4aae-a320-a284dffd02ee", node("LayerEntityAssignments",
                                   "f376e9e0-5d5c-5bd7-b35f-a38b5ff13118", assignment(knight, 0x6E0D3ED04AC, "NPC_Knight_Anim"),
                                   assignment(camera, 0x5237C5CE52, "Camera_Trans"))))
        self.assertEqual(scenes.assigned(entry), {knight: "000006e0d3ed04ac", camera: "0000005237c5ce52"})
        layers = scenes.clips_of_timeline(tl, entry)
        self.assertEqual(layers, [{"layer": "NPC_Knight_Anim", "entityId": "000006e0d3ed04ac", "aliases": ["IGC_Knight"]}])
        wanted = scenes.wanted_of([{"entity": "NPC_Knight", "id": "000006e0d3ed04ac", "mesh": ""},
                                   {"entity": "IGC_Camera", "id": "0000005237c5ce52", "mesh": ""}],
                                  [{"entity": "Timeline - Active_IGC", "asset": "", "layers": layers}])
        self.assertEqual([w["clips"] for w in wanted], [[{"alias": "IGC_Knight", "caf": ""}], []])

    def test_achievement_events_of_a_spawner(self):
        event = node(None, "9217f896-b2b1-4812-9ee4-e7789fcb3ec7", string("debugName", "Start Cutscene"),
                     string("RequiredAchievementConditional", "Start_IGC_Aelstrom_WW && !Watched_IGC_Aelstrom_WW"),
                     flag("RunEventsOnConditionalSatisfied", True), flag("RunEventsOnEnterAOI", True), real("eventDelayDuration", 0.0),
                     node("entityEvents", "2a3d1e1e-a69f-5860-8934-00c3bfe920cc", node(None, "82897793-be85-48cd-bac9-a1ad3b487133",
                          flag("applyOnChildren", True), node("entityNames", "0b66e343-c513-5eb3-b152-770c4628bb73",
                          node(None, "1611905c-c51f-4ff2-adc1-a553763826ba", string("m_string", "TriggerCutscene"))))))
        component = node(None, "3d939617-3f24-41dc-a05f-2f298e8f4cb8", node("m_scriptDataContainer", "c9ccb4fb-44b8-4be8-bca8-4909c1c22b82",
                         string("m_scriptName", "AchievementEvents"), node("m_scriptData", "59eaf609-9d81-561c-a744-77f737cf92c4",
                         node(None, "54cb00f3-c64e-48b9-9779-859545bf46b3", node("events", "777e8fda-6a1b-55fd-baf9-8aff3c0b7397", event)))))
        events = scenes.script_events(component)
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["condition"], "Start_IGC_Aelstrom_WW && !Watched_IGC_Aelstrom_WW")
        self.assertTrue(events[0]["onEnterAoi"])
        self.assertEqual(events[0]["entityEvents"][0]["entities"], ["TriggerCutscene"])

    def test_bink_header(self):
        # The header of TempestRising (quests-18e cinematic ANSWERS.md Q2): 3199 frames, 1920 x 1080, 30000/1001.
        head = b"KB2j" + struct.pack("<8I", 190681932, 3199, 400000, 3199, 1920, 1080, 30000, 1001)
        original = (scenes.source.has, scenes.source.read)
        scenes.source.has, scenes.source.read = (lambda path: True), (lambda path: head)
        try:
            video = scenes.bink_header("lyshineui/videos/tempestrising.bk2")
        finally:
            scenes.source.has, scenes.source.read = original
        self.assertEqual((video["frames"], video["width"], video["height"], video["rate"]), (3199, 1920, 1080, [30000, 1001]))
        self.assertAlmostEqual(video["seconds"], 106.739967, places=5)


if __name__ == "__main__":
    unittest.main()
