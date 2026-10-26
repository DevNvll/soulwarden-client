"""The persons of the settlements (stage P4.8, docs/PERSONS.md): the census of the person bodies of the level data, their looks,
their idle animations, their names, and their conversations, for the client view and the Talk.

The rules come from the research of P4.8 (~/Games/soulwarden-client/research/p48-persons/ and p48-talk/):

- A person is an entity with a SkinnedMeshComponent whose .cdf has the person skeleton (npc_skel.chr; it is byte-identical to the
  player skeletons), or the entity of a slice with an NpcComponent or a VariationDataComponent of an NPC table. The retail client
  makes these bodies itself: the backend sends no record for them (the Soulwarden interactions.md: the client makes each object of
  the level data).
- The walk: the chunk slices and the capitals of each region of the main world, and the slice of each PrefabSpawner with
  m_spawnOnInitialize (the child goes to the transform of the spawner entity, with the variant of the spawner). Depth 12.
- A master slice (masternpc_*, npc_variantmaster, ambientnpc_mastervariant) has a row of a variation table (VariantID = the variant
  of its spawner or of the slice): CharacterDefinition replaces the body of the template, IdleTimeline and LookPoseTimeline replace
  the start timelines, Name, NPCId, AttachedMesh, SwapAchievementId, ScaleOverride.
- The look: the visible CA_SKIN attachments of the .cdf (Flags bit 0: hidden) with their materials, the render skins of the CA_CLOTH
  attachments (no simulation), and the CA_BONE items (a .cgf on a bone with RelPosition and RelRotation w, x, y, z). The material
  of a skin: choose_skin_material (AST-277: an attachment with no Material takes the material of its skin).
- The animation: no Mannequin. The timelines of the entities with RunOnActivate (Play_idleLoop, Play_LookPoseAnim) play from time 0:
  the clips of the layer PlayAnim name an alias of the .chrparams of the .cdf (ParamsOverride) or of its skeleton.
- The presence: the NPCData entries of region.metadata (an npc id CRC at a place): SwapAchievementId and ShowOnAchievementLocked.

Outputs (SWC_CONTENT/persons, never in git):
  persons.json   each person (the place, the yaw, the scale, the body, the items, the clips, the names, the presence, the
                 conversation), and the wanted lists of the player pipeline (bodies, items, clips); docs/PERSONS.md has the format
Then `convert` makes the bodies, the items, and the clips with the player pipeline (player.py: the manifest kinds part, weapon,
clips) for tools/import-player.sh.
"""
from __future__ import annotations

import json
import math
import struct
import uuid
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

import numpy as np

from . import census, cgf, creature, datasheet, env, gltf, materials, player, region, source

from nav_slices import read_slice  # noqa: E402

WORLD = "sharedassets/coatlicue/newworld_vitaeeterna/"
F = source.field_id
COMPONENTS, NAME, WORLD_TM = F("Components"), F("Name"), F("m_worldTM")
MAX_DEPTH = 12


def _uuid(text: str) -> bytes:
    return uuid.UUID(text).bytes


ENTITY = _uuid("75651658-8663-478d-9090-2432dfcafa44")
TRANSFORM = _uuid("484ae67d-abd0-4d9c-b2c8-9bb0eec900e0")
SKINNED = _uuid("c99eb110-ca74-4d95-83f0-2fcdd1ff418b")
VARIATION = _uuid("4a2d9c0a-b147-4186-a9e0-5805b610d3cc")
NPC = _uuid("4bdb17d5-3b1e-474d-a783-29f47a7acd53")
NAMECOMP = _uuid("76e26125-7e1c-32f4-c1d0-b841d64312ad")
TIMELINE = _uuid("711f015c-c03b-4735-8c69-b21a224ef2dd")
LOOKAT = _uuid("3cca8cd0-d455-4531-9c1e-fc8171ec84cd")
INTERACT = _uuid("b69045a3-a129-4334-bdfb-069c18cc84a2")
ATTACH = _uuid("2d17a64a-7ac5-4c02-ac36-c5e8141ffddf")
MESH = _uuid("2f4bad46-c857-4dcb-a454-c412de67852a")
SPAWNER = _uuid("b52d0d52-99cd-41be-b6f7-d2519c15bf14")
MARKS = (SKINNED, VARIATION, NPC)
PERSON_SKELETONS = {"objects/characters/npc/npc_skel.chr", "objects/characters/player/male/player_male.chr",
                    "objects/characters/player/female/player_female.chr"}
VARIATION_TABLES = ("javelindata_variations_npcs.datasheet", "javelindata_variations_npcs_walkaway.datasheet",
                    "javelindata_variations_npcs_clientpathing_walkaway.datasheet")
# The start timelines that a row replaces (the entity of the slice -> the column of the variation row).
ROW_TIMELINES = {"Play_idleLoop": "IdleTimeline", "Play_LookPoseAnim": "LookPoseTimeline"}
# The skeleton of the bodies in Unreal: the person skeleton is the player skeleton (130 bones, byte-identical files).
GENDER = "Male"


def norm(path) -> str:
    return (path or "").replace("\\", "/").lower().lstrip("/")


def find(element, field: str):
    want = F(field)
    for node in source.walk(element):
        if node.name == want:
            return node
    return None


def text_of(element, field: str) -> str:
    node = element.child(F(field)) if element is not None else None
    return source.text(node) if node is not None and node.value is not None else ""


def flag(element, field: str):
    node = find(element, field)
    return bool(node.value[0]) if node is not None and node.value else None


def real(element, field: str):
    node = find(element, field)
    return round(struct.unpack(">f", node.value)[0], 5) if node is not None and node.value and len(node.value) == 4 else None


def asset(element, field: str) -> str:
    node = find(element, field)
    if node is None or node.value is None or len(node.value) < 56:
        return ""
    if any(node.value[:16]):
        path = region.asset_path(node)
        if path:
            return norm(path)
    return norm(node.value[56:].decode("latin-1"))


# ------------------------------------------------------------------------------------------------------------------ the census

_models: dict[str, str] = {}


def cdf_model(path: str) -> str:
    """@return The skeleton (Model File) of a .cdf, lower case."""
    if path not in _models:
        try:
            model = ET.fromstring(source.read(path).decode("utf-8", "replace")).find("Model")
            _models[path] = norm(model.get("File")) if model is not None else ""
        except Exception:
            _models[path] = ""
    return _models[path]


_slices: dict[str, dict] = {}


