"""The instances of the world level (stage 1.3): the placements of a region in Unreal space, by streaming cell.

Input: the census placements of a region (census.load_placements) and the manifest (the Unreal folder of each mesh).
Output: `regions/NAME/cells.bin`, the file of SwcEditorLibrary.SpawnInstanceActors: one actor for each cell of
CELL_M x CELL_M meters with one instanced static mesh component for each mesh. World Partition streams the actors by
their bounds.

File (little-endian): "SWCI", u32 version 3, u32 entry count, the entries (u16 length + UTF-8 Unreal folder of the
mesh, u16 slot count, and for each slot: the slot name and the material key that replaces its material, each u16
length + UTF-8, then u8 flags: bit 0 the wind, bit 1 a decal companion), u32 actor count, then each actor: u16
length + UTF-8 label, f64 x 3 location (cm), u32 group count, then each group: u32 entry index, u32 instance count, and
each instance: f64 x 3 translation (cm), f32 x 4 rotation (Unreal quaternion x y z w), f32 x 3 scale. The transforms
follow the space map of stage 1.1 (space.py).
An entry is a group (mesh, material override, wind) (P4.1, overrides.py): the slots of a placement with an override get
the material instances of the override (version 1 had only the mesh). The wind is the receiveWind flag of the render
node of the placement (region.py): the component of an entry with wind gets the custom primitive data 0 "SwcWind" = 1,
for the wind of the vegetation material (E1; version 2 had no flags).
A decal companion (the materials track, step c): Nanite does not draw deferred decal materials, so the %DECAL subsets of
a mesh are a small non-Nanite mesh of their own (manifest: the main mesh has "decals": the key of the companion). Each
group of such a mesh is followed by a group of its companion with the same transforms: no wind, no shadow, no collision.
The slot map of a companion is the "decal slots" of the override pair (AST-275 C11, overrides.py): the override's decal keys; a
group whose override has no decal at the companion slots has no companion. Before C11 the companion had no slot map.
"""
from __future__ import annotations

import functools
import json
import re
import struct

import numpy as np

from . import census, env, manifest, materials, overrides

CELL_M = 256.0
# Meshes that the cells leave out until their rule is done (the decision of the user for AST-266, 2026-10-22: "remove until fixed").
# objects/vfx/misc/snow/snow_blown_01.cgf: the blown-snow sheets of the Ice Castle (42 placements, r_+03_+03 and r_+04_+03; the override
# objects/volumetrics/wind_snow_01_mat.mtl, a GeometryBeam fog sheet). The mesh is not in the beam folders of mesh rule 8 and its pair
# has no slot map, so it drew as opaque white Nanite meshes over the icebergs. The fix (mesh rule 8, pairs version 3, M_SwcBeam) is in
# AST-265 (b); it brings these sheets back.
# The fog sheets and spheres of AST-265 that mesh rule 8 makes drawable (their own GeometryBeam materials; empty with rule 6): AST-265
# brings them back with the fog rule.
LEFT_OUT_MESHES = ("objects/vfx/misc/snow/snow_blown_01.cgf", "objects/volumetrics/jav_fogsheet_a_cross.cgf",
                   "objects/volumetrics/jav_fogsphere_a.cgf")
