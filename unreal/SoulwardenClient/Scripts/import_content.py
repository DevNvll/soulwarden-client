"""Imports the converted content (SWC_CONTENT/manifest.json) into the shared content plugin /SwcGameContent.

Run through tools/import.sh (UnrealEditor-Cmd -run=pythonscript, no window). The imported assets are made from the
game copy: the plugin is outside the repository (tools/content-link.sh), one copy for all checkouts.

Shared assets (stage 1.2):
- Base materials (made by this script): /SwcGameContent/Base/M_SwcOpaque, M_SwcMasked, M_SwcTranslucent, with the
  parameters BaseColor, BaseColorFactor, and Normal. The look is not a goal now.
- Textures: each texture is one asset (/SwcGameContent/Textures/STEM_HASH).
- Materials: each material record is one material instance (/SwcGameContent/Materials/m_HASH) of a base material.
  Then the materials track (Scripts/swc_materials.py) applies the spec of each instance that step 4 made or set again
  (SWC_CONTENT/materials/instances.json; a key with no spec keeps the rules of stage 1.2).
- Static meshes: glTF with no material and no texture import; each slot has the name of a material record, and the
  script gives it that material instance. A mesh with a translucent material has no Nanite.
- Characters and test assets: the default glTF pipeline (their own materials and textures).
- Collision (P4.1): a static mesh with a collision file (the physics file of the game) gets its triangles as the
  complex collision: a collision-only mesh in /SwcGameContent/Collision/STEM_HASH. A mesh with no collision file gets
  no collision. The state key is "collision:KEY".

Incremental: SWC_GAME_CONTENT/import-state.json keeps the hash of each imported asset. An asset imports again when its
hash changed (a new conversion) or SWC_IMPORT_FORCE=1.
Environment: SWC_CONTENT, SWC_GAME_CONTENT, SWC_IMPORT_FILTER (a text that the asset key must contain),
SWC_IMPORT_REGIONS (region names: only the meshes of these regions and their materials and textures),
SWC_IMPORT_KEYS (a file with a JSON list of manifest keys: only these assets and their materials and textures),
SWC_IMPORT_FORCE=1, SWC_IMPORT_MAX_SECONDS (stop after the batch that passes this time: the report has
"incomplete": true, and a next call continues; a long import releases the lock between calls).
"""
import json
import os
import sys
import time

import unreal

content = os.environ["SWC_CONTENT"]
plugin = os.environ["SWC_GAME_CONTENT"]
wanted = os.environ.get("SWC_IMPORT_FILTER", "")
regions = os.environ.get("SWC_IMPORT_REGIONS", "").split()
keys_file = os.environ.get("SWC_IMPORT_KEYS", "")
listed = set(json.load(open(keys_file))) if keys_file else set()
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
max_seconds = float(os.environ.get("SWC_IMPORT_MAX_SECONDS") or 0)
manifest = json.load(open(os.path.join(content, "manifest.json")))["assets"]
state_path = os.path.join(plugin, "import-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) else {}
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
library = unreal.SwcEditorLibrary
ROOT = "/SwcGameContent"
BASE = ROOT + "/Base"
BASE_VERSION = "1"   # the rule of the base materials and of the material instances
PIPELINE_VERSION = "5"   # the rule of the mesh pipelines (3: SwcMaterialLinkPipeline; 4: no distance field, no cards;
                         # 5: no generated collision: section 6 gives the collision of the game)
COLLISION_VERSION = "6"  # the rule of section 6 (tools/import-world.sh has the same value; 2: the cook waits for the
                         # build of the collision mesh, version 1 cooked empty bodies; 3: no distance field, no Lumen
                         # cards, no ray tracing for the collision meshes; 4: the render mesh has no collision and no
                         # complex collision mesh, the collision mesh has its own collision, the cells place it;
                         # 5: the corners keep their order, version 4 and before had the collision meshes inside out;
                         # 6: an old collision mesh is deleted first: a new build of an existing one kept its old
                         # triangles)
COLLISION_ROOT = "/SwcGameContent/Collision"
started = time.time()
# A commandlet does not scan the content of a plugin: scan it before any question to the asset registry.
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)


