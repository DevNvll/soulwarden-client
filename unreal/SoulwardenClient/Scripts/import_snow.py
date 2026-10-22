"""The falling snow in the world (AST-257, docs/ENVIRONMENT.md; research/snow.md sections 2 and 5): the flake atlas of the engine
(engineassets/textures/snowflakes.dds: 256 x 256, 2 x 2 tiles; R the far mask, G the near mask, B and A a scene displacement) as LINEAR
data, /SwcGameContent/Environment/Snow/T_SwcSnowflakesLinear, and the flake material /SwcGameContent/Environment/M_SwcSnowFlake for the
instanced flake mesh of the client (Environment/SwcSnow.cpp). Run through tools/import-snow.sh (the import lock). This script makes only
these two assets (new names); it changes no other asset.

The vertex part (the world position offset): each quad of the mesh is at the place of its flake; UV0 the corner (-1 to 1), UV1 the
random scale (0.005 to 0.010) and the phase, UV2 the index of the flake in its cluster and its atlas tile. The source turbulence
(Turbulence x (sin, cos) of phase x 2 pi + TurbulenceFreq x time, 1 = 1 m), then the corner toward the camera axes with the half size
TanHalfFov x FlakeSize x scale x depth^0.25 (m; the clip expansion of the source, posteffectsgame.cfxb.txt:614-665). The flakes with an
index at or above Count get no area.
The pixel part (additive, unlit): the atlas tile, the near mask within 1 m of the near plane, the soft depth fade (0.5 m), the roof map
(RoofMap: the height of the first surface under the sky, Unreal cm; RoofOrigin: its low corner relative to the camera, its side, and 1 when
it is ready; CameraZ), and the sun term of the source (saturate(dot(light, the bent flake normal) x 0.75 + 0.25)) x Light.
The surface snow (research/snow.md section 3, deferredsnow.cfxb.txt lines 155 to 280): the material function
/SwcGameContent/Environment/MF_SwcSnowSurface that the world and terrain masters call before the lighting (inputs and outputs BaseColor,
Metallic, Specular, Roughness, Normal in tangent space), the collection /SwcGameContent/Environment/MPC_SwcWeather (the client sets it
each frame: SnowCenter x y z (cm) and 1 / radius; SnowAmounts snow, frost, freezing, and 1 when a Snow volume is near; SnowBrightness
snow and frost; RoofOrigin x y of the roof map, its side, and 1 when it is ready), the roof map render target
/SwcGameContent/Environment/Snow/RT_SwcWeatherOcclusion (96 x 96 float heights, cm; the client writes it), and the textures of the
source (T_SwcSnowLayout: perlinnoise2d.dds; T_SwcSnowSpatter: snow_spatter.dds; linear). With SnowAmounts.w 0 the function gives its
inputs back unchanged (each blend weight is 0).
With SWC_SNOW_MASTERS=1 it then makes the surface masters M_SwcSurface and M_SwcSurfaceMasked again with the call of the function
(always after a new function: the call nodes keep the ids of the inputs and outputs of the former function)
(swc_materials.surface_parent; their versions in PARENT_VERSIONS and import-state.json of SWC_GAME_CONTENT); the terrain masters
come from tools/import-terrain.sh (the steps material and mesh material).
Environment: SWC_SNOW_PNG (the atlas), SWC_SNOW_DIR (content/environment/snow), SWC_SNOW_FORCE (1: make the assets again),
SWC_SNOW_MASTERS (1: the surface masters), SWC_GAME_CONTENT, SWC_CONTENT, SWC_SNOW_REPORT (the report file).
"""
import json
import os
import time

import unreal

FOLDER = "/SwcGameContent/Environment"
SNOW = FOLDER + "/Snow"
ATLAS = SNOW + "/T_SwcSnowflakesLinear"
MATERIAL = FOLDER + "/M_SwcSnowFlake"
FUNCTION = FOLDER + "/MF_SwcSnowSurface"
COLLECTION = FOLDER + "/MPC_SwcWeather"
ROOF = SNOW + "/RT_SwcWeatherOcclusion"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary
force = os.environ.get("SWC_SNOW_FORCE") == "1"
started = time.time()
report = {"atlas": False, "material": False, "function": False, "touched": []}
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FOLDER], True)

