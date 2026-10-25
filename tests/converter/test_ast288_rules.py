"""Tests of the AST-288 rules with no game data: the meshes with no Nanite (nanite.py, class W2: a translucent slot through an
override pair or a spec) and the shadow proxies that the cells leave out (cells.SHADOW_PROXY_FILE)."""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter" / "vendor" / "soulwarden"))
from swc_convert import cells, cover, nanite, overrides  # noqa: E402

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

    def test_state_override(self):
        self.assertTrue(cells.state_override("objects/props/cutlasskeys/cutlasskeys_pvp_pylon_light_00.mtl"))
        self.assertTrue(cells.state_override("materials/vfx/cutlasskeys_pvp_pylon_barrier.mtl"))
        self.assertFalse(cells.state_override("objects/pvp/pylon_stone_mat.mtl"))
        self.assertFalse(cells.state_override(""))

    def test_names(self):
        self.assertTrue(cells.shadow_proxy_file("objects/default/editorprimitive_shadowproxy_a.mtl"))
        self.assertTrue(cells.shadow_proxy_file(MEDUSA))
        self.assertTrue(cells.shadow_proxy_file("objects/dungeons/dungeon_isabellalair/impostershadow_mat.mtl"))
        self.assertFalse(cells.shadow_proxy_file("objects/props/shadowbox.cgf"))
        self.assertFalse(cells.shadow_proxy_file("objects/crafting/architecture/living_structures/cottage/dmn_house_shadow.cgf"))
        self.assertFalse(cells.shadow_proxy_file(""))

    def test_shadow_only(self):
        report = cells.write(REGION)   # SHADOW_ONLY is on
        self.assertEqual(report["shadow proxies placed shadow-only"], 2)
        self.assertEqual(report["instances"], 4)
        flags = {}
        b = (Path(os.environ["SWC_CONTENT"]) / "regions" / REGION / "cells.bin").read_bytes()
        count, o = struct.unpack_from("<I", b, 8)[0], 12
        for _ in range(count):
            size = struct.unpack_from("<H", b, o)[0]; o += 2
            folder = b[o:o + size].decode(); o += size
            slots = struct.unpack_from("<H", b, o)[0]; o += 2
            for _ in range(2 * slots):
                size = struct.unpack_from("<H", b, o)[0]; o += 2 + size
            flags.setdefault(folder, []).append(b[o]); o += 1
        from swc_convert import manifest
        assets = manifest.load()["assets"]
        self.assertEqual(flags[assets[MEDUSA]["unreal"]], [cells.FLAG_SHADOW_ONLY])
        self.assertEqual(sorted(flags[assets[CUBE]["unreal"]]), [0, cells.FLAG_SHADOW_ONLY])   # the mud cube and the proxy cube
        self.assertEqual(flags[assets[CRATE]["unreal"]], [0])

    def test_left_out(self):
        cells.SHADOW_ONLY = False   # the rule before the C++ step
        try:
            report = cells.write(REGION)
        finally:
            cells.SHADOW_ONLY = True
        self.assertEqual(report["shadow proxies left out"], 2)   # the cube with the proxy override and the medusa wall
        self.assertEqual(report["instances"], 2)                 # the crate and the cube with the mud override


class EntryNoNanite(unittest.TestCase):
    """The entry of a minority pair (no-nanite.json "minority") gets flag bit 2; the other entry of the mesh keeps Nanite."""

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        root = Path(self.folder.name)
        self.saved = os.environ.get("SWC_CONTENT")
        os.environ["SWC_CONTENT"] = str(root)
        assets = {CRATE: {"kind": "static", "materials": ["m_wood"], "nanite": True, "version": 7}}
        (root / "manifest.json").write_text(json.dumps({"format": 2, "assets": assets}))
        overrides.out_dir().mkdir(parents=True, exist_ok=True)
        (overrides.out_dir() / "pairs.json").write_text(json.dumps({overrides.pair_key(CRATE, "fx.mtl"): {"slots": {"m_wood": "m_fx"}}}))
        (root / "materials").mkdir()
        (root / "materials" / "instances.json").write_text(json.dumps({"instances": {"m_fx": {"parent": "fxmesh"}}}))
        (root / "materials" / "no-nanite.json").write_text(json.dumps({"meshes": {}, "minority": {CRATE: {"translucent": 1, "all": 3}}}))
        matrix = np.zeros((3, 3, 4))
        matrix[:, :, :3] = np.eye(3)
        matrix[:, :, 3] = [[10.0 + i, 10.0, 50.0] for i in range(3)]
        region = root / "regions" / REGION
        region.mkdir(parents=True)
        np.savez(region / "placements.npz", meshes=np.array([CRATE]), mesh=np.zeros(3, np.uint32), source=np.zeros(3, np.uint8),
                 matrix=matrix, sources=np.array(["chunk"]), materials=np.array(["", "fx.mtl"]),
                 material=np.array([0, 0, 1], np.uint16), wind=np.zeros(3, bool))

    def tearDown(self):
        if self.saved is None:
            os.environ.pop("SWC_CONTENT", None)
        else:
            os.environ["SWC_CONTENT"] = self.saved
        self.folder.cleanup()

    def test_flag(self):
        report = cells.write(REGION)   # ENTRY_NO_NANITE is on
        self.assertEqual(report["entries with no Nanite"], 1)
        self.assertEqual(report["entries"], 2)


class RefractionOverride(unittest.TestCase):
    """AST-290: an override material with a Meshparticle sub-material that has %REFRACTION is left out (cells.refraction_override)."""

    def test_flag(self):
        subs = {"subtle.mtl": [{"Shader": "Meshparticle", "StringGenMask": "%REFRACTION"},
                               {"Shader": "Meshparticle", "StringGenMask": "%DIFFUSE_MAP_3%DIFFUSE_MAP_4"}],
                "use.mtl": [{"Shader": "Meshparticle", "StringGenMask": "%USE_REFRACTION"}],
                "illum.mtl": [{"Shader": "Illum", "StringGenMask": "%REFRACTION"}]}
        saved = cells.materials.load_mtl
        cells.materials.load_mtl = lambda mtl: (None, subs[mtl])
        cells.refraction_override.cache_clear()
        try:
            self.assertTrue(cells.refraction_override("subtle.mtl"))
            self.assertFalse(cells.refraction_override("use.mtl"))     # another flag
            self.assertFalse(cells.refraction_override("illum.mtl"))   # another shader
            self.assertFalse(cells.refraction_override(""))
            self.assertFalse(cells.refraction_override("missing.mtl"))  # does not decode
        finally:
            cells.materials.load_mtl = saved
            cells.refraction_override.cache_clear()


class CoverPlaceholders(unittest.TestCase):
    def test_left_out(self):
        self.assertTrue(cover.left_out("objects/nature/veg_grass/grass_forest/emptyspace/jav_fst_emptyspace_a.cgf"))
        self.assertTrue(cover.left_out("Objects/Nature/Veg_Grass/Grass_Forest/EmptySpace/jav_fst_emptyspace_a.cgf"))
        self.assertFalse(cover.left_out("objects/nature/veg_grass/grass_forest/jav_fst_grass_a.cgf"))
        self.assertFalse(cover.left_out(None))


if __name__ == "__main__":
    unittest.main()
