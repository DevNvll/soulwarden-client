"""The material instances of the Unreal water (docs/WATER.md "The Unreal water"; decision of the user, 2026-10-25, AST-200).

Run through tools/import-water-unreal.sh (UnrealEditor-Cmd -run=pythonscript, no window). The client (Water/SwcWaterUnreal.cpp,
-SwcWaterMaterials=FOLDER) uses the instance FOLDER/NAME of each slot when it exists, else the material of the Water plugin. Each
instance is a child of the plugin material of its slot (the defaults of the editor settings of the plugin, WaterEditorSettings.cpp).
Stage 1 sets no value: the look is the plugin look. The values of the client (the colors from the source fog, the waves) come in
stage 2 as the VALUES of each slot.

Environment: SWC_WATER_UNREAL_DEST (the folder of the instances, for example /SwcWaterCheck/Water/Unreal for a check copy;
default /SwcGameContent/Water/Unreal), SWC_WATER_UNREAL_REPORT (the report file), SWC_IMPORT_FORCE=1 (make each instance again).
"""
import json
import os

import unreal

DEST = os.environ.get("SWC_WATER_UNREAL_DEST", "/SwcGameContent/Water/Unreal").rstrip("/")
REPORT = os.environ["SWC_WATER_UNREAL_REPORT"]
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
# The rule of the instances: a new value makes them again.
VERSION = "1"   # 1: the children of the plugin materials with no value (stage 1)

SURFACE = "/Water/Materials/WaterSurface"
SLOTS = {
    "MI_SwcWater_Ocean": f"{SURFACE}/Water_Material_Ocean",
    "MI_SwcWater_Lake": f"{SURFACE}/Water_Material_Lake",
    "MI_SwcWater_River": f"{SURFACE}/Water_Material_River",
    "MI_SwcWater_Custom": f"{SURFACE}/Water_Material_CustomMesh",
    "MI_SwcWater_RiverToLake": f"{SURFACE}/Transitions/Water_Material_River_To_Lake_Transition",
    "MI_SwcWater_RiverToOcean": f"{SURFACE}/Transitions/Water_Material_River_To_Ocean_Transition",
    "MI_SwcWater_FarMesh": f"{SURFACE}/Water_FarMesh",
    "MI_SwcWater_Underwater": "/Water/Materials/PostProcessing/M_UnderWater_PostProcess_Volume",
    "MI_SwcWater_UnderwaterCustom": "/Water/Materials/PostProcessing/MI_UnderWater_PostProcess_Volume_Custom",
}
# The values of each slot (stage 2): {"scalar": {name: value}, "vector": {name: (r, g, b, a)}}.
VALUES: dict[str, dict] = {}

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {"dest": DEST, "version": VERSION, "made": [], "kept": [], "failed": []}


def make(name: str, parent_path: str) -> None:
    path = f"{DEST}/{name}"
    parent = assets.load_asset(parent_path)
    if parent is None:
        raise RuntimeError(f"no plugin material {parent_path} (is the Water plugin enabled?)")
    if assets.does_asset_exist(path) and not force:
        existing = assets.load_asset(path)
        if existing is not None and existing.get_editor_property("parent") == parent and \
                assets.get_metadata_tag(existing, "SwcVersion") == VERSION:
            report["kept"].append(name)
            return
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    instance = tools.create_asset(name, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    for key, value in VALUES.get(name, {}).get("scalar", {}).items():
        editing.set_material_instance_scalar_parameter_value(instance, key, value)
    for key, value in VALUES.get(name, {}).get("vector", {}).items():
        editing.set_material_instance_vector_parameter_value(instance, key, unreal.LinearColor(*value))
    editing.update_material_instance(instance)
    assets.set_metadata_tag(instance, "SwcVersion", VERSION)
    assets.save_loaded_asset(instance, only_if_is_dirty=False)
    report["made"].append(name)


for slot, parent_path in SLOTS.items():
    try:
        make(slot, parent_path)
    except Exception as error:   # the report has the fault; the next slot goes on
        report["failed"].append(f"{slot}: {error}")
with open(REPORT, "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC WATER UNREAL " + json.dumps(report))
