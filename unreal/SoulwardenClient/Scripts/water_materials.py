"""The water textures and the source water material (AST-200, docs/WATER.md "The source water shader"), into /SwcGameContent/Water.

Run through tools/import-water.sh (UnrealEditor-Cmd -run=pythonscript, no window). The textures come from the converter
(tools/convert-water.sh write or source-textures: SWC_CONTENT/water/water.json "textures" and "sourceTextures"). The assets are
made from the game copy: they go to the shared content plugin, not into git.

M_SwcWaterSource: the water shaders of the source (research ~/Games/soulwarden-client/research/water-shader.md: water.cfx for the
ocean, watervolume.cfx for the water volumes and the rivers), in one translucent unlit material with the source formulas in a
Custom node (decision of the user, 2026-10-20: the look of the water must match retail; it replaces the Single Layer Water of
2026-10-18, whose composite adds its own reflection, Fresnel, and light in the water). The client (Water/SwcWaterSubsystem) makes
one dynamic instance for each surface and sets the source values. The node reads the opaque scene behind the water (the scene
color and the scene depth of the translucency pass) and does:
- the normals: the three-sample sum of the source shaders (two coarse normals at B.xy / 4 and B.xy, one detail XY at B.zw,
  x NormalsScale); the normal texture is still the static oceanwaves_ddn [inference: the source normal field is generated at run
  time, research "The real normal input"];
- the light in the water: the volumes and the rivers Cfog + T (Cbehind - Cfog), T = exp(-density x the view path in the water in m),
  Cfog = FogColor x FogColorScale x the sun color; the ocean the depth-light integral of WaterFogVolume/OceanInto with the shallow
  color (nearFogColor);
- the reflection: a screen-space trace of the opaque scene (64 steps in the screen, the thickness min(5 m, depth / 20), 16 steps of
  binary search, the view fade of WaterReflPS), the sky cube as the fallback of the volumes and the rivers, the environment cube of
  the material x ReflectionScale under the trace for the ocean; the sun as the two normalized power lobes of the source;
- the Fresnel of each kind: volumes saturate(0.05 + 0.95 (1 - NoV)^5) x the soft edge x the near fade, the ocean
  0.065 + 0.935 (1 - NoV)^5 / (40 - 39 FresnelGloss);
- the foam in the shallow water: the band of the source m x sat(m - 2 G s) x sat(s - s^2), s = sat(FoamSoftIntersectionFactor x the
  view-depth gap in m), m from waterfoam.dds as linear data; added and lit by the sun for the volumes, blended over the water for
  the ocean.
The radiance of one source unit is 10,000 cd/m2 (docs/ENVIRONMENT.md); the sun color of the source is the Unreal sun E / pi
(View.DirectionalLightColor). Unreal applies the air fog to the output (once over the surface; the scene behind it has its own fog).
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
PARENT = "M_SwcWaterSource"
# The rule of the parent: a new value makes it again.
PARENT_VERSION = "8"

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty
ST = unreal.MaterialSamplerType
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
    task.set_editor_property("save", record["color"] != "hdr")
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(asset)
    if texture is None:
        raise RuntimeError(f"{role}: no texture after the import")
    if record["color"] == "hdr":
        # The environment cube of the ocean (a long-lat Radiance picture): a cube texture, linear HDR, with its mips (the source
        # samples mip 6 - 6 g).
        if not isinstance(texture, unreal.TextureCube):
            raise RuntimeError(f"{asset}: the import gave {type(texture).__name__}, not a TextureCube")
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_HDR)
        texture.set_editor_property("never_stream", True)
    else:
        sm.texture_settings(texture, record["color"])
    assets.save_loaded_asset(texture, only_if_is_dirty=False)
    state["texture:" + role] = stamp
    report["textures"] += 1
    return texture


def connect(source, pin, target, name):
    # The pins of an output node with no own input names have the names of their properties.
    for candidate in (name, name.replace(" ", "")):
        if editing.connect_material_expressions(source, pin, target, candidate):
            return
    raise RuntimeError(f"the pin {name} of {target.get_name()} does not connect")


# The coordinates B of the source (research "Coordinates and normal sum"), with the world in m:
# - a river (UseMeshUV 1): q = (along, across) of the mesh UV (the converter writes u across, v along; the source flow moves the
#   first coordinate) [inference: the adapter of the research], B.xy = q T - f t (0.25, 0), B.zw = (2 q T - f t (1, 0)) D;
# - a volume: the mesh UV (the world x SurfaceUScale, SurfaceVScale of the converter) at UVScale, no flow;
# - the ocean (Kind 1): vTex = P x 0.005, B.xy = vTex T + move, B.zw = swap((2 vTex T + 2 move) max(2, D)); move = the wind of
#   the client (WindDirection, WaveSpeed x WindScale) [inference: the CPU map of OceanParams is not known].
UV_HLSL = """float2 world = P.xy * 0.01;
float2 wind = float2(cos(WindDirection), sin(WindDirection)) * WaveSpeed * WindScale * Time;
if (Kind > 0.5)
{
    float2 v = world * 0.005;
    float2 b = v * Tilling + wind;
    float2 d = (2.0 * v * Tilling + 2.0 * wind) * max(2.0, DetailTilling);
    return float4(b, d.yx);
}
float2 q = UseMeshUV > 0.5 ? float2(UV.y, UV.x) : UV * UVScale;
float f = UseMeshUV > 0.5 ? FlowSpeed * FlowScale * Time : 0.0;
float2 b = q * Tilling - f * float2(0.25, 0.0) + (UseMeshUV > 0.5 ? 0.0 : wind);
float2 d = (2.0 * q * Tilling - f * float2(1.0, 0.0)) * DetailTilling;
return float4(b, d);"""
# The normal sum of the source (V 0026-0044, O 0054-0077): b = W(B.xy / 4) + W(B.xy); b.xy += W(B.zw).xy x DetailNormalsScale;
# b.xy x= NormalsScale. NormalStrength scales the slope of the static texture (1: the source sum) [calibration of the client].
NORMAL_HLSL = """float3 b = N0 + N1;
b.xy += N2.xy * DetailNormalsScale;
b.xy *= NormalsScale * NormalStrength;
return normalize(b);"""
WATER_HLSL = """const float UNIT = 10000.0;   // cd/m2 of one radiance unit of the source
const float PI_ = 3.14159265;
float3 up = float3(0.0, 0.0, 1.0);
bool ocean = Kind > 0.5;
// FogSrgb 1: the fog colors of the data are sRGB-authored (bytes / 255) and decode to linear [inference: the setter of the component
// is not traced; research water-shader.md "The ocean water-fog pass"].
float3 FogLin = FogSrgb > 0.5 ? (FogColor <= 0.04045 ? FogColor / 12.92 : pow((FogColor + 0.055) / 1.055, 2.4)) : FogColor;
float3 NearLin = FogSrgb > 0.5 ? (NearFogColor <= 0.04045 ? NearFogColor / 12.92 : pow((NearFogColor + 0.055) / 1.055, 2.4)) : NearFogColor;
float3 V = Parameters.CameraVector;
float3 N = normalize(TransformTangentVectorToWorld(Parameters.TangentToWorld, Nts));
float2 uv = SvPositionToBufferUV(Parameters.SvPosition);
float zS = Parameters.ScreenPosition.w;
float zB = CalcSceneDepth(uv) + 0.0 * SceneDepthIn;
float q = max(zB - zS, 0.0) * 0.01;            // the view-depth gap behind the surface, m
float a = saturate(Soft * q);                  // SoftIntersectionFactor (per m)
float3 Sun = View.DirectionalLightColor.rgb;    // the sun color of the source x UNIT
float3 Ls = normalize(View.DirectionalLightDirection.xyz);
float sunUnit = dot(Sun, float3(0.2126, 0.7152, 0.0722)) / UNIT;

