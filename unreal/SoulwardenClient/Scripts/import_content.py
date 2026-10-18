"""Imports the converted content (SWC_CONTENT/manifest.json) into /Game/Imported with Interchange glTF.

Run through tools/import.sh (UnrealEditor-Cmd -run=pythonscript, no window). The imported assets are made from the
game copy: /Game/Imported is not in git.
Environment: SWC_CONTENT, SWC_IMPORT_FILTER (a text that the game path must contain), SWC_IMPORT_FORCE=1.
"""
import json
import os

import unreal

content = os.environ["SWC_CONTENT"]
wanted = os.environ.get("SWC_IMPORT_FILTER", "")
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
manifest = json.load(open(os.path.join(content, "manifest.json")))
tools = unreal.AssetToolsHelpers.get_asset_tools()
done, skipped, failed = 0, 0, []
for asset, entry in sorted(manifest["assets"].items()):
    if wanted and wanted not in asset:
        continue
    destination = entry["unreal"]
    if not force and unreal.EditorAssetLibrary.list_assets(destination, recursive=True):
        skipped += 1
        continue
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(content, entry["file"]))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    if task.get_editor_property("imported_object_paths"):
        done += 1
    else:
        failed.append(asset)
report = {"imported": done, "skipped (exists)": skipped, "failed": len(failed), "failures": failed[:20]}
with open(os.path.join(content, "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC IMPORT " + json.dumps(report))