# The fog of an override (AST-275): a Geometryfog shader, or a GeometryBeam material of the fog folders (the fog sheets of AST-265). A
# mesh with no material of its own and a fog override has no proved parent: not drawn (all 242 placements of the fog sphere
# objects/vfx/mesh/generic/fogsphere_01.cgf, a white opaque sphere of 33 m before, near 8836 5130; the fog beams jav_fogbeam_a and b).
FOG_MATERIAL_FOLDERS = ("objects/volumetrics/", "objects/climax/materials/volumetrics/")
# The fog cards (fx_fogcard_01 to 04: AST-265). Before AST-275 the cells left out every mesh named fx_*: also the light ray cards,
# the waterfall kits (an Illum surface), the alpha planes, and the portal spheres (1,083 placements of the playable regions).
FOG_CARD = "fx_fogcard"
# C4 (AST-275): the decal companions of the meshes with only decal subsets (the moss layers of the crater cliffs, the Dunwood floor
# decals). The first check drew them pale gray and glossy over the whole cliff: that check imported the companions before their dm_
# decal instances existed ("slots with no material"), so each slot kept its glTF import material. With the instances first
# (apply-materials --part decals, then the import) and the override decals of C11 (the green cliffs_mossy moss), they match retail.
PLACE_DECAL_ONLY = True
# The shaders of an override that the client draws on a mesh with no material of its own: the surface parents (M_SwcSurface, the
# foliage, the glass) and the beam parent of mesh rule 8 (M_SwcBeam, AST-207). NoDraw sub-materials have no slot.
EFFECT_PARENTS = frozenset({"illum", "vegetation", "glass", "geometrybeam", "nodraw"})
# C8 (AST-275): the effect shaders with their own parents (M_SwcFxMesh, M_SwcFxMeshAdd, M_SwcMeshParticle; material_records stage
# "c8"). On since the C8 deploy (2026-10-24). The overrides with an option that the parents do not have (FX_UNSUPPORTED: the procedural
# gradients, the FX sine) stay out.
C8 = True
C8_PARENTS = frozenset({"fxmeshadvancedtransp", "meshparticle"})
FX_UNSUPPORTED = ("%USE_PROC_GRADS", "%VERT_DEFORM_SINWAVE")
# State objects stay out with C8 (the server shows one state): the glyph bridges (objects/vfx/mesh/structures/glyph_bridge_*, the
# glyph mechanic; the retail still at a bridge, 2026-10-24: the character stands in the water), the other objects of the Azoth glyph
# mechanic (the override materials azoth_*, glow_procedural_azoth_*, azothglyphsphere_*), and the capture point cylinders
# (vfx_cylinder_*_capturepoint_alpha: six owner states at each place, blue, white, red, factions 1 to 3; the retail still at a capture
# point shows none). C8_LEFT_OUT: words of the mesh path; C8_LEFT_OUT_MATERIALS: words of the override material path.
C8_LEFT_OUT = ("glyph_bridge", "_capturepoint_")
C8_LEFT_OUT_MATERIALS = ("azoth",)
# State objects with a material of their own (AST-288, the decision of the orchestrator 2026-10-25): a placement whose override material
# path has one of these words is left out, as the C8 state objects. The PvP pylons of the Cutlass Keys outposts: the lights
# (cutlasskeys_pvp_pylon_light_00.mtl on ruins_trim_long_01 and ruins_trim_ring_big_02b: a bright additive wall) and the barrier
# (cutlasskeys_pvp_pylon_barrier.mtl on sideways_door); the server shows one state. A retail picture at 7018.7 34.0 checks it.
STATE_OVERRIDE_WORDS = ("pvp_pylon",)


def state_override(material: str) -> bool:
    """@return True for an override material of a state object (STATE_OVERRIDE_WORDS)."""
    return any(word in material.rsplit("/", 1)[-1].lower() for word in STATE_OVERRIDE_WORDS)
# The shadow proxies of the level (AST-288): a mesh or an override material whose file name says "shadow proxy" only casts the shadow
# of a larger object (the medusa walls of First Light, poi_shadow_proxy_*, the primitive cubes with
# objects/default/editorprimitive_shadowproxy_a.mtl: one Illum material, white, no texture). The proxy rule of the sub-materials
# (materials.is_shadow_proxy) reads the Name, and these files have none or name the drawn part "geo". The cells leave them out: before
# the version-3 pairs they drew the grey editorprimitive, with them white cubes. Known difference: their shadows are missing (a cell
# flag for a shadow-only component needs a C++ change).
SHADOW_PROXY_FILE = re.compile(r"shadow_?prox|impostershadow")
# The C++ step after AST-288 W1/W2 (SwcEditorLibrary: flag bit 2 bDisallowNanite, bit 3 hidden in game with bCastHiddenShadow). On since
# its deploy (2026-10-25); an older editor ignores the bits (a shadow proxy would draw white): build the world only with an editor
# that has them.
# SHADOW_ONLY: the shadow proxies are placed with flag bit 3 (their shadow stays), not left out.
# ENTRY_NO_NANITE: the entries of the minority pairs of materials/no-nanite.json (a translucent override on a mesh that keeps Nanite
# for its other placements) get flag bit 2.
SHADOW_ONLY = True
ENTRY_NO_NANITE = True
FLAG_WIND, FLAG_DECAL, FLAG_NO_NANITE, FLAG_SHADOW_ONLY = 1, 2, 4, 8