// Refraction: the offset N.xy x RefractionBumpScale x a; an offset onto a nearer surface takes the pixel itself.
float2 uvR = uv + N.xy * RefrBump * a * 0.05;
float zR = CalcSceneDepth(uvR);
if (zR < zS) { uvR = uv; zR = zB; }
float3 Cb = DecodeSceneColorForMaterialNode(uvR) + 0.0 * SceneColorIn;

// The light in the water.
float3 d = -V;
float cosV = max(dot(d, View.ViewForward.xyz), 0.001);
float Lw = max((ocean ? zB : zR) - zS, 0.0) * 0.01 / cosV;   // the view path in the water, m
float3 Cw;
float Tw = 1.0;
if (!ocean)
{
    float3 Cf = FogLin * FogScale * (SunAffected > 0.5 ? Sun : UNIT.xxx);
    float T = exp(-Density * Lw);
    Tw = T;
    Cw = Cf + T * (Cb - Cf);
}
else
{
    // WaterFogVolume/OceanInto: k base 2, the entry at the surface (the camera above the water).
    float k = Density * 1.442695;
    float j = k * (min(d.z, 0.0) - 1.0);
    float I = (exp2(j * Lw) - 1.0) / min(j, -1e-4);
    float T = exp2(-k * (Lw + max(0.0, -d.z * Lw)));
    Cw = FogLin * FogScale * Sun * I + NearLin * Sun * T * saturate(0.1 * Lw) + Cb * T;
    // SurfaceFog 1: the surface shader also applies its simple fog (cFogColorDensity, the same fog block) on the scene that the ocean
    // pass has fogged (the branch of the CPU that does not clear the uniform; research "The ocean water-fog pass").
    if (SurfaceFog > 0.5)
    {
        float3 Cf = FogLin * FogScale * Sun;
        float Ts = exp(-Density * Lw);
        Cw = Cf + Ts * (Cw - Cf);
        T *= Ts;
    }
    Tw = T;
}

