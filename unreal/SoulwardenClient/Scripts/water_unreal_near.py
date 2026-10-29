"""The near color term of the source ocean in the Unreal water (docs/WATER.md "Stage 2c", research/water-near-term.md option A; AST-200).

Run through tools/import-water-unreal.sh --near (UnrealEditor-Cmd -run=pythonscript, no window). The check plugin only: the folder
/SwcWaterCheck/Water/Near. Never the shared plugin, never /Water (the plugin materials are read, not changed).

1. A copy of the plugin parent /Water/Materials/WaterSurface/Water_Material as M_SwcWaterNear. Between its final material attributes and
   the output: Get Material Attributes (the emissive and the opacity), a Custom node with the near term N of the source ocean shader
   (water_materials.py: N = NearRGB x S x T x sat(0.1 L)), an Add to the emissive, and Set Material Attributes (the emissive). The
   emissive of Single Layer Water is added outside the floor transmittance (BasePassPixelShader.usf): an additive term under the surface.
2. The copies of the instance chain of the plugin ocean (Water_Material_Ocean and its instance parents) onto M_SwcWaterNear; the last one
   has the name of the ocean slot of the client, MI_SwcWater_Ocean. With -SwcWaterMaterials=/SwcWaterCheck/Water/Near only the ocean uses
   it: each other slot falls back to the plugin material (SwcWaterUnreal.cpp, UnrealMaterial).

The parameters (the client sets them on the ocean instance, Water/SwcWaterUnreal.cpp; all 0 by default: no effect):
SwcNearGain (the gain; 0: no add), SwcNearColor (NearRGB of the ocean of the place), SwcNearSun (the source sun / pi, cd/m2),
SwcNearSigma (the extinction of the ocean per cm), SwcNearMode (0 the plugin floor transmittance, 1 the source T), SwcNearDebug.

Environment: SWC_WATER_UNREAL_DEST (default /SwcWaterCheck/Water/Near), SWC_WATER_UNREAL_REPORT, SWC_IMPORT_FORCE=1.
"""
import json
import os

import unreal

DEST = os.environ.get("SWC_WATER_UNREAL_DEST", "/SwcWaterCheck/Water/Near").rstrip("/")
REPORT = os.environ["SWC_WATER_UNREAL_REPORT"]
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
VERSION = "4"   # 1: the first near term; 2: Break / Make Material Attributes; 3: the shore foam (additive); 4: the shore blend (opacity)
if not DEST.startswith("/SwcWaterCheck/"):
    raise RuntimeError(f"the near term goes into the check plugin only, not {DEST}")

PARENT = "/Water/Materials/WaterSurface/Water_Material"
OCEAN = "/Water/Materials/WaterSurface/Water_Material_Ocean"
NAME = "M_SwcWaterNear"
SLOT = "MI_SwcWater_Ocean"
# The IDs of the material attributes (MaterialAttributeDefinitionMap.cpp, InitializeAttributeMap).
EMISSIVE = "B769B54DD08D4440ABC21BA6CD27D0E2"
OPACITY = "B8F50FBA2A754EC19EF672CFEB27BF51"

# The near term (cm and the engine units). Behind and Surface: the linear depth of the scene without the water and of the water pixel; the
# ray in the water L = (Behind - Surface) / cos (the depth difference is along the view axis); u the downward part of the ray; the floor
# transmittance of the plugin exp(-sigma x the depth gap) x exp(-sigma x the vertical depth) (SingleLayerWaterShading.ush) or the source
# T = exp(-rho L (1 + u)); the ramp sat(0.1 L) with L in m; the transmission through the surface (1 - opacity) x (1 - F), F a Schlick
# Fresnel of a flat surface (the up normal) [the first test: an approximation of EnvBRDF].
CODE = r"""
float GapCm = max(Behind - Surface, 0.0);
float3 Dir = -normalize(CameraVector);
float CosView = max(dot(Dir, View.ViewForward), 0.05);
float RayCm = GapCm / CosView;
float Down = max(-Dir.z, 0.0);
float VerticalCm = RayCm * Down;
float TPlugin = exp(-Sigma * GapCm) * exp(-Sigma * VerticalCm);
float TSource = exp(-Sigma * RayCm * (1.0 + Down));
float T = Mode > 0.5 ? TSource : TPlugin;
float Ramp = saturate(0.001 * RayCm);
float NoV = saturate(abs(Dir.z));
float F = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
float3 Near = NearColor * min(Sun, 100000.0) * T * Ramp;
float3 Add = Gain * Near * (1.0 - saturate(Opacity)) * (1.0 - F);
if (Debug > 0.5)
{
    Add = Gain * 1000.0 * (Debug < 1.5 ? float3(Ramp, T, F) : float3(saturate(RayCm / 1000.0), Down, CosView));
}
return max(Add, 0.0);
"""

