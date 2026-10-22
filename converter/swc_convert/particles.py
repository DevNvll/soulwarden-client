"""The particle effects of the game (AST-210, docs/PARTICLES.md; research ~/Games/soulwarden-client/research/particles.md).

The sources (read only): the 640 particle libraries of libs/particles/ (XML, ParticleVersion 33) and the libraries that a
placement names by its asset outside that folder, the ParticleComponent of the world placements (the walk of environment.py:
the chunks, the capitals, the start spawners to depth 12) and of the menu level (nw_frontend_nighthaven), and the .mtl files and
textures of the effects.

Usage: python3 -B -m swc_convert.particles library | placements [REGION...] | menu | names | report | systems [KEY...] | check
       | all

Outputs (SWC_CONTENT/particles, never in git):
  libraries/LIB.json  each library: the pak path, the SHA-256, the root attributes, and its nodes in file order: the key, the
                      name, the tag (Particles or Folder), the parent index, the raw Params attributes, the LOD blocks
                      (distance, Active, raw Params), and the raw DynamicParams attributes
  index.json          the libraries, the node counts, and the key table: the lower-case key -> [library file, node index]; the
                      collisions (two nodes with one lower-case key) stay in their own list
  placements/REGION.json  the particle components that the region places: the effect, the world matrix (rows, engine m), the
                      component settings that differ from the defaults, the slice chain end, and the entity
  menu.json           the particle components of the menu level: the effect, the world matrix (rows, engine m), the Unreal
                      transform (space.unreal_transform), the distance to the Player Entity, and the settings
  names.json          the name checks of all placements: exact keys, keys with a different case, missing keys with counts
  support.json        each Params attribute of the libraries: its count, its support status in the client, and the note
  systems.json        the systems for the importer (tools/import-particles.sh): for each requested effect root its emitters with
                      the resolved values (the defaults, then the own attributes), the textures, the materials, and the list of
                      the attributes with a value that the client does not express (the support report of the effect)
  textures/...png     the textures of the systems (the mip of at most 2,048 pixels; sRGB files keep their sRGB bytes)

The rules (research "The rule for the client"; [R] retail code, [D] data, [I] inference):
- A root key is Library.Name, a child key is ParentKey.ChildName; a dot in a top name is part of the name. The lookup is exact;
  a key with a different case is reported, never used silently. A failed name is reported, never repaired.
- A node starts from the standard defaults (the constructor of ParticleParams [R]: Inheritance Standard; no library writes
  Inheritance), then its own attributes. A child does not inherit the visual values of its parent.
- A value is a base, then named fields (Random, ParticleAge, EmitterStrength, Color, ...); a field value in parentheses is a
  vector (x, y, z, w) or a spline (keys t, v, d, s, flags separated by ";"). The parser keeps the raw text.
- A scalar random: the factor 1 - r x Random, r uniform in [0, 1] [R: the range 1 - Random to 1; the distribution is the public one].
- A spline: t and v as bytes (/ 255) [R]; d is the out tangent, s the in tangent; the flags: bits 0-2 the in tangent type,
  bits 3-5 the out tangent type (0 smooth, 1 custom, 2 zero, 3 step, 4 linear) [P: the CryEngine spline key flags].
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

from . import env, source

LIBRARY_FOLDER = "libs/particles/"
# The libraries outside libs/particles/ that a placement names by its asset (research particles/external-xml-check.json).
EXTERNAL = ["slices/worldevents/firstlight/interact/ritualarcane/cfx_arcanesurge.xml"]
PARTICLE_COMPONENT = bytes.fromhex("65bc817aabf6440fad4f581c40f92795")
MENU_PAK = "levels/nw_frontend_nighthaven/level.pak"
MENU_ENTITIES = "mission0.entities_xml"
FORMAT = 1
# A new value makes the systems and their textures again (the import compares it).
CONVERTER_VERSION = "1"


def out_dir() -> Path:
    path = env.content_dir() / "particles"
    path.mkdir(parents=True, exist_ok=True)
    return path


def write_json(path: Path, doc, indent=None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    part = path.with_suffix(path.suffix + ".part")
    part.write_text(json.dumps(doc, indent=indent))
    part.replace(path)


# ------------------------------------------------------------------------------------------------------- the parameter text

def split_top(text: str, sep: str) -> list[str]:
    """@return The parts of `text` at `sep` outside parentheses."""
    out, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == sep and depth == 0:
            out.append(text[start:i])
            start = i + 1
    out.append(text[start:])
    return out


def scalar(text: str):
    text = text.strip()
    try:
        return float(text)
    except ValueError:
        return text


VECTOR_NAMES = {"x", "y", "z", "w"}
KEY_NAMES = {"t", "v", "d", "s", "flags"}


def parse_group(inner: str):
    """@return A vector {x, y, z, w} or a spline [keys] of the text inside parentheses."""
    parts = split_top(inner, ";")
    if len(parts) == 1:
        items = [i for i in split_top(inner, ",") if i.strip()]
        names = {i.split("=", 1)[0].strip() for i in items if "=" in i}
        if names and names <= VECTOR_NAMES:
            return {"vector": {i.split("=", 1)[0].strip(): float(i.split("=", 1)[1]) for i in items}}
    keys = []
    for part in parts:
        key = {}
        for item in split_top(part, ","):
            if "=" not in item:
                continue
            name, value = (s.strip() for s in item.split("=", 1))
            if value.startswith("("):
                key[name] = parse_group(value[1:-1]).get("vector", {})
            else:
                key[name] = scalar(value)
        keys.append(key)
    return {"keys": keys}


def parse_value(text: str) -> dict:
    """@return {"base": number, text, or {"vector"} or None, "fields": {name: number, text, {"vector"}, or {"keys"}}}."""
    out: dict = {"base": None, "fields": {}}
    for item in split_top(text or "", ","):
        item = item.strip()
        if not item:
            continue
        eq = next((i for i, ch in enumerate(item) if ch == "="), -1)
        paren = item.find("(")
        if eq < 0 or (0 <= paren < eq):
            out["base"] = parse_group(item[1:-1]) if item.startswith("(") else scalar(item)
            continue
        name, value = item[:eq].strip(), item[eq + 1:].strip()
        out["fields"][name] = parse_group(value[1:-1]) if value.startswith("(") else scalar(value)
    # The vector form "x=..,y=.." of an offset or an angle: the axis fields are the base.
    if out["base"] is None and out["fields"] and set(out["fields"]) <= VECTOR_NAMES:
        out["base"] = {"vector": {k: float(v) for k, v in out["fields"].items()}}
        out["fields"] = {}
    return out


# The standard defaults of ParticleParams (research "Defaults and child emission": the constructor at 0x6C60830 [R]). The other
# attributes start at 0, false, or the empty text [I: the zero fill of the constructor].
DEFAULTS = {
    "Enabled": "true", "EmitterType": "CPU", "EmitterShape": "ANGLE", "Inheritance": "Standard", "SpawnIndirection": "Direct",
    "Facing": "Camera", "BlendType": "AlphaBased", "SizeX": "1", "SizeY": "1", "SizeZ": "1", "Speed": "0", "GravityScale": "0",
    "DynamicLightContribution": "1", "SunContribution": "1", "DiffuseLighting": "1", "EmissiveLighting": "0",
    "EnvProbeLighting": "0", "TodBrightnessInfluence": "1", "SoftParticle": "false", "MotionBlur": "false", "Alpha": "1",
    "Color": "(x=1,y=1,z=1)", "Count": "0", "ParticleLifeTime": "0", "Continuous": "false", "MaintainAspectRatio": "true",
}


def params_of(attrs: dict) -> dict:
    """@return The resolved attributes of a node: the standard defaults, then its own (research: no library writes Inheritance)."""
    out = dict(DEFAULTS)
    out.update(attrs)
    return out


def number(text, default: float = 0.0) -> float:
    base = parse_value(text)["base"] if isinstance(text, str) else text
    return float(base) if isinstance(base, float) else default


def flag(text) -> bool:
    return str(text).strip().lower() in ("true", "1", "yes")


def vector(text, default=(0.0, 0.0, 0.0)) -> list[float]:
    base = parse_value(text)["base"] if text else None
    if isinstance(base, dict) and "vector" in base:
        v = base["vector"]
        return [v.get("x", 0.0), v.get("y", 0.0), v.get("z", 0.0)]
    if isinstance(base, float):
        return [base] * 3
    return list(default)


# ---------------------------------------------------------------------------------------------------------------- the splines

TANGENT_SMOOTH, TANGENT_CUSTOM, TANGENT_ZERO, TANGENT_STEP, TANGENT_LINEAR = 0, 1, 2, 3, 4


def byte(x: float) -> float:
    """@return x as the byte of the scalar spline key (t and v are stored as bytes / 255 [R])."""
    return round(min(max(x, 0.0), 1.0) * 255.0) / 255.0


def spline_keys(keys: list[dict], quantize: bool = True) -> list[tuple]:
    """@return The keys (t, v, out tangent, in tangent, in type, out type) of a spline, in the input order (equal times stay)."""
    out = []
    for key in keys:
        t, v = float(key.get("t", 0.0)), key.get("v", 0.0)
        if quantize and not isinstance(v, dict):
            t, v = byte(t), byte(float(v))
        flags = int(key.get("flags", 0))
        out.append((t, v, float(key.get("d", 0.0)), float(key.get("s", 0.0)), flags & 7, (flags >> 3) & 7))
    return out


def evaluate(keys: list[tuple], t: float) -> float:
    """@return The scalar spline at t (a Hermite segment between two keys; the tangent rules of the flags)."""
    if not keys:
        return 1.0
    if t <= keys[0][0]:
        return float(keys[0][1])
    if t >= keys[-1][0]:
        return float(keys[-1][1])
    for i in range(len(keys) - 1):
        a, b = keys[i], keys[i + 1]
        if a[0] <= t <= b[0] and b[0] > a[0]:
            break
    else:
        return float(keys[-1][1])
    dt = b[0] - a[0]
    u = (t - a[0]) / dt
    if a[5] == TANGENT_STEP:
        return float(a[1])

    def slope(i: int, out_side: bool) -> float:
        k = keys[i]
        kind = k[5] if out_side else k[4]
        if kind == TANGENT_CUSTOM:
            return k[2] if out_side else k[3]
        if kind == TANGENT_ZERO:
            return 0.0
        if kind == TANGENT_LINEAR:
            j = i + 1 if out_side else i - 1
            if 0 <= j < len(keys) and keys[j][0] != k[0]:
                return (keys[j][1] - k[1]) / (keys[j][0] - k[0])
            return 0.0
        # Smooth [I: the Catmull-Rom slope of the two neighbours, one-sided at an end]
        lo, hi = max(i - 1, 0), min(i + 1, len(keys) - 1)
        if keys[hi][0] == keys[lo][0]:
            return 0.0
        return (keys[hi][1] - keys[lo][1]) / (keys[hi][0] - keys[lo][0])

    m0, m1 = slope(i, True) * dt, slope(i + 1, False) * dt
    if a[5] == TANGENT_LINEAR and b[4] == TANGENT_LINEAR:
        return a[1] + (b[1] - a[1]) * u
    h00, h10, h01, h11 = 2 * u**3 - 3 * u**2 + 1, u**3 - 2 * u**2 + u, -2 * u**3 + 3 * u**2, u**3 - u**2
    return h00 * a[1] + h10 * m0 + h01 * b[1] + h11 * m1


def sample(keys: list[dict] | None, count: int = 32) -> list[float]:
    """@return `count` samples of a scalar spline at t = i / (count - 1) (a missing spline is 1)."""
    if not keys:
        return [1.0] * count
    k = spline_keys(keys)
    return [round(evaluate(k, i / (count - 1)), 5) for i in range(count)]


def color_keys(keys: list[dict]) -> list[list[float]]:
    """@return The keys [t, r, g, b] of a color spline (linear between the keys [I])."""
    out = []
    for key in keys:
        v = key.get("v", {})
        if isinstance(v, dict):
            out.append([float(key.get("t", 0.0)), v.get("x", 0.0), v.get("y", 0.0), v.get("z", 0.0)])
        else:
            out.append([float(key.get("t", 0.0))] + [float(v)] * 3)
    return out


def sample_color(keys: list[list[float]] | None, count: int = 32) -> list[list[float]]:
    if not keys:
        return [[1.0, 1.0, 1.0]] * count
    out = []
    for i in range(count):
        t = i / (count - 1)
        if t <= keys[0][0]:
            out.append(keys[0][1:])
            continue
        if t >= keys[-1][0]:
            out.append(keys[-1][1:])
            continue
        j = next(j for j in range(len(keys) - 1) if keys[j][0] <= t <= keys[j + 1][0])
        a, b = keys[j], keys[j + 1]
        u = 0.0 if b[0] == a[0] else (t - a[0]) / (b[0] - a[0])
        out.append([round(a[c] + (b[c] - a[c]) * u, 5) for c in (1, 2, 3)])
    return out


def scalar_param(text: str, default: float = 0.0) -> dict:
    """@return {"base", "random", "age" (the keys or None), "strength" (the EmitterStrength keys or None)} of a scalar attribute.
    A text with no base has the base `default` (the default of the attribute: a size base is 1, research "Parameter text")."""
    p = parse_value(text)
    base = p["base"] if isinstance(p["base"], float) else default
    age = p["fields"].get("ParticleAge")
    strength = p["fields"].get("EmitterStrength")
    return {"base": base, "random": float(p["fields"].get("Random", 0.0)) if isinstance(p["fields"].get("Random", 0.0), float) else 0.0,
            "age": age["keys"] if isinstance(age, dict) and "keys" in age else None,
            "strength": strength["keys"] if isinstance(strength, dict) and "keys" in strength else None,
            "extra": sorted(k for k in p["fields"] if k not in ("Random", "ParticleAge"))}


def field(params: dict, name: str) -> dict:
    """@return scalar_param of a resolved attribute, with the default base of the attribute."""
    return scalar_param(params.get(name, DEFAULTS.get(name, "0")), number(DEFAULTS.get(name, "0")))


def random_range(base: float, random: float) -> list[float]:
    """@return [min, max] of base x (1 - r x Random), r in [0, 1] [R: the registered range]."""
    a, b = base * (1.0 - random), base
    return [min(a, b), max(a, b)]


# -------------------------------------------------------------------------------------------------------------- the libraries

def library_paths() -> list[str]:
    paths = [p for p in source.paks().paths(LIBRARY_FOLDER, ".xml")]
    return paths + [p for p in EXTERNAL if source.has(p)]


def read_library(path: str) -> dict:
    """@return The record of one library: its attributes and its nodes in file order (the tree as parent indices)."""
    data = source.read(path)
    root = ET.fromstring(data)
    name = root.get("Name", "")
    nodes: list[dict] = []

    def visit(element, parent: int, key_prefix: str) -> None:
        key = f"{key_prefix}.{element.get('Name', '')}"
        params = element.find("Params")
        lods = []
        for lod in element.findall("LODs/LevelOfDetail/LodParticle"):
            lp = lod.find("Particle/Params")
            lods.append({"distance": float(lod.get("Distance", "0")), "active": lod.get("Active", "1"),
                         "params": dict(lp.attrib) if lp is not None else {}})
        dynamic = element.find("DynamicParams")
        node = {"key": key, "name": element.get("Name", ""), "tag": element.tag, "parent": parent,
                "params": dict(params.attrib) if params is not None else {}, "lods": lods}
        if dynamic is not None:
            node["dynamic"] = dict(dynamic.attrib)
        nodes.append(node)
        index = len(nodes) - 1
        for child in element.findall("Childs/Particles"):
            visit(child, index, key)
        if element.tag == "Folder":
            for child in element:
                if child.tag in ("Particles", "Folder"):
                    visit(child, index, key)

    for element in root:
        if element.tag in ("Particles", "Folder"):
            visit(element, -1, name)
    return {"path": path, "name": name, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "attributes": dict(root.attrib), "nodes": nodes}


def library() -> dict:
    """Writes libraries/*.json and index.json."""
    out = out_dir()
    keys: dict[str, list] = {}
    collisions: dict[str, list] = {}
    libraries = []
    counts = {"Particles": 0, "Folder": 0, "lods": 0, "children": 0}
    attributes: dict[str, int] = {}
    for path in library_paths():
        record = read_library(path)
        file_name = Path(path).stem + (".external" if not path.startswith(LIBRARY_FOLDER) else "")
        write_json(out / "libraries" / f"{file_name}.json", record)
        for i, node in enumerate(record["nodes"]):
            counts[node["tag"]] = counts.get(node["tag"], 0) + 1
            counts["lods"] += len(node["lods"])
            counts["children"] += node["parent"] >= 0 and node["tag"] == "Particles"
            for name in node["params"]:
                attributes[name] = attributes.get(name, 0) + 1
            for lod in node["lods"]:
                for name in lod["params"]:
                    attributes[name] = attributes.get(name, 0) + 1
            low = node["key"].lower()
            if low in keys:
                collisions.setdefault(low, [keys[low]]).append([file_name, i])
            else:
                keys[low] = [file_name, i]
        libraries.append({"file": file_name, "path": path, "name": record["name"], "sha256": record["sha256"],
                          "bytes": record["bytes"], "version": record["attributes"].get("ParticleVersion"),
                          "nodes": len(record["nodes"])})
    index = {"format": FORMAT, "libraries": libraries, "counts": counts, "attributes": attributes, "keys": keys,
             "collisions": collisions}
    write_json(out / "index.json", index)
    return {"libraries": len(libraries), "counts": counts, "attributes": len(attributes), "keys": len(keys),
            "collisions": len(collisions), "versions": sorted({str(l["version"]) for l in libraries})}


class Libraries:
    """The converted libraries (index.json, libraries/*.json), loaded on demand."""

    def __init__(self) -> None:
        self.index = json.loads((out_dir() / "index.json").read_text())
        self.files: dict[str, dict] = {}
        self.exact = {}

    def load(self, file_name: str) -> dict:
        if file_name not in self.files:
            self.files[file_name] = json.loads((out_dir() / "libraries" / f"{file_name}.json").read_text())
        return self.files[file_name]

    def find(self, key: str) -> tuple[str, dict, int, bool] | None:
        """@return (file, library, node index, exact) of an effect key, or None."""
        hit = self.index["keys"].get(key.lower())
        if hit is None:
            return None
        lib = self.load(hit[0])
        return hit[0], lib, hit[1], lib["nodes"][hit[1]]["key"] == key

    def subtree(self, lib: dict, index: int) -> list[int]:
        """@return The node index and all its descendants (file order)."""
        out = [index]
        for i in range(index + 1, len(lib["nodes"])):
            if lib["nodes"][i]["parent"] in out:
                out.append(i)
        return out


# ------------------------------------------------------------------------------------------------------- the placement fields

# The fields of ParticleComponent / ParticleEmitterSettings (the menu XML gives the names; the binary slices have their CRC).
SETTING_NAMES = ["Particle", "SelectedEmitter", "ParticleLibraryAssetId", "Visible", "Enable", "Load Emitter On Activate", "Color",
                 "Alpha Scale", "GlobalSizeScale", "ParticleSizeX", "ParticleSizeY", "ParticleSizeZ", "ParticleSizeRandom",
                 "Particle Count Scale", "Time Scale", "Speed Scale", "Strength", "Pulse Period", "Pre-roll", "AttachToMesh",
                 "MeshParticle", "Target Entity", "AttachToDissolvingEdge", "GPU Edge Dissolve Target Entity", "Ignore Rotation",
                 "Not Attached", "Use LOD", "View Distance Multiplier", "Register by Bounding Box", "Use VisArea", "Render Always",
                 "Accept Decals", "Accept Snow", "Accept Silhouette", "Kill On Deactivate", "Force Highest Contextual Priority",
                 "Enable Audio", "Audio RTPC", "BaseClass1"]
SETTING_CRC = {source.field_id(n): n for n in SETTING_NAMES}
# The defaults of the settings that the client reads (the menu XML and the field census: the common values).
SETTING_DEFAULTS = {"Visible": True, "Enable": True, "Load Emitter On Activate": True, "Color": [1.0, 1.0, 1.0, 1.0],
                    "Alpha Scale": 1.0, "GlobalSizeScale": 1.0, "ParticleSizeX": 1.0, "ParticleSizeY": 1.0, "ParticleSizeZ": 1.0,
                    "ParticleSizeRandom": 0.0, "Particle Count Scale": 1.0, "Time Scale": 1.0, "Speed Scale": 1.0,
                    "Strength": -1.0, "Pulse Period": 0.0, "Pre-roll": False, "Use LOD": True, "View Distance Multiplier": 1.0,
                    "Kill On Deactivate": False, "AttachToMesh": False, "AttachToDissolvingEdge": False, "Ignore Rotation": False,
                    "Not Attached": False}
SETTING_KEEP = set(SETTING_DEFAULTS) | {"Register by Bounding Box", "Use VisArea", "Render Always", "Force Highest Contextual Priority"}


def named(value):
    """@return A decoded component with the setting names in place of the CRC keys."""
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            if k.startswith("crc:"):
                k = SETTING_CRC.get(int(k[4:], 16), k)
            out[k] = named(v)
        return out
    if isinstance(value, list):
        return [named(v) for v in value]
    return value


def settings_of(fields: dict) -> tuple[str, dict]:
    """@return (the effect name, the settings that differ from SETTING_DEFAULTS, plus Color/Visible/Enable always)."""
    emitter = fields.get("Particle", {}) if isinstance(fields, dict) else {}
    if not isinstance(emitter, dict):
        emitter = {}
    flat = dict(emitter)
    if isinstance(flat.get("BaseClass1"), dict):
        flat.update(flat.pop("BaseClass1"))
    effect = flat.get("SelectedEmitter", "")
    out = {}
    for name in SETTING_KEEP:
        if name not in flat:
            continue
        value = flat[name]
        if isinstance(value, (int, float, bool, str)) or (isinstance(value, list) and all(isinstance(x, (int, float)) for x in value)):
            if isinstance(value, float):
                value = round(value, 6)
            if isinstance(value, list):
                value = [round(float(x), 6) for x in value]
            if SETTING_DEFAULTS.get(name) != value:
                out[name] = value
    asset = flat.get("ParticleLibraryAssetId")
    if isinstance(asset, dict) and asset.get("product"):
        out["library"] = asset["product"]
    return effect if isinstance(effect, str) else "", out


def particle_records(data: bytes) -> list[dict]:
    """@return The entities of a slice with a particle component (the record form of environment.entity_records, kind
    "particle")."""
    from . import environment as E
    if PARTICLE_COMPONENT not in data:
        return []
    out = []
    for root in source.parse(data):
        for entity in source.walk(root):
            components = entity.child(E.COMPONENTS)
            if components is None or any(c.child(E.ENTITIES) is not None for c in components.children):
                continue
            matrix = np.eye(4)
            found = []
            for component in components.children:
                tm = component.child(E.WORLD_TM)
                if tm is not None and tm.value is not None and len(tm.value) == 48:
                    matrix = E.az_matrix(tm.value)
                if component.type_id == PARTICLE_COMPONENT:
                    found.append(named(E.decode(component)))
            if found:
                active = entity.child(source.field_id("IsRuntimeActive"))
                out.append({"entity": source.text(entity.child(E.NAME)) if entity.child(E.NAME) is not None else "",
                            "active": bool(active.value[0]) if active is not None and active.value else None,
                            "matrix": matrix, "components": {"particle": found}, "shape": None})
    return out


def particle_walk():
    """@return The placement walk of environment.py with the particle records in place of the environment records."""
    from . import environment as E

    class Walk(E.Walk):
        def slice(self, path: str):
            if path not in self.slices:
                try:
                    data = source.read(path)
                    self.reads += 1
                    spawners = []
                    if E.SPAWNER_MARK in data:
                        for spawner in E.read_slice(data).spawners:
                            if spawner.slice is not None and spawner.on_start:
                                child = source.product(spawner.slice[0], spawner.slice[1], ".dynamicslice")
                                if child:
                                    spawners.append((child, E.rows4(spawner.matrix)))
                    self.slices[path] = (particle_records(data), spawners)
                except Exception as error:   # a slice that the reader cannot read: it has no particle record
                    self.failures.append(f"{path}: {str(error)[:120]}")
                    self.slices[path] = None
            return self.slices[path]
    return Walk()


def placements(names: list[str] | None = None) -> dict:
    """Writes placements/REGION.json for each region."""
    from . import environment as E
    names = names or E.regions_of_world()
    walk = particle_walk()
    out = out_dir() / "placements"
    total, regions = 0, {}
    for name in names:
        try:
            records, _ = E.region_records(name, walk)
        except Exception as error:   # a region with no chunk file
            walk.failures.append(f"{name}: {str(error)[:120]}")
            continue
        rows = []
        for record in records:
            effect, settings = settings_of(record["fields"])
            m = np.asarray(record["matrix"])
            rows.append({"effect": effect, "matrix": [round(float(x), 5) for x in m[:3, :].ravel()], "settings": settings,
                         "entity": record["entity"], "slice": record["chain"][-1], "origin": record["origin"]})
        write_json(out / f"{name}.json", {"format": FORMAT, "region": name, "rule": "matrix: rows 3 x 4, engine m", "rows": rows})
        regions[name] = len(rows)
        total += len(rows)
        print(f"{name}: {len(rows)} (total {total}, slices {walk.reads}, failures {len(walk.failures)})", file=sys.stderr, flush=True)
    write_json(out / "index.json", {"regions": regions, "total": total, "slices": walk.reads, "failures": walk.failures})
    return {"regions": len(regions), "total": total, "slices": walk.reads, "failures": len(walk.failures)}


def menu() -> dict:
    """Writes menu.json: the particle components of the menu level with their world matrix and Unreal transform."""
    from . import space
    from .environment import read_from_archive
    from .frontend import _field, _transform, _value
    data = read_from_archive(MENU_PAK, MENU_ENTITIES)
    root = ET.fromstring(data)
    rows, player = [], None
    for element in root.iter("Class"):
        if element.get("name") != "AZ::Entity":
            continue
        components = _field(element, "Components")
        if components is None:
            continue
        name = _value(element, "Name")
        tm = _transform(components)
        if name == "Player Entity":
            player = tm.get("world", [0.0] * 12)[9:12]
        for component in components:
            if component.get("name") != "ParticleComponent":
                continue
            fields = {}
            for n in component.iter("Class"):
                field = n.get("field")
                if field and n.get("value") is not None:
                    value = n.get("value")
                    try:
                        parts = [float(v) for v in value.split()]
                        value = parts[0] if len(parts) == 1 else parts
                    except ValueError:
                        value = {"true": True, "false": False}.get(value, value)
                    fields[field] = value
            effect = fields.get("SelectedEmitter", "")
            settings = {k: (round(v, 6) if isinstance(v, float) else v) for k, v in fields.items()
                        if k in SETTING_KEEP and SETTING_DEFAULTS.get(k) != v}
            w = tm.get("world", [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0])
            matrix = np.eye(4)
            matrix[:3, 0], matrix[:3, 1], matrix[:3, 2], matrix[:3, 3] = w[0:3], w[3:6], w[6:9], w[9:12]
            rows.append({"entity": name, "effect": effect, "matrix": [round(float(x), 5) for x in matrix[:3, :].ravel()],
                         "unreal": space.unreal_transform(matrix), "settings": settings, "position": w[9:12]})
    for row in rows:
        row["distance"] = round(math.dist(row.pop("position"), player or [0, 0, 0]), 3)
    rows.sort(key=lambda r: r["distance"])
    write_json(out_dir() / "menu.json", {"format": FORMAT, "source": f"{MENU_PAK}:{MENU_ENTITIES}", "player": player, "rows": rows}, 1)
    return {"components": len(rows), "within 60 m": sum(r["distance"] <= 60 for r in rows),
            "near": [(r["entity"], r["effect"], r["distance"]) for r in rows if r["distance"] <= 60]}


# --------------------------------------------------------------------------------------------------------- the name checks

def names() -> dict:
    """Writes names.json: each effect name of the placements (world and menu) against the key table."""
    libs = Libraries()
    counts: dict[str, int] = {}
    folder = out_dir() / "placements"
    for path in sorted(folder.glob("r_*.json")):
        for row in json.loads(path.read_text())["rows"]:
            counts[row["effect"]] = counts.get(row["effect"], 0) + 1
    menu_path = out_dir() / "menu.json"
    menu_counts: dict[str, int] = {}
    if menu_path.is_file():
        for row in json.loads(menu_path.read_text())["rows"]:
            menu_counts[row["effect"]] = menu_counts.get(row["effect"], 0) + 1
    result = {"exact": {}, "case": {}, "missing": {}, "folder": {}}
    for effect, n in sorted({**counts, **{k: counts.get(k, 0) + v for k, v in menu_counts.items()}}.items()):
        hit = libs.find(effect) if effect else None
        if hit is None:
            result["missing"][effect] = n
            continue
        file_name, lib, index, exact = hit
        node = lib["nodes"][index]
        if node["tag"] == "Folder":
            result["folder"][effect] = n
        elif exact:
            result["exact"][effect] = n
        else:
            result["case"][effect] = [n, node["key"]]
    summary = {k: [len(v), sum(x if isinstance(x, int) else x[0] for x in v.values())] for k, v in result.items()}
    write_json(out_dir() / "names.json", {"summary": summary, **result,
                                          "collisions": libs.index["collisions"]}, 1)
    return {"names [effects, components]": summary, "collisions": len(libs.index["collisions"])}


# ------------------------------------------------------------------------------------------------------- the support report

# The support of each Params attribute in the client (stage 2): "used" the importer maps it, "approx" it maps it with a named
# approximation, "editor" a field of the editor with no visual effect, "later N" the stage that adds it. An attribute that is
# not in this table is "unsupported".
SUPPORT = {
    "Enabled": ("used", "the emitter and LOD enable"), "Count": ("used", "the spawn rate Count / life (Continuous) or the burst"),
    "Continuous": ("used", ""), "ParticleLifeTime": ("used", "a uniform life range by the scalar random rule"),
    "EmitterLifeTime": ("approx", "the emitter stops after it; 'emits at least Count' is not traced"),
    "SpawnDelay": ("used", "the start delay of the emitter"), "PulsePeriod": ("approx", "the emitter loops with this duration"),
    "SizeX": ("used", "the sprite width = 2 x size (the source quad is -1..1); age curve, random"),
    "SizeY": ("used", "the sprite height = 2 x size; age curve, random"), "SizeZ": ("approx", "mesh particles only (stage 6)"),
    "MaintainAspectRatio": ("used", "true: SizeY follows SizeX"), "Alpha": ("used", "base, random, age curve"),
    "Color": ("used", "base, random (one factor for RGB), age curve (linear between the keys)"),
    "Texture": ("used", "the diffuse atlas when the material has none"), "Material": ("used", "the .mtl diffuse, glow, and blend"),
    "TextureTiling": ("used", "TilesX/Y, FirstTile, AnimFramesCount, AnimFramerate, AnimCycle, flips, VariantCount, AnimBlend"),
    "BlendType": ("used", "alpha, additive; Multiplicative and Opaque later"), "EmissiveLighting": ("used", "the emission scale"),
    "Facing": ("used", "Camera, CameraX, Free, Velocity; the others later"), "OrientToVelocity": ("used", "the velocity-aligned sprite"),
    "Speed": ("used", "the initial speed along the emit direction"), "EmitAngle": ("used", "the cone of the emit direction (degrees)"),
    "FocusAngle": ("approx", "a tilt of the emit axis (random azimuth)"), "FocusAzimuth": ("approx", "the azimuth of the focus tilt"),
    "FocusGravityDir": ("used", "the emit axis is up (against gravity)"), "FocusRotatesEmitter": ("approx", "the axis turns with the focus"),
    "GravityScale": ("used", "-9.8 x scale m/s2 [R]"), "Acceleration": ("used", "m/s2"),
    "AirResistance": ("approx", "the drag (the stateless solver: exponential, like the source GPU path); age curve and WindScale ignored"),
    "LocalSpawnPosOffset": ("used", "the shape offset"), "SpawnPosOffset": ("editor", "the legacy copy of LocalSpawnPosOffset"),
    "LocalSpawnPosRandomOffset": ("used", "a box (roundness 0) or an ellipsoid (roundness 1)"),
    "SpawnPosXYZRandom": ("editor", "the legacy copy"), "LocalOffsetRoundness": ("approx", "0 box, else ellipsoid"),
    "OffsetRoundness": ("editor", "the legacy copy"), "LocalInitAngles": ("approx", "the sprite rotation (the Y angle)"),
    "InitAngles": ("editor", "the legacy copy"), "LocalRandomAngles": ("approx", "the random sprite rotation"),
    "RandomAngles": ("editor", "the legacy copy"), "RotationRateY": ("approx", "the sprite rotation rate; age curve ignored"),
    "RotationRateX": ("later 6", ""), "RotationRateZ": ("later 6", ""),
    "MoveRelEmitter": ("approx", "Yes: local space; Dynamic and YesWithTail later"),
    "SoftParticle": ("approx", "the depth fade of the material (the source exp curve, K = 1 / softness [I])"),
    "Stretch": ("approx", "the velocity-aligned sprite gets length + speed x stretch"), "LocalStretch": ("editor", "the copy of Stretch"),
    "EmitterSizeDiameter": ("editor", "the bounds hint"), "CameraMaxDistance": ("used", "the emitter stops beyond it"),
    "CameraMinDistance": ("used", "the emitter stops nearer than it"), "SpawnIndirection": ("used", "Direct; the parent events later (stage 7)"),
    "EmitterType": ("approx", "CPU and GPU both use the stateless simulation"), "Comment": ("editor", ""), "Group": ("editor", ""),
    "BlendIn": ("approx", "the LOD bands switch at the distance"), "BlendOut": ("approx", ""), "Overlap": ("approx", ""),
    "PivotY": ("approx", "the sprite pivot offset (base only)"), "PivotX": ("approx", "the sprite pivot offset (base only)"),
    "CameraDistanceOffset": ("approx", "the camera offset (m)"), "Curvature": ("later 3", "the curved normal of lit sprites"),
    "DiffuseLighting": ("later 3", "lit sprites"), "DiffuseBacklighting": ("later 3", ""), "DynamicLightContribution": ("later 3", ""),
    "SunContribution": ("later 3", ""), "EnvProbeLighting": ("later 3", ""), "ReceiveShadows": ("later 3", ""), "NormalMap": ("later 3", ""),
    "TodBrightnessInfluence": ("approx", "the particle brightness of the time preset is not applied yet"),
    "AlphaClip": ("later 3", ""), "Tessellation": ("later 3", ""), "MaintainDensity": ("later 3", ""), "CastShadows": ("later 3", ""),
    "Turbulence3DSpeed": ("approx", "curl noise strength (not the source vortex rule)"), "TurbulenceSize": ("later 6", ""),
    "TurbulenceSpeed": ("later 6", ""), "MotionBlur": ("later 7", ""), "TailLength": ("later 7", ""),
    "Connection": ("later 7", ""), "Geometry": ("later 6", "mesh particles"), "GeometryPieces": ("later 6", ""),
    "UseEmitterPositionForLODDistance": ("used", "the LOD distance is measured from the placement"),
    "CameraNonFacingFade": ("later 3", ""), "SortOffset": ("approx", "the translucency sort priority"),
    "Velocity": ("approx", "the speed along the shape axis is added to Speed"), "InheritVelocity": ("later 7", ""),
    "LightSource": ("later 3", "the placed light of the data stays the light"), "FacingGpu": ("approx", "the facing of the GPU path"),
}


def support_status(name: str) -> tuple[str, str]:
    return SUPPORT.get(name, ("unsupported", ""))


def report() -> dict:
    """Writes support.json: each Params attribute with its count and status."""
    index = json.loads((out_dir() / "index.json").read_text())
    rows = []
    for name, count in sorted(index["attributes"].items(), key=lambda x: -x[1]):
        status, note = support_status(name)
        rows.append({"name": name, "count": count, "status": status, "note": note})
    by_status: dict[str, int] = {}
    for row in rows:
        by_status[row["status"]] = by_status.get(row["status"], 0) + 1
    write_json(out_dir() / "support.json", {"attributes": rows, "byStatus": by_status}, 1)
    return {"attributes": len(rows), "byStatus": by_status,
            "unsupported": [r["name"] for r in rows if r["status"] == "unsupported"]}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc_convert.particles")
    parser.add_argument("command", choices=["library", "placements", "menu", "names", "report", "skills", "coverage", "systems", "check", "all"])
    parser.add_argument("names", nargs="*")
    args = parser.parse_args(argv)
    from . import particle_systems as ps
    from . import skill_effects
    commands = {"library": library, "placements": lambda: placements(args.names or None), "menu": menu, "names": names,
                "report": report, "skills": skill_effects.manifest, "coverage": skill_effects.coverage, "systems": lambda: ps.systems(args.names or None), "check": ps.check}
    if args.command == "all":
        result = {c: commands[c]() for c in ("library", "menu", "placements", "names", "report", "systems", "check")}
    else:
        result = commands[args.command]()
    print(json.dumps(result, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
