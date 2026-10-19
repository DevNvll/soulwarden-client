"""Tests of the environment reader (converter/swc_convert/environment.py) with no game data: the spline keys and their evaluation,
the time of a curve, the decoder of the object stream values, the shapes of the volumes, and the preset paths."""
import math
import struct
import sys
import unittest
import uuid
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import environment, source  # noqa: E402
from nav_assets import Element  # noqa: E402

FLOAT = uuid.UUID("ea2c3e90-afbe-44d4-a90d-faaf79baf93d").bytes
BOOL = uuid.UUID("a0ca880c-afe4-43cb-926c-59ac48496112").bytes
VECTOR2 = uuid.UUID("3d80f623-c85c-4741-90d0-e4e66164e6bf").bytes
VECTOR3 = uuid.UUID("8379eb7d-01fa-4538-b64b-a6543b4be73d").bytes
COLOR = uuid.UUID("7894072a-9050-4f0f-901b-34b1a0d29417").bytes


def element(name: str | None, type_id: bytes = b"\x00" * 16, value: bytes | None = None, children=()) -> Element:
    e = Element()
    e.name = source.field_id(name) if name else None
    e.type_id = type_id
    e.value = value
    e.children = list(children)
    return e


class Keys(unittest.TestCase):
    def test_scalar_and_color_keys(self):
        keys = environment.parse_keys("0:5000:36,0.5:100000:18,1:5000:0,")
        self.assertEqual(keys, [[0.0, 5000.0, 36], [0.5, 100000.0, 18], [1.0, 5000.0, 0]])
        color = environment.parse_keys("0:(0.5:0.6:1):36,0.25:(1:0.7:0.4):458788,")
        self.assertEqual(color, [[0.0, [0.5, 0.6, 1.0], 36], [0.25, [1.0, 0.7, 0.4], 458788]])

    def test_exact_key_and_ends(self):
        keys = environment.parse_keys("0:5000:36,0.5:100000:18,1:5000:0,")
        self.assertEqual(environment.evaluate(keys, 0.5), 100000.0)
        self.assertEqual(environment.evaluate(keys, 0.0), 5000.0)
        self.assertEqual(environment.evaluate(keys, 1.0), 5000.0)

    def test_linear_tangents(self):
        # Flags 36 (4 | 4 << 3): linear on both sides: the Bezier of a straight segment is the straight line.
        keys = [[0.0, 0.0, 36], [0.5, 10.0, 36], [1.0, 0.0, 36]]
        self.assertAlmostEqual(environment.evaluate(keys, 0.25), 5.0, places=9)
        self.assertAlmostEqual(environment.evaluate(keys, 0.75), 5.0, places=9)

    def test_zero_tangents_at_a_key(self):
        # Flags 18 (2 | 2 << 3): zero tangents at the middle key: the curve is flat there (no overshoot above the key).
        keys = [[0.0, 0.0, 36], [0.5, 10.0, 18], [1.0, 0.0, 36]]
        values = [environment.evaluate(keys, t) for t in np.linspace(0.3, 0.7, 41)]
        self.assertLessEqual(max(values), 10.0 + 1e-9)
        self.assertAlmostEqual(environment.evaluate(keys, 0.499), 10.0, places=3)

    def test_color_keys_evaluate_each_channel(self):
        keys = [[0.0, [0.0, 1.0, 2.0], 36], [1.0, [1.0, 1.0, 0.0], 36]]
        self.assertEqual([round(v, 9) for v in environment.evaluate(keys, 0.5)], [0.5, 1.0, 1.0])

    def test_curve_time_of_the_retail_clock(self):
        # float32(hour) x float32(1/24): noon is 0.5, midnight 0.
        self.assertEqual(environment.curve_time(12.0), 0.5)
        self.assertEqual(environment.curve_time(0.0), 0.0)
        self.assertAlmostEqual(environment.curve_time(6.0), 0.25, places=7)

    def test_sample_keeps_a_repeated_name(self):
        preset = {"variables": [{"name": "Particle Brightness Multiplier", "keys": [[0.0, 1.0, 36], [1.0, 1.0, 36]], "saved": 1.0},
                                {"name": "Particle Brightness Multiplier", "keys": [[0.0, 2.0, 36], [1.0, 2.0, 36]], "saved": 2.0}]}
        values = environment.sample(preset, 12.0)
        self.assertEqual(values["Particle Brightness Multiplier"], 1.0)
        self.assertEqual(values["Particle Brightness Multiplier#1"], 2.0)


