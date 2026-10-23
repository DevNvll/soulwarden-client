"""The environment of the world (stage E0, docs/ENVIRONMENT.md; research ~/Games/soulwarden-client/research/world-environment.md):
the data of the time of day, the sky, the fog, the weather, and the local lights, read from the game copy (read only).

Usage: python3 -B -m swc_convert.environment write [--regions NAME...] | cookies | sky | probes | check | sample PRESET HOUR [NAME...]

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
  lights/REGION.json    the visible lights of the region for the client (stage E2): the type (point, spot, rect), the place, the +X
                        axis (the direction of a spot and of a rect) and the +Z axis, the color, the multipliers, the range, the
                        bulb, the field of view, the size, the cookie, the start state, the shadow flag, the animation, the view
                        distance cap; a light of an NPC slice has the slice, its matrix in the frame of that slice, and the
                        place of the slice root (the static place of the NPC)
  cookies/NAME.png      the projector textures of the lights (the command cookies, after write; the mip of at most 512 pixels),
  cookies.json          and their list: the game path of each texture, its PNG, its size, and its DXGI format
  sky/stars.png         the star catalog of the engine (engineassets/sky/stars.dat; the command sky): an equirectangular map of the
                        celestial sphere (u: the right ascension 0 to 24 h, v: the declination +90 to -90 degrees), each star a small
                        spot of its color and brightness (sRGB)
  sky/moon.png          the moon texture of the mission (Moon/@Texture)
  sky/rainfall.png, sky/snowflakes.png  the rain layer texture and the snowflake atlas of the engine (the precipitation of E3)
  snow/NAME.png, snow/snow.json  the textures of the surface snow pass of the source (the command snow, AST-257; research/snow.md
                        section 3: deferredsnow.cfxb): the areal layout noise, the frost bump noise, the snow normal derivatives,
                        the snow spatter, and the frost bubbles, as linear RGBA (the 3D volume noise is not read)
  sky/sky.json          the stars, the moon, and the cloud cover of each sky slice (the cloud masks of its dome layers: 1 - the
                        product of (1 - the mean of each mask))
  probes/NAME.hdr       the world probes of the outdoor ambient (the command probes, stage E5): the radiance cubemap (BC6H, mip 0)
                        of each probe whose box covers the world, as a long-lat Radiance picture in the units of the source (the
                        light unit 10,000 cd/m2 is the client's), u = the longitude (east at 0.75, north at 0.5), v = up to down
                        and of each cubemap of the local probes (step b)
  probes.json           world: the world probes (the cubemap, the picture, the diffuse and specular multipliers, the TOD influence,
                        the boxes); local: each local probe (its box: the place, the rows of the entity matrix, Area X,Y,Z; the
                        priority, the color, the multipliers, the edge falloff, the box projection, the vis area switches);
                        cubes: the picture of each cubemap of the local probes and the face means of its radiance and diffuse cubes
  volumes.json          the volumes of the world for the client in one compact file: the time-of-day volumes (shape, preset,
                        priority, blend, hour override, sky, color grading) and the fog, rain, snow, weather, and ocean volumes
  sky.json              the sky slices of the presets: their meshes, materials, textures, texture motion, and parameters
  index.json            the counts

The time of a curve is hour / 24 (the retail evaluator multiplies the hour by the float32 1/24: research, normalization
evidence). The distribution rows of a region are not read (no environment component; research scope).
"""
from __future__ import annotations

import argparse
import io
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

    def place(self, path: str, matrix: np.ndarray, origin: str, chain: tuple, out: list, seen: set, roots: tuple = ()) -> None:
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
        roots = roots + (matrix,)
        owner = next((i for i, c in enumerate(chain) if "/characters/" in c), None)   # an NPC slice owns the lights below it
        for index, record in enumerate(records):
            world = matrix @ record["matrix"]
            for kind, components in record["components"].items():
                for component in components:
                    entry = {"kind": light_kind(component) if kind == "light" else kind, "origin": origin, "chain": list(chain),
                             "entity": record["entity"], "index": index, "matrix": np.round(world, 6).tolist(),
                             "place": np.round(world[:3, 3], 4).tolist(), "shape": record["shape"], "fields": component}
                    if kind == "light" and owner is not None:
                        entry["npc"] = {"slice": chain[owner], "local": np.round(np.linalg.inv(roots[owner]) @ world, 6).tolist(),
                                        "root": np.round(roots[owner][:3, 3], 4).tolist()}
                    out.append(entry)
        if origin == "chunk":
            return   # the spawners of a chunk slice are not followed (region.py and the research census)
        for child, local in spawners:
            self.place(child, matrix @ local, "nested", chain, out, seen, roots)


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


