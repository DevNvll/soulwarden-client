"""Imports the converted player content (SWC_CONTENT/player/manifest.json) into /SwcGameContent/Player.

Run through tools/import-player.sh (UnrealEditor-Cmd -run=pythonscript, no window). The assets are made from the game
copy: they go to the shared content plugin of the imported content (tools/content-link.sh), not into git. The world
level and the client load the same assets.

- A base (one for each gender): the skeleton, the base mesh, the physics asset, and the clips (the default glTF pipeline).
- A part: one skeletal mesh on the skeleton of the base of its gender (a copy of the glTF asset pipeline with that
  skeleton, no physics asset, no animation). The client attaches it to the base with Leader Pose.

The script writes SWC_CONTENT/player/imported.json: for each manifest key, the object paths that the client loads.
Environment: SWC_CONTENT, SWC_IMPORT_FORCE=1 (import each asset again).
"""
import json
import os

import unreal

content = os.environ["SWC_CONTENT"]
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
player = os.path.join(content, "player")
manifest = json.load(open(os.path.join(player, "manifest.json")))
imported_path = os.path.join(player, "imported.json")
imported = json.load(open(imported_path)) if os.path.isfile(imported_path) and not force else {"format": 1, "assets": {}}
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
manager = unreal.InterchangeManager.get_interchange_manager_scripted()
GLTF_ASSETS = "/Interchange/Pipelines/DefaultGLTFAssetsPipeline"
GLTF_MATERIALS = "/Interchange/Pipelines/DefaultGLTFPipeline"
PIPELINES = "/SwcGameContent/Player/Pipelines"
# A commandlet does not scan the content of a plugin: scan it before any question to the asset registry.
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/SwcGameContent"], True)


def objects_in(folder):
    """@return {class name: [object paths]} of the assets of a folder."""
    out = {}
    for path in assets.list_assets(folder, recursive=True, include_folder=False):
        data = assets.find_asset_data(path)
        name = str(data.asset_class_path.asset_name)
        out.setdefault(name, []).append(str(data.package_name) + "." + str(data.asset_name))
    return out


def part_pipeline(gender, skeleton):
    """@return The soft path of the pipeline of the parts of one gender (an asset under /SwcGameContent/Player)."""
    path = f"{PIPELINES}/PartAssets_{gender}"
    pipeline = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if pipeline is None:
        pipeline = assets.duplicate_loaded_asset(unreal.load_asset(GLTF_ASSETS), path)
    common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", skeleton)
    common.set_editor_property("try_auto_select_skeleton", False)
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("create_physics_asset", False)
    animation = pipeline.get_editor_property("animation_pipeline")
    animation.set_editor_property("import_animations", False)
    assets.save_loaded_asset(pipeline)
    return unreal.SoftObjectPath(pipeline.get_path_name())


def import_file(path, destination, pipelines=None):
    if pipelines is None:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        task.set_editor_property("replace_existing", True)
        tools.import_asset_tasks([task])
        return
    parameters = unreal.ImportAssetParameters()
    parameters.set_editor_property("is_automated", True)
    parameters.set_editor_property("replace_existing", True)
    parameters.set_editor_property("override_pipelines", pipelines)
    source = unreal.InterchangeManager.create_source_data(path)
    manager.import_asset(destination, source, parameters)
    assets.save_directory(destination, only_if_is_dirty=False, recursive=True)


done, skipped, failed = 0, 0, []
order = {"base": 0, "part": 1}
skeletons = {}
pipelines = {}
for key, entry in sorted(manifest["assets"].items(), key=lambda item: (order.get(item[1]["kind"], 9), item[0])):
    destination = entry["unreal"]
    kind = entry["kind"]
    path = os.path.join(content, entry["file"])
    # A file that the converter wrote again (a new time) is imported again, onto the same assets.
    stamp = os.path.getmtime(path) if os.path.isfile(path) else 0
    known = imported["assets"].get(key, {})
    if not force and known and known.get("_source") == stamp and assets.list_assets(destination, recursive=True):
        skipped += 1
        if kind == "base":
            skeletons[entry["gender"]] = known["Skeleton"][0]
        continue
    if kind == "part":
        gender = entry["gender"]
        if gender not in skeletons:
            failed.append(key + ": no base skeleton of " + gender)
            continue
        if gender not in pipelines:
            pipelines[gender] = part_pipeline(gender, unreal.load_asset(skeletons[gender]))
        import_file(path, destination, [pipelines[gender], unreal.SoftObjectPath(GLTF_MATERIALS)])
    else:
        import_file(path, destination)
    found = objects_in(destination)
    wanted = {"base": "Skeleton", "part": "SkeletalMesh"}[kind]
    if wanted not in found:
        failed.append(key)
        continue
    found["_source"] = stamp
    imported["assets"][key] = found
    if kind == "base":
        skeletons[entry["gender"]] = found["Skeleton"][0]
    done += 1

with open(imported_path, "w") as handle:
    json.dump(imported, handle, indent=1, sort_keys=True)
report = {"imported": done, "skipped (exists)": skipped, "failed": len(failed), "failures": failed[:20]}
with open(os.path.join(player, "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC PLAYER IMPORT " + json.dumps(report))
