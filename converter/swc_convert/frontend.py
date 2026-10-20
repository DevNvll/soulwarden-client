"""The front end of the client (docs/FRONTEND.md, stage F4): the menu scene NightHaven and the menu clip.

Commands:
  clips   the menu idle clip of the Player Entity (SimpleAnimationComponent: unarmed_idle_loadingscreen, layer 0, loop, speed 1)
          in one clip pack for each gender on the player skeleton (the manifest kind "clips", tag "menu"); then
          tools/import-player.sh. Both genders use the clip of the male folder (player_shared.chrparams).
  scene   SWC_CONTENT/frontend/nighthaven.json from levels/nw_frontend_nighthaven/level.pak (mission0.entities_xml): the Player
          Entity, MainCamera with its parent chain, the camera sequences (the Bezier keys of each scalar track), and the
          time-of-day record. Positions are game metres; the client applies the space map.

The study of the native rules is research/frontend-native.md (FE1, FE7): the scheduled menu level NightHaven, the camera
FOV 55 (vertical, an inference), the cubic Bezier curves of the tracks in time-value space, and the start rules of the
sequences (no auto start: all six scene records have Flags 0).
"""
from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path

from . import env, player
from .environment import read_from_archive

LEVEL_PAK = "levels/nw_frontend_nighthaven/level.pak"
ENTITIES = "mission0.entities_xml"
MENU_CLIP = "animations/gameplay/character/player/male/unarmed/navigation/idle/unarmed_idle_loadingscreen.caf"
MENU_CLIP_NAME = "unarmed_idle_loadingscreen"
# The entities of the scene that the client reads by name.
# The radius of the bounded scene subset around the Player Entity (m): the camp, its props, and the near trees and walls.
SCENE_RADIUS = 60.0
# Meshes that the subset leaves out: the fog volumes (their volumetric materials), the meshes of particle effects, and the sky
# dome (the sky of the client is its environment).
SKIPPED = ("objects/volumetrics/", "objects/vfx/", "objects/sky/")
# The lights of the subset: within this radius of the Player Entity (m).
LIGHT_RADIUS = 40.0
# The ground of the menu: the Coatlicue world of the menu (MainMenuCoatOverride coatlicue/NW_Frontend_Base of the schedule row,
# research frontend-native.md FE1): a square of its heights around the Player Entity, 1 m apart.
GROUND_WORLD = "sharedassets/coatlicue/nw_frontend_base/regions/"
GROUND_MACRO = "materials/terrain/nw_frontend_base/nw_frontend_base_{rx}_{ry}/nw_frontend_base_{rx}.{ry}.macro_diff.dds"
GROUND_HALF = 60
# The light fields of LightComponent that the client reads.
LIGHT_FIELDS = ("LightType", "Visible", "OnInitially", "Color", "DiffuseMultiplier", "SpecMultiplier", "PointMaxDistance",
                "PointAttenuationBulbSize", "ProjectorDistance", "ProjectorAttenuationBulbSize", "ProjectorFOV", "AreaWidth",
                "AreaHeight", "AreaMaxDistance", "CastShadowsSpec", "VolumetricFog")
NAMED = ("Player Entity", "MainCamera", "CharacterCameraPlacement", "EstablishingCameraPlacement", "Main_layer", "TOD")
# The CAnimParamType of the compound tracks: Position (1) and Rotation (2); the sub tracks are X, Y, Z.
TRACK_NAMES = {"Position": "P", "Rotation": "R"}
AXES = "XYZ"


def out_dir() -> Path:
    return env.content_dir() / "frontend"


# ------------------------------------------------------------------------------------------------------------- the clips