# The source shore foam of the ocean (water_materials.py, the ocean branch of the foam: research/water-shallow-terms.md): the band of the
# view-depth gap s = sat(FoamSoft q), the foam map m (two samples of the linear foam data), the gloss G; fshore = m sat(m - 2 G s) sat(s - s^2);
# f = fshore x the sun luminance / 10000 x sat(0.5 + shade^2) x a, with the minimum shade 0.5 [the first test: an additive diagnostic,
# gain x f x FoamAmount x 10000 on the emissive; the source blend is f x (FoamAmount x 10000 - C)]. The coordinates of the source ocean:
# FoamUV = world (m) x 0.005 x Tilling + wind, wind = (cos, sin)(WindDirection) x WaveSpeed x WindScale x Time. No crest foam (h = 0).
SHORE = r"""
float q = max(Behind - Surface, 0.0) * 0.01;
float a = saturate(Soft * q);
float2 world = P.xy * 0.01;
float2 dir = float2(cos(WindDirection), sin(WindDirection));
float2 FoamUV = world * 0.005 * Tilling + dir * WaveSpeed * WindScale * Time;
float s = saturate(FoamSoft * q);
float G = 0.7 * Texture2DSample(Gloss, GlossSampler, FoamUV * 0.05).r + 0.3;
float2 u = FoamUV * FoamTilling;
float2 u2 = 2.73 * u - dir * WaveSpeed * WindScale * FoamTransition * 0.1 * Time * FoamTilling;
float m = Texture2DSampleBias(FoamData, FoamDataSampler, u, -1.0).r + 0.5 * Texture2DSampleBias(FoamData, FoamDataSampler, u2, -1.0).r;
float fshore = m * saturate(m - 2.0 * G * s) * saturate(s - s * s);
float sunUnit = dot(min(Sun, 100000.0), float3(0.2126, 0.7152, 0.0722)) / 10000.0;
float f = fshore * sunUnit * 0.5 * a;
float3 Add = Gain * f * FoamAmount * 10000.0;
if (Debug > 0.5)
{
    Add = Gain * 1000.0 * float3(s, saturate(0.5 * m), saturate(4.0 * f));
}
return max(Add, 0.0);
"""
# The blend weight of the shore foam (the same rule as SHORE, no debug): gain x f.
SHORE_F = SHORE.split("float3 Add = ")[0] + "return max(Gain * f, 0.0);\n"
# The source blend C' = (1 - f) C + f x FoamAmount x 10000 as a coverage of the Single Layer Water surface (SwcShoreBlend 1): the opacity is the
# covered part of the surface (the water volume light and the floor get 1 - opacity; BasePassPixelShader.usf), so the opacity takes f of the
# uncovered part, and the base color of that new part is black (the white of the foam is the emissive f x FoamAmount x 10000 of SHORE; a lit
# diffuse surface would add the sun). [I: the reflection of the composite is not proved to scale with the opacity.] Blend 0: the additive
# test of version 3. Out: (the new opacity, the scale of the base color).
COVER = r"""
float Old = saturate(OpacityOld);
float Add = Blend > 0.5 ? saturate(F) * (1.0 - Old) : 0.0;
float New = saturate(Old + Add);
float BaseScale = New > 1e-4 ? saturate(1.0 - Add / New) : 1.0;
return float2(New, BaseScale);
"""
FOAM_TEXTURES = {"FoamData": "/SwcGameContent/Water/Textures/T_SwcWater_FoamData", "Gloss": "/SwcGameContent/Water/Textures/T_SwcWater_Gloss"}

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Water", "/SwcGameContent/Water", DEST.rsplit("/", 1)[0]], True)
report = {"dest": DEST, "version": VERSION, "made": [], "kept": [], "failed": [], "notes": []}