LIGHT_TYPES = {0: "point", 1: "rect", 2: "spot"}
NEVER = 0xFFFFFFFF


def light_entry(record: dict) -> dict | None:
    """@return The compact light of a record for the client (lights/REGION.json; stage E2), or no value for a probe or a hidden light
    (Visible false: the light rigs of the mission NPC masters and the helpers of the level design). The direction of a projector and of
    an area light is the +X axis of the entity (the data: 988 of the 1,335 projectors of r_+04_+02 have +X down)."""
    configuration = record["fields"].get("LightConfiguration", {})
    kind = LIGHT_TYPES.get(configuration.get("LightType"))
    if kind is None or not configuration.get("Visible", True):
        return None
    m = np.array(record["matrix"])
    axis_x = m[:3, 0] / (np.linalg.norm(m[:3, 0]) or 1.0)
    axis_z = m[:3, 2] / (np.linalg.norm(m[:3, 2]) or 1.0)
    rng = configuration.get({"point": "PointMaxDistance", "spot": "ProjectorDistance", "rect": "AreaMaxDistance"}[kind], 0.0)
    cookie = (configuration.get("ProjectorTexture") or {}).get("BaseClass1", {}).get("AssetPath", "") if kind == "spot" else ""
    out = {"type": kind, "p": [round(v, 4) for v in m[:3, 3].tolist()], "x": [round(v, 5) for v in axis_x.tolist()],
           "z": [round(v, 5) for v in axis_z.tolist()], "color": [round(v, 6) for v in (configuration.get("COLOR") or [1, 1, 1])[:3]],
           "diffuse": configuration.get("DiffuseMultiplier", 1.0), "spec": configuration.get("SpecMultiplier", 1.0), "range": rng,
           "bulb": configuration.get("PointAttenuationBulbSize" if kind != "spot" else "ProjectorAttenuationBulbSize", 0.0),
           "fov": configuration.get("ProjectorFOV", 90.0), "w": configuration.get("AreaWidth", 0.0), "h": configuration.get("AreaHeight", 0.0),
           "cookie": cookie if cookie and "spot_default" not in cookie else "", "on": bool(configuration.get("OnInitially", True)),
           "shadow": configuration.get("CastShadowsSpec", NEVER) != NEVER,
           "anim": [configuration.get("AnimIndex", 0), configuration.get("AnimSpeed", 1.0), configuration.get("AnimPhase", 0.0),
                    bool(configuration.get("AnimPhaseRandom", False))],
           "cap": configuration.get("ViewDistanceCap", 0.0) if configuration.get("ViewDistanceCapEnabled") else 0.0,
           "volumetric": bool(configuration.get("VolumetricFog", False)), "origin": record["origin"], "slice": record["chain"][-1]}
    if "npc" in record:
        out["npc"] = record["npc"]["slice"]
        out["local"] = record["npc"]["local"]
        out["root"] = record["npc"]["root"]
    return out


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
        lights = [entry for entry in (light_entry(record) for record in records if record["kind"] == "light") if entry is not None]
        (out / "lights").mkdir(exist_ok=True)
        (out / "lights" / f"{name}.json").write_text(json.dumps({"region": name, "space": "engine (m, Z up, right-handed)", "lights": lights},
                                                                separators=(",", ":")))
        counts["lightsVisible"] = len(lights)
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


def write_table(preset: dict, out: Path) -> dict:
    """Writes the keys of a preset and the values of this evaluator each 15 game minutes, in a plain text form for the test of the
    client evaluator (tools/test-environment.sh): "key VARIABLE CHANNELS TIME V0 V1 V2 FLAGS" and "value VARIABLE HOUR V0 V1 V2"."""
    lines = []
    count = 0
    for index, variable in enumerate(preset["variables"]):
        if not variable["keys"]:
            continue
        channels = 3 if isinstance(variable["keys"][0][1], list) else 1
        for time, value, flags in variable["keys"]:
            v = value if channels == 3 else [value, 0.0, 0.0]
            lines.append(f"key {index} {channels} {time!r} {v[0]!r} {v[1]!r} {v[2]!r} {flags}")
        for step in range(97):
            hour = step * 0.25
            value = evaluate(variable["keys"], curve_time(hour))
            v = value if channels == 3 else [value, 0.0, 0.0]
            lines.append(f"value {index} {hour!r} {v[0]!r} {v[1]!r} {v[2]!r}")
            count += 1
    out.write_text("\n".join(lines) + "\n")
    return {"file": str(out), "values": count}


COOKIE_SIZE = 512


def cookie_key(path: str) -> str:
    """@return The game path of a projector texture (lower case, no @assets@ prefix)."""
    path = path.lower().replace("\\", "/")
    return path[len("@assets@/"):] if path.startswith("@assets@/") else path