class Decode(unittest.TestCase):
    def test_values_and_repeated_fields(self):
        root = element(None, children=[
            element("DiffuseMultiplier", FLOAT, struct.pack(">f", 4.0)),
            element("OnInitially", BOOL, b"\x01"),
            element("COLOR", COLOR, struct.pack(">4f", 1.0, 0.227451, 0.035294, 1.0)),
            element("Dimensions", VECTOR3, struct.pack(">3f", 800, 550, 80)),
            element("element", VECTOR2, struct.pack(">2f", 1, 2)),
            element("element", VECTOR2, struct.pack(">2f", 3, 4)),
            element("Radius", uuid.UUID(int=7).bytes, b"\x01\x02"),
        ])
        out = environment.decode(root)
        self.assertEqual(out["DiffuseMultiplier"], 4.0)
        self.assertIs(out["OnInitially"], True)
        self.assertAlmostEqual(out["COLOR"][1], 0.227451, places=6)
        self.assertEqual(out["Dimensions"], [800.0, 550.0, 80.0])
        self.assertEqual(out["element"], [[1.0, 2.0], [3.0, 4.0]])
        self.assertEqual(out["Radius"], {"type": str(uuid.UUID(int=7)), "hex": "0102"})

    def test_field_names_have_no_case(self):
        # "Height" and "height" have one field id: the first name of the list stays.
        self.assertEqual(environment.NAMES[source.field_id("height")], "Height")


class Shapes(unittest.TestCase):
    def record(self, kind: str, fields: dict, matrix=None) -> dict:
        return {"shape": {"type": kind, "fields": fields}, "matrix": (np.eye(4) if matrix is None else matrix).tolist()}

    def test_cylinder_is_centered(self):
        m = np.eye(4)
        m[:3, 3] = (9108.0, 4266.0, 140.0)
        record = self.record("cylinder", {"Configuration": {"Radius": 350.0, "Height": 400.0}}, m)
        self.assertTrue(environment.inside(record, (8880.0, 4250.0, 140.5)))
        self.assertTrue(environment.inside(record, (9108.0, 4266.0, -59.0)))
        self.assertFalse(environment.inside(record, (9108.0, 4266.0, 341.0)))
        self.assertFalse(environment.inside(record, (9108.0 + 351.0, 4266.0, 140.0)))

    def test_rotated_box(self):
        m = np.eye(4)
        a = math.radians(90)
        m[:3, :3] = [[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]]
        record = self.record("box", {"Configuration": {"Dimensions": [800.0, 550.0, 80.0]}}, m)
        self.assertTrue(environment.inside(record, (0.0, 390.0, 0.0)))    # local x 390 after the turn
        self.assertFalse(environment.inside(record, (390.0, 0.0, 0.0)))   # local y 390: outside 275

    def test_sphere_and_polygon_prism(self):
        self.assertTrue(environment.inside(self.record("sphere", {"Configuration": {"Radius": 5.0}}), (3.0, 3.0, 2.0)))
        prism = {"Configuration": {"PolygonPrism": {"Height": 4.0, "VertexContainer": {"Vertices": {"element": [[0, 0], [10, 0], [10, 10], [0, 10]]}}}}}
        self.assertTrue(environment.inside(self.record("polygonPrism", prism), (5.0, 5.0, 2.0)))
        self.assertFalse(environment.inside(self.record("polygonPrism", prism), (5.0, 5.0, -0.5)))
        self.assertFalse(environment.inside(self.record("polygonPrism", prism), (11.0, 5.0, 2.0)))


class Paths(unittest.TestCase):
    def test_preset_paths(self):
        self.assertEqual(environment.preset_path("Libs/TimeOfDay\\zones\\everfall_a.xml"), "libs/timeofday/zones/everfall_a.xml")
        self.assertEqual(environment.preset_path("Assets/Libs/TimeOfDay//interiors/Inn.xml"), "libs/timeofday/interiors/inn.xml")

    def test_region_of_a_place(self):
        self.assertEqual(environment.region_of(8880.0, 4250.0), "r_+04_+02")
        self.assertEqual(environment.region_of(9049.0, 3966.0), "r_+04_+01")


