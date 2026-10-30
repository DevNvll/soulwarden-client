"""The underwater view of the source water path (AST-200; the decision of the user, 2026-10-30, option 5; docs/WATER.md "The underwater
view"; the study ~/Games/soulwarden-client/research/water-underwater.md).

Run through tools/import-water-unreal.sh --under (UnrealEditor-Cmd -run=pythonscript, no window). The folder: the check plugin
/SwcWaterCheck/Water/Under, or /SwcGameContent/Water/Under (--under --shared: only with the word of the orchestrator). Two new materials;
the source parent M_SwcWaterSource and its textures do not change (the wave normal texture is read).

1. M_SwcUnderwater: a post-process material (before the bloom) for the water between the camera and the scene, the "Outof" passes of the
   source (research ground-cover/waterfogvolume.cfxb.txt: WaterVolumeOutofPS 180-203, OceanOutofPS 293-329). For each pixel: its first
   point (at the near plane) under the plane of the water level or not (a pixel in the air keeps the scene color: the crossing of the
   surface has no pop); the path L in the water from that point to the scene or to the plane of the level (the source clips the ray at
   that plane); then the volume rule Cf + T (Cscene - Cf), T = exp(-density L), or the ocean rule C 2^(-k h0) x (2^(j L) - 1) / j x Cf +
   NearFogColor x the sun x T x sat(0.1 L) + Cscene x T, T = 2^(-k (L + the depth of the end point)) (Water/SwcWaterData.h ocean_inside,
   volume_inside: the same formulas; tools/test-water.sh tests them). Cf = FogColor x FogScale x the source sun (the volumes with
   SunAffected 0: x 10000). The client sets the values of the water of the camera (Water/SwcWaterSubsystem.cpp UpdateUnder).
2. M_SwcWaterUnder: the underside of the source surfaces (the client draws a copy of each surface with the triangles in the other order,
   the same vertices, UVs, and frame: the copy shows only from below). Translucent, unlit, no air fog. The normal of the source (the same
   UV and normal code as M_SwcWaterSource, read from water_materials.py with no copy); the light from the air through the window of the sky
   (1 - F, the Fresnel equations with n = 1.333; SwcWaterData underside_transmission) as the scene behind the surface with the small
   refraction offset of the source; outside the window (the total reflection) and for the part F the water below: the light of a long path
   in the water from the surface (the ocean Cf / (k (1 + cos)), a volume Cf). No sun lobes, no reflection of the sky cube, no foam (the
   ocean shader of the source sets its reflection to 0 when the facing test fails). The fog between the camera and the sheet is the
   post-process pass (once).

The script writes no file into SWC_CONTENT (it reads water.json for the name of the wave normal texture); the stamp of each material is
its metadata tag SwcVersion (the version and a hash of its code).
Environment: SWC_CONTENT, SWC_WATER_UNREAL_DEST (default /SwcWaterCheck/Water/Under), SWC_WATER_UNREAL_REPORT, SWC_IMPORT_FORCE=1.
"""
import ast
import hashlib
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

DEST = os.environ.get("SWC_WATER_UNREAL_DEST", "/SwcWaterCheck/Water/Under").rstrip("/")
REPORT = os.environ["SWC_WATER_UNREAL_REPORT"]
content = os.environ["SWC_CONTENT"]
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
VERSION = "1"
if DEST not in ("/SwcWaterCheck/Water/Under", "/SwcGameContent/Water/Under"):
    raise RuntimeError(f"the underwater materials go to /SwcWaterCheck/Water/Under or /SwcGameContent/Water/Under, not {DEST}")

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty
ST = unreal.MaterialSamplerType
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/SwcGameContent", DEST.rsplit("/", 2)[0]], True)

report = {"dest": DEST, "version": VERSION, "made": [], "kept": [], "notes": [], "failed": []}


def source_strings():
    """@return The UV and normal code of the source parent (water_materials.py), read as literals: that script makes its assets when it
    runs, thus it is not imported."""
    tree = ast.parse(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "water_materials.py")).read())
    out = {}
    for item in tree.body:
        if isinstance(item, ast.Assign) and len(item.targets) == 1 and isinstance(item.targets[0], ast.Name):
            if item.targets[0].id in ("UV_HLSL", "NORMAL_HLSL", "SCALARS"):
                out[item.targets[0].id] = ast.literal_eval(item.value)
    return out


def connect(source, pin, target, name):
    for candidate in (name, name.replace(" ", "")):
        if editing.connect_material_expressions(source, pin, target, candidate):
            return
    raise RuntimeError(f"the pin {name} of {target.get_name()} does not connect")


