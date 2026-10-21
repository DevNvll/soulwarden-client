"""The effects of the skills on their timeline (AST-230, docs/EFFECTS.md; the study research/skill-effects.md).

The parts that the converter gives the client:
- effects_of: the visual procedural clips of a fragment (actions.py writes them into each fragment record of actions.json as
  "effects"): ParticleEffect, WeaponEffects, SpawnSlice, RunFxScript, and CAGE-CastSpell, with the source layer and clip index,
  the start and the end (s, on the clock of the fragment: the sum of the Blend.ExitTime values; the next procedural item of the
  layer ends a clip; an open last clip ends with the fragment), the blend inputs (ExitTime, StartTime, Duration: kept apart, the
  start is not moved by Duration [D]), and all the parameters.
- weapon_scripts: the weapon effect groups (javelindata_weaponeffects.datasheet: WeaponEffectId -> the script of each element;
  the scripts run in luabc.run_tables). A group is the list of its particle entries (particleName, jointName or
  attachmentName, positionOffset, rotationDeg in degrees, scale, followType, killOnStop, playOnOwner); the audio entries are
  dropped (no sound in the MVP).
- weapon_helpers: the CA_BONE attachments of a weapon character file (vfx_Base_01, vfx_Melee_01, vfx_Tip_01, vfx_muzzle ...):
  the bone and the position (m) and rotation (w, x, y, z) relative to it.
- slice_particles: the entities of a slice with a particle component: the entity name, its IsRuntimeActive flag, its matrix in
  the slice, the effect, and its settings (for the spell slices of effects.json, creature.py).
- manifest: SWC_CONTENT/particles/skill-effects.json: each effect key that the five weapons and the creature spells use, its
  library node (or "missing": no node of that name; the client logs it and plays nothing; no alias [D]), and its users.
"""
from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path

from . import datasheet, luabc, source

VISUAL_TYPES = {"ParticleEffect", "WeaponEffects", "SpawnSlice", "RunFxScript", "CAGE-CastSpell"}
WEAPON_EFFECTS_TABLE = "javelindata_weaponeffects.datasheet"
ELEMENTS = ["Default", "Fire", "Arcane", "Void", "Lightning", "Ice", "Nature"]
# The weapon effect ids of the five weapons of the MVP (the rows of javelindata_weaponeffects) by the tag set of actions.json.
WEAPON_EFFECT_IDS = {"1h_melee,sword": "Sword", "1h_melee,rapier": "Rapier", "bow": "Bow", "bow,longbow": "Bow", "2h_melee,greataxe": "GreatAxe",
                     "2h_melee,warhammer": "Warhammer"}


def param_value(node: ET.Element):
    """@return The value of a ProceduralParams child: its "value", or the list of its Element children, or a dict of its children."""
    if node.get("value") is not None:
        return node.get("value")
    children = list(node)
    if children and all(child.tag == "Element" for child in children):
        return [param_value(child) for child in children]
    return {child.tag: param_value(child) for child in children}


def _float(text, default: float = 0.0) -> float:
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


def _vec(value) -> list[float]:
    return [_float(v) for v in value] if isinstance(value, list) and len(value) == 3 else [0.0, 0.0, 0.0]


def _flag(text) -> bool:
    return str(text).lower() == "true"


def normalized(kind: str, params: dict) -> dict:
    """@return The fields that the client reads, from the raw parameters of a clip."""
    if kind == "ParticleEffect":
        return {"effect": params.get("EffectName", ""), "joint": params.get("JointName", ""),
                "attachment": params.get("AttachmentName", ""), "position": _vec(params.get("PosOffset")),
                "rotation": _vec(params.get("RotOffset")), "scale": _float(params.get("Scale"), 1.0),
                "follow": _flag(params.get("EmitterFollows")), "kill": _flag(params.get("KillOnExit")),
                "keep": _flag(params.get("KeepEmitterActive")), "ignoreRotation": _flag(params.get("IgnoreRotation")),
                "condition": params.get("Condition", ""), "alphaScale": _float(params.get("AlphaScale"), 1.0),
                "timeScale": _float(params.get("TimeScale"), 1.0), "tint": _vec(params.get("ColorTint")) if params.get("ColorTint") else [1.0, 1.0, 1.0]}
    if kind == "WeaponEffects":
        return {"group": params.get("EffectGroupName", ""), "slot": params.get("ItemSlotAlias", ""),
                "enter": params.get("OptionOnEnter", ""), "exit": params.get("OptionOnExit", ""), "condition": params.get("Condition", "")}
    if kind == "SpawnSlice":
        return {"slice": params.get("SliceToSpawn", ""), "despawn": _flag(params.get("DespawnOnExit")), "condition": params.get("Condition", "")}
    if kind == "RunFxScript":
        return {"script": params.get("ScriptToRun", ""), "stopOnExit": _flag(params.get("StopOnExit"))}
    if kind == "CAGE-CastSpell":
        return {"spell": params.get("SpellName", ""), "condition": params.get("Condition", "")}
    return {}


