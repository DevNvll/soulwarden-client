"""The landmarks of the world (AST-209, research/maelstrom.md): the sky maelstrom of the Shattered Mountain. Reads
SWC_CONTENT/landmarks/maelstrom.json (converter/swc_convert/landmarks.py) and makes, all new, in /SwcGameContent/Landmarks/Maelstrom:
- the five meshes (LOD 0, no Nanite: the layers are translucent; the pipeline P_SwcMesh of import_content.py),
- the textures of the materials that are not in the plugin yet (the folders of the texture records; an existing texture stays),
- two parents: M_SwcVortexLayer (the Illum layers and tendrils: Translucent, lit per pixel, two sided, the fog per pixel; the base
  color D x DiffuseColor, the opacity D.a x Opacity, the normal map, the emission of the source x EmissiveScale) and
  M_SwcVortexCore (the core, Fxmeshadvancedtransp: Additive, Unlit, two sided, the fog per pixel;
  saturate(pow(S.rgb x C.rgb, GlobalColorFalloff)) x C.a x pow(S.a, GlobalAlphaFalloff) x Opacity x EmissiveScale),
- one material instance for each source material (MI_STEM): the textures, the scalars, and the UV rule of each slot (the TexMod:
  tile, offset, the rotation in degrees per second about its centre; the material Time node).
It changes no existing asset of the plugin: MPC_SwcMaterials and the base textures are only read.
Environment: SWC_LANDMARKS (the JSON), SWC_LANDMARKS_REPORT (the report file), SWC_LANDMARKS_FORCE (1: make all again)."""
import json
import os
import sys
import time

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

PARENT_VERSION = "2"   # 2: the back light of %TRANSMITTANCE (MPC_SwcLandmarks)
doc = json.load(open(os.environ["SWC_LANDMARKS"]))
content = os.path.dirname(os.path.dirname(os.environ["SWC_LANDMARKS"]))
force = os.environ.get("SWC_LANDMARKS_FORCE") == "1"
folder = doc["folder"]
assets, editing, tools, MP = sm.assets, sm.editing, sm.tools, sm.MP
cf = unreal.CustomMaterialOutputType
st = unreal.MaterialSamplerType
report = {"meshes": {}, "textures": {"imported": 0, "existing": 0}, "parents": [], "instances": [], "errors": []}
started = time.time()

# The UV of a slot: uv x tile + offset, then turned about the centre by t x the rate (TexMod rotation type 2; research/maelstrom.md
# "Texture motion"). M = (tile U, tile V, offset U, offset V), R = (centre U, centre V, degrees per second, unused), T = seconds.
UV_HLSL = """float2 uv = UV * M.xy + M.zw;
float a = radians(R.z) * T;
float s = sin(a);
float c = cos(a);
float2 d = uv - R.xy;
return float2(c * d.x - s * d.y, s * d.x + c * d.y) + R.xy;"""
# The emission of the Illum layers (swc_materials EMISSIVE_HLSL, AST-207): E = Map.rgb x lum(Map.rgb)^(gamma - 1) x I.rgb x Map.a;
# output = C.rgb x C.a x 0.1 x E x Scale. Map and I: the sRGB or the linear sampler by the color space of the file (P.y, P.z).
# P = (EmittanceMapGamma, EmittanceSrgb, IntensitySrgb, unused).
LAYER_EMISSIVE_HLSL = """float3 w = float3(0.2126, 0.7152, 0.0722);
float4 M = P.y > 0.5 ? MapS : Map;
float4 In = P.z > 0.5 ? IS : I;
float3 e = M.rgb * pow(max(dot(M.rgb, w), 1e-6), P.x - 1.0) * In.rgb * M.a;
return C.rgb * C.a * 0.1 * e * Scale;"""
# The back light of the Illum layers with %TRANSMITTANCE (the forward Illum path of the source: clusterforward_defaultlighting
# default_frag_custom_per_light; illum.cfxb cShadingBack): for each light, pow(saturate(N.L x 0.6 + 0.4), 1 / (1 - NormalViewDependency))
# x the light x cShadingBack, cShadingBack = BackDiffuseMultiplier x Opacity map (R) x TransmittanceColor; then the albedo. One light
# here: the light of the hour (MPC_SwcLandmarks: SunDirection toward the light, SunColor in lux; a diffuse surface: x 1 / pi to cd/m2).
# The front light (the Lambert term) comes from the lit translucency of Unreal. B = (BackDiffuseMultiplier, NormalViewDependency).
BACK_HLSL = """float t = pow(saturate(dot(normalize(N), normalize(L)) * 0.6 + 0.4), 1.0 / max(1.0 - B.y, 0.01));
return D * Sun.rgb * (1.0 / 3.14159265) * t * B.x * Tr * TC.rgb;"""
# The core (Fxmeshadvancedtransp with an empty generation mask; research/maelstrom.md "Core FX material"): the source map ($CustomMap,
# sRGB), its RGB x Emittance RGB, ^GlobalColorFalloff, saturated, x Emittance A; the alpha: source alpha ^GlobalAlphaFalloff x the
# instance opacity (GlobalAlphaFill and GlobalAlphaStrength 1). Additive: the color x the alpha. F = (GlobalColorFalloff,
# GlobalAlphaFalloff, Opacity, unused).
CORE_HLSL = """float3 e = saturate(pow(max(S.rgb * C.rgb, 0.0), F.x)) * C.a;
float a = pow(saturate(S.a), F.y) * F.z;
return e * a * Scale;"""


