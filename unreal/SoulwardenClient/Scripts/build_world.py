"""Stage 1.3: the open world level (World Partition) of the main world, in the shared content plugin.

Run through tools/build-world.sh (a region in each call, with import.lock), after tools/convert.sh level and
tools/import-world.sh for the regions.
- The level /SwcGameContent/World/NewWorld (World Partition): made by the first call, loaded by the next calls.
  Streaming: the main runtime partition has cells of CELL_CM and a loading range of RANGE_CM.
- For each region: one Landscape (8 x 8 components of 255 quads, from regions/NAME/landscape.r16, see
  converter/swc_convert/terrain.py; its proxies are always loaded: the terrain to the horizon) with a material
  instance of M_SwcLandscape and the macro color map of the region; the actors of the streaming cells
  (regions/NAME/cells.bin, see converter/swc_convert/cells.py).
- SWC_GAME_CONTENT/world-state.json has the regions in the level; a region in it is not made again
  (SWC_WORLD_FORCE=1 makes the level again from no region).
Environment: SWC_CONTENT, SWC_GAME_CONTENT, SWC_WORLD_REGIONS (the region names), SWC_WORLD_LEVEL (another level
path, for a test), SWC_WORLD_FORCE=1.
"""
import json
import os
import time

import unreal

content = os.environ["SWC_CONTENT"]
plugin = os.environ["SWC_GAME_CONTENT"]
regions = os.environ.get("SWC_WORLD_REGIONS", "").split()
force = os.environ.get("SWC_WORLD_FORCE", "") == "1"
ROOT = "/SwcGameContent"
LEVEL = os.environ.get("SWC_WORLD_LEVEL") or ROOT + "/World/NewWorld"
WORLD = LEVEL.rsplit("/", 1)[0]   # the folder of the level and of its terrain materials
CELL_CM = 25600                   # 256 m: the cells of the converter (cells.py)
RANGE_CM = 102400                 # the meshes load within 1,024 m of a streaming source
assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.SwcEditorLibrary
started = time.time()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)
manifest = json.load(open(os.path.join(content, "manifest.json")))["assets"]
state_path = os.path.join(plugin, "world-state.json" if LEVEL.startswith(ROOT) else "world-state-test.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) and not force else {"level": LEVEL, "regions": {}}


def log(text):
    unreal.log("SWC " + text)


def save_state():
    with open(state_path + ".tmp", "w") as handle:
        json.dump(state, handle, indent=1, sort_keys=True)
    os.replace(state_path + ".tmp", state_path)


def landscape_material():
    """@return The base material of the terrain: the macro color map over the whole Landscape of one region."""
    path = WORLD + "/M_SwcLandscape"
    if assets.does_asset_exist(path):
        return assets.load_asset(path)
    material = tools.create_asset("M_SwcLandscape", WORLD, unreal.Material, unreal.MaterialFactoryNew())
    coords = editing.create_material_expression(material, unreal.MaterialExpressionLandscapeLayerCoords, -700, 0)
    coords.set_editor_property("mapping_scale", 2040.0)   # the quads of a region: UV 0..1 over the Landscape
    macro = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    macro.set_editor_property("parameter_name", "Macro")
    macro.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    editing.connect_material_expressions(coords, "", macro, "UVs")
    editing.connect_material_property(macro, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 300)
    roughness.set_editor_property("r", 0.9)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


def region_material(name, parent):
    """@return The material instance of the Landscape of a region with its macro color map (a texture asset)."""
    path = f"{WORLD}/Terrain/MI_{name.replace('+', '')}"
    instance = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        path.rsplit("/", 1)[1], path.rsplit("/", 1)[0], unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    entry = manifest.get("texture:macro:" + name)
    if entry:
        for asset_path in assets.list_assets(entry["unreal"], recursive=True, include_folder=False):
            texture = assets.load_asset(asset_path)
            if isinstance(texture, unreal.Texture2D):
                editing.set_material_instance_texture_parameter_value(instance, "Macro", texture)
                break
    editing.update_material_instance(instance)
    assets.save_loaded_asset(instance)
    return instance


levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if force or not state["regions"] or not assets.does_asset_exist(LEVEL):
    if assets.does_asset_exist(LEVEL):
        assets.delete_asset(LEVEL)
    if not levels.new_level(LEVEL, True):
        raise RuntimeError("cannot make the level " + LEVEL)
    state["regions"] = {}
    state.pop("environment", None)
    state.pop("ocean", None)
    log(f"level {LEVEL}: new; streaming {library.configure_streaming(CELL_CM, RANGE_CM)}")
else:
    if not levels.load_level(LEVEL):
        raise RuntimeError("cannot load the level " + LEVEL)
    log(f"level {LEVEL}: loaded, {len(state['regions'])} regions in it")
def environment():
    """The sun, the sky, and the fog of the level (always loaded). The look is not a goal now: engine defaults."""
    sun_transform = unreal.Transform()
    sun_transform.rotation = unreal.Rotator(0.0, -50.0, 135.0).quaternion()   # roll, pitch, yaw: 50 degrees high
    for cls, label, transform in ((unreal.DirectionalLight, "sun", sun_transform), (unreal.SkyAtmosphere, "sky", None),
                                  (unreal.SkyLight, "sky light", None), (unreal.ExponentialHeightFog, "fog", None)):
        actor = library.spawn_actor_of_class(cls, transform or unreal.Transform(), label)
        library.set_spatially_loaded(actor, False)
        if cls == unreal.SkyLight:
            component = actor.get_editor_property("light_component")
            component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            component.set_editor_property("real_time_capture", True)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    state["environment"] = True
    save_state()
    log("environment: sun, sky, sky light, fog")


def ocean():
    """A flat ocean at the ocean level of the world (terrain.json: oceanLevel 40 m) over the 14.3 km of the main world
    and 6 km around it (an engine plane; a simple opaque material: the water of milestone 4 comes later)."""
    material_path = WORLD + "/M_SwcOcean"
    if not assets.does_asset_exist(material_path):
        material = tools.create_asset("M_SwcOcean", WORLD, unreal.Material, unreal.MaterialFactoryNew())
        color = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
        color.set_editor_property("constant", unreal.LinearColor(0.02, 0.08, 0.12, 1.0))
        editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 200)
        roughness.set_editor_property("r", 0.15)
        editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        editing.recompile_material(material)
        assets.save_loaded_asset(material)
    size_cm = (14336.0 + 2 * 6000.0) * 100.0
    transform = unreal.Transform()
    transform.translation = unreal.Vector(716800.0, -716800.0, 4000.0)   # the center of the world, 40 m
    transform.scale3d = unreal.Vector(size_cm / 100.0, size_cm / 100.0, 1.0)   # the engine plane is 100 x 100 cm
    plane = library.spawn_mesh_actor(unreal.load_asset("/Engine/BasicShapes/Plane"), transform, "ocean")
    plane.static_mesh_component.set_material(0, unreal.load_asset(material_path))
    plane.static_mesh_component.set_editor_property("cast_shadow", False)
    plane.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    library.set_spatially_loaded(plane, False)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    state["ocean"] = True
    save_state()
    log("environment: ocean at 40 m")