VERTEX = """
float th = 6.2831853 * Seed.y + Freq * Time;
float3 t = float3(sin(th), cos(th), 0.0) * Turbulence * 100.0;
float d = max(dot(Rel + t, Fwd) * 0.01, 0.01);
float h = Flake.x < Count - 0.5 ? TanHalfFov * FlakeSize * Seed.x * pow(d, 0.25) * 100.0 : 0.0;
return t + (Right * Corner.x + Up * Corner.y) * h;
"""

PIXEL = """
float2 tile = float2(fmod(Flake.y, 2.0), floor(Flake.y * 0.5));
float2 uv = (Corner * float2(0.5, -0.5) + 0.5 + tile) * 0.5;
float4 a = Texture2DSample(Atlas, AtlasSampler, uv);
float nearMix = 1.0 - saturate(PixelDepth * 0.01 - 0.1);
float mask = lerp(a.r, a.g, nearMix);
float soft = saturate((SceneDepth - PixelDepth) * 0.02);
float roof = 1.0;
if (RoofOrigin.w > 0.5)
{
    float2 ruv = (Rel.xy - RoofOrigin.xy) / RoofOrigin.z;
    if (all(ruv >= 0.0) && all(ruv <= 1.0))
    {
        float h = Texture2DSampleLevel(RoofMap, RoofMapSampler, ruv, 0).r - CameraZ;
        roof = Rel.z >= h - 5.0 ? 1.0 : 0.0;
    }
}
float3 n = normalize(-Fwd + (Right * Corner.x + Up * Corner.y) * 0.8);
float sun = saturate(dot(LightToward.xyz, n) * 0.75 + 0.25);
return Light.rgb * (mask * soft * roof * sun);
"""


def log(text):
    unreal.log_warning("SWC SNOW " + text)


def parameter(material, cls, name, x, y, default=None):
    node = library.create_material_expression(material, cls, x, y)
    node.set_editor_property("parameter_name", name)
    if default is not None:
        node.set_editor_property("default_value", default)
    return node


def axis(material, x, y, vector):
    """A view axis in the world: the transform of a view-space constant (x right, y up, z forward)."""
    constant = library.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x - 250, y)
    constant.set_editor_property("constant", unreal.LinearColor(vector[0], vector[1], vector[2], 0.0))
    node = library.create_material_expression(material, unreal.MaterialExpressionTransform, x, y)
    node.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_VIEW)
    node.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    library.connect_material_expressions(constant, "", node, "")
    return node


def custom(material, code, name, output, x, y, pins):
    node = library.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output)
    node.set_editor_property("description", name)
    inputs = []
    for pin, _, _ in pins:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", pin)
        inputs.append(item)
    node.set_editor_property("inputs", inputs)
    for pin, source, out in pins:
        library.connect_material_expressions(source, out, node, pin)
    sources = library.get_inputs_for_material_expression(material, node)
    missing = [pins[i][0] for i in range(len(pins)) if i >= len(sources) or sources[i] is None]
    if missing:
        raise RuntimeError(f"{name}: pins with no source: {missing}")
    return node


def coordinate(material, index, x, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, x, y)
    node.set_editor_property("coordinate_index", index)
    return node


# The atlas as linear RGBA (the study: the importer of the sky asks for sRGB; the shader of the source reads linear data).
if force or not assets.does_asset_exist(ATLAS):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.environ["SWC_SNOW_PNG"])
    task.set_editor_property("destination_path", SNOW)
    task.set_editor_property("destination_name", "T_SwcSnowflakesLinear")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    atlas = unreal.load_asset(ATLAS)
    atlas.set_editor_property("srgb", False)
    atlas.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    atlas.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    atlas.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    atlas.set_editor_property("never_stream", True)
    assets.save_loaded_asset(atlas, only_if_is_dirty=False)
    report["atlas"] = True
    report["touched"].append(ATLAS)
atlas = unreal.load_asset(ATLAS)