def slice_content(path: str) -> dict:
    """@return The persons, the slice facts (NPC key, variation, timelines, attachments, look-at, interact), and the start
    spawners of a slice (cached)."""
    if path in _slices:
        return _slices[path]
    out = {"persons": [], "npc": [], "timelines": {}, "attachments": [], "lookAt": None, "interact": None, "spawners": [],
           "interactMatrix": None}
    try:
        data = source.read(path)
    except Exception:
        _slices[path] = out
        return out
    if SPAWNER in data:
        for spawner in read_slice(data).spawners:
            if spawner.slice is not None and spawner.on_start:
                child = source.product(spawner.slice[0], spawner.slice[1], ".dynamicslice")
                if child:
                    out["spawners"].append((child, region.rows4(spawner.matrix), spawner.variant or ""))
    if not any(mark in data for mark in MARKS):
        _slices[path] = out
        return out
    for root in source.parse(data):
        for entity in source.walk(root):
            components = entity.child(COMPONENTS)
            if entity.type_id != ENTITY or components is None or any(c.child(F("Entities")) is not None for c in components.children):
                continue
            name = source.text(entity.child(NAME)) if entity.child(NAME) is not None else ""
            active = entity.child(F("IsRuntimeActive"))
            info = {"name": name, "active": bool(active.value[0]) if active is not None and active.value else None, "matrix": np.eye(4)}
            for component in components.children:
                if component.type_id == TRANSFORM:
                    tm = component.child(WORLD_TM)
                    if tm is not None and tm.value and len(tm.value) == 48:
                        info["matrix"] = region.az_matrix(tm.value)
                elif component.type_id == SKINNED:
                    info["cdf"] = asset(component, "Skinned Mesh")
                    info["visible"] = flag(component, "Visible")
                elif component.type_id == VARIATION:
                    info["table"] = text_of(component, "m_variationTableUniqueName")
                    info["variant"] = text_of(component, "m_selectedVariant")
                elif component.type_id == NPC:
                    info["npcKey"] = text_of(component, "m_npcKey")
                elif component.type_id == NAMECOMP:
                    info["uiName"] = text_of(component, "m_name")
                elif component.type_id == TIMELINE:
                    paths = [norm(n.value[56:].decode("latin-1")) for n in source.walk(component)
                             if n.name == F("TimelineAsset") and n.value and len(n.value) >= 56]
                    if paths:
                        out["timelines"][name] = {"timelines": paths, "run": bool(flag(component, "RunOnActivate")), "active": info["active"]}
                elif component.type_id == LOOKAT:
                    out["lookAt"] = {"useLookIK": flag(component, "UseLookIK"), "rotateToTarget": flag(component, "RotateToTargetOnInteract"),
                                     "smoothTime": real(component, "SmoothTime"), "fadeoutAngle": real(component, "FadeoutAngle")}
                elif component.type_id == INTERACT:
                    out["interact"] = {"options": [source.text(n) for n in source.walk(component) if n.name == F("m_interactOption") and n.value],
                                       "rangeSq": real(component, "m_interactRangeSq")}
                elif component.type_id == ATTACH:
                    node = find(component, "Target Bone Name")
                    info["attachBone"] = source.text(node) if node is not None and node.value else ""
                elif component.type_id == MESH:
                    info["staticMesh"] = asset(component, "Static Mesh")
            if any(component.type_id == INTERACT for component in components.children):
                # The frame of the Talk camera presets: the Interact entity (retail Talk cameras of MVP-H: 0.05 to 0.07 m from the
                # preset point in this frame for four persons; the body can be turned 180 degrees from it, 0308_Alchemist).
                out["interactMatrix"] = info["matrix"]
            if info.get("npcKey") is not None or str(info.get("table", "")).lower().startswith("npc"):
                out["npc"].append({k: info[k] for k in ("npcKey", "table", "variant", "uiName") if k in info})
            if "attachBone" in info and info.get("active") is not False:
                out["attachments"].append({"entity": name, "bone": info["attachBone"], "mesh": info.get("staticMesh", "")})
            if info.get("cdf"):
                out["persons"].append(info)
    # A person: a skinned body with a person skeleton, or any skinned body of a slice with an NPC component or an NPC variation.
    out["persons"] = [p for p in out["persons"] if p.get("active") is not False
                      and (cdf_model(p["cdf"]) in PERSON_SKELETONS if p["cdf"].endswith(".cdf") else False)]
    _slices[path] = out
    return out


def walk(path: str, matrix: np.ndarray, origin: str, chain: list, placement: str, region_name: str, variant: str, found: list) -> None:
    if len(chain) > MAX_DEPTH or any(link["slice"] == path for link in chain):
        return
    content = slice_content(path)
    chain = chain + [{"slice": path, "variant": variant}]
    for person in content["persons"]:
        world = matrix @ person["matrix"]
        forward = world[:3, :3] @ np.array([0.0, 1.0, 0.0])
        yaw = float(np.degrees(np.arctan2(-forward[0], forward[1])))   # 0 = +Y (north), counterclockwise positive
        talk = None
        if content.get("interactMatrix") is not None:
            frame = matrix @ content["interactMatrix"]
            ahead = frame[:3, :3] @ np.array([0.0, 1.0, 0.0])
            talk = {"position": [round(float(v), 3) for v in frame[:3, 3]],
                    "yaw": round(float(np.degrees(np.arctan2(-ahead[0], ahead[1]))), 2)}
        found.append({"region": region_name, "origin": origin, "placement": placement, "chain": [dict(link) for link in chain],
                      "talkFrame": talk,
                      "entity": person["name"], "templateCdf": person["cdf"], "position": [round(float(v), 3) for v in world[:3, 3]],
                      "yaw": round(yaw, 2), "scale": round(float(np.linalg.norm(world[:3, 0])), 4), "slice": content})
    for child, local, child_variant in content["spawners"]:
        walk(child, matrix @ local, origin, chain, placement, region_name, child_variant, found)


def census_region(name: str) -> list:
    folder = WORLD + "regions/" + name + "/"
    found: list = []
    root = source.parse(source.read(folder + "region.chunks"))[0]
    for chunk in root.child(F("chunks")).children:
        if source.number(chunk.child(F("chunkType"))) == 2:
            continue   # collision chunks
        a = source.asset_id(chunk.child(F("AssetId")))
        path = source.product(a[0], a[1], ".dynamicslice") if a else None
        if path is None:
            continue
        base = np.eye(4)
        base[:3, 3] = struct.unpack(">3f", chunk.child(F("WorldPosition")).value)
        walk(path, base, "chunk", [], "", name, "", found)
    for manifest in source.paks().paths(folder + "capitals/", ".capitals.json"):
        layer = manifest.split("/capitals/")[1].split("/")[0]
        for capital in json.loads(source.read(manifest)).get("Capitals", []):
            if capital.get("sliceName") == "<PLOT>":
                continue
            text = str(capital.get("sliceAssetId", ""))
            path = source.product_of_text(text, ".dynamicslice") if text.startswith("{") and not text.startswith("{00000000") else None
            if path is None:
                key = region.slice_key(capital.get("sliceName", ""))
                path = next((p for p in ("slices/" + key + ".dynamicslice", key + ".dynamicslice") if source.has(p)), None)
            if path is None:
                continue
            q, p = capital.get("rotation", {}), capital.get("worldPosition", {})
            m = np.eye(4)
            m[:3, :3] = region.quat_matrix(q.get("x", 0.0), q.get("y", 0.0), q.get("z", 0.0), q.get("w", 1.0)) * float(capital.get("scale", 1.0))
            m[:3, 3] = (p.get("x", 0.0), p.get("y", 0.0), p.get("z", 0.0))
            walk(path, m, "capital:" + layer, [], str(capital.get("id", "")), name, str(capital.get("variantName", "")), found)
    return found