def shadow_proxy_file(path: str) -> bool:
    """@return True for a mesh or material path whose file name says "shadow proxy" (SHADOW_PROXY_FILE)."""
    return bool(path) and bool(SHADOW_PROXY_FILE.search(path.rsplit("/", 1)[-1].lower()))


@functools.lru_cache(maxsize=None)
def override_shaders(mtl: str) -> frozenset:
    """@return The shaders (lower case) of the sub-materials of an override material ({"?"} when it does not decode)."""
    try:
        return frozenset(m.get("Shader", "").lower() for m in materials.load_mtl(mtl)[1])
    except Exception:
        return frozenset({"?"})


@functools.lru_cache(maxsize=None)
def unsupported_fx(mtl: str) -> bool:
    """@return True for an override material with an effect option that the C8 parents do not have (FX_UNSUPPORTED)."""
    try:
        return any(f in (m.get("StringGenMask", "") or "").upper() for m in materials.load_mtl(mtl)[1] for f in FX_UNSUPPORTED)
    except Exception:
        return True


def effect_parents() -> frozenset:
    """@return The override shaders that have a parent of the client (EFFECT_PARENTS; with C8 also C8_PARENTS; with BEAM_SIMPLE
    also BEAM_SIMPLE_SHADERS)."""
    return (EFFECT_PARENTS | (C8_PARENTS if C8 else frozenset())) | (BEAM_SIMPLE_SHADERS if BEAM_SIMPLE else frozenset())


# Meshparticle with %REFRACTION (AST-290, the decision of the orchestrator 2026-10-25): the source draws these sub-materials as a
# refraction of the scene behind (nearly invisible in the retail kit fall video); M_SwcMeshParticle has no refraction and draws a
# near-opaque white sheet (the kit fall at 7817 6251: the twin fx_waterfall_subtle01 placement hid the flow sheet). A placement whose
# override material has such a sub-material is left out until the client has a refraction material: fx_waterfall_subtle01,
# fx_portalsphere01, fx_portalsphere02 (12 placements in 8 regions).
REFRACTION_SHADERS = ("meshparticle",)


@functools.lru_cache(maxsize=None)
def refraction_override(mtl: str) -> bool:
    """@return True for an override material with a REFRACTION_SHADERS sub-material that has the flag %REFRACTION."""
    if not mtl:
        return False
    try:
        return any(m.get("Shader", "").lower() in REFRACTION_SHADERS
                   and "REFRACTION" in (m.get("StringGenMask", "") or "").upper().split("%") for m in materials.load_mtl(mtl)[1])
    except Exception:
        return False


# AST-288 W5 (the decision of the orchestrator 2026-10-25): the effect shaders with no client parent drew a white sheet where the mesh
# has a material of its own (fog_override covers only the meshes with none). The retail shader cache has own shaders for
# GeometryFogNoVertAlpha and GeometryBeamSimple (not variants of the studied GeometryBeam and Geometryfog); Geometryfog has
# M_SwcFogCard only in the landmark importer. A placement whose override has one of these shaders is left out (the overrides are pure:
# no other drawn sub-material): the pirate ropes and the crafting ghosts (GeometryFogNoVertAlpha; the retail rope view shows no rope),
# the jump pad ribbons (GeometryBeamSimple; a later study), the cave beams (Geometryfog; a later mapping to M_SwcFogCard, AST-265 b).
# W5_LEFT_OUT: off until the next cell rebuild (the decision of 2026-10-25: 352 placements in 22 regions; with W3, W4, or AST-274).
W5_LEFT_OUT = False
NO_PARENT_EFFECT_SHADERS = frozenset({"geometryfog", "geometryfognovertalpha", "geometrybeamsimple"})