def effects_of(fragment: ET.Element) -> list[dict]:
    """@return The visual procedural clips of a fragment variant (see the module text), in the order of the layers."""
    out = []
    for layer_index, layer in enumerate(fragment.findall("ProcLayer")):
        time = 0.0
        blend = {}
        clips = []
        for item in layer:
            if item.tag == "Blend":
                time += _float(item.get("ExitTime"))
                blend = item.attrib
                continue
            if item.tag != "Procedural":
                continue
            params = item.find("ProceduralParams")
            clips.append({"type": item.get("type") or "", "start": time,
                          "blend": [_float(blend.get("ExitTime")), _float(blend.get("StartTime")), _float(blend.get("Duration"))],
                          "params": {child.tag: param_value(child) for child in params} if params is not None else {}})
        for clip_index, clip in enumerate(clips):
            if clip["type"] not in VISUAL_TYPES:
                continue
            end = clips[clip_index + 1]["start"] if clip_index + 1 < len(clips) else None
            out.append({"type": clip["type"], "layer": layer_index, "clip": clip_index, "start": round(clip["start"], 6),
                        "end": round(end, 6) if end is not None else None, "blend": [round(v, 6) for v in clip["blend"]],
                        **normalized(clip["type"], clip["params"]), "params": clip["params"]})
    return out


# ------------------------------------------------------------------------------------------------------- the weapon groups

def script_path(text: str) -> str:
    """@return The bytecode path of a script name of the table ("Scripts/WeaponEffects/X.lua" -> scripts/weaponeffects/x.luac)."""
    return text.replace("\\", "/").lower().removesuffix(".lua") + ".luac"


def run_script(path: str) -> dict:
    """@return The particle entries of each effect group of one weapon effect script (an empty group: [])."""
    main = luabc.load(source.read(path))
    result = luabc.run_tables(main, {"Vector3": lambda *a: [float(v) for v in a], "RequireScript": lambda name: luabc.Symbol(str(name))})
    table = result[0] if result and isinstance(result[0], luabc.Table) else None
    groups = table.get("effectGroups") if table is not None else None
    out = {}
    if not isinstance(groups, luabc.Table):
        return out
    for name, entries in groups.hash.items():
        rows = entries.plain() if isinstance(entries, luabc.Table) else []
        rows = rows if isinstance(rows, list) else []
        out[str(name)] = [{k: v for k, v in row.items() if k != "type"} for row in rows
                          if isinstance(row, dict) and str(row.get("type", "")).endswith("EffectTypes.Particle")]
    return out


def weapon_scripts() -> dict:
    """@return {"table": {WeaponEffectId: {element: script}}, "scripts": {script: {group: [entries]}}, "failures": {...}}."""
    sheet = datasheet.Sheet(source.read(datasheet.TABLES_FOLDER + WEAPON_EFFECTS_TABLE))
    table, scripts, failures = {}, {}, {}
    for row in sheet.rows():
        effect_id = str(row.get("WeaponEffectId") or "")
        if not effect_id:
            continue
        table[effect_id] = {}
        for element in ELEMENTS:
            text = str(row.get(element) or "")
            if not text:
                continue
            path = script_path(text)
            table[effect_id][element] = path
            if path in scripts or path in failures:
                continue
            try:
                scripts[path] = run_script(path)
            except Exception as error:   # a script that the runner cannot run: the client plays no group of it
                failures[path] = str(error)[:160]
    return {"table": table, "scripts": scripts, "failures": failures}


def weapon_helpers(cdf_path: str) -> dict:
    """@return The CA_BONE attachments of a weapon character file: name -> {"bone", "position" (m), "rotation" (w, x, y, z)}."""
    try:
        root = ET.fromstring(source.read(cdf_path).decode("utf-8", "replace"))
    except Exception:
        return {}
    out = {}
    for item in root.iter("Attachment"):
        if item.get("Type") != "CA_BONE" or not item.get("AName"):
            continue
        out[item.get("AName")] = {"bone": item.get("BoneName") or "",
                                  "position": [_float(v) for v in (item.get("RelPosition") or "0,0,0").split(",")],
                                  "rotation": [_float(v) for v in (item.get("RelRotation") or "1,0,0,0").split(",")]}
    return out


# --------------------------------------------------------------------------------------------------------- the slice parts