def fresh(name, stamp):
    """@return The material to make (an empty new asset), or None when the asset of this stamp exists (its metadata tag SwcVersion)."""
    path = f"{DEST}/{name}"
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        if not force and material is not None and assets.get_metadata_tag(material, "SwcVersion") == stamp:
            report["kept"].append(name)
            return None
        assets.delete_asset(path)   # only dynamic instances of the client use it
    return tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())


def stamp_of(*codes):
    return VERSION + ":" + hashlib.sha1("".join(codes).encode()).hexdigest()[:12]


def finish(material, name, stamp, code_node, pins):
    editing.recompile_material(material)
    assets.set_metadata_tag(material, "SwcVersion", stamp)
    assets.save_loaded_asset(material, only_if_is_dirty=False)
    sources = editing.get_inputs_for_material_expression(material, code_node)
    missing = [pins[i] for i in range(len(pins)) if i >= len(sources) or sources[i] is None]
    if missing:
        raise RuntimeError(f"{name}: pins with no source: {missing}")
    report["made"].append(name)


# The fog of the water between the camera and the scene (cm in, m in the formulas). Ray: the scene point of the pixel from the camera (the
# camera-relative world position of a post-process material; the sky has its far depth). Above: the camera z - the water level (cm; the
# graph subtracts in large-world precision). The near plane: View.NearPlane along the view axis.
UNDER_HLSL = """const float UNIT = 10000.0;
float3 S = Scene.rgb;
float dist = max(length(Ray), 1e-3);
float3 d = Ray / dist;
float nearCm = View.NearPlane / max(dot(d, View.ViewForward.xyz), 1e-3);
float startCm = Above + d.z * nearCm;
if (startCm >= 0.0) { return S; }
float h0 = -startCm * 0.01;
float tScene = max(dist - nearCm, 0.0) * 0.01;
float tPlane = d.z > 1e-5 ? h0 / d.z : 1.0e5;
float L = min(min(tPlane, tScene), 1.0e5);
bool ocean = Kind > 0.5;
float3 Sun = SunColor.a > 0.5 ? clamp(SunColor.rgb, 0.0, 1.0e5) : View.DirectionalLightColor.rgb;
float3 Cf = FogColor.rgb * FogScale * (ocean || SunAffected > 0.5 ? Sun : UNIT.xxx);
float3 C;
float T;
if (!ocean)
{
    // WaterVolumeOutofPS: the fog color and exp2(-density' L), density' = density x log2(e).
    T = exp(-Density * L);
    C = Cf + T * (S - Cf);
}
else
{
    // OceanOutofPS: cUnderWaterInScatterConst x (exp2(t L) - 1) / t, t = k (d.z - 1); the near color x a x sat(0.1 L); a = exp2(-k (L +
    // max(level - z of the scene point, 0))). The constant: 2^(-k h0) [I].
    float k = Density * 1.442695;
    float j = min(k * (d.z - 1.0), -1e-4);
    float I = exp2(-k * h0) * (exp2(j * L) - 1.0) / j;
    T = exp2(-k * (L + max(h0 - L * d.z, 0.0)));
    C = Cf * I + NearFogColor.rgb * Sun * T * saturate(0.1 * L) + S * T;
}
float white = 2000.0;
if (Debug > 0.5 && Debug < 1.5) { C = float3(1.0, 0.0, 0.0) * white; }
if (Debug > 1.5 && Debug < 2.5) { C = float3(T, saturate(L / 20.0), saturate(h0 / 5.0)) * white; }
if (Debug > 2.5 && Debug < 3.5) { C = tPlane < tScene ? float3(0.0, 0.0, 1.0) * white : float3(0.0, 1.0, 0.0) * white; }
return any((asuint(C) & 0x7f800000u) == 0x7f800000u) ? S : min(C, 6.0e4);"""
UNDER_PINS = ["Scene", "Ray", "Above", "Kind", "FogColor", "NearFogColor", "FogScale", "Density", "SunColor", "SunAffected", "Debug"]