# research/beamsimple.md (the decision of the orchestrator 2026-10-25: with W5 in the next cell rebuild): with BEAM_SIMPLE the
# GeometryBeamSimple overrides (the jump pad disks) draw M_SwcBeamSimple; they leave the W5 set and count as a client parent. Each
# jump pad has an "off" and an "on" group of disks; the slice saves the "on" group inactive (IsRuntimeActive 0), and an
# AchievementEvents rule activates it with the status 17DW_FoolSightPotion. Thus a GeometryBeamSimple placement whose entity starts
# inactive (regions/NAME/inactive.json, region.write_inactive) is left out: the default state "off" shows.
BEAM_SIMPLE = False
BEAM_SIMPLE_SHADERS = frozenset({"geometrybeamsimple"})


def no_parent_effect(mtl: str) -> bool:
    """@return True for an override material with a NO_PARENT_EFFECT_SHADERS sub-material (without GeometryBeamSimple when
    BEAM_SIMPLE)."""
    shaders = NO_PARENT_EFFECT_SHADERS - BEAM_SIMPLE_SHADERS if BEAM_SIMPLE else NO_PARENT_EFFECT_SHADERS
    return bool(mtl) and bool(override_shaders(mtl) & shaders)


def beam_simple_override(mtl: str) -> bool:
    """@return True for an override material with a GeometryBeamSimple sub-material."""
    return bool(mtl) and bool(override_shaders(mtl) & BEAM_SIMPLE_SHADERS)


def inactive_entities(name: str) -> np.ndarray:
    """@return The ids of the entities that start inactive in a region (regions/NAME/inactive.json; uint64). BEAM_SIMPLE needs
    the file: a cell write with no file stops (run region.write_inactive first)."""
    path = env.content_dir() / "regions" / name / "inactive.json"
    return np.array(json.loads(path.read_text())["entities"], dtype=np.uint64)


def fog_override(mtl: str) -> bool:
    """@return True for an override material of fog (FOG_MATERIAL_FOLDERS): a Geometryfog shader, or a GeometryBeam of the fog folders."""
    shaders = override_shaders(mtl)
    return any(s.startswith("geometryfog") for s in shaders) or ("geometrybeam" in shaders and mtl.startswith(FOG_MATERIAL_FOLDERS))
SIGNS = np.array([[1.0, -1.0, 1.0], [-1.0, 1.0, -1.0], [1.0, -1.0, 1.0]])   # S L S with S = diag(1, -1, 1)
INSTANCE = np.dtype([("t", "<f8", 3), ("q", "<f4", 4), ("s", "<f4", 3)])


def quaternions(r: np.ndarray) -> np.ndarray:
    """@return The unit quaternions (n, 4: x y z w, w >= 0) of rotation matrices (n, 3, 3), column vectors: the four
    cases of space.quaternion (the largest of w, x, y, z first), for many matrices."""
    trace = r[:, 0, 0] + r[:, 1, 1] + r[:, 2, 2]
    case = np.argmax(np.stack([trace, r[:, 0, 0], r[:, 1, 1], r[:, 2, 2]], axis=1), axis=1)
    q = np.zeros((len(r), 4))
    for k in range(4):
        m = r[case == k]
        if not len(m):
            continue
        if k == 0:
            s = np.sqrt(1.0 + m[:, 0, 0] + m[:, 1, 1] + m[:, 2, 2]) * 2
            v = [(m[:, 2, 1] - m[:, 1, 2]) / s, (m[:, 0, 2] - m[:, 2, 0]) / s, (m[:, 1, 0] - m[:, 0, 1]) / s, 0.25 * s]
        elif k == 1:
            s = np.sqrt(1.0 + m[:, 0, 0] - m[:, 1, 1] - m[:, 2, 2]) * 2
            v = [0.25 * s, (m[:, 0, 1] + m[:, 1, 0]) / s, (m[:, 0, 2] + m[:, 2, 0]) / s, (m[:, 2, 1] - m[:, 1, 2]) / s]
        elif k == 2:
            s = np.sqrt(1.0 + m[:, 1, 1] - m[:, 0, 0] - m[:, 2, 2]) * 2
            v = [(m[:, 0, 1] + m[:, 1, 0]) / s, 0.25 * s, (m[:, 1, 2] + m[:, 2, 1]) / s, (m[:, 0, 2] - m[:, 2, 0]) / s]
        else:
            s = np.sqrt(1.0 + m[:, 2, 2] - m[:, 0, 0] - m[:, 1, 1]) * 2
            v = [(m[:, 0, 2] + m[:, 2, 0]) / s, (m[:, 1, 2] + m[:, 2, 1]) / s, 0.25 * s, (m[:, 1, 0] - m[:, 0, 1]) / s]
        q[case == k] = np.stack(v, axis=1)
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    return np.where(q[:, 3:4] < 0, -q, q)


