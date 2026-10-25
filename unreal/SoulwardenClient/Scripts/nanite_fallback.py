"""The full Nanite fallback mesh of the meshes of the minority pairs (AST-288 W2; converter/swc_convert/nanite.py, materials/no-nanite.json
"minority"). A cell entry of such a pair has no Nanite (SwcEditorLibrary flag bit 2: bDisallowNanite) and draws the fallback mesh of
the asset. With the default settings (FallbackTarget Auto, r.Nanite.Builder.FallbackTriangleThreshold 0) the builder reduces the
fallback to the relative error of r.Nanite.Builder.FallbackTargetAutoRelativeError: this script sets FallbackTarget PercentTriangles with
100 % (relative error 0) on these meshes, builds them, and saves them. Only the Nanite fallback settings change: the Nanite placements
of the mesh draw as before.

Run through tools/nanite-fallback.sh (UnrealEditor-Cmd -run=pythonscript, no window, with the world lock and the import lock).
Environment: SWC_CONTENT, SWC_GAME_CONTENT (import-state.json gets "nanitefallback:KEY" = "1"), SWC_FALLBACK_REPORT (the JSON report).
"""

import json
import os

import unreal

FALLBACK_VERSION = "1"


def main():
    content = os.environ["SWC_CONTENT"]
    plugin = os.environ["SWC_GAME_CONTENT"]
    manifest = json.load(open(os.path.join(content, "manifest.json")))["assets"]
    listed = json.load(open(os.path.join(content, "materials", "no-nanite.json"))).get("minority", {})
    state_path = os.path.join(plugin, "import-state.json")
    state = json.load(open(state_path))
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    report = {"set": [], "already": [], "failed": []}
    for key in sorted(listed):
        entry = manifest.get(key, {})
        folder = entry.get("unreal")
        if not folder:
            report["failed"].append({"key": key, "reason": "not in the manifest"})
            continue
        if state.get("nanitefallback:" + key) == FALLBACK_VERSION:
            report["already"].append(key)
            continue
        meshes = [a for a in unreal.EditorAssetLibrary.list_assets(folder, recursive=True)
                  if unreal.EditorAssetLibrary.find_asset_data(a).asset_class_path.asset_name == "StaticMesh"]
        mesh = unreal.load_asset(meshes[0]) if meshes else None
        if mesh is None:
            report["failed"].append({"key": key, "reason": "no static mesh"})
            continue
        settings = subsystem.get_nanite_settings(mesh)
        settings.set_editor_property("fallback_target", unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
        settings.set_editor_property("fallback_percent_triangles", 1.0)
        settings.set_editor_property("fallback_relative_error", 0.0)
        subsystem.set_nanite_settings(mesh, settings, apply_changes=True)
        if unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
            state["nanitefallback:" + key] = FALLBACK_VERSION
            report["set"].append(key)
        else:
            report["failed"].append({"key": key, "reason": "the save failed"})
    with open(state_path + ".tmp", "w") as handle:
        json.dump(state, handle, indent=0, sort_keys=True)
    os.replace(state_path + ".tmp", state_path)
    with open(os.environ["SWC_FALLBACK_REPORT"], "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1)
    unreal.log(f"nanite fallback: {len(report['set'])} set, {len(report['already'])} already, {len(report['failed'])} failed")


main()