// The sun: two normalized power lobes (gloss g = Shininess / 255, no gloss map: G = 1).
float3 Rn = reflect(-V, N);
float mu = saturate(dot(Ls, Rn));
float p0 = 4.0 * 255.0 * MaterialGloss;
float p1 = 0.5 * 255.0 * MaterialGloss;
float lobe = (p0 / (2.0 * PI_) + 1.0 / PI_) * pow(mu, p0) + (p1 / (2.0 * PI_) + 1.0 / PI_) * pow(mu, p1);
float3 Csun = saturate(dot(N, Ls)) * lobe * Spec * Sun;

// The reflection: the screen-space trace of WaterReflPS with the flatter reflection normal.
float3 Nr = normalize(lerp(up, N, ocean ? ReflBump : 0.3));
float3 R = reflect(-V, Nr);
float3 P0 = GetTranslatedWorldPosition(Parameters);
float4 c0 = mul(float4(P0, 1.0), View.TranslatedWorldToClip);
float4 c1 = mul(float4(P0 + R * 200000.0, 1.0), View.TranslatedWorldToClip);   // 2000 m
const float nearW = 10.0;
if (c1.w < nearW) { c1 = lerp(c0, c1, (c0.w - nearW) / max(c0.w - c1.w, 1e-4)); }
float2 v0 = c0.xy / c0.w * float2(0.5, -0.5) + 0.5;
float2 v1 = c1.xy / c1.w * float2(0.5, -0.5) + 0.5;
float iw0 = 1.0 / c0.w;
float iw1 = 1.0 / c1.w;
float2 toBuffer = View.ViewSizeAndInvSize.xy * View.BufferSizeAndInvSize.zw;
float2 rectMin = View.ViewRectMin.xy * View.BufferSizeAndInvSize.zw;
float jitter = frac(52.9829189 * frac(dot(Parameters.SvPosition.xy, float2(0.06711056, 0.00583715))) + View.StateFrameIndexMod8 * 0.618034);
float tPrev = 0.0;
float tHit = -1.0;
float prevDepth = c0.w;
const int STEPS = 64;
[loop] for (int it = 0; it < STEPS; ++it)
{
    float t = (it + jitter) / STEPS;
    float2 vp = lerp(v0, v1, t);
    if (any(vp < 0.0) || any(vp > 1.0)) { break; }
    float rayDepth = 1.0 / lerp(iw0, iw1, t);
    float sceneDepth = CalcSceneDepth(vp * toBuffer + rectMin);
    float thickness = min(500.0, sceneDepth / 20.0);
    if (rayDepth > sceneDepth && prevDepth < sceneDepth + thickness) { tHit = t; break; }
    prevDepth = rayDepth;
    tPrev = t;
}
float4 ssr = 0.0;
float hitDepth = 0.0;
if (tHit >= 0.0)
{
    float lo = tPrev;
    float hi = tHit;
    [loop] for (int bs = 0; bs < 16; ++bs)
    {
        float mid = 0.5 * (lo + hi);
        float2 vp = lerp(v0, v1, mid);
        float rayDepth = 1.0 / lerp(iw0, iw1, mid);
        if (rayDepth > CalcSceneDepth(vp * toBuffer + rectMin)) { hi = mid; } else { lo = mid; }
    }
    float2 hitUV = lerp(v0, v1, hi) * toBuffer + rectMin;
    hitDepth = CalcSceneDepth(hitUV);
    // A hit on the far plane (the sky) or in front of the surface (a body in front of the water) is no hit.
    if (hitDepth < 1.0e7 && hitDepth >= zS - 1.0)
    {
        float kf = dot(abs(normalize(View.ViewForward.xyz)), Nr);
        float alpha = min(saturate((0.6 - kf) / 0.25), 0.95);
        ssr = float4(DecodeSceneColorForMaterialNode(hitUV) * alpha, alpha);
    }
}
if (SkyFallback > 0.5)
{
    // The sky: the rendered sky of the screen where the reflected direction is in the screen (LiveSky 1, a decision of the client:
    // $SkyCubemap of the source is a sky cube of unknown content), else the cube of the sky light (the world probe).
    // The world probe has the ground of its place under its horizon: a sky direction stays above it.
    float3 Rsky = normalize(float3(R.xy, max(R.z, 0.15)));
    float3 sky = TextureCubeSampleLevel(SkyCube, SkyCubeSampler, Rsky, 0.0).rgb * View.SkyLightColor.rgb * SkyGain;
    // The end of the ray in the screen: the end point, or the point where the ray leaves the screen.
    float2 dv = v1 - v0;
    float2 tx = (dv > 0.0 ? (1.0 - v0) : -v0) / (abs(dv) > 1e-6 ? dv : 1e-6);
    float tEnd = min(1.0, min(tx.x, tx.y));
    float2 vEnd = lerp(v0, v1, saturate(tEnd * 0.995));
    float2 skyUV = vEnd * toBuffer + rectMin;
    // Only an end in the screen or at its top edge (a side edge gives one column of pixels to all the rays of that side).
    bool topExit = tEnd >= 1.0 || (tx.y <= tx.x && dv.y < 0.0);
    if (LiveSky > 0.5 && topExit && all(vEnd >= 0.0) && all(vEnd <= 1.0) && CalcSceneDepth(skyUV) >= 1.0e7)
    {
        sky = DecodeSceneColorForMaterialNode(skyUV) * SkyGain;
    }
    ssr = float4(ssr.rgb + sky * (1.0 - ssr.a), 1.0);
}
float3 Crefl;
if (!ocean)
{
    // Cur = mix(Ccube x EnvCubeReflMul, ssr x RealtimeReflMul, a); the box cube of the material is under the sky fallback.
    float3 Ccube = TextureCubeSampleLevel(SkyCube, SkyCubeSampler, R, 6.0 - 6.0 * MaterialGloss).rgb * View.SkyLightColor.rgb * Spec;
    Crefl = lerp(Ccube * CubeMul, ssr.rgb * ReflMul, saturate(ssr.a));
}
else
{
    float dist = zS * 0.01 / cosV;   // the camera distance, m
    float g = MaterialGloss * max(1.0 - dist / 250.0, 0.5);
    float3 Ccube = TextureCubeSampleLevel(OceanCube, OceanCubeSampler, R, 6.0 - 6.0 * g).rgb * ReflScale * CubeGain;
    float4 r = saturate(ssr / float4(UNIT.xxx, 1.0)) * float4(UNIT.xxx, 1.0);   // the source clamps the RGBA to 0..1
    Crefl = dot(V, N) > 0.0 ? lerp(Ccube, r.rgb, r.a) : float3(0.0, 0.0, 0.0);
}