def material_folder():
    return folder + "/Materials"


def existing(path):
    """@return The asset of a package path, or None (a load by the path: the asset registry of the commandlet did not always list
    the new folder of an earlier run)."""
    try:
        return unreal.load_asset(path)
    except Exception:
        return None


def new_parent(name):
    path = f"{material_folder()}/{name}"
    material = existing(path)
    if material is not None:
        editing.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, material_folder(), unreal.Material, unreal.MaterialFactoryNew())
    return material


def uv_node(material, name, coords, time_node, x, y):
    """@return The UV of a slot (UV_HLSL) with its parameters NAMEMod and NAMERot."""
    mod = sm.vector(material, name + "Mod", (1.0, 1.0, 0.0, 0.0), x - 300, y)
    rot = sm.vector(material, name + "Rot", (0.5, 0.5, 0.0, 0.0), x - 300, y + 120)
    uv = sm.custom(material, UV_HLSL, cf.CMOT_FLOAT2, ("UV", "M", "R", "T"), x, y)
    editing.connect_material_expressions(coords, "", uv, "UV")
    editing.connect_material_expressions(mod, "RGBA", uv, "M")
    editing.connect_material_expressions(rot, "RGBA", uv, "R")
    editing.connect_material_expressions(time_node, "", uv, "T")
    return uv


def textured(material, name, kind, default, uv, x, y):
    s = sm.sampler(material, name, kind, default, x, y)
    editing.connect_material_expressions(uv, "", s, "UVs")
    return s


