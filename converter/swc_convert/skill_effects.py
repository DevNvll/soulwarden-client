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
import struct
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


# ------------------------------------------------------------------------------------------------------------ the hits

IMPACT_TABLES = {"characters": "impactsystem/impacttable_characters.datasheet", "environment": "impactsystem/impacttable_environment.datasheet"}
IMPACT_SCRIPTS = "scripts/combatimpact/"
DAMAGE_TABLE = "javelindata_damagetable.datasheet"
SPAWN_CALLS = ("SpawnParticle", "SpawnParticleAtAttachment", "SpawnParticleAtAttachmentOffset", "SpawnParticleAtJoint",
               "SpawnParticleAtJointOffset", "SpawnDecal")
# The field of a HitVolume component with the column of ImpactTable_Characters of the volume (Flesh, FleshHead, Armor, ...) [D]; the
# name of the field is not known (its CRC).
HIT_MATERIAL_FIELD = 0x01803569


def impact_functions() -> dict:
    """@return The hit functions of scripts/combatimpact (ImpactTable.NAME): their particle and decal calls in the order of the code
    (luabc.calls_of: both sides of a test are in the list). A call: {"call": SpawnParticle ..., "args": [...]}: the arguments are the
    constants, the parameters ("a1" the point, "a3" the direction, "a5" "a6" the two entities [I]), "a3*-1" for a product."""
    out = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(IMPACT_SCRIPTS) and p.endswith(".luac")):
        try:
            main = luabc.load(source.read(path))
        except Exception:
            continue
        upvalues = []
        for name, index in luabc.assigned_functions(main).items():
            if not name.startswith("ImpactTable."):
                continue
            calls = []
            for called, args in luabc.calls_of(main.protos[index], 8, upvalues):
                short = called.rsplit(".", 1)[-1]
                if short in SPAWN_CALLS and called.startswith("ParticleManagerBus"):
                    calls.append({"call": short, "args": [a if isinstance(a, (float, list, bool)) or a is None else str(a) for a in args]})
            out[name.split(".", 1)[1]] = {"script": path, "calls": calls}
    return out


def volume_materials(path: str) -> dict:
    """@return The column of the hit table of each hit volume of a creature slice: the volume name -> Flesh, FleshHead, ...."""
    from .creature import HIT_VOLUME_TYPE
    try:
        data = source.read(path)
    except Exception:
        return {}
    out = {}
    for root in source.parse(data):
        for node in source.walk(root):
            if node.type_id != HIT_VOLUME_TYPE:
                continue
            name = node.child(source.field_id("m_volumeName"))
            material = node.child(HIT_MATERIAL_FIELD)
            if name is not None and material is not None and material.value:
                out.setdefault(source.text(name), source.text(material))
    return out


def impact_data() -> dict:
    """Writes SWC_CONTENT/particles/impacts.json (AST-230 plan step 9, docs/EFFECTS.md "The hits"): the hit tables (ImpactTable_Characters
    and ImpactTable_Environment: the row key -> the column -> the function), the hit functions, the WeaponCategory and the DamageType of
    each row of the damage table of the player, and the column of each hit volume of the converted creature slices. @return Counts."""
    from . import particles
    from .creature import creature_dir
    tables = {}
    for kind, name in IMPACT_TABLES.items():
        sheet = datasheet.Sheet(source.read(datasheet.TABLES_FOLDER + name))
        tables[kind] = {str(row.get("Name") or ""): {c: str(row.get(c)) for c in sheet.columns if c != "Name" and row.get(c)}
                        for row in sheet.rows() if row.get("Name")}
    sheet = datasheet.Sheet(source.read(datasheet.TABLES_FOLDER + DAMAGE_TABLE))
    damage = {str(row["DamageID"]).lower(): {"category": str(row.get("WeaponCategory") or ""), "type": str(row.get("DamageType") or "")}
              for row in sheet.rows() if row.get("DamageID")}
    volumes = {}
    slices_path = creature_dir() / "slices.json"
    if slices_path.is_file():
        for record in json.loads(slices_path.read_text())["slices"].values():
            if record.get("path") and record["path"] not in volumes:
                volumes[record["path"]] = volume_materials(record["path"])
    doc = {"format": 1, "tables": tables, "functions": impact_functions(), "damage": damage, "volumes": volumes}
    particles.write_json(particles.out_dir() / "impacts.json", doc, 1)
    return {"impacts": str(particles.out_dir() / "impacts.json"), "functions": len(doc["functions"]),
            "character rows": len(tables["characters"]), "damage rows": len(damage),
            "slices with materials": sum(1 for v in volumes.values() if v)}