// The Fresnel and the blend.
float NoV = saturate(dot(N, V));
float3 C;
if (!ocean)
{
    float F = saturate(0.05 + 0.95 * pow(1.0 - NoV, 5.0));
    C = lerp(Cw, Crefl + Csun, F * a * saturate(zS * 0.01 - 0.33));
}
else
{
    float F = 0.065 + 0.935 * pow(1.0 - NoV, 5.0) / (40.0 - 39.0 * FresnelGloss);
    C = lerp(Cw, Crefl + Csun, saturate(F * a));
}

// The foam in the shallow water: the band of the view-depth gap (s), the linear foam map (m).
float s = saturate(FoamSoft * q);
float m = 0.0;
float f = 0.0;
if (!ocean)
{
    m = Texture2DSample(FoamData, FoamDataSampler, FoamUV * FoamTilling + Nts.xy * 0.1).r;
    f = m * saturate(m - 2.0 * s) * saturate(s - s * s);
    C += f * saturate(FoamAmount) * Sun * saturate(dot(N, Ls));
}
else
{
    float G = 0.7 * Texture2DSample(Gloss, GlossSampler, FoamUV * 0.05).r + 0.3;
    float h = 0.0;   // the crest signal of the displacement (no displacement yet)
    float2 u = FoamUV * FoamTilling;
    float2 move = -float2(cos(WindDirection), sin(WindDirection)) * WaveSpeed * WindScale * FoamTransition * 0.1 * Time;
    float2 u2 = 2.73 * u + move * FoamTilling;
    m = Texture2DSampleBias(FoamData, FoamDataSampler, u, -(1.0 - 4.0 * h)).r + 0.5 * Texture2DSampleBias(FoamData, FoamDataSampler, u2, -(1.0 - 2.5 * h)).r;
    float fshore = m * saturate(m - 2.0 * G * s) * saturate(s - s * s);
    float c = m * saturate(h * h);
    float fcrest = (c * c + 0.5 * smoothstep(0.7, 0.9, c)) * FoamCrest;
    f = max(fshore, fcrest) * sunUnit;
    float shade = dot((Crefl + Csun) / UNIT, float3(0.333, 0.333, 0.333));
    f *= saturate(0.5 + shade * shade) * a;
    C = C * (1.0 - f) + f * FoamAmount * UNIT;
}

