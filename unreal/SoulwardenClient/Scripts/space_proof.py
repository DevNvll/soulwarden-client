"""Stage 1.1: the proof of the space map in Unreal (docs/ARCHITECTURE.md, "Spaces").

Run: tools/convert.sh space-proof (the plan), then tools/editor-script.sh space_proof.py --render.
The script imports the test assets of the plan (the marker, the terrain patch), places all actors of the plan with
their Unreal transforms, reads the vertices back from Unreal, compares them with the engine positions of the plan
(through the map), and renders the views of the plan. The level and the pictures are made from the game copy: the
level goes to /SwcGameContent/Test (the shared content plugin, not in git), the pictures and the report to
SWC_EVIDENCE.
Environment: SWC_CONTENT, SWC_WORK, SWC_EVIDENCE (default SWC_WORK/evidence/1.1).
"""
import json
import math
import os
import time

import unreal

content = os.environ["SWC_CONTENT"]
evidence = os.environ.get("SWC_EVIDENCE") or os.path.join(os.environ["SWC_WORK"], "evidence", "1.1")
os.makedirs(evidence, exist_ok=True)
plan = json.load(open(os.path.join(content, "test", "space-proof.json")))
manifest = json.load(open(os.path.join(content, "manifest.json")))
assets = unreal.EditorAssetLibrary
editor_tools = unreal.SwcEditorLibrary   # spawns with no placement subsystem (a commandlet has none)
started = time.time()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/SwcGameContent"], True)   # see import_content.py


def log(text):
    unreal.log("SWC " + text)


def import_gltf(path, destination):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return bool(task.get_editor_property("imported_object_paths"))


def static_mesh(folder):
    for path in assets.list_assets(folder, recursive=True, include_folder=False):
        if assets.find_asset_data(path).asset_class_path.asset_name == "StaticMesh":
            return assets.load_asset(path)
    return None


def unreal_transform(t):
    xf = unreal.Transform()
    xf.translation = unreal.Vector(*t["location"])
    xf.rotation = unreal.Quat(*t["rotation"])
    xf.scale3d = unreal.Vector(*t["scale"])
    return xf


def world_points(actor):
    component = actor.static_mesh_component
    description = component.static_mesh.get_static_mesh_description(0)
    xf = component.get_world_transform()
    points = []
    for i in range(description.get_vertex_count()):
        vertex = unreal.VertexID()
        vertex.set_editor_property("id_value", i)
        w = xf.transform_location(description.get_vertex_position(vertex))
        points.append((w.x, w.y, w.z))
    return points


def nearest(a, b):
    """@return The largest distance from a point of `a` to the nearest point of `b` (cm)."""
    return max(min(math.dist(p, q) for q in b) for p in a)


# 1. The test assets: imported again each run (they are small).
for asset, entry in manifest["assets"].items():
    if entry["kind"] == "test":
        ok = import_gltf(os.path.join(content, entry["file"]), entry["unreal"])
        log(f"import {asset}: {'ok' if ok else 'FAILED'}")

# 2. A new level with the actors of the plan.
level_path = "/SwcGameContent/Test/SpaceProof"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if assets.does_asset_exist(level_path):   # a level of an earlier run: open it and remove its actors
    levels.load_level(level_path)
    actor_tools = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for old in actor_tools.get_all_level_actors():
        actor_tools.destroy_actor(old)
