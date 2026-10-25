"""The destructible quest objects for the client (stage Q9, Linear AST-244; Soulwarden docs/systems/quests.md "The destructible quest
objects" of stage 1.8e).

A destructible quest object is a capital placement whose slice has the variation table Gatherable_Quest_Damageable: the four corruption
nodules of 06_survivors, the monolith of 06_demonstorm2, 169 placements in this build. The client makes the object from its level data;
the server finds each hit itself (the slice has no HitVolume: the client reports no contact), and the LootTracker entry of the object
(type 0, Looted) destroys it for the character until its respawn.

Input: the backend copy gamedata/quest-objects.json (the objects: key, actor, variant, tag, onlyOnQuest, position, slice; the rows), the
capital files of the level data (the rotation of each placement), the variation table Gatherable_Quest_Damageable (the visual slices of a
variant, its marker values, its effects), and the GatherableData rows (the display name).
Output: SWC_CONTENT/world-objects/quest-objects.json (tools/convert-objects.sh quest-objects):
  "objects": [{"key", "actor" (hex), "tag", "onlyOnQuest", "variant", "row", "name" (text key), "place" (engine m), "rotation" (engine
      quaternion x y z w), "slices": {"placed", "healthy", "notOnQuest", "base", "depleted"} (slice paths, "" for none),
      "marker": {"type", "override", "forceHealthBar", "verticalOffset" (cm), "radius"}, "effects": {...}}];
  "slices": {PATH: [parts]} the render parts of each slice (server_visuals.slice_parts: mesh, collision, slots, decal, transforms);
  "missing": the meshes of the slices that the manifest has and the plugin does not have (an import with the engine);
  "unconverted": the meshes of the slices that the manifest does not have (a conversion, then an import).
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

from . import creature, datasheet, env, manifest, objects, overrides, region, server_visuals, source
from .creature import gamedata_dir

FORMAT = 1
TABLE = "Gatherable_Quest_Damageable"
REGIONS_PREFIX = "sharedassets/coatlicue/newworld_vitaeeterna/regions/"
# The size of a region of the world (m): the name r_+XX_+YY of the region of a place.
REGION_SIZE = 2048.0
VISUAL_COLUMNS = {"healthy": "Visual_Healthy", "notOnQuest": "Visual_NotOnRequiredQuest", "base": "Visual_Basemesh",
                  "depleted": "Visual_Depleted"}
EFFECT_COLUMNS = {"idle": "VFX_Idle", "idleZ": "IdleVFX_Offset_Z", "depleted": "DepletedVFX", "depletedZ": "depletedVFX_Offset_Z",
                  "depletedQuest": "DepletedVFX_Quest", "depletedQuestZ": "DepletedVFX_Quest_Offset_Z", "gather": "GatheringFX"}


def slice_path(value: str) -> str:
    return value.strip().replace("\\", "/").lower()


def region_of(place) -> str:
    x, y = int(place[0] // REGION_SIZE), int(place[1] // REGION_SIZE)
    return f"r_+{x:02d}_+{y:02d}"


def variant_rows() -> dict[str, dict]:
    """@return The rows of the variation table Gatherable_Quest_Damageable (the variant in lower case)."""
    out: dict[str, dict] = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except (ValueError, struct.error):
            continue
        if sheet.type != "VariationData" or sheet.name != TABLE:
            continue
        for row in sheet.rows():
            variant = str(row.get("VariantID", "")).strip().lower()
            if variant and variant not in out:
                out[variant] = row
    return out


def rotations(keys: set[str], regions: set[str]) -> dict[str, list[float]]:
    """@return The rotation of each placement key (the capital files of the regions of the objects)."""
    out: dict[str, list[float]] = {}
    for path in source.paks().paths(REGIONS_PREFIX, ".capitals.json"):
        if path[len(REGIONS_PREFIX):].split("/")[0] not in regions:
            continue
        for placement in json.loads(source.read(path)).get("Capitals") or []:
            key = str(placement.get("id", "")).strip().lower()
            if key in keys:
                r = placement.get("rotation") or {}
                out[key] = [round(float(r.get(axis, 1.0 if axis == "w" else 0.0)), 6) for axis in ("x", "y", "z", "w")]
    return out


def number(value, default=0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def write(out_path: Path | None = None) -> dict:
    data = json.loads((gamedata_dir() / "quest-objects.json").read_text())
    rows = data.get("rows", [])
    variants = variant_rows()
    names = objects.gatherable_columns()
    keys = {o["key"].lower() for o in data["objects"]}
    turns = rotations(keys, {region_of(o["position"]) for o in data["objects"]})
    assets = manifest.load()["assets"]
    pairs_path = overrides.out_dir() / "pairs.json"
    pairs = json.loads(pairs_path.read_text()) if pairs_path.is_file() else {}
    slices: dict[str, list] = {}
    missing: set[str] = set()
    unconverted: set[str] = set()
    markers: dict[str, dict] = {}

    def parts_of(path: str) -> str:
        if path and path not in slices:
            try:
                parts, gone = server_visuals.slice_parts(path, assets, pairs)
            except (KeyError, ValueError, struct.error) as error:
                parts, gone = [], [f"{path}: {error}"]
            slices[path] = parts
            missing.update(gone)
            try:
                unconverted.update(m for m, *_ in region.slice_meshes(source.read(path)) if m not in assets
                                   and not m.rsplit("/", 1)[-1].startswith("fx_"))
            except (KeyError, ValueError, struct.error):
                pass
        return path

    out_objects = []
    no_variant = []
    for obj in sorted(data["objects"], key=lambda o: o["key"]):
        row = rows[obj["row"]] if 0 <= obj.get("row", -1) < len(rows) else {}
        variant = variants.get(obj["variant"].lower(), {})
        if not variant:
            no_variant.append(obj["variant"])
        placed = parts_of(obj["slice"].lower())
        visuals = {key: parts_of(slice_path(str(variant.get(column, "") or ""))) for key, column in VISUAL_COLUMNS.items()}
        if placed not in markers:
            markers[placed] = creature.read_slice(placed).get("marker", {})
        marker = markers[placed]
        override = str(variant.get("Marker_Creature_Type_Override", "") or marker.get("creatureTypeOverride", ""))
        offset = variant.get("Marker_Vertical_Offset")
        effects = {key: (number(variant.get(column)) if key.endswith("Z") else str(variant.get(column, "") or ""))
                   for key, column in EFFECT_COLUMNS.items()}
        out_objects.append({
            "key": obj["key"], "actor": obj["actor"], "tag": obj.get("tag", ""), "onlyOnQuest": bool(obj.get("onlyOnQuest")),
            "variant": obj["variant"], "row": row.get("id", ""), "name": names.get(str(row.get("id", "")).lower(), {}).get("name", ""),
            "place": obj["position"], "rotation": turns.get(obj["key"].lower(), [0.0, 0.0, 0.0, 1.0]),
            "slices": {"placed": placed, **visuals},
            "marker": {"type": marker.get("type", ""), "override": override,
                       "forceHealthBar": bool(variant.get("Force_Display_Health_Bar")) or bool(marker.get("forceDisplayHealthBar")),
                       "verticalOffset": number(offset, number(marker.get("verticalOffset"))),
                       "radius": number(variant.get("MarkerRadius"))},
            "collision": {"asset": slice_path(str(variant.get("CollisionAssetOverride", "") or "")),
                          "offsetZ": number(variant.get("Collision_Offset_Z")),
                          "scale": [number(variant.get(f"Collision_Scale_{a}"), 1.0) for a in "XYZ"]},
            "effects": effects})
    document = {"format": FORMAT, "source": "the backend copy quest-objects.json, the capitals, the variation table " + TABLE,
                "objects": out_objects, "slices": {k: v for k, v in sorted(slices.items()) if k}, "missing": sorted(missing),
                "unconverted": sorted(unconverted)}
    path = out_path or env.content_dir() / "world-objects" / "quest-objects.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(document, indent=0))
    temporary.replace(path)
    drawn = [o for o in out_objects if any(slices.get(p) for p in o["slices"].values() if p)]
    return {"file": str(path), "objects": len(out_objects), "objects with meshes": len(drawn),
            "objects with no rotation": sum(1 for o in data["objects"] if o["key"].lower() not in turns),
            "variants not in the table": sorted(set(no_variant)), "slices": len(document["slices"]),
            "slices with no drawn part": sorted(k for k, v in document["slices"].items() if not v), "meshes not imported": sorted(missing),
            "meshes not converted": sorted(unconverted),
            "variants with no mesh": sorted({o["variant"] for o in out_objects if o not in drawn})}


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    out = Path(argv[argv.index("--out") + 1]) if "--out" in argv else None
    print(json.dumps(write(out), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