# The weapon categories of the five weapons of the MVP (DamageTable.WeaponCategory of their rows; the hits of their rows).
WEAPON_CATEGORIES = ("Sword", "Rapier", "Bow", "Bow_Heavy", "GreatAxe", "Warhammer", "Shield")


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
    # The hits of the five weapons (plan step 9): the functions of the rows of their categories (and the +Crit rows) for each column.
    impacts = impact_data()
    impact_doc = json.loads((particles.out_dir() / "impacts.json").read_text())
    for row, columns in impact_doc["tables"]["characters"].items():
        if row.split("+")[0] not in WEAPON_CATEGORIES:
            continue
        for column, function in columns.items():
            for call in impact_doc["functions"].get(function, {}).get("calls", []):
                key = next((a for a in call["args"] if isinstance(a, str) and "." in a and not a.startswith(("a", "Paperdoll", "IsAttack"))), "")
                if call["call"] != "SpawnDecal":
                    use(key, f"impact|{row}|{column}|{function}")
    # The statuses (plan step 10): the particle nodes of the On and Off scripts of the status rows, in the player list, the NPC list,
    # and the lists of the creature slices.
    status_doc = status_fx_data()
    used_lists = {status_doc["player"], status_doc["npc"]} | {n for s in status_doc["slices"].values() for n in s["lists"]}
    for row in status_doc["statuses"].values():
        for script in (row["on"], row["off"]):
            for list_name in used_lists:
                for node in status_doc["scripts"].get(list_name, {}).get(script, []):
                    use(node.get("effect", ""), f"status|{list_name}|{script}")
    # The trails of the projectiles (plan step 8): the effects of the trail maps of the ranged attack database.
    for name, attack in tracer_data()["attacks"].items():
        for element, key in attack["effects"].items():
            use(key, f"tracer|{name}|{element}")
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
    return {"skill-effects": str(out_path), "keys": len(keys), "resolved": len(keys) - len(missing), "missing": missing, "impacts": impacts}


# The creature spell and projectile slices of the first pass of the systems (the three models of the study "Creature spell model" and
# the bolt of the shaman); the other creature slices are in skill-effects.json and get their systems in plan step 11.
FIRST_SLICES = ("slices/characters/spellslices/undead_shaman_lightning_strike.dynamicslice",
                "slices/characters/spellslices/undead_shaman_lightning_projectile.dynamicslice",
                "slices/characters/spellslices/dryad_mushroom_bomb.dynamicslice",
                "slices/characters/spellslices/mage_ranged_aoe.dynamicslice")


# The spell and projectile slices of the weapons of the player (the vortex of Gravity Well, Shockwave, Path of Destiny, the arrows ...).
WEAPON_SLICES = ("slices/items/weapons/", "slices/perks/")
# The profiles of the ranged attack database of the five weapons whose trails get a system (the other profiles: plan step 11).
TRACER_PROFILES = ("Bow", "GreatAxe", "Warhammer", "Sword", "Rapier")

RADB = "sharedassets/genericassets/rangedattackdatabase.radb"
FX_SCRIPTS = "sharedassets/springboardentitites/fxscripts/"
JOINT_ALIASES = "sharedassets/springboardentitites/datatables/javelindata_jointalias.datasheet"
PLAYER_FX_LIST = "fxscriptlist_player"
NPC_FX_LIST = "fxscriptlist_npc_statuseffects"


