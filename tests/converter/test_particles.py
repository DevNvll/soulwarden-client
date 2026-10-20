"""Tests of the particle library reader (converter/swc_convert/particles.py and particle_systems.py, AST-210, docs/PARTICLES.md)
with no game data: made-up parameter texts in the form of the libraries, the spline rule, the random range, the defaults, and the
LOD bands. The numbers are made up."""
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import particles as P  # noqa: E402
from swc_convert import particle_systems as S  # noqa: E402


class ParseTests(unittest.TestCase):
    def test_base_random_and_curve(self):
        p = P.parse_value("0.6,Random=0.4,ParticleAge=(v=0.75,d=0.50196075,flags=32;t=1,v=1)")
        self.assertEqual(p["base"], 0.6)
        self.assertEqual(p["fields"]["Random"], 0.4)
        keys = p["fields"]["ParticleAge"]["keys"]
        self.assertEqual(len(keys), 2)
        self.assertEqual(keys[0], {"v": 0.75, "d": 0.50196075, "flags": 32.0})
        self.assertEqual(keys[1], {"t": 1.0, "v": 1.0})

    def test_vector_base(self):
        self.assertEqual(P.vector("y=-0.05,z=-0.125"), [0.0, -0.05, -0.125])
        self.assertEqual(P.vector(""), [0.0, 0.0, 0.0])

    def test_color_curve_keeps_nested_vectors(self):
        p = P.parse_value("(x=0.5,y=0.25,z=1),ParticleAge=(t=0.02,v=(x=1,y=1,z=1);t=0.5,v=(x=0.2,y=0.3,z=0.4)),Color=(x=0.9,y=0.1,z=0.05)")
        self.assertEqual(p["base"], {"vector": {"x": 0.5, "y": 0.25, "z": 1.0}})
        keys = p["fields"]["ParticleAge"]["keys"]
        self.assertEqual(keys[1]["v"], {"x": 0.2, "y": 0.3, "z": 0.4})
        self.assertEqual(p["fields"]["Color"], {"vector": {"x": 0.9, "y": 0.1, "z": 0.05}})

    def test_empty_first_key(self):
        # "(;t=0.5,v=1;t=1)": the first key has no fields (t 0, v 0).
        keys = P.parse_value("ParticleAge=(;t=0.5,v=1;t=1)")["fields"]["ParticleAge"]["keys"]
        self.assertEqual(keys[0], {})
        # t 0.5 is the byte 128 (0.50196): the middle sample is just before the key.
        for got, wanted in zip(P.sample(keys, 3), [0.0, 1.0, 0.0]):
            self.assertAlmostEqual(got, wanted, delta=1e-3)

    def test_missing_base_takes_the_default_of_the_attribute(self):
        params = P.params_of({"SizeY": "Random=0.3,ParticleAge=(v=0.4;t=1,v=1)"})
        self.assertEqual(P.field(params, "SizeY")["base"], 1.0)
        self.assertEqual(P.field(params, "Speed")["base"], 0.0)
        self.assertEqual(P.field(params, "Alpha")["base"], 1.0)

    def test_defaults_of_the_constructor(self):
        params = P.params_of({})
        self.assertEqual(params["BlendType"], "AlphaBased")
        self.assertEqual(params["SpawnIndirection"], "Direct")
        self.assertEqual(params["Facing"], "Camera")
        self.assertEqual(P.number(params["EmissiveLighting"]), 0.0)
        self.assertEqual(P.number(params["DiffuseLighting"]), 1.0)


class SplineTests(unittest.TestCase):
    def test_keys_are_bytes(self):
        keys = P.spline_keys([{"t": 0.1, "v": 0.7501}])
        self.assertAlmostEqual(keys[0][0], round(0.1 * 255) / 255)
        self.assertAlmostEqual(keys[0][1], round(0.7501 * 255) / 255)

    def test_linear_tangents(self):
        # flags 36: the in and the out tangents are linear (4 and 4 << 3).
        keys = P.spline_keys([{"v": 0.0, "flags": 36}, {"t": 1.0, "v": 1.0, "flags": 36}])
        self.assertAlmostEqual(P.evaluate(keys, 0.5), 0.5, places=6)

    def test_custom_tangents(self):
        # flags 9: custom in and out tangents (d out, s in); zero slopes give the smooth step 3u^2 - 2u^3.
        keys = P.spline_keys([{"v": 0.0, "d": 0.0, "flags": 9}, {"t": 1.0, "v": 1.0, "s": 0.0, "flags": 9}])
        self.assertAlmostEqual(P.evaluate(keys, 0.25), 3 * 0.25**2 - 2 * 0.25**3, places=6)

    def test_equal_times_keep_their_order(self):
        keys = P.spline_keys([{"v": 1.0}, {"t": 1.0, "v": 1.0}, {"t": 1.0, "v": 0.0}])
        self.assertEqual([k[1] for k in keys], [1.0, 1.0, 0.0])
        self.assertEqual(P.evaluate(keys, 1.0), 0.0)


class RandomTests(unittest.TestCase):
    def test_one_sided_range(self):
        self.assertEqual(P.random_range(1.0, 0.4), [0.6, 1.0])
        self.assertEqual(P.random_range(2.0, 1.5), [-1.0, 2.0])
        self.assertEqual(P.random_range(-0.15, 0.2), [-0.15, -0.12])


class LodTests(unittest.TestCase):
    def node(self, base, lods):
        return {"params": {"Enabled": "true" if base else "false"},
                "lods": [{"distance": d, "active": "1", "params": {"Enabled": "true" if on else "false"}} for d, on in lods]}

    def test_disabled_from_a_distance(self):
        bands, notes = S.lod_bands(self.node(True, [(20.0, False), (40.0, False)]))
        self.assertEqual(bands, [[0.0, 20.0]])
        self.assertEqual(notes, [])

    def test_enabled_at_all_distances(self):
        bands, _ = S.lod_bands(self.node(True, [(10.0, True)]))
        self.assertEqual(bands[0][0], 0.0)
        self.assertEqual(bands[0][1], float("inf"))

    def test_on_again_is_reported(self):
        bands, notes = S.lod_bands(self.node(True, [(20.0, False), (40.0, True)]))
        self.assertEqual(len(bands), 2)
        self.assertEqual(len(notes), 1)


class SupportTests(unittest.TestCase):
    def test_unknown_attribute_is_unsupported(self):
        self.assertEqual(P.support_status("NoSuchField")[0], "unsupported")
        self.assertEqual(P.support_status("Count")[0], "used")


if __name__ == "__main__":
    unittest.main()
