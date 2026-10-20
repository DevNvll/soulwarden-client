"""Tests of the manifest (no game data): a new conversion of a mesh keeps the collision record of collision.py (AST-206)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import manifest  # noqa: E402

KEY = "objects/crafting/architecture/ruins_structures/jav_struc_ruins_column_c_cap_ai.cgf"
COLLISION = {"file": "collision/objects/a.swccol", "hash": "0123456789abcdef"}


class KeptFields(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.old = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = self.folder.name

    def tearDown(self):
        if self.old is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.old
        self.folder.cleanup()

    def entry(self) -> dict:
        return manifest.load()["assets"][KEY]

    def test_new_conversion_keeps_the_collision(self):
        manifest.add_many({KEY: {"kind": "static", "file": "a.gltf", "hash": "1"}})
        doc = manifest.load()
        doc["assets"][KEY]["collision"] = COLLISION
        manifest.save(doc)
        manifest.add_many({KEY: {"kind": "static", "file": "a.gltf", "hash": "2"}})
        self.assertEqual(self.entry()["collision"], COLLISION)
        self.assertEqual(self.entry()["hash"], "2")

    def test_a_new_kind_drops_the_collision(self):
        manifest.add_many({KEY: {"kind": "static", "file": "a.gltf", "collision": COLLISION}})
        manifest.add_many({KEY: {"kind": "empty"}})
        self.assertNotIn("collision", self.entry())

    def test_a_new_record_wins(self):
        manifest.add_many({KEY: {"kind": "static", "file": "a.gltf", "collision": COLLISION}})
        new = {"file": "collision/objects/b.swccol", "hash": "fedcba9876543210"}
        manifest.add_many({KEY: {"kind": "static", "file": "a.gltf", "collision": new}})
        self.assertEqual(self.entry()["collision"], new)


if __name__ == "__main__":
    unittest.main()