def emissive_scale(material, x, y):
    node = sm.node(material, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", unreal.load_asset(f"{sm.BASE}/MPC_SwcMaterials"))   # read only (EmissiveScale)
    node.set_editor_property("parameter_name", "EmissiveScale")
    return node


def landmark_collection():
    """@return MPC_SwcLandmarks (new): SunDirection (toward the light of the hour) and SunColor (lux); World/SwcLandmarks sets them."""
    path = f"{material_folder()}/MPC_SwcLandmarks"
    mpc = existing(path) or tools.create_asset("MPC_SwcLandmarks", material_folder(), unreal.MaterialParameterCollection,
                                               unreal.MaterialParameterCollectionFactoryNew())
    vectors = list(mpc.get_editor_property("vector_parameters"))
    for name, value in (("SunDirection", (0.0, 0.0, 1.0, 0.0)), ("SunColor", (0.0, 0.0, 0.0, 1.0))):
        if not any(str(v.get_editor_property("parameter_name")) == name for v in vectors):
            parameter = unreal.CollectionVectorParameter()
            parameter.set_editor_property("parameter_name", name)
            parameter.set_editor_property("default_value", unreal.LinearColor(*value))
            vectors.append(parameter)
    mpc.set_editor_property("vector_parameters", vectors)
    assets.save_loaded_asset(mpc)
    return mpc


def collection_vector(material, mpc, name, x, y):
    node = sm.node(material, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", mpc)
    node.set_editor_property("parameter_name", name)
    return node


def layer_parent():
    material = new_parent("M_SwcVortexLayer")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    white_data = unreal.load_asset(f"{sm.BASE}/T_SwcWhiteData")
    flat = unreal.load_asset("/Engine/EngineMaterials/DefaultNormal")
    coords = sm.node(material, unreal.MaterialExpressionTextureCoordinate, -2200, 0)
    clock = sm.node(material, unreal.MaterialExpressionTime, -2200, 200)
    uv_d = uv_node(material, "Diffuse", coords, clock, -1500, 0)
    uv_n = uv_node(material, "Normal", coords, clock, -1500, 400)
    uv_e = uv_node(material, "Emittance", coords, clock, -1500, 800)
    uv_i = uv_node(material, "Intensity", coords, clock, -1500, 1300)
    diffuse = textured(material, "Diffuse", st.SAMPLERTYPE_COLOR, white, uv_d, -1100, 0)
    normal = textured(material, "Normal", st.SAMPLERTYPE_NORMAL, flat, uv_n, -1100, 400)
    em = textured(material, "Emittance", st.SAMPLERTYPE_COLOR, white, uv_e, -1100, 800)
    em_lin = textured(material, "EmittanceLinear", st.SAMPLERTYPE_MASKS, white_data, uv_e, -1100, 1000)
    inten = textured(material, "Intensity", st.SAMPLERTYPE_COLOR, white, uv_i, -1100, 1300)
    inten_lin = textured(material, "IntensityLinear", st.SAMPLERTYPE_MASKS, white_data, uv_i, -1100, 1500)
    white_linear = unreal.load_asset(f"{sm.BASE}/T_SwcWhiteLinear")
    trans = textured(material, "Transmission", st.SAMPLERTYPE_ALPHA, white_linear, uv_d, -1100, 2100)   # at the base UV (baseTC)
    base = sm.multiply(material, (diffuse, "RGB"), (sm.vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -1100, -200), "RGB"),
                       -600, -100)
    editing.connect_material_property(base, "", MP.MP_BASE_COLOR)
    opacity = sm.node(material, unreal.MaterialExpressionSaturate, -400, 200)
    editing.connect_material_expressions(sm.multiply(material, (diffuse, "A"), (sm.scalar(material, "Opacity", 1.0, -900, 250), ""),
                                                     -600, 200), "", opacity, "")
    editing.connect_material_property(opacity, "", MP.MP_OPACITY)
    editing.connect_material_property(normal, "RGB", MP.MP_NORMAL)
    zero = sm.scalar(material, "Specular", 0.0, -600, 450)   # the Specular color of these materials is 0
    editing.connect_material_property(zero, "", MP.MP_SPECULAR)
    editing.connect_material_property(sm.scalar(material, "Roughness", 1.0, -600, 550), "", MP.MP_ROUGHNESS)
    p = sm.custom(material, "return float4(G, ES, IS, 0.0);", cf.CMOT_FLOAT4, ("G", "ES", "IS"), -800, 1700)
    editing.connect_material_expressions(sm.scalar(material, "EmittanceMapGamma", 1.0, -1100, 1700), "", p, "G")
    editing.connect_material_expressions(sm.scalar(material, "EmittanceSrgb", 1.0, -1100, 1780), "", p, "ES")
    editing.connect_material_expressions(sm.scalar(material, "IntensitySrgb", 1.0, -1100, 1860), "", p, "IS")
    emission = sm.custom(material, LAYER_EMISSIVE_HLSL, cf.CMOT_FLOAT3, ("Map", "MapS", "I", "IS", "C", "P", "Scale"), -400, 1000)
    for pin, (node, out) in {"Map": (em_lin, "RGBA"), "MapS": (em, "RGBA"), "I": (inten_lin, "RGBA"), "IS": (inten, "RGBA"),
                             "C": (sm.vector(material, "EmittanceColor", (0.0, 0.0, 0.0, 0.0), -800, 1500), "RGBA"),
                             "P": (p, ""), "Scale": (emissive_scale(material, -800, 1950), "")}.items():
        editing.connect_material_expressions(node, out, emission, pin)
    mpc = landmark_collection()
    bn = sm.custom(material, "return float2(BD, NV);", cf.CMOT_FLOAT2, ("BD", "NV"), -800, 2300)
    editing.connect_material_expressions(sm.scalar(material, "BackDiffuseMultiplier", 0.0, -1100, 2300), "", bn, "BD")
    editing.connect_material_expressions(sm.scalar(material, "NormalViewDependency", 0.0, -1100, 2380), "", bn, "NV")
    back = sm.custom(material, BACK_HLSL, cf.CMOT_FLOAT3, ("N", "L", "B", "D", "Sun", "Tr", "TC"), -400, 2200)
    for pin, (node, out) in {"N": (sm.node(material, unreal.MaterialExpressionPixelNormalWS, -800, 2450), ""),
                             "L": (collection_vector(material, mpc, "SunDirection", -800, 2550), ""),
                             "B": (bn, ""), "D": (base, ""), "Sun": (collection_vector(material, mpc, "SunColor", -800, 2650), ""),
                             "Tr": (trans, "R"),
                             "TC": (sm.vector(material, "TransmittanceColor", (0.0, 0.0, 0.0, 0.0), -800, 2750), "RGBA")}.items():
        editing.connect_material_expressions(node, out, back, pin)
    total = sm.node(material, unreal.MaterialExpressionAdd, -200, 1500)
    editing.connect_material_expressions(emission, "", total, "A")
    editing.connect_material_expressions(back, "", total, "B")
    editing.connect_material_property(total, "", MP.MP_EMISSIVE_COLOR)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("compute_fog_per_pixel", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    report["parents"].append("M_SwcVortexLayer")
    return material


def core_parent():
    material = new_parent("M_SwcVortexCore")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    coords = sm.node(material, unreal.MaterialExpressionTextureCoordinate, -1800, 0)
    clock = sm.node(material, unreal.MaterialExpressionTime, -1800, 200)
    uv = uv_node(material, "Source", coords, clock, -1200, 0)
    source = textured(material, "Source", st.SAMPLERTYPE_COLOR, white, uv, -900, 0)
    f = sm.custom(material, "return float4(CF, AF, O, 0.0);", cf.CMOT_FLOAT4, ("CF", "AF", "O"), -700, 400)
    editing.connect_material_expressions(sm.scalar(material, "GlobalColorFalloff", 1.0, -1000, 350), "", f, "CF")
    editing.connect_material_expressions(sm.scalar(material, "GlobalAlphaFalloff", 1.0, -1000, 430), "", f, "AF")
    editing.connect_material_expressions(sm.scalar(material, "Opacity", 1.0, -1000, 510), "", f, "O")
    core = sm.custom(material, CORE_HLSL, cf.CMOT_FLOAT3, ("S", "C", "F", "Scale"), -400, 100)
    editing.connect_material_expressions(source, "RGBA", core, "S")
    editing.connect_material_expressions(sm.vector(material, "EmittanceColor", (0.0, 0.0, 0.0, 0.0), -700, 200), "RGBA", core, "C")
    editing.connect_material_expressions(f, "", core, "F")
    editing.connect_material_expressions(emissive_scale(material, -700, 600), "", core, "Scale")
    editing.connect_material_property(core, "", MP.MP_EMISSIVE_COLOR)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("compute_fog_per_pixel", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    report["parents"].append("M_SwcVortexCore")
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


def mesh_asset(product, record):
    stem = record["unreal"].rsplit("/", 1)[-1]
    if not force and assets.does_directory_exist(record["unreal"]) and sm.first_asset(record["unreal"], "StaticMesh") is not None:
        report["meshes"][stem] = "existing " + sm.first_asset(record["unreal"], "StaticMesh").get_path_name()
        return
    parameters = unreal.ImportAssetParameters()
    parameters.set_editor_property("is_automated", True)
    parameters.set_editor_property("replace_existing", True)
    parameters.set_editor_property("override_pipelines", [unreal.SoftObjectPath(f"{sm.BASE}/P_SwcMesh")])   # no Nanite, no link pipeline
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
        record["unreal"], unreal.InterchangeManager.create_source_data(os.path.join(content, record["file"])), parameters)
    assets.save_directory(record["unreal"], only_if_is_dirty=False, recursive=True)   # Interchange does not save the new packages
    mesh = sm.first_asset(record["unreal"], "StaticMesh")
    if mesh is None:
        raise RuntimeError(f"{product}: no static mesh after the import")
    report["meshes"][stem] = mesh.get_path_name()


def instance(key, spec, parents):
    stem = key.rsplit("/", 1)[-1][:-4]
    name = "MI_" + stem
    path = f"{material_folder()}/{name}"
    mi = existing(path) or tools.create_asset(
        name, material_folder(), unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(mi, parents[spec["parent"]])
    for parameter, texture_key in spec["textures"].items():
        editing.set_material_instance_texture_parameter_value(mi, parameter, texture_asset(texture_key))
    for parameter, value in spec["scalars"].items():
        editing.set_material_instance_scalar_parameter_value(mi, parameter, float(value))
    for parameter, value in spec["vectors"].items():
        editing.set_material_instance_vector_parameter_value(mi, parameter, unreal.LinearColor(*[float(v) for v in value[:4]]))
    for slot, rule in spec["uv"].items():
        editing.set_material_instance_vector_parameter_value(
            mi, slot + "Mod", unreal.LinearColor(rule["tile"][0], rule["tile"][1], rule["offset"][0], rule["offset"][1]))
        editing.set_material_instance_vector_parameter_value(
            mi, slot + "Rot", unreal.LinearColor(rule["center"][0], rule["center"][1], rule["degrees per second"], 0.0))
    editing.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    report["instances"].append(name)


try:
    for product, record in doc["meshes"].items():
        mesh_asset(product, record)
    parents = {"vortex layer": layer_parent(), "vortex core": core_parent()}
    for key, spec in doc["materials"].items():
        instance(key, spec, parents)
except Exception as error:   # the report has it; the tool exits 1
    report["errors"].append(str(error)[:300])
report["seconds"] = round(time.time() - started, 1)
with open(os.environ["SWC_LANDMARKS_REPORT"], "w") as handle:
    json.dump(report, handle, indent=1)
sm.log("landmarks " + json.dumps(report))
