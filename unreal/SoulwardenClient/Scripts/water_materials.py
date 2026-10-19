"""The water textures and the base water material (stage WF5, docs/WATER.md), into /SwcGameContent/Water.

Run through tools/import-water.sh (UnrealEditor-Cmd -run=pythonscript, no window). The textures come from the converter
(tools/convert-water.sh write: SWC_CONTENT/water/water.json "textures"). The assets are made from the game copy: they go to the
shared content plugin, not into git.

M_SwcWater: Single Layer Water (the Unreal way to render water: its reflections, refraction, and light in the water; decision of
the user, 2026-10-18: the content follows the source, the rendering uses what Unreal does better). The client
(Water/SwcWaterSubsystem) makes one dynamic instance for each surface and sets the source values:
- the waves: the wave normal map of the source shaders (oceanwaves_ddn) at two scales, moved by the wind (the ocean and the
  volumes: world UV) or along the river (UseMeshUV 1: the UV of the river mesh, v along the flow, WaterFlowSpeed);
  Tilling, DetailTilling, NormalsScale, DetailNormalsScale of the .mtl;
- the foam of the source (FoamAmount, FoamTilling, FoamSoftIntersectionFactor) at the shore: the depth of the water behind the
  surface (SceneDepthWithoutWater);
- the color: the water fog of the source (FogColor, density, color scale) gives the scattering and the absorption of Single
  Layer Water (FogLightScale: the light of the source fog color, a calibration of the matched views).
Environment: SWC_CONTENT, SWC_IMPORT_FORCE=1 (make each asset again).
"""
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

content = os.environ["SWC_CONTENT"]
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
WATER = "/SwcGameContent/Water"
TEXTURES = WATER + "/Textures"
# The rule of the parent: a new value makes it again.
PARENT_VERSION = "5"

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/SwcGameContent"], True)

