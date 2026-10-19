"""The render check of a stage of the materials track (docs/MATERIALS.md): the proof meshes in three variants.

Run: tools/convert-materials.sh proof-plan STAGE, then tools/material-proof.sh STAGE (tools/editor-script.sh --render
with the import lock). Each row of the plan is one mesh, placed three times along X: "stage 1.2" (transient
instances of the old base materials with the manifest record of each slot key: the look before the stage), "now"
(the material instances of /SwcGameContent/Materials as they are now), and "override" (the instances of the material
override of the region data, for example the fall leaves of Oxboro) or "candidate" (transient instances of the specs
of the stages under check, instances-candidate.json; this script imports their textures). Two cameras: the sun behind the
camera (front) and the sun behind the meshes (back: the transmitted light). The level and the test instances are not
saved. Pictures and the report go to SWC_EVIDENCE (default SWC_WORK/evidence/WF3/STAGE).
Environment: SWC_CONTENT, SWC_WORK, SWC_PROOF_STAGE, SWC_EVIDENCE.
"""
import json
import math
import os
import sys
import time

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

content = os.environ["SWC_CONTENT"]
stage = os.environ.get("SWC_PROOF_STAGE", "m1")
evidence = os.environ.get("SWC_EVIDENCE") or os.path.join(os.environ["SWC_WORK"], "evidence", "WF3", stage)
os.makedirs(evidence, exist_ok=True)
plan = json.load(open(os.path.join(content, "materials", f"proof-{stage}.json")))
manifest = json.load(open(os.path.join(content, "manifest.json")))["assets"]
textures = json.load(open(os.path.join(content, "materials", "textures.json")))["textures"]
editor_tools = unreal.SwcEditorLibrary
started = time.time()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([sm.ROOT], True)
TEST = sm.ROOT + "/Test/MaterialProof"
cache = {}


def texture_of(key):
    if key not in cache:
        record = textures.get(key) or manifest.get(key)
        cache[key] = sm.first_asset(record["unreal"], "Texture2D") if record else None
    return cache[key]


def legacy_instance(name, record):
    """A transient instance with the rules of stage 1.2 (Scripts/import_content.py, step 4)."""
    parent = sm.assets.load_asset(f"{sm.BASE}/" + {"opaque": "M_SwcOpaque", "masked": "M_SwcMasked",
                                                   "translucent": "M_SwcTranslucent"}[record["blend"]])
    instance = sm.tools.create_asset(name, TEST, unreal.MaterialInstanceConstant,
                                     unreal.MaterialInstanceConstantFactoryNew())
    sm.editing.set_material_instance_parent(instance, parent)
    for role, parameter in (("base", "BaseColor"), ("normal", "Normal")):
        if role in record and texture_of(record[role]) is not None:
            sm.editing.set_material_instance_texture_parameter_value(instance, parameter, texture_of(record[role]))
    sm.editing.set_material_instance_vector_parameter_value(instance, "BaseColorFactor",
                                                            unreal.LinearColor(*record["factor"]))
    overrides = instance.get_editor_property("base_property_overrides")
    overrides.set_editor_property("override_two_sided", True)
    overrides.set_editor_property("two_sided", bool(record.get("two sided")))
    if record["blend"] == "masked":
        overrides.set_editor_property("override_opacity_mask_clip_value", True)
        overrides.set_editor_property("opacity_mask_clip_value", float(record.get("cutoff", 0.5)))
    instance.set_editor_property("base_property_overrides", overrides)
    sm.editing.update_material_instance(instance)
    return instance


parents = {}


def spec_instance(name, spec):
    """A transient instance from a spec (a key that is not imported yet)."""
    instance = sm.tools.create_asset(name, TEST, unreal.MaterialInstanceConstant,
                                     unreal.MaterialInstanceConstantFactoryNew())
    if spec.get("parent") is None:
        raise RuntimeError("a spec with no parent needs its imported instance")
    sm.apply(instance, spec, parents, texture_of)
    return instance


def material_of(variant, slot, entry, n):
    if "legacy" in entry:
        return legacy_instance(f"L{n}_{slot}", entry["legacy"])
    if "instance" in entry and sm.assets.does_asset_exist(f"{sm.ROOT}/Materials/{entry['instance']}"):
        return sm.assets.load_asset(f"{sm.ROOT}/Materials/{entry['instance']}")
    if entry.get("spec"):
        return spec_instance(f"S{n}_{slot}", entry["spec"])
    raise RuntimeError(f"{variant} {slot}: no material ({entry})")