def fx_scripts() -> dict:
    """@return The FX scripts of the FX script lists (fxscripts/*.actionlist) by list name and script name: the particle nodes
    (FxScriptNodeSpawnParticleEffect: the effect, the joint alias or joint or attachment, the follow and rotation flags, the offsets
    when used (m, degrees), the scale, KillOnExit, KeepEmitterActive, the delay of a DelayExecute parent) and the material overrides."""
    out: dict = {}
    for path in sorted(n for n in source.paks().entries if n.startswith(FX_SCRIPTS) and n.endswith(".actionlist")):
        root = ET.fromstring(source.read(path).decode("utf-8", "replace"))
        scripts = out.setdefault(path.rsplit("/", 1)[-1][:-len(".actionlist")], {})

        def walk(node: ET.Element, delay: float, nodes: list) -> None:
            for child in node:
                if child.tag == "FxScriptNodeDelayExecute":
                    walk(child, delay + float(child.get("DelayTime", "0") or 0), nodes)
                elif child.tag == "FxScriptNodeSpawnParticleEffect" and child.get("EffectName"):
                    a = child.attrib
                    item = {"effect": a["EffectName"], "alias": a.get("AttachJointAlias", ""), "joint": a.get("AttachJointName", ""),
                            "attachment": a.get("AttachAttachmentName", ""), "entity": a.get("AttachToEntity") == "true",
                            "toJoint": a.get("AttachToJoint") == "true", "follow": a.get("AttachEmitterFollows") == "true",
                            "ignoreRotation": a.get("AttachIgnoreRotation") == "true", "scale": float(a.get("Scale", "1") or 1),
                            "kill": a.get("KillOnExit") == "true", "keep": a.get("KeepEmitterActive", "").lower() == "true",
                            "delay": delay}
                    if a.get("UsePosOffset") == "true":
                        item["position"] = [float(v) for v in a.get("PosOffset", "0,0,0").split(",")]
                    if a.get("UseRotOffset") == "true":
                        item["rotation"] = [float(v) for v in a.get("RotOffset", "0,0,0").split(",")]
                    nodes.append(item)
                elif child.tag == "FxScriptNodeMaterialOverride":
                    nodes.append({"material": child.get("MaterialKey", ""), "delay": delay})
                else:
                    walk(child, delay, nodes)
        for script in root.iter("FxScript"):
            nodes: list = []
            walk(script, 0.0, nodes)
            scripts[script.get("Name", "")] = nodes
    return out