index = json.load(open(os.path.join(content, "water", "water.json")))
state_path = os.path.join(content, "water", "import-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) and not force else {}
report = {"textures": 0, "material": False, "failed": []}


def import_texture(role, record):
    path = os.path.join(content, record["file"])
    stamp = sm.file_stamp(path)
    asset = f"{TEXTURES}/{record['name']}"
    if state.get("texture:" + role) == stamp and assets.does_asset_exist(asset):
        return assets.load_asset(asset)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", TEXTURES)
    task.set_editor_property("destination_name", record["name"])
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(asset)
    if texture is None:
        raise RuntimeError(f"{role}: no texture after the import")
    sm.texture_settings(texture, record["color"])
    assets.save_loaded_asset(texture)
    state["texture:" + role] = stamp
    report["textures"] += 1
    return texture


def connect(source, pin, target, name):
    # The pins of an output node with no own input names have the names of their properties (ScatteringCoefficients).
    for candidate in (name, name.replace(" ", "")):
        if editing.connect_material_expressions(source, pin, target, candidate):
            return
    raise RuntimeError(f"the pin {name} of {target.get_name()} does not connect")


UV_HLSL = """float2 world = P.xy * 0.01 * UVScale;
float2 baseUV = lerp(world, UV, UseMeshUV);
float2 wind = float2(cos(WindDirection + Turn), sin(WindDirection + Turn)) * WaveSpeed * WindScale;
float2 flow = lerp(wind, float2(0.0, FlowSpeed * FlowScale), UseMeshUV) * Rate;
return (baseUV - flow * Time) * Tiling + Offset;"""
NORMAL_HLSL = """float2 xy = (N0.xy * NormalsScale + N1.xy * DetailNormalsScale) * NormalStrength;
return normalize(float3(xy, 1.0));"""
FOAM_HLSL = """float depth = max(Below - Pixel, 0.0) * 0.01;
float shore = 1.0 - saturate(depth / max(Soft, 0.01));
return saturate(shore * F.r * Amount * FoamScale);"""
# The light in the water from the water fog of the source: the hue of the fog color, its brightness (the largest component x the
# color scale x FogLightScale), and the extinction (the fog density x ExtinctionScale). Scattering = extinction x albedo,
# absorption = extinction x (1 - albedo): the channels of the hue go farther in the water.
ALBEDO_HLSL = """float m = max(max(Fog.r, Fog.g), max(Fog.b, 0.0001));
return saturate(Fog.rgb / m * saturate(m * FogScale * FogLightScale));"""
SCATTERING_HLSL = "return Density * ExtinctionScale * " + "A;"
ABSORPTION_HLSL = "return Density * ExtinctionScale * (1.0 - A) + MinAbsorption;"


def make_parent(waves, foam):
    path = f"{WATER}/M_SwcWater"
    if assets.does_asset_exist(path) and state.get("parent") == PARENT_VERSION:
        return
    if assets.does_asset_exist(path):
        # A new asset each time: delete_all_material_expressions keeps the Single Layer Water output node (a material can have only one).
        # Only dynamic instances of the client use it.
        assets.delete_asset(path)
    material = tools.create_asset("M_SwcWater", WATER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    position = sm.node(material, unreal.MaterialExpressionWorldPosition, -1800, -400)
    coordinate = sm.node(material, unreal.MaterialExpressionTextureCoordinate, -1800, -300)
    time = sm.node(material, unreal.MaterialExpressionTime, -1800, -200)
    p = {name: sm.scalar(material, name, value, -1800, 100 + 60 * k) for k, (name, value) in enumerate([
        ("UseMeshUV", 0.0), ("UVScale", 0.1), ("Tilling", 1.0), ("DetailTilling", 2.0), ("NormalsScale", 0.5),
        ("DetailNormalsScale", 0.5), ("NormalStrength", 0.04), ("FlowSpeed", 0.0), ("FlowScale", 0.25), ("WaveSpeed", 1.0), ("WindDirection", 0.0),
        ("WindScale", 0.02), ("FoamAmount", 0.0), ("FoamTilling", 1.0), ("FoamSoftIntersectionFactor", 1.0), ("FoamScale", 0.15),
        ("Density", 0.5), ("FogScale", 1.0), ("FogLightScale", 15.0), ("ExtinctionScale", 0.4), ("MinAbsorption", 0.01), ("PhaseG", 0.1),
        ("Roughness", 0.02), ("FoamRoughness", 0.6)])}
    fog = sm.vector(material, "FogColor", (0.03, 0.09, 0.11, 1.0), -1800, 1500)

    def uv_node(rate, turn, offset, tiling, y):
        node = sm.custom(material, UV_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                         ["P", "UV", "UseMeshUV", "Time", "UVScale", "WindDirection", "Turn", "WaveSpeed", "WindScale", "FlowSpeed",
                          "FlowScale", "Rate", "Tiling", "Offset"], -1300, y)
        connect(position, "", node, "P")
        connect(coordinate, "", node, "UV")
        connect(time, "", node, "Time")
        for name in ("UseMeshUV", "UVScale", "WindDirection", "WaveSpeed", "WindScale", "FlowSpeed", "FlowScale"):
            connect(p[name], "", node, name)
        connect(tiling, "", node, "Tiling")
        constants = {"Turn": turn, "Rate": rate}
        for name, value in constants.items():
            c = sm.node(material, unreal.MaterialExpressionConstant, -1500, y + 40)
            c.set_editor_property("r", value)
            connect(c, "", node, name)
        o = sm.node(material, unreal.MaterialExpressionConstant2Vector, -1500, y + 80)
        o.set_editor_property("r", offset[0])
        o.set_editor_property("g", offset[1])
        connect(o, "", node, "Offset")
        return node

    uv0 = uv_node(1.0, 0.0, (0.0, 0.0), p["Tilling"], -500)
    uv1 = uv_node(0.7, 0.6, (0.37, 0.11), p["DetailTilling"], -250)
    n0 = sm.sampler(material, "WaveNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, waves, -900, -500)
    n1 = sm.sampler(material, "WaveNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, waves, -900, -250)
    connect(uv0, "", n0, "UVs")
    connect(uv1, "", n1, "UVs")
    normal = sm.custom(material, NORMAL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["N0", "N1", "NormalsScale", "DetailNormalsScale", "NormalStrength"],
                       -500, -400)
    connect(n0, "RGB", normal, "N0")
    connect(n1, "RGB", normal, "N1")
    connect(p["NormalsScale"], "", normal, "NormalsScale")
    connect(p["DetailNormalsScale"], "", normal, "DetailNormalsScale")
    connect(p["NormalStrength"], "", normal, "NormalStrength")
    editing.connect_material_property(normal, "", MP.MP_NORMAL)

    # The foam at the shore: the water depth behind the surface along the view.
    foam_uv = sm.custom(material, "return P.xy * 0.01 * UVScale * FoamTiling;", unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                        ["P", "UVScale", "FoamTiling"], -1300, 0)
    connect(position, "", foam_uv, "P")
    connect(p["UVScale"], "", foam_uv, "UVScale")
    connect(p["FoamTilling"], "", foam_uv, "FoamTiling")
    foam_sample = sm.sampler(material, "Foam", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, foam, -900, 0)
    connect(foam_uv, "", foam_sample, "UVs")
    below = sm.node(material, unreal.MaterialExpressionSceneDepthWithoutWater, -900, 250)
    pixel = sm.node(material, unreal.MaterialExpressionPixelDepth, -900, 350)
    foam_amount = sm.custom(material, FOAM_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                            ["Below", "Pixel", "Soft", "F", "Amount", "FoamScale"], -500, 100)
    connect(below, "", foam_amount, "Below")
    connect(pixel, "", foam_amount, "Pixel")
    connect(p["FoamSoftIntersectionFactor"], "", foam_amount, "Soft")
    connect(foam_sample, "RGBA", foam_amount, "F")
    connect(p["FoamAmount"], "", foam_amount, "Amount")
    connect(p["FoamScale"], "", foam_amount, "FoamScale")
    base = sm.custom(material, "return Foam.xxx;", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Foam"], -200, 100)
    connect(foam_amount, "", base, "Foam")
    editing.connect_material_property(base, "", MP.MP_BASE_COLOR)
    # The opacity of Single Layer Water blends the surface (the foam) over the water: 0 is water only (an unconnected opacity is 1, an
    # opaque surface: the first matched views had black water).
    editing.connect_material_property(foam_amount, "", MP.MP_OPACITY)
    roughness = sm.custom(material, "return lerp(Roughness, FoamRoughness, Foam);", unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                          ["Roughness", "FoamRoughness", "Foam"], -200, 250)
    connect(p["Roughness"], "", roughness, "Roughness")
    connect(p["FoamRoughness"], "", roughness, "FoamRoughness")
    connect(foam_amount, "", roughness, "Foam")
    editing.connect_material_property(roughness, "", MP.MP_ROUGHNESS)

    # The light in the water: Single Layer Water from the water fog of the source.
    output = sm.node(material, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 200, 700)
    albedo = sm.custom(material, ALBEDO_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Fog", "FogScale", "FogLightScale"], -500, 650)
    connect(fog, "", albedo, "Fog")
    connect(p["FogScale"], "", albedo, "FogScale")
    connect(p["FogLightScale"], "", albedo, "FogLightScale")
    for code, pin, y in ((SCATTERING_HLSL, "Scattering Coefficients", 600), (ABSORPTION_HLSL, "Absorption Coefficients", 750)):
        inputs = ["A", "Density", "ExtinctionScale"] + (["MinAbsorption"] if "MinAbsorption" in code else [])
        node = sm.custom(material, code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, -200, y)
        connect(albedo, "", node, "A")
        for name in inputs[1:]:
            connect(p[name], "", node, name)
        connect(node, "", output, pin)
    connect(p["PhaseG"], "", output, "Phase G")
    one = sm.node(material, unreal.MaterialExpressionConstant, 0, 950)
    one.set_editor_property("r", 1.0)
    connect(one, "", output, "Color Scale Behind Water")
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["parent"] = PARENT_VERSION
    report["material"] = True


try:
    textures = {role: import_texture(role, record) for role, record in index["textures"].items()}
    make_parent(textures["waves"], textures["foam"])
except Exception as error:   # the report has the fault; the commandlet ends
    report["failed"].append(str(error))
with open(state_path, "w") as handle:
    json.dump(state, handle, indent=1)
with open(os.path.join(content, "water", "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC WATER IMPORT " + json.dumps(report))
