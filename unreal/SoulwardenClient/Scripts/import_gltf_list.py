"""Imports a list of static mesh glTF files into given folders (the materials track: the cost measure of the M3 rebuild
in /SwcGameContent/Test/M3; docs/MATERIALS.md). Run through tools/import-gltf-list.sh (the import lock).

The pipelines are those of Scripts/import_content.py: P_SwcMeshNanite or P_SwcMesh, then the link pipeline (each
slot gets the material instance of its name in /SwcGameContent/Materials). Batches of 16 (a save waits for the builds
of the batch). A file whose folder has a static mesh is skipped (a next call continues).
Environment: SWC_GLTF_LIST (a JSON list of {"file", "folder", "nanite"}), SWC_GLTF_MAX_SECONDS, SWC_GLTF_REPORT (the
report file).
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


def flush():
    if not batch:
        return
    start = time.time()
    for item in batch:
        try:
            sm.assets.save_directory(item["folder"], only_if_is_dirty=False, recursive=True)
            mesh = sm.first_asset(item["folder"], "StaticMesh")
            if mesh is None:
                raise RuntimeError("no static mesh")
            for slot in mesh.get_editor_property("static_materials"):
                if slot.get_editor_property("material_interface") is None:
                    report["slots with no material"] += 1
            report["imported"] += 1
        except Exception as error:
            report["failed"].append(f"{item['file']}: {str(error)[:120]}")
    report["batches"].append({"meshes": len(batch), "save seconds": round(time.time() - start, 1),
                              "since start": round(time.time() - started, 1)})
    batch.clear()
    unreal.SystemLibrary.collect_garbage()


for item in items:
    if max_seconds and time.time() - started > max_seconds:
        report["incomplete"] = True
        break
    if sm.assets.does_directory_exist(item["folder"]) and sm.first_asset(item["folder"], "StaticMesh") is not None:
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
report["failed"] = len(report["failed"])
with open(report_path, "w") as handle:
    json.dump(report, handle, indent=1)
sm.log("GLTF LIST " + json.dumps({k: v for k, v in report.items() if k != "batches"}))
