"""The actions of the player with a weapon (stage P2.7): the stances, the draw and the sheathe, and the attack graph.

Writes SWC_CONTENT/player/actions.json (tools/convert-player.sh actions):

- weapons: for each weapon tag set, the stance fragments (Idle, Strafe, Sprint, ...) found through the SubADB chain of
  player.adb, the off-hand fragments of a shield, the draw and the sheathe fragments, and (melee) the attack graph of
  the backend copy (SWC_GAMEDATA_DIR/weapon-actions.json: the same graph as the server, protocol notes 4.32).
- fragments: the layers of each fragment (a sequence of clips or blend spaces with their blend, speed, start, and
  weight), its blend-out time, and its marker windows (CAGE-Named: start and end in seconds).
- activities: for each attack fragment, the AnimationDrivenMotion, CameraRelativeFacing, and InputRelativeFacing of its
  action (playeractions_attack.actionlist) with their conditionals, and the named conditions of the action list.
- clips: for each clip, its path, its duration, its flags (1: additive, 2: loop), its animated bones, and the root track
  of the clips of the attacks (the action moves the character with it: AnimationDrivenMotion).
- the strikes of each graph fragment (stage P2.8): the damage key, the window, and the shapes of the backend file, with the
  rotation offsets and the flags of their CAGE-Damage items (the backend file has no rotation); weaponJoints: the joints of
  the weapon skeletons of the melee weapons (the frames of the shapes of the weapon).
- blendSpaces: the blend spaces of the layers.
- weaponScope (AST-202): for each weapon tag set, the attachment database of its weapon (AnimDbPath), the weapon record of each
  fragment record of the main hand that owns the scope Weapon or WeaponTransition (playermalecontrollerdefs.xml), and the weapon
  clips of these records (in clips with "prop": true; their bones are the bones of the weapon skeletons). docs/GEAR.md.
- ranged (stage P3.6, ranged.py): for the bow, the fire staff, and the life staff, the actions, the fragments with their timed
  items, the abilities, and the named conditions of the action lists; the shared part: the shot rules with their flight, the
  weapon profiles, and the spells of the spell tables.
- the abilities of each melee weapon (stage P3.5): the ability id and its CRC-32 (the id on the bar), the fragment, the
  selections, the casts, and the cooldown (ability-data.json of the backend copy); the shots: each cast of a projectile spell
  at a named mark (CastingTypes Projectile of the spell tables), with the CRC-32 of its RangedAttackName (the value "attack"
  of the shot item, protocol notes 4.27: Gravity Well 8E14366B).

Research: ~/Games/soulwarden-client/research/gear.md ("Sheathe and draw animation"), feel-values.md R3, the Soulwarden
protocol notes 4.32 (the attack graph of the client).
"""
from __future__ import annotations

import json
import os
import re
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

from . import anim, cgf, datasheet, source
from .bounds import BoundShapes
from . import gather_actions, player, ranged

ADB_FOLDER = "animations/mannequin/adb/player/"
ATTACK_ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_attack.actionlist"
DODGE_ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_dodge.actionlist"
# The death and the respawn of the player (stage P2.10, playeractions_death.actionlist): the action Death plays r_Death with
# the tag Unarmed (it sheathes the weapons and sets Unarmed) and holds it; the action Respawn plays Respawn (the tag Respawn
# of the death) until the fragment is done.
DEATH_ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_death.actionlist"
LIFE_FRAGMENTS = {"death": ("r_Death", "Unarmed"), "respawn": ("Respawn", "")}
# The timed use of an object (stage P2.10, playeractions_interact.actionlist): the action DelayedInteraction (the options
# Trigger, PickupBuildable, RevivePlayer) sheathes the weapons and plays Delayed_Interact (the kneel) while the request lasts.
INTERACT_ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_interact.actionlist"
USE_FRAGMENTS = {"delayed": ("Delayed_Interact", "")}
# The use of a consumable (stage MVP-E, playeractions_use.actionlist Use_QuickSlot_1 to 4): the fragment Use of player.adb for the
# MannequinTag of the consumable (javelindata_itemdefinitions_consumables: Potion, Eat, WeaponOil, Boost) and the posture: the
# upper-body clips unarmed_potion_drink_ub, unarmed_eatfromhand_ub (speed 1.5), unarmed_drink_ub (speed 1.3; the variant with no
# tag, also for Boost), rswrd_imbue (WeaponOil). Keys "use|POSTURE|TAG".
CONSUMABLE_TABLE = "javelindata_itemdefinitions_consumables.datasheet"
USE_TAGS = ("", "Drink", "Potion", "Eat", "Bandage", "WeaponOil")
for _posture in ("", "Crouch", "Prone"):
    for _tag in USE_TAGS:
        USE_FRAGMENTS[f"use|{_posture}|{_tag}"] = ("Use", "+".join(t for t in (_posture, _tag) if t))