# --------------------------------------------------------------------------------------------------------------- the resolver

class Tables:
    """The tables of the resolver: the variation rows, the NPCData rows, the texts, the presence metadata, the interaction data."""

    def __init__(self) -> None:
        self.rows: dict[str, dict[str, dict]] = {}
        for file_name in VARIATION_TABLES:
            sheet = datasheet.load(file_name)
            self.rows[sheet.name.lower()] = {str(r.get("VariantID", "")).lower(): r for r in sheet.rows()}
        self.npcdata: dict[str, dict] = {}
        for path in source.paks().entries:
            if path.startswith(datasheet.TABLES_FOLDER) and path.endswith("_npcs.datasheet"):
                try:
                    sheet = datasheet.Sheet(source.read(path))
                except Exception:
                    continue
                if sheet.type != "NPCData":
                    continue
                for r in sheet.rows():
                    if r.get("NPCId"):
                        self.npcdata.setdefault(str(r["NPCId"]).lower(), r)
        self.crc_npc = {zlib.crc32(k.encode()) & 0xFFFFFFFF: k for k in self.npcdata}
        for rows in self.rows.values():
            for r in rows.values():
                if r.get("NPCId"):
                    key = str(r["NPCId"]).lower()
                    self.crc_npc.setdefault(zlib.crc32(key.encode()) & 0xFFFFFFFF, key)
        texts = env.content_dir() / "ui" / "localization" / "en-us.json"
        self.loc = json.loads(texts.read_text()) if texts.is_file() else {}
        data = json.loads((creature.gamedata_dir() / "interaction-data.json").read_text())
        self.npc_objects = {o["key"]: o for o in data["objects"] if o["kind"] == "npc"}
        # The person children of a PrefabSpawner (format 4, backend stage 2.5c): their objects have the client id of tag 7 of the retail
        # client (backend docs/systems/persons.md); by the placement of the spawner. A slot near a border of the id rule has a second
        # object (KEY~1): the first is kept.
        self.child_objects: dict[str, list] = {}
        self.child_by_key: dict[str, dict] = {}
        for o in data["objects"]:
            person = o.get("person") or {}
            if o["kind"] == "npc" and person.get("clientChild") and not o["key"].endswith("~1"):
                self.child_objects.setdefault(person.get("spawner", ""), []).append(o)
                self.child_by_key[o["key"]] = o
        self.meta = self._metadata()
        # The achievement bits (javelindata_achievements AchievementIndex: the bit of the Achievement state of the character, the rule
        # of the backend interactions.md) and the non-persisted achievements (an id with "*": an objective flow of a quest).
        sheet = datasheet.Sheet(source.read(datasheet.TABLES_FOLDER + "javelindata_achievements.datasheet"))
        self.achievement_bits = {str(r["AchievementID"]).lower(): int(r["AchievementIndex"]) for r in sheet.rows()
                                 if r.get("AchievementID") and r.get("AchievementIndex") not in (None, "")}

    def text(self, key) -> str:
        return self.loc.get(str(key or "").lstrip("@").lower(), "") if key else ""

    def _metadata(self) -> list:
        out = []
        for path in source.paks().paths(WORLD + "regions/", "/region.metadata"):
            for root in source.parse(source.read(path)):
                for node in source.walk(root):
                    if node.name != F("NPCData"):
                        continue
                    for row in node.children:
                        fields = {c.name: c for c in row.children}
                        place, key = fields.get(F("WorldPosition")), fields.get(F("npcId"))
                        raw = next((c.value for c in source.walk(key) if c.value), None) if key is not None else None
                        if place is None or raw is None:
                            continue
                        swap, locked = fields.get(F("SwapAchievementId")), fields.get(F("ShowOnAchievementLocked"))
                        out.append({"npc": self.crc_npc.get(int.from_bytes(raw, "big"), ""),
                                    "position": [round(v, 3) for v in struct.unpack(">3f", place.value)],
                                    "swap": source.text(swap) if swap is not None and swap.value else "",
                                    "showOnLocked": bool(locked.value[0]) if locked is not None and locked.value else False})
        return out

    def presence(self, position) -> dict | None:
        best = None
        for item in self.meta:
            d = math.dist(item["position"], position)
            if d < 0.6 and (best is None or d < best[0]):
                best = (d, item)
        return best[1] if best else None


_timelines: dict[str, dict | None] = {}


def read_timeline(path: str) -> dict | None:
    """@return The layers of a timeline with their clips (alias, start, end, loop, speed, the start in the clip, blends, weight)."""
    path = norm(path)
    if path in _timelines:
        return _timelines[path]
    out = None
    if path and source.has(path):
        roots = source.parse(source.read(path))
        layers = []
        for layer in source.walk(roots[0]):
            if layer.child(F("Clips")) is None:
                continue
            clips = []
            for clip in source.walk(layer.child(F("Clips"))):
                alias = clip.child(F("AnimationAlias"))
                if alias is None:
                    continue
                base = clip.child(F("BaseClass1"))

                def number(element, field: str, default: float = 0.0) -> float:
                    node = element.child(F(field)) if element is not None else None
                    return round(struct.unpack(">f", node.value)[0], 4) if node is not None and node.value and len(node.value) == 4 else default

                loop = clip.child(F("Loop"))
                clips.append({"alias": source.text(alias), "start": number(base, "StartTime"), "end": number(base, "EndTime"),
                              "loop": bool(loop.value[0]) if loop is not None and loop.value else False,
                              "speed": number(clip, "playbackSpeed", 1.0), "clipStart": number(clip, "AnimationStartTime"),
                              "blendIn": number(clip, "BlendInTime"), "blendOut": number(clip, "BlendOutTime"),
                              "weight": number(clip, "Weight", 1.0)})
            name = source.text(layer.child(F("Name"))) if layer.child(F("Name")) is not None else ""
            layers.append({"layer": name, "clips": clips})
        out = {"path": path, "layers": layers}
    _timelines[path] = out
    return out


_clip_lists: dict[str, dict] = {}


def clips_of(cdf: str) -> dict:
    """@return The clip names of a .cdf: the .chrparams of its ParamsOverride, else of its skeleton (research p48-persons)."""
    if cdf in _clip_lists:
        return _clip_lists[cdf]
    names: dict = {}
    try:
        model = ET.fromstring(source.read(cdf).decode("utf-8", "replace")).find("Model")
        override = norm(model.get("ParamsOverride") or "")
        names = creature.clip_list(override.rsplit(".", 1)[0] + ".chr") if override else {}
        if not names:
            names = creature.clip_list(norm(model.get("File") or ""))
    except Exception:
        names = {}
    _clip_lists[cdf] = names
    return names


