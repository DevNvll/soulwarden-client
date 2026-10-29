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
VERSION = "1"   # 1: the first near term (the plugin floor T, a flat-surface Fresnel)
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

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Water", DEST.rsplit("/", 1)[0]], True)
report = {"dest": DEST, "version": VERSION, "made": [], "kept": [], "failed": [], "notes": []}


def guid(text):
    value, ok = unreal.GuidLibrary.parse_string_to_guid(text)   # KismetGuidLibrary Parse_StringToGuid: (OutGuid, Success)
    if not ok:
        raise RuntimeError(f"bad guid {text}")
    return value


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

    get = editing.create_material_expression(material, unreal.MaterialExpressionGetMaterialAttributes, x, y)
    get.set_editor_property("attribute_get_types", [guid(EMISSIVE), guid(OPACITY)])
    connect(final, [final_out, ""], get, ["MaterialAttributes", "", "Material Attributes"])

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
    connect(get, ["Opacity", "Opacity "], custom, ["Opacity"])

    add = editing.create_material_expression(material, unreal.MaterialExpressionAdd, x + 800, y)
    connect(get, ["EmissiveColor", "Emissive Color", "Emissive"], add, ["A"])
    connect(custom, [""], add, ["B"])

    put = editing.create_material_expression(material, unreal.MaterialExpressionSetMaterialAttributes, x + 1100, y)
    put.set_editor_property("attribute_set_types", [guid(EMISSIVE)])
    connect(final, [final_out, ""], put, ["MaterialAttributes", "Material Attributes", ""])
    connect(add, [""], put, ["EmissiveColor", "Emissive Color", "Emissive"])
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