def log(text):
    unreal.log("SWC " + text)


def save_state():
    with open(state_path + ".tmp", "w") as handle:
        json.dump(state, handle, indent=0, sort_keys=True)
    os.replace(state_path + ".tmp", state_path)


def file_stamp(path):
    info = os.stat(path)
    return f"{info.st_size}:{info.st_mtime_ns}"


def first_asset(folder, class_name):
    for path in assets.list_assets(folder, recursive=True, include_folder=False):
        if str(assets.find_asset_data(path).asset_class_path.asset_name) == class_name:
            return assets.load_asset(path)
    return None


def import_file(path, destination, pipelines=None):
    """Imports one file into a folder (the default pipelines, or a list of pipeline paths)."""
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
    parameters.set_editor_property("override_pipelines", [unreal.SoftObjectPath(p) for p in pipelines])
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
        destination, unreal.InterchangeManager.create_source_data(path), parameters)


# 1. The selection: the meshes (by filter or region), then their materials and textures.
selected = set()
if regions:
    for name in regions:
        selected.update(json.load(open(os.path.join(content, "regions", name, "meshes.json"))))
        selected.add("texture:macro:" + name)   # the macro color map of the terrain of the region (stage 1.3)
        selected.update(k for k in manifest if k.startswith(f"impostor:{name}:"))   # the far meshes of the region
for key, entry in manifest.items():
    if entry["kind"] in ("texture", "material"):
        continue
    if regions and key not in selected:
        continue
    if keys_file and key not in listed:
        continue
    if wanted and wanted not in key:
        continue
    selected.add(key)
# A listed material or texture key (for example the material overrides of the placements: overrides.py) is selected too.
selected.update(k for k in listed if k in manifest and manifest[k]["kind"] in ("material", "texture"))
selected = {k for k in selected if k in manifest}
for key in list(selected):
    for material in manifest[key].get("materials", []):
        selected.add("material:" + material)
for key in list(selected):
    entry = manifest[key]
    if entry["kind"] == "material":
        for role in ("base", "normal"):
            if role in entry:
                selected.add(entry[role])
by_kind = {}
for key in sorted(selected):
    by_kind.setdefault(manifest[key]["kind"], []).append(key)
log("selected " + json.dumps({k: len(v) for k, v in by_kind.items()}))
report = {"imported": {}, "skipped (up to date)": {}, "failed": [], "seconds": {}, "incomplete": False}


def out_of_time():
    if max_seconds and time.time() - started > max_seconds:
        report["incomplete"] = True
    return report["incomplete"]


def count(kind, done):
    bucket = report["imported" if done else "skipped (up to date)"]
    bucket[kind] = bucket.get(kind, 0) + 1