# The dodge fragments of the load classes (stamina-data.json of the backend copy), with the direction variants of the move keys
# (no tag: forward). The Prone variants are not used (the backend file lists them as not used).
DODGE_FRAGMENTS = ("Dodge_Roll_Fast", "Dodge_Roll", "Dodge_Roll_Slow")
DODGE_VARIANTS = ("", "BackNav", "LeftNav", "RightNav")
# The locomotion fragments of a stance, with the variant tags of each (the unarmed set of locomotion.json).
# Stage P3.3: the crouch variants (a weapon has crouch fragments: 1H_Melee, 2H_Melee, the bow, the magic weapons, the shields;
# research p33-movement.md Q1); a weapon with no crouch fragment uses the unarmed one of locomotion.json.
# Stage P3.G: the block variants of Idle and Strafe (tag Blocking: the stand and the move of the block, rswrd_block_idle and
# rswrd_block_strafe_blend of a 1H melee weapon), and the aim variants of a bow (Aim: bow_Aim_HalfDrw_Idle and
# bow_aim_halfdrw_strafe_blend; ChargedAim: the full draw).
STANCE_FRAGMENTS = {"Idle": ["", "Crouch", "Blocking", "Aim", "ChargedAim"], "Strafe": ["", "Crouch", "Blocking", "Aim", "ChargedAim"],
                    "Sprint": ["", "Crouch"], "Sprint_Start": ["", "Crouch"],
                    "Fall": ["ForwardNav", ""],
                    "Land": ["Soft", "Normal", "Hard"], "Jump": ["ForwardNav", "BackNav", "LeftNav", "RightNav", ""],
                    "Jump_Standing": [""],
                    # AST-199: the stop (Nav_Stop with the NavType tag at its request; the rapier: cmbt_walk_stop_1h_f with the pose layer
                    # rapier_pose_add, the sprint rswrd_sprint_stop_blend with rapier_sprint_stop_blend).
                    "Nav_Stop": ["Walk", "Run", "Sprint"]}
# The context tags that a fragment of a settled stance can have.
CONTEXT_TAGS = {"stand"}
# AST-202 (research weapon-props.md "Retail playback rule"): the controller of the player. A fragment owns its scopes (FragmentDefs,
# with the tag overrides); the scopes Weapon and WeaponTransition are in the context WeaponContext, which reads the attachment
# database of the weapon (AnimDbPath of its weapon row). The weapon clips are under the prop folder.
CONTROLLER_DEFS = ADB_FOLDER + "playermalecontrollerdefs.xml"
WEAPON_SCOPES = ("Weapon", "WeaponTransition")
PROP_FOLDER = "animations/gameplay/prop/weapons/"
MOTION_ACTIVITIES = ("AnimationDrivenMotion", "CameraRelativeFacing", "InputRelativeFacing")


def gamedata_dir() -> Path:
    return Path(os.environ.get("SWC_GAMEDATA_DIR", str(Path.home() / "Games/soulwarden-client/backend/current/gamedata")))


def tag_set(text: str) -> set[str]:
    return {tag.strip().lower() for tag in text.replace(",", "+").split("+") if tag.strip()}


def adb_chain(tags: set[str]) -> list[str]:
    """The animation databases of a tag set: the SubADBs of player.adb whose tags it has (in their order), then player.adb
    (Mannequin tags compare without case)."""
    root = player.adb_root(player.ADB)
    chain = [player.norm(sub.get("File")) for sub in root.find("SubADBs") if tag_set(sub.get("Tags", "")) <= tags]
    return chain + [player.norm(player.ADB)]


# The posture tags: a variant with one of them takes only a fragment with it (else the stand fragment is no crouch fragment).
POSTURE_TAGS = {"crouch", "prone"}
# The tags that a variant takes only in a fragment with them: the postures, the block, and the aim (stage P3.G: the rapier has no Idle
# with Blocking in player_rapier.adb; the variant is the 1H_Melee+Blocking fragment of player_1h_melee.adb, the next database of the
# chain).
REQUIRED_TAGS = POSTURE_TAGS | {"blocking", "aim", "chargedaim"}


def find_fragment(name: str, tags: set[str], extra: set[str], offhand: bool) -> tuple[str, ET.Element] | None:
    """The fragment variant of the first database of the chain that has one: the variant with the most tags, each of them
    in the tags, the extra tags, or the context tags. offhand: the variants of the Offhand scope (tag Offhand). A posture tag
    or the block tag of the extra tags must be in the fragment."""
    required = extra & REQUIRED_TAGS
    for adb in adb_chain(tags):
        node = player.adb_root(adb).find(f"FragmentList/{name}")
        best = None
        for fragment in node if node is not None else []:
            names = tag_set(fragment.get("Tags") or "")
            if ("offhand" in names) != offhand or not required <= names:
                continue
            if names <= tags | extra | CONTEXT_TAGS and (best is None or len(names) > best[0]):
                best = (len(names), fragment)
        if best is not None:
            return adb, best[1]
    return None


def scope_table() -> dict[str, tuple[str, list]]:
    """The scopes of each fragment of the controller (lower-case name -> (the default scopes, [(override tags, scopes)]))."""
    root = ET.fromstring(source.read(CONTROLLER_DEFS).decode("utf-8", "replace"))
    return {fragment.tag.lower(): (fragment.get("scopes") or "", [(tag_set(o.get("tags") or ""), o.get("scopes") or "")
                                                                  for o in fragment.findall("Override")])
            for fragment in root.find("FragmentDefs")}


def scopes_of(table: dict, name: str, tags: set[str]) -> list[str]:
    """The scopes of a fragment for a tag state: the first override (in the order of the file) whose tags the state has, else the
    default scopes. The order rule is an inference (the overrides of the file go from the most tags to the fewest)."""
    default, overrides = table.get(name.lower(), ("", []))
    scopes = next((text for need, text in overrides if need <= tags), default)
    return [scope for scope in scopes.split("+") if scope]