def slice_particles(path: str) -> list[dict]:
    """@return The particle entities of a slice: {"entity", "active", "matrix" (rows 3 x 4 as 12 numbers: the matrix of the entity in
    the slice, m; the form of the placements), "effect", "settings"} (particles.particle_records with the IsRuntimeActive flag)."""
    from . import particles
    try:
        data = source.read(path)
    except Exception:
        return []
    out = []
    for record in particles.particle_records(data):
        for component in record["components"]["particle"]:
            effect, settings = particles.settings_of(component)
            out.append({"entity": record["entity"], "active": record.get("active"),
                        "matrix": [round(float(v), 6) for row in record["matrix"].tolist()[:3] for v in row],
                        "effect": effect, "settings": settings})
    return out


# ---------------------------------------------------------------------------------------------------------- the manifest

def manifest() -> dict:
    """Writes SWC_CONTENT/particles/skill-effects.json (see the module text). @return Counts and the missing keys."""
    from . import particles, player
    from .creature import creature_dir
    libs = particles.Libraries()
    actions = json.loads((player.player_dir() / "actions.json").read_text())
    groups = actions.get("weaponEffects", {})
    users: dict[str, set[str]] = {}

    def use(key: str, who: str) -> None:
        if key:
            users.setdefault(key, set()).add(who)

    for weapon in actions["weapons"]:
        tags = ",".join(weapon["tags"]).lower()
        effect_id = WEAPON_EFFECT_IDS.get(tags)
        if effect_id is None:
            continue
        records = set()
        for value in weapon.values():
            if isinstance(value, dict):
                records |= {v for v in value.values() if isinstance(v, str) and v in actions["fragments"]}
        records |= {item["fragment"] for item in weapon.get("graph", []) if "fragment" in item}
        records |= {v["record"] for v in weapon.get("weaponScope", {}).get("records", {}).values()}
        for variants in weapon.get("ranged", {}).get("fragments", {}).values():
            records |= {v["fragment"] for v in variants if "fragment" in v}
        for ability in weapon.get("abilities", {}).values() if isinstance(weapon.get("abilities"), dict) else []:
            records |= {v for v in (ability.values() if isinstance(ability, dict) else []) if isinstance(v, str) and v in actions["fragments"]}
        for key in sorted(records):
            for clip in actions["fragments"][key].get("effects", []):
                who = f"{effect_id}|{actions['fragments'][key]['name']}"
                if clip["type"] == "ParticleEffect":
                    use(clip["effect"], who)
                elif clip["type"] == "WeaponEffects":
                    for element, script in groups.get("table", {}).get(effect_id, {}).items():
                        for entry in groups.get("scripts", {}).get(script, {}).get(clip["group"], []):
                            use(entry.get("particleName", ""), f"{who}|{clip['group']}|{element}")
    effects_path = creature_dir() / "effects.json"
    if effects_path.is_file():
        creature_effects = json.loads(effects_path.read_text())
        for kind in ("spells", "projectiles"):
            for record in creature_effects.get(kind, {}).values():
                for part in record.get("particles", []):
                    use(part["effect"], f"{kind}|{record['path']}|{part['entity']}")
    keys = {}
    for key in sorted(users, key=str.lower):
        hit = libs.find(key)
        keys[key] = {"library": hit[0], "node": hit[2], "exact": hit[3], "users": sorted(users[key])} if hit else \
            {"missing": True, "users": sorted(users[key])}
    out_path = particles.out_dir() / "skill-effects.json"
    particles.write_json(out_path, {"format": 1, "keys": keys}, 1)
    missing = [k for k, v in keys.items() if v.get("missing")]
    return {"skill-effects": str(out_path), "keys": len(keys), "resolved": len(keys) - len(missing), "missing": missing}


# The creature spell and projectile slices of the first pass of the systems (the three models of the study "Creature spell model" and
# the bolt of the shaman); the other creature slices are in skill-effects.json and get their systems in plan step 11.
FIRST_SLICES = ("slices/characters/spellslices/undead_shaman_lightning_strike.dynamicslice",
                "slices/characters/spellslices/undead_shaman_lightning_projectile.dynamicslice",
                "slices/characters/spellslices/dryad_mushroom_bomb.dynamicslice",
                "slices/characters/spellslices/mage_ranged_aoe.dynamicslice")


def build_keys(only: str = "") -> list[str]:
    """@return The resolved keys of skill-effects.json that get a system: the keys of the five weapons and of FIRST_SLICES (only: the
    keys with a user that contains this text, no case)."""
    from . import particles
    path = particles.out_dir() / "skill-effects.json"
    if not path.is_file():
        return []
    keys = json.loads(path.read_text())["keys"]

    def wanted(user: str) -> bool:
        if user.startswith(("spells|", "projectiles|")):
            return user.split("|")[1] in FIRST_SLICES
        return True
    return [k for k, v in keys.items() if not v.get("missing") and any(wanted(u) for u in v["users"])
            and (not only or any(only.lower() in u.lower() for u in v["users"]))]
