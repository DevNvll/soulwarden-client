"""The cookies of the local lights (stage E2, docs/ENVIRONMENT.md): the projector textures of SWC_CONTENT/environment/cookies.json
(tools/convert-environment.sh cookies) as textures in /SwcGameContent/Environment/Cookies, the light function material
/SwcGameContent/Environment/M_SwcCookie (the texture parameter Cookie on the emissive color: Unreal maps the UV of a light function
of a spot light from one side of the outer cone to the other), and one material instance MI_NAME for each texture. Run through
tools/import-cookies.sh (the import lock).

The textures are linear (the source formats are BC4 and BC2 UNORM), clamped, with no streaming (44 small textures).
Environment: SWC_COOKIES (cookies.json), SWC_COOKIES_FORCE (1: import again), SWC_COOKIES_REPORT (the report file).
"""
import json
import os
import time

import unreal

FOLDER = "/SwcGameContent/Environment"
COOKIES = FOLDER + "/Cookies"
MATERIAL = FOLDER + "/M_SwcCookie"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary
table = json.load(open(os.environ["SWC_COOKIES"]))["cookies"]
content = os.path.dirname(os.environ["SWC_COOKIES"])
force = os.environ.get("SWC_COOKIES_FORCE") == "1"
started = time.time()
report = {"textures": 0, "up to date": 0, "instances": 0, "failed": []}
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FOLDER], True)   # does_asset_exist needs the scan


def log(text):
    unreal.log("SWC COOKIES " + text)


def texture_of(entry):
    name = "T_" + entry["name"]
    path = f"{COOKIES}/{name}"
    if assets.does_asset_exist(path) and not force:
        report["up to date"] += 1
        return unreal.load_asset(path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(content, entry["png"]))
    task.set_editor_property("destination_path", COOKIES)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(path)
    if texture is None:
        raise RuntimeError("no texture after the import")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("never_stream", True)
    assets.save_loaded_asset(texture, only_if_is_dirty=False)
    report["textures"] += 1
    return texture


def material(default_texture):
    if assets.does_asset_exist(MATERIAL) and not force:
        return unreal.load_asset(MATERIAL)
    if assets.does_asset_exist(MATERIAL):
        assets.delete_asset(MATERIAL)
    result = tools.create_asset("M_SwcCookie", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    result.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    sample = library.create_material_expression(result, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    sample.set_editor_property("parameter_name", "Cookie")
    sample.set_editor_property("texture", default_texture)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    library.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(result)
    assets.save_loaded_asset(result, only_if_is_dirty=False)
    return result


textures = {}
for key, entry in sorted(table.items()):
    try:
        textures[key] = texture_of(entry)
    except Exception as error:
        report["failed"].append(f"{key}: {str(error)[:120]}")
parent = material(next(iter(textures.values()))) if textures else None
for key, texture in textures.items():
    name = "MI_" + table[key]["name"]
    path = f"{COOKIES}/{name}"
    if assets.does_asset_exist(path) and not force:
        continue
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    instance = tools.create_asset(name, COOKIES, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property("parent", parent)
    library.set_material_instance_texture_parameter_value(instance, "Cookie", texture)
    assets.save_loaded_asset(instance, only_if_is_dirty=False)
    report["instances"] += 1
report["seconds"] = round(time.time() - started, 1)
with open(os.environ["SWC_COOKIES_REPORT"], "w") as handle:
    json.dump(report, handle, indent=1)
log(json.dumps(report))
