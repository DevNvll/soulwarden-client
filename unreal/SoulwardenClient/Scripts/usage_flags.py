"""The usage flags that the client needs (AST-203; docs/WORLD.md "The loading"). An uncooked client that draws a mesh with a
material that has no usage flag for it logs "Material PATH missing usage flag NAME! Default Material will be used in game."
and draws the default material of the engine (for example the Nanite meshes of the weapons of AST-202: the materials of an
Interchange import have no Nanite flag). This script sets each flag of the list on the base material, compiles it, and saves
it. Only the usage flags change.

Run through tools/fix-usage-flags.sh (UnrealEditor-Cmd -run=pythonscript, no window, with the world lock and the import
lock). Environment: SWC_USAGE_LIST (a file with lines "PATH NAME": the object path of the material and the usage name of the
engine), SWC_USAGE_REPORT (the JSON report).
"""

import json
import os

import unreal

# The usage names of the engine (UMaterialInterface::GetUsageName) and the properties of UMaterial.
PROPERTIES = {
    "SkeletalMesh": "used_with_skeletal_mesh",
    "StaticLighting": "used_with_static_lighting",
    "MorphTargets": "used_with_morph_targets",
    "SplineMeshes": "used_with_spline_meshes",
    "InstancedStaticMeshes": "used_with_instanced_static_meshes",
    "Clothing": "used_with_clothing",
    "GeometryCache": "used_with_geometry_cache",
    "Water": "used_with_water",
    "HairStrands": "used_with_hair_strands",
    "Nanite": "used_with_nanite",
}


def main():
    wanted = {}
    with open(os.environ["SWC_USAGE_LIST"], encoding="utf-8") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) == 2:
                wanted.setdefault(parts[0], set()).add(parts[1])
    report = {"set": [], "already": [], "failed": []}
    for path, usages in sorted(wanted.items()):
        asset = unreal.load_asset(path)
        material = asset if isinstance(asset, unreal.Material) else None
        if material is None and isinstance(asset, unreal.MaterialInterface):
            material = asset.get_base_material()   # the flags are on the base material
        if material is None:
            report["failed"].append({"path": path, "reason": "not a material"})
            continue
        changed = []
        for usage in sorted(usages):
            name = PROPERTIES.get(usage)
            if name is None:
                report["failed"].append({"path": path, "reason": f"usage {usage} has no property here"})
                continue
            if not material.get_editor_property(name):
                material.set_editor_property(name, True)
                changed.append(usage)
        entry = {"path": material.get_path_name(), "usages": changed or sorted(usages)}
        if not changed:
            report["already"].append(entry)
            continue
        unreal.MaterialEditingLibrary.recompile_material(material)
        if unreal.EditorAssetLibrary.save_loaded_asset(material, False):
            report["set"].append(entry)
        else:
            report["failed"].append(dict(entry, reason="the save failed"))
    with open(os.environ["SWC_USAGE_REPORT"], "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1)
    unreal.log(f"usage flags: {len(report['set'])} set, {len(report['already'])} already set, {len(report['failed'])} failed")


main()