def cookies() -> dict:
    """Decodes the projector textures of the lights of lights/REGION.json (stage E2) to cookies/NAME.png and writes cookies.json."""
    from . import dds
    import hashlib
    out = out_dir()
    paths = set()
    for file in sorted((out / "lights").glob("*.json")):
        paths.update(cookie_key(light["cookie"]) for light in json.loads(file.read_text())["lights"] if light.get("cookie"))
    # The projector lights of the front end scenes (frontend.py: nighthaven.json, creation-scene.json; stage Q3).
    for file in sorted((env.content_dir() / "frontend").glob("*.json")):
        doc = json.loads(file.read_text())
        if isinstance(doc, dict) and isinstance(doc.get("lights"), list):
            paths.update(cookie_key(light["cookie"]) for light in doc["lights"] if isinstance(light, dict) and light.get("cookie"))
    (out / "cookies").mkdir(exist_ok=True)
    table, failures = {}, []
    for path in sorted(paths):
        try:
            data, w, h, fmt = dds.join(source.read, path, COOKIE_SIZE)
            image = dds.to_image(data, w, h, fmt)
        except Exception as error:   # a missing or an unknown texture: the light stays with no cookie
            failures.append(f"{path}: {str(error)[:100]}")
            continue
        name = f"{Path(path).stem}_{hashlib.sha1(path.encode()).hexdigest()[:6]}"
        image.save(out / "cookies" / f"{name}.png")
        table[path] = {"png": f"cookies/{name}.png", "name": name, "size": [w, h], "format": fmt, "mode": image.mode}
    (out / "cookies.json").write_text(json.dumps({"cookies": table, "failures": failures}, indent=1))
    return {"cookies": len(table), "failures": failures}


STAR_MAP = (4096, 2048)


def read_stars(data: bytes) -> list[tuple[float, float, tuple[int, int, int], int]]:
    """@return The stars of engineassets/sky/stars.dat: "STAR", the version 0x00010001, the count, then 12 bytes each: the right
    ascension (float32, hours), the declination (float32, degrees), and the bytes red, green, blue, and brightness (255 the
    brightest). The first two stars are Sirius (6.75 h, -16.7) and Canopus (6.40 h, -52.7)."""
    if data[:4] != b"STAR":
        raise ValueError("not a star catalog")
    version, count = struct.unpack_from("<II", data, 4)
    if version != 0x00010001 or len(data) < 12 + 12 * count:
        raise ValueError(f"star catalog version {version:#x} or size {len(data)}")
    stars = []
    for i in range(count):
        ra, dec = struct.unpack_from("<ff", data, 12 + 12 * i)
        r, g, b, brightness = data[12 + 12 * i + 8:12 + 12 * i + 12]
        stars.append((ra, dec, (r, g, b), brightness))
    return stars


def star_map(stars: list, size: tuple[int, int] = STAR_MAP) -> np.ndarray:
    """@return A linear RGB map (height, width, 3) of the stars: a Gaussian spot of 0.7 texel (wider in u by 1 / cos(declination)) with
    the color of the star times its brightness / 255."""
    width, height = size
    out = np.zeros((height, width, 3), dtype=np.float32)
    sigma = 0.7
    for ra, dec, color, brightness in stars:
        u = (ra % 24.0) / 24.0 * width
        v = (90.0 - dec) / 180.0 * height
        stretch = 1.0 / max(math.cos(math.radians(dec)), 0.05)
        su = sigma * stretch
        x0, x1 = int(math.floor(u - 3 * su)), int(math.ceil(u + 3 * su))
        y0, y1 = max(int(math.floor(v - 3 * sigma)), 0), min(int(math.ceil(v + 3 * sigma)), height - 1)
        xs = np.arange(x0, x1 + 1)
        ys = np.arange(y0, y1 + 1)
        weight = np.exp(-0.5 * (((xs[None, :] + 0.5 - u) / su) ** 2 + ((ys[:, None] + 0.5 - v) / sigma) ** 2))
        value = np.array(color, dtype=np.float32) / 255.0 * (brightness / 255.0)
        out[np.ix_(ys, xs % width)] += weight[:, :, None] * value[None, None, :]
    return out


def to_srgb8(linear: np.ndarray) -> np.ndarray:
    x = np.clip(linear, 0.0, 1.0)
    return np.round(np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055) * 255).astype(np.uint8)


