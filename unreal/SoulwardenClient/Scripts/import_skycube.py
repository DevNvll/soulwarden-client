"""The source cloud layers of the water sky cube (AST-200 part 5; research/water-sky-cube-list.md, research/e5-distance-clouds.md).
Reads SWC_CONTENT/water/skycube.json (converter/swc_convert/skycube.py) and makes, all new, in /SwcGameContent/Water/SkyCube:
- one mesh for each subset of the sky dome objects/sky/skydome_a.cgf (no Nanite; the pipeline P_SwcMesh; the vertex RGBA and the tangents),
- the mask textures that the plugin does not have yet,
- the parent M_SwcDistanceClouds (the normal DistanceCloudsPS of the source): Translucent, Unlit, two sided, no depth write; the mask
  red channel R at uv = the UV of the TexMod; opacity = R(uv) x the vertex alpha (below 0.001: none); alpha = opacity ^ (AlphaSaturation
  / (Opacity + 0.01)) (opacity^(AlphaSaturation / 1.01) at the full opacity of these layers); the density = the sum of R(uv + i x d),
  i = 0..7, d = the tangent-space direction toward the light (xy) x StepSize; c = exp2(-Attenuation x density); RGB = (SkyColorMultiplier
  x CloudSky x (1 - c) + SunColorMultiplier x CloudSun x c) x the vertex RGB, in source units x SourceUnit (10,000 cd/m2); source-alpha
  blending. CloudSun, CloudSky, and the light direction are dynamic values of the client (USwcWaterSubsystem). The fog of the
  capture applies to the layer (Unreal translucency fog: with the same fog on the sky, this is the fog once on the composed cube [I]).
- one instance for each layer (MI_skydome_a_layerN).
It changes no existing asset. Environment: SWC_SKYCUBE (the JSON), SWC_SKYCUBE_REPORT (the report), SWC_SKYCUBE_FORCE (1: make all again)."""
import json
import os
import sys
import time

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

doc = json.load(open(os.environ["SWC_SKYCUBE"]))
content = os.path.dirname(os.path.dirname(os.environ["SWC_SKYCUBE"]))
force = os.environ.get("SWC_SKYCUBE_FORCE") == "1"
folder = doc["folder"]
assets, editing, tools, MP = sm.assets, sm.editing, sm.tools, sm.MP
cf = unreal.CustomMaterialOutputType
st = unreal.MaterialSamplerType
report = {"meshes": {}, "textures": {"imported": 0, "existing": 0}, "instances": [], "errors": []}
started = time.time()

# The UV of the TexMod (research/e5-distance-clouds.md "Texture and UV"): uv x tile + offset + the oscillator offsets. O = (type, rate,
# amplitude, phase) of U and of V: type 2 a constant scroll t x rate x amplitude; type 4 a sine pan amplitude x sin(2 pi (frac(t x rate)
# + phase)); else none. T: seconds (the texture clock of the source; its pause and reset rules are open).
UV_HLSL = """float2 o = 0.0;
float4 oscs[2] = { OU, OV };
for (int k = 0; k < 2; k++) {
  float4 s = oscs[k];
  float v = 0.0;
  if (abs(s.x - 2.0) < 0.5) v = T * s.y * s.z;
  else if (abs(s.x - 4.0) < 0.5) v = s.z * sin(6.2831853 * (frac(T * s.y) + s.w));
  if (k == 0) o.x = v; else o.y = v;
}
return UV * M.xy + M.zw + o;"""
CLOUD_HLSL = """float r0 = Texture2DSample(Mask, MaskSampler, uv).r;
float opacity = r0 * VA;
if (opacity < 0.001) return float4(0.0, 0.0, 0.0, 0.0);
float2 d = normalize(Q).xy * P.x;
float density = 0.0;
for (int i = 0; i < 8; i++) density += Texture2DSample(Mask, MaskSampler, uv + i * d).r;
float c = exp2(-P.y * density);
float3 rgb = (P.w * Sky.rgb * (1.0 - c) + P.z * Sun.rgb * c) * VC * Unit;
float a = pow(saturate(opacity), A.x / (A.y + 0.01));
return float4(max(rgb, 0.0), saturate(a));"""


def material_folder():
    return folder + "/Materials"


def existing(path):
    try:
        return unreal.load_asset(path)
    except Exception:
        return None


