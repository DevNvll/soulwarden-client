"""Tests of the rule of the inactive helper entities (AST-261; converter/swc_convert/region.py inactive_helper) with no game data: an
entity with IsRuntimeActive 0 and the name MeshVisualizer is a helper (its meshes are not placed); an inactive entity with another
name (a part that a script, a quest, or an event activates later) and an active MeshVisualizer stay."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import region  # noqa: E402


class Field:
    def __init__(self, value):
        self.value = value


class Entity:
    """The two fields of a slice entity that the rule reads (the parsed element of source.parse has the same child())."""

    def __init__(self, name, active):
        self.fields = {region.NAME: Field(name)}
        if active is not None:
            self.fields[region.IS_RUNTIME_ACTIVE] = Field(b"\x01" if active else b"\x00")

    def child(self, field):
        return self.fields.get(field)


class InactiveHelpers(unittest.TestCase):
    def test_the_weather_box_visualizer(self):
        self.assertTrue(region.inactive_helper(Entity(b"MeshVisualizer", False)))

    def test_other_inactive_entities_stay(self):
        for name in (b"Glyph_Bridge_Top", b"durevos_door_lock_open", b"BlueFill", b"FogCard", b"circle_main_thin"):
            self.assertFalse(region.inactive_helper(Entity(name, False)), name)

    def test_active_entities_stay(self):
        self.assertFalse(region.inactive_helper(Entity(b"MeshVisualizer", True)))
        self.assertFalse(region.inactive_helper(Entity(b"MeshVisualizer", None)))   # no field: active
        self.assertTrue(region.runtime_active(Entity(b"x", None)))
        self.assertFalse(region.runtime_active(Entity(b"x", False)))


if __name__ == "__main__":
    unittest.main()
