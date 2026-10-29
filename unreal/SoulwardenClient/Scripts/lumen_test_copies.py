"""The copies of the Lumen mesh data test (stage E2, docs/ENVIRONMENT.md): each mesh of a list (tools/lib/lumen_test.py) is imported
again from its glTF file into /SwcGameContent/Test/E2 with a mesh distance field (resolution scale 1) and Lumen cards (12): a copy
of the mesh pipeline of the world (Scripts/import_content.py, mesh_pipeline) with these two values, and the material link pipeline.
The import settings of the world meshes do not change (they stay with no distance field and no cards). The report has the
seconds of each batch of 16 (the import and the save: the save waits for the builds). Run through tools/lumen-test.sh (the import
lock).

Environment: SWC_LUMEN_LIST (the list), SWC_LUMEN_REPORT (the report), SWC_LUMEN_MAP (the map of the client: the object path of
each original mesh to its copy), SWC_LUMEN_MIN_SIZE (cm: a smaller mesh is not copied; it is below the detail of the distance
fields), SWC_LUMEN_FORCE (1: import again), SWC_LUMEN_DEST (the folder of the copies and their pipelines; default /SwcGameContent/Test/E2;
the water track sets /SwcWaterCheck/Lumen: a check plugin, docs/WATER.md "Stage 2c").
"""
import json
import os
import time

import unreal

ROOT = "/SwcGameContent"
TEST = (os.environ.get("SWC_LUMEN_DEST") or ROOT + "/Test/E2").rstrip("/")
BASE = TEST + "/Base"
assets = unreal.EditorAssetLibrary
items = json.load(open(os.environ["SWC_LUMEN_LIST"]))
min_size = float(os.environ.get("SWC_LUMEN_MIN_SIZE") or 100.0)
report = {"copied": 0, "up to date": 0, "small": 0, "missing": [], "failed": [], "batches": [], "meshes": []}
mapping = {}
started = time.time()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT + "/Meshes", TEST], True)


def log(text):
    unreal.log_warning("SWC LUMEN TEST " + text)   # a warning: the commandlet prints it


def first_mesh(folder):
    for path in assets.list_assets(folder, recursive=True, include_folder=False):
        if str(assets.find_asset_data(path).asset_class_path.asset_name) == "StaticMesh":
            return path.split(".")[0]
    return None


def pipeline(name, nanite, distance_fields):
    """@return The path of a glTF pipeline: the settings of the world pipeline (import_content.py mesh_pipeline, PIPELINE_VERSION 5),
    with or without the distance field and the Lumen cards."""
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    result = assets.duplicate_loaded_asset(unreal.load_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline"), path)
    materials = result.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", True)
    materials.set_editor_property("reuse_existing_materials", False)
    materials.set_editor_property("create_new_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    meshes = result.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("build_nanite", nanite)
    meshes.set_editor_property("distance_field_resolution_scale", 1.0 if distance_fields else 0.0)
    meshes.set_editor_property("max_lumen_mesh_cards", 12 if distance_fields else 0)
    meshes.set_editor_property("import_skeletal_meshes", False)
    meshes.set_editor_property("collision", False)   # the render meshes of the world have no collision (PIPELINE_VERSION 5)
    result.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    assets.save_loaded_asset(result)
    return path


link = unreal.new_object(unreal.SwcMaterialLinkPipeline)   # a transient object: Interchange copies it
link.set_editor_property("material_folder", ROOT + "/Materials")
pipelines = {nanite: [unreal.SoftObjectPath(pipeline(f"P_E2{'Nanite' if nanite else 'Mesh'}DF", nanite, True)),
                      unreal.SoftObjectPath(link.get_path_name())]
             for nanite in (True, False)}


def import_batch(batch):
    """Imports a batch of (item, folder) and saves it: the save waits for the builds of the meshes. @return The seconds."""
    start = time.time()
    for item, folder in batch:
        parameters = unreal.ImportAssetParameters()
        parameters.set_editor_property("is_automated", True)
        parameters.set_editor_property("replace_existing", True)
        parameters.set_editor_property("override_pipelines", pipelines[bool(item.get("nanite", True))])
        unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
            folder, unreal.InterchangeManager.create_source_data(item["file"]), parameters)
    for item, folder in batch:
        assets.save_directory(folder, only_if_is_dirty=False, recursive=True)
    seconds = round(time.time() - start, 1)
    report["batches"].append({"meshes": len(batch), "seconds": seconds})
    unreal.SystemLibrary.collect_garbage()
    return seconds


selected = []
for item in items:
    original = first_mesh(item["folder"])
    if original is None or not os.path.isfile(item.get("file", "")):
        report["missing"].append(item["mesh"])
        continue
    extent = unreal.load_asset(original).get_bounds().box_extent
    if 2.0 * max(extent.x, extent.y, extent.z) < min_size:
        report["small"] += 1
        continue
    item["original"] = original
    item["size cm"] = [round(2 * extent.x), round(2 * extent.y), round(2 * extent.z)]
    selected.append(item)
unreal.SystemLibrary.collect_garbage()
log(f"{len(selected)} mesh(es) of {len(items)} (the others are small or missing)")

batch = []
for index, item in enumerate(selected):
    folder = f"{TEST}/{item['folder'].split('/')[-1]}"
    if first_mesh(folder) is not None and os.environ.get("SWC_LUMEN_FORCE") != "1":
        report["up to date"] += 1
    else:
        batch.append((item, folder))
    if len(batch) >= 16 or (batch and index == len(selected) - 1):
        seconds = import_batch(batch)
        report["copied"] += len(batch)
        log(f"{report['copied']} copied ({seconds} s for {len(batch)})")
        batch = []
for item in selected:
    copy = first_mesh(f"{TEST}/{item['folder'].split('/')[-1]}")
    if copy is None:
        report["failed"].append(item["mesh"])
        continue
    name = item["original"].split("/")[-1]
    mapping[f"{item['original']}.{name}"] = f"{copy}.{copy.split('/')[-1]}"
    report["meshes"].append({"mesh": item["mesh"], "copy": copy, "nanite": item.get("nanite", True), "size cm": item["size cm"],
                             "buildings": item.get("buildings", [])})

report["build seconds"] = round(sum(b["seconds"] for b in report["batches"]), 1)
# The distance fields and the cards that are still in the build queues (a save waits only for the builds of its own meshes).
report["wait seconds"] = round(unreal.SwcLumenTestLibrary.wait_for_mesh_builds(), 1)
report["seconds"] = round(time.time() - started, 1)
with open(os.environ["SWC_LUMEN_REPORT"], "w") as handle:
    json.dump(report, handle, indent=1)
with open(os.environ["SWC_LUMEN_MAP"], "w") as handle:
    json.dump({"meshes": mapping}, handle, indent=1)
log(json.dumps({k: v for k, v in report.items() if k not in ("meshes", "batches")}))