# 2. The base materials and the mesh pipelines (made once; BASE_VERSION makes them again).
def base_material(name, blend):
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path) and state.get("base:" + name) == BASE_VERSION and not force:
        return assets.load_asset(path)
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    material = tools.create_asset(name, BASE, unreal.Material, unreal.MaterialFactoryNew())
    color = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, -100)
    color.set_editor_property("parameter_name", "BaseColor")
    color.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    factor = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 200)
    factor.set_editor_property("parameter_name", "BaseColorFactor")
    factor.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    product = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -350, 0)
    editing.connect_material_expressions(color, "RGB", product, "A")
    editing.connect_material_expressions(factor, "RGB", product, "B")
    editing.connect_material_property(product, "", unreal.MaterialProperty.MP_BASE_COLOR)
    normal = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, 450)
    normal.set_editor_property("parameter_name", "Normal")
    normal.set_editor_property("texture", unreal.load_asset("/Engine/EngineMaterials/FlatNormal"))
    normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    editing.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 300)
    roughness.set_editor_property("r", 0.8)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    if blend == "masked":
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        editing.connect_material_property(color, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    elif blend == "translucent":
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        editing.connect_material_property(factor, "A", unreal.MaterialProperty.MP_OPACITY)
    material.set_editor_property("used_with_nanite", blend != "translucent")
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["base:" + name] = BASE_VERSION
    return material


def mesh_pipeline(name, nanite):
    """@return The path of a glTF pipeline for static meshes (an asset in Base): no new material and no texture. The
    pipeline SwcMaterialLinkPipeline (after it) gives each slot the material instance of its name
    (/SwcGameContent/Materials) before the mesh is built. (A material that a script sets after the import makes Unreal
    build the mesh a second time: Nanite, distance field; a large merged mesh then took about 30 s. The search of
    the generic pipeline for existing materials took about one second for each mesh.)"""
    path = f"{BASE}/{name}"
    if not assets.does_asset_exist(path) or state.get("base:" + name) != PIPELINE_VERSION or force:
        if assets.does_asset_exist(path):
            assets.delete_asset(path)
        pipeline = assets.duplicate_loaded_asset(unreal.load_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline"), path)
        materials = pipeline.get_editor_property("material_pipeline")
        materials.set_editor_property("import_materials", True)   # keeps the material nodes for the link pipeline
        materials.set_editor_property("reuse_existing_materials", False)
        materials.set_editor_property("create_new_materials", False)
        materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
        meshes = pipeline.get_editor_property("mesh_pipeline")
        meshes.set_editor_property("build_nanite", nanite)
        # No mesh distance field and no Lumen cards: the build was 6 times slower with them (20 meshes: 80 s against
        # 14 s), and the look is not a goal now (Lumen can use hardware ray tracing).
        meshes.set_editor_property("distance_field_resolution_scale", 0.0)
        meshes.set_editor_property("max_lumen_mesh_cards", 0)
        meshes.set_editor_property("import_skeletal_meshes", False)
        meshes.set_editor_property("collision", False)   # no 18-DOP: the collision of the game (section 6)
        pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
        assets.save_loaded_asset(pipeline)
        state["base:" + name] = PIPELINE_VERSION
    return path


parents = {}
pipelines = {}
if by_kind.get("material") or by_kind.get("static"):
    parents = {"opaque": base_material("M_SwcOpaque", "opaque"), "masked": base_material("M_SwcMasked", "masked"),
               "translucent": base_material("M_SwcTranslucent", "translucent")}
    link = unreal.new_object(unreal.SwcMaterialLinkPipeline)   # a transient object: Interchange copies it
    link.set_editor_property("material_folder", ROOT + "/Materials")
    pipelines = {nanite: [mesh_pipeline(name, nanite), link.get_path_name()]
                 for nanite, name in ((True, "P_SwcMeshNanite"), (False, "P_SwcMesh"))}
    save_state()

# 3. Textures.
phase = time.time()
for key in by_kind.get("texture", []):
    if out_of_time():
        break
    entry = manifest[key]
    path = os.path.join(content, entry["file"])
    stamp = file_stamp(path)
    folder = entry["unreal"]
    if not force and state.get(key) == stamp and assets.list_assets(folder, recursive=True):
        count("texture", False)
        continue
    import_file(path, folder)
    texture = first_asset(folder, "Texture2D")
    if texture is None:
        report["failed"].append(key)
        continue
    normal = entry["role"] == "normal"
    texture.set_editor_property("srgb", not normal)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP if normal
                                else unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP if normal
                                else unreal.TextureGroup.TEXTUREGROUP_WORLD)
    assets.save_loaded_asset(texture)
    state[key] = stamp
    count("texture", True)
    if sum(report["imported"].values()) % 200 == 0:
        save_state()
save_state()
report["seconds"]["textures"] = round(time.time() - phase, 1)

# 4. Material instances.
phase = time.time()
texture_cache = {}


def texture_of(key):
    if key not in texture_cache:
        texture_cache[key] = first_asset(manifest[key]["unreal"], "Texture2D") if key in manifest else None
    return texture_cache[key]


made_materials = []   # the keys that this step made or set again (the materials track applies their specs below)
for key in by_kind.get("material", []):
    if report["incomplete"]:
        break
    entry = manifest[key]
    name = entry["name"]
    path = f"{entry['unreal']}/{name}"
    stamp = BASE_VERSION
    if not force and state.get(key) == stamp and assets.does_asset_exist(path):
        count("material", False)
        continue
    instance = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        name, entry["unreal"], unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parents[entry["blend"]])
    for role, parameter in (("base", "BaseColor"), ("normal", "Normal")):
        texture = texture_of(entry[role]) if role in entry else None
        if texture is not None:
            editing.set_material_instance_texture_parameter_value(instance, parameter, texture)
    editing.set_material_instance_vector_parameter_value(instance, "BaseColorFactor", unreal.LinearColor(*entry["factor"]))
    overrides = instance.get_editor_property("base_property_overrides")
    overrides.set_editor_property("override_two_sided", bool(entry.get("two sided")))
    overrides.set_editor_property("two_sided", bool(entry.get("two sided")))
    if entry["blend"] == "masked":
        overrides.set_editor_property("override_opacity_mask_clip_value", True)
        overrides.set_editor_property("opacity_mask_clip_value", float(entry.get("cutoff", 0.5)))
    instance.set_editor_property("base_property_overrides", overrides)
    editing.update_material_instance(instance)
    assets.save_loaded_asset(instance)
    state[key] = stamp
    count("material", True)
    made_materials.append(key)