def clips(force: bool = False) -> dict:
    """The clip pack "menu" of each gender: the menu idle clip on the player skeleton."""
    from . import gltf
    manifest = player.load_manifest()
    report = {}
    for gender, folder in player.GENDERS.items():
        key = f"clips|{folder}|menu"
        path = player.player_dir() / "clips" / folder / "menu" / "clips.gltf"
        known = manifest["assets"].get(key, {}).get("clips", [])
        if path.is_file() and not force and known == [MENU_CLIP]:
            report[gender] = "exists"
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        skeleton = player.Skeleton(gender)
        w = gltf.Writer()
        first, roots = skeleton.write(w)
        w.doc["scenes"][0]["nodes"] += roots
        # The SimpleAnimationComponent of the Player Entity has animation-driven motion off: the root bone stays (root lock).
        skeleton.add_clip(w, first, MENU_CLIP, MENU_CLIP_NAME, root_lock=True)
        w.save(path)
        manifest["assets"][key] = {"kind": "clips", "gender": gender, "tags": ["menu"],
                                   "file": str(path.relative_to(env.content_dir())),
                                   "unreal": f"{player.UNREAL_ROOT}/{gender}/Clips/menu", "clips": [MENU_CLIP]}
        report[gender] = str(path)
    player.save_manifest(manifest)
    return report


# ------------------------------------------------------------------------------------------------------------- the scene

def _field(element, name: str):
    return next((c for c in element if c.get("field") == name), None)


def _value(element, name: str, default: str = "") -> str:
    node = _field(element, name) if element is not None else None
    return node.get("value", default) if node is not None else default


def _floats(text: str) -> list[float]:
    return [float(v) for v in text.split()] if text else []


def _vec2(element) -> list[float]:
    return [float(_value(element, "x", "0")), float(_value(element, "y", "0"))]


def _transform(components) -> dict:
    """@return The world and local matrices of an entity (12 floats each: the 3x3 rotation by rows, then the translation),
    and the parent entity id."""
    out = {}
    for component in components:
        if component.get("name") not in ("TransformComponent", "GameTransformComponent"):
            continue
        for node in component.iter():
            field = node.get("field")
            if field in ("Transform", "m_worldTM") and node.get("value") and "world" not in out:
                out["world"] = _floats(node.get("value"))
            elif field in ("LocalTransform", "m_localTM") and node.get("value"):
                out["local"] = _floats(node.get("value"))
            elif field == "Parent" and node.get("name") == "EntityId":
                ident = next((n.get("value") for n in node.iter() if n.get("field") == "id"), None)
                out["parent"] = ident
    return out


def _camera(components) -> dict | None:
    for component in components:
        if component.get("name") != "CameraComponent":
            continue
        values = {n.get("field"): n.get("value") for n in component.iter() if n.get("value") is not None}
        return {"fov": float(values.get("Field of View", "0")), "near": float(values.get("Near Clip Plane Distance", "0")),
                "far": float(values.get("Far Clip Plane Distance", "0")),
                "enableOnActivate": values.get("EnableOnActivate") == "true"}
    return None


def _sequence(component) -> dict | None:
    """@return A sequence: its name, flags, time range, and the Bezier keys of each scalar track of each node (by node name:
    P X, P Y, P Z, R X, R Y, R Z)."""
    seq = next((n for n in component.iter() if n.get("name") == "CAnimSequence"), None)
    if seq is None:
        return None
    rng = _field(seq, "TimeRange")
    out = {"name": _value(seq, "Name"), "flags": int(_value(seq, "Flags", "0")),
           "start": float(_value(rng, "Start", "0")), "end": float(_value(rng, "End", "0")), "nodes": {}}
    nodes = _field(seq, "Nodes")
    # The nodes: an entity node (its name: the entity, MainCamera or Player Entity) and its component nodes (Parent: the ID of
    # the entity node; the Transform component node has the tracks). The tracks go to the name of the entity.
    names = {}
    anim_nodes = [n for n in (nodes.iter() if nodes is not None else [])
                  if n.get("name") in ("CAnimAzEntityNode", "CAnimComponentNode", "CAnimEntityNode") and n.get("field") == "element"]
    for node in anim_nodes:
        base = _field(node, "BaseClass1")
        names[_value(base, "ID")] = (_value(base, "Name"), _value(base, "Parent", "0"))
    for node in anim_nodes:
        base = _field(node, "BaseClass1")
        name, parent = names.get(_value(base, "ID"), ("", "0"))
        if parent not in ("", "0") and parent in names:
            name = names[parent][0]
        tracks = {}
        for track in node.iter():
            if track.get("name") != "CCompoundSplineTrack":
                continue
            param = _field(track, "ParamType")
            kind = TRACK_NAMES.get(_value(param, "Name"))
            subs = _field(track, "SubTracks")
            if kind is None or subs is None:
                continue
            for index, sub in enumerate(n for n in subs.iter() if n.get("name", "").startswith("TAnimSplineTrack")):
                keys = []
                for key in sub.iter():
                    if key.get("name") != "spline::SplineKey<Vec2>":
                        continue
                    keys.append({"time": float(_value(key, "time", "0")), "flags": int(_value(key, "flags", "0")),
                                 "value": _vec2(_field(key, "value")), "ds": _vec2(_field(key, "ds")), "dd": _vec2(_field(key, "dd"))})
                tracks[f"{kind}{AXES[index]}"] = {"default": _vec2(_field(sub, "DefaultValue"))[1], "keys": keys}
        if tracks:
            out["nodes"].setdefault(name, {}).update(tracks)
    return out


