"""The environment of the world (stage E0, docs/ENVIRONMENT.md; research ~/Games/soulwarden-client/research/world-environment.md):
the data of the time of day, the sky, the fog, the weather, and the local lights, read from the game copy (read only).

Usage: python3 -B -m swc_convert.environment write [--regions NAME...] | check | sample PRESET HOUR [NAME...]

Output in SWC_CONTENT/environment:
  mission.json          the Environment element of the main-world mission (levels/newworld_vitaeeterna/level.pak: the
                        shared pak index resolves mission_mission0.xml to the introduction level) and its base TimeOfDay
  presets.json          each time preset of libs/timeofday (160 XML files): its variables in the file order (a name can come
                        two times: both stay), each with its spline keys [time, value or [r, g, b], flags] and its saved
                        editor value (not a sample: the file was saved at its own hour)
  regions/REGION.json   the environment entities that the region places: its chunks, its capitals, and the slices that their
                        spawners spawn at the start (depth 12; the rule of region.py): the kind (light, probe, timeOfDay, fog,
                        rain, snow, weather, ocean, waterVolume, river), the world matrix (4 x 4, rows, engine m), the shape,
                        the chain of the slices from the placement (an NPC or a camp owns the lights of its slice), and the raw
                        fields of the components (BaseClass1 stays)
  volumes.json          the volumes of the world for the client in one compact file: the time-of-day volumes (shape, preset,
                        priority, blend, hour override, sky, color grading) and the fog, rain, snow, weather, and ocean volumes
  sky.json              the sky slices of the presets: their meshes, materials, textures, texture motion, and parameters
  index.json            the counts

The time of a curve is hour / 24 (the retail evaluator multiplies the hour by the float32 1/24: research, normalization
evidence). The distribution rows of a region are not read (no environment component; research scope).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys
import uuid
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

import numpy as np

from . import env, source
from nav_slices import read_slice
from .region import az_matrix, quat_matrix, rows4, slice_key, slice_meshes

WORLD = "sharedassets/coatlicue/newworld_vitaeeterna/"
MAIN_LEVEL_PAK = "levels/newworld_vitaeeterna/level.pak"
F = source.field_id
COMPONENTS, ENTITIES, NAME, WORLD_TM = F("Components"), F("Entities"), F("Name"), F("m_worldTM")
SPAWNER_MARK = bytes.fromhex("b52d0d5299cd41beb6f7d2519c15bf14")   # the class of a slice spawner (census of the research)
MAX_DEPTH = 12


def _uuid(text: str) -> bytes:
    return uuid.UUID(text).bytes


# The environment components (class ids of the client image) and their kinds.
KINDS = {_uuid("6b9ab512-ca8a-4d2b-b570-df128ea7ce6a"): "light",          # LightComponent (LightType 3: a probe)
         _uuid("3e64fb88-5593-47d4-9aa5-2c5fff5ff439"): "timeOfDay",      # TimeOfDayPOIConfig
         _uuid("c01b9e8f-c015-46ac-9065-79445ce1408a"): "fog",            # FogVolumeComponent
         _uuid("9eece8bd-2355-4600-bef0-49415caf72e8"): "rain",           # RainComponent
         _uuid("f16ad691-091b-44e2-8fbd-2e3afc2739ef"): "snow",           # SnowComponent
         _uuid("1e8cd3a4-0aff-4dc6-95dc-4f78e68672a7"): "weather",        # WeatherConfiguration
         _uuid("517760d2-237e-4582-9cef-a91eff641441"): "ocean",          # WaterOceanComponent
         _uuid("d1576a29-b1b5-4fe2-9d9e-3a7781561c49"): "waterVolume",    # WaterVolumeComponent
         _uuid("c4091244-a779-4438-a0b7-406afee41d89"): "river"}          # RiverComponent
# The shapes of a volume (a sibling component of the same entity) by the class of their configuration.
SHAPES = {_uuid("f034fba2-ac2f-4e66-8152-14dfb90d6283"): "box",           # BoxShapeConfig: Dimensions
          _uuid("53254779-82f1-441e-9116-81e1facfecf4"): "cylinder",      # CylinderShapeConfig: Radius, Height (centered)
          _uuid("4aadfd75-48a7-4f31-8f30-fe4505f09e35"): "sphere",        # SphereShapeConfig: Radius
          _uuid("00931aeb-2ad8-42ce-b1dc-fa4332f51501"): "capsule",       # CapsuleShapeConfig: Height, Radius
          _uuid("f01c8bdd-6f24-4344-8945-521a8750b30b"): "polygonPrism",  # PolygonPrism: Height, Vertices (local x, y)
          _uuid("91a31d7e-f63a-4aa8-bc50-909b37f0ad8b"): "spline"}        # SplineCommon: the spline of a river

# The value types of the object stream (AZ type ids) and their decoders.
VALUES = {
    "a0ca880c-afe4-43cb-926c-59ac48496112": lambda b: b != b"\x00" * len(b),                       # bool
    "ea2c3e90-afbe-44d4-a90d-faaf79baf93d": lambda b: struct.unpack(">f", b)[0],                   # float
    "110c4b14-11a8-4e9d-8638-5051013a56ac": lambda b: struct.unpack(">d", b)[0],                   # double
    "d6597933-47cd-4fc8-b911-63f3e2b0993a": lambda b: int.from_bytes(b, "big"),                    # AZ::u64
    "70d8a282-a1ea-462d-9d04-51ede81fac2f": lambda b: int.from_bytes(b, "big", signed=True),       # AZ::s64
    "72039442-eb38-4d42-a1ad-cb68f7e0eef6": lambda b: int.from_bytes(b, "big"),                    # unsigned int
    "72b9409a-7d1a-4831-9cfe-fcb3fadd3426": lambda b: int.from_bytes(b, "big", signed=True),       # int
    "43da906b-7def-4ca8-9790-854106d3f983": lambda b: int.from_bytes(b, "big"),                    # unsigned char
    "ef8fb7e6-b745-4c16-a4b0-c1cc8f9e7e8c": lambda b: int.from_bytes(b, "big", signed=True),       # char
    "58422c0e-1e47-4854-98e6-34098f6fe12d": lambda b: int.from_bytes(b, "big", signed=True),       # AZ::s8
    "ecdbb2a0-fc91-47ec-81cc-cb5cba56e4e7": lambda b: int.from_bytes(b, "big"),                    # unsigned short
    "b8a56d56-a10d-4dce-9f63-405ee243dd3c": lambda b: int.from_bytes(b, "big", signed=True),       # AZ::s16
    "03aaab3f-5c47-5a66-9ebc-d5fa4db353c9": lambda b: b.decode("utf-8", "replace"),                # AZStd::string
    "6383f1d3-bb27-4e6b-a49a-6409b2059eaa": lambda b: int.from_bytes(b, "big"),                    # AZ::EntityId
    "e152c105-a133-4d03-bbf8-3d4b2fba3e2a": lambda b: str(uuid.UUID(bytes=b)) if len(b) == 16 else b.hex(),   # AZ::Uuid
    "3d80f623-c85c-4741-90d0-e4e66164e6bf": lambda b: list(struct.unpack(">2f", b)),               # Vector2
    "8379eb7d-01fa-4538-b64b-a6543b4be73d": lambda b: list(struct.unpack(">3f", b)),               # AZ::Vector3
    "7894072a-9050-4f0f-901b-34b1a0d29417": lambda b: list(struct.unpack(">4f", b)),               # AZ::Color (linear RGBA)
    "5d9958e9-9f1e-4985-b532-fffde75fedfd": lambda b: list(struct.unpack(">12f", b)),              # Transform (3 x 4, columns)
}
ASSET = "77a19d40-8731-4d3c-9041-1b43047366a4"   # AZ::Data::Asset: guid, sub id, ..., the length and the text of the path hint

# The field names of the environment components and of their shapes (engine field names; the object stream has their CRC).
FIELD_NAMES = """Advanced AffectsThisAreaOnly Ambient Amount Angle AnimIndex AnimPhase AnimPhaseRandom AnimSpeed Animation AreaFOV AreaHeight
AreaMaxDistance AreaWidth AssetPath AttenuationFalloffMax BaseClass1 BlendDistance BlendTime BoxHeight BoxLength BoxProject BoxWidth COLOR
CapFogAtVolumeDepth CastShadowsSpec CausticHeight Caustics Closed ColorGrading Common Configuration ConvexBorder CubemapResolution
CubemapTexture DefaultWidth Deferred DensityNoiseFrequency DensityNoiseOffset DensityNoiseScale DensityNoiseTimeFrequency DensityOffset
DiffuseDarkening DiffuseMultiplier Dimensions DistMult Elements Enabled EngineSpec ExtrudeBorder FallOffDirLatitude FallOffDirLong
FallOffScale FallOffShift FogColor FogColorAffectedBySun FogColorScale FogShadowing FogVolumeConfiguration Forward FrostAmount
FrostBrightness General GlobalDensity Granularity HasTimeOfDayOverride HdrDynamic Height IgnoreEnds IgnoreVisAreas IgnoresVisAreas Id
IndoorOnly InnerRadius LengthEndFadeDist LengthStartFadeDist LightConfiguration LightType Material MaterialAsset MinSpec MinimumSpec
NearCutoff ObjectSizeLimit OnInitially OuterRadius Physicalize PointAttenuationBulbSize PointMaxDistance PolygonPrism Priority
ProjectorAttenuationBulbSize ProjectorDistance ProjectorFOV ProjectorMaterial ProjectorNearPlane ProjectorTexture PuddlesAmount
PuddlesMaskAmount PuddlesRippleAmount Radius RainDropsAmount RainDropsLighting RainDropsSpeed RampEnd RampInfluence RampStart River
SegmentLength ShadowBias ShadowMaxCameraDistance ShadowResScale ShadowSlopeBias ShadowUpdateMinRadius ShadowUpdateRatio SnowAmount
SnowBrightness SnowFallBrightness SnowFallGravityScale SnowFallTurbulence SnowFallTurbulenceFreq SnowFallWindScale SnowFlakeCount
SnowFlakeSize SoftEdges SortPriority Spawners SpecMultiplier SpillableVolume SplashesAmount Spline SplineGeometry SurfaceFreezing
SurfaceUScale SurfaceVScale TODInfluence TerrainShadows TileLength TileWidth TimeOfDayOverride UseGlobalFogColor VertexContainer Vertices
ViewDistanceCap ViewDistanceCapEnabled ViewDistanceMultiplier Visible VolumeAccuracy VolumeType VolumetricFog VolumetricFogOnly
WaterCapFogAtVolumeDepth WaterCausticHeight WaterCausticIntensity WaterCausticTiling WaterCaustics WaterFogDensity WaterFogShadowing
WaterStreamSpeed WaterVolumeDepth WaveDampening WaveDepthCellSize WaveForce WaveHeightLimit WaveSimulationAreaGrowth WaveSleepThreshold
WaveSpeed WaveSurfaceCellSize WaveTimestep WeatherConfiguration Width WidthAttribute WidthFadeDist WindFadingDistance WindInfluence
WindRadius WindStrength X Y Z animationWavesAmount animationWavesSize animationWavesSpeed animationWindDirection animationWindSpeed
causticDepth causticDistanceAttenuation causticIntensity causticTiling causticsEnabled element fileName fogColorMultiplier fogDensity
height nearFogColor oceanMaterial oceanTransitionTime sliceAsset subId underwaterDistortion useOceanBottom m_worldTM m_localTM""".split()
FIELD_NAMES += ["Area X,Y,Z", "Bezier Data", "Disable Occlusion", "Polygon Shape Asset Id", "Rain Options", "Reduced Vertices",
                "Snow Options", "Spline Shape Asset Id", "Spline Type"]
NAMES: dict[int, str] = {}
for _name in FIELD_NAMES:
    NAMES.setdefault(F(_name), _name)   # a field id has no case ("Height" and "height"): the first name stays


def out_dir() -> Path:
    return env.content_dir() / "environment"


# ------------------------------------------------------------------------------------------------------------- the raw fields

def decode(element) -> object:
    """@return The value of an element of the object stream as JSON data: a container is an object of its named fields (a field
    that comes more than one time is a list; an unknown name is "crc:XXXXXXXX"), a value is the decoded number, text, vector,
    or asset; an unknown value type is {"type", "hex"}."""
    if element.value is None:
        out: dict = {}
        repeated: set[str] = set()
        for child in element.children:
            key = NAMES.get(child.name, "-" if child.name is None else f"crc:{child.name:08x}")
            value = decode(child)
            if key in repeated:
                out[key].append(value)
            elif key in out:
                out[key] = [out[key], value]
                repeated.add(key)
            else:
                out[key] = value
        return out
    type_text = str(uuid.UUID(bytes=element.type_id))
    data = element.value
    if type_text == ASSET and len(data) >= 56:
        size = int.from_bytes(data[48:56], "big")
        sub = int.from_bytes(data[16:20], "big")
        return {"guid": str(uuid.UUID(bytes=data[:16])), "subId": sub, "hint": data[56:56 + size].decode("latin-1"),
                "product": source.product(data[:16], sub)}
    decoder = VALUES.get(type_text)
    if decoder is not None:
        try:
            return decoder(data)
        except (struct.error, ValueError):
            pass
    return {"type": type_text, "hex": data.hex()}


# ---------------------------------------------------------------------------------------------------------- the main mission

def read_from_archive(archive: str, name: str) -> bytes:
    """@return An entry of one named pak (the shared index keeps only the first pak of an entry name)."""
    paks = source.paks()
    pak = paks.assets / archive
    number = paks.paks.index(pak)
    with zipfile.ZipFile(pak) as handle:
        info = handle.getinfo(name)
    saved = paks.entries.get(name)
    paks.entries[name] = (number, info.header_offset, info.compress_type, info.compress_size, info.file_size)
    try:
        return paks.read(name)
    finally:
        if saved is None:
            del paks.entries[name]
        else:
            paks.entries[name] = saved


def read_preset(root: ET.Element) -> dict:
    """@return The variables of a TimeOfDay element in their order: name, keys [time, value or [r, g, b], flags], editor value."""
    variables = []
    for variable in root.findall("Variable"):
        spline = variable.find("Spline")
        keys = parse_keys(spline.get("Keys", "")) if spline is not None else []
        saved = variable.get("Color") if "Color" in variable.attrib else variable.get("Value")
        variables.append({"name": variable.get("Name", ""), "keys": keys,
                          "saved": [float(x) for x in saved.split(",")] if saved and "," in saved else float(saved) if saved else None})
    return {"savedHour": float(root.get("Time", "0") or 0), "variables": variables}


def mission() -> dict:
    data = read_from_archive(MAIN_LEVEL_PAK, "mission_mission0.xml")
    root = ET.fromstring(data)
    environment = root.find("Environment")
    out = {"source": MAIN_LEVEL_PAK + ":mission_mission0.xml", "environment": {}}
    for element in (environment if environment is not None else []):
        out["environment"][element.tag] = dict(element.attrib)
    tod = root.find("TimeOfDay")
    if tod is not None:
        out["timeOfDay"] = read_preset(tod)
    return out


# ---------------------------------------------------------------------------------------------------------------- the curves

def parse_keys(text: str) -> list:
    """@return The keys of a spline text "t:v:flags," or "t:(r:g:b):flags,": [time, value, flags]."""
    out = []
    for token in text.strip().strip(",").split(","):
        if not token:
            continue
        match = re.fullmatch(r"([^:]+):\(([^)]+)\):([^:]+)", token)
        if match:
            out.append([float(match[1]), [float(x) for x in match[2].split(":")], int(match[3])])
        else:
            parts = token.split(":")
            out.append([float(parts[0]), float(parts[1]), int(parts[2]) if len(parts) > 2 else 0])
    return out


def evaluate(keys: list, t: float):
    """@return The value of a spline at the time t (0 to 1): the Bezier rule of the public Lumberyard splines (ISplines.h) with
    the tangent flags of each key: bits 0 to 2 the incoming tangent, bits 3 to 5 the outgoing one (2 and 3 zero or step, 4
    linear; the higher bits are editor flags). An exact key time gives the key value. This is a compatible evaluator (research,
    open point 4)."""
    if not keys:
        return None
    values = np.array([np.atleast_1d(np.array(k[1], dtype=np.float64)) for k in keys])
    times = np.array([k[0] for k in keys], dtype=np.float64)
    flags = [int(k[2]) for k in keys]
    scalar = not isinstance(keys[0][1], list)

    def result(v):
        return float(v[0]) if scalar else [float(x) for x in v]

    for index, time in enumerate(times):
        if abs(time - t) < 1e-7:
            return result(values[index])
    if len(keys) == 1 or t <= times[0]:
        return result(values[0])
    if t >= times[-1]:
        return result(values[-1])
    incoming = np.zeros_like(values)
    outgoing = np.zeros_like(values)
    outgoing[0] = (values[1] - values[0]) / 3
    incoming[-1] = (values[-1] - values[-2]) / 3
    for i in range(1, len(keys) - 1):
        weight = (times[i] - times[i - 1]) / (times[i + 1] - times[i - 1])
        delta = values[i + 1] - values[i - 1]
        incoming[i] = delta * weight / 3
        outgoing[i] = delta * (1 - weight) / 3
        flag_in, flag_out = flags[i] & 7, (flags[i] >> 3) & 7
        if flag_in in (2, 3):
            incoming[i] = 0
        elif flag_in == 4:
            incoming[i] = (values[i] - values[i - 1]) / 3
        if flag_out in (2, 3):
            outgoing[i] = 0
        elif flag_out == 4:
            outgoing[i] = (values[i + 1] - values[i]) / 3
    j = min(max(int(np.searchsorted(times, t, side="right")) - 1, 0), len(keys) - 2)
    u = (t - times[j]) / (times[j + 1] - times[j])
    if (flags[j] >> 3) & 7 == 3:
        return result(values[j + 1])
    if flags[j + 1] & 7 == 3:
        return result(values[j])
    p0, p3 = values[j], values[j + 1]
    p1, p2 = p0 + outgoing[j], p3 - incoming[j + 1]
    return result(((1 - u) ** 3) * p0 + 3 * u * ((1 - u) ** 2) * p1 + 3 * u * u * (1 - u) * p2 + u ** 3 * p3)


def curve_time(hour: float) -> float:
    """@return The time of a curve for an hour of the world clock (retail: float32(hour) x float32(1/24))."""
    return float(np.float32(hour) * np.float32(1.0 / 24.0))


def sample(preset: dict, hour: float) -> dict:
    """@return Each variable of a preset at an hour (the first variable of a name and its index; a later one of the same name has
    the key "NAME#INDEX")."""
    out = {}
    t = curve_time(hour)
    for index, variable in enumerate(preset["variables"]):
        name = variable["name"] if variable["name"] not in out else f"{variable['name']}#{index}"
        out[name] = evaluate(variable["keys"], t) if variable["keys"] else variable["saved"]
    return out


def presets() -> dict:
    out = {}
    for path in source.paks().paths("libs/timeofday/", ".xml"):
        out[path] = read_preset(ET.fromstring(source.read(path)))
    return out


def preset_path(file_name: str) -> str:
    """@return The pak path of the fileName of a TimeOfDayPOIConfig ("Libs/TimeOfDay\\zones\\everfall_a.xml", an old "Assets" prefix)."""
    path = file_name.replace("\\", "/").lower()
    while "//" in path:
        path = path.replace("//", "/")
    if path.startswith("assets/"):
        path = path[len("assets/"):]
    return path


# ------------------------------------------------------------------------------------------------------------- the placements

def entity_records(data: bytes) -> list[dict]:
    """@return The entities of a slice with an environment component: name, local matrix, kinds, components, shape."""
    if not any(kind in data for kind in KINDS):
        return []
    out = []
    for root in source.parse(data):
        for entity in source.walk(root):
            components = entity.child(COMPONENTS)
            if components is None or any(c.child(ENTITIES) is not None for c in components.children):
                continue
            found: dict[str, list] = {}
            shape = None
            matrix = np.eye(4)
            for component in components.children:
                tm = component.child(WORLD_TM)
                if tm is not None and tm.value is not None and len(tm.value) == 48:
                    matrix = az_matrix(tm.value)
                nodes = list(source.walk(component))
                kinds = {KINDS[n.type_id] for n in nodes if n.type_id in KINDS}
                for kind in kinds:
                    found.setdefault(kind, []).append(decode(component))
                if not kinds:
                    shapes = [SHAPES[n.type_id] for n in nodes if n.type_id in SHAPES]
                    if shapes and shape is None:
                        shape = {"type": shapes[0], "fields": decode(component)}
            if not found:
                continue
            out.append({"entity": source.text(entity.child(NAME)) if entity.child(NAME) is not None else "", "matrix": matrix,
                        "components": found, "shape": shape})
    return out


def light_kind(component: dict) -> str:
    configuration = component.get("LightConfiguration", {}) if isinstance(component, dict) else {}
    return "probe" if configuration.get("LightType") == 3 else "light"


class Walk:
    """The placement walk of the world (region.py rules): the chunk slices, the capitals, and the slices that a spawner spawns at
    the start, with a cache of each slice."""

    def __init__(self) -> None:
        self.slices: dict[str, tuple[list, list] | None] = {}
        self.failures: list[str] = []
        self.reads = 0

    def slice(self, path: str):
        if path not in self.slices:
            try:
                data = source.read(path)
                self.reads += 1
                records = entity_records(data)
                spawners = []
                if SPAWNER_MARK in data:
                    for spawner in read_slice(data).spawners:
                        if spawner.slice is not None and spawner.on_start:
                            child = source.product(spawner.slice[0], spawner.slice[1], ".dynamicslice")
                            if child:
                                spawners.append((child, rows4(spawner.matrix)))
                self.slices[path] = (records, spawners)
            except Exception as error:   # a slice that the reader cannot read: it has no environment record
                self.failures.append(f"{path}: {str(error)[:120]}")
                self.slices[path] = None
        return self.slices[path]

    def place(self, path: str, matrix: np.ndarray, origin: str, chain: tuple, out: list, seen: set) -> None:
        if len(chain) > MAX_DEPTH or path in chain:
            return
        key = (path, tuple(np.round(matrix[:3, :].ravel(), 4)))
        if key in seen:
            return
        seen.add(key)
        item = self.slice(path)
        if item is None:
            return
        records, spawners = item
        chain = chain + (path,)
        for index, record in enumerate(records):
            world = matrix @ record["matrix"]
            for kind, components in record["components"].items():
                for component in components:
                    out.append({"kind": light_kind(component) if kind == "light" else kind, "origin": origin, "chain": list(chain),
                                "entity": record["entity"], "index": index, "matrix": np.round(world, 6).tolist(),
                                "place": np.round(world[:3, 3], 4).tolist(), "shape": record["shape"], "fields": component})
        if origin == "chunk":
            return   # the spawners of a chunk slice are not followed (region.py and the research census)
        for child, local in spawners:
            self.place(child, matrix @ local, "nested", chain, out, seen)


def region_records(name: str, walk: Walk) -> tuple[list, dict]:
    """@return (records, counts) of the environment entities that a region places."""
    folder = WORLD + "regions/" + name + "/"
    out: list[dict] = []
    seen: set = set()
    root = source.parse(source.read(folder + "region.chunks"))[0]
    chunks = 0
    for chunk in root.child(F("chunks")).children:
        if source.number(chunk.child(F("chunkType"))) == 2:
            continue   # collision chunks
        a = source.asset_id(chunk.child(F("AssetId")))
        path = source.product(a[0], a[1], ".dynamicslice") if a else None
        if path is None:
            continue
        base = np.eye(4)
        base[:3, 3] = struct.unpack(">3f", chunk.child(F("WorldPosition")).value)
        chunks += 1
        walk.place(path, base, "chunk", (), out, seen)
    capitals = 0
    for manifest in source.paks().paths(folder + "capitals/", ".capitals.json"):
        for capital in json.loads(source.read(manifest)).get("Capitals", []):
            if capital.get("sliceName") == "<PLOT>":
                continue
            asset = str(capital.get("sliceAssetId", ""))
            path = source.product_of_text(asset, ".dynamicslice") if asset.startswith("{") and not asset.startswith("{00000000") else None
            if path is None:
                key = slice_key(capital.get("sliceName", ""))
                path = next((p for p in ("slices/" + key + ".dynamicslice", key + ".dynamicslice") if source.has(p)), None)
            if path is None:
                continue
            q, p = capital.get("rotation", {}), capital.get("worldPosition", {})
            m = np.eye(4)
            m[:3, :3] = quat_matrix(q.get("x", 0.0), q.get("y", 0.0), q.get("z", 0.0), q.get("w", 1.0)) * float(capital.get("scale", 1.0))
            m[:3, 3] = (p.get("x", 0.0), p.get("y", 0.0), p.get("z", 0.0))
            capitals += 1
            walk.place(path, m, "capital", (), out, seen)
    counts: dict[str, int] = {"chunks": chunks, "capitals": capitals}
    for record in out:
        counts[record["kind"]] = counts.get(record["kind"], 0) + 1
    return out, counts


# ------------------------------------------------------------------------------------------------------------------- the sky

def sky(paths: set[str]) -> dict:
    """@return The meshes of each sky slice and their materials (the sub-materials with their shader, textures, texture motion,
    and public parameters: the cloud layers of the dome)."""
    from . import cgf, materials
    out = {}
    for path in sorted(paths):
        if not source.has(path):
            out[path] = {"missing": True}
            continue
        meshes = []
        for mesh, matrix in slice_meshes(source.read(path)):
            entry = {"mesh": mesh, "matrix": np.round(matrix, 6).tolist()}
            try:
                reference = cgf.material_guid(cgf.read_chunks(source.read(mesh)))
                mtl, subs = materials.load_mtl(reference, mesh) if reference else (None, [])
                entry["material"] = mtl
                entry["subMaterials"] = [{"attributes": dict(sub.attrib),
                                          "textures": [{**t.attrib, "motion": [dict(m.attrib) for m in t.iter("TexMod")]} for t in sub.iter("Texture")],
                                          "parameters": [dict(p.attrib) for p in sub.iter("PublicParams")]} for sub in subs]
            except Exception as error:
                entry["error"] = str(error)[:160]
            meshes.append(entry)
        out[path] = {"meshes": meshes}
    return out


# ----------------------------------------------------------------------------------------------------------------- the shapes

def inside(record: dict, point: tuple[float, float, float]) -> bool:
    """@return True when a point (engine m) is in the shape of a record (its local frame: the inverse of the world matrix; a
    cylinder and a box are centered on the origin of the entity, a polygon prism goes up from it)."""
    shape = record.get("shape")
    if not shape:
        return False
    local = np.linalg.inv(np.array(record["matrix"])) @ np.array([*point, 1.0])
    x, y, z = local[:3]
    configuration = shape["fields"].get("Configuration", shape["fields"]) if isinstance(shape["fields"], dict) else {}
    kind = shape["type"]
    if kind == "cylinder":
        return math.hypot(x, y) <= configuration.get("Radius", 0) and abs(z) <= configuration.get("Height", 0) / 2
    if kind == "sphere":
        return math.sqrt(x * x + y * y + z * z) <= configuration.get("Radius", 0)
    if kind == "box":
        dx, dy, dz = configuration.get("Dimensions", [0, 0, 0])
        return abs(x) <= dx / 2 and abs(y) <= dy / 2 and abs(z) <= dz / 2
    if kind == "capsule":
        height, radius = configuration.get("Height", 0), configuration.get("Radius", 0)
        zc = min(max(z, -height / 2 + radius), height / 2 - radius)
        return math.sqrt(x * x + y * y + (z - zc) ** 2) <= radius
    if kind == "polygonPrism":
        prism = find_field(shape["fields"], "PolygonPrism") or {}
        vertices = find_field(prism, "Vertices") or []
        if isinstance(vertices, dict):
            vertices = vertices.get("element", [])
        height = prism.get("Height", 0) if isinstance(prism, dict) else 0
        return 0 <= z <= height and point_in_polygon(x, y, vertices)
    return False


def find_field(value, name: str):
    if isinstance(value, dict):
        if name in value:
            return value[name]
        for child in value.values():
            found = find_field(child, name)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find_field(child, name)
            if found is not None:
                return found
    return None


def point_in_polygon(x: float, y: float, vertices) -> bool:
    points = [v for v in (vertices if isinstance(vertices, list) else []) if isinstance(v, list) and len(v) >= 2]
    result = False
    for i in range(len(points)):
        (x1, y1), (x2, y2) = points[i][:2], points[i - 1][:2]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            result = not result
    return result


# ---------------------------------------------------------------------------------------------------------------- the commands

def regions_of_world() -> list[str]:
    return sorted({p[len(WORLD + "regions/"):].split("/")[0] for p in source.paks().paths(WORLD + "regions/", "/region.chunks")})


VOLUME_KINDS = ("timeOfDay", "fog", "rain", "snow", "weather", "ocean")


def shape_entry(shape: dict | None) -> dict | None:
    """@return The compact shape of a volume for the client: the type and its sizes (m, in the frame of the entity)."""
    if not shape:
        return None
    configuration = shape["fields"].get("Configuration", shape["fields"]) if isinstance(shape["fields"], dict) else {}
    kind = shape["type"]
    if kind in ("cylinder", "capsule"):
        return {"type": kind, "radius": configuration.get("Radius", 0.0), "height": configuration.get("Height", 0.0)}
    if kind == "sphere":
        return {"type": kind, "radius": configuration.get("Radius", 0.0)}
    if kind == "box":
        return {"type": kind, "dimensions": configuration.get("Dimensions", [0.0, 0.0, 0.0])}
    if kind == "polygonPrism":
        prism = find_field(shape["fields"], "PolygonPrism") or {}
        vertices = find_field(prism, "Vertices") or []
        if isinstance(vertices, dict):
            vertices = vertices.get("element", [])
        return {"type": kind, "height": prism.get("Height", 0.0) if isinstance(prism, dict) else 0.0,
                "vertices": [v[:2] for v in vertices if isinstance(v, list)]}
    return {"type": kind}


def volume_entry(record: dict, region: str) -> dict:
    """@return The compact volume of a record for the client (volumes.json): a time-of-day volume with its preset, priority, blend,
    hour override, sky, and color grading; another kind with its raw fields (stage E3)."""
    out = {"kind": record["kind"], "region": region, "entity": record["entity"], "matrix": record["matrix"], "shape": shape_entry(record["shape"])}
    if record["kind"] == "timeOfDay":
        configuration = record["fields"].get("Configuration", {})
        out.update({"preset": preset_path(configuration.get("fileName", "")), "priority": configuration.get("Priority", 0),
                    "blendTime": configuration.get("BlendTime", 0.0), "blendDistance": configuration.get("BlendDistance", 0.0),
                    "hourOverride": configuration.get("TimeOfDayOverride") if configuration.get("HasTimeOfDayOverride") else None,
                    "sky": (configuration.get("sliceAsset") or {}).get("product"),
                    "colorGrading": (configuration.get("ColorGrading") or {}).get("product")})
    else:
        out["fields"] = record["fields"]
    return out


def write(names: list[str] | None = None) -> dict:
    out = out_dir()
    (out / "regions").mkdir(parents=True, exist_ok=True)
    doc_mission = mission()
    (out / "mission.json").write_text(json.dumps(doc_mission, indent=1))
    doc_presets = presets()
    (out / "presets.json").write_text(json.dumps(doc_presets, separators=(",", ":")))
    walk = Walk()
    index = {"regions": {}, "presets": len(doc_presets), "failures": []}
    sky_paths: set[str] = set()
    volumes: list[dict] = []
    for name in names or regions_of_world():
        records, counts = region_records(name, walk)
        volumes += [volume_entry(record, name) for record in records if record["kind"] in VOLUME_KINDS]
        for record in records:
            if record["kind"] == "timeOfDay":
                configuration = record["fields"].get("Configuration", {})
                slice_asset = configuration.get("sliceAsset") if isinstance(configuration, dict) else None
                if isinstance(slice_asset, dict) and (slice_asset.get("product") or slice_asset.get("hint")):
                    sky_paths.add(slice_asset.get("product") or slice_asset.get("hint"))
        (out / "regions" / f"{name}.json").write_text(json.dumps({"region": name, "space": "engine (m, Z up, right-handed; rows)",
                                                                  "counts": counts, "records": records}, separators=(",", ":")))
        index["regions"][name] = counts
        print(f"environment: {name} {json.dumps(counts)}", flush=True)
    (out / "volumes.json").write_text(json.dumps({"space": "engine (m, Z up, right-handed; rows)", "volumes": volumes}, separators=(",", ":")))
    index["volumes"] = len(volumes)
    (out / "sky.json").write_text(json.dumps(sky(sky_paths), indent=1))
    index["skySlices"] = len(sky_paths)
    index["slicesRead"] = walk.reads
    index["failures"] = walk.failures[:50]
    index["failureCount"] = len(walk.failures)
    (out / "index.json").write_text(json.dumps(index, indent=1))
    return {k: v for k, v in index.items() if k != "regions"} | {"regionsWritten": len(index["regions"])}


def region_of(x: float, y: float) -> str:
    return f"r_{int(x // 2048):+03d}_{int(y // 2048):+03d}"


def time_volumes_at(point: tuple[float, float, float]) -> list[dict]:
    """@return The time-of-day volumes that hold a point, with their preset, priority, blend, and override (the highest priority
    first)."""
    out = []
    records = []
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):   # a volume of a near region can hold the point (a placement is in the region of its chunk)
            path = out_dir() / "regions" / f"{region_of(point[0] + dx * 2048, point[1] + dy * 2048)}.json"
            if path.is_file():
                records += json.loads(path.read_text())["records"]
    for record in records:
        if record["kind"] != "timeOfDay" or not inside(record, point):
            continue
        configuration = record["fields"].get("Configuration", {})
        out.append({"entity": record["entity"], "slice": record["chain"][-1], "place": record["place"], "shape": record["shape"]["type"],
                    "size": {k: v for k, v in record["shape"]["fields"].get("Configuration", {}).items() if k in ("Radius", "Height", "Dimensions")},
                    "preset": preset_path(configuration.get("fileName", "")), "priority": configuration.get("Priority"),
                    "blendTime": configuration.get("BlendTime"), "blendDistance": configuration.get("BlendDistance"),
                    "hourOverride": configuration.get("TimeOfDayOverride") if configuration.get("HasTimeOfDayOverride") else None,
                    "sky": (configuration.get("sliceAsset") or {}).get("product"), "colorGrading": (configuration.get("ColorGrading") or {}).get("product")})
    return sorted(out, key=lambda v: -(v["priority"] or 0))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-environment", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("write")
    p.add_argument("--regions", nargs="*")
    p = sub.add_parser("volumes")
    p.add_argument("x", type=float)
    p.add_argument("y", type=float)
    p.add_argument("z", type=float)
    p = sub.add_parser("sample")
    p.add_argument("preset")
    p.add_argument("hour", type=float)
    p.add_argument("names", nargs="*")
    args = parser.parse_args(argv)
    if args.command == "write":
        print(json.dumps(write(args.regions), indent=1))
    elif args.command == "volumes":
        print(json.dumps(time_volumes_at((args.x, args.y, args.z)), indent=1))
    elif args.command == "sample":
        doc = json.loads((out_dir() / "presets.json").read_text())
        values = sample(doc[preset_path(args.preset)], args.hour)
        print(json.dumps({k: v for k, v in values.items() if not args.names or k in args.names}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