def weapon_variant(adb: str, name: str, tags: set[str]) -> ET.Element | None:
    """The variant of a fragment (no case) of a weapon database for a tag state: the variant with the most tags, each one in the
    state (the first of equal variants: the order of the file; research weapon-props.md, the option policy is [U])."""
    best = None
    fragment_list = player.adb_root(adb).find("FragmentList")
    for node in fragment_list if fragment_list is not None else []:
        if node.tag.lower() != name.lower():
            continue
        for fragment in node:
            names = tag_set(fragment.get("Tags") or "")
            if names <= tags and (best is None or len(names) > best[0]):
                best = (len(names), fragment)
    return best[1] if best is not None else None


def body_records(out: dict) -> list[str]:
    """The fragment records of a weapon tag set that its main hand plays (not the off-hand fragments of a shield)."""
    keys = list(out["stance"].values()) + list(out["dodge"].values()) + list(out.get("block", {}).values())
    keys += [out[kind] for kind in ("draw", "sheathe") if kind in out]
    keys += [item["fragment"] for item in out.get("graph", []) if "fragment" in item]
    keys += [variant["fragment"] for variants in out.get("ranged", {}).get("fragments", {}).values() for variant in variants]
    return list(dict.fromkeys(keys))


# The parameters of a Homing procedural clip that the client uses (stage P3.G, research p3g-collision: the move to the target of
# an attack: the root motion stops while it runs).
HOMING_PARAMS = {"MoveToTarget": bool, "MoveToDistance": float, "MoveToDuration": float, "MoveToVelocity": float,
                 "MaxMoveDistance": float, "Radius": float, "MaxAngle": float, "Height": float, "TurnRate": float,
                 "UseExponential": bool, "AllowUpwardsMovement": bool}


def homing_of(fragment: ET.Element) -> list[dict]:
    """The Homing procedural clips of a fragment: start and end (seconds; the end None: to the end of the fragment) and the
    parameters of HOMING_PARAMS (the names with a lower-case first letter)."""
    out = []
    for layer in fragment.findall("ProcLayer"):
        time = 0.0
        open_clip = None
        for item in layer:
            if item.tag == "Blend":
                time += float(item.get("ExitTime", 0))
                continue
            if item.tag != "Procedural":
                continue
            if open_clip is not None:
                open_clip["end"] = round(time, 5)
                out.append(open_clip)
                open_clip = None
            if item.get("type") == "Homing":
                open_clip = {"start": round(time, 5), "end": None}
                for name, kind in HOMING_PARAMS.items():
                    value = item.find(f"ProceduralParams/{name}")
                    if value is not None and value.get("value") is not None:
                        text = value.get("value")
                        open_clip[name[0].lower() + name[1:]] = text.lower() == "true" if kind is bool else float(text)
        if open_clip is not None:
            out.append(open_clip)
    return out


def markers_of(fragment: ET.Element) -> list[list]:
    """The CAGE-Named markers of a fragment: [name, start, end] (seconds; end None: to the end of the fragment), and a
    CAGE-TransitionInstant as the marker TransitionInstant. A marker lasts to the next procedural item of its layer."""
    out = []
    for layer in fragment.findall("ProcLayer"):
        time = 0.0
        open_marker = None
        for item in layer:
            if item.tag == "Blend":
                time += float(item.get("ExitTime", 0))
                continue
            if item.tag != "Procedural":
                continue
            if open_marker is not None:
                open_marker[2] = round(time, 5)
                out.append(open_marker)
                open_marker = None
            name = item.find("ProceduralParams/Name")
            if item.get("type") == "CAGE-Named" and name is not None and name.get("value"):
                open_marker = [name.get("value"), round(time, 5), None]
            elif item.get("type") == "CAGE-TransitionInstant":
                # Stage P3.G: the instant change to the next action (Sprint_Start to Sprint at 1.4 s; research p3g-move 3.4).
                open_marker = ["TransitionInstant", round(time, 5), None]
        if open_marker is not None:
            out.append(open_marker)
    return out


def layers_of(fragment: ET.Element, spaces: dict, add_clip, add_space) -> list[list[dict]]:
    """The animation layers of a fragment: each one a sequence of items (Blend then Animation)."""
    layers = []
    for layer in fragment.findall("AnimLayer"):
        items = []
        blend = None
        for item in layer:
            if item.tag == "Blend":
                blend = item
            elif item.tag == "Animation":
                name = (item.get("name") or "").lower()
                entry = {"exit": float(blend.get("ExitTime", 0)) if blend is not None else 0.0,
                         "start": float(blend.get("StartTime", 0)) if blend is not None else 0.0,
                         "blendIn": float(blend.get("Duration", 0)) if blend is not None else 0.0,
                         "speed": float(item.get("speed") or 1.0), "weight": float(item.get("weight") if item.get("weight") is not None else 1.0),
                         "loop": "Loop" in (item.get("flags") or "")}
                if not name:
                    entry["clip"] = ""
                elif name in spaces:
                    entry["blendSpace"] = name
                    add_space(name)
                else:
                    entry["clip"] = name
                    add_clip(name)
                items.append(entry)
                blend = None
        layers.append(items)
    return layers


