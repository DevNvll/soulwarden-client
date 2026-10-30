"""The data of the world map (stage MVP-C, Linear AST-213): the territories, the areas, the points of interest, their shapes,
and the landmarks of the world. The native map component of the game (MapComponentBus, TerritoryDefinitionsDataManagerBus)
reads the same sources; the UI runtime (swc/worldmap.lua) gives this data to the original map scripts.

Sources (the game copy, read only):
- The definitions: the data tables javelindata_territorydefinitions (the territories) and
  pointofinterestdefinitions/javelindata_poidefinitions_*.datasheet (the areas and the points of interest). Each column goes
  to the name of the native class: the lower camel case of the column ("NameLocalizationKey" -> "nameLocalizationKey").
- The shapes: the master slice of the territories (tracts.json "territoryMasterSlicePath") names the slices with the
  shapes: zones/territorybounds, zones/areabounds_*, and territories/r_*_pois. Each entity of these slices has a world
  transform, a polygon prism (the vertices in the plane of the entity, and a height), and the id of its territory (the
  component field 744121785, a text). The converter keeps the polygon in world meters (x, y) and its box.
- The landmarks: TerritoryLandmarks of regions/REGION/region.metadata (the source of the travel places of the backend:
  ~/Developer/azoth/tools/gamedata/travel_places.py): the type (eTerritoryLandmarkType), the ids of the territories that
  contain it (territory, area, point of interest), and the world position.

Output: SWC_CONTENT/ui/gamedata/worldmap.json
  {"format": 1, "definitions": {ID: {column: value}}, "shapes": [{"id", "name", "position", "polygon", "min", "max",
   "height"}], "landmarks": [{"type", "ids", "position"}]}
"""
from __future__ import annotations

import json
import math
import re
import struct

import numpy as np

from . import datasheet, env, source
from .source import field_id as F

WORLD = "sharedassets/coatlicue/newworld_vitaeeterna/"
TERRITORY_TABLE = "javelindata_territorydefinitions.datasheet"
# The areas (the "area" kind of the territory definitions; ut567: the faction hunt cards name an area by its POI tag, as
# FM_S_Hunt_0605 "southwindsward5" = 9015 "@ui_area_southwindsward_5"; before, only the territories and the points of interest).
AREA_TABLE = "javelindata_areadefinitions.datasheet"
POI_TABLES = "sharedassets/springboardentitites/datatables/pointofinterestdefinitions/"
# The territory id of a shape entity (the component of the territory; the field name is not in the name table).
TERRITORY_ID_FIELD = 744121785
POLYGON_PRISM = F("PolygonPrism")


def camel(column: str) -> str:
    """@return The name of a column in the native class: "NameLocalizationKey" -> "nameLocalizationKey", "IsPOI" -> "isPOI"."""
    return column[:1].lower() + column[1:]


def definitions() -> dict[int, dict]:
    """@return Each territory, area, and point of interest by id, with the non-empty columns of its row."""
    out: dict[int, dict] = {}
    tables = [datasheet.load(TERRITORY_TABLE), datasheet.load(AREA_TABLE)]
    tables += [datasheet.Sheet(source.read(path)) for path in sorted(source.paks().paths(POI_TABLES, ".datasheet"))
               if "devworld" not in path]
    for sheet in tables:
        for row in sheet.rows():
            ident = row.get("TerritoryID")
            if not isinstance(ident, float):
                continue
            entry = {}
            for column, value in row.items():
                if value == "" or column == "TerritoryID":
                    continue
                entry[camel(column)] = value
            out[int(ident)] = entry
    return out


def slice_territory_ids(path: str) -> list[int]:
    """@return The territory ids of the TerritoryDataProviderComponent fields "Territory Id" (a text) of a slice, in their order."""
    ids = []
    for top in source.parse(source.read(path)):
        for node in source.walk(top):
            if node.name == TERRITORY_ID_FIELD and node.value:
                text = source.text(node)
                if text.isdigit():
                    ids.append(int(text))
    return ids


def encounter_titles(recipes: list[dict], territory_ids, names: dict[int, str]) -> dict[str, dict]:
    """The head title of the tracker row of each encounter recipe (E2b, ut567; static read work/ut567/qgplan/E2b/ANSWERS.md): the
    objective of the root takes the NameLocalizationKey of the territory definition of the "Territory Id" of its
    TerritoryDataProviderComponent (0x3A575E0, 0x3A2E590, the lookup 0x44D1C60); with no id or no row the title is empty (no fallback:
    not the stage title, not the success event). territory_ids(slice) gives the ids of a slice; names: territory id -> name key.
    @return The upper-case slice asset of each recipe -> {"territory": ID or 0, "title": KEY or ""}."""
    out: dict[str, dict] = {}
    for recipe in recipes:
        ids = territory_ids(recipe["slice"])
        ident = ids[0] if ids else 0
        out[str(recipe["asset"]).upper()] = {"territory": ident, "title": str(names.get(ident, "")) if ident else ""}
    return out


def definition_names() -> dict[int, str]:
    """@return The NameLocalizationKey of each row of each table of the type TerritoryDefinition (the territories, areas, points of
    interest, darkness, defend objects, arenas, invasions, trigger volume POIs; not devworld) by id: the rows of the native territory
    definition manager (the head titles of the encounters, E2b; for example 8102 of javelindata_defendobjectdefinitions)."""
    names: dict[int, str] = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")
                       and "devworld" not in p):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except (ValueError, struct.error):
            continue
        if sheet.type != "TerritoryDefinition":
            continue
        for row in sheet.rows():
            ident = row.get("TerritoryID")
            if isinstance(ident, float) and row.get("NameLocalizationKey"):
                names.setdefault(int(ident), str(row["NameLocalizationKey"]))
    return names


