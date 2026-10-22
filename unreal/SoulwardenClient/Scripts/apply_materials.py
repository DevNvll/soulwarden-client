"""Applies the material specs of the converter to the material instances of /SwcGameContent (docs/MATERIALS.md).

Run through tools/apply-materials.sh (UnrealEditor-Cmd -run=pythonscript, no window, with the import lock). Reads
SWC_CONTENT/materials/instances.json and textures.json (converter/swc_convert/material_records.py). Steps:
1. The textures of the specs that are new or changed (the stamps of import-state.json, as import_content.py).
2. The parents of Scripts/swc_materials.py.
3. Each material instance /SwcGameContent/Materials/KEY: its spec (the asset path does not change; the meshes keep
   their slots). The state key is "materials:KEY" (the stamp of the spec and of the rules of swc_materials).
Environment: SWC_CONTENT, SWC_GAME_CONTENT, SWC_MATERIALS_FILTER (a text that the key must contain),
SWC_MATERIALS_KEYS (a file with a JSON list of material keys), SWC_MATERIALS_MAX_SECONDS (stop after the item that
passes this time; the report has "incomplete": true, and a next call continues), SWC_MATERIALS_PART (decals: the specs of
SWC_CONTENT/decals, for example the dm_ instances of the mesh decal companions; bodies: the glTF material instances of the
skinned bodies of SWC_CONTENT/bodies, the filter and the keys are Unreal folders; default materials).
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
wanted = os.environ.get("SWC_MATERIALS_FILTER", "")
keys_file = os.environ.get("SWC_MATERIALS_KEYS", "")
listed = set(json.load(open(keys_file))) if keys_file else None
max_seconds = float(os.environ.get("SWC_MATERIALS_MAX_SECONDS") or 0)
part = os.environ.get("SWC_MATERIALS_PART") or "materials"
state_path = os.path.join(plugin, "import-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) else {}
started = time.time()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([sm.ROOT], True)
specs, textures = sm.load_specs(content, part)
report = {"seconds": {}}
if os.environ.get("SWC_MATERIALS_PREFETCH") == "1":
    # Only the textures of the candidate specs (instances-candidate.json): a later deployment of these stages is fast.
    candidates = json.load(open(os.path.join(content, "materials", "instances-candidate.json")))["instances"]
    keys = sorted({t for spec in candidates.values() for t in spec.get("textures", {}).values() if t in textures})
    report.update({"textures": {"imported": 0, "up to date": 0}, "failed": [], "incomplete": False})
    for n, key in enumerate(keys):
        if max_seconds and time.time() - started > max_seconds:
            report["incomplete"] = True
            break
        try:
            done = sm.import_texture(key, textures[key], content, state)
        except Exception as error:
            report["failed"].append(f"{key}: {str(error)[:160]}")
            continue
        report["textures"]["imported" if done else "up to date"] += 1
        if done and report["textures"]["imported"] % 100 == 0:
            unreal.SystemLibrary.collect_garbage()
            with open(state_path + ".tmp", "w") as handle:
                json.dump(state, handle, indent=0, sort_keys=True)
            os.replace(state_path + ".tmp", state_path)
            sm.log(f"prefetch {n + 1}/{len(keys)} {round(time.time() - started)} s")
elif part == "bodies":
    # The glTF material instances of the skinned bodies (SWC_CONTENT/bodies; the filter: a text of the Unreal folder).
    folders = sorted(f for f in sm.load_body_slots(content) if (not wanted or wanted in f) and (listed is None or f in listed))
    sm.log(f"selected {len(folders)} bodies")
    sm.apply_bodies(folders, state, force=False, content=content, max_seconds=max_seconds, report=report)
else:
    selected = [k for k in specs if (not wanted or wanted in k) and (listed is None or k in listed)]
    sm.log(f"selected {len(selected)} instances")
    sm.apply_specs(selected, state, force=False, content=content, max_seconds=max_seconds, report=report, part=part)
# import_content.py writes the same file: the lock of tools/apply-materials.sh keeps one writer at a time.
with open(state_path + ".tmp", "w") as handle:
    json.dump(state, handle, indent=0, sort_keys=True)
os.replace(state_path + ".tmp", state_path)
report["seconds"]["all"] = round(time.time() - started, 1)
report["failures"] = report["failed"][:20]
report["failed"] = len(report["failed"])
with open(os.path.join(content, "materials", "apply-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
sm.log("APPLY " + json.dumps(report))