def make_post():
    name = "M_SwcUnderwater"
    stamp = stamp_of(UNDER_HLSL)
    material = fresh(name, stamp)
    if material is None:
        return
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_BLOOM)
    scene = sm.node(material, unreal.MaterialExpressionSceneTexture, -900, -300)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    ray = sm.node(material, unreal.MaterialExpressionWorldPosition, -900, -200)
    ray.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    camera = sm.node(material, unreal.MaterialExpressionCameraPositionWS, -1300, -100)
    camera_z = sm.mask(material, camera, "", "b", -1100, -100)
    level = sm.scalar(material, "WaterZ", 4000.0, -1300, 0)
    above = sm.node(material, unreal.MaterialExpressionSubtract, -900, -50)
    connect(camera_z, "", above, "A")
    connect(level, "", above, "B")
    values = {"Kind": sm.scalar(material, "Kind", 1.0, -900, 50), "FogScale": sm.scalar(material, "FogScale", 1.0, -900, 300),
              "Density": sm.scalar(material, "Density", 0.5, -900, 350), "SunAffected": sm.scalar(material, "SunAffected", 1.0, -900, 450),
              "Debug": sm.scalar(material, "Debug", 0.0, -900, 500),
              "FogColor": sm.vector(material, "FogColor", (0.03, 0.09, 0.11, 1.0), -900, 100),
              "NearFogColor": sm.vector(material, "NearFogColor", (0.0, 0.0, 0.0, 1.0), -900, 200),
              "SunColor": sm.vector(material, "SunColor", (0.0, 0.0, 0.0, 0.0), -900, 400)}
    code = sm.custom(material, UNDER_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, UNDER_PINS, -400, 0)
    connect(scene, "Color", code, "Scene")
    connect(ray, "", code, "Ray")
    connect(above, "", code, "Above")
    for pin in UNDER_PINS[3:]:
        connect(values[pin], "RGBA" if pin == "SunColor" else "", code, pin)
    editing.connect_material_property(code, "", MP.MP_EMISSIVE_COLOR)
    finish(material, name, stamp, code, UNDER_PINS)


# The underside of a surface (the copy with the other triangle order; its vertex frame is the frame of the top side: N is up).
UNDERSIDE_HLSL = """const float UNIT = 10000.0;
const float n = 1.333;
float3 N = normalize(TransformTangentVectorToWorld(Parameters.TangentToWorld, Nts));
// The water normal points up (a frame of the copy that the mesh build turns down gives the same surface).
N = N.z < 0.0 ? -N : N;
float3 d = -Parameters.CameraVector;
float cosI = saturate(dot(d, N));
float sinT2 = n * n * (1.0 - cosI * cosI);
float t = 0.0;
if (cosI > 0.0 && sinT2 < 1.0)
{
    float cosT = sqrt(1.0 - sinT2);
    float rs = (n * cosI - cosT) / (n * cosI + cosT);
    float rp = (n * cosT - cosI) / (n * cosT + cosI);
    t = 1.0 - 0.5 * (rs * rs + rp * rp);
}
float2 uv = SvPositionToBufferUV(Parameters.SvPosition);
float zS = Parameters.ScreenPosition.w;
float2 uvR = uv + N.xy * RefrBump * 0.05;
float zR = CalcSceneDepth(uvR) + 0.0 * SceneDepthIn;
if (zR < zS) { uvR = uv; }
float3 Cair = DecodeSceneColorForMaterialNode(uvR) + 0.0 * SceneColorIn;
bool ocean = Kind > 0.5;
float3 Sun = SunColor.a > 0.5 ? clamp(SunColor.rgb, 0.0, 1.0e5) : View.DirectionalLightColor.rgb;
float3 Cf = FogColor.rgb * FogScale * (ocean || SunAffected > 0.5 ? Sun : UNIT.xxx);
float k = Density * 1.442695;
float3 Cdeep = ocean ? Cf / max(k * (1.0 + cosI), 1e-4) : Cf;
float3 C = t * Cair + (1.0 - t) * Cdeep;
float white = 2000.0;
if (Debug > 0.5 && Debug < 1.5) { C = float3(t, 1.0 - t, 0.0) * white; }
if (Debug > 1.5 && Debug < 2.5) { C = Cair; }
if (Debug > 2.5 && Debug < 3.5) { C = Cdeep; }
return any((asuint(C) & 0x7f800000u) == 0x7f800000u) ? float3(0.0, 0.0, 0.0) : min(C, 6.0e4);"""
UNDERSIDE_PINS = ["Nts", "SceneColorIn", "SceneDepthIn", "Kind", "FogColor", "FogScale", "Density", "SunAffected", "RefrBump", "Debug", "SunColor"]
UV_PINS = ["P", "UV", "Time", "Kind", "UseMeshUV", "UVScale", "Tilling", "DetailTilling", "FlowSpeed", "FlowScale", "WaveSpeed",
           "WindDirection", "WindScale"]