def sky_cover(slice_entry: dict) -> tuple[float, list]:
    """@return The cloud cover of a sky slice (1 - the product of (1 - the mean of each cloud mask of its dome layers)) and the
    layers (the mask, its mean, the sky color multiplier)."""
    from . import dds
    layers, clear = [], 1.0
    for mesh in slice_entry.get("meshes", []):
        for sub in mesh.get("subMaterials", []):
            for texture in sub.get("textures", []):
                if texture.get("Map") != "Diffuse":
                    continue
                path = texture["File"].lower().replace("\\", "/")
                try:
                    data, w, h, fmt = dds.join(source.read, path, 256)
                    mean = float(np.asarray(dds.to_image(data, w, h, fmt).convert("L"), dtype=np.float32).mean() / 255.0)
                except Exception:   # a source texture that is not in the game copy (.tif): no layer
                    continue
                multiplier = (sub.get("parameters") or [{}])[0].get("SkyColorMultiplier")
                layers.append({"mask": path, "mean": round(mean, 4), "skyColorMultiplier": multiplier})
                clear *= 1.0 - mean
    return (round(1.0 - clear, 4) if layers else 0.0), layers


def sky() -> dict:
    """Writes sky/stars.png, sky/moon.png, and sky/sky.json (stage E3)."""
    from PIL import Image
    from . import dds
    out = out_dir() / "sky"
    out.mkdir(parents=True, exist_ok=True)
    stars = read_stars(source.read("engineassets/sky/stars.dat"))
    Image.fromarray(to_srgb8(star_map(stars)), "RGB").save(out / "stars.png")
    mission = json.loads((out_dir() / "mission.json").read_text())
    moon_path = ((mission.get("environment") or {}).get("Moon") or {}).get("Texture", "textures/skys/night/moon_full.dds").lower()
    data, w, h, fmt = dds.join(source.read, moon_path, 512)
    dds.to_image(data, w, h, fmt).convert("RGBA").save(out / "moon.png")
    # The precipitation (stage E3): the rain layer texture of the rain stage of the source and the snowflake atlas (2 x 2).
    for path, name in (("textures/rain/rainfall.dds", "rainfall.png"), ("engineassets/textures/snowflakes.dds", "snowflakes.png")):
        pdata, pw, ph, pfmt = dds.join(source.read, path, 512)
        dds.to_image(pdata, pw, ph, pfmt).convert("RGBA").save(out / name)
    skies = {}
    for name, entry in json.loads((out_dir() / "sky.json").read_text()).items():
        cover, layers = sky_cover(entry)
        skies[name] = {"cover": cover, "layers": layers}
    doc = {"stars": {"png": "sky/stars.png", "count": len(stars), "size": list(STAR_MAP), "source": "engineassets/sky/stars.dat",
                     "frame": "u: right ascension 0 to 24 h; v: declination +90 to -90 degrees"},
           "moon": {"png": "sky/moon.png", "source": moon_path, "size": [w, h]},
           "rain": {"png": "sky/rainfall.png", "source": "textures/rain/rainfall.dds"},
           "snow": {"png": "sky/snowflakes.png", "source": "engineassets/textures/snowflakes.dds", "atlas": [2, 2]}, "skies": skies}
    (out / "sky.json").write_text(json.dumps(doc, indent=1))
    return {"stars": len(stars), "moon": moon_path, "skies": {k.split("/")[-1]: v["cover"] for k, v in skies.items()}}


# The textures of the surface snow pass of the source (AST-257; deferredsnow.cfxb.txt lines 61 to 112): the key, the game path.
SNOW_TEXTURES = (("layout", "engineassets/textures/defaults/perlinnoise2d.dds"),
                 ("frost", "engineassets/textures/frozen/frost_noise3.dds"),
                 ("derivatives", "engineassets/textures/perlinnoisederivatives.dds"),
                 ("spatter", "engineassets/textures/frozen/snow_spatter.dds"),
                 ("bubbles", "engineassets/textures/frozen/frost_noise4.dds"))


def snow() -> dict:
    """Writes snow/NAME.png (linear RGBA, the decoded values of each texture) and snow/snow.json (AST-257)."""
    from PIL import Image
    from . import dds
    out = out_dir() / "snow"
    out.mkdir(parents=True, exist_ok=True)
    doc = {}
    for key, path in SNOW_TEXTURES:
        data, w, h, fmt = dds.join(source.read, path, 1024)
        dds.to_image(data, w, h, fmt).convert("RGBA").save(out / f"{key}.png")
        doc[key] = {"png": f"snow/{key}.png", "source": path, "size": [w, h], "format": fmt}
    (out / "snow.json").write_text(json.dumps(doc, indent=1))
    return doc


# The world probes (stage E5): a probe box larger than this (m3) covers the world (the outdoor ambient of the source).
WORLD_PROBE_VOLUME = 1.0e12
PROBE_SIZE = (512, 256)


