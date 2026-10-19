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
- A region has three steps (the Landscape, the cell actors, the far meshes); "partial" in the state has the steps
  that are done. With SWC_WORLD_CELLS=1, a region in the level whose "cells version" is older than CELLS_VERSION gets
  new cell actors in two steps (destroy the old ones, spawn the new cells file). After a step, a call that is older than SWC_WORLD_MAX_SECONDS stops ("incomplete" in the report): the
  next call continues. Thus a call holds import.lock for less than 10 minutes (docs/WORLD.md).
Environment: SWC_CONTENT, SWC_GAME_CONTENT, SWC_WORLD_REGIONS (the region names), SWC_WORLD_LEVEL (another level
path, for a test), SWC_WORLD_FORCE=1, SWC_WORLD_MAX_SECONDS (default 240).
"""
import json
import math
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
MAX_SECONDS = float(os.environ.get("SWC_WORLD_MAX_SECONDS") or 400)   # then the call stops after its current step
CELLS_AGAIN = os.environ.get("SWC_WORLD_CELLS", "") == "1"
# The rule of the cell actors: 1 = stage 1.3; 2 = P4.1 (a placement in the cell of its place, the depleted meshes of the
# gathering nodes left out, the hidden collision instances of the game, no collision on the render instances; 3: a
# distribution row places the meshes of its variant; 4: the material overrides of the placements, cells file version 2;
# 5: the instances of the instanced mesh components of the slices, S0 of research/settlements.md).
CELLS_VERSION = 5
# The terrain material of the game (world fidelity T1, SWC_WORLD_TERRAIN=1): a region with a terrain plan with slots
# gets its Landscape again, with one target layer for each slot (the raw weights of its splat) and the material instance
# MI_SwcTerrain_NAME (tools/import-terrain.sh). Version 1: the macro color map only (M_SwcLandscape).
TERRAIN_AGAIN = os.environ.get("SWC_WORLD_TERRAIN", "") == "1"
TERRAIN_VERSION = 2
TERRAIN = ROOT + "/Terrain"
CELL_CHUNK = 8   # cell actors in one spawn step of the cells again (about 1 minute for the dense town of Oxboro)
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
    state.pop("exposure", None)
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
    plane.static_mesh_component.set_collision_profile_name("NoCollision")   # no ground on the water (the backend swims)
    library.set_spatially_loaded(plane, False)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    state["ocean"] = True
    save_state()
    log("environment: ocean at 40 m")


def no_collision_fix():
    """The ocean and the far meshes of levels built before: the collision profile NoCollision (set_collision_enabled
    did not stay in the saved actors)."""
    changed = 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        if actor.get_actor_label() != "ocean" and unreal.Name("SwcImpostor") not in actor.get_editor_property("tags"):
            continue
        component = actor.static_mesh_component
        if str(component.get_collision_profile_name()) != "NoCollision":
            component.set_collision_profile_name("NoCollision")
            changed += 1
    if changed:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log(f"no collision for {changed} ocean or far mesh actors")


def empty_folders():
    """@return The asset folders of the meshes that are "empty" in the manifest (nothing to draw since mesh rule 6):
    the folder of their old static mesh asset (manifest.unreal_folder("static", key))."""
    import hashlib
    import re
    folders = []
    for key, entry in manifest.items():
        if entry.get("kind") == "empty":
            stem = re.sub(r"[^A-Za-z0-9_]", "_", key.rsplit("/", 1)[-1].rsplit(".", 1)[0])[:40]
            folders.append(f"{ROOT}/Meshes/{stem}_{hashlib.sha1(key.encode()).hexdigest()[:6]}")
    return folders


def remove_empty(name, folders):
    """Removes the instance components of the empty meshes from the cell actors of a region built before."""
    result = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    descs = result[1] if isinstance(result, tuple) else result
    guids = [d.guid for d in descs if str(d.label).startswith(f"{name} cell ")]
    if not guids:
        return 0
    unreal.WorldPartitionBlueprintLibrary.load_actors(guids)
    removed = 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if actor.get_actor_label().startswith(f"{name} cell "):
            removed += library.remove_instance_components_of(actor, folders)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.WorldPartitionBlueprintLibrary.unload_actors(guids)
    return removed


EXPOSURE_BIAS = -1.0


def exposure():
    """An unbound post-process volume with manual exposure (always loaded): with the auto exposure of the game the
    sunny ground was white (the brightest surface; in the unlit view it had its color). The look is not a goal now:
    a fixed exposure for the default sun (10 lux). EXPOSURE_BIAS -1: at 0 the ground (macro color map) was still
    pale (evidence 1.3/test, town)."""
    volume = next((actor for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
                   if isinstance(actor, unreal.PostProcessVolume) and actor.get_actor_label() == "exposure"), None)
    if volume is None:
        volume = library.spawn_actor_of_class(unreal.PostProcessVolume, unreal.Transform(), "exposure")
    volume.set_editor_property("unbound", True)
    settings = volume.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    volume.set_editor_property("settings", settings)
    library.set_spatially_loaded(volume, False)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    state["exposure"] = EXPOSURE_BIAS
    save_state()
    log(f"environment: manual exposure, bias {EXPOSURE_BIAS}")


parent = landscape_material()
no_collision_fix()
if state.get("exposure") != EXPOSURE_BIAS:
    exposure()
folders = empty_folders()
for name, done in state["regions"].items():   # mesh rule 6 made some meshes empty: remove them from older cells
    if done.get("empty removed") != len(folders):
        done["empty removed"] = len(folders)
        log(f"region {name}: {remove_empty(name, folders)} components of empty meshes removed")
        save_state()
if not state.get("environment"):
    environment()
if not state.get("ocean"):
    ocean()
def add_impostors(name):
    """The far meshes of the game of a region (groups of 512 m, converter terrain.impostors): always loaded actors
    with the tag SwcImpostor; the client shows a group only beyond the loading range (USwcWorldSubsystem).
    @return The number of groups."""
    count = 0
    prefix = f"impostor:{name}:"
    for key, entry in sorted(manifest.items()):
        if not key.startswith(prefix):
            continue
        mesh = None
        for asset_path in assets.list_assets(entry["unreal"], recursive=True, include_folder=False):
            candidate = assets.load_asset(asset_path)
            if isinstance(candidate, unreal.StaticMesh):
                mesh = candidate
                break
        if mesh is None:
            continue
        x, y, z = entry["origin m"]
        transform = unreal.Transform()
        transform.translation = unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)   # the space map of stage 1.1
        actor = library.spawn_mesh_actor(mesh, transform, f"{name} {key.split(':')[2]} {key.split(':')[3]}")
        actor.set_editor_property("tags", [unreal.Name("SwcImpostor")])
        component = actor.static_mesh_component
        component.set_editor_property("cast_shadow", False)
        component.set_collision_profile_name("NoCollision")
        library.set_spatially_loaded(actor, False)
        count += 1
    return count


for name, done in state["regions"].items():   # a macro color map that the import added later (once: each update and
    if done.get("macro material"):               # save of a material instance takes about 2.5 s)
        continue
    region_material(name, parent)
    if manifest.get("texture:macro:" + name):
        done["macro material"] = True
        save_state()
for name, done in state["regions"].items():   # the far meshes of a region that was built before them
    if not done.get("impostors"):
        count = add_impostors(name)   # 0 while the import of the region has not taken them yet: retried
        if count:
            done["impostors"] = count
            unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
            save_state()
            log(f"region {name}: {count} impostor groups")
def out_of_time():
    return time.time() - started > MAX_SECONDS


def destroy_cells(name):
    """Destroys the cell actors of a region in the level (a new cells file replaces them). @return The count."""
    result = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    descs = result[1] if isinstance(result, tuple) else result
    guids = [d.guid for d in descs if str(d.label).startswith(f"{name} cell ")]
    if not guids:
        return 0
    destroyed.update(str(g) for g in guids)   # their descriptors stay in this session (check_bounds skips them)
    unreal.WorldPartitionBlueprintLibrary.load_actors(guids)
    count = 0
    files = []
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if actor.get_actor_label().startswith(f"{name} cell "):
            package = actor.get_outermost().get_name()   # the external actor package (/SwcGameContent/__ExternalActors__/...)
            if library.destroy_actor(actor):
                count += 1
                files.append(os.path.join(plugin, "Content", package[len(ROOT) + 1:] + ".uasset"))
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.SystemLibrary.collect_garbage()
    # The save of a commandlet keeps the files of destroyed external actors (the next load had them again): delete them.
    for path in files:
        if os.path.isfile(path):
            os.remove(path)
    return count


destroyed = set()   # the guids of the cell actors that this call destroyed
# The regions that this call changed: their descriptors in this session have the bounds of the spawn time, while the
# meshes still compile (async: UpdateBounds waits, the bounds stay at the world origin). The saved descriptors have the
# right bounds (r_+01_+03, a new session: largest distance 99 m). check_bounds reads only regions of earlier calls.
touched = set()


def check_bounds(name):
    """The check of the cell actors of a region in the actor descriptors of the level (World Partition streams them by
    these bounds; a call with -nullrhi makes them from the saved mesh bounds). Writes the bounds of each actor (cm) to
    SWC_WORK/logs/cell-bounds/NAME.json for the offline compare with the meshes. @return A report."""
    result = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    descs = result[1] if isinstance(result, tuple) else result
    rx, ry = (int(v) for v in name[2:].split("_"))
    count, empty, far, beyond = 0, 0, 0.0, 0
    rows = {}
    for d in descs:
        label = str(d.label)
        if not label.startswith(f"{name} cell ") or str(d.guid) in destroyed:
            continue
        count += 1
        lo, hi = d.bounds.min, d.bounds.max
        rows[label] = [[lo.x, lo.y, lo.z], [hi.x, hi.y, hi.z]]
        if hi.x - lo.x <= 0 or hi.y - lo.y <= 0:
            empty += 1
            continue
        x, y = (int(v) for v in label.split()[-2:])
        cx, cy = (rx * 2048.0 + (x + 0.5) * 256.0) * 100.0, -(ry * 2048.0 + (y + 0.5) * 256.0) * 100.0
        distance = math.hypot((lo.x + hi.x) / 2 - cx, (lo.y + hi.y) / 2 - cy) / 100.0
        far = max(far, distance)
        beyond += distance > 500.0
    folder = os.path.join(os.environ.get("SWC_WORK", os.path.expanduser("~/Games/soulwarden-client")), "logs", "cell-bounds")
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, name + ".json"), "w") as out:
        json.dump(rows, out)
    return {"cell actors in the descriptors": count, "with empty bounds": empty,
            "bounds centers more than 500 m from the cell center": beyond,
            "largest distance of a bounds center from its cell center (m)": round(far, 1)}


def landscape_again(name):
    """Destroys the Landscape of a region (the Landscape actor and its streaming proxies, and their files) and makes it
    again with the target layers of its terrain plan. @return A report, or None when the region has no plan with slots."""
    plan_path = os.path.join(content, "regions", name, "terrain-plan.json")
    if not os.path.isfile(plan_path):
        return None
    plan = json.load(open(plan_path))
    instance_path = f"{TERRAIN}/Regions/{name}/MI_SwcTerrain_{name}"
    if not plan["slots"]:
        return None
    if not assets.does_asset_exist(instance_path):
        return {"landscape": False, "error": "no material instance: run tools/import-terrain.sh --regions " + name}
    folder = os.path.join(content, "regions", name)
    layout = json.load(open(os.path.join(folder, "landscape.json")))
    phase = time.time()
    files = []
    removed = 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        label = actor.get_actor_label()
        if label in (f"{name} landscape", f"{name} landscape proxy"):
            package = actor.get_outermost().get_name()
            if library.destroy_actor(actor):
                removed += 1
                if package.startswith(ROOT + "/__ExternalActors__/"):
                    files.append(os.path.join(plugin, "Content", package[len(ROOT) + 1:] + ".uasset"))
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.SystemLibrary.collect_garbage()
    for path in files:
        if os.path.isfile(path):
            os.remove(path)
    names = [slot["slot"] for slot in plan["slots"]]
    weights = [os.path.join(folder, slot["weights"]) for slot in plan["slots"]]
    landscape = library.create_landscape_with_layers(os.path.join(folder, layout["file"]), layout["components"],
                                                     layout["quads per component"], unreal.Vector(*layout["location cm"]),
                                                     unreal.Vector(*layout["scale cm"]), assets.load_asset(instance_path),
                                                     f"{name} landscape", 2, True, names, weights, f"{TERRAIN}/LayerInfo")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.SystemLibrary.collect_garbage()
    return {"landscape": landscape is not None, "old actors removed": removed, "layers": len(names),
            "terrain seconds": round(time.time() - phase, 1)}


incomplete = False
partial = state.setdefault("partial", {})
for name in regions:
    done = state["regions"].get(name)
    if done is not None and TERRAIN_AGAIN and done.get("terrain version", 1) < TERRAIN_VERSION:
        if out_of_time():
            incomplete = True
            break
        result = landscape_again(name)
        if result is None or result["landscape"]:
            done["terrain version"] = TERRAIN_VERSION   # (None: no layer list, the macro Landscape stays)
        save_state()
        log(f"region {name}: terrain again {json.dumps(result)}")
        continue
    if done is not None and CELLS_AGAIN and done.get("cells version", 1) < CELLS_VERSION:
        cells = os.path.join(content, "regions", name, "cells.bin")
        total = library.count_instance_actors(cells) if os.path.isfile(cells) else 0
        if "cells destroyed" not in done:
            if out_of_time():
                incomplete = True
                break
            phase = time.time()
            done["cells destroyed"] = destroy_cells(name)
            done["cells spawned"] = 0
            done["cells seconds"] = 0
            save_state()
            log(f"region {name}: {done['cells destroyed']} old cell actors destroyed ({time.time() - phase:.1f} s)")
        # The spawn in chunks of CELL_CHUNK actors, each with a time check (a call holds import.lock less than 10 min).
        while done.get("cells spawned", 0) < max(total, 0):
            if out_of_time():
                incomplete = True
                break
            phase = time.time()
            spawned = library.spawn_instance_actor_range(cells, done["cells spawned"], CELL_CHUNK)
            unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
            unreal.SystemLibrary.collect_garbage()
            if spawned < 0:
                log(f"region {name}: the cells file cannot be read")
                break
            done["cells spawned"] += spawned
            done["cells seconds"] = round(done.get("cells seconds", 0) + time.time() - phase, 1)
            save_state()
            log(f"region {name}: {done['cells spawned']} of {total} cell actors ({time.time() - phase:.1f} s)")
            if spawned == 0:
                break
        if incomplete:
            break
        done["cell actors"] = max(total, 0)
        done["cells version"] = CELLS_VERSION
        done.pop("cells destroyed", None)
        done.pop("cells spawned", None)
        save_state()
        log(f"region {name}: cells again {json.dumps(done)}")
        touched.add(name)
        continue
    if name in state["regions"]:
        log(f"region {name}: in the level")
        continue
    if out_of_time():
        incomplete = True
        break
    folder = os.path.join(content, "regions", name)
    steps = partial.setdefault(name, {})
    if "landscape" not in steps:
        layout = json.load(open(os.path.join(folder, "landscape.json")))
        phase = time.time()
        landscape = library.create_landscape(os.path.join(folder, layout["file"]), layout["components"],
                                             layout["quads per component"], unreal.Vector(*layout["location cm"]),
                                             unreal.Vector(*layout["scale cm"]), region_material(name, parent),
                                             f"{name} landscape", 2, True)
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        unreal.SystemLibrary.collect_garbage()
        steps.update({"landscape": landscape is not None, "landscape seconds": round(time.time() - phase, 1)})
        save_state()
        log(f"region {name}: landscape {json.dumps(steps)}")
        if out_of_time():
            incomplete = True
            break
    if "cell actors" not in steps:
        phase = time.time()
        cells = os.path.join(folder, "cells.bin")
        actors = library.spawn_instance_actors(cells) if os.path.isfile(cells) else 0
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        unreal.SystemLibrary.collect_garbage()
        steps.update({"cell actors": actors, "cells seconds": round(time.time() - phase, 1)})
        save_state()
        log(f"region {name}: cells {json.dumps(steps)}")
        if out_of_time():
            incomplete = True
            break
    phase = time.time()
    impostor_groups = add_impostors(name)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.SystemLibrary.collect_garbage()
    state["regions"][name] = dict(steps, impostors=impostor_groups, **{"empty removed": len(folders), "cells version": CELLS_VERSION,
                                                                      "impostors seconds": round(time.time() - phase, 1)})
    partial.pop(name)
    save_state()
    log(f"region {name}: {json.dumps(state['regions'][name])}")
# The bounds of the regions that have the current cells and no bounds file yet, from their saved descriptors.
bounds_folder = os.path.join(os.environ.get("SWC_WORK", os.path.expanduser("~/Games/soulwarden-client")), "logs", "cell-bounds")
for name, done in state["regions"].items():
    if done.get("cells version", 1) >= CELLS_VERSION and "cells destroyed" not in done and name not in touched and \
            not os.path.isfile(os.path.join(bounds_folder, name + ".json")):
        log(f"region {name}: bounds {json.dumps(check_bounds(name))}")
report = {"level": LEVEL, "regions in the level": len(state["regions"]), "incomplete": incomplete,
          "seconds": round(time.time() - started, 1)}
log("WORLD " + json.dumps(report))