def _mesh(components) -> tuple[str, str] | None:
    """@return The static mesh asset id (guid text, sub id) and the material override path of a visible mesh component."""
    for component in components:
        if component.get("name") != "MeshComponent":
            continue
        node = next((n for n in component.iter() if n.get("field") == "Static Mesh Render Node"), None)
        if node is None or _value(node, "Visible", "true") == "false":
            return None
        asset = next((n.get("value") for n in node.iter() if n.get("field") == "Static Mesh" and n.get("value")), None)
        material = next((n.get("value") for n in node.iter() if n.get("field") == "AssetPath" and n.get("value")), "")
        if asset:
            return asset, material
    return None


def _cgf(asset: str) -> str | None:
    """@return The .cgf of an asset reference "id={GUID}:SUB,type=...", or None."""
    from . import source
    head = asset.split(",")[0]
    if not head.startswith("id={"):
        return None
    guid, _, sub = head[4:].partition("}:")
    path = source.product(source.guid_bytes(guid), int(sub or "0", 16) if sub and not sub.isdigit() else int(sub or "0"), ".cgf")
    return path


def _light(components) -> dict | None:
    for component in components:
        if component.get("name") != "LightComponent":
            continue
        values = {n.get("field"): n.get("value") for n in component.iter() if n.get("value") is not None}
        return {k: values[k] for k in LIGHT_FIELDS if k in values}
    return None