def cdf_look(cdf: str) -> dict:
    """@return The visible skins (with materials), the render skins of the cloth, and the bone items of a .cdf."""
    out = {"skins": [], "cloth": [], "items": []}
    try:
        root = ET.fromstring(source.read(cdf).decode("utf-8", "replace"))
    except Exception as error:
        out["error"] = str(error)[:80]
        return out
    for attachment in root.iter("Attachment"):
        kind = attachment.get("Type")
        binding = norm(attachment.get("Binding"))
        hidden = (int(attachment.get("Flags") or 0) & 1) == 1
        if hidden or not binding:
            continue
        material = norm(attachment.get("Material"))
        if kind == "CA_SKIN":
            out["skins"].append([binding, material])
        elif kind == "CA_CLOTH":
            out["cloth"].append([binding, material])
        elif kind == "CA_BONE" and binding.endswith(".cgf") and "xformattach" not in binding:
            position = [float(v) for v in (attachment.get("RelPosition") or attachment.get("Position") or "0,0,0").split(",")]
            rotation = [float(v) for v in (attachment.get("RelRotation") or attachment.get("Rotation") or "1,0,0,0").split(",")]
            out["items"].append({"mesh": binding, "material": material, "bone": attachment.get("BoneName") or "",
                                 "position": position, "rotationWXYZ": rotation})
    return out


def choose_skin_material(skin: str, material: str, loads, own_material, tone_body, row_material) -> str:
    """The material of one skin of a person (AST-277; the pure rule, the readers are arguments):
    1. the Material of the .cdf attachment, when the game copy has it;
    2. a race body material that the game copy does not have (naked_mix/female_naked_body_mix_l1): the body of the same tone in
       the skin tone data of the player (the *.skintonedb rows use naked_shared/..._body_shared_TONE for each race);
    3. the material of the skin itself (its MTLNAME chunk; the .cdf attachment of 159 armor skins has no Material);
    4. the material of the player appearance row with this skin (looks.json armor; a skin with no material of its own, or with a
       bare file name of another folder).
    Not the Material of the Model of the .cdf: it is the material of the base model (dummyPlane_mat_group for the persons).
    loads(reference, skin) -> the .mtl path or None; own_material(skin) -> the MTLNAME text or ""; tone_body(mtl) -> a body .mtl
    or ""; row_material(skin) -> a .mtl or "". @return The .mtl path, or "" (no material: the part draws white)."""
    if material:
        path = loads(material, skin)
        if path:
            return path
        body = tone_body(material)
        if body and loads(body, skin):
            return loads(body, skin)
    own = own_material(skin)
    path = loads(own, skin) if own else None
    if path:
        return path
    row = row_material(skin)
    return (loads(row, skin) or "") if row else ""


def _loads(reference: str, skin: str) -> str | None:
    try:
        path, subs = materials.load_mtl(reference, skin)
    except (OSError, LookupError, ValueError, ET.ParseError, source.Failure):   # a file that is not in the game copy, or not XML
        return None
    return path if len(subs) else None


def _own_material(skin: str) -> str:
    try:
        return cgf.material_guid(cgf.read_chunks(source.read(skin))) or ""
    except (OSError, ValueError, struct.error, source.Failure):
        return ""


_looks_rules: dict = {}


def _looks_rule(name: str) -> dict:
    """The tone bodies and the materials of the skins of the player appearance rows (SWC_CONTENT/player/looks.json; once)."""
    if not _looks_rules:
        path = env.content_dir() / "player" / "looks.json"
        looks = json.loads(path.read_text()) if path.is_file() else {}
        tones, rows = {}, {}
        for entries in looks.get("toneSets", {}).values():
            for tone in entries:
                # The race body of the head of a tone: naked_RACE/G_naked_head_RACE_TONE -> naked_RACE/G_naked_body_RACE_TONE.
                if tone.get("head") and tone.get("body"):
                    tones.setdefault(tone["head"].replace("_naked_head_", "_naked_body_"), tone["body"])
        for row in looks.get("armor", {}).values():
            for n in (1, 2):
                skin = row.get(f"skin{n}", "")
                material = row.get(f"material{n}") or row.get("material1", "")   # material2 can be a number (0): material1
                material = material if isinstance(material, str) and material.endswith(".mtl") else row.get("material1", "")
                if skin and material.endswith(".mtl") and not row.get(f"isSkin{n}"):
                    rows.setdefault(skin, material)
            for _, skin, material in row.get("cloth", []):
                if skin and material:
                    rows.setdefault(skin, material)
        _looks_rules.update(tones=tones, rows=rows)
    return _looks_rules[name]


_skin_materials: dict = {}


def skin_material(skin: str, material: str) -> str:
    """The material of one skin of a person with the readers of the game copy (choose_skin_material). @return a .mtl path or ""."""
    key = (skin, material)
    if key not in _skin_materials:
        _skin_materials[key] = choose_skin_material(skin, material, _loads, _own_material,
                                                    lambda mtl: _looks_rule("tones").get(mtl, ""),
                                                    lambda s: _looks_rule("rows").get(s, ""))
    return _skin_materials[key]


def body_skins(look: dict) -> list[tuple[str, str]]:
    """@return (skin, .mtl) of each drawn skin of a person body: the visible skins and the render skins of the cloth, with the
    material of skin_material (the body conversion and the body specs of bodies.py use the same list)."""
    skins = list(look["skins"]) + [(cloth_render_skin(binding), material) for binding, material in look["cloth"]]
    return [(skin, skin_material(skin, material) if source.has(skin) else material) for skin, material in skins if skin]