// Debug views (-SwcWater=Debug=N): 1 the soft edge a, the foam gap s, the foam f x 4; 2 the reflection with the sun; 3 the water
// term; 4 the trace (hit alpha, the sky part, the hit depth / 100 m); 5 the transmittance T, the path / 20 m, the depth gap q / 5 m;
// 6 the scene behind the water.
float3 white = UNIT * max(sunUnit, 0.3);
if (Debug > 0.5 && Debug < 1.5) { C = float3(a, s, saturate(f * 4.0)) * white; }
if (Debug > 1.5 && Debug < 2.5) { C = Crefl + Csun; }
if (Debug > 2.5 && Debug < 3.5) { C = Cw; }
if (Debug > 4.5 && Debug < 5.5) { C = float3(Tw, saturate(Lw / 20.0), saturate(q / 5.0)) * white; }
if (Debug > 5.5) { C = Cb; }
if (Debug > 3.5 && Debug < 4.5) { C = float3(tHit >= 0.0 ? 1.0 : 0.0, ssr.a, saturate(hitDepth / 10000.0)) * white; }
return C;"""
WATER_INPUTS = ["Nts", "FoamUV", "Time", "FoamData", "Gloss", "OceanCube", "SkyCube", "SceneColorIn", "SceneDepthIn", "Kind", "FogColor",
                "NearFogColor", "FogScale", "Density", "SunAffected", "Soft", "RefrBump", "ReflBump", "FresnelGloss", "ReflScale", "ReflMul",
                "CubeMul", "Spec", "MaterialGloss", "SkyFallback", "SkyGain", "CubeGain", "FoamAmount", "FoamTilling", "FoamSoft",
                "FoamCrest", "FoamTransition", "WindDirection", "WaveSpeed", "WindScale", "Debug", "FogSrgb", "LiveSky", "SurfaceFog"]
# The parameters of the instances (name: the default; the client sets the values of the data). Each pin of WATER_HLSL that has a
# parameter of another name maps here.
PIN_PARAMETER = {"Soft": "SoftIntersectionFactor", "RefrBump": "RefractionBumpScale", "ReflBump": "ReflectionBumpScale",
                 "ReflScale": "ReflectionScale", "ReflMul": "RealtimeReflMul", "CubeMul": "EnvCubeReflMul", "Spec": "Specular",
                 "MaterialGloss": "Gloss", "FoamSoft": "FoamSoftIntersectionFactor", "FoamCrest": "FoamCrestAmount",
                 "FoamTransition": "FoamTransitionSpeed"}
SCALARS = [("Kind", 0.0), ("UseMeshUV", 0.0), ("UVScale", 0.1), ("Tilling", 1.0), ("DetailTilling", 2.0), ("NormalsScale", 0.5),
           ("DetailNormalsScale", 0.5), ("NormalStrength", 1.0), ("FlowSpeed", 0.0), ("FlowScale", 0.25), ("WaveSpeed", 1.0),
           ("WindDirection", 0.0), ("WindScale", 0.02), ("FogScale", 1.0), ("Density", 0.5), ("SunAffected", 1.0),
           ("SoftIntersectionFactor", 1.0), ("RefractionBumpScale", 0.1), ("ReflectionBumpScale", 0.1), ("FresnelGloss", 0.98),
           ("ReflectionScale", 0.2), ("RealtimeReflMul", 0.9), ("EnvCubeReflMul", 1.5), ("Specular", 0.0497), ("Gloss", 1.0),
           ("SkyFallback", 1.0), ("SkyGain", 1.0), ("CubeGain", 10000.0), ("FoamAmount", 0.0), ("FoamTilling", 1.0),
           ("FoamSoftIntersectionFactor", 1.0), ("FoamCrestAmount", 0.0), ("FoamTransitionSpeed", 5.0), ("Debug", 0.0), ("FogSrgb", 1.0), ("LiveSky", 1.0), ("SurfaceFog", 0.0)]


def make_parent(textures):
    path = f"{WATER}/{PARENT}"
    if assets.does_asset_exist(path) and state.get("source-parent") == PARENT_VERSION:
        return
    if assets.does_asset_exist(path):
        assets.delete_asset(path)   # only dynamic instances of the client use it
    material = tools.create_asset(PARENT, WATER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("compute_fog_per_pixel", True)
    p = {name: sm.scalar(material, name, value, -2200, -600 + 50 * k) for k, (name, value) in enumerate(SCALARS)}
    fog = sm.vector(material, "FogColor", (0.03, 0.09, 0.11, 1.0), -2200, 1200)
    near = sm.vector(material, "NearFogColor", (0.0, 0.0, 0.0, 1.0), -2200, 1300)
    position = sm.node(material, unreal.MaterialExpressionWorldPosition, -1900, -700)
    coordinate = sm.node(material, unreal.MaterialExpressionTextureCoordinate, -1900, -600)
    time = sm.node(material, unreal.MaterialExpressionTime, -1900, -500)

    uv = sm.custom(material, UV_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                   ["P", "UV", "Time", "Kind", "UseMeshUV", "UVScale", "Tilling", "DetailTilling", "FlowSpeed", "FlowScale", "WaveSpeed",
                    "WindDirection", "WindScale"], -1500, -700)
    connect(position, "", uv, "P")
    connect(coordinate, "", uv, "UV")
    connect(time, "", uv, "Time")
    for name in ("Kind", "UseMeshUV", "UVScale", "Tilling", "DetailTilling", "FlowSpeed", "FlowScale", "WaveSpeed", "WindDirection", "WindScale"):
        connect(p[name], "", uv, name)
    base = sm.mask(material, uv, "", "rg", -1250, -750)
    detail = sm.mask(material, uv, "", "ba", -1250, -650)
    quarter = sm.custom(material, "return B * 0.25;", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["B"], -1100, -800)
    connect(base, "", quarter, "B")
    samples = []
    for k, source in enumerate((quarter, base, detail)):
        node = sm.sampler(material, "WaveNormal", ST.SAMPLERTYPE_NORMAL, textures["waves"], -900, -800 + 220 * k)
        connect(source, "", node, "UVs")
        samples.append(node)
    normal = sm.custom(material, NORMAL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                       ["N0", "N1", "N2", "NormalsScale", "DetailNormalsScale", "NormalStrength"], -600, -700)
    for name, node in zip(("N0", "N1", "N2"), samples):
        connect(node, "RGB", normal, name)
    for name in ("NormalsScale", "DetailNormalsScale", "NormalStrength"):
        connect(p[name], "", normal, name)

    def texture_object(name, texture, kind, y):
        node = sm.node(material, unreal.MaterialExpressionTextureObjectParameter, -900, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", kind)
        return node

    objects = {"FoamData": texture_object("FoamData", textures["foamData"], ST.SAMPLERTYPE_MASKS, 0),
               "Gloss": texture_object("GlossMap", textures["gloss"], ST.SAMPLERTYPE_MASKS, 150),
               "OceanCube": texture_object("OceanCube", textures["oceanCube"], ST.SAMPLERTYPE_LINEAR_COLOR, 300),
               "SkyCube": texture_object("SkyCube", textures["oceanCube"], ST.SAMPLERTYPE_LINEAR_COLOR, 450)}
    scene_color = sm.node(material, unreal.MaterialExpressionSceneColor, -900, 600)
    scene_depth = sm.node(material, unreal.MaterialExpressionSceneDepth, -900, 700)
    water = sm.custom(material, WATER_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, WATER_INPUTS, -300, 0)
    connect(normal, "", water, "Nts")
    connect(base, "", water, "FoamUV")
    connect(time, "", water, "Time")
    for name, node in objects.items():
        connect(node, "", water, name)
    connect(scene_color, "", water, "SceneColorIn")
    connect(scene_depth, "", water, "SceneDepthIn")
    connect(fog, "", water, "FogColor")
    connect(near, "", water, "NearFogColor")
    for pin in WATER_INPUTS[9:]:
        if pin in ("FogColor", "NearFogColor"):
            continue
        connect(p[PIN_PARAMETER.get(pin, pin)], "", water, pin)
    editing.connect_material_property(water, "", MP.MP_EMISSIVE_COLOR)
    one = sm.node(material, unreal.MaterialExpressionConstant, -100, 300)
    one.set_editor_property("r", 1.0)
    editing.connect_material_property(one, "", MP.MP_OPACITY)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["source-parent"] = PARENT_VERSION
    report["material"] = True


try:
    textures = {role: import_texture(role, record) for role, record in index["textures"].items()}
    textures.update({role: import_texture(role, record) for role, record in index.get("sourceTextures", {}).items()})
    missing = [role for role in ("foamData", "gloss", "oceanCube") if role not in textures]
    if missing:
        raise RuntimeError(f"no source textures {missing} in water.json: run tools/convert-water.sh source-textures")
    make_parent(textures)
except Exception as error:   # the report has the fault; the commandlet ends
    report["failed"].append(str(error))
with open(state_path, "w") as handle:
    json.dump(state, handle, indent=1)
with open(os.path.join(content, "water", "import-report.json"), "w") as handle:
    json.dump(report, handle, indent=1)
unreal.log("SWC WATER IMPORT " + json.dumps(report))