# The rules of the materials track (Scripts/swc_materials.py, docs/MATERIALS.md): the spec of each instance that this
# step made or set again replaces the rules above (a key with no spec keeps them).
try:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import swc_materials
    track = swc_materials.apply_specs(made_materials, state)
    report["failed"].extend(track["failed"])
    report["materials track"] = {k: track[k] for k in ("textures", "instances")}
except Exception as error:
    report["failed"].append(f"materials track: {str(error)[:160]}")
save_state()
report["seconds"]["materials"] = round(time.time() - phase, 1)

# 5. Static meshes (and the characters and test assets with the default glTF pipeline). Interchange builds a static
# mesh after the import on worker threads: the script imports a batch, then saves it (a save waits for the build) and
# collects the garbage, so that the builds of a batch run at the same time and the memory stays flat.
phase = time.time()
missing_materials = set()   # slots with no material instance of their name
BATCH = 16
batch = []


def flush():
    for key, kind, folder, stamp in batch:
        try:
            assets.save_directory(folder, only_if_is_dirty=False, recursive=True)
            if kind == "static":
                mesh = first_asset(folder, "StaticMesh")
                if mesh is None:
                    raise RuntimeError("no static mesh")
                for slot in mesh.get_editor_property("static_materials"):
                    material = slot.get_editor_property("material_interface")
                    if material is None or not material.get_path_name().startswith(ROOT + "/Materials/"):
                        missing_materials.add(str(slot.get_editor_property("material_slot_name")))
            elif not assets.list_assets(folder, recursive=True):
                raise RuntimeError("nothing imported")
        except Exception as error:
            report["failed"].append(f"{key}: {str(error)[:120]}")
            continue
        state[key] = stamp
        count(kind, True)
    batch.clear()
    save_state()
    unreal.SystemLibrary.collect_garbage()