def resolve(found: list, tables: Tables) -> list:
    persons = []
    for item in found:
        content = item.pop("slice")
        top = item["chain"][-1]
        npc = content["npc"]
        table = next((n.get("table") for n in npc if n.get("table")), "")
        own_variant = next((n.get("variant") for n in npc if n.get("variant")), "")
        variant = top.get("variant") or own_variant
        row = tables.rows.get(str(table).lower(), {}).get(str(variant).lower()) if table else None
        npc_key = next((n.get("npcKey") for n in npc if n.get("npcKey")), "")
        if row is None and not table and npc_key:
            # A slice with an NpcComponent and no variation data (the commander of Oxboro, 0407a_Commander): the row of the
            # variation tables whose VariantID is the NPC id gives its body and its timelines [inference; the retail commander
            # plays the lean of malek_standing_ambient of that row (user check, 2026-10-21), not the generic idle of the slice].
            row = next((rows[npc_key.lower()] for rows in tables.rows.values() if npc_key.lower() in rows), None)
        row = row or {}
        npc_id = str(row.get("NPCId") or npc_key or "")
        data = tables.npcdata.get(npc_id.lower(), {})
        cdf = norm(row.get("CharacterDefinition")) or item["templateCdf"]
        name_key = row.get("Name") or next((n.get("uiName") for n in npc if n.get("uiName")), "")
        start = []
        for entity, timeline in content["timelines"].items():
            if not timeline["run"] or timeline["active"] is False or not timeline["timelines"]:
                continue
            column = ROW_TIMELINES.get(entity)
            path = norm(row.get(column)) if column and row.get(column) else timeline["timelines"][0]
            start.append({"entity": entity, "timeline": path})
        talk = {}
        for column, entity in (("GreetTimeline", "Play_GreetAnim"), ("ByeTimeline", "Play_ByeAnim"), ("InteractTimeline", "Play_interactLoop")):
            path = norm(row.get(column)) if row.get(column) else ""
            if not path and entity in content["timelines"] and content["timelines"][entity]["timelines"]:
                path = content["timelines"][entity]["timelines"][0]
            if path:
                talk[column] = path
        presence = tables.presence(item["position"])
        scale = [float(row.get(f"ScaleOverride_{axis}") or 0.0) for axis in "XYZ"]
        persons.append({
            "key": f"{item['region']}:{item['placement'] or 'chunk'}:{item['entity']}:{item['position'][0]:.2f},{item['position'][1]:.2f}",
            "region": item["region"], "placement": item["placement"], "origin": item["origin"],
            "chain": [link["slice"].rsplit("/", 1)[-1].replace(".dynamicslice", "") + (f"[{link['variant']}]" if link.get("variant") else "")
                      for link in item["chain"]],
            "position": item["position"], "yaw": item["yaw"], "scale": item["scale"] * (scale[0] if scale[0] > 0 else 1.0),
            "talkFrame": item.get("talkFrame"),
            "npcId": npc_id, "nameKey": name_key or data.get("GenericName", ""),
            "name": tables.text(name_key) or tables.text(data.get("GenericName", "")), "titleKey": data.get("Title", ""),
            "title": tables.text(data.get("Title", "")), "cdf": cdf, "look": cdf_look(cdf),
            "attachedMesh": norm(row.get("AttachedMesh")), "attachments": content["attachments"],
            "start": start, "talkTimelines": talk, "lookAt": content["lookAt"], "interact": content["interact"],
            "conversationStates": [data.get(f"ConversationStateId{i}") for i in range(1, 11) if data.get(f"ConversationStateId{i}")],
            "swap": (presence or {}).get("swap") or row.get("SwapAchievementId", ""),
            "showOnLocked": bool((presence or {}).get("showOnLocked", False)), "presence": presence is not None,
            "presenceRule": presence_rule((presence or {}).get("swap") or row.get("SwapAchievementId", ""), tables.achievement_bits),
            "interactionData": interaction_of(item, tables),
        })
        # The presence of the backend for a person with interaction data (format 4, stage 2.5c: showWhen or hideWhen with a conditional;
        # no presence: always there). Two persons of one spawner place can have one rule of the level data and different rules of the
        # backend (Aelstrom WW_01 and the season person NPC_03976 on the Windsward beach: AST-248).
        linked = tables.npc_objects.get(persons[-1]["interactionData"] or "") or tables.child_by_key.get(persons[-1]["interactionData"] or "")
        if linked is not None and "person" in linked:
            backend = linked["person"].get("presence")
            rule = [] if not backend else presence_rule(backend.get("condition", ""), tables.achievement_bits)
            if backend and backend.get("mode") == "hideWhen":
                rule = rule + ["!"]
            persons[-1]["presenceRule"] = rule
            # The rule of the backend is the whole presence (the client shows a person while its rule is true): no ShowOnAchievementLocked;
            # no presence of the backend is no swap (always there).
            persons[-1]["swap"] = backend.get("condition", "") if backend else ""
            persons[-1]["showOnLocked"] = False
            persons[-1]["presenceSource"] = "backend"
    return persons


PRESETS = "sharedassets/springboardentitites/datatables/cinematics/javelindata_cutscenecamerapresets.datasheet"


def camera_point(text, dummy: dict | None) -> list[float]:
    """@return A camera point of a line or a preset in the frame of the speaker, or [] when the client cannot place it. The values
    have a frame mark (the localization attributes cameraStateOrigin and cameraStateLookAt, the preset columns): none or "s" (the
    frame of the speaker; "s 1.4 2.2 1.7": the line 01_Urda_Default_00, AST-244 item 6), "p" (the frame of the player: in a Talk the
    dummy of the preset, "p .5 -2.25 1.9"), "n NPC_ID" (the frame of a named person: [] here; no first line of a person uses it)."""
    values = str(text or "").replace(",", " ").split()
    if values and values[0] == "s":
        return numbers(" ".join(values[1:]))
    if values and values[0] == "p":
        point = numbers(" ".join(values[1:]))
        if dummy is None or len(point) < 3:
            return []
        # The dummy frame in the frame of the speaker: its place, turned by its z rotation (180 degrees in each Talk preset of the data).
        angle = math.radians(dummy["rotation"][2])
        x, y = point[0] * math.cos(angle) - point[1] * math.sin(angle), point[0] * math.sin(angle) + point[1] * math.cos(angle)
        return [round(dummy["position"][0] + x, 6), round(dummy["position"][1] + y, 6), round(dummy["position"][2] + point[2], 6)]
    if values and values[0] == "n":
        return []
    return numbers(text)


def numbers(text) -> list[float]:
    try:
        return [float(v) for v in str(text or "").replace(",", " ").split()]
    except ValueError:
        return []


def interaction_of(item: dict, tables) -> str | None:
    """The key of the npc object of a person: a direct person by its placement; a spawner child (AST-248) by the placement of its spawner
    and its place (the object is at the place of the spawner, the same as the place of the child: within 5 cm)."""
    if len(item["chain"]) == 1:
        return tables.npc_objects[item["placement"]]["key"] if item["placement"] in tables.npc_objects else None
    for o in tables.child_objects.get(item["placement"], []):
        if all(abs(a - b) < 0.05 for a, b in zip(o["position"], item["position"])):
            return o["key"]
    return None


