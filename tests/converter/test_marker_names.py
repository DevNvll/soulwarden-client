"""Tests of creature.read_marker_names (stage WF7: the marker type of a creature slice for the AiMarker plate of the UI) with
no game data: a client facet of a MarkerComponent made in the test."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import creature, source  # noqa: E402


def facet(fields: dict[int, bytes]) -> source.Element:
    out = source.Element()
    out.type_id = creature.MARKER_FACET_TYPE
    for name, value in fields.items():
        child = source.Element()
        child.name, child.value = name, value
        out.children.append(child)
    return out


class MarkerNames(unittest.TestCase):
    def test_type_and_override(self):
        names = creature.read_marker_names(facet({creature.F_MARKER_TYPE: b"Wolf", creature.F_CREATURE_TYPE_OVERRIDE: b"Elite"}))
        self.assertEqual(names, {"type": "Wolf", "creatureTypeOverride": "Elite"})

    def test_empty_texts_are_left_out(self):
        # The wolf slice has an empty m_creatureTypeOverride.
        names = creature.read_marker_names(facet({creature.F_MARKER_TYPE: b"Wolf", creature.F_CREATURE_TYPE_OVERRIDE: b""}))
        self.assertEqual(names, {"type": "Wolf"})
        self.assertEqual(creature.read_marker_names(facet({})), {})

    def test_field_ids(self):
        self.assertEqual(creature.F_CREATURE_TYPE_OVERRIDE, 0x1369E5E3)


if __name__ == "__main__":
    unittest.main()