if force or not assets.does_asset_exist(MATERIAL):
    if assets.does_asset_exist(MATERIAL):
        assets.delete_asset(MATERIAL)
    m = tools.create_asset("M_SwcSnowFlake", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    right = axis(m, -900, -400, (1.0, 0.0, 0.0))
    up = axis(m, -900, -250, (0.0, 1.0, 0.0))
    fwd = axis(m, -900, -100, (0.0, 0.0, 1.0))
    rel_vs = library.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 50)
    rel_vs.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE_NO_OFFSETS)
    rel_ps = library.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 150)
    rel_ps.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    corner = coordinate(m, 0, -900, 250)
    seed = coordinate(m, 1, -900, 330)
    flake = coordinate(m, 2, -900, 410)
    clock = library.create_material_expression(m, unreal.MaterialExpressionTime, -900, 490)
    pixel_depth = library.create_material_expression(m, unreal.MaterialExpressionPixelDepth, -900, 570)
    scene_depth = library.create_material_expression(m, unreal.MaterialExpressionSceneDepth, -900, 650)
    count = parameter(m, unreal.MaterialExpressionScalarParameter, "Count", -1200, 0, 0.0)
    size = parameter(m, unreal.MaterialExpressionScalarParameter, "FlakeSize", -1200, 80, 1.5)
    tan_half = parameter(m, unreal.MaterialExpressionScalarParameter, "TanHalfFov", -1200, 160, 1.0)
    turbulence = parameter(m, unreal.MaterialExpressionScalarParameter, "Turbulence", -1200, 240, 0.0)
    freq = parameter(m, unreal.MaterialExpressionScalarParameter, "TurbulenceFreq", -1200, 320, 0.0)
    camera_z = parameter(m, unreal.MaterialExpressionScalarParameter, "CameraZ", -1200, 400, 0.0)
    light = parameter(m, unreal.MaterialExpressionVectorParameter, "Light", -1200, 480, unreal.LinearColor(0, 0, 0, 0))
    toward = parameter(m, unreal.MaterialExpressionVectorParameter, "LightToward", -1200, 560, unreal.LinearColor(0, 0, 1, 0))
    origin = parameter(m, unreal.MaterialExpressionVectorParameter, "RoofOrigin", -1200, 640, unreal.LinearColor(0, 0, 1, 0))
    atlas_object = parameter(m, unreal.MaterialExpressionTextureObjectParameter, "Atlas", -1200, 720)
    atlas_object.set_editor_property("texture", atlas)
    atlas_object.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    # The roof map of the client is a float texture of the run; the default is the atlas (the same linear sampler type), unused while
    # RoofOrigin.w is 0.
    roof_object = parameter(m, unreal.MaterialExpressionTextureObjectParameter, "RoofMap", -1200, 820)
    roof_object.set_editor_property("texture", atlas)
    roof_object.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    vertex = custom(m, VERTEX, "SwcSnowFlakeVS", unreal.CustomMaterialOutputType.CMOT_FLOAT3, -300, -200, [
        ("Rel", rel_vs, ""), ("Right", right, ""), ("Up", up, ""), ("Fwd", fwd, ""), ("Corner", corner, ""), ("Seed", seed, ""),
        ("Flake", flake, ""), ("Count", count, ""), ("FlakeSize", size, ""), ("TanHalfFov", tan_half, ""),
        ("Turbulence", turbulence, ""), ("Freq", freq, ""), ("Time", clock, "")])
    pixel = custom(m, PIXEL, "SwcSnowFlakePS", unreal.CustomMaterialOutputType.CMOT_FLOAT3, -300, 300, [
        ("Rel", rel_ps, ""), ("Right", right, ""), ("Up", up, ""), ("Fwd", fwd, ""), ("Corner", corner, ""), ("Flake", flake, ""),
        ("Atlas", atlas_object, ""), ("PixelDepth", pixel_depth, ""), ("SceneDepth", scene_depth, ""), ("RoofMap", roof_object, ""),
        ("RoofOrigin", origin, "RGBA"), ("CameraZ", camera_z, ""), ("Light", light, ""), ("LightToward", toward, "")])
    library.connect_material_property(vertex, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    library.connect_material_property(pixel, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(m)
    assets.save_loaded_asset(m, only_if_is_dirty=False)
    report["material"] = True
    report["touched"].append(MATERIAL)

# The surface snow of the source (deferredsnow.cfxb.txt DeferredSnowPS) in the material of each surface: the blend weight of the snow,
# the frost and freezing albedo term, the ice top value, and the spatter. PosC: the place relative to the Snow centre (cm); PosR: the
# place relative to the low corner of the roof map (cm; z the world height); WN: the world normal of the surface; D: its base color.
# Not ported [I]: the 3D volume noise (the layout noise on three planes instead), the frost bubbles and their glitter, the snow
# normal of the derivative map (the snow normal is flat), the water fade. The skinned meshes (the characters, the persons, the
# creatures; the flag PRIMITIVE_SCENE_DATA_FLAG_SKINNED_MESH of the primitive, also with the pass-through of the skin cache) take no
# snow [I] (the retail takes: no snow on the character at the Snow centre).
SURFACE = """
float4 r = float4(0.0, 0.0, 1.0, 0.0);
if (Amounts.w < 0.5) return r;
#if defined(GPU_SKINNED_MESH_FACTORY) && GPU_SKINNED_MESH_FACTORY
return r;
#endif
#if defined(PRIMITIVE_SCENE_DATA_FLAG_SKINNED_MESH)
if ((GetPrimitiveData(Parameters).Flags & PRIMITIVE_SCENE_DATA_FLAG_SKINNED_MESH) != 0) return r;
#endif
float inv = Center.w;
float2 p0 = PosC.xy * inv;
float l0 = dot(p0, p0);
float outer = smoothstep(0.0, 1.0, saturate(1.0 - l0 * l0));
if (outer <= 0.0) return r;
float2 p1 = p0 * 1.2;
float l1 = dot(p1, p1);
float inner = smoothstep(0.0, 1.0, saturate(1.0 - l1 * l1));
float3 n = normalize(WN);
float3 wm = PosC * 0.01;
float2 sp = wm.xy * 0.01;
float noise = smoothstep(0.05, 0.7, Texture2DSample(Layout, LayoutSampler, sp).x)
    + 0.75 * smoothstep(0.05, 0.7, Texture2DSample(Layout, LayoutSampler, sp * 2.3).x)
    + 0.5 * smoothstep(0.05, 0.65, Texture2DSample(Layout, LayoutSampler, sp * 3.7).x);
float att = noise * outer + 0.9 * inner;
float occ = 1.0;
if (Roof.w > 0.5)
{
    // The four taps of the source (deferredsnow.cfxb.txt, the roof visibility): each cell open or covered, then a bilinear blend.
    float2 ruv = PosR.xy / Roof.z;
    if (all(ruv >= 0.0) && all(ruv <= 1.0))
    {
        float2 tc = ruv * 96.0 - 0.5;
        float2 w = frac(tc);
        float2 c0 = (floor(tc) + 0.5) / 96.0;
        float t = 1.0 / 96.0;
        float4 o;
        o.x = PosR.z >= Texture2DSampleLevel(RoofMap, RoofMapSampler, c0, 0).r - 25.0 ? 1.0 : 0.0;
        o.y = PosR.z >= Texture2DSampleLevel(RoofMap, RoofMapSampler, c0 + float2(t, 0.0), 0).r - 25.0 ? 1.0 : 0.0;
        o.z = PosR.z >= Texture2DSampleLevel(RoofMap, RoofMapSampler, c0 + float2(0.0, t), 0).r - 25.0 ? 1.0 : 0.0;
        o.w = PosR.z >= Texture2DSampleLevel(RoofMap, RoofMapSampler, c0 + float2(t, t), 0).r - 25.0 ? 1.0 : 0.0;
        occ = lerp(lerp(o.x, o.y, w.x), lerp(o.z, o.w, w.x), w.y);
    }
}
float3 blend = saturate(abs(n) - 0.25);
float3 st = wm * 10.0;
float spatter = Texture2DSample(Spatter, SpatterSampler, st.yz).x * blend.x + Texture2DSample(Spatter, SpatterSampler, st.zx).x * blend.y
    + Texture2DSample(Spatter, SpatterSampler, st.xy).x * blend.z;
float3 tv = wm * 0.5;
float fv = Texture2DSample(Layout, LayoutSampler, tv.xy * 0.25).x;
fv *= Texture2DSample(Layout, LayoutSampler, tv.yz * 0.5 + fv).x * 2.0;
fv *= Texture2DSample(Layout, LayoutSampler, tv.zx * 1.25 + fv).x * 4.0;
fv = saturate(fv * 0.5 - 0.125);
float freezeVar = fv;
float amount = min(10.0, Amounts.x) * att * occ;
float snowPow = max(1e-6, 10.0 / max(1e-6, amount));
fv = saturate(fv - saturate(1.0 - amount * 0.1));
float iceTop = saturate(1.0 - 0.4 * fv);
float topZ = saturate((pow(max(0.0001, n.z), snowPow) - 0.2) * 10.0) * Bright.x;
float a = saturate(topZ * saturate(fv * 8.0));
float freeze = saturate(Amounts.z * n.z * freezeVar * att);
float frost = freeze * saturate(dot(D, 1.0) * 1.5) * Bright.y;
return float4(a, frost, iceTop, spatter);
"""


def collection_asset():
    """MPC_SwcWeather (new): its vector parameters with their defaults (no snow)."""
    if assets.does_asset_exist(COLLECTION) and not force:
        return unreal.load_asset(COLLECTION)
    mpc = unreal.load_asset(COLLECTION) if assets.does_asset_exist(COLLECTION) else tools.create_asset(
        "MPC_SwcWeather", FOLDER, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    vectors = []
    for name, value in (("SnowCenter", (0.0, 0.0, 0.0, 0.0)), ("SnowAmounts", (0.0, 0.0, 0.0, 0.0)),
                        ("SnowBrightness", (0.0, 0.0, 0.0, 0.0)), ("RoofOrigin", (0.0, 0.0, 1.0, 0.0))):
        item = unreal.CollectionVectorParameter()
        item.set_editor_property("parameter_name", name)
        item.set_editor_property("default_value", unreal.LinearColor(*value))
        vectors.append(item)
    mpc.set_editor_property("vector_parameters", vectors)
    assets.save_loaded_asset(mpc, only_if_is_dirty=False)
    report["touched"].append(COLLECTION)
    return mpc


def roof_target():
    """RT_SwcWeatherOcclusion (new): the roof map of the client, 96 x 96 R32F (FSwcWeatherOcclusion::Size)."""
    if assets.does_asset_exist(ROOF) and not force:
        return unreal.load_asset(ROOF)
    if assets.does_asset_exist(ROOF):
        assets.delete_asset(ROOF)
    target = tools.create_asset("RT_SwcWeatherOcclusion", SNOW, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
    target.set_editor_property("size_x", 96)
    target.set_editor_property("size_y", 96)
    target.set_editor_property("render_target_format", unreal.TextureRenderTargetFormat.RTF_R32F)
    target.set_editor_property("clear_color", unreal.LinearColor(-1.0e7, 0.0, 0.0, 0.0))
    target.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    target.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    target.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    assets.save_loaded_asset(target, only_if_is_dirty=False)
    report["touched"].append(ROOF)
    return target


def data_texture(name, png):
    path = f"{SNOW}/{name}"
    if assets.does_asset_exist(path) and not force:
        return unreal.load_asset(path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(os.environ["SWC_SNOW_DIR"], png))
    task.set_editor_property("destination_path", SNOW)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(path)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    assets.save_loaded_asset(texture, only_if_is_dirty=False)
    report["touched"].append(path)
    return texture


def function_asset(mpc, roof, layout, spatter):
    """MF_SwcSnowSurface (new)."""
    if assets.does_asset_exist(FUNCTION) and not force:
        return unreal.load_asset(FUNCTION)
    if assets.does_asset_exist(FUNCTION):
        assets.delete_asset(FUNCTION)
    f = tools.create_asset("MF_SwcSnowSurface", FOLDER, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
    f.set_editor_property("description", "AST-257: the surface snow and frost of the source (deferredsnow.cfxb); MPC_SwcWeather drives it.")
    make = library.create_material_expression_in_function
    t = unreal.FunctionInputType
    inputs = {}
    for i, (name, kind) in enumerate((("BaseColor", t.FUNCTION_INPUT_VECTOR3), ("Metallic", t.FUNCTION_INPUT_SCALAR),
                                      ("Specular", t.FUNCTION_INPUT_SCALAR), ("Roughness", t.FUNCTION_INPUT_SCALAR),
                                      ("Normal", t.FUNCTION_INPUT_VECTOR3))):
        node = make(f, unreal.MaterialExpressionFunctionInput, -1400, i * 150)
        node.set_editor_property("input_name", name)
        node.set_editor_property("input_type", kind)
        node.set_editor_property("sort_priority", i)
        inputs[name] = node

    def mpc_value(name, y):
        node = make(f, unreal.MaterialExpressionCollectionParameter, -1400, y)
        node.set_editor_property("collection", mpc)
        node.set_editor_property("parameter_name", name)
        return node

    center = mpc_value("SnowCenter", 800)
    amounts = mpc_value("SnowAmounts", 900)
    bright = mpc_value("SnowBrightness", 1000)
    origin = mpc_value("RoofOrigin", 1100)
    world = make(f, unreal.MaterialExpressionWorldPosition, -1400, 1250)

    def relative(source, y):
        """TruncateLWC(the absolute world position - (x, y, z of a collection vector)); z_mask: keep z of the vector."""
        sub = make(f, unreal.MaterialExpressionSubtract, -1000, y)
        library.connect_material_expressions(world, "", sub, "A")
        library.connect_material_expressions(source, "", sub, "B")
        trunc = make(f, unreal.MaterialExpressionTruncateLWC, -850, y)
        library.connect_material_expressions(sub, "", trunc, "")
        return trunc

    corner3 = make(f, unreal.MaterialExpressionAppendVector, -1150, 1400)
    corner2 = make(f, unreal.MaterialExpressionComponentMask, -1300, 1400)
    for c in ("r", "g", "b", "a"):
        corner2.set_editor_property(c, c in ("r", "g"))
    library.connect_material_expressions(origin, "", corner2, "")
    zero = make(f, unreal.MaterialExpressionConstant, -1300, 1500)
    library.connect_material_expressions(corner2, "", corner3, "A")
    library.connect_material_expressions(zero, "", corner3, "B")
    center3 = make(f, unreal.MaterialExpressionComponentMask, -1300, 1250)
    for c in ("r", "g", "b", "a"):
        center3.set_editor_property(c, c in ("r", "g", "b"))
    library.connect_material_expressions(center, "", center3, "")
    pos_c = relative(center3, 1250)     # the center (cm): x, y, z of the collection vector
    pos_r = relative(corner3, 1400)
    # The coverage test reads the normal of the surface in world space (the source reads the G-buffer normal). The vertex normal
    # (VertexNormalWS) changed the look of the skinned bodies even with no snow (a check run of 2026-10-22) and did not remove
    # the hard triangle edges of the snow on some rocks (their mesh normals): not used.
    to_world = make(f, unreal.MaterialExpressionTransform, -1000, 600)
    to_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
    to_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    library.connect_material_expressions(inputs["Normal"], "", to_world, "")

    def texture_object(texture, y):
        node = make(f, unreal.MaterialExpressionTextureObject, -1000, y)
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if texture == roof
                                 else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        return node

    code = make(f, unreal.MaterialExpressionCustom, -500, 800)
    code.set_editor_property("code", SURFACE)
    code.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    code.set_editor_property("description", "SwcSnowSurface")
    pins = [("PosC", pos_c, ""), ("PosR", pos_r, ""), ("WN", to_world, ""), ("D", inputs["BaseColor"], ""),
            ("Center", center, ""), ("Amounts", amounts, ""), ("Bright", bright, ""), ("Roof", origin, ""),
            ("Layout", texture_object(layout, 1600), ""), ("Spatter", texture_object(spatter, 1700), ""),
            ("RoofMap", texture_object(roof, 1800), "")]
    items = []
    for pin, _, _ in pins:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", pin)
        items.append(item)
    code.set_editor_property("inputs", items)
    for pin, source, out in pins:
        if not library.connect_material_expressions(source, out, code, pin):
            raise RuntimeError(f"MF_SwcSnowSurface: no connection to {pin}")

    def channel(c, y):
        node = make(f, unreal.MaterialExpressionComponentMask, -250, y)
        for k in ("r", "g", "b", "a"):
            node.set_editor_property(k, k == c)
        library.connect_material_expressions(code, "", node, "")
        return node

    weight, frost, ice, spat = channel("r", 0), channel("g", 150), channel("b", 300), channel("a", 450)

    def lerp(a, b, alpha, y):
        node = make(f, unreal.MaterialExpressionLinearInterpolate, 0, y)
        library.connect_material_expressions(a[0], a[1], node, "A")
        library.connect_material_expressions(b[0], b[1], node, "B")
        library.connect_material_expressions(alpha, "", node, "Alpha")
        return node

    def math(cls, a, b, y):
        node = make(f, cls, -100, y)
        library.connect_material_expressions(a[0], a[1], node, "A")
        if isinstance(b, float):
            node.set_editor_property("const_b", b)
        else:
            library.connect_material_expressions(b[0], b[1], node, "B")
        return node

    def constant(value, y):
        node = make(f, unreal.MaterialExpressionConstant, -250, y)
        node.set_editor_property("r", value)
        return node

    def constant3(value, y):
        node = make(f, unreal.MaterialExpressionConstant3Vector, -250, y)
        node.set_editor_property("constant", unreal.LinearColor(*value, 0.0))
        return node

    # The albedo: lerp(albedo, ice top, a) + the frost term; the smoothness lerp(s, spatter x 0.4, a) as a roughness; the reflectance
    # lerp(r, spatter x 40 / 255, a) as the Unreal specular (F0 / 0.08); the metal goes away under snow; the snow normal is flat.
    base = math(unreal.MaterialExpressionAdd, (lerp((inputs["BaseColor"], ""), (ice, ""), weight, 0), ""), (frost, ""), 0)
    rough_snow = math(unreal.MaterialExpressionSubtract, (constant(1.0, 900), ""), (math(unreal.MaterialExpressionMultiply, (spat, ""), 0.4, 800), ""), 850)
    rough = lerp((inputs["Roughness"], ""), (rough_snow, ""), weight, 900)
    spec_snow = math(unreal.MaterialExpressionMultiply, (spat, ""), 40.0 / 255.0 / 0.08, 1000)
    spec = lerp((inputs["Specular"], ""), (spec_snow, ""), weight, 1000)
    metal = lerp((inputs["Metallic"], ""), (constant(0.0, 1100), ""), weight, 1100)
    normal = lerp((inputs["Normal"], ""), (constant3((0.0, 0.0, 1.0), 1200), ""), weight, 1200)
    for i, (name, node) in enumerate((("BaseColor", base), ("Metallic", metal), ("Specular", spec), ("Roughness", rough), ("Normal", normal))):
        out = make(f, unreal.MaterialExpressionFunctionOutput, 300, i * 200)
        out.set_editor_property("output_name", name)
        out.set_editor_property("sort_priority", i)
        library.connect_material_expressions(node, "", out, "")
    library.update_material_function(f)
    assets.save_loaded_asset(f, only_if_is_dirty=False)
    report["function"] = True
    report["touched"].append(FUNCTION)
    state_path = os.path.join(os.environ["SWC_GAME_CONTENT"], "import-state.json")
    plugin_state = json.load(open(state_path)) if os.path.isfile(state_path) else {}
    plugin_state["snow:function"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    temporary = state_path + ".tmp"
    with open(temporary, "w") as out:
        json.dump(plugin_state, out, indent=0, sort_keys=True)
    os.replace(temporary, state_path)
    return f


weather = collection_asset()
roof_map = roof_target()
layout_texture = data_texture("T_SwcSnowLayout", "layout.png")
spatter_texture = data_texture("T_SwcSnowSpatter", "spatter.png")
function_asset(weather, roof_map, layout_texture, spatter_texture)

if os.environ.get("SWC_SNOW_MASTERS") == "1":
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import swc_materials  # noqa: E402
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([swc_materials.BASE], True)   # does_asset_exist
    plugin = os.environ["SWC_GAME_CONTENT"]
    state_path = os.path.join(plugin, "import-state.json")
    masters_state = json.load(open(state_path)) if os.path.isfile(state_path) else {}
    before = {k: masters_state.get("base:" + k) for k in ("M_SwcSurface", "M_SwcSurfaceMasked")}
    # A new function has new ids of its inputs and outputs: the calls of the masters must be made again (also when the function
    # came from an earlier call of this script: the stamp of the function against the stamp of the masters).
    function_stamp = masters_state.get("snow:function", "")
    if force or report["function"] or masters_state.get("snow:masters-function") != function_stamp:
        for k in before:
            masters_state.pop("base:" + k, None)
    swc_materials.surface_parent(os.environ["SWC_CONTENT"], masters_state, masked=False)
    swc_materials.surface_parent(os.environ["SWC_CONTENT"], masters_state, masked=True)
    report["masters"] = {k: [before[k], masters_state.get("base:" + k)] for k in before}
    masters_state["snow:masters-function"] = masters_state.get("snow:function", "")
    temporary = state_path + ".tmp"
    with open(temporary, "w") as out:
        json.dump(masters_state, out, indent=0, sort_keys=True)
    os.replace(temporary, state_path)
    report["touched"] += [k for k in before if before[k] != masters_state.get("base:" + k)]

report["seconds"] = round(time.time() - started, 1)
log(json.dumps(report))
with open(os.environ["SWC_SNOW_REPORT"], "w") as out:
    json.dump(report, out, indent=1)