def status_fx_data() -> dict:
    """Writes SWC_CONTENT/particles/status-fx.json (AST-230 plan step 10, docs/EFFECTS.md "The statuses"): the status rows of the backend
    by their index (ability-data.json statusEffects; the index of the StatusEffects state) with FxScriptOn, FxScriptOff,
    ShouldRefreshFxScript, and IgnoreFxScriptWhenPotencyIsZero of the status tables; the scripts of the FX lists (fx_scripts); the joint
    aliases of each skeleton (javelindata_jointalias); for each creature slice of the client (creatures/slices.json) the FX lists that it
    references (the asset ids in its bytes) and the aliases of its skeleton. @return The document."""
    from . import particles
    from .creature import creature_dir, gamedata_dir
    rows: dict = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and "statuseffects_" in p
                       and p.endswith(".datasheet")):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except Exception:
            continue
        if sheet.type != "StatusEffectData":
            continue
        for row in sheet.rows():
            if row.get("StatusID") and (row.get("FxScriptOn") or row.get("FxScriptOff")):
                rows.setdefault(str(row["StatusID"]).lower(), row)
    statuses: dict = {}
    for entry in json.loads((gamedata_dir() / "ability-data.json").read_text()).get("statusEffects", []):
        row = rows.get(str(entry[0]).lower())
        if row is not None:
            statuses[str(entry[1])] = {"id": entry[0], "on": str(row.get("FxScriptOn") or ""), "off": str(row.get("FxScriptOff") or ""),
                                       "refresh": bool(row.get("ShouldRefreshFxScript")),
                                       "ignoreZero": bool(row.get("IgnoreFxScriptWhenPotencyIsZero"))}
    aliases = {}
    for row in datasheet.Sheet(source.read(JOINT_ALIASES)).rows():
        aliases[str(row["SkeletonName"]).lower()] = {k: str(v) for k, v in row.items() if k != "SkeletonName" and v not in ("", None)}
    lists_by_guid = {guid: path.rsplit("/", 1)[-1][:-len(".actionlist")] for guid, products in source.catalog().items()
                     for _sub, path in products if path.startswith(FX_SCRIPTS)}
    slices: dict = {}
    slices_path = creature_dir() / "slices.json"
    if slices_path.is_file():
        doc = json.loads(slices_path.read_text())
        for key, record in doc["slices"].items():
            path = record.get("path") or record.get("slice") or ""
            try:
                data = source.read(path) if path else b""
            except Exception:
                data = b""
            lists = [name for guid, name in lists_by_guid.items() if guid in data]
            skeleton = str(record.get("skeleton") or "")
            stem = skeleton.rsplit("_", 1)[0].lower() if skeleton else ""
            slices[path.lower()] = {"lists": sorted(lists, key=lambda n: (n != NPC_FX_LIST, n)),
                                    "aliases": aliases.get(stem, aliases.get("npc_skel", {}))}
    out = {"format": 1, "statuses": statuses, "scripts": fx_scripts(), "aliases": aliases, "slices": slices,
           "player": PLAYER_FX_LIST, "npc": NPC_FX_LIST}
    particles.write_json(particles.out_dir() / "status-fx.json", out, 1)
    return out


def tracer_data() -> dict:
    """Writes SWC_CONTENT/particles/tracers.json (AST-230 plan step 8, docs/EFFECTS.md "The trails"): for each profile/attack of the
    ranged attack database, the trail effect by the weapon effect of the weapon (m_tracerEffectsByEffect: Arcane, Default, Fire, ..., and
    _Default; an empty value is left out), the time before the projectile shows (m_projectileVisibilityDelaySeconds, s), and the
    trail of the predicted projectile (m_predictedProjectileTrailEffect). @return The document."""
    from . import particles
    pair_key, pair_value = source.field_id("value1"), source.field_id("value2")
    out: dict = {"format": 1, "source": RADB, "attacks": {}}

    def first(node, wanted: int):
        return next((n for n in source.walk(node) if n is not node and n.name == wanted), None)

    for root in source.parse(source.read(RADB)):
        profiles = first(root, source.field_id("m_rangedAttackProfiles"))
        for pair in profiles.children if profiles is not None else []:
            profile, data = source.text(pair.child(pair_key)), pair.child(pair_value)
            attacks = first(data, source.field_id("m_rangedAttackData")) if data is not None else None
            for entry in attacks.children if attacks is not None else []:
                name, values = source.text(entry.child(pair_key)), entry.child(pair_value)
                if not profile or not name or values is None:
                    continue
                record: dict = {"effects": {}}
                tracers = values.child(source.field_id("m_tracerEffectsByEffect"))
                for item in tracers.children if tracers is not None else []:
                    element, key = source.text(item.child(pair_key)), source.text(item.child(pair_value))
                    if element and key:
                        record["effects"][element] = key
                delay = values.child(source.field_id("m_projectileVisibilityDelaySeconds"))
                if delay is not None and delay.value is not None and len(delay.value) == 4:
                    record["delay"] = round(struct.unpack(">f", delay.value)[0], 6)
                predicted = values.child(source.field_id("m_predictedProjectileTrailEffect"))
                if predicted is not None and source.text(predicted):
                    record["predicted"] = source.text(predicted)
                out["attacks"][f"{profile}/{name}"] = record
    # The shots of the creatures: a projectile record has its owner and the CRC-32 of its AmmoID; the ranged attacks of the creature
    # slice of the backend data (creatures.json "ranged") give the profile and the attack of that ammunition (the first one) [I].
    from .creature import gamedata_dir
    creatures_path = gamedata_dir() / "creatures.json"
    shots: dict = {}
    if creatures_path.is_file():
        for entry in json.loads(creatures_path.read_text()).get("slices", []):
            for _action, _length, releases in entry.get("ranged", []):
                for release in releases:
                    if release.get("profile") and release.get("attack") and release.get("ammoKey"):
                        shots.setdefault(entry["path"].lower(), {}).setdefault(str(release["ammoKey"]),
                                                                               f"{release['profile']}/{release['attack']}")
    out["creatureShots"] = shots
    particles.write_json(particles.out_dir() / "tracers.json", out, 1)
    return out