def guid(text):
    value, ok = unreal.GuidLibrary.parse_string_to_guid(text)   # KismetGuidLibrary Parse_StringToGuid: (OutGuid, Success)
    if not ok:
        raise RuntimeError(f"bad guid {text}")
    return value


def custom_inputs(names):
    """The input list of a Custom node."""
    entries = []
    for name in names:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        entries.append(entry)
    return entries


def connect(source, outputs, target, inputs):
    """Connects the first output name and input name that the editor accepts (the pin names of the attribute nodes vary)."""
    for out in outputs:
        for name in inputs:
            if editing.connect_material_expressions(source, out, target, name):
                report["notes"].append(f"{source.get_name()}.{out or '-'} -> {target.get_name()}.{name or '-'}")
                return
    raise RuntimeError(f"no connection {source.get_name()} {outputs} -> {target.get_name()} {inputs}")


def make_parent():
    path = f"{DEST}/{NAME}"
    if assets.does_asset_exist(path) and not force:
        existing = assets.load_asset(path)
        if existing is not None and assets.get_metadata_tag(existing, "SwcVersion") == VERSION:
            report["kept"].append(NAME)
            return existing
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    material = assets.duplicate_asset(PARENT, path)
    if material is None:
        raise RuntimeError(f"no copy of {PARENT}")
    prop = unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES
    final = editing.get_material_property_input_node(material, prop)
    if final is None:
        raise RuntimeError("the parent has no node at its material attributes")
    try:
        final_out = editing.get_material_property_input_node_output_name(material, prop)
    except Exception:
        final_out = ""
    report["notes"].append(f"the final attributes: {final.get_name()} output '{final_out}'")
    x, y = final.get_editor_property("material_expression_editor_x") + 400, final.get_editor_property("material_expression_editor_y")

    # Break / Make Material Attributes (fixed pins; Get / Set Material Attributes make their pins only with an editor graph node, which a
    # commandlet has not: UMaterialExpressionGetMaterialAttributes::PostEditChangeProperty).
    get = editing.create_material_expression(material, unreal.MaterialExpressionBreakMaterialAttributes, x, y)
    connect(final, [final_out, ""], get, ["MaterialAttributes", "", "Material Attributes", "Attr"])

    def scalar(name, value, dy):
        node = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y + dy)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "Swc Near")
        return node

    def vector(name, dy):
        node = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y + dy)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        node.set_editor_property("group", "Swc Near")
        return node

    gain = scalar("SwcNearGain", 0.0, 200)
    near = vector("SwcNearColor", 300)
    sun = vector("SwcNearSun", 400)
    sigma = scalar("SwcNearSigma", 0.003, 500)
    mode = scalar("SwcNearMode", 0.0, 600)
    debug = scalar("SwcNearDebug", 0.0, 700)
    behind = editing.create_material_expression(material, unreal.MaterialExpressionSceneDepthWithoutWater, x, y + 800)
    surface = editing.create_material_expression(material, unreal.MaterialExpressionPixelDepth, x, y + 900)
    camera = editing.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, x, y + 1000)

    custom = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x + 400, y + 400)
    custom.set_editor_property("code", CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "SwcNearTerm")
    names = ["Gain", "NearColor", "Sun", "Sigma", "Mode", "Debug", "Behind", "Surface", "CameraVector", "Opacity"]
    custom_inputs = []
    for name in names:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        custom_inputs.append(entry)
    custom.set_editor_property("inputs", custom_inputs)
    for node, name, out in ((gain, "Gain", ""), (near, "NearColor", "RGB"), (sun, "Sun", "RGB"), (sigma, "Sigma", ""), (mode, "Mode", ""),
                            (debug, "Debug", ""), (behind, "Behind", ""), (surface, "Surface", ""), (camera, "CameraVector", "")):
        connect(node, [out, ""], custom, [name])
    connect(get, ["Opacity"], custom, ["Opacity"])

    add = editing.create_material_expression(material, unreal.MaterialExpressionAdd, x + 800, y)
    connect(get, ["EmissiveColor"], add, ["A"])
    connect(custom, [""], add, ["B"])

    # The shore foam (SHORE): its own Custom node and parameters; the source textures are read, not changed.
    shore_params = {"Gain": scalar("SwcShoreGain", 0.0, 1100), "Debug": scalar("SwcShoreDebug", 0.0, 1200),
                    "FoamAmount": scalar("SwcFoamAmount", 0.5, 1300), "FoamTilling": scalar("SwcFoamTilling", 1.0, 1400),
                    "FoamSoft": scalar("SwcFoamSoft", 1.0, 1500), "FoamTransition": scalar("SwcFoamTransition", 5.0, 1600),
                    "WindDirection": scalar("SwcWindDirection", 0.0, 1700), "WaveSpeed": scalar("SwcWaveSpeed", 1.0, 1800),
                    "WindScale": scalar("SwcWindScale", 0.02, 1900), "Tilling": scalar("SwcTilling", 32.0, 2000),
                    "Soft": scalar("SwcSoft", 1.0, 2100)}
    textures = {}
    for k, (name, path) in enumerate(FOAM_TEXTURES.items()):
        texture = assets.load_asset(path)
        if texture is None:
            raise RuntimeError(f"no source texture {path}")
        node = editing.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, x, y + 2200 + 150 * k)
        node.set_editor_property("parameter_name", f"Swc{name}")
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        textures[name] = node
    position = editing.create_material_expression(material, unreal.MaterialExpressionWorldPosition, x, y + 2500)
    time = editing.create_material_expression(material, unreal.MaterialExpressionTime, x, y + 2600)
    shore = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x + 400, y + 1600)
    shore.set_editor_property("code", SHORE)
    shore.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    shore.set_editor_property("description", "SwcShoreTerm")
    shore_names = list(shore_params) + ["Sun", "Behind", "Surface", "P", "Time", "FoamData", "Gloss"]
    entries = []
    for name in shore_names:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        entries.append(entry)
    shore.set_editor_property("inputs", entries)
    for name, node in shore_params.items():
        connect(node, [""], shore, [name])
    connect(sun, ["RGB", ""], shore, ["Sun"])
    connect(behind, [""], shore, ["Behind"])
    connect(surface, [""], shore, ["Surface"])
    connect(position, [""], shore, ["P"])
    connect(time, [""], shore, ["Time"])
    for name, node in textures.items():
        connect(node, [""], shore, [name])
    total = editing.create_material_expression(material, unreal.MaterialExpressionAdd, x + 950, y)
    connect(add, [""], total, ["A"])
    connect(shore, [""], total, ["B"])

    # The shore blend (COVER): the weight f of the same rule, the new opacity, and the scale of the base color.
    weight = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x + 400, y + 2000)
    weight.set_editor_property("code", SHORE_F)
    weight.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    weight.set_editor_property("description", "SwcShoreWeight")
    weight.set_editor_property("inputs", custom_inputs(shore_names))
    for name, node in shore_params.items():
        connect(node, [""], weight, [name])
    connect(sun, ["RGB", ""], weight, ["Sun"])
    connect(behind, [""], weight, ["Behind"])
    connect(surface, [""], weight, ["Surface"])
    connect(position, [""], weight, ["P"])
    connect(time, [""], weight, ["Time"])
    for name, node in textures.items():
        connect(node, [""], weight, [name])
    blend = scalar("SwcShoreBlend", 1.0, 2700)
    cover = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x + 700, y + 2000)
    cover.set_editor_property("code", COVER)
    cover.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    cover.set_editor_property("description", "SwcShoreCover")
    cover.set_editor_property("inputs", custom_inputs(["OpacityOld", "F", "Blend"]))
    connect(get, ["Opacity"], cover, ["OpacityOld"])
    connect(weight, [""], cover, ["F"])
    connect(blend, [""], cover, ["Blend"])
    opacity = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, x + 900, y + 2000)
    opacity.set_editor_property("r", True)
    connect(cover, [""], opacity, [""])
    scale = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, x + 900, y + 2100)
    scale.set_editor_property("g", True)
    connect(cover, [""], scale, [""])
    base = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, x + 950, y + 2100)
    connect(get, ["BaseColor"], base, ["A"])
    connect(scale, [""], base, ["B"])

    # Each attribute of the parent goes from Break to the pin of the same name on Make; the emissive goes through the Add. The break outputs
    # (MaterialExpressions.cpp, UMaterialExpressionBreakMaterialAttributes): the names below and CustomizedUV0..7.
    put = editing.create_material_expression(material, unreal.MaterialExpressionMakeMaterialAttributes, x + 1100, y)
    outputs = ["Metallic", "Specular", "Roughness", "Anisotropy", "OpacityMask", "Normal", "Tangent",
               "WorldPositionOffset", "SubsurfaceColor", "ClearCoat", "ClearCoatRoughness", "AmbientOcclusion", "Refraction",
               "PixelDepthOffset", "ShadingModel", "Displacement"] + [f"CustomizedUV{i}" for i in range(8)]
    pins = list(editing.get_material_expression_input_names(put))
    report["notes"].append(f"the inputs of Make: {pins}")

    def norm(text):
        return str(text).replace(" ", "").replace("_", "").lower().replace("customizeduvs", "customizeduv")

    missing = []
    for out in outputs:
        match = [pin for pin in pins if norm(pin) == norm(out)]
        if not match or not editing.connect_material_expressions(get, out, put, str(match[0])):
            missing.append(out)
    report["notes"].append(f"not connected: {missing}")
    # A main attribute that does not go through would take the default of Make (for example no waves: WorldPositionOffset).
    main = [out for out in missing if not out.startswith("CustomizedUV")]
    if main:
        raise RuntimeError(f"the attributes {main} do not go from Break to Make")
    connect(total, [""], put, ["EmissiveColor", "Emissive Color"])
    connect(opacity, [""], put, ["Opacity"])
    connect(base, [""], put, ["BaseColor", "Base Color"])
    try:
        report["notes"].append(f"customized UVs of the parent: {material.get_editor_property('num_customized_u_vs')}")
    except Exception:
        pass
    if not editing.connect_material_property(put, "", prop):
        raise RuntimeError("no connection to the material attributes of the output")
    editing.recompile_material(material)
    assets.set_metadata_tag(material, "SwcVersion", VERSION)
    assets.save_loaded_asset(material, only_if_is_dirty=False)
    report["made"].append(NAME)
    return material