if __name__ == "__main__":
    unittest.main()


class Lights(unittest.TestCase):
    """The compact lights of lights/REGION.json (stage E2)."""

    @staticmethod
    def record(configuration: dict, matrix=None, npc=None) -> dict:
        record = {"kind": "light", "origin": "nested", "chain": ["a.dynamicslice", "b.dynamicslice"],
                  "matrix": (np.eye(4) if matrix is None else matrix).tolist(), "fields": {"LightConfiguration": configuration}}
        if npc is not None:
            record["npc"] = npc
        return record

    def test_a_projector_points_along_its_x_axis(self):
        m = np.eye(4)
        m[:3, :3] = [[0, 0, 1], [0, 1, 0], [-1, 0, 0]]   # +X down
        m[:3, 3] = [10.0, 20.0, 5.0]
        light = environment.light_entry(self.record({"LightType": 2, "COLOR": [1.0, 0.5, 0.25], "DiffuseMultiplier": 2.5,
                                                     "ProjectorDistance": 15.0, "ProjectorFOV": 60.0, "CastShadowsSpec": 1,
                                                     "ProjectorTexture": {"BaseClass1": {"AssetPath": "textures/lights/x.dds"}}}, m))
        self.assertEqual(light["type"], "spot")
        self.assertEqual(light["x"], [0.0, 0.0, -1.0])
        self.assertEqual(light["p"], [10.0, 20.0, 5.0])
        self.assertEqual((light["range"], light["fov"], light["diffuse"]), (15.0, 60.0, 2.5))
        self.assertTrue(light["shadow"])
        self.assertEqual(light["cookie"], "textures/lights/x.dds")
        self.assertEqual(light["slice"], "b.dynamicslice")

    def test_types_ranges_and_defaults(self):
        point = environment.light_entry(self.record({"LightType": 0, "PointMaxDistance": 4.0, "PointAttenuationBulbSize": 0.6,
                                                     "CastShadowsSpec": 0xFFFFFFFF}))
        self.assertEqual((point["type"], point["range"], point["bulb"], point["shadow"]), ("point", 4.0, 0.6, False))
        self.assertEqual(point["cookie"], "")
        area = environment.light_entry(self.record({"LightType": 1, "AreaMaxDistance": 6.0, "AreaWidth": 1.5, "AreaHeight": 3.0}))
        self.assertEqual((area["type"], area["range"], area["w"], area["h"]), ("rect", 6.0, 1.5, 3.0))
        spot = environment.light_entry(self.record({"LightType": 2, "ProjectorTexture": {"BaseClass1": {"AssetPath": "textures/lights/spot_default.dds"}}}))
        self.assertEqual(spot["cookie"], "", "the default spot texture is no cookie")
        capped = environment.light_entry(self.record({"LightType": 0, "ViewDistanceCapEnabled": True, "ViewDistanceCap": 80.0}))
        self.assertEqual(capped["cap"], 80.0)

    def test_probes_and_hidden_lights_are_out(self):
        self.assertIsNone(environment.light_entry(self.record({"LightType": 3})))
        self.assertIsNone(environment.light_entry(self.record({"LightType": 0, "Visible": False})))
        off = environment.light_entry(self.record({"LightType": 0, "OnInitially": False}))
        self.assertFalse(off["on"], "a light that is off at the start stays in the data")

    def test_a_light_of_an_npc_slice_keeps_its_owner(self):
        npc = {"slice": "slices/characters/mission/npc_0422_innkeep.dynamicslice", "local": np.eye(4).tolist(), "root": [1.0, 2.0, 3.0]}
        light = environment.light_entry(self.record({"LightType": 0}, npc=npc))
        self.assertEqual(light["npc"], npc["slice"])
        self.assertEqual((light["local"], light["root"]), (npc["local"], [1.0, 2.0, 3.0]))
        self.assertNotIn("npc", environment.light_entry(self.record({"LightType": 0})))