def cube_face_lookup(faces: list, direction: np.ndarray) -> np.ndarray:
    """@return The texels of a cube (6 faces in the D3D order and layout, the D3D z axis up: the world probes of the source) for
    the unit directions (N x 3: east, north, up), bilinear in the face."""
    x, y, z = direction[:, 0], direction[:, 1], direction[:, 2]
    ax, ay, az = np.abs(x), np.abs(y), np.abs(z)
    face = np.where((ax >= ay) & (ax >= az), np.where(x > 0, 0, 1), np.where(ay >= az, np.where(y > 0, 2, 3), np.where(z > 0, 4, 5)))
    ma = np.choose(face, [ax, ax, ay, ay, az, az])
    sc = np.choose(face, [-z, z, x, x, x, -x])
    tc = np.choose(face, [-y, -y, z, -z, -y, -y])
    size = faces[0].shape[0]
    s = np.clip((sc / ma + 1.0) * 0.5 * size - 0.5, 0.0, size - 1.0)
    t = np.clip((tc / ma + 1.0) * 0.5 * size - 0.5, 0.0, size - 1.0)
    s0, t0 = np.floor(s).astype(int), np.floor(t).astype(int)
    s1, t1 = np.minimum(s0 + 1, size - 1), np.minimum(t0 + 1, size - 1)
    fs, ft = (s - s0)[:, None], (t - t0)[:, None]
    stack = np.stack(faces)
    return (stack[face, t0, s0] * (1 - fs) * (1 - ft) + stack[face, t0, s1] * fs * (1 - ft)
            + stack[face, t1, s0] * (1 - fs) * ft + stack[face, t1, s1] * fs * ft)


def write_hdr(path: Path, rgb: np.ndarray) -> None:
    """Writes a Radiance picture (flat RGBE scanlines, the top row first)."""
    height, width, _ = rgb.shape
    peak = rgb.max(axis=2)
    mantissa, exponent = np.frexp(np.maximum(peak, 1e-32))
    scale = np.where(peak > 1e-32, mantissa * 256.0 / np.maximum(peak, 1e-32), 0.0)
    out = np.zeros((height, width, 4), np.uint8)
    out[..., :3] = np.clip(rgb * scale[..., None], 0, 255).astype(np.uint8)
    out[..., 3] = np.where(peak > 1e-32, exponent + 128, 0).astype(np.uint8)
    # A flat pixel must not look like a run mark (1, 1, 1) or like the start of a run-length scanline (2, 2).
    green, blue = out[..., 1], out[..., 2]
    blue[(out[..., 0] == 1) & (green == 1) & (blue == 1)] = 2
    green[(out[..., 0] == 2) & (green == 2)] = 3
    path.write_bytes(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode() + out.tobytes())


FACE_LABELS = ("+east", "-east", "+north", "-north", "+up", "-up")


