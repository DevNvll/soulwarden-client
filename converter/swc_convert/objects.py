"""The interactable objects of the world for the object layer of the client (stage P4.1 part 2; docs/OBJECTS.md).

The client makes the objects of the level data itself (Soulwarden docs/systems/interactions.md, gathering.md, chests.md): the
lore pages, the chests, the gathering nodes, the stations, the persons, the doors, the shrines, the furniture. The server sends
a record of such an object only when its state is not the state of the data (a depleted node, a disabled bound). This tool writes
the data of the layer from the game data of the backend copy (made by the Soulwarden tools; read only):

  interaction-data.json   the objects of the layers (a placement UUID each), their Interact bounds, the options, the achievements
  chest-data.json         the chests (a part of the gathering objects): their kind and gatherable row
  gathering-data.json     the gathering templates, the capital nodes (UUID -> template, row), the natural node regions
  gathering/REGION.swgn   the records of the distribution files of a region (the natural nodes)

and from the variation tables of the game copy (the StartMesh and the DepletedMesh of each variant of a node).

Output (SWC_CONTENT/world-objects, outside the repository):
  objects.json            the tables: the options, the gathering templates (with the visuals of the meshes of their slice: the
                          level places them at each node), the gatherable rows, the visuals of the variants
                          (the folders of the StartMesh and of the DepletedMesh, the object path of the imported DepletedMesh:
                          write the file again after tools/import.sh), the regions (their node types), and the gathering
                          (stage P5.2): the rows (the tool family, the gather action, the rank gate, the time, the name), the
                          tools by item index, the tool slots, the bodies of the creature slices with skinning, the texts
  regions/REGION.json     the objects of the interaction data of a region, with the ids of the backend (actor, facet target,
                          option entities) and their bounds
  nodes/REGION.bin        the natural nodes of a region in cells of 64 m (the record index gives the offline id)

The file of the nodes (little-endian): "SWON", u32 format 1, i32 region x, i32 region y, u32 node count, u32 side (32), u32
offsets[side * side + 1] (the first node of each cell; cell index = cy * side + cx from the corner of the region), then the
nodes, 20 bytes each: u32 record (the index in the .swgn file: counter = record + 1), u16 type (the index in the types of the
region), u16 0, f32 x y z (engine m).

The tool checks the id rules of the client against the ids of the backend data: the actor of each object from its UUID, and
the facet target and the option entity of each capital node from its gathering template.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
import uuid
from pathlib import Path

import numpy as np

from . import datasheet, env, source
from .bounds import BoundShapes
from . import manifest as shared_manifest
from .character import norm
from .creature import gamedata_dir

FORMAT = 1
REGION_M = 2048.0
CELL_M = 64.0
SIDE = int(REGION_M / CELL_M)
NODE = np.dtype([("record", "<u4"), ("type", "<u2"), ("pad", "<u2"), ("x", "<f4"), ("y", "<f4"), ("z", "<f4")])
SWGN_HEADER = 24
SWGN_RECORD = np.dtype([("type", "<u2"), ("x", "<f4"), ("y", "<f4"), ("z", "<f4")])
MASK64 = (1 << 64) - 1


def out_dir() -> Path:
    return env.content_dir() / "world-objects"   # not "objects": the converted meshes of the game paths objects/... are there


# ------------------------------------------------------------------------------------------------------------- id rules

def placement_actor(key: str) -> bytes:
    """@return The actor id of a placement of a layer from its UUID (the client rule; research P4.1, test cases IT10 of the
    backend): XOR the two halves of the 16 bytes, reverse the 8 bytes, set the low 3 bits of the first byte to 3, then 8 zero
    bytes."""
    raw = uuid.UUID(key).bytes
    half = bytes(a ^ b for a, b in zip(raw[:8], raw[8:]))[::-1]
    return bytes([(half[0] & 0xF8) | 3]) + half[1:] + bytes(8)


def group_of(actor: bytes) -> int:
    """@return G: the first 8 bytes of an actor id (little-endian)."""
    return int.from_bytes(actor[:8], "little")


def natural_group(region_x: int, region_y: int, counter: int) -> int:
    """@return The offline id G of a record of a distribution file (backend GatheringData::natural_group; client 0x69D5090)."""
    return ((counter & 0x7FFFF) << 13) | ((region_y & 0x1F) << 8) | ((region_x & 0x1F) << 3) | 1


def facet_target(root: int, group: int, entity: int, component: int) -> int:
    """@return The facet target of a component of an entity of a slice: (entity XOR root XOR G) + component (protocol notes 4.2)."""
    return (((entity ^ root ^ group) & MASK64) + component) & MASK64


# ------------------------------------------------------------------------------------------------------------- visuals

def variation_rows(tables: set[str]) -> dict[str, dict[str, dict]]:
    """@return The rows of the variation tables with these names: table -> variant (lower case) -> row."""
    out: dict[str, dict[str, dict]] = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except (ValueError, struct.error):
            continue
        if sheet.type != "VariationData" or sheet.name not in tables or "StartMesh" not in sheet.columns:
            continue
        rows = out.setdefault(sheet.name, {})
        for row in sheet.rows():
            variant = str(row.get("VariantID", "")).strip().lower()
            if variant and variant not in rows:
                rows[variant] = row
    return out


def imported_mesh(folder: str) -> str:
    """@return The object path of the static mesh that the import made in a manifest folder (FOLDER/NAME/StaticMeshes/ASSET.ASSET:
    Interchange changes some characters of the name), or "" when the content plugin has none."""
    root = Path(os.environ.get("SWC_GAME_CONTENT") or env.work_dir() / "unreal/SwcGameContent") / "Content"
    prefix = shared_manifest.ROOT + "/"
    if not folder.startswith(prefix):
        return ""
    for asset in sorted((root / folder[len(prefix):]).glob("*/StaticMeshes/*.uasset")):
        relative = asset.relative_to(root).with_suffix("")
        return f"{shared_manifest.ROOT}/{relative.as_posix()}.{asset.stem}"
    return ""


class Visuals:
    """The visuals of the node variants: the Unreal folders of the StartMesh and of the DepletedMesh (the manifest rule), and the
    object path of the imported DepletedMesh (the layer shows it when the level has no depleted instance)."""

    def __init__(self) -> None:
        self.rows: list[list] = []
        self.index: dict[tuple[str, str], int] = {}
        self.assets = shared_manifest.load()["assets"]

    def of(self, start: str, depleted: str) -> int:
        start, depleted = norm(start.strip()), norm(depleted.strip())
        if not start:
            return -1
        key = (start, depleted)
        if key not in self.index:
            self.index[key] = len(self.rows)
            depleted_folder = shared_manifest.unreal_folder("static", depleted) if depleted else ""
            self.rows.append([shared_manifest.unreal_folder("static", start), depleted_folder, start in self.assets,
                              imported_mesh(depleted_folder) if depleted_folder else "", start, depleted])
        return self.index[key]


def slice_meshes(path: str) -> set[str]:
    """@return The static meshes of a slice (game paths, lower case; no LOD file)."""
    out: set[str] = set()
    try:
        roots = source.parse(source.read(path))
    except Exception:   # a slice that the reader cannot read: no meshes
        return out
    for root in roots:
        for element in source.walk(root):
            found = source.asset_id(element)
            product = source.product(found[0], found[1], ".cgf") if found else None
            if product and "_lod" not in product:
                out.add(norm(product))
    return out


# ------------------------------------------------------------------------------------------------------------- gathering

# The paperdoll slot of the tool of each gathering type (Soulwarden protocol notes 4.44, live check L6: 33 the logging axe, 34 the
# sickle, 35 the skinning knife, 36 the pick) and the prompt text of a missing tool (the localization keys ui_notool<type>).
TOOL_SLOTS = {"Chopping": 33, "Cutting": 34, "Dressing": 35, "Mining": 36}


def gatherable_columns() -> dict[str, dict]:
    """@return GatheringAction and DisplayName of each gatherable row of the GatherableData tables (the id in lower case)."""
    out: dict[str, dict] = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except (ValueError, struct.error):
            continue
        if sheet.type != "GatherableData" or "GatheringAction" not in sheet.columns:
            continue
        for row in sheet.rows():
            identifier = str(row.get("GatherableID", "")).strip().lower()
            if identifier and identifier not in out:
                out[identifier] = {"action": str(row.get("GatheringAction", "") or "None"), "name": str(row.get("DisplayName", ""))}
    return out


def localized_texts() -> dict[str, str]:
    """@return The converted texts of the game (the UI track: SWC_CONTENT/ui/localization/en-us.json), keys in lower case."""
    path = env.content_dir() / "ui" / "localization" / "en-us.json"
    if not path.is_file():
        return {}
    return {key.lower(): value for key, value in json.loads(path.read_text()).items() if isinstance(value, str)}


def text_key(value: str) -> str:
    return value[1:].lower() if value.startswith("@") else value.lower()


def gathering_tables(folder: Path, gathering: dict, chest_names: set[str] = frozenset()) -> dict:
    """@return The rows of the gathering (the tool family, the gather action, the rank gate, the time, the name), the tools by
    their item index (the position in item-catalog.json plus 1: the value of the paperdoll; their weapon tags of weapons.json),
    the bodies of the creature slices with skinning, and the texts of the prompts."""
    columns = gatherable_columns()
    texts = localized_texts()
    used_texts: dict[str, str] = {}
    rows = []
    for row in gathering["rows"]:
        extra = columns.get(row["id"].lower(), {})
        name = extra.get("name", "")
        if name and text_key(name) in texts:
            used_texts[text_key(name)] = texts[text_key(name)]
        rows.append([row["id"], row["type"], extra.get("action", "None"), row["restriction"], row["tradeskill"], row["level"], row["time"],
                     name, list(row.get("unsupported", []))[:1]])
    for kind in TOOL_SLOTS:
        key = f"ui_notool{kind.lower()}"
        if key in texts:
            used_texts[key] = texts[key]
    for key in chest_names:   # the names of the chests (the prompt "E SUPPLY CHEST")
        if key in texts:
            used_texts[key] = texts[key]
    catalog = json.loads((folder / "item-catalog.json").read_text())["items"]
    index_of = {item[0].lower(): position + 1 for position, item in enumerate(catalog)}
    weapons_path = env.content_dir() / "player" / "weapons.json"
    weapon_items = json.loads(weapons_path.read_text())["items"] if weapons_path.is_file() else {}
    tools = {}
    for item, tool in gathering["tools"].items():
        index = index_of.get(item.lower())
        if index is not None:
            tools[str(index)] = [item, (weapon_items.get(item) or {}).get("tag", ""), tool["types"]]
    # The key of a slice as the entity table has it (Creatures/SwcEntityTable slice_key: the UUID with dashes, upper case, ":" sub).
    bodies = {}
    for key, entry in gathering.get("creatures", {}).items():
        asset, sub = key.split(":")
        bodies[f"{str(uuid.UUID(asset)).upper()}:{sub}"] = [entry["template"], entry["row"]]
    return {"rows": rows, "tools": tools, "bodies": bodies, "texts": used_texts}


# ------------------------------------------------------------------------------------------------------------- write

def file_hash(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()[:16]


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def object_kind(entry: dict, chests: dict, capitals: dict) -> str:
    if entry["kind"] != "gathering":
        return entry["kind"]
    if entry["key"] in chests:
        return "chest"
    if entry["key"] in capitals:
        return "node"
    return "gatherable"


def write() -> dict:
    folder = gamedata_dir()
    interactions = json.loads((folder / "interaction-data.json").read_text())
    chest_doc = json.loads((folder / "chest-data.json").read_text())
    gathering = json.loads((folder / "gathering-data.json").read_text())
    chests = {c["key"]: c for c in chest_doc["chests"]}
    capitals = gathering["capitals"]
    templates = gathering["templates"]
    report: dict = {"objects": 0, "bounds": 0, "nodes": 0, "kinds": {}, "actorMismatches": 0, "capitalMismatches": 0,
                    "capitalsChecked": 0, "regions": 0, "boundsWithShape": 0}
    shapes = BoundShapes()

    # The visuals of the variants of each gathering template (its variation table).
    tables = {t["table"] for t in templates if t["table"]}
    rows_by_table = variation_rows(tables)
    visuals = Visuals()
    template_visuals: list[list[int]] = []
    for template in templates:
        rows = rows_by_table.get(template["table"], {})
        template_visuals.append(sorted({visuals.of(str(r.get("StartMesh", "")), str(r.get("DepletedMesh", ""))) for r in rows.values()} - {-1}))

    # The meshes of each gathering slice: the level of the world track places them at each node of the slice (its default variant,
    # not the variant of the record): the layer also takes these visuals as the meshes of the node.
    by_start: dict[str, list[int]] = {}
    by_depleted: dict[str, list[int]] = {}
    for index, row in enumerate(visuals.rows):
        by_start.setdefault(row[4], []).append(index)
        if row[5]:
            by_depleted.setdefault(row[5], []).append(index)
    slice_visuals: list[list[int]] = []
    for template in templates:
        meshes = slice_meshes(template["slice"])
        slice_visuals.append(sorted({v for m in meshes for v in by_start.get(m, []) + by_depleted.get(m, [])}))
    report["templatesWithSliceVisuals"] = sum(1 for v in slice_visuals if v)

    # The objects of the interaction data, by region.
    achievements = interactions["achievements"]
    chest_names: set[str] = set()
    by_region: dict[str, list] = {}
    for entry in interactions["objects"]:
        kind = object_kind(entry, chests, capitals)
        actor = bytes.fromhex(entry["actor"])
        if placement_actor(entry["key"]) != actor:
            report["actorMismatches"] += 1
        bounds = []
        for bound in entry["bounds"]:
            if not bound["options"]:
                continue
            bounds.append({"slot": bound["slot"], "component": bound["component"], "position": bound["position"], "rangeSq": bound["rangeSq"],
                           "lineOfSight": bound["lineOfSight"], "options": bound["options"], "target": bound["target"],
                           "lore": achievements[bound["lore"]][1] if bound["lore"] >= 0 else -1})
            # AST-198: the candidate shape of the bound (its own shape in its slice; the range is only the use check).
            shape = shapes.shape(entry["slice"], component=bound["component"])
            if shape is not None:
                bounds[-1]["shape"] = shape
                report["boundsWithShape"] += 1
        if not bounds:
            continue
        out = {"key": entry["key"], "kind": kind, "actor": entry["actor"], "position": entry["position"], "bounds": bounds}
        if kind == "node":
            template_index, row = capitals[entry["key"]]
            out["gather"] = [template_index, row]
            # The rule of a capital node: G from the UUID, the target and the option entity from the template (gathering.md).
            template = templates[template_index]
            group = group_of(actor)
            root = int(template["root"], 16)
            target = facet_target(root, group, int(template["bound"]["entity"], 16), int(template["bound"]["component"], 16))
            option = (int(template["bound"]["option"], 16) ^ root ^ group) & MASK64
            report["capitalsChecked"] += 1
            first = bounds[0]
            if f"{target:016x}" != first["target"] or not first["options"] or f"{option:016x}" != first["options"][0][1]:
                report["capitalMismatches"] += 1
        elif kind == "chest":
            chest = chests[entry["key"]]
            gatherable = chest_doc.get("gatherables", {}).get(chest.get("gatherable", ""), {})
            # [kind, gatherable row, state, name (a text key), elite] (stage P5.1: the prompt, the depletion effect of an elite chest)
            out["chest"] = [chest.get("kind", ""), chest.get("gatherable", ""), chest.get("state", ""), chest.get("name", ""),
                            bool(gatherable.get("elite")) or chest.get("kind") == "elite"]
            chest_names.add(text_key(chest.get("name", "")))
        by_region.setdefault(entry["region"], []).append(out)
        report["objects"] += 1
        report["bounds"] += len(bounds)
        report["kinds"][kind] = report["kinds"].get(kind, 0) + 1

    # The natural nodes of each region of the gathering data.
    regions: dict[str, dict] = {}
    for entry in gathering["regions"]:
        name = entry["name"]
        region_x, region_y = entry["x"], entry["y"]
        types = []
        for template_index, row, variant in entry["types"]:
            visual = -1
            # AST-198: the sphere radius of the variant (the column InteractionRadius: the variation action "Change Sphere Shape Radius" of
            # the bound entity; the small iron vein 3 m, not the 2 m of its slice); -1: the radius of the slice.
            radius = -1.0
            if template_index >= 0 and row >= 0:
                table_rows = rows_by_table.get(templates[template_index]["table"], {})
                found = table_rows.get(str(variant).lower())
                if found is not None:
                    visual = visuals.of(str(found.get("StartMesh", "")), str(found.get("DepletedMesh", "")))
                    if found.get("InteractionRadius") not in (None, ""):
                        radius = round(float(found["InteractionRadius"]), 5)
            types.append([template_index, row, visual, radius])
        nodes = np.zeros(0, dtype=NODE)
        swgn = folder / entry["file"]
        if entry["records"] > 0 and swgn.is_file():
            data = swgn.read_bytes()
            magic, version, x, y, count, tails = struct.unpack_from("<4sIiiII", data, 0)
            if magic != b"SWGN" or x != region_x or y != region_y or count != entry["records"] or tails != entry["tails"]:
                raise ValueError(f"{swgn}: the header does not fit the entry of gathering-data.json")
            records = np.frombuffer(data, dtype=SWGN_RECORD, count=count, offset=SWGN_HEADER)
            is_node = np.array([t[0] >= 0 and t[1] >= 0 for t in entry["types"]], dtype=bool)
            keep = np.nonzero(is_node[records["type"]])[0]
            nodes = np.zeros(len(keep), dtype=NODE)
            nodes["record"] = keep
            for field in ("type", "x", "y", "z"):
                nodes[field] = records[field][keep]
        cx = np.clip(((nodes["x"] - region_x * REGION_M) // CELL_M).astype(int), 0, SIDE - 1)
        cy = np.clip(((nodes["y"] - region_y * REGION_M) // CELL_M).astype(int), 0, SIDE - 1)
        cell = cy * SIDE + cx
        order = np.argsort(cell, kind="stable")
        nodes, cell = nodes[order], cell[order]
        offsets = np.searchsorted(cell, np.arange(SIDE * SIDE + 1)).astype("<u4")
        blob = b"SWON" + struct.pack("<IiiII", FORMAT, region_x, region_y, len(nodes), SIDE) + offsets.tobytes() + nodes.tobytes()
        write_atomic(out_dir() / "nodes" / f"{name}.bin", blob)
        regions[name] = {"x": region_x, "y": region_y, "nodes": int(len(nodes)), "objects": len(by_region.get(name, [])), "types": types}
        report["nodes"] += int(len(nodes))

    for name, objects in by_region.items():
        region_x, region_y = (int(v) for v in name[2:].split("_"))
        regions.setdefault(name, {"x": region_x, "y": region_y, "nodes": 0, "objects": len(objects), "types": []})
        write_atomic(out_dir() / "regions" / f"{name}.json", json.dumps({"region": name, "x": region_x, "y": region_y, "objects": objects},
                                                                        separators=(",", ":")).encode())
    report["regions"] = len(regions)

    options = [[o["name"], o["id"], o["action"], o["committed"], o["delay"], o["hold"]] for o in interactions["options"]]
    gather_option = next((i for i, o in enumerate(interactions["options"]) if o["id"] == gathering["option"]["id"]), -1)
    doc = {
        "format": FORMAT,
        "gamedata": {name: file_hash(folder / name) for name in ("interaction-data.json", "chest-data.json", "gathering-data.json")},
        "options": options,
        "gatherOption": gather_option,
        "templates": [{"root": t["root"], "controllerSlot": t["controllerSlot"], "table": t["table"], "entry": t["entry"],
                       "bound": {k: t["bound"][k] for k in ("entity", "component", "option", "slot", "rangeSq", "translation", "lineOfSight")},
                       "shape": shapes.shape(t["slice"], entity=t["bound"]["entity"]),
                       "visuals": template_visuals[i], "sliceVisuals": slice_visuals[i]} for i, t in enumerate(templates)],
        "gathering": gathering_tables(folder, gathering, chest_names),
        "visuals": [[row[0], row[1], row[3]] for row in visuals.rows],
        "regions": regions,
    }
    write_atomic(out_dir() / "objects.json", json.dumps(doc, separators=(",", ":")).encode())
    report["templatesWithShape"] = sum(1 for t in doc["templates"] if t["shape"])
    report["shapeSlicesMissing"] = len(shapes.missing)
    report["visuals"] = len(visuals.rows)
    report["visualsWithStart"] = sum(1 for r in visuals.rows if r[2])
    report["visualsWithImportedDepleted"] = sum(1 for r in visuals.rows if r[3])
    report["depletedMissing"] = sorted({r[5] for r in visuals.rows if r[5] and r[5] not in visuals.assets})
    report["depletedNotImported"] = sorted({r[5] for r in visuals.rows if r[5] and not r[3]})
    report["depletedMeshes"] = sorted({r[5] for r in visuals.rows if r[5]})
    (out_dir() / "report.json").write_text(json.dumps(report, indent=1))
    report["depletedMissing"] = len(report["depletedMissing"])
    report["depletedNotImported"] = len(report["depletedNotImported"])
    report["depletedMeshes"] = len(report["depletedMeshes"])
    if report["actorMismatches"] or report["capitalMismatches"]:
        raise SystemExit(f"objects: the id rules of the client do not give the ids of the backend data: {report}")
    return report


def depleted_meshes() -> dict:
    """Converts the DepletedMesh of each node variant (the level places only the StartMesh of a node: the layer shows the depleted
    mesh itself). Writes the list of the mesh keys for the import (tools/import.sh --keys); then write the data again.
    @return Counts."""
    from . import mesh as shared_mesh
    doc = json.loads((out_dir() / "objects.json").read_text())
    report = json.loads((out_dir() / "report.json").read_text())
    keys = report.get("depletedMeshes", [])   # mesh.convert skips a mesh that is up to date
    done, failed = 0, []
    for key in keys:
        try:
            shared_mesh.convert(key)
            done += 1
        except Exception as error:   # a mesh that the reader cannot read: the layer then only hides the healthy mesh
            failed.append(f"{key}: {error}")
    assets = shared_manifest.load()["assets"]
    converted = [key for key in keys if key in assets]
    write_atomic(out_dir() / "depleted-meshes.json", json.dumps(converted, indent=0).encode())
    return {"meshes": len(keys), "converted": done, "inManifest": len(converted), "failed": failed[:20], "failedCount": len(failed),
            "visuals": len(doc["visuals"]), "keys": str(out_dir() / "depleted-meshes.json")}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-convert-objects", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("write")
    sub.add_parser("meshes")
    args = parser.parse_args(argv)
    if args.command == "write":
        result = write()
    else:
        result = depleted_meshes()
    json.dump(result, sys.stdout, indent=1)
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
