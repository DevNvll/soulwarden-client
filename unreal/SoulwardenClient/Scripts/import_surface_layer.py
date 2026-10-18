"""Imports the surface layer of regions (stage M5 with S1, docs/MATERIALS.md): the decal materials and the road meshes.

Run through tools/import-surface-layer.sh (UnrealEditor-Cmd -run=pythonscript, no window, with the import lock).
Reads SWC_CONTENT/decals (converter/swc_convert/decals.py: NAME/layer.json, instances.json, textures.json).
1. The decal material instances of the regions: /SwcGameContent/Materials/d_KEY, the parent M_SwcDecal
   (Scripts/swc_materials.py apply_specs; the state key "materials:d_KEY").
2. The road meshes: each glTF of layer.json into its folder /SwcGameContent/Roads/R..., with the mesh pipeline with no
   Nanite (P_SwcMesh: a decal material cannot be on a Nanite mesh) and the link pipeline (each slot gets the instance
   of its name). The state key is "road:KEY" (the hash of the glTF).
The client layer (Source/SoulwardenClient/World/SwcSurfaceLayer) places them at run time.
Environment: SWC_CONTENT, SWC_GAME_CONTENT, SWC_SURFACE_REGIONS (region names), SWC_SURFACE_MAX_SECONDS.
"""
import json
import os
import sys
import time

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

content = os.environ["SWC_CONTENT"]
plugin = os.environ["SWC_GAME_CONTENT"]
regions = os.environ.get("SWC_SURFACE_REGIONS", "").split()
max_seconds = float(os.environ.get("SWC_SURFACE_MAX_SECONDS") or 0)
state_path = os.path.join(plugin, "import-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) else {}
started = time.time()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([sm.ROOT], True)
report = {"regions": regions, "roads": {"imported": 0, "up to date": 0}, "seconds": {}}


def save_state():
    with open(state_path + ".tmp", "w") as handle:
        json.dump(state, handle, indent=0, sort_keys=True)
    os.replace(state_path + ".tmp", state_path)


layers = [json.load(open(os.path.join(content, "decals", name, "layer.json"))) for name in regions]
keys = sorted({k for layer in layers for road in layer["roads"] for k in road["materials"]}
              | {d["material"] for layer in layers for d in layer["decals"]})
sm.log(f"surface layer: {len(regions)} regions, {len(keys)} decal materials")

# 1. The decal materials.
phase = time.time()
sm.apply_specs(keys, state, force=False, content=content, max_seconds=max_seconds, report=report, part="decals")
save_state()
report["seconds"]["materials"] = round(time.time() - phase, 1)

# 2. The road meshes (batches of 16: a save waits for the builds of the batch).
phase = time.time()
pipeline = None
batch = []


def flush():
    for key, folder, stamp in batch:
        try:
            sm.assets.save_directory(folder, only_if_is_dirty=False, recursive=True)
            if sm.first_asset(folder, "StaticMesh") is None:
                raise RuntimeError("no static mesh")
        except Exception as error:
            report["failed"].append(f"{key}: {str(error)[:120]}")
            continue
        state[key] = stamp
        report["roads"]["imported"] += 1
    batch.clear()
    save_state()
    unreal.SystemLibrary.collect_garbage()


if not report.get("incomplete"):
    link = unreal.new_object(unreal.SwcMaterialLinkPipeline)
    link.set_editor_property("material_folder", sm.ROOT + "/Materials")
    pipelines = [unreal.SoftObjectPath(sm.BASE + "/P_SwcMesh"), unreal.SoftObjectPath(link.get_path_name())]
    for layer in layers:
        for road in layer["roads"]:
            if max_seconds and time.time() - started > max_seconds:
                report["incomplete"] = True
                break
            key = road["key"]
            folder = road["unreal"]
            if state.get(key) == road["hash"] and sm.assets.list_assets(folder, recursive=True):
                report["roads"]["up to date"] += 1
                continue
            parameters = unreal.ImportAssetParameters()
            parameters.set_editor_property("is_automated", True)
            parameters.set_editor_property("replace_existing", True)
            parameters.set_editor_property("override_pipelines", pipelines)
            unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
                folder, unreal.InterchangeManager.create_source_data(os.path.join(content, road["file"])), parameters)
            batch.append((key, folder, road["hash"]))
            if len(batch) >= 16:
                flush()
    flush()
report["seconds"]["roads"] = round(time.time() - phase, 1)
report["seconds"]["all"] = round(time.time() - started, 1)
report["failures"] = report["failed"][:20]
report["failed"] = len(report["failed"])
with open(os.path.join(content, "decals", "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
sm.log("SURFACE " + json.dumps(report))