def make_ocean(parent):
    """The instance chain of the plugin ocean, from the material up to Water_Material_Ocean, as copies onto the new parent."""
    chain = []
    node = assets.load_asset(OCEAN)
    while isinstance(node, unreal.MaterialInstance):
        chain.append(node)
        node = node.get_editor_property("parent")
    if node is None or node.get_path_name().split(".")[0] != PARENT:
        raise RuntimeError(f"the ocean chain does not end at {PARENT}: {node.get_path_name() if node else None}")
    previous = parent
    for index, instance in enumerate(reversed(chain)):
        name = SLOT if index == len(chain) - 1 else f"MI_SwcWaterNear_{instance.get_name()}"
        path = f"{DEST}/{name}"
        if assets.does_asset_exist(path):
            assets.delete_asset(path)
        copy = assets.duplicate_asset(instance.get_path_name().split(".")[0], path)
        editing.set_material_instance_parent(copy, previous)
        editing.update_material_instance(copy)
        assets.set_metadata_tag(copy, "SwcVersion", VERSION)
        assets.save_loaded_asset(copy, only_if_is_dirty=False)
        report["made"].append(f"{name} (a copy of {instance.get_name()})")
        previous = copy


try:
    make_ocean(make_parent())
except Exception as error:
    report["failed"].append(str(error))
with open(REPORT, "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC WATER NEAR " + json.dumps(report))