def az_matrix(value: bytes) -> np.ndarray:
    v = struct.unpack(">12f", value)
    m = np.eye(4)
    m[:3, 0], m[:3, 1], m[:3, 2], m[:3, 3] = v[0:3], v[3:6], v[6:9], v[9:12]
    return m


def shape_slices() -> list[str]:
    tracts = json.loads(source.read(WORLD + "tracts.json"))
    master = tracts["territoryMasterSlicePath"].lower()
    data = source.read(master)
    return sorted({m.decode().lower() for m in re.findall(rb"slices/pois/[a-z0-9_/+\-]+\.dynamicslice", data)})


def shapes() -> list[dict]:
    """@return Each shape entity of the slices of the master slice: its territory id, its polygon, and its box."""
    out = []
    for path in shape_slices():
        for root in source.parse(source.read(path)):
            for node in source.walk(root):
                if node.name != F("Entities"):
                    continue
                for entity in node.children:
                    shape = entity_shape(entity)
                    if shape is not None:
                        shape["slice"] = path.rsplit("/", 1)[-1].removesuffix(".dynamicslice")
                        out.append(shape)
    return out


def entity_shape(entity) -> dict | None:
    fields = {c.name: c for c in entity.children}
    name = source.text(fields[F("Name")]) if F("Name") in fields else ""
    ident, world, vertices, height = None, None, None, 0.0
    for node in source.walk(entity):
        if node.name == TERRITORY_ID_FIELD and node.value:
            text = source.text(node)
            if text.isdigit():
                ident = int(text)
        elif node.name == F("Transform") and node.value and len(node.value) == 48 and world is None:
            world = az_matrix(node.value)
        elif node.name == POLYGON_PRISM:
            for part in source.walk(node):
                if part.name == F("Height") and part.value and len(part.value) == 4:
                    height = struct.unpack(">f", part.value)[0]
                elif part.name == F("vertices"):
                    vertices = [struct.unpack(">2f", v.value) for v in part.children if v.value and len(v.value) == 8]
    if ident is None or world is None or not vertices:
        return None
    local = np.array([[x, y, 0.0, 1.0] for x, y in vertices])
    points = (world @ local.T).T[:, :2]
    if not np.all(np.isfinite(points)):
        return None
    return {"id": ident, "name": name, "position": [round(float(v), 2) for v in world[:3, 3]],
            "polygon": [[round(float(x), 2), round(float(y), 2)] for x, y in points],
            "min": [round(float(v), 2) for v in points.min(axis=0)], "max": [round(float(v), 2) for v in points.max(axis=0)],
            "height": round(float(height), 2)}


def landmarks() -> list[dict]:
    out = []
    seen = set()
    for path in source.paks().paths(WORLD + "regions/", "/region.metadata"):
        if not re.search(r"/r_\+\d\d_\+\d\d/region\.metadata$", path):
            continue
        for root in source.parse(source.read(path)):
            for node in source.walk(root):
                if node.name != F("TerritoryLandmarks"):
                    continue
                for element in node.children:
                    fields = {c.name: c for c in element.children}
                    kind, position = fields.get(F("landmarkType")), fields.get(F("WorldPosition"))
                    if kind is None or not kind.value or position is None or not position.value or len(position.value) != 12:
                        continue
                    place = struct.unpack(">3f", position.value)
                    if not all(math.isfinite(v) for v in place):
                        continue
                    ids = fields.get(F("TerritoryIds"))
                    entry = {"type": int.from_bytes(kind.value, "big"),
                             "ids": [int.from_bytes(c.value, "big") for c in (ids.children if ids is not None else []) if c.value],
                             "position": [round(v, 3) for v in place]}
                    # The actor id of the landmark (16 bytes; the id of the map destination records, MissionDestinationData: 32
                    # uppercase hex digits in byte order; static read ut567/qgplan/mapsettle).
                    actor = fields.get(F("ActorId"))
                    if actor is not None and actor.value and len(actor.value) == 16:
                        entry["actor"] = actor.value.hex().upper()
                    # The data text of the landmark (LandmarkData: the station and its tier of a crafting station "Blacksmith5", the
                    # NPC id of a person, the fishing hotspot id, the tower id; collapsiblesettlementicon.luac takes the station type of
                    # it); an empty text is left out.
                    text = fields.get(F("LandmarkData"))
                    if text is not None and text.value:
                        entry["data"] = text.value.decode("utf-8", "replace").rstrip("\x00")
                    key = (entry["type"], tuple(entry["position"]))
                    # A region file has a landmark two times when two layers place it.
                    if key in seen:
                        continue
                    seen.add(key)
                    out.append(entry)
    return out


def convert() -> dict:
    target = env.content_dir() / "ui" / "gamedata" / "worldmap.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    defs = definitions()
    shape_list = shapes()
    marks = landmarks()
    doc = {"format": 1, "definitions": {str(k): v for k, v in sorted(defs.items())}, "shapes": shape_list, "landmarks": marks}
    target.write_text(json.dumps(doc, separators=(",", ":")))
    kinds: dict[int, int] = {}
    for mark in marks:
        kinds[mark["type"]] = kinds.get(mark["type"], 0) + 1
    without = sorted({s["id"] for s in shape_list} - set(defs))
    return {"definitions": len(defs), "territories": sum(1 for d in defs.values() if d.get("isTerritory") is True),
            "shapes": len(shape_list), "shape slices": len(shape_slices()), "shapes without a definition": without[:20],
            "landmarks": len(marks), "landmark types": dict(sorted(kinds.items())), "file": str(target)}