def damage_params(fragment: ET.Element | None) -> list[dict]:
    """The CAGE-Damage items of a fragment: the damage key and the shape parameters (rotation offsets in degrees)."""
    out = []
    for item in fragment.iter("Procedural") if fragment is not None else []:
        if item.get("type") != "CAGE-Damage":
            continue
        params = {child.tag: child.get("value") for child in item.find("ProceduralParams") or []}
        number = lambda name, default=0.0: float(params.get(name) or default)
        out.append({"key": (params.get("DamageKey") or "").lower(), "type": params.get("MeleeAttackShapeCastType") or "",
                    "joint": params.get("JointName") or "",
                    "offset": [number("OffsetX"), number("OffsetY"), number("OffsetZ")],
                    "rotation": [number("xRotationOffset"), number("yRotationOffset"), number("zRotationOffset")],
                    "offhand": (params.get("UseOffhandWeapon") or "").lower() == "true",
                    "endAsCenter": (params.get("UseEndAsCenter") or "").lower() == "true",
                    "noSight": (params.get("DisableLOSCheck") or "").lower() == "true"})
    return out


def strikes_of(fragment: dict, params: list[dict]) -> list[dict]:
    """The strikes of a graph fragment of the backend file (its damage key, window, and shapes: type, radius, half height,
    box, offset, scale, joint), with the rotation offsets and the flags of the CAGE-Damage item of each shape (the file
    of the backend has no rotation). Stage P3.5: the rows of each strike [row, needs, forbids, shape] (the learned nodes of the
    clip conditions: a strike with no row for the nodes of the character is no strike of it; the shape of a row is the index of
    its shape)."""
    out = []
    for key, start, end, rows, shapes in fragment.get("strikes", []):
        items = []
        for shape in shapes:
            kind, radius, half_height = shape[0], shape[1], shape[2]
            box, offset, scale, joint = shape[3:6], shape[6:9], shape[9:12], shape[12]
            match = next((p for p in params if p["key"] == key.lower() and p["type"] == kind and p["joint"] == joint
                          and all(abs(a - b) < 1e-3 for a, b in zip(p["offset"], offset))), None)
            items.append({"type": kind.replace("MeleeAttackShapeCastType", "").lower(), "radius": radius, "halfHeight": half_height,
                          "box": box, "offset": offset, "scale": scale, "joint": joint,
                          "rotation": match["rotation"] if match else [0.0, 0.0, 0.0], "matched": match is not None,
                          "offhand": bool(match and match["offhand"]), "endAsCenter": bool(match and match["endAsCenter"]),
                          "noSight": bool(match and match["noSight"])})
        out.append({"key": key, "start": start, "end": end, "shapes": items, "rows": [list(row) for row in rows]})
    return out


def quaternion_of(matrix) -> list[float]:
    """@return The quaternion (w, x, y, z) of a rotation matrix (engine space)."""
    m = matrix
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        k = 0.5 / (trace + 1.0) ** 0.5
        q = [0.25 / k, (m[2][1] - m[1][2]) * k, (m[0][2] - m[2][0]) * k, (m[1][0] - m[0][1]) * k]
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        k = 2.0 * (1.0 + m[0][0] - m[1][1] - m[2][2]) ** 0.5
        q = [(m[2][1] - m[1][2]) / k, 0.25 * k, (m[0][1] + m[1][0]) / k, (m[0][2] + m[2][0]) / k]
    elif m[1][1] > m[2][2]:
        k = 2.0 * (1.0 + m[1][1] - m[0][0] - m[2][2]) ** 0.5
        q = [(m[0][2] - m[2][0]) / k, (m[0][1] + m[1][0]) / k, 0.25 * k, (m[1][2] + m[2][1]) / k]
    else:
        k = 2.0 * (1.0 + m[2][2] - m[0][0] - m[1][1]) ** 0.5
        q = [(m[1][0] - m[0][1]) / k, (m[0][2] + m[2][0]) / k, (m[1][2] + m[2][1]) / k, 0.25 * k]
    return [round(float(v), 6) for v in q]


def weapon_joints(models: set[str]) -> dict:
    """The joints of the weapon skeletons (model space: position in meters, rotation w, x, y, z): the frames of the damage
    shapes of the weapon attachment."""
    out = {}
    for model in sorted(models):
        try:
            bones = cgf.read_bones(cgf.read_chunks(source.read(model)))
        except Exception:   # a skeleton that does not read: the shapes use the frame of the weapon
            continue
        out[model] = {b.name: [round(float(v), 6) for v in b.b2w[:3, 3]] + quaternion_of(b.b2w[:3, :3]) for b in bones}
    return out


def condition_json(element: ET.Element) -> dict:
    return {"type": element.tag, "attrs": {k: v for k, v in element.attrib.items() if k != "Name"},
            "children": [condition_json(child) for child in element]}


ABILITY_ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_ability_"


def projectile_spells() -> dict:
    """@return The spells of the spell tables that throw a projectile (CastingTypes Projectile): lower-case SpellID ->
    RangedAttackName (stage P3.5: the attack of the shot item of the cast)."""
    out = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")
                       and "spelltable" in p):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except Exception:
            continue
        if sheet.type != "SpellData":
            continue
        for row in sheet.rows():
            spell, attack = str(row.get("SpellID") or ""), str(row.get("RangedAttackName") or "")
            if spell and attack and "Projectile" in str(row.get("CastingTypes") or ""):
                out.setdefault(spell.lower(), attack)
    return out


