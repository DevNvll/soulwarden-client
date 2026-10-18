"""Imports a list of static mesh glTF files into given folders (the materials track: the cost measure of the M3 rebuild
in /SwcGameContent/Test/M3; docs/MATERIALS.md). Run through tools/import-gltf-list.sh (the import lock).

The pipelines are those of Scripts/import_content.py: P_SwcMeshNanite or P_SwcMesh, then the link pipeline (each
slot gets the material instance of its name in /SwcGameContent/Materials). Batches of 16 (a save waits for the builds
of the batch). A next call continues:
- An item with no "key" (the test import): it is skipped when its folder has a static mesh.
- An item with "key" and "hash" (the M3 rebuild into the existing mesh folders: material_records.rebuild_convert): it
  is skipped when the import state of the pipeline (SWC_GAME_CONTENT/import-state.json) has its hash and its folder has
  a static mesh. After each batch the state gets the hash of each mesh of the batch, so that tools/import.sh sees them
  as up to date.
Environment: SWC_GLTF_LIST (a JSON list of {"file", "folder", "nanite"[, "key", "hash"]}), SWC_GLTF_MAX_SECONDS,
SWC_GLTF_REPORT (the report file), SWC_GAME_CONTENT.
"""
import json
import os
import sys
import time

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

items = json.load(open(os.environ["SWC_GLTF_LIST"]))
max_seconds = float(os.environ.get("SWC_GLTF_MAX_SECONDS") or 0)
report_path = os.environ["SWC_GLTF_REPORT"]
started = time.time()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([sm.ROOT], True)
link = unreal.new_object(unreal.SwcMaterialLinkPipeline)
link.set_editor_property("material_folder", sm.ROOT + "/Materials")
pipelines = {nanite: [unreal.SoftObjectPath(sm.BASE + ("/P_SwcMeshNanite" if nanite else "/P_SwcMesh")),
                      unreal.SoftObjectPath(link.get_path_name())] for nanite in (True, False)}
report = {"imported": 0, "up to date": 0, "failed": [], "incomplete": False, "batches": [], "slots with no material": 0}
batch = []
state_path = os.path.join(os.environ["SWC_GAME_CONTENT"], "import-state.json")


def load_state():
    return json.load(open(state_path)) if os.path.isfile(state_path) else {}


def save_state(done):
    """Writes the hash of each imported mesh into the import state of the pipeline (read again first: only these keys
    change). The caller holds the import lock."""
    if not done:
        return
    state = load_state()
    state.update(done)
    with open(state_path + ".tmp", "w") as handle:
        json.dump(state, handle, indent=0, sort_keys=True)
    os.replace(state_path + ".tmp", state_path)


state = load_state()
# The instances of the slots (the rebuild: the n_ instances of A1 and the dm_ instances of the mesh decal companions)
# must exist before the link pipeline sets the slots: apply their specs first (a spec that is up to date is skipped).
slot_keys = sorted({k for item in items for k in item.get("slots", [])})
if slot_keys:
    before = dict(state)
    for part, keys in (("materials", [k for k in slot_keys if not k.startswith("dm_")]),
                       ("decals", [k for k in slot_keys if k.startswith("dm_")])):
        if keys:
            applied = sm.apply_specs(keys, state, force=False, part=part)
            report.setdefault("slot specs", {})[part] = {"instances": applied["instances"], "failed": len(applied["failed"])}
    save_state({k: v for k, v in state.items() if before.get(k) != v})


def flush():
    if not batch:
        return
    start = time.time()
    done = {}
    for item in batch:
        try:
            sm.assets.save_directory(item["folder"], only_if_is_dirty=False, recursive=True)
            mesh = sm.first_asset(item["folder"], "StaticMesh")
            if mesh is None:
                raise RuntimeError("no static mesh")
            for slot in mesh.get_editor_property("static_materials"):
                material = slot.get_editor_property("material_interface")   # the rule of import_content.py
                if material is None or not material.get_path_name().startswith(sm.ROOT + "/Materials/"):
                    report["slots with no material"] += 1
                    report.setdefault("slots", []).append(str(slot.get_editor_property("material_slot_name")))
            report["imported"] += 1
            if "key" in item:
                done[item["key"]] = item["hash"]
        except Exception as error:
            report["failed"].append(f"{item['file']}: {str(error)[:120]}")
    save_state(done)
    report["batches"].append({"meshes": len(batch), "save seconds": round(time.time() - start, 1),
                              "since start": round(time.time() - started, 1)})
    batch.clear()
    unreal.SystemLibrary.collect_garbage()


for item in items:
    if max_seconds and time.time() - started > max_seconds:
        report["incomplete"] = True
        break
    exists = sm.assets.does_directory_exist(item["folder"]) and sm.first_asset(item["folder"], "StaticMesh") is not None
    if exists and ("key" not in item or state.get(item["key"]) == item["hash"]):
        report["up to date"] += 1
        continue
    parameters = unreal.ImportAssetParameters()
    parameters.set_editor_property("is_automated", True)
    parameters.set_editor_property("replace_existing", True)
    parameters.set_editor_property("override_pipelines", pipelines[bool(item.get("nanite", True))])
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
        item["folder"], unreal.InterchangeManager.create_source_data(item["file"]), parameters)
    batch.append(item)
    if len(batch) >= 16:
        flush()
flush()
report["seconds"] = round(time.time() - started, 1)
report["failures"] = report["failed"][:20]
report["slots"] = sorted(set(report.get("slots", [])))[:20]
report["failed"] = len(report["failed"])
with open(report_path, "w") as handle:
    json.dump(report, handle, indent=1)
sm.log("GLTF LIST " + json.dumps({k: v for k, v in report.items() if k != "batches"}))