def conversations(persons: list) -> None:
    """Adds the conversation of each person with conversation states: the state that the client takes for a character with no
    faction, level, or achievement of a requirement (the list in reverse order, the first state with no requirement: client code
    0x37AC710, research p48-talk), its line (the localization entry of DefaultDialogue: the text with no tags, the speaker, the next
    line, the camera attributes), and the camera preset of the line (javelindata_cutscenecamerapresets.datasheet; a line attribute
    replaces the column of the same name [inference of the research])."""
    states: dict[str, dict] = {}
    for path in source.paks().paths(datasheet.TABLES_FOLDER):
        if not path.endswith(".datasheet"):
            continue
        try:
            sheet = datasheet.Sheet(source.read(path))
        except Exception:
            continue
        if sheet.type == "ConversationStateData":
            for row in sheet.rows():
                if row.get("ConversationStateId"):
                    states.setdefault(str(row["ConversationStateId"]), row)
    presets = {str(r["CutsceneCameraId"]): r for r in datasheet.Sheet(source.read(PRESETS)).rows()}
    wanted: dict[str, dict] = {}
    for person in persons:
        chosen = None
        for state_id in reversed(person["conversationStates"]):
            row = states.get(str(state_id))
            if row is None:
                continue
            if any(key.startswith("Required") and value not in (None, "", 0, 0.0, False) for key, value in row.items()):
                continue
            chosen = row
            break
        person["conversation"] = None
        if chosen and chosen.get("DefaultDialogue"):
            key = str(chosen["DefaultDialogue"]).lstrip("@").lower()
            person["conversation"] = {"state": chosen["ConversationStateId"], "line": key}
            wanted[key] = {}
    if not wanted:
        return
    import html
    import re
    files = [source.read(path).decode("utf-8", "replace") for path in source.paks().paths("localization/en-us/") if path.endswith(".loc.xml")]
    # The lines of a conversation: the first line and its chain of dialogue-next (the Next key of the screen; at most 10 lines). Each
    # pass reads the entries that the chain asks for.
    for _ in range(10):
        missing = {key for key, value in wanted.items() if not value}
        if not missing:
            break
        for text in files:
            for match in re.finditer(r"<string\s+([^>]*?)>(.*?)</string>", text, re.S):
                attrs = dict(re.findall(r'([\w-]+)="([^"]*)"', match.group(1)))
                key = attrs.get("key", "").lower()
                if key in missing and not wanted[key]:
                    # The gesture tags (<g id=.../>) are escaped in the file: the screen removes each tag (research p48-talk).
                    wanted[key] = dict(attrs, text=re.sub(r"<[^>]*>", "", html.unescape(match.group(2))).strip())
        for key in missing:
            following = str(wanted[key].get("dialogue-next", "")).lstrip("@").lower()
            if not wanted[key]:
                wanted[key] = {"missing": True}
            elif following and following not in wanted:
                wanted[following] = {}
    for person in persons:
        conversation = person.get("conversation")
        if not conversation:
            continue
        line = wanted.get(conversation["line"], {})
        conversation.update({"text": line.get("text", ""), "speaker": line.get("speaker", ""), "next": line.get("dialogue-next", "")})
        lines, key = [], conversation["line"]
        while key and key not in [entry["line"] for entry in lines] and len(lines) < 10 and wanted.get(key, {}).get("text"):
            entry = wanted[key]
            lines.append({"line": key, "text": entry["text"], "speaker": entry.get("speaker", "")})
            key = str(entry.get("dialogue-next", "")).lstrip("@").lower()
        conversation["lines"] = lines
        preset = presets.get(line.get("cutsceneCameraPresetId", ""), {})
        state = line.get("cameraState") or preset.get("CameraState", "")
        fov = re.search(r"FOV(\d+)", str(state))
        # The dummy of the player (a two-shot preset): the body of the player at a place in the frame of the speaker ("s"), turned by
        # the rotation (degrees about x, y, z), armed or not; the real player is hidden (HidePlayerAvatar).
        place = str(preset.get("DummyPresetPos") or "").split()
        turn = str(preset.get("DummyPresetRot") or "").split()
        dummy = None
        if len(place) == 4 and place[0] == "s":
            dummy = {"position": [float(v) for v in place[1:]],
                     "rotation": [float(v) for v in turn[1:]] if len(turn) == 4 and turn[0] == "s" else [0.0, 0.0, 0.0],
                     "armed": str(preset.get("DummyIsArmed", "")).lower() == "true"}
        origin = camera_point(line.get("cameraStateOrigin") or preset.get("CameraStateOrigin"), dummy)
        look_at = camera_point(line.get("cameraStateLookAt") or preset.get("CameraStateLookAt"), dummy)
        if not origin and line.get("cameraStateOrigin"):
            # [I] A line point that the client cannot place (the frame of the player with no dummy: 06_FrancisTurner, "p .5 -2.25 1.9"):
            # the origin and the look-at of the preset as one pair (an empty origin puts the camera into the person).
            origin = camera_point(preset.get("CameraStateOrigin"), dummy)
            look_at = camera_point(preset.get("CameraStateLookAt"), dummy)
        conversation["camera"] = {
            "preset": line.get("cutsceneCameraPresetId", ""), "state": state,
            "fov": float(fov.group(1)) if fov else None,
            "origin": origin,
            "lookAt": look_at,
            "hidePlayer": str(preset.get("HidePlayerAvatar", "")).lower() == "true",
            "enterBlend": float(preset.get("EnterBlendTime") or 0.0), "exitBlend": float(preset.get("ExitBlendTime") or 0.0)}
        if dummy:
            conversation["camera"]["dummy"] = dummy


def timeline_clips(persons: list) -> dict:
    """@return Each start timeline and each talk timeline (GreetTimeline, InteractTimeline, ByeTimeline: the Talk of stage P4.8) of
    the persons with the files of its clips (the aliases of the .cdf of its first person)."""
    out: dict[str, dict] = {}
    for person in persons:
        names = clips_of(person["cdf"])
        paths = [start["timeline"] for start in person["start"]] + [norm(p) for p in (person.get("talkTimelines") or {}).values() if p]
        for path in paths:
            if path in out:
                continue
            timeline = read_timeline(path)
            if timeline is None:
                out[path] = {"missing": True}
                continue
            layers = []
            for layer in timeline["layers"]:
                clips = []
                for clip in layer["clips"]:
                    file = names.get(clip["alias"]) or names.get(clip["alias"].lower()) or ""
                    clips.append(dict(clip, file=norm(file)))
                layers.append({"layer": layer["layer"], "clips": clips})
            out[path] = {"layers": layers}
    return out


def presence_rule(text: str, bits: dict) -> list:
    """@return The swap expression of a person in postfix order: a bit of the Achievement state (an int >= 0), -1 for an achievement
    that the character cannot have (a non-persisted achievement "*Name": the objective flow of a quest, which the backend does not
    keep; an unknown id), "!" (not), "&" (and), "|" (or). The data uses the operators !, &&, || and parentheses (887 expressions of
    the world; one expression is a timeline path: no known id, thus false) [the precedence ! > && > || is an inference]."""
    import re
    tokens = re.findall(r"&&|\|\||!|\(|\)|\*?[A-Za-z0-9_]+|\S", text or "")
    out: list = []
    ops: list = []
    rank = {"!": 3, "&": 2, "|": 1}
    for token in tokens:
        token = {"&&": "&", "||": "|"}.get(token, token)
        if token == "(":
            ops.append(token)
        elif token == ")":
            while ops and ops[-1] != "(":
                out.append(ops.pop())
            if ops:
                ops.pop()
        elif token in rank:
            while ops and ops[-1] != "(" and token != "!" and rank[ops[-1]] >= rank[token]:
                out.append(ops.pop())
            ops.append(token)
        elif re.fullmatch(r"[A-Za-z0-9_]+", token):
            out.append(bits.get(token.lower(), -1))
        else:
            out.append(-1)   # a non-persisted achievement, or a token of no id
    while ops:
        token = ops.pop()
        if token != "(":
            out.append(token)
    return out


def evaluate(rule: list, bits: set) -> bool:
    """@return The value of a presence rule for the achievement bits of a character (a malformed rule is false)."""
    stack: list = []
    for token in rule:
        if token == "!":
            if not stack:
                return False
            stack.append(not stack.pop())
        elif token in ("&", "|"):
            if len(stack) < 2:
                return False
            b, a = stack.pop(), stack.pop()
            stack.append((a and b) if token == "&" else (a or b))
        else:
            stack.append(token >= 0 and token in bits)
    return len(stack) == 1 and stack[0]


