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
- The materials of a weapon (AST-206): the glTF pipeline makes instances of the Interchange master
  /InterchangeAssets/gltf/M_Default, a content of the installed engine that the editor does not save. Its usage flags (Nanite,
  skeletal mesh) cannot be set, so the instances compiled their shaders at each start and drew the default material of the
  engine. Each instance with a base material outside the content plugin gets the project copy of that base,
  /SwcGameContent/Player/Materials/NAME_Weapon, with the usage flags of WEAPON_USAGES (the same parameters: the look does not
  change). A base material in the content plugin gets the flags. A weapon with no mark WEAPON_USAGE in imported.json gets this
  pass one time, also with no new import.
- A clip pack (stage P2.7): the clips of one weapon tag set as animations on the skeleton of the base of its gender (a copy
  of the glTF asset pipeline with that skeleton and "import only animations"). The client plays them on the base.

The script writes SWC_CONTENT/player/imported.json: for each manifest key, the object paths that the client loads.
Environment: SWC_CONTENT, SWC_IMPORT_FORCE=1 (import each asset again), SWC_IMPORT_MAX_SECONDS (stop after the asset that
passes this time; the report has "incomplete": true, and a next call continues: the chunks of the look preparation of AST-215,
so that the import lock is free between them). The script saves imported.json each 25 imports.
"""
import json
import time
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
# The project copies of the base materials of other places for the weapons (with the usage flags of WEAPON_USAGES).
WEAPON_MATERIALS = "/SwcGameContent/Player/Materials"
# The usage flags of the weapon materials: the weapon meshes are Nanite meshes (static and skeletal), the bow and the flail are
# skeletal meshes (the lines "missing usage flag" of the client logs, tools/fix-usage-flags.sh). The mark of this rule.
WEAPON_USAGES = ("used_with_nanite", "used_with_skeletal_mesh")
WEAPON_USAGE = "nanite-skeletal-1"
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


# The material instances of the glTF pipeline: the parent for each alpha mode of a glTF material (Interchange plugin; each one
# is M_Default with its blend mode, and M_Default has the skeletal usage).
GLTF_PARENTS = "/InterchangeAssets/gltf/MaterialInstances/MI_Default_"
GLTF_ALPHA = {"OPAQUE": "Opaque", "MASK": "Mask", "BLEND": "Blend"}


def gltf_alpha_parents(path, destination):
    """Sets the parent of each glTF material instance of a part to the instance of its alpha mode. A reimport onto the same
    assets keeps the old parent: the head layers (player.py HEAD_LAYER) stayed MI_Default_Opaque after the converter made them
    BLEND. Only an instance whose parent is a MI_Default_ instance changes. @return The number of changed instances."""
    modes = {}
    for material in json.load(open(path)).get("materials", []):
        suffix = "_DS" if material.get("doubleSided") else ""
        modes[material.get("name", "")] = GLTF_PARENTS + GLTF_ALPHA.get(material.get("alphaMode", "OPAQUE"), "Opaque") + suffix
    changed = 0
    for asset_path in objects_in(destination).get("MaterialInstanceConstant", []):
        instance = unreal.load_asset(asset_path)
        wanted = modes.get(instance.get_name())
        parent = instance.get_editor_property("parent")
        if wanted is None or parent is None or not parent.get_path_name().startswith(GLTF_PARENTS):
            continue
        if parent.get_path_name().split(".")[0] != wanted:
            unreal.MaterialEditingLibrary.set_material_instance_parent(instance, unreal.load_asset(wanted))
            assets.save_loaded_asset(instance, only_if_is_dirty=False)
            changed += 1
    return changed


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


def weapon_usage(destination):
    """Gives the materials of a weapon the usage flags of WEAPON_USAGES (a material with no usage flag for its mesh compiles its
    shaders again at each start and draws the default material of the engine). A material instance takes the flags from its base
    material. A base material in the content plugin gets the flags; a base material of another place (the glTF M_Default of the
    Interchange plugin, which the editor does not save) is not changed: the instance gets its project copy in WEAPON_MATERIALS
    with the flags (the same parameters). @return The base materials that got a copy."""
    copied = []
    found = objects_in(destination)
    for path in found.get("Material", []) + found.get("MaterialInstanceConstant", []):
        material = unreal.load_asset(path)
        instance = isinstance(material, unreal.MaterialInstanceConstant)
        base = material.get_base_material() if instance else material
        if base is None:
            continue
        new = False
        if instance and not base.get_path_name().startswith("/SwcGameContent/"):
            copy_path = f"{WEAPON_MATERIALS}/{base.get_name()}_Weapon"
            copy = unreal.load_asset(copy_path) if assets.does_asset_exist(copy_path) else None
            if copy is None:
                copy = assets.duplicate_loaded_asset(base, copy_path)
                new = True
            copied.append(base.get_path_name())
            unreal.MaterialEditingLibrary.set_material_instance_parent(material, copy)
            assets.save_loaded_asset(material, False)
            base = copy
        missing = [name for name in WEAPON_USAGES if not base.get_editor_property(name)]
        for name in missing:
            base.set_editor_property(name, True)
        if missing or new:
            unreal.MaterialEditingLibrary.recompile_material(base)
            assets.save_loaded_asset(base, False)
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


done, skipped, failed, usage_done, parents_done = 0, 0, [], 0, 0
started = time.time()
max_seconds = float(os.environ.get("SWC_IMPORT_MAX_SECONDS", "0") or 0)
incomplete = False


def save_imported():
    """Writes imported.json in one step (a temporary file, then a rename): the client and the converter read it."""
    with open(imported_path + ".tmp", "w") as handle:
        json.dump(imported, handle, indent=1, sort_keys=True)
    os.replace(imported_path + ".tmp", imported_path)


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
    if max_seconds and time.time() - started > max_seconds and kind != "base":
        incomplete = True
        break
    if not force and known and known.get("_source") == stamp and current and assets.list_assets(destination, recursive=True):
        skipped += 1
        if kind == "base":
            skeletons[entry["gender"]] = known["Skeleton"][0]
        if kind == "part" and known.get("_alpha") != stamp:
            # The alpha parents of a part of an earlier import: one time for each source (a mark; with thousands of parts
            # the pass loaded each material at each call).
            parents_done += gltf_alpha_parents(path, destination)
            known["_alpha"] = stamp
        if kind == "weapon" and known.get("_usage") != WEAPON_USAGE:
            usage_others.update(weapon_usage(destination))
            known["_usage"] = WEAPON_USAGE
            usage_done += 1
        continue
    if kind == "part":
        gender = entry["gender"]
        if gender not in skeletons:
            failed.append(key + ": no base skeleton of " + gender)
            continue
        if gender not in pipelines:
            pipelines[gender] = part_pipeline(gender, unreal.load_asset(skeletons[gender]))
        import_file(path, destination, [pipelines[gender], unreal.SoftObjectPath(GLTF_MATERIALS)])
        parents_done += gltf_alpha_parents(path, destination)
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
    else:
        if "weapon" not in pipelines:
            pipelines["weapon"] = weapon_pipeline()
        import_file(path, destination, [pipelines["weapon"], unreal.SoftObjectPath(GLTF_MATERIALS)])
    if kind == "weapon":
        usage_others.update(weapon_usage(destination))
    found = objects_in(destination)
    wanted = {"base": "Skeleton", "part": "SkeletalMesh", "weapon": "SkeletalMesh" if skeletal else "StaticMesh",
              "clips": "AnimSequence"}[kind]
    if wanted not in found:
        failed.append(key)
        continue
    found["_source"] = stamp
    if kind == "part":
        found["_alpha"] = stamp
    if kind == "weapon":
        found["_pipeline"] = mark
        found["_usage"] = WEAPON_USAGE
    imported["assets"][key] = found
    if kind == "base":
        skeletons[entry["gender"]] = found["Skeleton"][0]
    done += 1
    if done % 25 == 0:
        save_imported()
        unreal.SystemLibrary.collect_garbage()

save_imported()
report = {"imported": done, "skipped (exists)": skipped, "failed": len(failed), "failures": failed, "incomplete": incomplete,
          "seconds": round(time.time() - started, 1),
          "weapons with the usage pass only": usage_done, "base materials copied for the weapons": sorted(usage_others),
          "part materials with a new alpha parent": parents_done}
with open(os.path.join(player, "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC PLAYER IMPORT " + json.dumps(report))
