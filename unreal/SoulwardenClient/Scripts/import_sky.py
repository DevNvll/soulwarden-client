"""The night sky of the environment (stage E3, docs/ENVIRONMENT.md): the star map and the moon of SWC_CONTENT/environment/sky
(tools/convert-environment.sh sky) as textures in /SwcGameContent/Environment/Sky, and the sky material
/SwcGameContent/Environment/M_SwcSky (unlit, a sky material: the SkyAtmosphere of Unreal through SkyAtmosphereViewLuminance, the
sun disk through SkyAtmosphereLightDiskLuminance, and the stars, the moon, and its two coronas of the data). The client puts it on a
sky sphere around the camera (Environment/SwcEnvironmentSubsystem). Run through tools/import-sky.sh (the import lock).

The parameters of the material (the client sets them each frame):
  Stars, Moon               the textures (the star map: u the right ascension, v the declination; the moon disc)
  CelestialX, Y, Z          the rows of the rotation from the Unreal world to the celestial frame (x = cos(dec) sin(ra),
                            y = cos(dec) cos(ra), z = sin(dec))
  StarIntensity             "Night sky: Star intensity" times a look scale
  MoonDirection, MoonSize   the direction toward the moon (Unreal world) and the tangent of the angular radius of its disc
  MoonColor                 "Night sky: Moon color" times its multiplier and a look scale
  InnerCorona, OuterCorona  the corona colors times their multipliers (rgb) and their scales (a: radians)
The precipitation material /SwcGameContent/Environment/M_SwcPrecipitation (a post-process material before the tone mapping): three
layers of the rain layer texture of the source that fall over the screen, and the snowflakes of the engine atlas in cells that drift;
the drops cover the scene with their own light (a part of the light of a white surface). Its parameters: Rainfall, Snowflakes (the textures), RainAmount, SnowAmount
(0 to about 1), DropLight (the luminance of the drops, lit by the sun or the moon).
Environment: SWC_SKY (sky/sky.json), SWC_SKY_FORCE (1: import again), SWC_SKY_REPORT (the report file).
"""
import json
import os
import time

import unreal

FOLDER = "/SwcGameContent/Environment"
SKY = FOLDER + "/Sky"
MATERIAL = FOLDER + "/M_SwcSky"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary
doc = json.load(open(os.environ["SWC_SKY"]))
content = os.path.dirname(os.path.dirname(os.environ["SWC_SKY"]))
force = os.environ.get("SWC_SKY_FORCE") == "1"
started = time.time()
report = {"textures": [], "material": False}
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FOLDER], True)   # does_asset_exist needs the scan

CODE = """
float3 d = normalize(-ViewToPixel);
// The sky color: the SkyAtmosphere of Unreal, or the sky model of the source (stage E5: SourceSky 1): the dome of the upper half of
// the sky (u: the azimuth from north to east, v: the zenith angle 0 to 90 degrees; below the horizon the horizon row), its Mie and
// Rayleigh parts with the phases of the sky shader of the source (skyhdr@skyhdrps), and the night sky gradient, in the units of the
// source (SourceUnit cd/m2 each).
float3 sky = Atmosphere;
if (SourceSky > 0.5)
{
    float2 dome = float2(frac(atan2(d.x, -d.y) / 6.28318530718), saturate(acos(clamp(d.z, -1.0, 1.0)) / 1.57079632679));
    float3 mie = Texture2DSampleLevel(SourceMie, SourceMieSampler, dome, 0).rgb;
    float3 rayleigh = Texture2DSampleLevel(SourceRayleigh, SourceRayleighSampler, dome, 0).rgb;
    float cosine = dot(d, normalize(SkySun.xyz));
    float g = SkySun.w;
    float g2 = g * g;
    float miePhase = 1.5 * (1.0 - g2) / (2.0 + g2) * (1.0 + cosine * cosine) / pow(max(1.0 + g2 + 2.0 * g * cosine, 1e-6), 1.5);
    float rayleighPhase = 0.75 * (1.0 + cosine * cosine);
    float zenith = saturate(d.z * NightShift.x + NightShift.y);
    zenith = zenith * (2.0 - zenith);
    sky = (mie * miePhase + rayleigh * rayleighPhase + NightBase.rgb + NightDelta.rgb * zenith) * SourceUnit;
}
float3 c = float3(dot(CelestialX.xyz, d), dot(CelestialY.xyz, d), dot(CelestialZ.xyz, d));
float dec = asin(clamp(c.z, -1.0, 1.0));
float ra = atan2(c.x, c.y);
float2 uv = float2(frac(ra / 6.28318530718), 0.5 - dec / 3.14159265359);
float3 result = sky + Texture2DSampleLevel(Stars, StarsSampler, uv, 0).rgb * StarIntensity;
float3 m = normalize(MoonDirection.xyz);
float cosA = dot(d, m);
if (cosA > 0.0)
{
    float3 t1 = abs(m.z) < 0.99 ? normalize(cross(float3(0.0, 0.0, 1.0), m)) : float3(1.0, 0.0, 0.0);
    float3 t2 = cross(m, t1);
    float2 p = float2(dot(d, t1), dot(d, t2)) / cosA / max(MoonSize, 1e-5);
    if (abs(p.x) < 1.0 && abs(p.y) < 1.0)
    {
        float4 disc = Texture2DSampleLevel(Moon, MoonSampler, float2(p.x, -p.y) * 0.5 + 0.5, 0);
        result += disc.rgb * disc.a * MoonColor.rgb;
    }
    float angle = acos(clamp(cosA, -1.0, 1.0));
    result += InnerCorona.rgb * exp(-angle / max(InnerCorona.a, 1e-4)) + OuterCorona.rgb * exp(-angle / max(OuterCorona.a, 1e-4));
}
return result;
"""


