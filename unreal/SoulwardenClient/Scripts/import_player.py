"""Imports the converted player content (SWC_CONTENT/player/manifest.json) into /SwcGameContent/Player.

Run through tools/import-player.sh (UnrealEditor-Cmd -run=pythonscript, no window). The assets are made from the game
copy: they go to the shared content plugin of the imported content (tools/content-link.sh), not into git. The world
level and the client load the same assets.

- A base (one for each gender): the skeleton, the base mesh, the physics asset, and the clips (the default glTF pipeline).
- A part: one skeletal mesh on the skeleton of the base of its gender (a copy of the glTF asset pipeline with that
  skeleton, no physics asset, no animation). The client attaches it to the base with Leader Pose.
- A weapon: one static mesh (a copy of the glTF asset pipeline with no mesh distance field and no Lumen cards, as the world
  meshes: the look is not a goal now, and a run then does not build them). The client attaches it to a bone attachment of the
  base. A weapon of an earlier pipeline (no mark WEAPON_PIPELINE in imported.json) is imported again one time.
- A skeletal weapon (AST-202: a weapon with weapon clips, the bow string and the flail chain; manifest "skeletal"): its own
  skeleton, its skeletal mesh (no physics asset, no mesh distance field, no Lumen cards), and its clips as animations on that
  skeleton. A static weapon of an earlier import in its folder is deleted first. The client plays the clips in step with the
  fragments of the player (docs/GEAR.md "The weapon clips").
- A clip pack (stage P2.7): the clips of one weapon tag set as animations on the skeleton of the base of its gender (a copy
  of the glTF asset pipeline with that skeleton and "import only animations"). The client plays them on the base.

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
# The mark of the weapon pipeline with no mesh distance field and no Lumen cards (a weapon with an older mark imports again).
WEAPON_PIPELINE = "no-distance-field-1"
WEAPON_SKELETAL_PIPELINE = "skeletal-3"
# The copies of base materials of other places for the skeletal weapons (with the skeletal mesh usage).
SKELETAL_MATERIALS = "/SwcGameContent/Player/Materials"
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


def weapon_pipeline():
    """@return The soft path of the pipeline of the weapons: no mesh distance field, no Lumen cards (import_content.py does the
    same for the world meshes)."""
    path = f"{PIPELINES}/WeaponAssets"
    pipeline = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if pipeline is None:
        pipeline = assets.duplicate_loaded_asset(unreal.load_asset(GLTF_ASSETS), path)
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("distance_field_resolution_scale", 0.0)
    mesh.set_editor_property("max_lumen_mesh_cards", 0)
    assets.save_loaded_asset(pipeline)
    return unreal.SoftObjectPath(pipeline.get_path_name())


def weapon_skeletal_pipeline():
    """@return The soft path of the pipeline of the skeletal weapons: a new skeleton for each weapon (no automatic choice: never the
    skeleton of the player), no physics asset, the animations, no mesh distance field, no Lumen cards."""
    path = f"{PIPELINES}/WeaponSkeletalAssets"
    pipeline = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if pipeline is None:
        pipeline = assets.duplicate_loaded_asset(unreal.load_asset(GLTF_ASSETS), path)
    common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", None)
    common.set_editor_property("try_auto_select_skeleton", False)
    common.set_editor_property("import_only_animations", False)
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("create_physics_asset", False)
    mesh.set_editor_property("distance_field_resolution_scale", 0.0)
    mesh.set_editor_property("max_lumen_mesh_cards", 0)
    animation = pipeline.get_editor_property("animation_pipeline")
    animation.set_editor_property("import_animations", True)
    assets.save_loaded_asset(pipeline)
    return unreal.SoftObjectPath(pipeline.get_path_name())


def skeletal_usage(destination):
    """Sets the skeletal mesh usage on the materials of a skeletal weapon (a material with no usage flag for its mesh compiles its
    shaders again at each start of the editor). A material instance takes the flag from its base material. A base material with the
    flag stays. A base material in the content plugin gets the flag; a base material of another place (the glTF M_Default of the
    Interchange plugin) is not changed: the instance gets a copy of it in the content plugin with the flag (the same parameters).
    @return The base materials that got a copy."""
    copied = []
    found = objects_in(destination)
    for path in found.get("Material", []) + found.get("MaterialInstanceConstant", []):
        material = unreal.load_asset(path)
        instance = isinstance(material, unreal.MaterialInstanceConstant)
        base = material.get_base_material() if instance else material
        if base is None:
            continue
        if base.get_editor_property("used_with_skeletal_mesh"):
            continue
        if instance and not base.get_path_name().startswith("/SwcGameContent/"):
            copy_path = f"{SKELETAL_MATERIALS}/{base.get_name()}_Skeletal"
            copy = unreal.load_asset(copy_path) if assets.does_asset_exist(copy_path) else None
            if copy is None:
                copy = assets.duplicate_loaded_asset(base, copy_path)
                copy.set_editor_property("used_with_skeletal_mesh", True)
                unreal.MaterialEditingLibrary.recompile_material(copy)
                assets.save_loaded_asset(copy)
            copied.append(base.get_path_name())
            unreal.MaterialEditingLibrary.set_material_instance_parent(material, copy)
            assets.save_loaded_asset(material)
            base = copy
        if not base.get_editor_property("used_with_skeletal_mesh"):
            base.set_editor_property("used_with_skeletal_mesh", True)
            unreal.MaterialEditingLibrary.recompile_material(base)
            assets.save_loaded_asset(base)
    return sorted(set(copied))


def clip_pipeline(gender, skeleton):
    """@return The soft path of the pipeline of the clip packs of one gender: only the animations, on the base skeleton."""
    path = f"{PIPELINES}/ClipAssets_{gender}"
    pipeline = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if pipeline is None:
        pipeline = assets.duplicate_loaded_asset(unreal.load_asset(GLTF_ASSETS), path)
    common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", skeleton)
    common.set_editor_property("try_auto_select_skeleton", False)
    common.set_editor_property("import_only_animations", True)
    animation = pipeline.get_editor_property("animation_pipeline")
    animation.set_editor_property("import_animations", True)
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
usage_others = set()
order = {"base": 0, "part": 1, "weapon": 2, "clips": 3}
skeletons = {}
pipelines = {}
clip_pipelines = {}
for key, entry in sorted(manifest["assets"].items(), key=lambda item: (order.get(item[1]["kind"], 9), item[0])):
    destination = entry["unreal"]
    kind = entry["kind"]
    path = os.path.join(content, entry["file"])
    # A file that the converter wrote again (a new time) is imported again, onto the same assets.
    stamp = os.path.getmtime(path) if os.path.isfile(path) else 0
    known = imported["assets"].get(key, {})
    skeletal = kind == "weapon" and bool(entry.get("skeletal"))
    mark = WEAPON_SKELETAL_PIPELINE if skeletal else WEAPON_PIPELINE
    current = known.get("_pipeline") == mark if kind == "weapon" else True
    if not force and known and known.get("_source") == stamp and current and assets.list_assets(destination, recursive=True):
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
    elif kind == "clips":
        gender = entry["gender"]
        if gender not in skeletons:
            failed.append(key + ": no base skeleton of " + gender)
            continue
        if gender not in clip_pipelines:
            clip_pipelines[gender] = clip_pipeline(gender, unreal.load_asset(skeletons[gender]))
        import_file(path, destination, [clip_pipelines[gender]])
    elif skeletal:
        if "weapon-skeletal" not in pipelines:
            pipelines["weapon-skeletal"] = weapon_skeletal_pipeline()
        # The static mesh of an earlier import of this weapon goes (the client takes the first mesh of the folder).
        if "StaticMesh" in objects_in(destination):
            assets.delete_directory(destination)
        import_file(path, destination, [pipelines["weapon-skeletal"], unreal.SoftObjectPath(GLTF_MATERIALS)])
        usage_others.update(skeletal_usage(destination))
    else:
        if "weapon" not in pipelines:
            pipelines["weapon"] = weapon_pipeline()
        import_file(path, destination, [pipelines["weapon"], unreal.SoftObjectPath(GLTF_MATERIALS)])
    found = objects_in(destination)
    wanted = {"base": "Skeleton", "part": "SkeletalMesh", "weapon": "SkeletalMesh" if skeletal else "StaticMesh",
              "clips": "AnimSequence"}[kind]
    if wanted not in found:
        failed.append(key)
        continue
    found["_source"] = stamp
    if kind == "weapon":
        found["_pipeline"] = mark
    imported["assets"][key] = found
    if kind == "base":
        skeletons[entry["gender"]] = found["Skeleton"][0]
    done += 1

with open(imported_path, "w") as handle:
    json.dump(imported, handle, indent=1, sort_keys=True)
report = {"imported": done, "skipped (exists)": skipped, "failed": len(failed), "failures": failed[:20],
          "base materials copied for the skeletal weapons": sorted(usage_others)}
with open(os.path.join(player, "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC PLAYER IMPORT " + json.dumps(report))