def visible(person: dict, bits: set | None = None) -> bool:
    """The presence (research p48-persons, presence.json): a person with no swap shows; else it shows while its swap expression is
    true, or while it is false with ShowOnAchievementLocked [the runtime has the same rule with the Achievement state of the
    character]. With no bits: a character with no achievement of a swap."""
    if not person["swap"]:
        return True
    return evaluate(person["presenceRule"], bits or set()) != person["showOnLocked"]


def write(near: tuple[float, float, float] | None = None, out_file: str | None = None) -> dict:
    """Writes SWC_CONTENT/persons/persons.json (or out_file): the persons of the main world (or within a radius of a point: x y
    radius)."""
    names = census.world_regions()
    if near is not None:
        # Only the regions under the circle (2048 m regions; a spawner child stays near its spawner).
        x, y, radius = near
        names = [n for n in names if any(abs(a * 2048 + 1024 - c) <= 1024 + radius for a, c in [(int(n[2:].split("_")[0]), x)])
                 and abs(int(n[2:].split("_")[1]) * 2048 + 1024 - y) <= 1024 + radius]
    found = []
    for name in names:
        found += census_region(name)
    if near is not None:
        found = [f for f in found if math.hypot(f["position"][0] - x, f["position"][1] - y) <= radius]
    tables = Tables()
    persons = resolve(found, tables)
    conversations(persons)
    timelines = timeline_clips(persons)
    target = Path(out_file) if out_file else env.content_dir() / "persons" / "persons.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    bodies = sorted({p["cdf"] for p in persons})
    items = sorted({(i["mesh"], i["material"]) for p in persons for i in p["look"]["items"]}
                   | {(p["attachedMesh"], "") for p in persons if p["attachedMesh"]})
    clips = sorted({c["file"] for t in timelines.values() for layer in t.get("layers", []) for c in layer["clips"]
                    if c["file"].endswith(".caf")})
    doc = {"format": 1, "skeletonGender": GENDER, "persons": persons, "timelines": timelines,
           "wanted": {"bodies": bodies, "items": [list(i) for i in items], "clips": clips}}
    target.write_text(json.dumps(doc, indent=1))
    return {"persons": len(persons), "visible for a new character": sum(1 for p in persons if visible(p)),
            "with talk": sum(1 for p in persons if p["interact"] and "OpenConversation" in (p["interact"].get("options") or [])),
            "with interaction data": sum(1 for p in persons if p["interactionData"]),
            "with a conversation line": sum(1 for p in persons if p.get("conversation") and p["conversation"].get("text")),
            "with a camera preset": sum(1 for p in persons if p.get("conversation") and (p["conversation"].get("camera") or {}).get("preset")),
            "bodies": len(bodies), "items": len(items),
            "clips": len(clips), "timelines": len(timelines), "file": str(target)}


# ------------------------------------------------------------------------------------------------- the player pipeline (Unreal)

def cloth_render_skin(cloth: str) -> str:
    """@return The render mesh of a .cloth file: a skin on the player skeleton (the rule of player.cloth_parts: no simulation)."""
    import re
    try:
        skins = re.findall(rb"[A-Za-z0-9_/\.\-]+\.skin", source.read(cloth))
    except Exception:
        return ""
    return norm(skins[0].decode()) if skins else ""


def body(cdf: str, look: dict, force: bool = False, doc: dict | None = None) -> dict:
    """Converts the body of a person (its visible skins and the render skins of its cloth) onto the player skeleton: one glTF of
    the manifest kind "part" (one skeletal mesh on the skeleton of the base)."""
    key = f"person|{cdf}"
    # The folder PersonBodies: one skeletal mesh for each body (an older import in Persons had one asset for each skin).
    folder = player.unreal_folder("PersonBodies", cdf)
    path = player.player_dir() / "persons" / (folder.rsplit("/", 1)[-1] + ".gltf")
    if path.is_file() and not force and key in doc["assets"]:
        return {"body": cdf, "skipped": "exists"}
    path.parent.mkdir(parents=True, exist_ok=True)
    skeleton = player.Skeleton(GENDER)
    w = gltf.Writer()
    _, roots = skeleton.write(w)
    reports = []
    missing = []
    for skin, material in body_skins(look):
        if not source.has(skin):
            missing.append(skin)   # a skin of the .cdf that is not in the game copy (9 bodies): the body has no such part
            continue
        reports.append(skeleton.add_skin(w, skin, material, path, "SK_" + skin.rsplit("/", 1)[-1].split(".")[0], 1024))
    if not reports:
        raise ValueError("no skin of the body is in the game copy: " + ", ".join(missing))
    # One mesh node with the primitives of each skin: Unreal makes one skeletal mesh for each mesh node of a glTF.
    nodes = w.doc["nodes"]
    scene = w.doc["scenes"][0]["nodes"]
    mesh_nodes = [index for index in scene if "mesh" in nodes[index]]
    if len(mesh_nodes) > 1:
        primitives = [primitive for index in mesh_nodes for primitive in w.doc["meshes"][nodes[index]["mesh"]]["primitives"]]
        first = nodes[mesh_nodes[0]]
        w.doc["meshes"][first["mesh"]]["primitives"] = primitives
        for index in mesh_nodes[1:]:
            scene.remove(index)
    if mesh_nodes:
        nodes[mesh_nodes[0]]["name"] = "SK_" + folder.rsplit("/", 1)[-1]
        w.doc["meshes"][nodes[mesh_nodes[0]]["mesh"]]["name"] = "SK_" + folder.rsplit("/", 1)[-1]
    w.doc["scenes"][0]["nodes"] += roots
    w.save(path)
    doc["assets"][key] = {"kind": "part", "gender": GENDER, "file": str(path.relative_to(env.content_dir())), "unreal": folder}
    return {"body": cdf, "skins": len(reports), **({"missing": missing} if missing else {})}


def clip_pack(files: list, force: bool = False, doc: dict | None = None) -> dict:
    """Converts the clips of the start and talk timelines of the persons into one pack on the player skeleton (the root bone locked)."""
    key = f"clips|{player.GENDERS[GENDER]}|persons"
    path = player.player_dir() / "clips" / player.GENDERS[GENDER] / "persons" / "clips.gltf"
    known = doc["assets"].get(key, {}).get("clips", [])
    if path.is_file() and not force and sorted(known) == sorted(files):
        return {"clips": key, "skipped": "exists"}
    path.parent.mkdir(parents=True, exist_ok=True)
    skeleton = player.Skeleton(GENDER)
    w = gltf.Writer()
    first, roots = skeleton.write(w)
    w.doc["scenes"][0]["nodes"] += roots
    failed = {}
    for file in files:
        try:
            skeleton.add_clip(w, first, file, file.rsplit("/", 1)[-1].rsplit(".", 1)[0], root_lock=True)
        except Exception as error:   # a clip that does not read: the person plays no clip for it
            failed[file] = str(error)[:120]
    w.save(path)
    doc["assets"][key] = {"kind": "clips", "gender": GENDER, "tags": ["persons"], "file": str(path.relative_to(env.content_dir())),
                          "unreal": f"{player.UNREAL_ROOT}/{GENDER}/Clips/persons", "clips": files}
    return {"clips": key, "count": len(files) - len(failed), "failed": failed}