def log(text):
    unreal.log_warning("SWC SKY " + text)


def texture(name, png, srgb, clamp):
    path = f"{SKY}/{name}"
    if assets.does_asset_exist(path) and not force:
        return unreal.load_asset(path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(content, png))
    task.set_editor_property("destination_path", SKY)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    result = unreal.load_asset(path)
    result.set_editor_property("srgb", srgb)
    result.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    if clamp:
        result.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        result.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    result.set_editor_property("never_stream", True)
    result.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)   # the sky samples mip 0
    assets.save_loaded_asset(result, only_if_is_dirty=False)
    report["textures"].append(name)
    return result


def parameter(material, cls, name, x, y, default=None):
    node = library.create_material_expression(material, cls, x, y)
    node.set_editor_property("parameter_name", name)
    if default is not None:
        node.set_editor_property("default_value", default)
    return node


stars = texture("T_SwcStars", doc["stars"]["png"], True, False)
moon = texture("T_SwcMoon", doc["moon"]["png"], True, True)
if force or not assets.does_asset_exist(MATERIAL):
    if assets.does_asset_exist(MATERIAL):
        assets.delete_asset(MATERIAL)
    material = tools.create_asset("M_SwcSky", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("is_sky", True)
    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 200)
    custom.set_editor_property("code", CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "SwcNightSky")
    inputs = []
    sources = []
    view = library.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -900, -200)
    # The SkyAtmosphere of Unreal on the sky mesh (the view luminance): the sky color when SourceSky is 0.
    atmosphere = library.create_material_expression(material, unreal.MaterialExpressionSkyAtmosphereViewLuminance, -900, -350)
    for name, node in [("ViewToPixel", view), ("Atmosphere", atmosphere)]:
        inputs.append(name)
        sources.append((node, ""))
    # The dome of the source sky (the client fills two float textures; a black dummy here).
    for name, y in (("SourceMie", -500), ("SourceRayleigh", -650)):
        dome = parameter(material, unreal.MaterialExpressionTextureObjectParameter, name, -900, y)
        dome.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black.Black"))
        inputs.append(name)
        sources.append((dome, ""))
    star_object = parameter(material, unreal.MaterialExpressionTextureObjectParameter, "Stars", -900, 0)
    star_object.set_editor_property("texture", stars)
    moon_object = parameter(material, unreal.MaterialExpressionTextureObjectParameter, "Moon", -900, 150)
    moon_object.set_editor_property("texture", moon)
    inputs += ["Stars", "Moon"]
    sources += [(star_object, ""), (moon_object, "")]
    y = 300
    for name in ("CelestialX", "CelestialY", "CelestialZ", "MoonDirection", "MoonColor", "InnerCorona", "OuterCorona", "SkySun", "NightBase",
                 "NightDelta", "NightShift"):
        node = parameter(material, unreal.MaterialExpressionVectorParameter, name, -900, y, unreal.LinearColor(0, 0, 0, 0))
        inputs.append(name)
        sources.append((node, "RGBA"))   # the alpha: the scale of a corona (the default pin has RGB only)
        y += 150
    for name in ("StarIntensity", "MoonSize", "SourceSky", "SourceUnit"):
        node = parameter(material, unreal.MaterialExpressionScalarParameter, name, -900, y, 0.0)
        inputs.append(name)
        sources.append((node, ""))
        y += 120
    custom_inputs = []
    for name in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        custom_inputs.append(item)
    custom.set_editor_property("inputs", custom_inputs)
    for index, (node, pin) in enumerate(sources):
        library.connect_material_expressions(node, pin, custom, inputs[index])
    # The disk of the sun (light 0) of the SkyAtmosphere.
    disk = library.create_material_expression(material, unreal.MaterialExpressionSkyAtmosphereLightDiskLuminance, -300, 0)
    add = library.create_material_expression(material, unreal.MaterialExpressionAdd, 150, 50)
    library.connect_material_expressions(disk, "", add, "A")
    library.connect_material_expressions(custom, "", add, "B")
    library.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Each pin of the custom node must have its source (a pin that does not connect fails the compile only at run time).
    pins = library.get_inputs_for_material_expression(material, custom)
    missing = [inputs[i] for i in range(len(inputs)) if i >= len(pins) or pins[i] is None]
    if missing:
        raise RuntimeError(f"M_SwcSky: pins with no source: {missing}")
    library.recompile_material(material)
    assets.save_loaded_asset(material, only_if_is_dirty=False)
    report["material"] = True