def activities() -> tuple[dict, dict]:
    """The motion and facing activities of each attack fragment, of the dodge, and of the ability fragments (stage P3.5: their
    actions in playeractions_attack, playeractions_dodge, and playeractions_ability_<weapon>), and the named conditions of these
    action lists."""
    out, conditions = {}, {}
    ability_lists = sorted(n for n in source.paks().paths(ABILITY_ACTIONS.rsplit("/", 1)[0] + "/")
                           if n.startswith(ABILITY_ACTIONS) and n.endswith(".actionlist"))
    for path in (ATTACK_ACTIONS, DODGE_ACTIONS, *ability_lists):
        root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
        for c in root.find("Conditions") if root.find("Conditions") is not None else []:
            if c.get("Name") and c.get("Name") not in conditions:
                conditions[c.get("Name")] = condition_json(c)
        list_activities(root, out)
    return out, conditions


def list_activities(root: ET.Element, out: dict) -> None:
    for action in root.iter("Action"):
        fragment = action.get("FragmentName")
        acts = action.find("Activities")
        if not fragment or fragment in out or acts is None:
            continue
        entry = {"action": action.get("Name")}
        for kind in MOTION_ACTIVITIES:
            node = acts.find(f".//{kind}")
            if node is None:
                continue
            default = node.find("Default")
            entry[kind] = {"default": dict(default.attrib) if default is not None else {},
                           "conditionals": [dict(c.attrib) for c in node.findall("Conditional")]}
        out[fragment] = entry


def clip_info(path: str, skeleton: player.Skeleton, root: bool, names: dict[int, str] | None = None) -> dict:
    """The duration, the flags, the animated bones, and (root) the root track of a clip. names: the bones of the controllers (a
    weapon clip: the bones of the weapon skeletons; else the player skeleton)."""
    f = cgf.read_chunks(source.read(path))
    motion = [c for c in f.chunks if c.type == anim.T_MOTION][0].data
    flags = struct.unpack_from("<I", motion, 0)[0]
    clip = anim.read_clip(f)
    names = names if names is not None else {b.controller_id: b.name for b in skeleton.bones}
    out = {"path": path, "duration": round((clip.end - clip.start) * clip.secs_per_frame, 5), "flags": flags,
           "additive": bool(flags & 1), "bones": sorted(names[c] for c in clip.controllers if c in names)}
    xform = clip.controllers.get(zlib.crc32(b"Xform"))
    if root and xform is not None and len(xform.positions):
        times = (xform.pos_times - clip.start) * clip.secs_per_frame
        out["root"] = [[round(float(t), 5)] + [round(float(v), 5) for v in p] for t, p in zip(times, xform.positions)]
    return out


def consumable_tags() -> dict[str, str]:
    """@return The MannequinTag of each consumable with one (the item id in lower case): the variant of the fragment Use."""
    out = {}
    for row in datasheet.load(CONSUMABLE_TABLE).rows():
        tag = row.get("MannequinTag") or ""
        if row.get("ConsumableID") and isinstance(tag, str) and tag:
            out[str(row["ConsumableID"]).lower()] = tag
    return out