def cube_faces(cubemap: str):
    """@return The six faces of mip 0 of a BC6H cubemap of the source, or no value."""
    from . import bc6h
    base = source.read(cubemap)
    size = struct.unpack_from("<I", base, 16)[0]
    face_bytes = max(1, size // 4) ** 2 * 16
    # Mip 0 of the six faces: the split file of that size (the source keeps the larger mips in NAME.dds.1 to .N).
    data = None
    for split in range(8, 0, -1):
        if source.has(f"{cubemap}.{split}"):
            part = source.read(f"{cubemap}.{split}")
            data = part if len(part) == 6 * face_bytes else None
            break
    if data is None and len(base) >= 148 + 6 * face_bytes:
        data = base[148:148 + 6 * face_bytes]
    if data is None:
        return None
    return [bc6h.decode(data[f * face_bytes:(f + 1) * face_bytes], size, size) for f in range(6)]


def face_means(faces: list) -> dict:
    return {label: [round(float(c), 4) for c in face.reshape(-1, 3).mean(0)] for label, face in zip(FACE_LABELS, faces)}


def probe_name(cubemap: str) -> str:
    """@return The picture name of a cubemap: its file name in textures/cubemaps/world, else its folder and file name (the same file
    name is in more than one folder: environmentprobe1_cm in 7)."""
    path = Path(cubemap)
    return path.stem if path.parent.name == "world" else f"{path.parent.name}_{path.stem}"


def probe_picture(cubemap: str, folder: Path, direction: np.ndarray) -> dict:
    """Writes probes/NAME.hdr (the radiance of mip 0, long-lat) and @return its entry; the face means of the diffuse cube of the source
    (NAME_diff.dds, 32 x 32: the radiance convolved with the cosine over pi, within 2 % of the mean of the radiance cube; the cube
    that the tiled shading of the source samples for the ambient, tiledshading.cfib AddProbeDiffuse)."""
    faces = cube_faces(cubemap)
    if faces is None:
        return {"error": "no mip 0 of six faces"}
    width, height = PROBE_SIZE
    name = probe_name(cubemap)
    write_hdr(folder / f"{name}.hdr", cube_face_lookup(faces, direction).reshape(height, width, 3))
    entry = {"hdr": f"probes/{name}.hdr", "size": [width, height], "faceSize": faces[0].shape[0], "faceMeans": face_means(faces)}
    diffuse = cubemap.removesuffix(".dds") + "_diff.dds"
    if source.has(diffuse):
        diffuse_faces = cube_faces(diffuse)
        if diffuse_faces is not None:
            entry["diffuseMeans"] = face_means(diffuse_faces)
    return entry


def probes() -> dict:
    """Writes probes/NAME.hdr and probes.json: the world probes of the outdoor ambient and the local probes (stage E5)."""
    found: dict[str, dict] = {}
    local: list[dict] = []
    for path in sorted((out_dir() / "regions").glob("*.json")):
        for record in json.loads(path.read_text())["records"]:
            if record.get("kind") != "probe":
                continue
            config = record["fields"]["LightConfiguration"]
            area = config.get("Area X,Y,Z") or [0.0, 0.0, 0.0]
            cubemap = config["CubemapTexture"].replace("\\", "/").lower()
            if area[0] * area[1] * area[2] < WORLD_PROBE_VOLUME:
                # A local probe (step b): its box (the rows of the entity matrix and the Area X,Y,Z), its order, its multipliers, the
                # fade at its edge, the box projection, and the vis area switches; the rule of the weights is in the client.
                local.append({"region": path.stem, "place": record["place"], "rows": [row[:3] for row in record["matrix"][:3]],
                              "area": area, "priority": config.get("SortPriority", 0), "cubemap": cubemap,
                              "color": (config.get("COLOR") or [1.0, 1.0, 1.0])[:3], "diffuseMultiplier": config.get("DiffuseMultiplier", 1.0),
                              "specMultiplier": config.get("SpecMultiplier", 1.0), "falloff": config.get("AttenuationFalloffMax", 0.0),
                              "boxProject": bool(config.get("BoxProject")),
                              "boxSize": [config.get("BoxWidth", 1.0), config.get("BoxLength", 1.0), config.get("BoxHeight", 1.0)],
                              "todInfluence": config.get("TODInfluence", 1.0), "on": bool(config.get("Visible", True) and config.get("OnInitially", True)),
                              "affectsThisAreaOnly": bool(config.get("AffectsThisAreaOnly")), "ignoreVisAreas": bool(config.get("IgnoreVisAreas")),
                              "indoorOnly": bool(config.get("IndoorOnly"))})
                continue
            entry = found.setdefault(cubemap, {"cubemap": cubemap, "diffuseMultiplier": config.get("DiffuseMultiplier", 1.0),
                                               "specMultiplier": config.get("SpecMultiplier", 1.0), "todInfluence": config.get("TODInfluence", 1.0),
                                               "boxes": []})
            entry["boxes"].append({"region": path.stem, "place": record["place"], "area": area, "priority": config.get("SortPriority", 0)})
    folder = out_dir() / "probes"
    folder.mkdir(parents=True, exist_ok=True)
    width, height = PROBE_SIZE
    u = (np.arange(width) + 0.5) / width
    v = (np.arange(height) + 0.5) / height
    longitude, polar = np.meshgrid((2.0 * u - 1.0) * math.pi, v * math.pi)
    direction = np.stack([np.sin(polar) * np.sin(longitude), np.sin(polar) * np.cos(longitude), np.cos(polar)], -1).reshape(-1, 3)
    for cubemap, entry in found.items():
        entry.update(probe_picture(cubemap, folder, direction))
    cubes = {}
    for cubemap in sorted({probe["cubemap"] for probe in local}):
        cubes[cubemap] = probe_picture(cubemap, folder, direction) if source.has(cubemap) else {"error": "no cubemap in the source"}
    doc = {"rule": "the outdoor ambient of the source: the radiance of the probe x 10,000 cd/m2 x its DiffuseMultiplier x the time-of-day "
                   "'Environment Probe Diffuse Multiplier' (by its TODInfluence) x 'Environment Probe Multiplier'",
           "world": list(found.values()), "local": local, "cubes": cubes}
    # One rename: the clients of all tracks read this file at their start.
    temporary = out_dir() / "probes.json.part"
    temporary.write_text(json.dumps(doc, indent=1))
    temporary.replace(out_dir() / "probes.json")
    return {"world": {Path(k).stem: {"boxes": len(v["boxes"]), "up": v.get("faceMeans", {}).get("+up")} for k, v in found.items()},
            "local": len(local), "cubes": len(cubes), "errors": {Path(k).stem: v["error"] for k, v in cubes.items() if "error" in v}}


def cube_mips(cubemap: str):
    """@return All mips of a BC6H cubemap of the source: [mip][face] RGB float arrays (the face order and layout of D3D), from the
    split files (NAME.dds.N: one mip of the six faces, the highest N is mip 0) and the tail of the base file (the smaller mips of each
    face in turn), or no value."""
    from . import bc6h
    base = source.read(cubemap)
    size = struct.unpack_from("<I", base, 16)[0]
    mips = struct.unpack_from("<I", base, 28)[0] or 1
    sizes = [max(1, size >> i) for i in range(mips)]
    face_bytes = [max(1, (s + 3) // 4) ** 2 * 16 for s in sizes]
    splits = 0
    while source.has(f"{cubemap}.{splits + 1}"):
        splits += 1
    blocks = {}   # (mip, face) -> the bytes
    for i in range(splits):
        data = source.read(f"{cubemap}.{splits - i}")
        if len(data) != 6 * face_bytes[i]:
            return None
        for f in range(6):
            blocks[i, f] = data[f * face_bytes[i]:(f + 1) * face_bytes[i]]
    # The tail: each face with its smaller mips in turn (the face means are smooth over the mips only in this order).
    tail, offset = base[148:], 0
    for f in range(6):
        for i in range(splits, mips):
            blocks[i, f] = tail[offset:offset + face_bytes[i]]
            offset += face_bytes[i]
    if offset != len(tail):
        return None
    return [[bc6h.decode(blocks[i, f], max(4, sizes[i]), max(4, sizes[i]))[:sizes[i], :sizes[i]] for f in range(6)] for i in range(mips)]


def write_cube_dds(path: Path, mips: list) -> None:
    """Writes a DDS cube of RGBA16F with its mips (DX10 header: TEXTURECUBE, array size 1; the data of each face with its mips in
    turn), which Unreal imports with the mips of the file."""
    # Unreal imports a full mip chain only: the mips below the smallest of the source (4 x 4, mip 6) are the means of 2 x 2 texels
    # (the term samples mips 0 to 6 only: lod = 6 - 6 x smoothness).
    mips = list(mips)
    while mips[-1][0].shape[0] > 1:
        mips.append([face.reshape(face.shape[0] // 2, 2, face.shape[1] // 2, 2, 3).mean((1, 3)) for face in mips[-1]])
    size, count = mips[0][0].shape[0], len(mips)
    header = bytearray(4 + 124 + 20)
    header[0:4] = b"DDS "
    struct.pack_into("<7I", header, 4, 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000, size, size, size * 8, 0, count)
    struct.pack_into("<2I4s", header, 4 + 72, 32, 0x4, b"DX10")
    struct.pack_into("<4I", header, 4 + 104, 0x8 | 0x1000 | 0x400000, 0xFE00, 0, 0)
    struct.pack_into("<5I", header, 128, 10, 3, 0x4, 1, 0)
    parts = [bytes(header)]
    for face in range(6):
        for mip in mips:
            rgb = mip[face].astype(np.float32)
            rgba = np.concatenate([rgb, np.ones(rgb.shape[:2] + (1,), np.float32)], -1)
            parts.append(rgba.astype(np.float16).tobytes())
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_bytes(b"".join(parts))
    temporary.replace(path)


BRDF_TABLE = "engineassets/shading/environmentbrdf.dds"


def brdf_table():
    """@return The environment BRDF table of the source (research probe-flags.md section 3): 128 x 128, the columns N.V, the rows the
    smoothness (row 0: 0), R and G linear (the file is BC1 sRGB: the sRGB view of its binder decodes it), or no value. Mip 0 is the
    highest split part (NAME.dds.N)."""
    from PIL import Image
    if not source.has(BRDF_TABLE):
        return None
    header = bytearray(source.read(BRDF_TABLE)[:148])
    parts = 0
    while source.has(f"{BRDF_TABLE}.{parts + 1}"):
        parts += 1
    if parts == 0:
        return None
    size = struct.unpack_from("<I", header, 16)[0]
    struct.pack_into("<I", header, 28, 1)    # one mip
    struct.pack_into("<I", header, 128, 71)  # BC1 UNORM: Pillow decodes the stored codes; the sRGB transfer below
    picture = Image.open(io.BytesIO(bytes(header) + source.read(f"{BRDF_TABLE}.{parts}"))).convert("RGB")
    codes = np.asarray(picture).astype(np.float64) / 255.0
    if codes.shape[:2] != (size, size):
        return None
    return np.where(codes <= 0.04045, codes / 12.92, ((codes + 0.055) / 1.055) ** 2.4)


def write_dds_2d(path: Path, rgb: np.ndarray) -> None:
    """Writes a DDS 2D texture of RGBA16F with one mip (DX10 header), which Unreal imports as linear float data."""
    height, width = rgb.shape[:2]
    header = bytearray(4 + 124 + 20)
    header[0:4] = b"DDS "
    struct.pack_into("<7I", header, 4, 124, 0x1 | 0x2 | 0x4 | 0x1000, height, width, width * 8, 0, 1)
    struct.pack_into("<2I4s", header, 4 + 72, 32, 0x4, b"DX10")
    struct.pack_into("<4I", header, 4 + 104, 0x1000, 0, 0, 0)
    struct.pack_into("<5I", header, 128, 10, 3, 0, 1, 0)
    rgba = np.concatenate([rgb.astype(np.float32), np.ones(rgb.shape[:2] + (1,), np.float32)], -1)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_bytes(bytes(header) + rgba.astype(np.float16).tobytes())
    temporary.replace(path)


def probe_specular() -> dict:
    """Writes probes/spec/NAME.dds (each world and local probe cube with its pre-filtered mips of the source: the specular term of
    the probes for each pixel, research weapon-materials.md section 2) and probes-spec.json (NAME -> the file, the cubemap, the face
    size, the mips) with the environment BRDF table of the source (probes/spec/environment_brdf.dds, "brdf"). The command probes-spec,
    after probes."""
    doc = json.loads((out_dir() / "probes.json").read_text())
    cubemaps = [w["cubemap"] for w in doc["world"]] + [c for c in doc.get("cubes", {}) if c]
    records, failed = {}, {}
    for cubemap in sorted(set(cubemaps)):
        name = probe_name(cubemap)
        target = out_dir() / "probes" / "spec" / f"{name}.dds"
        try:
            mips = cube_mips(cubemap)
        except Exception as error:
            failed[cubemap] = str(error)[:120]
            continue
        if mips is None:
            failed[cubemap] = "the mips do not read"
            continue
        write_cube_dds(target, mips)
        records[name] = {"dds": str(target.relative_to(out_dir())), "cubemap": cubemap, "faceSize": int(mips[0][0].shape[0]),
                         "mips": len(mips)}
    doc = {"format": 1, "cubes": records}
    table = brdf_table()
    if table is not None:
        target = out_dir() / "probes" / "spec" / "environment_brdf.dds"
        write_dds_2d(target, table)
        doc["brdf"] = {"dds": str(target.relative_to(out_dir())), "source": BRDF_TABLE, "size": list(table.shape[:2]),
                       "rule": "(R, G) at (N.V, smoothness); F = R x (1 - F0) + G x F0; linear"}
    else:
        failed[BRDF_TABLE] = "the table does not read"
    (out_dir() / "probes-spec.json").write_text(json.dumps(doc, indent=1, sort_keys=True))
    return {"cubes": len(records), "brdf": table is not None, "failed": failed}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-environment", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("write")
    p.add_argument("--regions", nargs="*")
    sub.add_parser("cookies")
    sub.add_parser("sky")
    sub.add_parser("snow")
    sub.add_parser("probes")
    sub.add_parser("probes-spec")
    p = sub.add_parser("volumes")
    p.add_argument("x", type=float)
    p.add_argument("y", type=float)
    p.add_argument("z", type=float)
    p = sub.add_parser("table")
    p.add_argument("preset")
    p.add_argument("out")
    p = sub.add_parser("sample")
    p.add_argument("preset")
    p.add_argument("hour", type=float)
    p.add_argument("names", nargs="*")
    args = parser.parse_args(argv)
    if args.command == "write":
        print(json.dumps(write(args.regions), indent=1))
    elif args.command == "cookies":
        print(json.dumps(cookies(), indent=1))
    elif args.command == "sky":
        print(json.dumps(sky(), indent=1))
    elif args.command == "snow":
        print(json.dumps(snow(), indent=1))
    elif args.command == "probes":
        print(json.dumps(probes(), indent=1))
    elif args.command == "probes-spec":
        print(json.dumps(probe_specular(), indent=1))
    elif args.command == "volumes":
        print(json.dumps(time_volumes_at((args.x, args.y, args.z)), indent=1))
    elif args.command == "table":
        doc = json.loads((out_dir() / "presets.json").read_text())
        print(json.dumps(write_table(doc[preset_path(args.preset)], Path(args.out))))
    elif args.command == "sample":
        doc = json.loads((out_dir() / "presets.json").read_text())
        values = sample(doc[preset_path(args.preset)], args.hour)
        print(json.dumps({k: v for k, v in values.items() if not args.names or k in args.names}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