for kind in ("static", "character", "test"):
    for key in by_kind.get(kind, []):
        if report["incomplete"]:
            break
        entry = manifest[key]
        folder = entry["unreal"]
        path = os.path.join(content, entry["file"])
        stamp = entry.get("hash") or file_stamp(path)
        if not force and state.get(key) == stamp and assets.list_assets(folder, recursive=True):
            count(kind, False)
            continue
        try:
            if kind == "static":
                import_file(path, folder, pipelines[bool(entry.get("nanite", True))])
            else:
                # The default glTF pipeline reuses the material instances of an earlier import (bReuseExistingMaterials):
                # a new color factor or alpha mode of the glTF (the creature bodies, stage P3.C) did not come in. Remove them.
                for stale in assets.list_assets(folder, recursive=True):
                    if "/Materials/" in stale:
                        assets.delete_asset(stale.split(".")[0])
                import_file(path, folder)
        except Exception as error:
            report["failed"].append(f"{key}: {str(error)[:120]}")
            continue
        batch.append((key, kind, folder, stamp))
        if len(batch) >= BATCH:
            flush()
            out_of_time()
            done = sum(report["imported"].get(k, 0) for k in ("static", "character", "test"))
            if done % 160 == 0:
                log(f"progress {json.dumps(report['imported'])} {round(time.time() - started)} s")
flush()
save_state()
report["seconds"]["meshes"] = round(time.time() - phase, 1)

# 6. The collision of the game (P4.1, converter/swc_convert/collision.py): a static mesh with a collision file gets its
# triangles as the complex collision (a collision-only mesh in COLLISION_ROOT); a mesh with none gets no collision (the
# game has no physics file for it: plants, cloth, small props). Only the mesh asset changes: the cells of the level
# take the collision of their meshes.
phase = time.time()
changed = []
# A render mesh of version 1 to 3 has a complex collision mesh: its async post-load can cook while that mesh compiles
# (a crash in GetPhysicsTriMeshDataCheckComplex). Synchronous mesh compilation loads them safely.
unreal.SystemLibrary.execute_console_command(None, "Editor.AsyncStaticMeshCompilation 0")
for key in by_kind.get("static", []):
    if report["incomplete"] or out_of_time():
        break
    entry = manifest[key]
    record = entry.get("collision")
    wanted_state = (record["hash"] if record else "none") + ":" + COLLISION_VERSION
    if not force and state.get("collision:" + key) == wanted_state:
        count("collision", False)
        continue
    mesh = first_asset(entry["unreal"], "StaticMesh")
    if mesh is None:
        continue   # not imported
    folder = COLLISION_ROOT + "/" + entry["unreal"].rsplit("/", 1)[1]
    collision_path = folder + "/C_" + mesh.get_name()
    if assets.does_asset_exist(collision_path):
        assets.delete_asset(collision_path)   # a new build of an existing collision mesh kept its old triangles
    if not library.set_game_collision(mesh, os.path.join(content, record["file"]) if record else "", collision_path):
        report["failed"].append(f"collision {key}: the collision file cannot be read")
        continue
    changed.append((key, wanted_state, mesh.get_path_name(), folder if record else None))
    if len(changed) >= 50:
        for done_key, done_state, mesh_path, collision_folder in changed:
            assets.save_asset(mesh_path, only_if_is_dirty=False)
            if collision_folder:
                assets.save_directory(collision_folder, only_if_is_dirty=False, recursive=True)
            state["collision:" + done_key] = done_state
            count("collision", True)
        changed.clear()
        save_state()
        unreal.SystemLibrary.collect_garbage()
for done_key, done_state, mesh_path, collision_folder in changed:
    assets.save_asset(mesh_path, only_if_is_dirty=False)
    if collision_folder:
        assets.save_directory(collision_folder, only_if_is_dirty=False, recursive=True)
    state["collision:" + done_key] = done_state
    count("collision", True)
save_state()
report["seconds"]["collision"] = round(time.time() - phase, 1)
report["slots with no material"] = sorted(missing_materials)[:20]
report["seconds"]["all"] = round(time.time() - started, 1)
report["failures"] = report["failed"][:20]
report["failed"] = len(report["failed"])
with open(os.path.join(content, "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
log("IMPORT " + json.dumps(report))