PRECIPITATION = FOLDER + "/M_SwcPrecipitation"
PRECIPITATION_CODE = """
float aspect = ViewSize.x / max(ViewSize.y, 1.0);
float veil = 0.0;
if (RainAmount > 0.0)
{
    float rain = 0.0;
    [unroll] for (int i = 0; i < 3; ++i)
    {
        float scale = 1.2 + 0.9 * i;
        float2 p = float2(UV.x * aspect + UV.y * 0.12, UV.y) * scale;
        p.y -= Time * (1.6 + 0.5 * i);
        p.x += 0.37 * i;
        rain += Texture2DSample(Rainfall, RainfallSampler, p).r * (0.55 - 0.15 * i);
    }
    veil += rain * RainAmount;
}
if (SnowAmount > 0.0)
{
    // Sparse small flakes: a cell holds a flake when its hash is under the amount; the flake (the luminance of the atlas) is a third
    // of its cell, at a place of the hash in it.
    float flakes = 0.0;
    [unroll] for (int i = 0; i < 3; ++i)
    {
        float scale = 18.0 + 14.0 * i;
        float2 p = float2(UV.x * aspect, UV.y) * scale;
        p.y -= Time * (0.6 + 0.35 * i);
        p.x += sin(Time * 0.7 + 1.7 * i + p.y * 0.3) * 0.4;
        float2 cell = floor(p);
        float h = frac(sin(dot(cell, float2(12.9898, 78.233)) + 17.0 * i) * 43758.5453);
        if (h < SnowAmount * 0.5)
        {
            float2 offset = float2(frac(h * 13.7), frac(h * 71.3)) * 0.5 + 0.25;
            float2 q = (frac(p) - offset) * 3.0 + 0.5;
            if (all(q > 0.0) && all(q < 1.0))
            {
                float2 tile = floor(float2(frac(h * 5.1), frac(h * 9.7)) * 2.0);
                float3 c = Texture2DSample(Snowflakes, SnowflakesSampler, (q + tile) * 0.5).rgb;
                flakes += saturate(dot(c, float3(0.3, 0.6, 0.1)) * 1.5 - 0.2) * (0.9 - 0.2 * i);
            }
        }
    }
    veil += flakes;
}
float a = saturate(veil) * saturate(Depth / 200.0);
return lerp(Scene.rgb, DropLight.rgb, a);
"""
if force or not assets.does_asset_exist(PRECIPITATION):
    rainfall = texture("T_SwcRainfall", doc["rain"]["png"], False, False)
    snowflakes = texture("T_SwcSnowflakes", doc["snow"]["png"], True, True)
    if assets.does_asset_exist(PRECIPITATION):
        assets.delete_asset(PRECIPITATION)
    post = tools.create_asset("M_SwcPrecipitation", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    post.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    post.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_DOF)
    code = library.create_material_expression(post, unreal.MaterialExpressionCustom, -300, 0)
    code.set_editor_property("code", PRECIPITATION_CODE)
    code.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    code.set_editor_property("description", "SwcPrecipitation")
    scene = library.create_material_expression(post, unreal.MaterialExpressionSceneTexture, -900, -300)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    depth = library.create_material_expression(post, unreal.MaterialExpressionSceneDepth, -900, -200)
    uv = library.create_material_expression(post, unreal.MaterialExpressionTextureCoordinate, -900, -100)
    clock = library.create_material_expression(post, unreal.MaterialExpressionTime, -900, 0)
    size = library.create_material_expression(post, unreal.MaterialExpressionViewSize, -900, 100)
    rain_object = parameter(post, unreal.MaterialExpressionTextureObjectParameter, "Rainfall", -900, 200)
    rain_object.set_editor_property("texture", rainfall)
    flake_object = parameter(post, unreal.MaterialExpressionTextureObjectParameter, "Snowflakes", -900, 350)
    flake_object.set_editor_property("texture", snowflakes)
    rain_amount = parameter(post, unreal.MaterialExpressionScalarParameter, "RainAmount", -900, 500, 0.0)
    snow_amount = parameter(post, unreal.MaterialExpressionScalarParameter, "SnowAmount", -900, 600, 0.0)
    drop_light = parameter(post, unreal.MaterialExpressionVectorParameter, "DropLight", -900, 700, unreal.LinearColor(0, 0, 0, 0))
    pins = [("Scene", scene, "Color"), ("Depth", depth, ""), ("UV", uv, ""), ("Time", clock, ""), ("ViewSize", size, ""),
            ("Rainfall", rain_object, ""), ("Snowflakes", flake_object, ""), ("RainAmount", rain_amount, ""),
            ("SnowAmount", snow_amount, ""), ("DropLight", drop_light, "")]
    pin_inputs = []
    for name, _, _ in pins:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        pin_inputs.append(item)
    code.set_editor_property("inputs", pin_inputs)
    for name, node, output in pins:
        library.connect_material_expressions(node, output, code, name)
    library.connect_material_property(code, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(post)
    assets.save_loaded_asset(post, only_if_is_dirty=False)
    report["precipitation"] = True
# The color grade of the source (stage E5; the decision of the user: the client colors copy the retail colors): a post-process
# material that replaces the tone mapper of Unreal. It copies the final scene shader of the game copy (HDRFinalScenePS of
# shaders/cache/d3d11/cgpshaders/hdrpostprocess@hdrfinalsceneps.fxcb, read with its constant names) and the photo filter of its
# color chart shader (posteffectsgame@combinecolorgradingwithcolorchartps): the exposed value x (the exposure is the eye adaptation
# of Unreal with the EV rule of the source, Environment/SwcEnvironmentSubsystem), then the saturation ("Saturation": a mix with the
# Rec. 709 luminance) and the color balance ("Color balance": a multiply), both before the curve, then the film curve (the Hable curve
# with A = 0.22 x "Film curve shoulder scale", B = 0.3 x "midtones scale", C = 0.1, D = 0.2, E = 0.01 x "toe scale", F = 0.3, over
# its value at "Film curve whitepoint"), the sRGB encoding, and the photo filter on the encoded value (an overlay of
# "Filters: photofilter color" by the luminance, mixed in by saturate(luminance x "Filters: photofilter density")). The color chart
# of the world volumes (textures/colorcharts/default_cch.dds) is an identity chart. The bloom is the bloom of Unreal (the technique
# stays Unreal), added to the scene as the Unreal tone mapper adds it.
GRADE = FOLDER + "/M_SwcGrade"
GRADE_CODE = """
// The scene input of a material that replaces the tone mapper is the luminance with no pre-exposure (measured): the exposed value is
// the input times the exposure.
float3 x = max((Scene.rgb + Bloom.rgb * BloomScale) * Exposure, 0.0);
if (Debug > 0.5)
{
    // A check of the inputs (swc.GradeDebug): 1 the exposed value only (no curve, no grade, sRGB); 2 the exposure as
    // 0.5 + log2(v) / 32 (v = 1: 0.5).
    float3 y = Debug < 1.5 ? saturate(x) : saturate(0.5 + log2(max(Exposure, 1e-12)) / 32.0).xxx;
    return y <= 0.0031308 ? 12.92 * y : 1.055 * pow(y, 1.0 / 2.4) - 0.055;
}
const float3 Rec709 = float3(0.2126, 0.7152, 0.0722);
x = max(lerp(dot(x, Rec709).xxx, x, Balance.a) * Balance.rgb, 0.0);
float A = 0.22 * FilmCurve.x, B = 0.3 * FilmCurve.y, C = 0.1, D = 0.2, E = 0.01 * FilmCurve.z, F = 0.3;
float4 c = float4(x, max(FilmCurve.w, 1e-3));
float4 m = ((c * (A * c + C * B) + D * E) / (c * (A * c + B) + D * F)) - E / F;
float3 t = saturate(m.rgb / max(m.w, 1e-6));
float3 s = t < 0.0031308 ? 12.92 * t : 1.055 * pow(t, 1.0 / 2.4) - 0.055;
float l = dot(s, Rec709);
float low = saturate(2.0 * l);
float3 overlay = saturate(l - 0.5) * 2.0 * (1.0 - low * PhotoFilter.rgb) + low * PhotoFilter.rgb;
return saturate(lerp(s, overlay, saturate(l * PhotoFilter.a)));
"""
if force or not assets.does_asset_exist(GRADE):
    if assets.does_asset_exist(GRADE):
        assets.delete_asset(GRADE)
    grade = tools.create_asset("M_SwcGrade", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    grade.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    grade.set_editor_property("blendable_location", unreal.BlendableLocation.BL_REPLACING_TONEMAPPER)
    code = library.create_material_expression(grade, unreal.MaterialExpressionCustom, -300, 0)
    code.set_editor_property("code", GRADE_CODE)
    code.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    code.set_editor_property("description", "SwcGrade")
    scene = library.create_material_expression(grade, unreal.MaterialExpressionSceneTexture, -900, -300)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    exposure = library.create_material_expression(grade, unreal.MaterialExpressionEyeAdaptation, -900, -150)
    film = parameter(grade, unreal.MaterialExpressionVectorParameter, "FilmCurve", -900, 0, unreal.LinearColor(1, 1, 1, 4))
    balance = parameter(grade, unreal.MaterialExpressionVectorParameter, "Balance", -900, 150, unreal.LinearColor(1, 1, 1, 1))
    photo = parameter(grade, unreal.MaterialExpressionVectorParameter, "PhotoFilter", -900, 300, unreal.LinearColor(1, 1, 1, 0))
    debug = parameter(grade, unreal.MaterialExpressionScalarParameter, "Debug", -900, 450, 0.0)
    # The combined bloom of Unreal: the input 2 of a material that replaces the tone mapper (half resolution, filtered).
    bloom = library.create_material_expression(grade, unreal.MaterialExpressionSceneTexture, -900, -450)
    bloom.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT2)
    bloom.set_editor_property("filtered", True)
    bloom_scale = parameter(grade, unreal.MaterialExpressionScalarParameter, "BloomScale", -900, 550, 1.0)
    pins = [("Scene", scene, "Color"), ("Exposure", exposure, ""), ("FilmCurve", film, "RGBA"), ("Balance", balance, "RGBA"),
            ("PhotoFilter", photo, "RGBA"), ("Debug", debug, ""), ("Bloom", bloom, "Color"), ("BloomScale", bloom_scale, "")]
    pin_inputs = []
    for name, _, _ in pins:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        pin_inputs.append(item)
    code.set_editor_property("inputs", pin_inputs)
    for name, node, output in pins:
        library.connect_material_expressions(node, output, code, name)
    library.connect_material_property(code, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(grade)
    assets.save_loaded_asset(grade, only_if_is_dirty=False)
    report["grade"] = True
    # Each pin of the custom node must have its source (a pin that does not connect fails the compile only at run time).
    sources = library.get_inputs_for_material_expression(grade, code)
    report["grade pins"] = {name: sources[index] is not None if index < len(sources) else False for index, (name, _, _) in enumerate(pins)}
    if not all(report["grade pins"].values()):
        raise RuntimeError(f"M_SwcGrade: a pin of the custom node has no source: {report['grade pins']}")
# The world probes of the outdoor ambient (stage E5): the long-lat Radiance pictures of probes.json (tools/convert-environment.sh
# probes) as cube textures /SwcGameContent/Environment/Probes/T_SwcProbe_NAME (HDR, linear). The client puts the probe into its sky
# light (a specified cubemap) with the multipliers of the data.
PROBES = FOLDER + "/Probes"
probe_doc = os.path.join(content, "probes.json")
report["probes"] = []
if os.path.exists(probe_doc):
    for entry in json.load(open(probe_doc))["world"]:
        if "hdr" not in entry:
            continue
        name = "T_SwcProbe_" + os.path.splitext(os.path.basename(entry["hdr"]))[0]
        path = f"{PROBES}/{name}"
        if assets.does_asset_exist(path) and not force:
            report["probes"].append(name)
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(content, entry["hdr"]))
        task.set_editor_property("destination_path", PROBES)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tools.import_asset_tasks([task])
        cube = unreal.load_asset(path)
        if not isinstance(cube, unreal.TextureCube):
            raise RuntimeError(f"{path}: the import gave {type(cube).__name__}, not a TextureCube")
        cube.set_editor_property("srgb", False)
        cube.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_HDR)
        cube.set_editor_property("never_stream", True)
        assets.save_loaded_asset(cube, only_if_is_dirty=False)
        report["probes"].append(name)
report["seconds"] = round(time.time() - started, 1)
with open(os.environ["SWC_SKY_REPORT"], "w") as handle:
    json.dump(report, handle, indent=1)
log(json.dumps(report))