elif not levels.new_level(level_path):
    raise RuntimeError("cannot make the level " + level_path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
log("level " + world.get_path_name())
meshes, spawned, missing = {}, {}, 0
for a in plan["actors"]:
    folder = a["unreal"]
    if folder not in meshes:
        meshes[folder] = static_mesh(folder)
    if meshes[folder] is None:
        missing += 1
        continue
    actor = editor_tools.spawn_mesh_actor(meshes[folder], unreal_transform(a["transform"]), a["label"])
    spawned[a["label"]] = actor
log(f"actors placed {len(spawned)}, mesh missing {missing}, unique meshes {len(meshes)}")

# 3. Read back from Unreal and compare with the engine positions of the plan (through the map).
results = []
for check in plan["checks"]:
    actor = spawned.get(check["label"])
    if actor is None:
        results.append({"label": check["label"], "error": "not placed"})
        continue
    points = world_points(actor)
    row = {"label": check["label"], "vertices": len(points)}
    if check["kind"] == "vertices":
        row["max distance expected -> unreal cm"] = round(nearest(check["expected"], points), 4)
        row["max distance unreal -> expected cm"] = round(nearest(points, check["expected"]), 4)
        xf = actor.get_actor_transform()
        row["tips"] = {}
        for name, expected in check["tips"].items():
            local = {"x tip": (300.0, 0.0, 0.0), "y tip": (0.0, -200.0, 0.0), "z tip": (0.0, 0.0, 100.0)}[name]
            got = xf.transform_location(unreal.Vector(*local))
            row["tips"][name] = {"unreal": [round(got.x, 3), round(got.y, 3), round(got.z, 3)],
                                 "expected": [round(v, 3) for v in expected],
                                 "error cm": round(math.dist((got.x, got.y, got.z), expected), 4)}
        row["ok"] = max(row["max distance expected -> unreal cm"], row["max distance unreal -> expected cm"]) < 0.05
    else:
        lo = [min(p[k] for p in points) for k in range(3)]
        hi = [max(p[k] for p in points) for k in range(3)]
        error = max(max(abs(a - b) for a, b in zip(lo, check["expected min"])),
                    max(abs(a - b) for a, b in zip(hi, check["expected max"])))
        row.update({"mesh": check["mesh"], "shear": check["shear"], "bounds error cm": round(error, 4),
                    "tolerance cm": check["tolerance cm"], "ok": error < check["tolerance cm"]})
    results.append(row)
    log(f"check {row['label']}: ok={row.get('ok')}")

# 4. Light and the views of the plan (the same cameras as the software pictures).
light = plan["light engine"]
direction = unreal.Vector(-light[0], light[1], -light[2])   # the light travels from the sun to the ground: -C l
sun_transform = unreal.Transform()
sun_transform.rotation = unreal.MathLibrary.make_rot_from_x(direction).quaternion()
sun = editor_tools.spawn_actor_of_class(unreal.DirectionalLight, sun_transform, "sun")
sun.light_component.set_intensity(4.0)
editor_tools.spawn_actor_of_class(unreal.SkyAtmosphere, unreal.Transform(), "atmosphere")
sky = editor_tools.spawn_actor_of_class(unreal.SkyLight, unreal.Transform(), "sky light")
sky.light_component.set_editor_property("real_time_capture", True)
sky.light_component.set_intensity(1.0)
unreal.SwcEditorLibrary.finish_compilation()
pictures = {}
for name, cam in plan["cameras"].items():
    width, height = cam["size"]
    location, target = unreal.Vector(*cam["location"]), unreal.Vector(*cam["target"])
    view = unreal.Transform()
    view.translation = location
    view.rotation = unreal.MathLibrary.find_look_at_rotation(location, target).quaternion()
    capture_actor = editor_tools.spawn_actor_of_class(unreal.SceneCapture2D, view, "camera " + name)
    capture = capture_actor.capture_component2d
    capture.set_editor_property("fov_angle", cam["fov horizontal"])
    capture.set_editor_property("capture_every_frame", False)
    capture.set_editor_property("capture_on_movement", False)
    capture.set_editor_property("always_persist_rendering_state", True)
    settings = capture.get_editor_property("post_process_settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", 0.0)
    capture.set_editor_property("post_process_settings", settings)
    for source, suffix in ((unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR, "unreal"),
                           (unreal.SceneCaptureSource.SCS_BASE_COLOR, "unreal-base-color")):
        target_rt = unreal.RenderingLibrary.create_render_target2d(world, width, height,
                                                                   unreal.TextureRenderTargetFormat.RTF_RGBA8)
        capture.set_editor_property("texture_target", target_rt)
        capture.set_editor_property("capture_source", source)
        path = os.path.join(evidence, f"space-proof-{name}-{suffix}.png")
        ok = unreal.SwcEditorLibrary.capture_to_png(capture, path, 8)
        pictures[f"{name} {suffix}"] = path if ok else "FAILED"
        log(f"picture {name} {suffix}: {'ok' if ok else 'FAILED'}")

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
report = {"actors placed": len(spawned), "mesh missing": missing, "checks": results,
          "all ok": all(r.get("ok") for r in results), "pictures": pictures, "level": level_path,
          "seconds": round(time.time() - started, 1)}
with open(os.path.join(evidence, "space-proof-unreal.json"), "w") as handle:
    json.dump(report, handle, indent=1)
log("SPACE PROOF " + json.dumps({k: v for k, v in report.items() if k != "checks"}))