def convert(force: bool = False) -> dict:
    """Converts the wanted bodies, items, and clips of persons.json with the player pipeline (then tools/import-player.sh)."""
    doc_persons = json.loads((env.content_dir() / "persons" / "persons.json").read_text())
    looks = {p["cdf"]: p["look"] for p in doc_persons["persons"]}
    manifest = player.load_manifest()
    report = {"bodies": [], "items": [], "clips": None, "failed": {}}
    for cdf in doc_persons["wanted"]["bodies"]:
        try:
            report["bodies"].append(body(cdf, looks[cdf], force, manifest))
        except Exception as error:
            report["failed"][cdf] = str(error)[:160]
    for mesh, material in doc_persons["wanted"]["items"]:
        try:
            # An item with no material in the .cdf: the material of the mesh (its material name chunk).
            material = material or (cgf.material_guid(cgf.read_chunks(source.read(mesh))) or "")
            report["items"].append(player.weapon(f"personitem|{mesh}", [[mesh, material]], force, manifest))
        except Exception as error:
            report["failed"][mesh] = str(error)[:160]
    report["clips"] = clip_pack(doc_persons["wanted"]["clips"], force, manifest)
    player.save_manifest(manifest)
    return {"bodies": len(report["bodies"]), "items": len(report["items"]), "clips": report["clips"], "failed": report["failed"],
            "missing skins": {b["body"]: b["missing"] for b in report["bodies"] if b.get("missing")}}


def read_line_attributes(keys: set[str]) -> dict:
    """The localization entries of the text keys and of their chains of dialogue-next (at most 10 passes): key -> {speaker, next}
    (the attributes of the en-us .loc.xml files; the text itself is in the UI localization)."""
    import re
    files = [source.read(path).decode("utf-8", "replace") for path in source.paks().paths("localization/en-us/") if path.endswith(".loc.xml")]
    entries: dict[str, dict] = {}
    for text in files:
        for match in re.finditer(r"<string\s+([^>]*?)>", text):
            attrs = dict(re.findall(r'([\w-]+)="([^"]*)"', match.group(1)))
            key = attrs.get("key", "").lower()
            if key and key not in entries:
                entries[key] = attrs
    out: dict[str, dict] = {}
    wanted = set(keys)
    for _ in range(10):
        new: set[str] = set()
        for key in wanted - set(out):
            attrs = entries.get(key)
            if attrs is None:
                continue
            following = str(attrs.get("dialogue-next", "")).lstrip("@").lower()
            out[key] = {"speaker": attrs.get("speaker", ""), "next": following}
            # AST-239: the player reply that leads to this line (dialogue-prompt: the option text of the line before it; retail 1.8b
            # P1-05 "Well... born again?").
            prompt = str(attrs.get("dialogue-prompt", "")).strip()
            if prompt:
                out[key]["prompt"] = prompt
            if following:
                new.add(following)
        if not new - set(out):
            break
        wanted |= new
    return out


def write_conversations() -> dict:
    """SWC_CONTENT/ui/gamedata/conversations.json (Linear AST-248; docs/PERSONS.md "The conversation state"): the conversation data of
    the backend copy (gamedata/conversation-data.json, format 1: the NPC rows with their states in the order of their columns, the states
    with their requirements and topics, the topics with their requirements and ProvidedTopics) for the selection of the client
    (swc/conversation.lua), and the speaker and the next line of each text key of a state or a topic response."""
    data = json.loads((creature.gamedata_dir() / "conversation-data.json").read_text())
    # AST-239: "initial" (IsInitialState: the validity of a record, backend stage 1.8f) and "objectives" (the LinearObjectiveId1 to N
    # of the state: its offers).
    states = {s["id"]: {"dialogue": s["dialogue"].lstrip("@").lower(), "requires": s["requires"], "topics": s["topics"],
                        "services": s["services"], "type": s["type"], "initial": bool(s.get("initial")),
                        "objectives": list(s.get("objectives") or [])} for s in data["states"]}
    topics = {t["id"]: {"crc": t["crc"], "prompt": t["prompt"], "response": t["response"].lstrip("@").lower(), "requires": t["requires"],
                        "provided": [x.strip() for x in str(t.get("providedTopics") or "").split(",") if x.strip()]}
              for t in data["topics"]}
    npcs: dict[str, list] = {}
    for row in data["npcs"]:
        npcs.setdefault(row["id"].lower(), row["states"])
    keys = {s["dialogue"] for s in states.values() if s["dialogue"]} | {t["response"] for t in topics.values() if t["response"]} \
        | {t["prompt"].lstrip("@").lower() for t in topics.values() if t["prompt"]}
    # AST-239: the conversation texts of the objectives (the columns *Prompt and *Response of the objective rows of quest-data.json:
    # PlayerPrompt, ObjectiveProposalResponse, InProgressResponse, DestinationCompletionAvailablePrompt and Response, ...) and their chains.
    quest_file = creature.gamedata_dir() / "quest-data.json"
    if quest_file.is_file():
        for objective in json.loads(quest_file.read_text()).get("objectives", []):
            for column, value in (objective.get("cells") or {}).items():
                if (column.endswith("Prompt") or column.endswith("Response")) and str(value).startswith("@"):
                    keys.add(str(value).lstrip("@").lower())
    lines = read_line_attributes(keys)
    doc = {"format": 1, "source": data.get("format"), "npcs": npcs, "states": states, "topics": topics, "lines": lines}
    target = env.content_dir() / "ui" / "gamedata"
    target.mkdir(parents=True, exist_ok=True)
    (target / "conversations.json").write_text(json.dumps(doc, separators=(",", ":"), sort_keys=True))
    return {"npcs": len(npcs), "states": len(states), "topics": len(topics), "lines": len(lines), "keys": len(keys),
            "keysWithNoEntry": len(keys - set(lines)), "bytes": (target / "conversations.json").stat().st_size}


def main(argv=None) -> int:
    import argparse
    parser = argparse.ArgumentParser(prog="python3 -B -m swc_convert.persons")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("write")
    p.add_argument("--near", nargs=3, type=float, metavar=("X", "Y", "RADIUS"))
    p.add_argument("--out", help="the output file (default SWC_CONTENT/persons/persons.json)")
    p = sub.add_parser("convert")
    p.add_argument("--force", action="store_true")
    sub.add_parser("conversations")
    args = parser.parse_args(argv)
    if args.command == "conversations":
        result = write_conversations()
    else:
        result = write(tuple(args.near) if args.near else None, args.out) if args.command == "write" else convert(args.force)
    print(json.dumps(result, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