def make_underside(code):
    name = "M_SwcWaterUnder"
    stamp = stamp_of(UNDERSIDE_HLSL, code["UV_HLSL"], code["NORMAL_HLSL"])
    material = fresh(name, stamp)
    if material is None:
        return
    index = json.load(open(os.path.join(content, "water", "water.json")))
    waves = assets.load_asset(f"/SwcGameContent/Water/Textures/{index['textures']['waves']['name']}")
    if waves is None:
        raise RuntimeError("no wave normal texture in /SwcGameContent/Water/Textures (tools/import-water.sh)")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try:
        # The fog of the path from the camera to the sheet is the post-process pass: no air fog on the sheet.
        material.set_editor_property("apply_fogging", False)
    except Exception as error:   # the name of the property in this engine version
        report["notes"].append(f"M_SwcWaterUnder: apply_fogging not set ({error})")
    defaults = dict(code["SCALARS"])
    names = ["Kind", "UseMeshUV", "UVScale", "Tilling", "DetailTilling", "NormalsScale", "DetailNormalsScale", "NormalStrength", "FlowSpeed",
             "FlowScale", "WaveSpeed", "WindDirection", "WindScale", "FogScale", "Density", "SunAffected", "RefractionBumpScale", "Debug"]
    p = {key: sm.scalar(material, key, defaults.get(key, 0.0), -2200, -600 + 50 * k) for k, key in enumerate(names)}
    fog = sm.vector(material, "FogColor", (0.03, 0.09, 0.11, 1.0), -2200, 400)
    sun = sm.vector(material, "SunColor", (0.0, 0.0, 0.0, 0.0), -2200, 500)
    position = sm.node(material, unreal.MaterialExpressionWorldPosition, -1900, -700)
    coordinate = sm.node(material, unreal.MaterialExpressionTextureCoordinate, -1900, -600)
    time = sm.node(material, unreal.MaterialExpressionTime, -1900, -500)
    uv = sm.custom(material, code["UV_HLSL"], unreal.CustomMaterialOutputType.CMOT_FLOAT4, UV_PINS, -1500, -700)
    connect(position, "", uv, "P")
    connect(coordinate, "", uv, "UV")
    connect(time, "", uv, "Time")
    for pin in UV_PINS[3:]:
        connect(p[pin], "", uv, pin)
    base = sm.mask(material, uv, "", "rg", -1250, -750)
    detail = sm.mask(material, uv, "", "ba", -1250, -650)
    quarter = sm.custom(material, "return B * 0.25;", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["B"], -1100, -800)
    connect(base, "", quarter, "B")
    samples = []
    for k, source in enumerate((quarter, base, detail)):
        node = sm.sampler(material, "WaveNormal", ST.SAMPLERTYPE_NORMAL, waves, -900, -800 + 220 * k)
        connect(source, "", node, "UVs")
        samples.append(node)
    normal = sm.custom(material, code["NORMAL_HLSL"], unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                       ["N0", "N1", "N2", "NormalsScale", "DetailNormalsScale", "NormalStrength"], -600, -700)
    for pin, node in zip(("N0", "N1", "N2"), samples):
        connect(node, "RGB", normal, pin)
    for pin in ("NormalsScale", "DetailNormalsScale", "NormalStrength"):
        connect(p[pin], "", normal, pin)
    scene_color = sm.node(material, unreal.MaterialExpressionSceneColor, -900, 600)
    scene_depth = sm.node(material, unreal.MaterialExpressionSceneDepth, -900, 700)
    under = sm.custom(material, UNDERSIDE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, UNDERSIDE_PINS, -300, 0)
    connect(normal, "", under, "Nts")
    connect(scene_color, "", under, "SceneColorIn")
    connect(scene_depth, "", under, "SceneDepthIn")
    connect(fog, "", under, "FogColor")
    connect(sun, "RGBA", under, "SunColor")
    for pin, key in (("Kind", "Kind"), ("FogScale", "FogScale"), ("Density", "Density"), ("SunAffected", "SunAffected"),
                     ("RefrBump", "RefractionBumpScale"), ("Debug", "Debug")):
        connect(p[key], "", under, pin)
    editing.connect_material_property(under, "", MP.MP_EMISSIVE_COLOR)
    one = sm.node(material, unreal.MaterialExpressionConstant, -100, 300)
    one.set_editor_property("r", 1.0)
    editing.connect_material_property(one, "", MP.MP_OPACITY)
    finish(material, name, stamp, under, UNDERSIDE_PINS)


try:
    make_post()
    make_underside(source_strings())
except Exception as error:   # the report has the fault; the commandlet ends
    report["failed"].append(str(error))
with open(REPORT, "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC WATER UNDER " + json.dumps(report))
