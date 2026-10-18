"""Tests of the materials hook of the import pipeline (unreal/SoulwardenClient/Scripts/swc_materials.py) with no engine:
a stub replaces the module `unreal`. A key with no spec is skipped; a key with a spec is applied once."""
import json
import os
import sys
import tempfile
import types
import unittest
from pathlib import Path

sys.dont_write_bytecode = True


class Stub(types.ModuleType):
    """The module `unreal`: each attribute is a callable stub that records its calls."""

    def __init__(self):
        super().__init__("unreal")
        self.calls = []

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return Recorder(self, name)


class Recorder:
    def __init__(self, stub, name):
        self.stub, self.name = stub, name

    def __getattr__(self, name):
        return Recorder(self.stub, f"{self.name}.{name}")

    def __call__(self, *args, **kwargs):
        self.stub.calls.append(self.name)
        return Recorder(self.stub, f"{self.name}()")


stub = Stub()
sys.modules["unreal"] = stub
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "unreal" / "SoulwardenClient" / "Scripts"))
import swc_materials  # noqa: E402

SPEC = {"parent": "foliage", "textures": {}, "vectors": {"DiffuseColor": [0.5, 0.5, 0.5, 1.0]}, "scalars": {},
        "two sided": True, "cutoff": 0.23}


class Plan(unittest.TestCase):
    def test_key_with_no_spec_is_skipped(self):
        self.assertEqual(swc_materials.plan(["material:m_0000000001", "m_0000000002"], {}, {}, True), [])

    def test_manifest_key_and_name(self):
        specs = {"m_00000000aa": SPEC}
        self.assertEqual(swc_materials.plan(["material:m_00000000aa", "material:m_00000000bb"], specs, {}, True),
                         ["m_00000000aa"])

    def test_stamp(self):
        specs = {"m_00000000aa": SPEC}
        state = {"materials:m_00000000aa": swc_materials.spec_stamp(SPEC)}
        self.assertEqual(swc_materials.plan(["m_00000000aa"], specs, state, False), [])   # up to date
        self.assertEqual(swc_materials.plan(["m_00000000aa"], specs, state, True), ["m_00000000aa"])   # import_content
        changed = {"m_00000000aa": {**SPEC, "cutoff": 0.25}}
        self.assertEqual(swc_materials.plan(["m_00000000aa"], changed, state, False), ["m_00000000aa"])


class ApplySpecs(unittest.TestCase):
    def test_no_spec_touches_no_asset(self):
        # The hook of import_content.py with keys that have no spec: no call of the engine, the state does not change.
        with tempfile.TemporaryDirectory() as content:
            os.makedirs(os.path.join(content, "materials"))
            with open(os.path.join(content, "materials", "instances.json"), "w") as handle:
                json.dump({"version": 1, "instances": {"m_00000000aa": SPEC}}, handle)
            state = {"material:m_0000000001": "1"}
            stub.calls.clear()
            report = swc_materials.apply_specs(["material:m_0000000001", "material:m_0000000002"], dict(state),
                                               content=content)
            self.assertEqual(stub.calls, [])
            self.assertEqual(report["instances"]["applied"], 0)
            self.assertEqual(report["failed"], [])

    def test_no_spec_files(self):
        with tempfile.TemporaryDirectory() as content:
            stub.calls.clear()
            report = swc_materials.apply_specs(["material:m_0000000001"], {}, content=content)
            self.assertEqual((stub.calls, report["instances"]["applied"]), ([], 0))


if __name__ == "__main__":
    unittest.main()