def actions() -> dict:
    """Writes actions.json (see the module text). @return Counts."""
    graph = json.loads((gamedata_dir() / "weapon-actions.json").read_text())
    weapons_doc = json.loads((player.player_dir() / "weapons.json").read_text())
    # The cooldowns of the abilities (ability-data.json of the backend copy: [AbilityID, Time, MinTime]; stage P3.5).
    cooldowns = {row[0].lower(): row for row in json.loads((gamedata_dir() / "ability-data.json").read_text())["cooldowns"]}
    projectiles = projectile_spells()
    mana_costs = {row[0].lower(): row[1] for row in json.loads((gamedata_dir() / "ability-data.json").read_text())["manaCosts"]}
    cafs, space_files = player.stem_index(".caf"), player.stem_index(".bspace")
    spaces_lower = {name.lower(): paths for name, paths in space_files.items()}
    # The weapon clips (AST-202): the clips of the prop folder by their lower-case file stem.
    props: dict[str, list[str]] = {}
    for name in source.paks().entries:
        if name.startswith(PROP_FOLDER) and name.endswith(".caf"):
            props.setdefault(Path(name).stem.lower(), []).append(name)
    scopes = scope_table()
    skeleton = player.Skeleton("Male")
    fragments, blend_spaces, clips, root_clips, prop_clips = {}, {}, {}, set(), set()

    def add_clip(name: str) -> None:
        if name and name not in clips and name in cafs:
            clips[name] = None
        elif name and name not in clips and name in props:
            clips[name] = None
            prop_clips.add(name)

    def add_space(name: str) -> None:
        if name not in blend_spaces:
            blend_spaces[name] = player.read_blend_space(player.pick(spaces_lower[name]))
            for example in blend_spaces[name]["examples"]:
                add_clip(example["clip"])

    def record(adb: str, element: ET.Element, name: str, root: bool = False) -> str:
        key = f"{adb.rsplit('/', 1)[-1]}|{name}|{element.get('Tags') or ''}"
        if key not in fragments:
            layers = layers_of(element, spaces_lower, add_clip, add_space)
            if root:
                for layer in layers:
                    for item in layer:
                        if item.get("clip"):
                            root_clips.add(item["clip"])
            fragments[key] = {"adb": adb, "name": name, "tags": element.get("Tags") or "",
                              "blendOut": float(element.get("BlendOutDuration") or 0.2), "layers": layers, "markers": markers_of(element)}
            if homing := homing_of(element):
                fragments[key]["homing"] = homing
        return key

    def dodges(tags: set[str]) -> dict:
        out = {}
        for name in DODGE_FRAGMENTS:
            for variant in DODGE_VARIANTS:
                found = find_fragment(name, tags, tag_set(variant), False)
                if found is not None:
                    out[name + ("+" + variant if variant else "")] = record(found[0], found[1], name, root=True)
        return out

    def stance(tags: set[str], offhand: bool) -> dict:
        out = {}
        for name, variants in STANCE_FRAGMENTS.items():
            for variant in variants:
                found = find_fragment(name, tags, tag_set(variant), offhand)
                if found is not None:
                    out[name + ("+" + variant if variant else "")] = record(found[0], found[1], name)
        return out

    # The tag sets: the melee graphs of the backend, and each other tag set of a weapon with a draw fragment.
    sets = {"+".join(w["tags"]): {"tags": w["tags"], "graph": w} for w in graph["weapons"]}
    for tag_text, timing in weapons_doc["timings"].items():
        tags = [t.strip() for t in tag_text.split(",") if t.strip()]
        if tags and "draw" in timing and "+".join(tags) not in sets:
            sets["+".join(tags)] = {"tags": tags, "graph": None}
    weapons = []
    missing = []
    for key, entry in sorted(sets.items()):
        tags = tag_set(key)
        out = {"tags": entry["tags"], "stance": stance(tags, False),
               "offhand": stance(tags | {"offhand", "shield_oh"}, True), "dodge": dodges(tags)}
        timing = weapons_doc["timings"].get(",".join(entry["tags"])) or {}
        # The block (stage P3.G; playeractions_block: the action Block_UpperBody raises the weapon with Block_UpperBody_Raise, then
        # Block_Continue_UpperBody holds it with Block_UpperBody while the block key is down; both set the tag Blocking).
        block = {}
        for kind, fragment_name in (("raise", "Block_UpperBody_Raise"), ("loop", "Block_UpperBody")):
            found = find_fragment(fragment_name, tags, set(), False)
            if found is not None:
                block[kind] = record(found[0], found[1], fragment_name)
        if block:
            out["block"] = block
        for kind, fragment_name in (("draw", "Unsheathe_UpperBody"), ("sheathe", "Sheathe_UpperBody")):
            data = timing.get(kind)
            if data:
                node = player.adb_root(data["adb"]).find(f"FragmentList/{fragment_name}")
                match = next((f for f in node if (f.get("Tags") or "").lower() == data["tags"].lower()), None) if node is not None else None
                if match is not None:
                    out[kind] = record(data["adb"], match, fragment_name)
        if entry["graph"]:
            w = entry["graph"]
            out["start"] = w["start"]
            # The abilities of the weapon (stage P3.5): the ability id and its CRC-32 (the id of the bar), the action, the fragment,
            # the selections of a fragment by the learned nodes and the perks, the casts of the action, the cooldown of the data.
            out["abilities"] = []
            for ability in w.get("abilities", []):
                row = cooldowns.get(ability["ability"].lower())
                # The casts of a projectile spell at a mark: [mark, spell, RangedAttackName, its CRC-32, needs, forbids].
                shots = [[cast[5], cast[0], projectiles[cast[0].lower()], zlib.crc32(projectiles[cast[0].lower()].lower().encode()),
                          cast[1], cast[2]] for cast in ability.get("casts", []) if cast[5] and cast[0].lower() in projectiles]
                out["abilities"].append({**ability, "crc": zlib.crc32(ability["ability"].lower().encode()),
                                         "cooldown": row[1] if row else None, "minCooldown": row[2] if row else None,
                                         "shots": shots})
            out["graph"] = []
            # The attachment database of the weapons of the tags (AnimDbPath of a weapon row): the damage clips of the weapon.
            attach_db = next((item.get("animDb") for item in weapons_doc["items"].values()
                              if item.get("tag", "").lower() == ",".join(entry["tags"]).lower() and item.get("animDb")), None)
            for fragment in w["fragments"]:
                adb = ADB_FOLDER + fragment["file"]
                node = player.adb_root(adb).find(f"FragmentList/{fragment['name']}")
                match = next((f for f in node if (f.get("Tags") or "") == fragment["tags"]), None) if node is not None else None
                item = {name: fragment[name] for name in ("name", "length", "switch", "canTransition", "dodge")}
                # Each transition (stage P3.5: also the starts of the abilities, with their ability id, and the transitions inside the
                # abilities, with their terms): [kind, start, end, input, to, condition, ability id | terms].
                item["transitions"] = [list(t) for t in fragment["transitions"]]
                # The named marks and the casts of the backend fragment (the throw of Gravity Well, the ground of Mighty Gavel).
                item["markers"] = fragment.get("markers", [])
                item["casts"] = fragment.get("casts", [])
                params = damage_params(match)
                if attach_db:
                    attach = player.adb_root(player.norm(attach_db)).find(f"FragmentList/{fragment['name']}")
                    for variant in attach if attach is not None else []:
                        if tag_set(variant.get("Tags") or "") <= tags | CONTEXT_TAGS:
                            params += damage_params(variant)
                item["strikes"] = strikes_of(fragment, params)
                if match is not None:
                    item["fragment"] = record(adb, match, fragment["name"], root=True)
                else:
                    missing.append(f"{key}: {fragment['name']}")
                out["graph"].append(item)
        # The ranged and magic actions (stage P3.6, ranged.py): the bow, the fire staff, and the life staff.
        if key.lower() in {k.lower() for k in ranged.RANGED_SETS}:
            ranged_key = next(k for k in ranged.RANGED_SETS if k.lower() == key.lower())
            out["ranged"] = ranged.weapon(ranged_key, tags, record, find_fragment, tag_set, adb_chain, mana_costs, cooldowns)
        # The weapon scopes (AST-202, research weapon-props.md "The rule for the client" 2 and 3): for each fragment record of the
        # main hand that owns Weapon or WeaponTransition for its tags, the variant of the same fragment in the attachment database
        # of the weapon (the tags of the weapon, of the body record, and the context tags; these scopes add no tag). The body and
        # the weapon records are separate: their clips and blend times differ.
        weapon_db = next((player.norm(item["animDb"]) for item in weapons_doc["items"].values()
                          if item.get("tag", "").lower() == ",".join(entry["tags"]).lower() and item.get("animDb")), None)
        if weapon_db and source.has(weapon_db):
            records = {}
            for body_key in body_records(out):
                body = fragments[body_key]
                state = tags | tag_set(body["tags"]) | CONTEXT_TAGS
                owned = [scope for scope in scopes_of(scopes, body["name"], state) if scope in WEAPON_SCOPES]
                element = weapon_variant(weapon_db, body["name"], state) if owned else None
                if element is not None:
                    records[body_key] = {"record": record(weapon_db, element, body["name"]), "scopes": owned}
            names = {item["clip"] for value in records.values() for layer in fragments[value["record"]]["layers"]
                     for item in layer if item.get("clip") in props}
            out["weaponScope"] = {"adb": weapon_db, "records": records, "clips": sorted(names)}
        weapons.append(out)
    # The unarmed set: no stance (the fragments of locomotion.json), the dodge of player.adb, the death and the respawn.
    life = {}
    for kind, (name, variant) in LIFE_FRAGMENTS.items():
        found = find_fragment(name, set(), tag_set(variant), False)
        if found is not None:
            life[kind] = record(found[0], found[1], name)
    use = {}
    for kind, (name, variant) in USE_FRAGMENTS.items():
        found = find_fragment(name, set(), tag_set(variant), False)
        if found is not None:
            use[kind] = record(found[0], found[1], name)
    # The gather fragments (stage P5.2, the creature track: converter/swc_convert/gather_actions.py): "gather|ACTION|TOOLTAGS".
    use.update(gather_actions.fragments(record, find_fragment, tag_set))
    weapons.append({"tags": [], "stance": {}, "offhand": {}, "dodge": dodges(set()), "life": life, "use": use})
    # The bones of the weapon clips: the controllers of the skeletons of the weapons whose attachment database has weapon records.
    prop_names: dict[int, str] = {}
    scope_tags = {",".join(w["tags"]).lower() for w in weapons if w.get("weaponScope", {}).get("clips")}
    looks = {item["model"]: [skin for skin, _ in item["skins"]] for item in weapons_doc["items"].values()
             if item.get("tag", "").lower() in scope_tags and item.get("model", "").endswith(".chr")}
    for model, skins in sorted(looks.items()):
        try:
            prop_names.update({b.controller_id: b.name for b in player.Skeleton.weapon(model, skins).bones})
        except Exception:   # a skeleton that does not read: its clips list fewer bones
            continue
    for name in list(clips):
        try:
            if name in prop_clips:
                clips[name] = {**clip_info(sorted(props[name])[0], skeleton, False, prop_names), "prop": True}
            else:
                clips[name] = clip_info(player.pick(cafs[name]), skeleton, name in root_clips)
        except Exception as error:   # a clip that does not read: the client plays no clip for it
            clips[name] = {"error": str(error)[:120]}
    ranged.finish([w["ranged"] for w in weapons if "ranged" in w], fragments, clips)
    # The ammunition key of the shot items of each ranged tag set: the fallback of the AmmoType of the weapon rows of its profile
    # (projectile-data.json "weapons"; DefaultArrow for a bow, none for a magic weapon).
    ranged_weapons = ranged.flights(json.loads((gamedata_dir() / "projectile-data.json").read_text()))["weapons"]
    for w in weapons:
        if "ranged" in w:
            profile = w["ranged"]["profile"]
            w["ranged"]["ammo"] = next((r["ammo"] for r in ranged_weapons.values() if r["profile"] == profile and r["ammo"]), "")
    acts, conditions = activities()
    graph_tags = {",".join(w["tags"]).lower() for w in weapons if w.get("graph")}
    models = {item["model"] for item in weapons_doc["items"].values() if item.get("model") and item.get("tag", "").lower() in graph_tags}
    # The stamina and the dodge (stage P2.5): the stamina data of the backend copy (the same values as the server), the
    # encumbrance row of the player, and the equip load of each armor item (tenths; perk-data.json of the backend copy).
    perks = json.loads((gamedata_dir() / "perk-data.json").read_text())
    stamina = {"data": json.loads((gamedata_dir() / "stamina-data.json").read_text()), "encumbrance": perks["encumbrance"],
               "itemLoads": {item.lower(): load for item, load in perks["equipLoad"].items()}}
    doc = {"format": 1, "sources": [player.ADB, CONTROLLER_DEFS, ATTACK_ACTIONS, DODGE_ACTIONS, DEATH_ACTIONS, INTERACT_ACTIONS, gather_actions.GATHER_ACTIONS, ABILITY_ACTIONS + "*",
                                    "sharedassets/springboardentitites/playerdata/cage/playeractions_use.actionlist",
                                    datasheet.TABLES_FOLDER + CONSUMABLE_TABLE,
                                    datasheet.TABLES_FOLDER + "javelindata_spelltable_*", str(gamedata_dir() / "weapon-actions.json"), str(gamedata_dir() / "ability-data.json"),
                                    str(gamedata_dir() / "stamina-data.json"), str(gamedata_dir() / "perk-data.json")],
           "consumables": consumable_tags(),
           "weapons": weapons, "fragments": fragments, "blendSpaces": blend_spaces, "clips": clips,
           "activities": acts, "conditions": conditions, "weaponJoints": weapon_joints(models), "stamina": stamina,
           "ranged": {**ranged.flights(json.loads((gamedata_dir() / "projectile-data.json").read_text())),
                      "manaCosts": {name: value for name, value in mana_costs.items()},
                      "spells": {name: row for name, row in ranged.spells().items()
                                 if name in ranged.spells_of([w["ranged"] for w in weapons if "ranged" in w])}}}
    out = player.player_dir() / "actions.json"
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"actions": str(out), "weapon tag sets": len(weapons), "fragments": len(fragments), "blend spaces": len(blend_spaces),
            "clips": len(clips), "weapon clips": len(prop_clips), "clips with a root track": len(root_clips), "clip errors": sum(1 for c in clips.values() if "error" in c),
            "graph fragments with no variant": missing[:10]}