def ground(player: list[float]) -> dict:
    """@return The heights of the menu world in a square of (2 GROUND_HALF + 1)^2 points 1 m apart about the Player Entity (m;
    row 0 = the smallest y), and the mean color of the macro map of the region under it (sRGB 0..1)."""
    import numpy as np
    from nav_terrain import HEIGHT_SCALE, read_heightmap
    from . import dds, source
    cache: dict = {}

    def samples(rx: int, ry: int):
        key = (rx, ry)
        if key not in cache:
            path = f"{GROUND_WORLD}r_+{rx:02d}_+{ry:02d}/region.heightmap"
            cache[key] = (np.frombuffer(read_heightmap(source.read(path)), dtype=np.uint16).reshape(2048, 2048)
                          if source.has(path) else None)
        return cache[key]

    def height(x: float, y: float) -> float:
        rx, ry = int(x // 2048), int(y // 2048)
        h = samples(rx, ry)
        if h is None:
            return float("nan")
        lx, ly = min(max(x - rx * 2048, 0.0), 2047.0), min(max(y - ry * 2048, 0.0), 2047.0)
        x0, y0 = int(lx), int(ly)
        x1, y1 = min(x0 + 1, 2047), min(y0 + 1, 2047)
        fx, fy = lx - x0, ly - y0
        v = (h[y0, x0] * (1 - fx) * (1 - fy) + h[y0, x1] * fx * (1 - fy) + h[y1, x0] * (1 - fx) * fy + h[y1, x1] * fx * fy)
        return float(v) * HEIGHT_SCALE

    ox, oy = round(player[0]) - GROUND_HALF, round(player[1]) - GROUND_HALF
    size = 2 * GROUND_HALF + 1
    heights = [round(height(ox + i, oy + j), 3) for j in range(size) for i in range(size)]
    color = [0.5, 0.5, 0.5]
    rx, ry = int(player[0] // 2048), int(player[1] // 2048)
    base = GROUND_MACRO.format(rx=rx, ry=ry)
    if source.has(base):
        data, w, h, fmt = dds.join(source.read, base, 512)
        image = np.asarray(dds.to_image(data, w, h, fmt).convert("RGB"), dtype=np.float64) / 255.0
        # The pixels of the square (row 0 = north, the rule of the landscape maps).
        u0 = int((ox - rx * 2048) / 2048 * w)
        u1 = int((ox + size - rx * 2048) / 2048 * w) + 1
        v0 = int((2048 - (oy + size - ry * 2048)) / 2048 * h)
        v1 = int((2048 - (oy - ry * 2048)) / 2048 * h) + 1
        patch = image[max(v0, 0):max(v1, 1), max(u0, 0):max(u1, 1)]
        if patch.size:
            color = [round(float(c), 4) for c in patch.reshape(-1, 3).mean(axis=0)]
    return {"origin": [ox, oy], "step": 1.0, "size": size, "heights": heights, "color": color, "macro": base,
            "player height": round(height(player[0], player[1]), 3)}


def environment(tod: dict | None, mission: bytes | None = None) -> dict:
    """@return The environment of the menu scene for the menu mode of the client environment (docs/ENVIRONMENT.md): the preset of
    the TOD entity (the key of presets.json, environment.preset_path), its hour (the hour override, else the hour of the menu
    mission), its sky slice (the hint of SliceAsset: the cloud cover of sky.json), and the Lighting and Moon elements of the menu
    mission (its sun path is not the one of the world mission: SunRotation 190 and Longitude 110, not 215 and 130)."""
    from .environment import preset_path
    root = ET.fromstring(mission if mission is not None else read_from_archive(LEVEL_PAK, "mission_mission0.xml"))
    elements = {e.tag: dict(e.attrib) for e in (root.find("Environment") if root.find("Environment") is not None else [])}
    mission_tod = root.find("TimeOfDay")
    hour = float(mission_tod.get("Time", "12")) if mission_tod is not None else 12.0
    tod = tod or {}
    if tod.get("HasTimeOfDayOverride") == "true" and tod.get("TimeOfDayOverride"):
        hour = float(tod["TimeOfDayOverride"])
    slice_hint = (tod.get("SliceAsset") or "").partition("hint={")[2].rstrip("}")
    lighting, moon = elements.get("Lighting", {}), elements.get("Moon", {})
    number = lambda values, key, default: float(values.get(key, default))
    return {"preset": preset_path(tod.get("FileName", "")) if tod.get("FileName") else "", "hour": hour, "sky": slice_hint,
            "sunRotation": number(lighting, "SunRotation", 215.0), "longitude": number(lighting, "Longitude", 130.0),
            "moonLatitude": number(moon, "Latitude", 240.0), "moonLongitude": number(moon, "Longitude", 35.0),
            "moonSize": number(moon, "Size", 0.8), "source": f"{LEVEL_PAK}:mission_mission0.xml"}


def scene(radius: float = SCENE_RADIUS) -> dict:
    """Writes SWC_CONTENT/frontend/nighthaven.json."""
    import math
    data = read_from_archive(LEVEL_PAK, ENTITIES)
    entities, sequences, ids = {}, {}, {}
    tod = None
    meshes, lights = [], []
    for _, element in ET.iterparse(__import__("io").BytesIO(data), events=("end",)):
        if element.get("name") != "AZ::Entity":
            continue
        components = _field(element, "Components")
        name = _value(element, "Name")
        ident = next((n.get("value") for n in (_field(element, "Id") or []) if n.get("field") == "id"), None)
        if components is not None:
            if name in NAMED:
                entities[name] = {"id": ident, **_transform(components)}
                camera = _camera(components)
                if camera:
                    entities[name]["camera"] = camera
            if ident:
                ids[ident] = name
            light = _light(components)
            if light is not None:
                world = _transform(components).get("world")
                if world and len(world) == 12:
                    lights.append({"name": name, "world": world, **light})
            mesh = _mesh(components)
            if mesh is not None:
                world = _transform(components).get("world")
                if world and len(world) == 12:
                    meshes.append({"asset": mesh[0], "material": mesh[1], "world": world})
            for component in components:
                if component.get("name") == "SequenceComponent":
                    seq = _sequence(component)
                    if seq:
                        sequences[seq["name"]] = seq
                elif component.get("name") == "TimeOfDayPOIComponent":
                    values = {n.get("field"): n.get("value") for n in component.iter() if n.get("value") is not None}
                    tod = {k: values.get(k) for k in ("FileName", "Priority", "BlendTime", "BlendDistance", "OverrideHour",
                                                      "Hour", "HasTimeOfDayOverride", "TimeOfDayOverride", "SliceAsset") if k in values}
        # The nested entities of a slice are inside this one: keep nothing of it after the read.
        for child in list(element):
            element.remove(child)
    for record in entities.values():
        if record.get("parent"):
            record["parentName"] = ids.get(record["parent"], "")
    # The bounded subset of the scene (docs/FRONTEND.md F4): the static meshes within `radius` of the Player Entity, with the
    # folder of the content manifest when the mesh is converted (else it goes to the wanted list of the meshes) and the object
    # path of the imported asset (the import shortens a long name and changes some characters: objects.imported_mesh).
    import numpy as np
    from . import manifest, space
    from .objects import imported_mesh
    assets: dict[str, str] = {}
    known = manifest.load()["assets"]
    player = entities.get("Player Entity", {}).get("world", [0.0] * 12)[9:12]
    placements, wanted, skipped = [], set(), 0
    for item in meshes:
        position = item["world"][9:12]
        if math.dist(position, player) > radius:
            continue
        path = _cgf(item["asset"])
        if not path:
            continue
        if path.startswith(SKIPPED):
            skipped += 1
            continue
        entry = known.get(path)
        if entry is None:
            wanted.add(path)
        # The matrix of the scene: the three axis columns, then the translation (game metres); the Unreal transform by the space
        # map of the world placements (space.unreal_transform: S L S and 100 S t).
        w = item["world"]
        matrix = np.eye(4)
        matrix[:3, 0], matrix[:3, 1], matrix[:3, 2], matrix[:3, 3] = w[0:3], w[3:6], w[6:9], w[9:12]
        transform = space.unreal_transform(matrix)
        folder = entry["unreal"] if entry else ""
        if folder and folder not in assets:
            assets[folder] = imported_mesh(folder)
        placements.append({"mesh": path, "unreal": folder, "asset": assets.get(folder, ""),
                           "stem": Path(entry["file"]).stem if entry else "", "material": item["material"],
                           "location": transform["location"], "rotation": transform["rotation"], "scale": transform["scale"]})
    doc = {"format": 1, "source": f"{LEVEL_PAK}:{ENTITIES}", "entities": entities, "sequences": sequences, "tod": tod,
           "environment": environment(tod),
           "menuClip": {"path": MENU_CLIP, "name": MENU_CLIP_NAME}, "radius": radius, "meshes": placements,
           "skipped": skipped, "wanted": sorted(wanted), "notImported": sorted(f for f, a in assets.items() if not a),
           "lights": [l for l in lights if math.dist(l["world"][9:12], player) <= LIGHT_RADIUS],
           "ground": ground(player)}
    out_dir().mkdir(parents=True, exist_ok=True)
    path = out_dir() / "nighthaven.json"
    path.write_text(json.dumps(doc, indent=1))
    return {"file": str(path), "entities": sorted(entities), "sequences": {k: [v["start"], v["end"], sorted(v["nodes"])]
                                                                            for k, v in sequences.items()}, "tod": tod,
            "environment": doc["environment"],
            "meshes": len(placements), "unique": len({p["mesh"] for p in placements}), "skipped": skipped, "wanted": len(wanted),
            "lights": len(doc["lights"]), "ground": {k: v for k, v in doc["ground"].items() if k != "heights"}}


def main(argv=None) -> int:
    import argparse
    parser = argparse.ArgumentParser(prog="python3 -B -m swc_convert.frontend")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("clips")
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("scene")
    p.add_argument("--radius", type=float, default=SCENE_RADIUS)
    args = parser.parse_args(argv)
    result = clips(args.force) if args.command == "clips" else scene(args.radius)
    print(json.dumps(result, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
