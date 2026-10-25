"""Tests of the AST-288 rules with no game data: the meshes with no Nanite (nanite.py, class W2: a translucent slot through an
override pair or a spec) and the shadow proxies that the cells leave out (cells.SHADOW_PROXY_FILE)."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import cells, nanite, overrides  # noqa: E402

BANANA = "objects/nature/veg_trees/trees_coast/bananaplant/jav_cst_bananaplant_d.cgf"
WINDOW = "objects/crafting/props/doorswindows/jav_prp_window_a.cgf"
CHUNK = "coatgen/86a5ab0f/05wc11_etrnlpool_props_xpac_32x32_mesh_chunk_24_55_5__28236438930.cgf"
CRATE = "objects/props/crate.cgf"
GLASS_MESH = "objects/props/lantern_glass.cgf"
CUBE = "objects/default/primitive_cube.cgf"
MEDUSA = "objects/crafting/props/props_firstlight/medusas/fl_medusas_wall_interior_shadow_proxy_a.cgf"
REGION = "r_+00_+00"


class NoNanite(unittest.TestCase):
    def test_blocked(self):
        assets = {
            BANANA: {"kind": "static", "materials": ["m_banana"], "slots": ["n_leaf"], "nanite": True},
            WINDOW: {"kind": "static", "materials": ["m_frame"], "nanite": True},
            CHUNK: {"kind": "static", "materials": ["m_stone", "m_fx"], "nanite": True},
            CRATE: {"kind": "static", "materials": ["m_wood"], "nanite": True},
            GLASS_MESH: {"kind": "static", "materials": ["m_glass"], "nanite": False},   # mesh.convert gave it no Nanite
            "material:m_fx": {"kind": "material", "blend": "opaque"},
            "material:m_legacyglass": {"kind": "material", "blend": "translucent"},
        }
        specs = {"m_fx": {"parent": "fxmesh"}, "m_overridefx": {"parent": "fxmesh additive"}, "n_leaf": {"parent": "foliage"},
                 "m_wood2": {"parent": "surface"}}
        pairs = {
            overrides.pair_key(BANANA, "materials/vfx/burn.mtl"): {"slots": {"n_leaf": "m_overridefx"}},
            overrides.pair_key(WINDOW, "glass.mtl"): {"slots": {"m_frame": "m_legacyglass"}},   # no spec: the stage 1.2 blend
            overrides.pair_key(CRATE, "wood2.mtl"): {"slots": {"m_wood": "m_wood2"}},
            overrides.pair_key(CRATE, "old.mtl"): {"slots": {"m_oldslot": "m_overridefx"}},   # not a slot of the mesh: no draw
            overrides.pair_key(GLASS_MESH, "fx.mtl"): {"slots": {"m_glass": "m_overridefx"}},
        }
        out = nanite.blocked(assets, pairs, specs)
        self.assertEqual(out, {BANANA: ["m_overridefx"], CHUNK: ["m_fx"], WINDOW: ["m_legacyglass"]})

    def test_majority(self):
        assets = {
            BANANA: {"kind": "static", "materials": ["m_banana"], "nanite": True},
            CRATE: {"kind": "static", "materials": ["m_wood"], "nanite": True},
            CHUNK: {"kind": "static", "materials": ["m_fx"], "nanite": True},
        }
        specs = {"m_fx": {"parent": "fxmesh"}}
        pairs = {overrides.pair_key(BANANA, "fx.mtl"): {"slots": {"m_banana": "m_fx"}},
                 overrides.pair_key(CRATE, "fx.mtl"): {"slots": {"m_wood": "m_fx"}}}
        counts = {BANANA: {"": 2, "fx.mtl": 3}, CRATE: {"": 19880, "fx.mtl": 1}, CHUNK: {"": 4}}
        self.assertEqual(sorted(nanite.blocked(assets, pairs, specs, counts)), sorted([BANANA, CHUNK]))   # 3 of 5; an own fx slot
        self.assertEqual(nanite.minority(assets, pairs, specs, counts), {CRATE: {"translucent": 1, "all": 19881}})

    def test_parent_of(self):
        assets = {"material:m_a": {"kind": "material", "blend": "masked"}}
        self.assertEqual(nanite.parent_of("m_a", {}, assets), "legacy masked")
        self.assertEqual(nanite.parent_of("m_b", {}, assets), "legacy opaque")
        self.assertEqual(nanite.parent_of("m_a", {"m_a": {"parent": "glass"}}, assets), "glass")


class ShadowProxyCells(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        root = Path(self.folder.name)
        self.saved = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = str(root)
        assets = {
            CRATE: {"kind": "static", "materials": ["m_0000000001"], "version": 7},
            CUBE: {"kind": "static", "materials": ["m_0000000002"], "slots": ["n_editorgrey"], "version": 7},
            MEDUSA: {"kind": "static", "materials": ["m_0000000003"], "version": 7},
        }
        (root / "manifest.json").write_text(json.dumps({"format": 2, "assets": assets}))
        overrides.out_dir().mkdir(parents=True, exist_ok=True)
        (overrides.out_dir() / "pairs.json").write_text("{}")
        meshes = [CRATE, CUBE, MEDUSA]
        mesh = np.array([0, 1, 1, 2], dtype=np.uint32)
        material = np.array([0, 1, 2, 0], dtype=np.uint16)   # the cube once with the shadow proxy override, once with mud
        matrix = np.zeros((len(mesh), 3, 4))
        matrix[:, :, :3] = np.eye(3)
        matrix[:, :, 3] = [[10.0 + i, 10.0, 50.0] for i in range(len(mesh))]
        region = root / "regions" / REGION
        region.mkdir(parents=True)
        np.savez(region / "placements.npz", meshes=np.array(meshes), mesh=mesh, source=np.zeros(len(mesh), np.uint8),
                 matrix=matrix, sources=np.array(["chunk"]),
                 materials=np.array(["", "objects/default/editorprimitive_shadowproxy_a.mtl", "textures/terrain/mud/mud.mtl"]),
                 material=material, wind=np.zeros(len(mesh), bool))

    def tearDown(self):
        if self.saved is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.saved
        self.folder.cleanup()

    def test_names(self):
        self.assertTrue(cells.shadow_proxy_file("objects/default/editorprimitive_shadowproxy_a.mtl"))
        self.assertTrue(cells.shadow_proxy_file(MEDUSA))
        self.assertTrue(cells.shadow_proxy_file("objects/dungeons/dungeon_isabellalair/impostershadow_mat.mtl"))
        self.assertFalse(cells.shadow_proxy_file("objects/props/shadowbox.cgf"))
        self.assertFalse(cells.shadow_proxy_file("objects/crafting/architecture/living_structures/cottage/dmn_house_shadow.cgf"))
        self.assertFalse(cells.shadow_proxy_file(""))

    def test_left_out(self):
        report = cells.write(REGION)
        self.assertEqual(report["shadow proxies left out"], 2)   # the cube with the proxy override and the medusa wall
        self.assertEqual(report["instances"], 2)                 # the crate and the cube with the mud override


if __name__ == "__main__":
    unittest.main()