STATION_NAME = re.compile(rb"@[A-Za-z0-9]+_MasterName")


def station_name(slice_path: str) -> str:
    """@return The name key of a station slice ("@BlacksmithT5_MasterName"): the one MasterName text of the slice (the name of its
    Interact component). A slice with no such text or with two of them stops the tool."""
    names = sorted({m.decode() for m in STATION_NAME.findall(source.read(slice_path))})
    if len(names) != 1:
        raise ValueError(f"the station slice {slice_path} has {len(names)} MasterName texts: {names}")
    return names[0]


def interactions() -> dict:
    """The interaction data of the server objects that the client uses (stage P2.10): the templates of the fixtures (the powder
    keg: the timed use Trigger, 5 s) and of the personal loot bag (TakeAllOption, OpenStorageOption) from interaction-data.json of
    the backend copy (the same data as the server; protocol notes 4.35, 4.36, 4.42). Writes SWC_CONTENT/player/interactions.json:
    for each template its slice asset and sub id, the root entity id, the slot of PositionInTheWorld, and each Interact bound (its
    slot, component, entity, place in the frame of the slice, rangeSq) with its options (name, CRC-32 id, action, committed,
    Delay Time, hold time, option entity). The client makes the ids of an object of the server (K = root XOR G)."""
    source_path = gamedata_dir() / "interaction-data.json"
    data = json.loads(source_path.read_text())
    options = data["options"]
    wanted = {name: index for name, index in data.get("fixtures", {}).items()}
    for name, container in data.get("lootContainers", {}).items():
        wanted["loot:" + name] = container["template"]
    # Stage MVP-J: the crafting stations of the settlements (the server children of the placements, `spawned`; decision D1 of the
    # backend: the template of a child is its runtime variant). A camp stays out: the retail client hides OpenCraftingOption of a camp
    # (Soulwarden interactions.md, live check L5; docs/CRAFTING.md "The gaps").
    for child in data.get("spawned", []):
        template = data["templates"][child["template"]]
        if template["kind"] == "craftingStation" and template.get("assembly") and not template["assembly"]["key"].startswith("Camp"):
            wanted["station:" + template["slice"].rsplit("/", 1)[-1].removesuffix(".dynamicslice")] = child["template"]
    templates = []
    shapes = BoundShapes()
    for name, index in sorted(wanted.items()):
        template = data["templates"][index]
        bounds = []
        for bound in template["bounds"]:
            bounds.append({"slot": bound["slot"], "component": bound["component"], "entity": bound["entity"],
                           "position": bound["position"], "rangeSq": bound["rangeSq"], "capacity": bound["capacity"],
                           "options": [{"name": options[option]["name"], "id": options[option]["id"], "action": options[option]["action"],
                                        "committed": options[option]["committed"], "delay": options[option]["delay"],
                                        "hold": options[option]["hold"], "entity": entity} for option, entity in bound["options"]]})
            # AST-198: the candidate shape of the bound (the bag: the box 2 x 2 x 2 m of its Interaction entity; the range is the use check).
            shape = shapes.shape(template["slice"], entity=bound["entity"])
            if shape is not None:
                bounds[-1]["shape"] = shape
        entry = {"name": name, "slice": template["slice"], "asset": template["asset"], "sub": template["sub"],
                 "kind": template["kind"], "root": template["root"], "positionSlot": template["slots"]["position"],
                 "permissionRule": template["permissionRule"], "bounds": bounds}
        if name.startswith("station:"):
            # The station facts of a use (the Assembly key m_craftingStationEntry, the fee flag m_chargeCraftingFee) and the name of the
            # station (the text key "@BlacksmithT5_MasterName" of the slice: the prompt "FORGE TIER 5" and Hud.LocalPlayer.InteractName,
            # which stationproperties.luac reads for its header and its trade skills).
            entry["assembly"] = {"key": template["assembly"]["key"], "fee": bool(template["assembly"]["fee"])}
            entry["interactName"] = station_name(template["slice"])
        templates.append(entry)
    doc = {"format": 1, "source": str(source_path), "templates": templates, "lootContainers": data.get("lootContainers", {})}
    out = player.player_dir() / "interactions.json"
    out.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"interactions": str(out), "templates": [t["name"] for t in templates]}
