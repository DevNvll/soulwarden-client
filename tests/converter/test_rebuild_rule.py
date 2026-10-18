"""Tests of the M3 rebuild rule (the materials track; docs/MATERIALS.md) with no game data: a mesh of
materials/rule7-meshes.json converts with rule 7 when the caller asks for the default rule; another mesh, or an
explicit rule, keeps its rule; no file means no change."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import mesh  # noqa: E402


class RebuildRule(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.previous = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = self.folder.name
        mesh.rule7_meshes.cache_clear()

    def tearDown(self):
        mesh.rule7_meshes.cache_clear()
        if self.previous is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.previous
        self.folder.cleanup()

    def test_no_file(self):
        self.assertEqual(mesh.rule_for("objects/a.cgf"), mesh.VERSION)

    def test_rebuild_set(self):
        folder = Path(self.folder.name) / "materials"
        folder.mkdir()
        (folder / "rule7-meshes.json").write_text(json.dumps({"meshes": ["objects/a.cgf"]}))
        self.assertEqual(mesh.rule_for("objects/a.cgf"), mesh.RULE_COLORS)
        self.assertEqual(mesh.rule_for("Objects/A.cgf"), mesh.RULE_COLORS)   # the game paths are lower case
        self.assertEqual(mesh.rule_for("objects/b.cgf"), mesh.VERSION)
        self.assertEqual(mesh.rule_for("objects/a.cgf", 5), 5)   # an explicit older rule stays


if __name__ == "__main__":
    unittest.main()