def build_keys(only: str = "") -> list[str]:
    """@return The resolved keys of skill-effects.json that get a system: the keys of the five weapons and of FIRST_SLICES (only: the
    keys with a user that contains this text, no case)."""
    from . import particles
    path = particles.out_dir() / "skill-effects.json"
    if not path.is_file():
        return []
    keys = json.loads(path.read_text())["keys"]
    tracers_path = particles.out_dir() / "tracers.json"
    creature_attacks = ({a for shots in json.loads(tracers_path.read_text()).get("creatureShots", {}).values() for a in shots.values()}
                        if tracers_path.is_file() else set())

    def wanted(user: str) -> bool:
        if user.startswith(("spells|", "projectiles|")):
            # Plan step 11: the spell and projectile slices of all creatures and weapons (FIRST_SLICES were the first pass).
            return True
        if user.startswith("tracer|"):
            return user.split("|")[1].split("/")[0] in TRACER_PROFILES or user.split("|")[1] in creature_attacks
        return True
    return [k for k, v in keys.items() if not v.get("missing") and any(wanted(u) for u in v["users"])
            and (not only or any(only.lower() in u.lower() for u in v["users"]))]


def coverage() -> dict:
    """Writes SWC_CONTENT/particles/skill-coverage.json (AST-230 plan step 11): for each family of users of the effect keys (each of the
    five weapons, the hits, the trails, the statuses, the creature slices), the keys, the keys that are not in the libraries, the keys
    with no system, and the emitters that the client does not draw by their reason (the notes of systems.json: no silent fallback).
    @return The counts by family."""
    from collections import Counter
    from . import particles
    keys = json.loads((particles.out_dir() / "skill-effects.json").read_text())["keys"]
    # The client finds a system by its key with no case (the library spelling of a key can differ in case).
    systems = {s["key"].lower(): s for s in json.loads((particles.out_dir() / "systems.json").read_text())["systems"]}

    def family(user: str) -> str:
        head = user.split("|")[0]
        return head if head in WEAPON_EFFECT_IDS.values() or head in ("impact", "tracer", "status") else "creature slices"

    out: dict = {}
    for key, value in sorted(keys.items()):
        for name in sorted({family(u) for u in value["users"]}):
            item = out.setdefault(name, {"keys": 0, "missing": [], "noSystem": [], "notDrawn": Counter()})
            item["keys"] += 1
            if value.get("missing"):
                item["missing"].append(key)
            elif key.lower() not in systems:
                item["noSystem"].append(key)
            else:
                for emitter in systems[key.lower()]["emitters"]:
                    if not emitter.get("enabled"):
                        reason = next((n for n in emitter.get("notes", []) if "not drawn" in n or "AST-230" in n or "stage" in n),
                                      "disabled (alpha, LOD, texture, or a rule of particle_systems.py)")
                        item["notDrawn"][reason.split(":")[0][:80]] += 1
    for item in out.values():
        item["notDrawn"] = dict(item["notDrawn"].most_common())
    particles.write_json(particles.out_dir() / "skill-coverage.json", {"format": 1, "families": out}, 1)
    return {name: {"keys": v["keys"], "missing": len(v["missing"]), "no system": len(v["noSystem"]),
                   "emitters not drawn": sum(v["notDrawn"].values())} for name, v in out.items()}
