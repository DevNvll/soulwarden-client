"""The cookies of the local lights (stage E2, docs/ENVIRONMENT.md): the projector textures of SWC_CONTENT/environment/cookies.json
(tools/convert-environment.sh cookies) as textures in /SwcGameContent/Environment/Cookies, the light function material
/SwcGameContent/Environment/M_SwcCookie (the texture parameter Cookie on the emissive color: Unreal maps the UV of a light function
of a spot light from one side of the outer cone to the other), and one material instance MI_NAME for each texture. Run through
tools/import-cookies.sh (the import lock).

It also makes the light function of the point lights /SwcGameContent/Environment/M_SwcPointFalloff (research/lights-emission.md
"Point lights"; Environment/SwcLocalLightRules.h point_falloff): the correction of the Unreal inverse-square falloff to the source
falloff (the near clamp max(d, r) and the range fade f = sat(5 (1 - d / R))^2), divided by the gain of the light (a light function
gives at most 1). The client sets the light function scale to 100, so the light vector is in m. The world position is an input of
the code (not used): the engine then does not put the function in the light function atlas (HLSLMaterialTranslator: a function
with no world position is atlas compatible, and in the atlas the light vector is not the vector to the pixel; the first run showed
"126 light(s) will skip light functions due to out of atlas slot"). SWC_POINT_FALLOFF_FORCE=1 makes it again.

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
POINT_FALLOFF = FOLDER + "/M_SwcPointFalloff"
# The same formula as env::point_falloff (tests/environment/local_light_tests.cpp), in m.
POINT_FALLOFF_HLSL = """float d = length(LV);
if (Range <= 0 || d >= Range) return 0;
float r = max(Bulb, 0.001);
float x = d / Range;
float f = saturate(5 * (1 - x)); f *= f;
float q = saturate(1 - x * x * x * x); float ue = q * q;
float ratio = ue > 1e-6 ? f / ue : 25.0 / 16.0;
float n = max(d, r);
return saturate((d * d + 0.0001) / (n * n) * ratio / max(Gain, 1));"""
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


def point_falloff():
    if assets.does_asset_exist(POINT_FALLOFF) and os.environ.get("SWC_POINT_FALLOFF_FORCE") != "1":
        return "up to date"
    if assets.does_asset_exist(POINT_FALLOFF):
        assets.delete_asset(POINT_FALLOFF)
    result = tools.create_asset("M_SwcPointFalloff", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    result.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    vector = library.create_material_expression(result, unreal.MaterialExpressionLightVector, -700, 0)
    code = library.create_material_expression(result, unreal.MaterialExpressionCustom, -350, 0)
    code.set_editor_property("code", POINT_FALLOFF_HLSL)
    code.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for name in ("LV", "Bulb", "Range", "Gain", "WP"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    code.set_editor_property("inputs", pins)
    library.connect_material_expressions(vector, "", code, "LV")
    position = library.create_material_expression(result, unreal.MaterialExpressionWorldPosition, -700, -150)
    library.connect_material_expressions(position, "", code, "WP")
    for i, (name, value) in enumerate((("Bulb", 0.6), ("Range", 6.0), ("Gain", 3.0))):
        p = library.create_material_expression(result, unreal.MaterialExpressionScalarParameter, -700, 150 + 120 * i)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", value)
        library.connect_material_expressions(p, "", code, name)
    library.connect_material_property(code, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(result)
    assets.save_loaded_asset(result, only_if_is_dirty=False)
    return "made"


try:
    report["point falloff"] = point_falloff()
except Exception as error:
    report["failed"].append(f"M_SwcPointFalloff: {str(error)[:160]}")
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