parent = landscape_material()
if not state.get("environment"):
    environment()
if not state.get("ocean"):
    ocean()
for name in state["regions"]:   # a macro color map that the import added later
    region_material(name, parent)
for name in regions:
    if name in state["regions"]:
        log(f"region {name}: in the level")
        continue
    folder = os.path.join(content, "regions", name)
    layout = json.load(open(os.path.join(folder, "landscape.json")))
    phase = time.time()
    landscape = library.create_landscape(os.path.join(folder, layout["file"]), layout["components"],
                                         layout["quads per component"], unreal.Vector(*layout["location cm"]),
                                         unreal.Vector(*layout["scale cm"]), region_material(name, parent),
                                         f"{name} landscape", 2, True)
    landscape_seconds = round(time.time() - phase, 1)
    cells = os.path.join(folder, "cells.bin")
    actors = library.spawn_instance_actors(cells) if os.path.isfile(cells) else 0
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.SystemLibrary.collect_garbage()
    state["regions"][name] = {"landscape": landscape is not None, "cell actors": actors,
                              "landscape seconds": landscape_seconds, "seconds": round(time.time() - phase, 1)}
    save_state()
    log(f"region {name}: {json.dumps(state['regions'][name])}")
report = {"level": LEVEL, "regions in the level": len(state["regions"]), "seconds": round(time.time() - started, 1)}
log("WORLD " + json.dumps(report))