# The textures of the candidate specs (the stages under check): imported here, as apply_specs does.
state_path = os.path.join(os.environ["SWC_GAME_CONTENT"], "import-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) else {}
needed = {t for row in plan["rows"] for slots in row["variants"].values() for entry in slots.values()
          for t in (entry.get("spec") or {}).get("textures", {}).values()}
for key in sorted(needed):
    if key in textures:
        sm.import_texture(key, textures[key], content, state)
# The parents through their versioned functions (a parent of an older version is made again here).
parents["foliage"] = sm.foliage_parent(content, state)
parents["surface"] = sm.surface_parent(content, state, masked=False)
parents["surface masked"] = sm.surface_parent(content, state, masked=True)
parents["glass"] = sm.glass_parent(content, state)
parents["decal"] = sm.decal_parent(content, state)   # the roads, the projected decals, the mesh decals
sm.log("parents ready")   # tools/material-proof.sh checks the compile warnings after this line
# Each parent compiles again after the line: a failure of its final compile is in the checked part of the log (a parent
# that the functions above made compiles while its graph is built; those lines come before the line).
for parent in parents.values():
    sm.editing.recompile_material(parent)
with open(state_path + ".tmp", "w") as handle:
    json.dump(state, handle, indent=0, sort_keys=True)
os.replace(state_path + ".tmp", state_path)

if sm.assets.does_directory_exist(TEST):   # the level and the test instances of an earlier run
    sm.assets.delete_directory(TEST)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not levels.new_level(f"{TEST}/Level_{stage}"):
    raise RuntimeError("cannot make the proof level")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
report = {"stage": stage, "rows": [], "pictures": {}}
n = 0
for row in plan["rows"]:
    mesh = sm.first_asset(row["unreal"], "StaticMesh")
    if mesh is None:
        report["rows"].append({"label": row["label"], "error": "mesh not imported"})
        continue
    bounds = mesh.get_bounds()
    width = max(bounds.box_extent.x, bounds.box_extent.y) * 2.2
    used = {"label": row["label"], "variants": {}}
    for column, variant in enumerate(row["columns"]):
        xf = unreal.Transform()
        # The camera looks along +Y: +X is on the left of the picture. Left to right: stage 1.2, materials, override.
        xf.translation = unreal.Vector((1 - column) * width, 0.0, 0.0)
        actor = editor_tools.spawn_mesh_actor(mesh, xf, f"{row['label']} {variant}")
        component = actor.static_mesh_component
        if variant in row.get("custom data", {}):   # M6: SwcWind, the custom primitive data 0 of the cells
            component.set_custom_primitive_data_float(0, float(row["custom data"][variant]))
        used["variants"][variant] = {}
        for slot, entry in row["variants"][variant].items():
            n += 1
            material = material_of(variant, slot, entry, n)
            component.set_material_by_name(slot, material)
            used["variants"][variant][slot] = material.get_path_name()
    report["rows"].append(used)

    # Ground, sun, sky (the same in both pictures), and two cameras on the line of the research camera.
    ground = sm.assets.load_asset("/Engine/BasicShapes/Plane")
    gx = unreal.Transform()
    gx.translation = unreal.Vector(0.0, 0.0, 0.0)
    gx.scale3d = unreal.Vector(width * 4 / 100, width * 4 / 100, 1.0)
    editor_tools.spawn_mesh_actor(ground, gx, f"{row['label']} ground")
    center = bounds.origin.z
    height = bounds.box_extent.z * 2
    distance = max(1.6 * width / math.tan(math.radians(35)), 0.6 * height / math.tan(math.radians(19)))
    # The light of the world level (build_world.py): the default sun of 10 lux, 50 degrees high, a sky light.
    sun = editor_tools.spawn_actor_of_class(unreal.DirectionalLight, unreal.Transform(), "sun")
    sun.light_component.set_intensity(10.0)
    atmosphere = editor_tools.spawn_actor_of_class(unreal.SkyAtmosphere, unreal.Transform(), "atmosphere")
    sky = editor_tools.spawn_actor_of_class(unreal.SkyLight, unreal.Transform(), "sky light")
    sky.light_component.set_editor_property("real_time_capture", True)
    for side, sun_yaw in (("front", 90.0), ("back", -90.0)):
        # The camera stands at -Y and looks toward +Y. The light travels along the X axis of the sun actor: front = from
        # the camera side (yaw 90), back = from behind the meshes toward the camera (yaw -90, the transmitted light).
        sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-50.0, yaw=sun_yaw), False)
        sky.light_component.recapture_sky()
        unreal.SwcEditorLibrary.finish_compilation()
        location = unreal.Vector(0.0, -distance, center)
        look = unreal.Vector(0.0, 0.0, center)
        view = unreal.Transform()
        view.translation = location
        view.rotation = unreal.MathLibrary.find_look_at_rotation(location, look).quaternion()
        capture_actor = editor_tools.spawn_actor_of_class(unreal.SceneCapture2D, view, f"camera {side}")
        capture = capture_actor.capture_component2d
        capture.set_editor_property("fov_angle", 70.0)
        capture.set_editor_property("capture_every_frame", False)
        capture.set_editor_property("capture_on_movement", False)
        capture.set_editor_property("always_persist_rendering_state", True)
        settings = capture.get_editor_property("post_process_settings")
        settings.set_editor_property("override_auto_exposure_method", True)
        settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
        settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
        settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", 1.0)   # the same for all columns (the sky light of a commandlet is dark)
        capture.set_editor_property("post_process_settings", settings)
        target_rt = unreal.RenderingLibrary.create_render_target2d(world, 1800, 900,
                                                                   unreal.TextureRenderTargetFormat.RTF_RGBA8)
        capture.set_editor_property("texture_target", target_rt)
        capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        path = os.path.join(evidence, f"proof-{row['label']}-{side}.png")
        ok = unreal.SwcEditorLibrary.capture_to_png(capture, path, 16)
        report["pictures"][f"{row['label']} {side}"] = path if ok else "FAILED"
        sm.log(f"picture {row['label']} {side}: {'ok' if ok else 'FAILED'}")
        capture_actor.destroy_actor()
    for actor in (sun, atmosphere, sky):
        actor.destroy_actor()
    # The next row stands far away: one row in each picture.
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
        actor.destroy_actor()

report["seconds"] = round(time.time() - started, 1)
with open(os.path.join(evidence, f"proof-{stage}.json"), "w") as handle:
    json.dump(report, handle, indent=1)
sm.log("PROOF " + json.dumps({k: v for k, v in report.items() if k != "rows"}))