def unreal_transforms(matrices: np.ndarray) -> np.ndarray:
    """@return The Unreal transforms (INSTANCE records) of engine placement matrices (n, 3, 4): space.unreal_transform
    for many placements (scale of each axis, the nearest rotation, a mirror as a negative X scale)."""
    linear = matrices[:, :, :3] * SIGNS
    scale = np.linalg.norm(linear, axis=1)
    mirror = np.linalg.det(linear) < 0
    scale[mirror, 0] *= -1
    unscaled = linear / np.where(scale == 0, 1.0, scale)[:, None, :]
    left, _values, right = np.linalg.svd(unscaled)
    out = np.empty(len(matrices), dtype=INSTANCE)
    out["t"] = matrices[:, :, 3] * np.array([100.0, -100.0, 100.0])
    out["q"] = quaternions(left @ right)
    out["s"] = scale
    return out


def text(value: str) -> bytes:
    data = value.encode()
    return struct.pack("<H", len(data)) + data


def write(name: str) -> dict:
    """Writes the cell file of one region. @return Counts."""
    data = census.load_placements(name)
    assets = manifest.load()["assets"]
    names = [str(m) for m in data["meshes"]]
    # The landmark meshes (the sky maelstrom: landmarks.LANDMARK_MESHES) are not cell instances: World/SwcLandmarks places them as one
    # persistent actor (AST-209).
    from .landmarks import LANDMARK_MESHES
    # A mesh with only decal subsets (AST-275, C4): the record "empty" names its decal companion; the cells place the companion alone.
    decal_only = np.array([PLACE_DECAL_ONLY and assets.get(m, {}).get("kind") == "empty" and assets.get(m, {}).get("decals") in assets
                           for m in names], dtype=bool)
    usable = np.array([(assets.get(m, {}).get("kind") == "static" or d) and not m.rsplit("/", 1)[-1].startswith(FOG_CARD)
                       and not m.startswith(LANDMARK_MESHES) and m not in LEFT_OUT_MESHES for m, d in zip(names, decal_only)],
                      dtype=bool)
    keep = usable[data["mesh"]] if len(names) else np.zeros(0, bool)
    material_names = [str(m) for m in data.get("materials", np.array([""]))]
    material_all = data["material"] if "material" in data else np.zeros(len(data["mesh"]), np.uint16)
    if len(names):
        proxy_mesh = np.array([shadow_proxy_file(m) for m in names], dtype=bool)
        proxy_material = np.array([shadow_proxy_file(m) for m in material_names], dtype=bool)
        state_objects = np.array([state_override(m) for m in material_names], dtype=bool)[material_all]
        keep &= ~state_objects
        refraction = np.array([refraction_override(m) for m in material_names], dtype=bool)[material_all]
        keep &= ~refraction
        no_parent = np.array([W5_LEFT_OUT and no_parent_effect(m) for m in material_names], dtype=bool)[material_all] & keep
        keep &= ~no_parent
        beam_state = np.zeros(len(keep), bool)
        if BEAM_SIMPLE:
            simple = np.array([beam_simple_override(m) for m in material_names], dtype=bool)[material_all]
            if simple.any():
                beam_state = simple & np.isin(data["entity"], inactive_entities(name)) & keep
                keep &= ~beam_state
        shadow_proxies = proxy_mesh[data["mesh"]] | proxy_material[material_all]
        if not SHADOW_ONLY:
            keep &= ~shadow_proxies
    else:
        shadow_proxies = np.zeros(0, bool)
        state_objects = np.zeros(0, bool)
        refraction = np.zeros(0, bool)
        no_parent = np.zeros(0, bool)
        beam_state = np.zeros(0, bool)
    pairs_path = overrides.out_dir() / "pairs.json"
    pairs = json.loads(pairs_path.read_text()) if pairs_path.is_file() else {}

    def slots_of(mesh_index: int, material_index: int) -> dict:
        if not material_index:
            return {}
        return pairs.get(overrides.pair_key(names[mesh_index], material_names[material_index]), {}).get("slots", {})

    def decal_slots_of(mesh_index: int, material_index: int) -> dict:
        """C11: {companion slot: the decal key of the override, or "" for no decal} of the pair (overrides.slot_map)."""
        if not material_index:
            return {}
        return pairs.get(overrides.pair_key(names[mesh_index], material_names[material_index]), {}).get("decal slots", {})

    def companion_of(mesh_index: int) -> str | None:
        key = assets.get(names[mesh_index], {}).get("decals")
        return key if key in assets else None

    def no_decal(mesh_index: int, material_index: int) -> bool:
        """C11: the override has no decal at any slot of the companion: retail draws no decal for the placement."""
        changes = decal_slots_of(mesh_index, material_index)
        companion = companion_of(mesh_index)
        return bool(changes) and companion is not None and all(changes.get(s) == "" for s in assets[companion].get("slots", []))

    # A mesh with no material of its own (its only record is the key of no sub-material) draws only with the slot map of an override
    # (AST-275): with none it would draw the fallback material (white); retail has no material for it. Its override must also have
    # a parent of the client (EFFECT_PARENTS): the effect shaders with no parent yet (Fxmeshadvancedtransp: the glyph bridges, the
    # capture point cylinders; Meshparticle: the light ray cards) would draw an opaque legacy instance, and a fog override (fog_override)
    # has no proved parent. All are left out.
    no_material = materials.shared_material(None)[0]["name"]
    bare = {i for i, m in enumerate(names) if assets.get(m, {}).get("materials") == [no_material]}
    if bare and len(names):
        for m, o in set(zip(data["mesh"][keep].tolist(), material_all[keep].tolist())):
            if m in bare and (not slots_of(m, o) or not override_shaders(material_names[o]) <= effect_parents()
                              or fog_override(material_names[o])
                              or (C8 and override_shaders(material_names[o]) & C8_PARENTS
                                  and (unsupported_fx(material_names[o]) or any(word in names[m] for word in C8_LEFT_OUT)
                                       or any(word in material_names[o].lower() for word in C8_LEFT_OUT_MATERIALS)))):
                keep &= ~((data["mesh"] == m) & (material_all == o))

    # C11: a decal-only mesh (C4) whose override has no decal at its slots is not placed (its companion is the whole mesh).
    dropped_decal_only = 0
    if len(names):
        for m, o in set(zip(data["mesh"][keep].tolist(), material_all[keep].tolist())):
            if decal_only[m] and no_decal(m, o):
                rows = (data["mesh"] == m) & (material_all == o) & keep
                dropped_decal_only += int(rows.sum())
                keep &= ~rows

    # The entry of a placement: (mesh, override) when the override changes a slot or a decal slot (C11), else the mesh alone.
    entry_of_pair: dict[tuple[int, int], tuple[int, int]] = {}
    for m, o in set(zip(data["mesh"][keep].tolist(), material_all[keep].tolist())):
        proxy = SHADOW_ONLY and (shadow_proxy_file(names[m]) or shadow_proxy_file(material_names[o]))
        entry_of_pair[(m, o)] = (m, o) if slots_of(m, o) or decal_slots_of(m, o) or proxy else (m, 0)
    # The flags of the C++ step (SHADOW_ONLY, ENTRY_NO_NANITE): bit 3 for a shadow proxy, bit 2 for a minority pair of no-nanite.json.
    minority: set = set()
    if ENTRY_NO_NANITE:
        from . import nanite
        listed_path = nanite.path()
        if listed_path.is_file():
            minority = set(json.loads(listed_path.read_text()).get("minority", {}))
        nanite_specs = nanite.load_specs() if minority else {}

    def entry_flags(mesh_index: int, material_index: int) -> int:
        flags = 0
        if SHADOW_ONLY and (shadow_proxy_file(names[mesh_index]) or shadow_proxy_file(material_names[material_index])):
            flags |= FLAG_SHADOW_ONLY
        if (ENTRY_NO_NANITE and material_index and names[mesh_index] in minority
                and nanite.translucent_pair(names[mesh_index], material_names[material_index], assets, pairs, nanite_specs)):
            flags |= FLAG_NO_NANITE
        return flags

    pair_rows = np.array([entry_of_pair[(m, o)] for m, o in zip(data["mesh"][keep].tolist(), material_all[keep].tolist())],
                         dtype=np.int64).reshape(-1, 2)
    wind_all = data["wind"] if "wind" in data else np.zeros(len(data["mesh"]), bool)
    # One number for each entry: (mesh * 65536 + override) * 2 + wind.
    mesh_of = (pair_rows[:, 0] * 65536 + pair_rows[:, 1]) * 2 + wind_all[keep].astype(np.int64)
    matrices = data["matrix"][keep]
    rx, ry = (int(v) for v in name[2:].split("_"))
    ox, oy = rx * 2048.0, ry * 2048.0
    # The cell of the place of each instance (no clip: 0.11 % of the placements are more than 200 m outside the square of
    # their region, up to 970 m; a cell index can be below 0 or above 7). The actor stands at the center of its cell:
    # floor(X / 25600) and floor(-Y / 25600) give the global cell (the creature track's SwcLevelInstances).
    cx = np.floor((matrices[:, 0, 3] - ox) / CELL_M).astype(int)
    cy = np.floor((matrices[:, 1, 3] - oy) / CELL_M).astype(int)
    transforms = unreal_transforms(matrices)
    used = sorted(set(mesh_of.tolist()))
    table = {m: i for i, m in enumerate(used)}
    # The decal companion of a group: one entry for each (mesh, override with decal slots, else 0), after the entries of the groups.
    # C11: the override's decal keys replace the slots of the companion; a slot with no decal in the override keeps its own decal
    # when other slots change ("decal slots kept"), and a group whose override has no decal at all has no companion.
    def companion_slots(mesh_index: int, material_index: int) -> dict:
        return {s: k for s, k in decal_slots_of(mesh_index, material_index).items() if k}

    companion_of_entry: dict[int, tuple[int, int]] = {}
    for m in used:
        mesh_index, material_index = divmod(m // 2, 65536)
        if decal_only[mesh_index] or companion_of(mesh_index) is None or no_decal(mesh_index, material_index):
            continue
        companion_of_entry[m] = (mesh_index, material_index if companion_slots(mesh_index, material_index) else 0)
    companions = sorted(set(companion_of_entry.values()))
    companion_entry = {c: len(used) + i for i, c in enumerate(companions)}

    def companion_bytes(mesh_index: int, material_index: int) -> bytes:
        # No wind, no shadow, no collision (flag bit 1).
        slots = companion_slots(mesh_index, material_index)
        data_ = text(assets[companion_of(mesh_index)]["unreal"]) + struct.pack("<H", len(slots))
        for slot, key in sorted(slots.items()):
            data_ += text(slot) + text(key)
        return data_ + struct.pack("<B", 2)

    out = bytearray(b"SWCI" + struct.pack("<II", 3, len(used) + len(companions)))
    overridden = 0
    for m in used:
        pair, wind = divmod(m, 2)
        slots = slots_of(pair // 65536, pair % 65536)
        overridden += bool(slots)
        if decal_only[pair // 65536]:
            out += companion_bytes(pair // 65536, pair % 65536)   # the decal companion alone
            continue
        out += text(assets[names[pair // 65536]]["unreal"]) + struct.pack("<H", len(slots))
        for slot, key in sorted(slots.items()):
            out += text(slot) + text(key)
        out += struct.pack("<B", wind | entry_flags(pair // 65536, pair % 65536))
    for mesh_index, material_index in companions:
        out += companion_bytes(mesh_index, material_index)
    cells = sorted(set(zip(cx.tolist(), cy.tolist())))
    out += struct.pack("<I", len(cells))
    groups = decal_groups = 0
    for x, y in cells:
        rows = np.nonzero((cx == x) & (cy == y))[0]
        z = float(np.median(transforms["t"][rows, 2]))
        center = ((ox + (x + 0.5) * CELL_M) * 100.0, -(oy + (y + 0.5) * CELL_M) * 100.0, z)
        meshes_here = mesh_of[rows]
        order = sorted(set(meshes_here.tolist()))
        with_decals = [m for m in order if m in companion_of_entry]
        out += text(f"{name} cell {x} {y}") + struct.pack("<3d", *center) + struct.pack("<I", len(order) + len(with_decals))
        for m in order:
            picked = rows[meshes_here == m]
            out += struct.pack("<II", table[m], len(picked)) + transforms[picked].tobytes()
            if m in companion_of_entry:
                out += struct.pack("<II", companion_entry[companion_of_entry[m]], len(picked)) + transforms[picked].tobytes()
        groups += len(order) + len(with_decals)
        decal_groups += len(with_decals)
    path = env.content_dir() / "regions" / name / "cells.bin"
    temporary = path.with_suffix(".tmp")   # a level build can read the file: atomic
    temporary.write_bytes(bytes(out))
    temporary.replace(path)
    report = {"region": name, "placements": int(len(data["mesh"])), "instances": int(keep.sum()), "entries": len(used),
              "entries with an override": overridden,
              "instances with an override": int(sum(1 for m in mesh_of.tolist() if (m // 2) % 65536)),
              "entries with wind": sum(m % 2 for m in used), "instances with wind": int((mesh_of % 2).sum()),
              "decal companions": len(companions), "decal components": decal_groups,
              "decal companions with an override (C11)": sum(1 for _, o in companions if o),
              "instances with no companion: no decal in the override (C11)":
                  int(sum(1 for m in mesh_of.tolist() if m not in companion_of_entry and not decal_only[m // 131072]
                          and companion_of(m // 131072) is not None)),
              "decal-only placements left out: no decal in the override (C11)": dropped_decal_only,
              "cells": len(cells), "components": groups, "file": str(path), "bytes": len(out),
              "left out (not imported, fog cards, fog volumes, no material)": int((~keep).sum()),
              "state objects left out (pvp pylons)": int(state_objects.sum()),
              "refraction placements left out (Meshparticle %REFRACTION)": int(refraction.sum()),
              "effect placements left out (no client parent, W5)": int(no_parent.sum()),
              "jump pad disks left out (inactive state group, BEAM_SIMPLE)": int(beam_state.sum()),
              "shadow proxies left out": 0 if SHADOW_ONLY else int(shadow_proxies.sum()),
              "shadow proxies placed shadow-only": int(shadow_proxies.sum()) if SHADOW_ONLY else 0,
              "entries with no Nanite": sum(1 for m in used if entry_flags(*divmod(m // 2, 65536)) & FLAG_NO_NANITE),
              "decal-only meshes": int(decal_only[data["mesh"][keep]].sum()) if len(names) else 0}
    (path.parent / "cells.json").write_text(json.dumps(report, indent=1))
    return report