def clouds_parent():
    path = f"{material_folder()}/M_SwcDistanceClouds"
    material = existing(path)
    if material is not None:
        editing.delete_all_material_expressions(material)
    else:
        material = tools.create_asset("M_SwcDistanceClouds", material_folder(), unreal.Material, unreal.MaterialFactoryNew())
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    coords = sm.node(material, unreal.MaterialExpressionTextureCoordinate, -1800, 0)
    time_node = sm.node(material, unreal.MaterialExpressionTime, -1800, 100)
    uv = sm.custom(material, UV_HLSL, cf.CMOT_FLOAT2, ("UV", "M", "OU", "OV", "T"), -1400, 0)
    for pin, (src, out) in {"UV": (coords, ""), "M": (sm.vector(material, "UVTile", (1.0, 1.0, 0.0, 0.0), -1800, 200), "RGBA"),
                            "OU": (sm.vector(material, "UOscillator", (0.0, 0.0, 0.0, 0.0), -1800, 300), "RGBA"),
                            "OV": (sm.vector(material, "VOscillator", (0.0, 0.0, 0.0, 0.0), -1800, 400), "RGBA"),
                            "T": (time_node, "")}.items():
        editing.connect_material_expressions(src, out, uv, pin)
    mask = sm.node(material, unreal.MaterialExpressionTextureObjectParameter, -1400, 300)
    mask.set_editor_property("parameter_name", "Mask")
    mask.set_editor_property("texture", white)
    mask.set_editor_property("sampler_type", st.SAMPLERTYPE_MASKS)
    # The direction toward the light in the tangent frame of the mesh (the source VS: dot(T, L), dot(B, L), dot(N, L)).
    sun_dir = sm.vector(material, "SunDirection", (0.0, 0.0, 1.0, 0.0), -1800, 600)
    to_tangent = sm.node(material, unreal.MaterialExpressionTransform, -1400, 600)
    to_tangent.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    to_tangent.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    editing.connect_material_expressions(sun_dir, "RGB", to_tangent, "")
    vertex = sm.node(material, unreal.MaterialExpressionVertexColor, -1400, 800)
    p = sm.custom(material, "return float4(SS, AT, SUN, SKY);", cf.CMOT_FLOAT4, ("SS", "AT", "SUN", "SKY"), -1400, 1000)
    for i, (pin, name, default) in enumerate((("SS", "StepSize", 0.01), ("AT", "Attenuation", 0.5),
                                              ("SUN", "SunColorMultiplier", 1.0), ("SKY", "SkyColorMultiplier", 1.0))):
        editing.connect_material_expressions(sm.scalar(material, name, default, -1700, 1000 + 80 * i), "", p, pin)
    a = sm.custom(material, "return float2(AS, OP);", cf.CMOT_FLOAT2, ("AS", "OP"), -1400, 1400)
    editing.connect_material_expressions(sm.scalar(material, "AlphaSaturation", 1.0, -1700, 1400), "", a, "AS")
    editing.connect_material_expressions(sm.scalar(material, "Opacity", 1.0, -1700, 1480), "", a, "OP")
    cloud = sm.custom(material, CLOUD_HLSL, cf.CMOT_FLOAT4, ("uv", "Mask", "Q", "VC", "VA", "P", "A", "Sun", "Sky", "Unit"), -900, 500)
    for pin, (src, out) in {"uv": (uv, ""), "Mask": (mask, ""), "Q": (to_tangent, ""), "VC": (vertex, ""), "VA": (vertex, "A"),
                            "P": (p, ""), "A": (a, ""),
                            "Sun": (sm.vector(material, "CloudSun", (0.0, 0.0, 0.0, 0.0), -1400, 1600), "RGB"),
                            "Sky": (sm.vector(material, "CloudSky", (0.0, 0.0, 0.0, 0.0), -1400, 1700), "RGB"),
                            "Unit": (sm.scalar(material, "SourceUnit", 10000.0, -1400, 1800), "")}.items():
        editing.connect_material_expressions(src, out, cloud, pin)
    rgb = sm.mask(material, cloud, "", "rgb", -600, 500)
    alpha = sm.mask(material, cloud, "", "a", -600, 650)
    editing.connect_material_property(rgb, "", MP.MP_EMISSIVE_COLOR)
    editing.connect_material_property(alpha, "", MP.MP_OPACITY)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("compute_fog_per_pixel", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


def texture_asset(key):
    record = doc["textures"][key]
    texture = sm.first_asset(record["unreal"], "Texture2D") if assets.does_directory_exist(record["unreal"]) else None
    if texture is not None:
        report["textures"]["existing"] += 1
        return texture
    texture = sm.import_png(os.path.join(content, record["file"]), record["unreal"])
    if texture is None:
        raise RuntimeError(f"{key}: no texture after the import")
    sm.texture_settings(texture, record["color"])
    assets.save_loaded_asset(texture)
    report["textures"]["imported"] += 1
    return texture


def mesh_asset(layer):
    path = layer["unreal"]
    stem = path.rsplit("/", 1)[-1]
    if not force and assets.does_directory_exist(path) and sm.first_asset(path, "StaticMesh") is not None:
        report["meshes"][stem] = "existing " + sm.first_asset(path, "StaticMesh").get_path_name()
        return
    parameters = unreal.ImportAssetParameters()
    parameters.set_editor_property("is_automated", True)
    parameters.set_editor_property("replace_existing", True)
    parameters.set_editor_property("override_pipelines", [unreal.SoftObjectPath(f"{sm.BASE}/P_SwcMesh")])   # no Nanite, no link pipeline
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
        path, unreal.InterchangeManager.create_source_data(os.path.join(content, layer["file"])), parameters)
    assets.save_directory(path, only_if_is_dirty=False, recursive=True)   # Interchange does not save the new packages
    mesh = sm.first_asset(path, "StaticMesh")
    if mesh is None:
        raise RuntimeError(f"{stem}: no static mesh after the import")
    report["meshes"][stem] = mesh.get_path_name()


def instance(layer, parent):
    spec = layer["spec"]
    name = "MI_" + layer["name"]
    path = f"{material_folder()}/{name}"
    mi = existing(path) or tools.create_asset(
        name, material_folder(), unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(mi, parent)
    for parameter, texture_key in spec["textures"].items():
        editing.set_material_instance_texture_parameter_value(mi, parameter, texture_asset(texture_key))
    for parameter, value in spec["scalars"].items():
        editing.set_material_instance_scalar_parameter_value(mi, parameter, float(value))
    for parameter, value in spec["vectors"].items():
        editing.set_material_instance_vector_parameter_value(mi, parameter, unreal.LinearColor(*[float(v) for v in value[:4]]))
    editing.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    report["instances"].append(name)


try:
    for layer in doc["layers"]:
        mesh_asset(layer)
    parent = clouds_parent()
    for layer in doc["layers"]:
        instance(layer, parent)
except Exception as error:   # the report has it; the tool exits 1
    report["errors"].append(str(error)[:300])
report["seconds"] = round(time.time() - started, 1)
with open(os.environ["SWC_SKYCUBE_REPORT"], "w") as handle:
    json.dump(report, handle, indent=1)
sm.log("skycube " + json.dumps(report))
