"""The parent materials of the materials track and the rules of its material instances and textures (docs/MATERIALS.md).

A module for the editor scripts (Scripts/apply_materials.py; later the import pipeline can call it). The specs come
from the converter (converter/swc_convert/material_records.py: SWC_CONTENT/materials/instances.json, textures.json).

Parents (in /SwcGameContent/Base, made by this module, versioned by PARENT_VERSIONS in import-state.json):
- M_SwcSurface, M_SwcSurfaceMasked (stage M2): the Illum shader (surface_parent: the UV transform, the reflectance
  split of REFLECTANCE_HLSL, the gloss).
- M_SwcFoliage: masked, two sided, Two Sided Foliage. The cutout is the alpha of the diffuse map (research
  world-materials.md, section 2). Base color = diffuse map x DiffuseColor (the XML Diffuse, a linear factor: the source
  shader does cAlbedo *= PerMaterial_DiffuseColor.rgb). Roughness = 1 - Smoothness x Gloss (Smoothness = max(1/127,
  Shininess/255); the source GGX uses (1 - s)^2 and Unreal squares its Roughness: research section 3). Specular = the
  luminance of the XML Specular / 0.08 (Unreal F0 = 0.08 x Specular). Subsurface color (the transmitted light) =
  saturate(BackDiffuseMultiplier x Transmission.r) x TransmittanceColor (the transmission block of the source shader).
"""
import hashlib
import json
import os

import unreal

ROOT = "/SwcGameContent"
BASE = ROOT + "/Base"
PARENT_VERSIONS = {"M_SwcFoliage": "4", "M_SwcSurface": "8", "M_SwcSurfaceMasked": "8", "M_SwcDecal": "7",
                   "M_SwcGlass": "1", "M_SwcBeam": "5",
                   "M_SwcSurfaceTranslucent": "8"}   # the rule of each parent
# (M_SwcSurface 2: the UV offset from the RGBA output of UVTransform, version 1 did not compile; 3: the overlay masks;
# 4: M3b, the blend layer, the vertex colors, the detail map; 5: M4, the emission; 6: the detail map is linear data;
# 7: the UV transforms of the emittance and intensity maps (AST-207); 8: the sRGB emission files on Color samplers;
# M_SwcDecal 2: the vertex alpha of the mesh decals, static switch UseVertexAlpha; 3: the view bias of the mesh decals;
# 4: the entity color and opacity (DecalColor), the angle fade of the projected decals (static switch Projected);
# 5: the view bias in the vertex shader without CameraVectorWS (3 and 4 did not compile); 6: the normal of the
# projected decals out of the surface (5 had it into the surface); 7: the reflectance of the source decal (the
# specular map x XML Specular; 6 and before: the default 0.5); M_SwcFoliage 2: the wind (M6) and
# the vertex alpha occlusion; 3: the subsurface color x the albedo, as the back light of the source; 4: the camera fade)
# The parent asset of each spec parent (apply_specs: a stale parent is made again).
PARENT_NAMES = {"foliage": "M_SwcFoliage", "surface": "M_SwcSurface", "surface masked": "M_SwcSurfaceMasked",
                "surface translucent": "M_SwcSurfaceTranslucent", "decal": "M_SwcDecal", "glass": "M_SwcGlass",
                "beam": "M_SwcBeam"}
INSTANCE_VERSION = "1"  # the rule of apply(): a new value applies each spec again

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty


def log(text):
    unreal.log("SWC " + text)


def file_stamp(path):
    info = os.stat(path)
    return f"{info.st_size}:{info.st_mtime_ns}"


def first_asset(folder, class_name):
    for path in assets.list_assets(folder, recursive=True, include_folder=False):
        if str(assets.find_asset_data(path).asset_class_path.asset_name) == class_name:
            return assets.load_asset(path)
    return None


def import_png(path, folder):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    return first_asset(folder, "Texture2D")


def texture_settings(texture, color):
    """The import settings of a color space of the converter (material_records.COLOR): srgb (base color: sRGB, the
    default compression), normal (linear, BC5), linear (one channel of data: linear, BC4 from R)."""
    if color == "normal":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    elif color == "data":   # several channels of data (an overlay mask): linear, the default compression with alpha
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    elif color == "linear":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_ALPHA)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR)
    else:
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)


def import_texture(key, record, content, state):
    """Imports one texture of textures.json when it is new or its PNG changed (the stamp of import_content.py: a
    later import of the same key by the pipeline sees it as up to date). @return True when it imported."""
    path = os.path.join(content, record["file"])
    stamp = file_stamp(path)
    folder = record["unreal"]
    if state.get(key) == stamp and assets.list_assets(folder, recursive=True):
        return False
    texture = import_png(path, folder)
    if texture is None:
        raise RuntimeError(f"{key}: no texture after the import")
    texture_settings(texture, record["color"])
    assets.save_loaded_asset(texture)
    state[key] = stamp
    return True


def base_texture(name, color, channels, value, content, state):
    """@return A default texture of this module (4 x 4 pixels of one value; a sampler needs a default texture of its own
    type): `channels` 1 (a grayscale PNG) or 4 (RGBA), imported with the settings of `color`; `value` one byte for all
    channels, or a tuple of the four bytes."""
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path) and state.get("base:" + name) == "1":
        return assets.load_asset(path)
    png = os.path.join(content, "materials", "base", name + ".png")
    os.makedirs(os.path.dirname(png), exist_ok=True)
    if not os.path.isfile(png):
        # A PNG of 4 x 4 pixels, written with no extra Python module (zlib and the PNG chunks).
        import struct
        import zlib

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        pixel = bytes(value) if isinstance(value, tuple) else bytes([value]) * channels   # one value, or RGBA
        raw = b"".join(b"\x00" + pixel * 4 for _ in range(4))
        kind = 0 if channels == 1 else 6
        with open(png, "wb") as handle:
            handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, kind, 0, 0, 0))
                         + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", BASE)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(path)
    texture_settings(texture, color)
    assets.save_loaded_asset(texture)
    state["base:" + name] = "1"
    return texture


def white_linear(content, state):
    """@return The default texture of the one-channel parameters: 4 x 4 white, TC_Alpha."""
    return base_texture("T_SwcWhiteLinear", "linear", 1, 255, content, state)


def white_data(content, state):
    """@return The default texture of a linear RGB map (the specular map of the blend layer): white, TC_Masks."""
    return base_texture("T_SwcWhiteData", "data", 4, 255, content, state)


def black_data(content, state):
    """@return The default texture of the mask parameters: 4 x 4 black RGBA (no mask), TC_Masks."""
    return base_texture("T_SwcBlackData", "data", 4, 0, content, state)


def sampler(material, name, kind, default, x, y):
    node = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("sampler_type", kind)
    node.set_editor_property("texture", default)
    return node


def scalar(material, name, value, x, y):
    node = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def vector(material, name, value, x, y):
    node = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*value))
    return node


def node(material, kind, x, y):
    return editing.create_material_expression(material, kind, x, y)


def parent_asset(name, content, state):
    """@return (the parent material with no expressions, the white linear texture) when the parent must be made again
    (new, or its version in PARENT_VERSIONS changed), else (None, None). The asset path stays: its instances keep
    their parameters."""
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path) and state.get("base:" + name) == PARENT_VERSIONS[name]:
        return None, None
    white = white_linear(content, state)
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        editing.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, BASE, unreal.Material, unreal.MaterialFactoryNew())
    return material, white


# The reflectance of the source (research section 3): D = diffuse map x XML Diffuse, S = specular map x XML Specular
# (F0, linear). Unreal (Default Lit): F0 = lerp(0.08 Specular, BaseColor, Metallic), diffuse = BaseColor (1 - Metallic).
# A dielectric (luminance of S at most 0.08): BaseColor D, Metallic 0, Specular lum(S) / 0.08. Above 0.08 Specular is 1
# and u = 1 - Metallic solves 0.08 u^2 - (s + d) u + d = 0 (s, d: the luminances), so that the luminances of the
# diffuse color and of F0 are kept; BaseColor goes from S (u = 0, a metal) to D / u. Output: float4(BaseColor,
# Metallic). The colored F0 of a dielectric becomes one gray value (a limit of the legacy shading model).
REFLECTANCE_HLSL = """float3 w = float3(0.2126, 0.7152, 0.0722);
float d = dot(D, w);
float s = dot(S, w);
if (s <= 0.08) return float4(D, 0.0);
float b = s + d;
float u = saturate((b - sqrt(max(b * b - 0.32 * d, 0.0))) / 0.16);
float3 base = lerp(S, saturate(D / max(u, 0.001)), u);
return float4(base, 1.0 - u);"""
SPECULAR_HLSL = "return saturate(dot(S, float3(0.2126, 0.7152, 0.0722)) / 0.08);"
# The overlay masks of the source (charactermasking.cfib, AddMasking; called after the diffuse factor in
# clusterforward_commonzpass): Mode 1 = %OVERLAY_MASK, Mode 2 = %COLOR_SAMPLER_OVERLAY_MASK, 0 = none. M is the mask
# map (the Custom slot, linear RGBA). Amt = (Mask_R, Mask_G, Mask_B, Mask_A_Gloss), Ovr = (Mask_R_Override,
# Mask_G_Override, Mask_B_Override, Mask_A_SpecColor_Override), Ext = (Mask_A_GlossShift, ColorMaskStrength,
# ColorMaskOverride, ColorMaskAlphaInf). Output: float4(the diffuse color, the smoothness).
MASK_HLSL = """float3 w = float3(0.2125, 0.7154, 0.0721);
float l = dot(D, w);
float3 d = D;
float g = G;
float3 hi = step(0.5, l.xxx);
if (Mode > 1.5) {
  float3 o = lerp(2.0 * l * M.rgb, 1.0 - 2.0 * (1.0 - l) * (1.0 - M.rgb), hi);
  float3 c = lerp(o, M.rgb, Ext.b);
  d = lerp(d, c, lerp(1.0, M.a, Ext.a) * Ext.g);
} else if (Mode > 0.5) {
  float3 oR = lerp(2.0 * l * CR, 1.0 - 2.0 * (1.0 - l) * (1.0 - CR), hi);
  float3 oG = lerp(2.0 * l * CG, 1.0 - 2.0 * (1.0 - l) * (1.0 - CG), hi);
  float3 oB = lerp(2.0 * l * CB, 1.0 - 2.0 * (1.0 - l) * (1.0 - CB), hi);
  d = lerp(d, lerp(oR, CR * CR, Ovr.r), M.r * Amt.r);
  d = lerp(d, lerp(oG, CG * CG, Ovr.g), M.g * Amt.g);
  d = lerp(d, lerp(oB, CB * CB, Ovr.b), M.b * Amt.b);
  float gm = lerp(0.0, g, saturate(Ext.r / 0.5));
  gm = lerp(gm, 1.0, saturate((Ext.r - 0.5) / 0.5));
  g = lerp(g, gm, M.a * Amt.a);
}
return float4(d, g);"""
# The specular color of the overlay mask (Mode 1): S = lerp(S, Mask_A_SpecColor, M.a x Mask_A_SpecColor_Override).
MASK_SPEC_HLSL = "return (Mode > 0.5 && Mode < 1.5) ? lerp(S, SC, M.a * Ovr.a) : S;"


def custom(material, code, output, inputs, x, y):
    node_ = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node_.set_editor_property("code", code)
    node_.set_editor_property("output_type", output)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    node_.set_editor_property("inputs", pins)
    return node_


def mask(material, source, pin, channels, x, y):
    node_ = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, x, y)
    for c in "rgba":
        node_.set_editor_property(c, c in channels)
    editing.connect_material_expressions(source, pin, node_, "")
    return node_


# M3 (stage M3b): the blend layer, the vertex colors, and the detail map, in the order of the source forward pass
# (clusterforward_commonzpass): D = diffuse x XML Diffuse -> blend layer -> D x vertex RGB -> detail -> overlay masks.
# The blend value of the source (shadelib GetLayerBlendingValue): b = saturate(abs(vertex alpha x mask.r x (1 + BlendFactor))
# ^ BlendFalloff); the vertex alpha is 1 with %BLENDLAYER_IGNORE_VERTEX_ALPHA. P = (BlendFactor, BlendFalloff, unused,
# IgnoreVertexAlpha).
BLEND_HLSL = "float a = P.w > 0.5 ? 1.0 : VA; return saturate(pow(abs(a * M * (1.0 + P.x)), P.y));"
# The detail map of the source (common GetDetailMap: sample.garb x 2 - 1, x (DetailBumpScale, DetailBumpScale,
# DetailDiffuseScale, DetailGlossScale), x the diffuse alpha with %ALPHAMASK_DETAILMAP): D += D x z, s += s x w.
# Sc = the three scales, AM = 1 with %ALPHAMASK_DETAILMAP. Output: float4(D, s).
DETAIL_HLSL = """float4 v = (Det.garb * 2.0 - 1.0) * float4(Sc.x, Sc.x, Sc.y, Sc.z);
if (AM > 0.5) v *= A;
return float4(D + D * v.z, G + G * v.w);"""
# The tangent normal with the detail: N.xy += the detail x y.
DETAIL_NORMAL_HLSL = """float4 v = (Det.garb * 2.0 - 1.0) * float4(Sc.x, Sc.x, Sc.y, Sc.z);
if (AM > 0.5) v *= A;
return normalize(float3(N.xy + v.xy, N.z));"""


# M4: the emission of the source (illum.cfxb GetEmittance, fxconstantdefs GetMaterial_Emittance). With an emittance map
# (Map, raw) and its intensity map (I: the Decal slot, raw; white when the slot is empty): E = Map.rgb x
# lum(Map.rgb)^(EmittanceMapGamma - 1) x I.rgb x Map.a; with no map, E = 0.5. Output = Emittance RGB x Emittance A
# x 0.1 (EMITTANCE_TO_ENGINE_LIGHT_SCALE, 1000/10000) x E, when the Emittance RGB is not black; x the global
# EmissiveScale of MPC_SwcMaterials: at run time 10,000 cd/m2 for each engine unit of the source x swc.LightScale
# (USwcMaterialScale; the balance with the local lights of the environment work).
# C = EmittanceColor (RGB, A), P = (EmittanceMapGamma, has map, unused, unused).
EMISSIVE_HLSL = """float3 w = float3(0.2126, 0.7152, 0.0722);
if (dot(C.rgb, w) <= 0.0) return float3(0.0, 0.0, 0.0);
float3 e = float3(0.5, 0.5, 0.5);
float4 M = P.z > 0.5 ? MapS : Map;
float4 In = P.w > 0.5 ? IS : I;
if (P.y > 0.5) {
  e = M.rgb * pow(max(dot(M.rgb, w), 1e-6), P.x - 1.0);
  e *= In.rgb;
  e *= M.a;
}
return C.rgb * C.a * 0.1 * e * Scale;"""


def collection(content, state):
    """@return MPC_SwcMaterials: the global values of this module. EmissiveScale (default 1; at run time
    USwcMaterialScale), WindVector (M6: the global wind of the level data, EnvState WindVector of the mission, in Unreal
    axes: SWC_CONTENT/materials/globals.json; its length is the wind strength), VegCameraFade (the method, the start and the
    end distance in m of the camera fade of the vegetation). An existing parameter keeps its id (the materials refer to it)."""
    name = "MPC_SwcMaterials"
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path) and state.get("base:" + name) == "3":
        return assets.load_asset(path)
    if assets.does_asset_exist(path):
        mpc = assets.load_asset(path)
    else:
        mpc = tools.create_asset(name, BASE, unreal.MaterialParameterCollection,
                                 unreal.MaterialParameterCollectionFactoryNew())
    globals_path = os.path.join(content, "materials", "globals.json")
    values = json.load(open(globals_path)) if os.path.isfile(globals_path) else {}
    scalars = list(mpc.get_editor_property("scalar_parameters"))
    if not any(str(s.get_editor_property("parameter_name")) == "EmissiveScale" for s in scalars):
        value = unreal.CollectionScalarParameter()
        value.set_editor_property("parameter_name", "EmissiveScale")
        value.set_editor_property("default_value", 1.0)
        scalars.append(value)
        mpc.set_editor_property("scalar_parameters", scalars)
    vectors = list(mpc.get_editor_property("vector_parameters"))
    wind = values.get("WindVector", [1.0, 0.0, 0.0])
    found = [v for v in vectors if str(v.get_editor_property("parameter_name")) == "WindVector"]
    if found:
        found[0].set_editor_property("default_value", unreal.LinearColor(*wind, 0.0))
    else:
        value = unreal.CollectionVectorParameter()
        value.set_editor_property("parameter_name", "WindVector")
        value.set_editor_property("default_value", unreal.LinearColor(*wind, 0.0))
        vectors.append(value)
    # The camera fade of the vegetation (research/ground-cover.md "Camera fade": vegCameraFadeParams = method 1, start 2 m,
    # end 3 m, the defaults of the cvars of the source).
    if not any(str(v.get_editor_property("parameter_name")) == "VegCameraFade" for v in vectors):
        value = unreal.CollectionVectorParameter()
        value.set_editor_property("parameter_name", "VegCameraFade")
        value.set_editor_property("default_value", unreal.LinearColor(1.0, 2.0, 3.0, 0.0))
        vectors.append(value)
    mpc.set_editor_property("vector_parameters", vectors)
    assets.save_loaded_asset(mpc)
    state["base:" + name] = "3"
    return mpc


def switch(material, name, on, off, x, y):
    node_ = editing.create_material_expression(material, unreal.MaterialExpressionStaticSwitchParameter, x, y)
    node_.set_editor_property("parameter_name", name)
    node_.set_editor_property("default_value", False)
    editing.connect_material_expressions(on[0], on[1], node_, "True")
    editing.connect_material_expressions(off[0], off[1], node_, "False")
    return node_


def lerp(material, a, b, alpha, x, y):
    node_ = editing.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    editing.connect_material_expressions(a[0], a[1], node_, "A")
    editing.connect_material_expressions(b[0], b[1], node_, "B")
    editing.connect_material_expressions(alpha[0], alpha[1], node_, "Alpha")
    return node_


def multiply(material, a, b, x, y):
    node_ = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, x, y)
    editing.connect_material_expressions(a[0], a[1], node_, "A")
    editing.connect_material_expressions(b[0], b[1], node_, "B")
    return node_


def surface_parent(content, state, masked, translucent=False):
    """@return M_SwcSurface (opaque), M_SwcSurfaceMasked (the cutout is the alpha of the diffuse map), or
    M_SwcSurfaceTranslucent (AST-207: an alpha blended Illum material, material_records.BLENDED_ILLUM; the opacity is
    the alpha of the diffuse map x the XML Opacity): the parents of the Illum shader (stage M2). UV = TexCoord0 x
    UVTransform.rg + UVTransform.ba (the tiling and offset of the diffuse slot); base color, metallic, and specular from
    REFLECTANCE_HLSL; roughness 1 - Smoothness x gloss."""
    name = "M_SwcSurfaceTranslucent" if translucent else "M_SwcSurfaceMasked" if masked else "M_SwcSurface"
    material, white = parent_asset(name, content, state)
    if material is None:
        return assets.load_asset(f"{BASE}/{name}")
    st = unreal.MaterialSamplerType
    coords = node(material, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    transform = vector(material, "UVTransform", (1.0, 1.0, 0.0, 0.0), -1600, 200)
    # The default output of a VectorParameter is RGB (three values): the offset needs the RGBA output.
    tile = mask(material, transform, "RGBA", "rg", -1400, 150)
    offset = mask(material, transform, "RGBA", "ba", -1400, 300)
    scaled = node(material, unreal.MaterialExpressionMultiply, -1250, 50)
    editing.connect_material_expressions(coords, "", scaled, "A")
    editing.connect_material_expressions(tile, "", scaled, "B")
    uv = node(material, unreal.MaterialExpressionAdd, -1100, 100)
    editing.connect_material_expressions(scaled, "", uv, "A")
    editing.connect_material_expressions(offset, "", uv, "B")

    def sample(parameter, kind, default, y):
        node_ = sampler(material, parameter, kind, default, -900, y)
        editing.connect_material_expressions(uv, "", node_, "UVs")
        return node_

    color = sample("BaseColor", st.SAMPLERTYPE_COLOR, unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), -400)
    diffuse = vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -900, -150)
    albedo = node(material, unreal.MaterialExpressionMultiply, -600, -300)
    editing.connect_material_expressions(color, "RGB", albedo, "A")
    editing.connect_material_expressions(diffuse, "RGB", albedo, "B")
    spec_map = sample("Specular", st.SAMPLERTYPE_COLOR, unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), 0)
    spec_color = vector(material, "SpecularColor", (0.04, 0.04, 0.04, 1.0), -900, 250)
    reflectance = node(material, unreal.MaterialExpressionMultiply, -600, 50)
    editing.connect_material_expressions(spec_map, "RGB", reflectance, "A")
    editing.connect_material_expressions(spec_color, "RGB", reflectance, "B")
    gloss = sample("Gloss", st.SAMPLERTYPE_ALPHA, white, 750)
    smoothness = scalar(material, "Smoothness", 0.5, -900, 1000)
    s_value = node(material, unreal.MaterialExpressionMultiply, -500, 800)
    editing.connect_material_expressions(gloss, "R", s_value, "A")
    editing.connect_material_expressions(smoothness, "", s_value, "B")
    # M3b: the blend layer (static switch Blend), the vertex colors (UseVertexColor), the detail map (static switch Detail).
    normal = sample("Normal", st.SAMPLERTYPE_NORMAL, unreal.load_asset("/Engine/EngineMaterials/FlatNormal"), 400)
    uv2 = multiply(material, (uv, ""), (scalar(material, "BlendLayer2Tiling", 1.0, -1400, 2600), ""), -1250, 2600)
    uvm = multiply(material, (uv, ""), (scalar(material, "BlendMaskTiling", 1.0, -1400, 2750), ""), -1250, 2750)

    def sample_at(parameter, kind, default, at, y):
        node_ = sampler(material, parameter, kind, default, -900, y)
        editing.connect_material_expressions(at, "", node_, "UVs")
        return node_

    diffuse2 = sample_at("Diffuse2", st.SAMPLERTYPE_COLOR, unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), uv2, 2600)
    normal2 = sample_at("Normal2", st.SAMPLERTYPE_NORMAL, unreal.load_asset("/Engine/EngineMaterials/FlatNormal"), uv2, 2850)
    gloss2 = sample_at("Gloss2", st.SAMPLERTYPE_ALPHA, white, uv2, 3100)
    spec2 = sample_at("Specular2", st.SAMPLERTYPE_MASKS, white_data(content, state), uv2, 3350)
    blend_mask = sample_at("BlendMask", st.SAMPLERTYPE_ALPHA, white, uvm, 3600)
    vertex = node(material, unreal.MaterialExpressionVertexColor, -900, 3850)
    blend_params = vector(material, "BlendParams", (0.5, 32.0, 0.0, 0.0), -900, 4000)
    blend_value = custom(material, BLEND_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("M", "VA", "P"), -600, 3600)
    editing.connect_material_expressions(blend_mask, "R", blend_value, "M")
    editing.connect_material_expressions(vertex, "A", blend_value, "VA")
    editing.connect_material_expressions(blend_params, "RGBA", blend_value, "P")
    tint2 = vector(material, "BlendLayer2Diffuse", (1.0, 1.0, 1.0, 1.0), -900, 4150)
    spec_tint2 = vector(material, "BlendLayer2Specular", (0.2314, 0.2314, 0.2314, 0.0), -900, 4300)
    smooth2 = scalar(material, "BlendLayer2Smoothness", 10.0 / 256.0, -900, 4450)   # BlendLayer2Smoothness / 256
    d2 = multiply(material, (diffuse2, "RGB"), (tint2, "RGB"), -600, 2600)
    sp2 = multiply(material, (spec2, "RGB"), (spec_tint2, "RGB"), -600, 3350)
    g2 = multiply(material, (gloss2, "R"), (smooth2, ""), -600, 3100)
    blended_d = lerp(material, (albedo, ""), (d2, ""), (blend_value, ""), -400, 2600)
    blended_s = lerp(material, (reflectance, ""), (sp2, ""), (blend_value, ""), -400, 3350)
    blended_g = lerp(material, (s_value, ""), (g2, ""), (blend_value, ""), -400, 3100)
    blended_n = lerp(material, (normal, "RGB"), (normal2, "RGB"), (blend_value, ""), -400, 2850)
    d_b = switch(material, "Blend", (blended_d, ""), (albedo, ""), -250, 2600)
    s_b = switch(material, "Blend", (blended_s, ""), (reflectance, ""), -250, 3350)
    g_b = switch(material, "Blend", (blended_g, ""), (s_value, ""), -250, 3100)
    n_b = switch(material, "Blend", (blended_n, ""), (normal, "RGB"), -250, 2850)
    one = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 3850)
    one.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    use_vertex = scalar(material, "UseVertexColor", 0.0, -400, 4000)
    vertex_tint = lerp(material, (one, ""), (vertex, "RGB"), (use_vertex, ""), -250, 3850)
    d_v = multiply(material, (d_b, ""), (vertex_tint, ""), -100, 2600)
    detail_transform = vector(material, "DetailTransform", (1.0, 1.0, 0.0, 0.0), -1600, 4600)
    detail_uv = node(material, unreal.MaterialExpressionAdd, -1100, 4600)
    detail_scaled = multiply(material, (coords, ""), (mask(material, detail_transform, "RGBA", "rg", -1400, 4600), ""), -1250, 4600)
    editing.connect_material_expressions(detail_scaled, "", detail_uv, "A")
    editing.connect_material_expressions(mask(material, detail_transform, "RGBA", "ba", -1400, 4750), "", detail_uv, "B")
    detail_map = sample_at("Detail", st.SAMPLERTYPE_MASKS, white_data(content, state), detail_uv, 4600)   # linear data
    detail_scales = vector(material, "DetailScales", (1.0, 1.0, 1.0, 0.0), -900, 4850)   # bump, diffuse, gloss, alpha mask
    detail_color = custom(material, DETAIL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                          ("D", "G", "Det", "Sc", "A", "AM"), -100, 4600)
    detail_normal = custom(material, DETAIL_NORMAL_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                           ("N", "Det", "Sc", "A", "AM"), -100, 4900)
    for node_, pins in ((detail_color, (("D", d_v, ""), ("G", g_b, ""))), (detail_normal, (("N", n_b, ""),))):
        for pin, source, out in pins:
            editing.connect_material_expressions(source, out, node_, pin)
        editing.connect_material_expressions(detail_map, "RGBA", node_, "Det")
        editing.connect_material_expressions(detail_scales, "RGB", node_, "Sc")
        editing.connect_material_expressions(color, "A", node_, "A")
        editing.connect_material_expressions(detail_scales, "A", node_, "AM")
    detail_d = mask(material, detail_color, "", "rgb", 50, 4600)
    detail_g = mask(material, detail_color, "", "a", 50, 4700)
    d_final = switch(material, "Detail", (detail_d, ""), (d_v, ""), 200, 2600)
    g_final = switch(material, "Detail", (detail_g, ""), (g_b, ""), 200, 3100)
    n_final = switch(material, "Detail", (detail_normal, ""), (n_b, ""), 200, 2850)
    # The overlay masks (M3 part 1): the diffuse color and the smoothness, then the specular color.
    mask_map = sample("MaskTex", st.SAMPLERTYPE_MASKS, black_data(content, state), 1300)
    mode = scalar(material, "MaskMode", 0.0, -1600, 1300)
    inputs = {"CR": vector(material, "MaskColorR", (0.0, 0.0, 0.0, 0.0), -1600, 1450),
              "CG": vector(material, "MaskColorG", (0.0, 0.0, 0.0, 0.0), -1600, 1600),
              "CB": vector(material, "MaskColorB", (0.0, 0.0, 0.0, 0.0), -1600, 1750)}
    packed = {"Amt": vector(material, "MaskAmount", (0.0, 0.0, 0.0, 0.0), -1600, 1900),
              "Ovr": vector(material, "MaskOverride", (0.0, 0.0, 0.0, 0.0), -1600, 2050),
              "Ext": vector(material, "MaskExtra", (0.0, 0.0, 0.0, 0.0), -1600, 2200)}
    masked_color = custom(material, MASK_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                          ("D", "G", "M", "Mode", "CR", "CG", "CB", "Amt", "Ovr", "Ext"), -600, 1300)
    editing.connect_material_expressions(d_final, "", masked_color, "D")
    editing.connect_material_expressions(g_final, "", masked_color, "G")
    editing.connect_material_expressions(mask_map, "RGBA", masked_color, "M")
    editing.connect_material_expressions(mode, "", masked_color, "Mode")
    for pin, source in inputs.items():
        editing.connect_material_expressions(source, "RGB", masked_color, pin)
    for pin, source in packed.items():
        editing.connect_material_expressions(source, "RGBA", masked_color, pin)
    spec_out = vector(material, "MaskSpecColor", (0.0, 0.0, 0.0, 0.0), -1600, 2350)
    masked_spec = custom(material, MASK_SPEC_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                         ("S", "SC", "M", "Mode", "Ovr"), -600, 1600)
    editing.connect_material_expressions(s_b, "", masked_spec, "S")
    editing.connect_material_expressions(spec_out, "RGB", masked_spec, "SC")
    editing.connect_material_expressions(mask_map, "RGBA", masked_spec, "M")
    editing.connect_material_expressions(mode, "", masked_spec, "Mode")
    editing.connect_material_expressions(packed["Ovr"], "RGBA", masked_spec, "Ovr")
    masked_albedo = mask(material, masked_color, "", "rgb", -450, 1300)
    masked_gloss = mask(material, masked_color, "", "a", -450, 1450)
    split = custom(material, REFLECTANCE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4, ("D", "S"), -350, -200)
    editing.connect_material_expressions(masked_albedo, "", split, "D")
    editing.connect_material_expressions(masked_spec, "", split, "S")
    base = mask(material, split, "", "rgb", -150, -250)
    metallic = mask(material, split, "", "a", -150, -100)
    editing.connect_material_property(base, "", MP.MP_BASE_COLOR)
    editing.connect_material_property(metallic, "", MP.MP_METALLIC)
    specular = custom(material, SPECULAR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("S",), -350, 50)
    editing.connect_material_expressions(masked_spec, "", specular, "S")
    editing.connect_material_property(specular, "", MP.MP_SPECULAR)
    editing.connect_material_property(n_final, "", MP.MP_NORMAL)
    rough = node(material, unreal.MaterialExpressionOneMinus, -300, 800)
    editing.connect_material_expressions(masked_gloss, "", rough, "")
    editing.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    # M4: the emission (static switch Emissive).
    # Version 7 (AST-207): each map has its own UV transform (the source _TCMMatrixEmittance and
    # _TCMMatrixEmissiveMultiplier); the converter gives the decoded RGB of an sRGB file (`@data-decoded`).
    def transformed(parameter, y):
        t = vector(material, parameter, (1.0, 1.0, 0.0, 0.0), -1600, y)
        scaled_ = multiply(material, (coords, ""), (mask(material, t, "RGBA", "rg", -1400, y), ""), -1250, y)
        at = node(material, unreal.MaterialExpressionAdd, -1100, y)
        editing.connect_material_expressions(scaled_, "", at, "A")
        editing.connect_material_expressions(mask(material, t, "RGBA", "ba", -1400, y + 100), "", at, "B")
        return at

    emittance = sample_at("Emittance", st.SAMPLERTYPE_MASKS, white_data(content, state),
                          transformed("EmittanceTransform", 5100), 5100)
    intensity = sample_at("EmissiveIntensity", st.SAMPLERTYPE_MASKS, white_data(content, state),
                          transformed("IntensityTransform", 5350), 5350)
    # Version 8: the sRGB files on Color samplers (EmittanceParams z, w = 1; the GPU decodes the RGB).
    white_color = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    emittance_srgb = sample_at("EmittanceSrgb", st.SAMPLERTYPE_COLOR, white_color, transformed("EmittanceTransform", 5100),
                               5200)
    intensity_srgb = sample_at("IntensitySrgb", st.SAMPLERTYPE_COLOR, white_color, transformed("IntensityTransform", 5350),
                               5450)
    emittance_color = vector(material, "EmittanceColor", (0.0, 0.0, 0.0, 0.0), -900, 5600)
    emittance_params = vector(material, "EmittanceParams", (1.0, 0.0, 0.0, 0.0), -900, 5750)
    scale = editing.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, -900, 5900)
    scale.set_editor_property("collection", collection(content, state))
    scale.set_editor_property("parameter_name", "EmissiveScale")
    emission = custom(material, EMISSIVE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      ("Map", "MapS", "I", "IS", "C", "P", "Scale"), -500, 5100)
    editing.connect_material_expressions(emittance, "RGBA", emission, "Map")
    editing.connect_material_expressions(emittance_srgb, "RGBA", emission, "MapS")
    editing.connect_material_expressions(intensity, "RGBA", emission, "I")
    editing.connect_material_expressions(intensity_srgb, "RGBA", emission, "IS")
    editing.connect_material_expressions(emittance_color, "RGBA", emission, "C")
    editing.connect_material_expressions(emittance_params, "RGBA", emission, "P")
    editing.connect_material_expressions(scale, "", emission, "Scale")
    black = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -500, 5450)
    black.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    emissive = switch(material, "Emissive", (emission, ""), (black, ""), -250, 5100)
    editing.connect_material_property(emissive, "", MP.MP_EMISSIVE_COLOR)
    if translucent:
        opacity = multiply(material, (color, "A"), (scalar(material, "Opacity", 1.0, -600, 5900), ""), -250, 5900)
        editing.connect_material_property(opacity, "", MP.MP_OPACITY)
        material.set_editor_property("translucency_lighting_mode",
                                     unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    elif masked:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        editing.connect_material_property(color, "A", MP.MP_OPACITY_MASK)
    material.set_editor_property("used_with_nanite", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["base:" + name] = PARENT_VERSIONS[name]
    return material


# The decal alpha of the source (clusterforward_commonzpass, %DECAL): saturate(pow(DiffuseColor.w x diffuse.a x the
# decal opacity map x DecalAlphaMult, DecalFalloff)). Unreal has one opacity for the color, the normal, and the roughness
# of a DBuffer decal: DecalDiffuseOpacity (the color alone) is a limit.
DECAL_ALPHA_HLSL = "return saturate(pow(max(A * O * Mult, 0.0), Falloff));"
# The angle fade of a projected decal (deferredshading.cfxb getDecalAttenuation): smoothstep(0.55, 1, dot(the normal of the
# receiving surface, the normal of the decal)); 0 for a surface more than about 57 degrees from it (the walls under a
# ground decal). The DBuffer has no normal yet: the face normal from the derivatives of the world position. The local
# X axis of our decals is the source decal normal (decals.decal_transforms: X = front); the absolute value: the fade
# does not depend on the sign of the axis (a limit: a face turned away from the decal, under an overhang, is not
# rejected). A = the alpha before the fade, P = the world position, X = the decal X axis in world space.
DECAL_ANGLE_HLSL = """float3 n = normalize(cross(ddy(P), ddx(P)));
float d = abs(dot(n, normalize(X)));
return A * smoothstep(0.55, 1.0, clamp(d, 0.55, 1.0));"""
# The view bias of a mesh decal (the road ribbons, the mesh decal companions): the vertex moves toward the camera along
# its view ray (no shift on the screen, only the depth), 0 within 20 m (a foot on the road is not painted), then 4 mm for
# each meter, at most 1 m: the Landscape LOD can rise above the ribbon far away. The source has its own decal depth
# remedy (PerFrame_DecalZFightingRemedy). A projected decal does not use a world position offset.
DECAL_BIAS_HLSL = """float3 d = C - P;
float l = length(d);
return d / max(l, 1.0) * clamp((l - 2000.0) * 0.004, 0.0, 100.0);"""


def decal_parent(content, state):
    """@return M_SwcDecal: the material of the projected decals and of the road meshes (mesh decals): the domain Deferred
    Decal, the blend mode Translucent (the DBuffer color, normal, and roughness). UV = TexCoord0 x UVTransform.rg +
    UVTransform.ba (the mesh UV of a road, the projection UV of a decal); base color = diffuse map x DiffuseColor;
    roughness 1 - Smoothness x gloss; opacity DECAL_ALPHA_HLSL."""
    name = "M_SwcDecal"
    material, white = parent_asset(name, content, state)
    if material is None:
        return assets.load_asset(f"{BASE}/{name}")
    st = unreal.MaterialSamplerType
    coords = node(material, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    transform = vector(material, "UVTransform", (1.0, 1.0, 0.0, 0.0), -1600, 200)
    tile = mask(material, transform, "RGBA", "rg", -1400, 150)
    offset = mask(material, transform, "RGBA", "ba", -1400, 300)
    scaled = node(material, unreal.MaterialExpressionMultiply, -1250, 50)
    editing.connect_material_expressions(coords, "", scaled, "A")
    editing.connect_material_expressions(tile, "", scaled, "B")
    uv = node(material, unreal.MaterialExpressionAdd, -1100, 100)
    editing.connect_material_expressions(scaled, "", uv, "A")
    editing.connect_material_expressions(offset, "", uv, "B")

    def sample(parameter, kind, default, y):
        node_ = sampler(material, parameter, kind, default, -900, y)
        editing.connect_material_expressions(uv, "", node_, "UVs")
        return node_

    color = sample("BaseColor", st.SAMPLERTYPE_COLOR, unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), -400)
    diffuse = vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -900, -150)
    albedo = node(material, unreal.MaterialExpressionMultiply, -600, -300)
    editing.connect_material_expressions(color, "RGB", albedo, "A")
    editing.connect_material_expressions(diffuse, "RGB", albedo, "B")
    # The color and the opacity of the decal entity (DecalConfiguration Color, Opacity: g_DecalDiffuse of the source;
    # USwcSurfaceLayer sets them with SetDecalColor; white for the mesh decals).
    entity = node(material, unreal.MaterialExpressionDecalColor, -900, -550)
    tinted = multiply(material, (albedo, ""), (entity, "RGB"), -450, -350)
    editing.connect_material_property(tinted, "", MP.MP_BASE_COLOR)
    normal = sample("Normal", st.SAMPLERTYPE_NORMAL, unreal.load_asset("/Engine/EngineMaterials/FlatNormal"), 200)
    # A projected decal: Unreal turns the tangent normal (x, y, z) into -z X + y Y + x Z of the decal axes
    # (MaterialTemplate.ush TransformTangentNormalToWorld, DECAL_PRIMITIVE: its decals project along +X). Our X is the
    # source decal normal, out of the surface (decals.decal_transforms), thus z changes sign: the flat normal points
    # out, x stays along U (Z) and y along V (Y), as on a mesh. Version 5 had the normals into the surface: no sun on
    # the decal (the black seaweed of the ocean beach). The road ribbons and the mesh decals use their mesh tangents.
    flip_z = node(material, unreal.MaterialExpressionConstant3Vector, -450, 300)
    flip_z.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, -1.0, 1.0))
    flipped = multiply(material, (normal, "RGB"), (flip_z, ""), -300, 250)
    projected_normal = switch(material, "Projected", (flipped, ""), (normal, "RGB"), -150, 200)
    editing.connect_material_property(projected_normal, "", MP.MP_NORMAL)
    gloss = sample("Gloss", st.SAMPLERTYPE_ALPHA, white, 500)
    smoothness = scalar(material, "Smoothness", 0.5, -900, 750)
    s_value = node(material, unreal.MaterialExpressionMultiply, -500, 550)
    editing.connect_material_expressions(gloss, "R", s_value, "A")
    editing.connect_material_expressions(smoothness, "", s_value, "B")
    rough = node(material, unreal.MaterialExpressionOneMinus, -300, 550)
    editing.connect_material_expressions(s_value, "", rough, "")
    editing.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    # The reflectance of the source decal: the zpass encodes all the G-buffer channels of the decal, the reflectance too,
    # and blends them with the decal alpha (commonzpass EncodeGBuffer, SetGBufferBlending). S = the specular map x XML
    # Specular, Specular = lum(S) / 0.08 (SPECULAR_HLSL; Metallic stays 0: a limit for a decal above 0.08). Version 6 and
    # before wrote the Unreal default 0.5 (a reflectance of 0.04; the road decals have 0.01 to 0.02): at a grazing view
    # the sun specular made the roads and the cobbles pale and gray.
    spec_map = sample("Specular", st.SAMPLERTYPE_COLOR, unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), 650)
    spec_color = vector(material, "SpecularColor", (0.04, 0.04, 0.04, 1.0), -900, 650)
    reflectance = multiply(material, (spec_map, "RGB"), (spec_color, "RGB"), -600, 650)
    specular = custom(material, SPECULAR_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("S",), -350, 650)
    editing.connect_material_expressions(reflectance, "", specular, "S")
    editing.connect_material_property(specular, "", MP.MP_SPECULAR)
    opacity_map = sample("DecalOpacity", st.SAMPLERTYPE_ALPHA, white, 900)
    texture_alpha = node(material, unreal.MaterialExpressionMultiply, -750, 950)
    editing.connect_material_expressions(color, "A", texture_alpha, "A")
    editing.connect_material_expressions(diffuse, "A", texture_alpha, "B")
    # A mesh decal with %VERTCOLORS (the companions of mesh rule 7): x the vertex alpha (static switch UseVertexAlpha;
    # off for the projected decals and the roads).
    vertex = node(material, unreal.MaterialExpressionVertexColor, -900, 1450)
    with_vertex = multiply(material, (texture_alpha, ""), (vertex, "A"), -600, 1100)
    vertex_alpha = switch(material, "UseVertexAlpha", (with_vertex, ""), (texture_alpha, ""), -500, 950)
    entity_alpha = multiply(material, (vertex_alpha, ""), (entity, "A"), -420, 1000)
    # A projected decal (static switch Projected; the dp_ instances): the angle fade of the source deferred decal
    # (DECAL_ANGLE_HLSL). The road ribbons and the mesh decals have no projection axis: off.
    position_ws = node(material, unreal.MaterialExpressionWorldPosition, -1300, 1500)
    axis = node(material, unreal.MaterialExpressionConstant3Vector, -1500, 1700)
    axis.set_editor_property("constant", unreal.LinearColor(1.0, 0.0, 0.0, 1.0))
    axis_ws = node(material, unreal.MaterialExpressionTransform, -1300, 1700)
    axis_ws.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    axis_ws.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    editing.connect_material_expressions(axis, "", axis_ws, "")
    angle = custom(material, DECAL_ANGLE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("A", "P", "X"), -1000, 1500)
    editing.connect_material_expressions(entity_alpha, "", angle, "A")
    editing.connect_material_expressions(position_ws, "", angle, "P")
    editing.connect_material_expressions(axis_ws, "", angle, "X")
    alpha = switch(material, "Projected", (angle, ""), (entity_alpha, ""), -800, 1100)
    mult = scalar(material, "DecalAlphaMult", 1.0, -900, 1150)
    falloff = scalar(material, "DecalFalloff", 1.0, -900, 1300)
    opacity = custom(material, DECAL_ALPHA_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                     ("A", "O", "Mult", "Falloff"), -350, 1000)
    editing.connect_material_expressions(alpha, "", opacity, "A")
    editing.connect_material_expressions(opacity_map, "R", opacity, "O")
    editing.connect_material_expressions(mult, "", opacity, "Mult")
    editing.connect_material_expressions(falloff, "", opacity, "Falloff")
    editing.connect_material_property(opacity, "", MP.MP_OPACITY)
    # The view bias in the vertex shader: CameraVectorWS is a pixel input (version 3 and 4 did not compile).
    camera = node(material, unreal.MaterialExpressionCameraPositionWS, -900, 1700)
    position = node(material, unreal.MaterialExpressionWorldPosition, -900, 1800)
    offset = custom(material, DECAL_BIAS_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ("C", "P"), -500, 1750)
    editing.connect_material_expressions(camera, "", offset, "C")
    editing.connect_material_expressions(position, "", offset, "P")
    editing.connect_material_property(offset, "", MP.MP_WORLD_POSITION_OFFSET)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # The mesh decal companions of the cells are instanced components: with no flag the engine drew its default material
    # (a flat gray band on the moldings and the tree trunks, AST-203).
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["base:" + name] = PARENT_VERSIONS[name]
    return material


# M4: the Glass shader of the source (glass.cfxb, frag_custom_begin and frag_custom_end, the path with no multi-layer
# alpha blend). Tint = saturate(TintColor x the tint color map); with the tint mask (%TINT_MAP, read only with
# %SPECULAR_MAP): cloudiness = saturate(TintCloudiness x K.r x K.g), tint = saturate(tint + 1 - K.r). The source color:
# lerp(lerp(lerp(background x tint, lit tint, cloudiness), lit dirt, dirt.a), the reflection, Fresnel). Unreal (Thin
# Translucent): the background x TransmittanceColor x (1 - Opacity) + the lit BaseColor x Opacity, and the specular
# reflection. Thus Opacity = cloudiness + a - cloudiness x a, BaseColor = (tint x cloudiness x (1 - a) + dirt x a) /
# Opacity, TransmittanceColor = tint. F = (%TINT_MAP and %SPECULAR_MAP, %SPECULAR_MAP, %DIRT_MAP, unused).
GLASS_TINT_HLSL = """float3 t = saturate(TC.rgb * TM.rgb);
float c = Cloud;
if (F.x > 0.5) {
  c = saturate(Cloud * K.r * K.g);
  t = saturate(t + (1.0 - K.r));
}
return float4(t, c);"""
GLASS_COVER_HLSL = """float a = F.z > 0.5 ? Dirt.a : 0.0;
float o = T.a + a - T.a * a;
float3 b = (T.rgb * T.a * (1.0 - a) + Dirt.rgb * DC.rgb * a) / max(o, 1e-4);
return float4(b, o);"""
# The F0 of the source glass: XML Specular; with %DIRT_MAP lerp(F0, 0.03, dirt.a); then %SPECULAR_MAP replaces it
# (K.b with %TINT_MAP, else K.rgb). Unreal: Specular = lum(F0) / 0.08.
GLASS_SPEC_HLSL = """float3 s = S;
if (F.z > 0.5) s = lerp(s, float3(0.03, 0.03, 0.03), Dirt.a);
if (F.y > 0.5) s = F.x > 0.5 ? K.bbb : K.rgb;
return saturate(dot(s, float3(0.2126, 0.7152, 0.0722)) / 0.08);"""
# The gloss: Smoothness x the gloss map, x saturate(1 - K.g x CloudinessReducesGloss) with %TINT_MAP.
GLASS_ROUGH_HLSL = """float g = Sm * G;
if (F.x > 0.5) g *= saturate(1.0 - K.g * R);
return 1.0 - g;"""


def glass_parent(content, state):
    """@return M_SwcGlass: the parent of the Glass shader (stage M4): Translucent, the shading model Thin Translucent
    (lighting mode Surface ForwardShading), the GLASS_*_HLSL formulas. The normal map uses TexCoord0 x BumpMapTile.
    Limits: no refraction offset (BumpScale) and no back light (BackLightScale); the Unreal reflections."""
    name = "M_SwcGlass"
    material, white = parent_asset(name, content, state)
    if material is None:
        return assets.load_asset(f"{BASE}/{name}")
    st = unreal.MaterialSamplerType
    white_color = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    flags = vector(material, "GlassFlags", (0.0, 0.0, 0.0, 0.0), -1300, 1200)
    tint_color = vector(material, "TintColor", (1.0, 1.0, 1.0, 1.0), -1300, -400)
    tint_map = sampler(material, "TintColorMap", st.SAMPLERTYPE_COLOR, white_color, -1300, -200)
    tint_mask = sampler(material, "TintMask", st.SAMPLERTYPE_COLOR, white_color, -1300, 100)
    cloud = scalar(material, "TintCloudiness", 0.0, -1300, 350)
    tint = custom(material, GLASS_TINT_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                  ("TC", "TM", "K", "Cloud", "F"), -900, -200)
    editing.connect_material_expressions(tint_color, "RGBA", tint, "TC")
    editing.connect_material_expressions(tint_map, "RGBA", tint, "TM")
    editing.connect_material_expressions(tint_mask, "RGBA", tint, "K")
    editing.connect_material_expressions(cloud, "", tint, "Cloud")
    editing.connect_material_expressions(flags, "RGBA", tint, "F")
    dirt = sampler(material, "Dirt", st.SAMPLERTYPE_COLOR, white_color, -1300, 550)
    diffuse = vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -1300, 800)
    cover = custom(material, GLASS_COVER_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                   ("T", "Dirt", "DC", "F"), -600, 0)
    editing.connect_material_expressions(tint, "", cover, "T")
    editing.connect_material_expressions(dirt, "RGBA", cover, "Dirt")
    editing.connect_material_expressions(diffuse, "RGB", cover, "DC")
    editing.connect_material_expressions(flags, "RGBA", cover, "F")
    base = mask(material, cover, "", "rgb", -350, -50)
    editing.connect_material_property(base, "", MP.MP_BASE_COLOR)
    opacity = mask(material, cover, "", "a", -350, 100)
    editing.connect_material_property(opacity, "", MP.MP_OPACITY)
    transmittance = mask(material, tint, "", "rgb", -600, -350)
    # ThinTranslucentCommon.ush: the background x (1 - F)^2 x TransmittanceColor^(1/NoV) x (1 - Opacity) + the lit
    # BaseColor x Opacity + the specular, all x SurfaceCoverage (1: the whole pane).
    output = node(material, unreal.MaterialExpressionThinTranslucentMaterialOutput, -100, -350)
    editing.connect_material_expressions(transmittance, "", output, "TransmittanceColor")
    coverage = node(material, unreal.MaterialExpressionConstant, -350, -250)
    coverage.set_editor_property("r", 1.0)
    editing.connect_material_expressions(coverage, "", output, "SurfaceCoverage")
    spec_color = vector(material, "SpecularColor", (0.04, 0.04, 0.04, 1.0), -1300, 1000)
    specular = custom(material, GLASS_SPEC_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                      ("S", "K", "Dirt", "F"), -600, 400)
    editing.connect_material_expressions(spec_color, "RGB", specular, "S")
    editing.connect_material_expressions(tint_mask, "RGBA", specular, "K")
    editing.connect_material_expressions(dirt, "RGBA", specular, "Dirt")
    editing.connect_material_expressions(flags, "RGBA", specular, "F")
    editing.connect_material_property(specular, "", MP.MP_SPECULAR)
    gloss = sampler(material, "Gloss", st.SAMPLERTYPE_ALPHA, white, -1300, 1400)
    smoothness = scalar(material, "Smoothness", 1.0, -1300, 1650)
    reduce = scalar(material, "CloudinessReducesGloss", 0.0, -1300, 1800)
    rough = custom(material, GLASS_ROUGH_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                   ("Sm", "G", "K", "R", "F"), -600, 700)
    editing.connect_material_expressions(smoothness, "", rough, "Sm")
    editing.connect_material_expressions(gloss, "R", rough, "G")
    editing.connect_material_expressions(tint_mask, "RGBA", rough, "K")
    editing.connect_material_expressions(reduce, "", rough, "R")
    editing.connect_material_expressions(flags, "RGBA", rough, "F")
    editing.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    coords = node(material, unreal.MaterialExpressionTextureCoordinate, -1700, 2000)
    bump_tile = scalar(material, "BumpMapTile", 1.0, -1700, 2150)
    bump_uv = multiply(material, (coords, ""), (bump_tile, ""), -1500, 2050)
    normal = sampler(material, "Normal", st.SAMPLERTYPE_NORMAL, unreal.load_asset("/Engine/EngineMaterials/FlatNormal"),
                     -1300, 2050)
    editing.connect_material_expressions(bump_uv, "", normal, "UVs")
    editing.connect_material_property(normal, "RGB", MP.MP_NORMAL)
    # The lighting mode first: Thin Translucent needs Surface ForwardShading (a compile between the two fails).
    material.set_editor_property("translucency_lighting_mode",
                                 unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_THIN_TRANSLUCENT)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["base:" + name] = PARENT_VERSIONS[name]
    return material


# M_SwcBeam (AST-207): the GeometryBeam shader of the source (geometrybeam.cfxb BeamVS and BeamPS; research
# emission-fogcards.md). The vertex part (BeamVS): V = the vertex RGBA x the XML Diffuse and Opacity (DC), W = the direction
# to the camera, N = the world normal; q = saturate(dot(W, N)^2 x viewDependencyFactor)^2; B = FinalMultiplier x V x q;
# B.rgb x= lerp(StartColor, EndColor, u) with u = saturate(U), or V with %VERTICAL_GRADIENT (F1.w); %GRADIENT_ALPHA (F2.y):
# B.a x= 1 - u. The vertex colors: the source bytes / 255 (mesh.vertex_colors).
BEAM_VS_HLSL = """float3 w = normalize(W);
float d = dot(w, normalize(N));
float q = saturate(d * d * VD);
q *= q;
float4 B = FM * float4(VC, VA) * DC * q;
float u = saturate(F1.w > 0.5 ? UV.y : UV.x);
B.rgb *= lerp(SC.rgb, EC.rgb, u);
if (F2.y > 0.5) B.a *= 1.0 - u;
return B;"""
# The coordinate of the 3D noise (BeamVS): the object position in source metres (Unreal local cm, Y mirrored) x
# (0.05, 0.05, 0.1) + the time x BackgroudAlphaNoiseSpeedXYZ (S).
BEAM_NOISE_VS_HLSL = """float3 p = LP * float3(0.01, -0.01, 0.01);
return p * float3(0.05, 0.05, 0.1) + T * S.xyz;"""
# The Noise3D sample of the source (a 64^3 A8 volume, a wrapping linear sampler, its alpha): eight Loads of the tiles of
# T_SwcNoise3D (material_records.noise_picture) and the trilinear weights. f = 2 x noise - 0.25 (not saturated).
BEAM_NOISE_HLSL = """float3 p = NR * 64.0 - 0.5;
float3 i = floor(p);
float3 t = p - i;
float r = 0.0;
for (int k = 0; k < 8; k++) {
  int3 o = int3(k & 1, (k >> 1) & 1, k >> 2);
  int3 c = (int3(i) + o) & 63;
  float v = Tex.Load(int3((c.z & 7) * 64 + c.x, (c.z >> 3) * 64 + c.y, 0)).r;
  float3 g = lerp(1.0 - t, t, float3(o));
  r += v * g.x * g.y * g.z;
}
return 2.0 * r - 0.25;"""
# The shared UV of BeamPS (version 5, research beach-sheet-inputs.md "Shared UV order"): UV0 turned by uvRot turns (R), then
# the stretch oscillators of the TexMod (type 5, about UV zero, not about 0.5; O = rate U, rate V, amplitude U, amplitude V:
# 1 + amplitude x sin(2 pi rate t)), then the static tiling and offset of the TexMod (UVT). The base map samples it (x
# baseUVScale 1); the dust map samples the same UV (below). The vertical color gradient keeps the original UV0.y (BEAM_VS_HLSL).
BEAM_BASE_UV_HLSL = """float a = R * 6.2831853;
float2 r = float2(UV.x * cos(a) - UV.y * sin(a), UV.y * cos(a) + UV.x * sin(a));
float2 s = 1.0 + O.zw * sin(6.2831853 * O.xy * T);
return r * s * UVT.xy + UVT.zw;"""
# The dust UV (BeamPS): the shared UV x dustUVScale + the noise coordinate xy x dustTimeScale (its scroll; no second TexMod).
BEAM_DUST_UV_HLSL = "return U * DS + NR.xy * DT;"
# The turbulence UV: ((0.17 f, f) x 0.5 + 0.5) x turbStrength.
BEAM_TURB_UV_HLSL = "return (float2(0.17 * f, f) * 0.5 + 0.5) * TS;"
# The color and the alpha (BeamPS). F: the depth fade in source metres: min(saturate(s x min(zs - z, h, z)),
# saturate(s x (z - zn))); zs the scene depth, z the depth of the sheet, h the height above the water plane (WL m), zn the
# near clip. The sun: x lerp(1, the source sun color, SunColorInfluence) (%APPLY_SUN_COLOR, F1.y); the fog: x lerp(1,
# the fog color / max(lum, 1e-4), FogColorInfluence) (%APPLY_FOG_COLOR, F2.z). With %NOISE (F1.x): the dust lerp(D, T x
# 0.5 (T.r T.g + T.b), turbRatio) and the noise f; else 1. RGB = base.rgb x color x dust x F x (1 + AmbStrength) x f
# (no %RECEIVE_SHADOWS: the shadow term is 1). The alpha: min(base.r, base.a) x the UV vignette (%UV_VIGNETTING, F2.x:
# exp2(-uvVigFeaturing x length(frac(UV x baseUVScale) x 2 - 1)^2)) x (1 - exp2(-k B.a^2)), k = fadingFeaturing, x
# saturate((1 - the relative view distance) / FadeOutDistance) with %ENABLE_FADEOUT (F1.z; the relative distance: the
# camera distance / the maximum view distance of the object, min(MD, VDR x the bounding radius OR): the CryEngine rule of
# e_ViewDistRatio, default 60, an inference; MD 8,000 m is the far clip of the menu camera). The blend of the source is SRC_ALPHA, ONE: the output is RGB x alpha (premultiplied, the
# Additive parent has opacity 1), in source units (x Scale: EmissiveScale, cd/m2 x swc.LightScale).
BEAM_PS_HLSL = """float3 lw = float3(0.2126, 0.7152, 0.0722);
float z = PD * 0.01;
float zs = SD * 0.01;
float h = WZ * 0.01 - WL;
float F = min(saturate(SI * min(min(zs - z, h), z)), saturate(SI * (z - ZN)));
float3 color = B.rgb;
if (F1.y > 0.5) color *= lerp(float3(1.0, 1.0, 1.0), Sun.rgb, SCI);
if (F2.z > 0.5) color *= lerp(float3(1.0, 1.0, 1.0), Fog.rgb / max(dot(Fog.rgb, lw), 0.0001), FCI);
float3 dust = float3(1.0, 1.0, 1.0);
float n = 1.0;
if (F1.x > 0.5) {
  float3 t = Turb.rgb;
  t *= 0.5 * (t.r * t.g + t.b);
  dust = lerp(Dust.rgb, t, TR);
  n = f;
}
float3 rgb = Base.rgb * color * dust * F * (1.0 + AMB) * n;
float a = min(Base.r, Base.a);
if (F2.x > 0.5) {
  float2 v = frac(UV * BUS) * 2.0 - 1.0;
  a *= exp2(-VIG * dot(v, v));
}
float k = FF;
if (F1.z > 0.5) k *= saturate((1.0 - CD / max(min(MD * 100.0, VDR * OR), 1.0)) / max(FOD, 0.0001));
a *= 1.0 - exp2(-k * B.a * B.a);
a = saturate(a);
return float4(max(rgb, 0.0) * a * Scale * BS, a);"""


def effects_collection(content, state):
    """@return MPC_SwcEffects: the global values of M_SwcBeam, set at run time by USwcMaterialScale: SunColorSource (the
    sun color of the source in its units: the color x the intensity / pi of the main directional light / 10,000) and
    FogColorSource (the inscattering color of the height fog). Its own collection: MPC_SwcMaterials stays (a new parameter
    of a collection compiles all its materials again)."""
    name = "MPC_SwcEffects"
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path) and state.get("base:" + name) == "2":
        return assets.load_asset(path)
    mpc = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        name, BASE, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    vectors = list(mpc.get_editor_property("vector_parameters"))
    for parameter, default in (("SunColorSource", (1.0, 1.0, 1.0, 0.0)), ("FogColorSource", (1.0, 1.0, 1.0, 0.0))):
        if not any(str(v.get_editor_property("parameter_name")) == parameter for v in vectors):
            value = unreal.CollectionVectorParameter()
            value.set_editor_property("parameter_name", parameter)
            value.set_editor_property("default_value", unreal.LinearColor(*default))
            vectors.append(value)
    mpc.set_editor_property("vector_parameters", vectors)
    # BeamScale (version 2): the scale of M_SwcBeam, set by USwcMaterialScale from swc.BeamScale; the asset default 0 keeps the
    # beams out of the runs of a client without that code.
    scalars = list(mpc.get_editor_property("scalar_parameters"))
    if not any(str(v.get_editor_property("parameter_name")) == "BeamScale" for v in scalars):
        value = unreal.CollectionScalarParameter()
        value.set_editor_property("parameter_name", "BeamScale")
        value.set_editor_property("default_value", 0.0)
        scalars.append(value)
        mpc.set_editor_property("scalar_parameters", scalars)
    assets.save_loaded_asset(mpc)
    state["base:" + name] = "2"
    return mpc


def noise_texture(content, state):
    """@return T_SwcNoise3D: the tiles of the source noise volume (material_records.noise_picture), gray, no mips, no
    streaming, no compression (BEAM_NOISE_HLSL reads mip 0 with Load)."""
    name = "T_SwcNoise3D"
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path) and state.get("base:" + name) == "1":
        return assets.load_asset(path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(content, "materials", "base", name + ".png"))
    task.set_editor_property("destination_path", BASE)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(path)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    assets.save_loaded_asset(texture)
    state["base:" + name] = "1"
    return texture


def beam_parent(content, state):
    """@return M_SwcBeam: the parent of the GeometryBeam shader (AST-207): Additive, Unlit, two sided, the depth test
    on, no depth write (translucent), the fog of the scene on the result; BEAM_VS_HLSL and the noise coordinate in the
    vertex shader (vertex interpolators), BEAM_PS_HLSL in the pixel shader. Not a fog volume and no surface light."""
    name = "M_SwcBeam"
    material, _white = parent_asset(name, content, state)
    if material is None:
        return assets.load_asset(f"{BASE}/{name}")
    st = unreal.MaterialSamplerType
    white_color = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    cf = unreal.CustomMaterialOutputType
    coords = node(material, unreal.MaterialExpressionTextureCoordinate, -2400, 0)
    # The vertex part.
    vertex = node(material, unreal.MaterialExpressionVertexColor, -2400, -600)
    normal = node(material, unreal.MaterialExpressionVertexNormalWS, -2400, -450)
    camera = node(material, unreal.MaterialExpressionCameraPositionWS, -2600, -350)
    world = node(material, unreal.MaterialExpressionWorldPosition, -2600, -250)
    toward = node(material, unreal.MaterialExpressionSubtract, -2400, -300)
    editing.connect_material_expressions(camera, "", toward, "A")
    editing.connect_material_expressions(world, "", toward, "B")
    diffuse = vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -2400, -150)
    start = vector(material, "StartColor", (1.0, 1.0, 1.0, 1.0), -2400, 0)
    end = vector(material, "EndColor", (1.0, 1.0, 1.0, 1.0), -2400, 150)
    flags = vector(material, "BeamFlags", (0.0, 0.0, 0.0, 0.0), -2400, 300)
    flags2 = vector(material, "BeamFlags2", (0.0, 0.0, 0.0, 0.0), -2400, 450)
    final = scalar(material, "FinalMultiplier", 1.0, -2400, 600)
    view_dep = scalar(material, "viewDependencyFactor", 2.0, -2400, 700)
    beam_vs = custom(material, BEAM_VS_HLSL, cf.CMOT_FLOAT4,
                     ("VC", "VA", "N", "W", "UV", "DC", "SC", "EC", "F1", "F2", "FM", "VD"), -2000, -300)
    for pin, (source, out) in {"VC": (vertex, ""), "VA": (vertex, "A"), "N": (normal, ""), "W": (toward, ""), "UV": (coords, ""),
                               "DC": (diffuse, "RGBA"), "SC": (start, "RGBA"), "EC": (end, "RGBA"),
                               "F1": (flags, "RGBA"), "F2": (flags2, "RGBA"), "FM": (final, ""),
                               "VD": (view_dep, "")}.items():
        editing.connect_material_expressions(source, out, beam_vs, pin)
    b = node(material, unreal.MaterialExpressionVertexInterpolator, -1800, -300)
    editing.connect_material_expressions(beam_vs, "", b, "VS")
    local = node(material, unreal.MaterialExpressionPreSkinnedPosition, -2400, 850)
    time = node(material, unreal.MaterialExpressionTime, -2400, 950)
    speed = vector(material, "NoiseSpeed", (0.1, 0.05, 0.15, 0.0), -2400, 1050)
    noise_vs = custom(material, BEAM_NOISE_VS_HLSL, cf.CMOT_FLOAT3, ("LP", "T", "S"), -2000, 900)
    editing.connect_material_expressions(local, "", noise_vs, "LP")
    editing.connect_material_expressions(time, "", noise_vs, "T")
    editing.connect_material_expressions(speed, "RGB", noise_vs, "S")
    nr = node(material, unreal.MaterialExpressionVertexInterpolator, -1800, 900)
    editing.connect_material_expressions(noise_vs, "", nr, "VS")
    # The pixel part: the noise, the three UVs, the three samples.
    noise_object = node(material, unreal.MaterialExpressionTextureObject, -1800, 1100)
    noise_object.set_editor_property("texture", noise_texture(content, state))
    f = custom(material, BEAM_NOISE_HLSL, cf.CMOT_FLOAT1, ("Tex", "NR"), -1500, 1000)
    editing.connect_material_expressions(noise_object, "", f, "Tex")
    editing.connect_material_expressions(nr, "", f, "NR")
    uvt = vector(material, "UVTransform", (1.0, 1.0, 0.0, 0.0), -1800, 1250)
    osc = vector(material, "BaseOscillator", (0.0, 0.0, 0.0, 0.0), -1800, 1400)
    rot = scalar(material, "uvRot", 0.0, -1800, 1550)
    base_uv = custom(material, BEAM_BASE_UV_HLSL, cf.CMOT_FLOAT2, ("UV", "R", "UVT", "O", "T"), -1500, 1250)
    editing.connect_material_expressions(coords, "", base_uv, "UV")
    editing.connect_material_expressions(rot, "", base_uv, "R")
    editing.connect_material_expressions(uvt, "RGBA", base_uv, "UVT")
    editing.connect_material_expressions(osc, "RGBA", base_uv, "O")
    editing.connect_material_expressions(time, "", base_uv, "T")
    dust_scale = scalar(material, "dustUVScale", 0.6, -1800, 1650)
    dust_time = scalar(material, "dustTimeScale", 1.0, -1800, 1750)
    dust_uv = custom(material, BEAM_DUST_UV_HLSL, cf.CMOT_FLOAT2, ("U", "DS", "DT", "NR"), -1500, 1550)
    for pin, source in {"U": base_uv, "DS": dust_scale, "DT": dust_time, "NR": nr}.items():
        editing.connect_material_expressions(source, "", dust_uv, pin)
    turb_strength = scalar(material, "turbStrength", 1.0, -1800, 1850)
    turb_uv = custom(material, BEAM_TURB_UV_HLSL, cf.CMOT_FLOAT2, ("f", "TS"), -1500, 1850)
    editing.connect_material_expressions(f, "", turb_uv, "f")
    editing.connect_material_expressions(turb_strength, "", turb_uv, "TS")
    base = sampler(material, "BaseColor", st.SAMPLERTYPE_COLOR, white_color, -1200, 1250)
    editing.connect_material_expressions(base_uv, "", base, "UVs")
    dust = sampler(material, "Dust", st.SAMPLERTYPE_COLOR, white_color, -1200, 1550)
    editing.connect_material_expressions(dust_uv, "", dust, "UVs")
    # The turbulence color: an empty Bumpmap slot samples engineassets/textures/white_ddn.dds raw (signed BC5, about
    # (0.0045, 0.0045, 0); research beach-sheet-inputs.md): black (version 5; version 4 had the flat normal (0.5, 0.5, 1),
    # which made the dust term 10 to 13 times too strong at a near beach sheet).
    turb = sampler(material, "Turbulence", st.SAMPLERTYPE_MASKS, black_data(content, state), -1200, 1850)
    editing.connect_material_expressions(turb_uv, "", turb, "UVs")
    # The depth inputs and the globals.
    pixel_depth = node(material, unreal.MaterialExpressionPixelDepth, -1200, 2100)
    scene_depth = node(material, unreal.MaterialExpressionSceneDepth, -1200, 2200)
    world_z = mask(material, world, "", "b", -1200, 2300)
    camera_distance = node(material, unreal.MaterialExpressionDistance, -1200, 2400)
    editing.connect_material_expressions(camera, "", camera_distance, "A")
    object_position = node(material, unreal.MaterialExpressionObjectPositionWS, -1400, 2450)
    editing.connect_material_expressions(object_position, "", camera_distance, "B")
    effects = effects_collection(content, state)
    sun = node(material, unreal.MaterialExpressionCollectionParameter, -1200, 2550)
    sun.set_editor_property("collection", effects)
    sun.set_editor_property("parameter_name", "SunColorSource")
    fog = node(material, unreal.MaterialExpressionCollectionParameter, -1200, 2650)
    fog.set_editor_property("collection", effects)
    fog.set_editor_property("parameter_name", "FogColorSource")
    scale = node(material, unreal.MaterialExpressionCollectionParameter, -1200, 2750)
    scale.set_editor_property("collection", collection(content, state))
    scale.set_editor_property("parameter_name", "EmissiveScale")
    beam_scale = node(material, unreal.MaterialExpressionCollectionParameter, -1200, 2850)
    beam_scale.set_editor_property("collection", effects)
    beam_scale.set_editor_property("parameter_name", "BeamScale")
    pins = {"BS": (beam_scale, ""), "B": (b, ""), "Base": (base, "RGBA"), "Dust": (dust, "RGBA"), "Turb": (turb, "RGBA"), "f": (f, ""),
            "UV": (coords, ""), "PD": (pixel_depth, ""), "SD": (scene_depth, ""), "WZ": (world_z, ""),
            "CD": (camera_distance, ""), "OR": (node(material, unreal.MaterialExpressionObjectRadius, -1400, 2350), ""),
            "Sun": (sun, ""), "Fog": (fog, ""), "Scale": (scale, ""),
            "F1": (flags, "RGBA"), "F2": (flags2, "RGBA")}
    for i, (pin, parameter, default) in enumerate((
            ("SI", "SoftIntersectionFactor", 1.0), ("WL", "WaterLevel", 40.0), ("ZN", "NearClip", 0.1),
            ("SCI", "SunColorInfluence", 0.5), ("FCI", "FogColorInfluence", 0.0), ("TR", "turbRatio", 0.55),
            ("AMB", "AmbStrength", 0.12), ("BUS", "baseUVScale", 1.0), ("VIG", "uvVigFeaturing", 0.0),
            ("FF", "fadingFeaturing", 0.55), ("FOD", "FadeOutDistance", 0.2), ("MD", "MaxViewDistance", 8000.0),
            ("VDR", "ViewDistRatio", 60.0))):
        pins[pin] = (scalar(material, parameter, default, -1600, 2100 + 100 * i), "")
    color = custom(material, BEAM_PS_HLSL, cf.CMOT_FLOAT4, tuple(pins), -700, 1500)
    for pin, (source, out) in pins.items():
        editing.connect_material_expressions(source, out, color, pin)
    emissive = mask(material, color, "", "rgb", -400, 1500)
    editing.connect_material_property(emissive, "", MP.MP_EMISSIVE_COLOR)
    one = node(material, unreal.MaterialExpressionConstant, -400, 1650)
    one.set_editor_property("r", 1.0)
    editing.connect_material_property(one, "", MP.MP_OPACITY)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["base:" + name] = PARENT_VERSIONS[name]
    return material


# M6: the wind of the source (modificatorvt.cfib: _DetailBending, SimpleWindNoise, _MainBending), in the engine space of the
# source (meters; Unreal (x, -y, z) x 100). LP, LN: the local position (cm) and normal of the vertex; VC: the vertex
# color (the bend data of the source: R the leaf edge, G the branch phase, B 1 - the branch amplitude); OP: the instance
# origin (cm, world); WL: the global wind in local axes (its length is the strength: PerFrame_WindDirection.w); Bend:
# the bend of the instance (CustomPrimitiveData[0], SwcWind: 0 = no motion); P: bendDetailFrequency,
# bendDetailLeafAmplitude, bendDetailBranchAmplitude; H: the height of the mesh (cm, the local bounds). Output: the
# local offset (cm). The main bending: xy += the wind x the bend x (z / height)^2, the length kept (the source shader;
# the CPU rule of the source for the bend vector is not in the shader text: the wind x the bend, and 1 / height).
WIND_HLSL = """if (Bend <= 0.0) return float3(0.0, 0.0, 0.0);
float3 S = float3(0.01, -0.01, 0.01);
float3 p = LP * S;
float3 n = LN * float3(1.0, -1.0, 1.0);
float3 wp = OP * S;
float3 w = WL * float3(1.0, -1.0, 1.0);
float4 q = wp.xyxy * float4(0.3, 0.2, 0.1, 0.02) + T * float4(1.0, 0.3, 0.1, 0.1);
q = abs(frac(q + 0.5) * 2.0 - 1.0);
q.zw = saturate(q.zw - float2(0.3, 0.7)) / float2(0.7, 0.3);
float s = length(w) * Bend * saturate(dot(q, float4(1.0, 1.0, 6.0, 10.0)) / 14.0);
float bp = VC.g + dot(wp, float3(2.0, 2.0, 2.0));
float vp = dot(p, float3(bp, bp, bp));
float2 wi = T + float2(vp, bp);
float4 v = (frac(wi.xxyy * float4(1.2, 0.793, 0.375, 0.193)) * 2.0 - 1.0) * P.x * s;
v = abs(frac(v + 0.5) * 2.0 - 1.0);
float2 sum = v.xz + v.yw;
float3 r = p + sum.xxy * float3(VC.r * P.y * n.xy, (1.0 - VC.b) * P.z);
float f = r.z * 100.0 / max(H, 1.0);
f *= f;
float len = length(r);
float3 m = r;
m.xy += w.xy * Bend * f;
r = normalize(m) * len;
return (r - p) * float3(100.0, -100.0, 100.0);"""


def wind_parent_graph(material, content, state):
    """The world position offset of WIND_HLSL in a parent: the inputs from the Unreal nodes (the local position and
    normal, the vertex color, the time, the instance origin, the global wind in local axes, SwcWind of the custom
    primitive data 0, BendDetail, the height of the local bounds); the local offset goes to world axes."""
    position = node(material, unreal.MaterialExpressionPreSkinnedPosition, -1700, 1500)
    normal = node(material, unreal.MaterialExpressionPreSkinnedNormal, -1700, 1600)
    vertex = node(material, unreal.MaterialExpressionVertexColor, -1700, 1700)
    time = node(material, unreal.MaterialExpressionTime, -1700, 1850)
    zero = node(material, unreal.MaterialExpressionConstant3Vector, -1900, 1950)
    origin = node(material, unreal.MaterialExpressionTransformPosition, -1700, 1950)
    origin.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    origin.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    editing.connect_material_expressions(zero, "", origin, "")
    wind = node(material, unreal.MaterialExpressionCollectionParameter, -1900, 2100)
    wind.set_editor_property("collection", collection(content, state))
    wind.set_editor_property("parameter_name", "WindVector")
    wind_local = node(material, unreal.MaterialExpressionTransform, -1700, 2100)
    wind_local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    wind_local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    wind_rgb = mask(material, wind, "", "rgb", -1800, 2100)
    editing.connect_material_expressions(wind_rgb, "", wind_local, "")
    bend = scalar(material, "SwcWind", 0.0, -1700, 2250)
    bend.set_editor_property("use_custom_primitive_data", True)
    bend.set_editor_property("primitive_data_index", 0)
    detail = vector(material, "BendDetail", (0.0, 0.0, 0.0, 0.0), -1700, 2400)
    bounds = node(material, unreal.MaterialExpressionObjectLocalBounds, -1700, 2550)
    height = mask(material, bounds, "Extents", "b", -1500, 2550)
    offset = custom(material, WIND_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                    ("LP", "LN", "VC", "T", "OP", "WL", "Bend", "P", "H"), -1200, 1900)
    for pin, source, out in (("LP", position, ""), ("LN", normal, ""), ("VC", vertex, ""), ("T", time, ""),
                             ("OP", origin, ""), ("WL", wind_local, ""), ("Bend", bend, ""), ("P", detail, "RGB"),
                             ("H", height, "")):
        editing.connect_material_expressions(source, out, offset, pin)
    world = node(material, unreal.MaterialExpressionTransform, -900, 1900)
    world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    editing.connect_material_expressions(offset, "", world, "")
    # Static switch Wind (the m6 specs): with it off the parent has no world position offset, so Nanite and the virtual
    # shadow maps treat the mesh as static (no cost before the wind deploys).
    still = node(material, unreal.MaterialExpressionConstant3Vector, -900, 2050)
    moving = switch(material, "Wind", (world, ""), (still, ""), -700, 1900)
    editing.connect_material_property(moving, "", MP.MP_WORLD_POSITION_OFFSET)


# The camera fade of the source vegetation (common.cfib GetCameraDissolveOpacity, research/ground-cover.md "Camera fade"):
# D the 3D distance (cm) of the pixel to the render camera, S the pixel position, F = VegCameraFade (the method, the start
# and the end in m). A fragment with opacity o = saturate((d - start) / (end - start)) at or below 0.99 is discarded where
# the screen-space gradient noise is above o. The mask goes into the opacity mask, so the depth pass has the same cutout.
CAMERA_FADE_HLSL = """float d = D * 0.01;
float o = saturate((d - F.y) / max(F.z - F.y, 0.001));
float n = frac(52.9829189 * frac(dot(floor(S), float2(0.06711056, 0.00583715))));
return (F.x > 0.0 && o <= 0.99 && n > o) ? 0.0 : 1.0;"""


def camera_fade(material, content, state, x, y):
    """@return The node of the camera fade mask (1 = keep, 0 = discard) of CAMERA_FADE_HLSL."""
    world = node(material, unreal.MaterialExpressionWorldPosition, x - 600, y)
    camera = node(material, unreal.MaterialExpressionCameraPositionWS, x - 600, y + 100)
    delta = node(material, unreal.MaterialExpressionSubtract, x - 400, y)
    editing.connect_material_expressions(world, "", delta, "A")
    editing.connect_material_expressions(camera, "", delta, "B")
    distance = node(material, unreal.MaterialExpressionLength, x - 250, y)
    editing.connect_material_expressions(delta, "", distance, "")
    screen = node(material, unreal.MaterialExpressionScreenPosition, x - 400, y + 200)
    fade = node(material, unreal.MaterialExpressionCollectionParameter, x - 400, y + 350)
    fade.set_editor_property("collection", collection(content, state))
    fade.set_editor_property("parameter_name", "VegCameraFade")
    mask = custom(material, CAMERA_FADE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("D", "S", "F"), x, y)
    editing.connect_material_expressions(distance, "", mask, "D")
    editing.connect_material_expressions(screen, "PixelPosition", mask, "S")
    editing.connect_material_expressions(fade, "", mask, "F")
    return mask


def foliage_parent(content, state):
    """@return M_SwcFoliage (made again when its version in PARENT_VERSIONS changes; its instances keep their parameters).
    Version 2: the world position offset of the source wind (WIND_HLSL; no motion while SwcWind, the custom primitive
    data 0, is 0) and the vertex alpha occlusion of the Vegetation shader (the diffuse and the specular x the vertex
    alpha; white with no vertex colors). Version 4: the camera fade of the source vegetation in the opacity mask
    (camera_fade; the ground cover rule of research/ground-cover.md)."""
    name = "M_SwcFoliage"
    material, white = parent_asset(name, content, state)
    if material is None:
        return assets.load_asset(f"{BASE}/{name}")
    st = unreal.MaterialSamplerType
    color = sampler(material, "BaseColor", st.SAMPLERTYPE_COLOR,
                    unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), -900, -300)
    diffuse = vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -900, -50)
    albedo = node(material, unreal.MaterialExpressionMultiply, -500, -200)
    editing.connect_material_expressions(color, "RGB", albedo, "A")
    editing.connect_material_expressions(diffuse, "RGB", albedo, "B")
    vertex = node(material, unreal.MaterialExpressionVertexColor, -900, -500)
    occluded = multiply(material, (albedo, ""), (vertex, "A"), -350, -250)   # the vertex alpha occlusion of the source
    editing.connect_material_property(occluded, "", MP.MP_BASE_COLOR)
    near = camera_fade(material, content, state, -500, -900)
    opacity = multiply(material, (color, "A"), (near, ""), -250, -700)
    editing.connect_material_property(opacity, "", MP.MP_OPACITY_MASK)
    normal = sampler(material, "Normal", st.SAMPLERTYPE_NORMAL, unreal.load_asset("/Engine/EngineMaterials/FlatNormal"),
                     -900, 150)
    editing.connect_material_property(normal, "RGB", MP.MP_NORMAL)
    gloss = sampler(material, "Gloss", st.SAMPLERTYPE_ALPHA, white, -900, 400)
    smoothness = scalar(material, "Smoothness", 0.5, -900, 650)
    s = node(material, unreal.MaterialExpressionMultiply, -500, 450)
    editing.connect_material_expressions(gloss, "R", s, "A")
    editing.connect_material_expressions(smoothness, "", s, "B")
    rough = node(material, unreal.MaterialExpressionOneMinus, -300, 450)
    editing.connect_material_expressions(s, "", rough, "")
    editing.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    specular = scalar(material, "Specular", 0.5, -500, 650)
    occluded_specular = multiply(material, (specular, ""), (vertex, "A"), -350, 650)
    editing.connect_material_property(occluded_specular, "", MP.MP_SPECULAR)
    wind_parent_graph(material, content, state)
    transmission = sampler(material, "Transmission", st.SAMPLERTYPE_ALPHA, white, -900, 850)
    back = scalar(material, "BackDiffuseMultiplier", 1.0, -900, 1100)
    amount = node(material, unreal.MaterialExpressionMultiply, -600, 900)
    editing.connect_material_expressions(transmission, "R", amount, "A")
    editing.connect_material_expressions(back, "", amount, "B")
    clamp = node(material, unreal.MaterialExpressionSaturate, -450, 900)
    editing.connect_material_expressions(amount, "", clamp, "")
    tint = vector(material, "TransmittanceColor", (1.0, 1.0, 1.0, 1.0), -600, 1100)
    transmittance = node(material, unreal.MaterialExpressionMultiply, -300, 950)
    editing.connect_material_expressions(clamp, "", transmittance, "A")
    editing.connect_material_expressions(tint, "RGB", transmittance, "B")
    # The source multiplies all the diffuse light, the back light too, by the albedo (deferredshading ThinTranslucencyBRDF:
    # Transmittance x back + front, then x the albedo; vegetation.cfxb frag_custom_end: (ambient + front + back x
    # cShadingBack) x cDiffuseMap). The transmission of Unreal (TwoSidedBxDF) has no base color: the albedo goes into the
    # subsurface color. Version 2 and before had no albedo here: the leaves and the grass were near white in the back light.
    subsurface = multiply(material, (transmittance, ""), (occluded, ""), -150, 950)
    editing.connect_material_property(subsurface, "", MP.MP_SUBSURFACE_COLOR)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_nanite", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["base:" + name] = PARENT_VERSIONS[name]
    return material


LUMEN_VERSION = "1"   # the rule of the Lumen mesh pipelines (lumen_pipelines)


def lumen_pipelines(state):
    """@return {nanite: the path of a mesh pipeline with the Lumen mesh data}: P_SwcMeshNaniteLumen and P_SwcMeshLumen, copies
    of the world pipelines of Scripts/import_content.py (P_SwcMeshNanite, P_SwcMesh: the same settings) with a mesh
    distance field (resolution scale 1) and Lumen cards (12): the settings of the Lumen copies of the environment work
    (E2, evidence/E2/lumen; the decision of the user for the M3 rebuild). The world pipelines do not change."""
    out = {}
    for nanite, source_name in ((True, "P_SwcMeshNanite"), (False, "P_SwcMesh")):
        name = source_name + "Lumen"
        path = f"{BASE}/{name}"
        if not assets.does_asset_exist(path) or state.get("base:" + name) != LUMEN_VERSION:
            if assets.does_asset_exist(path):
                assets.delete_asset(path)
            pipeline = assets.duplicate_loaded_asset(unreal.load_asset(f"{BASE}/{source_name}"), path)
            meshes = pipeline.get_editor_property("mesh_pipeline")
            meshes.set_editor_property("distance_field_resolution_scale", 1.0)
            meshes.set_editor_property("max_lumen_mesh_cards", 12)
            assets.save_loaded_asset(pipeline)
            state["base:" + name] = LUMEN_VERSION
        out[nanite] = path
    return out


def spec_stamp(spec):
    return INSTANCE_VERSION + ":" + hashlib.sha1(
        json.dumps(spec, sort_keys=True).encode()).hexdigest()[:16]


def apply(instance, spec, parents, texture_of):
    """Sets one material instance from its spec: the parent (None: the parent stays), the textures, the vectors, the
    scalars, two sided, and the cutoff. A new parent clears the old parameters first."""
    parent = parents.get(spec.get("parent"))
    if parent is not None:
        editing.clear_all_material_instance_parameters(instance)
        editing.set_material_instance_parent(instance, parent)
    for name, key in spec.get("textures", {}).items():
        texture = texture_of(key)
        if texture is None:
            raise RuntimeError(f"no texture asset {key}")
        editing.set_material_instance_texture_parameter_value(instance, name, texture)
    for name, value in spec.get("vectors", {}).items():
        editing.set_material_instance_vector_parameter_value(instance, name, unreal.LinearColor(*value))
    for name, value in spec.get("scalars", {}).items():
        editing.set_material_instance_scalar_parameter_value(instance, name, float(value))
    for name, value in spec.get("switches", {}).items():   # static switches (one shader for each combination)
        editing.set_material_instance_static_switch_parameter_value(instance, name, bool(value))
    if parent is not None and spec.get("parent") != "decal":
        overrides = instance.get_editor_property("base_property_overrides")
        overrides.set_editor_property("override_two_sided", True)
        overrides.set_editor_property("two_sided", bool(spec.get("two sided")))
        if "cutoff" in spec:
            overrides.set_editor_property("override_opacity_mask_clip_value", True)
            overrides.set_editor_property("opacity_mask_clip_value", float(spec["cutoff"]))
        instance.set_editor_property("base_property_overrides", overrides)
    editing.update_material_instance(instance)


def load_specs(content, part="materials"):
    """@return (the specs, the texture records) of SWC_CONTENT/PART (materials: the instances of the material records;
    decals: the decal materials of the roads and decals), empty when the converter wrote none."""
    folder = os.path.join(content, part)
    specs, textures = {}, {}
    if os.path.isfile(os.path.join(folder, "instances.json")):
        with open(os.path.join(folder, "instances.json")) as handle:
            specs = json.load(handle)["instances"]
    if os.path.isfile(os.path.join(folder, "textures.json")):
        with open(os.path.join(folder, "textures.json")) as handle:
            textures = json.load(handle)["textures"]
    return specs, textures


def plan(keys, specs, state, force):
    """@return The material keys of `keys` to apply: each key with a spec (a manifest key "material:m_X" or the name
    "m_X"), when `force` or when its state stamp differs from the stamp of its spec. A key with no spec is skipped."""
    out = []
    for key in keys:
        name = key.split(":", 1)[1] if key.startswith("material:") else key
        if name in specs and (force or state.get("materials:" + name) != spec_stamp(specs[name])):
            out.append(name)
    return sorted(set(out))


def apply_specs(keys, state, force=True, content=None, max_seconds=0.0, report=None, part="materials"):
    """Applies the spec of each material key that has one (a key with no spec is skipped): the textures of the specs
    that are new or changed, the parents, then each instance /SwcGameContent/Materials/KEY. import_content.py calls it
    at the end of its step 4 with the keys that the step made or set again (force: the step set the stage 1.2 rules
    again, so the stamp of this module does not count). Scripts/apply_materials.py calls it with force False. The caller
    holds the import lock and saves `state`. @return The report (counts, failures, "incomplete")."""
    import time
    started = time.time()
    content = content or os.environ["SWC_CONTENT"]
    report = report if report is not None else {}
    report.setdefault("textures", {"imported": 0, "up to date": 0})
    report.setdefault("instances", {"applied": 0, "up to date": 0, "missing": 0})
    report.setdefault("failed", [])
    report.setdefault("incomplete", False)
    specs, textures = load_specs(content, part)
    todo = plan(keys, specs, state, force)
    report["instances"]["up to date"] += len({k for k in keys if (k.split(":", 1)[-1]) in specs}) - len(todo)
    # A parent of a selected spec with a new version in PARENT_VERSIONS is made again also when no instance changes.
    names = {k.split(":", 1)[-1] for k in keys} & set(specs)
    stale = {specs[n].get("parent") for n in names} & {kind for kind, parent in PARENT_NAMES.items()
                                                         if state.get("base:" + parent) != PARENT_VERSIONS[parent]}
    if not todo and not stale:
        return report

    def out_of_time():
        if max_seconds and time.time() - started > max_seconds:
            report["incomplete"] = True
        return report["incomplete"]

    for key in sorted({t for name in todo for t in specs[name].get("textures", {}).values()}):
        if out_of_time():
            return report
        record = textures.get(key)
        if record is None:
            continue   # a key of the manifest: tools/import.sh imports it
        try:
            done = import_texture(key, record, content, state)
        except Exception as error:
            report["failed"].append(f"{key}: {str(error)[:160]}")
            continue
        report["textures"]["imported" if done else "up to date"] += 1
        if done and report["textures"]["imported"] % 50 == 0:
            unreal.SystemLibrary.collect_garbage()
    wanted = {specs[name].get("parent") for name in todo} | stale
    parents = {}
    if "foliage" in wanted:
        parents["foliage"] = foliage_parent(content, state)
    if "surface" in wanted:
        parents["surface"] = surface_parent(content, state, masked=False)
    if "surface masked" in wanted:
        parents["surface masked"] = surface_parent(content, state, masked=True)
    if "surface translucent" in wanted:
        parents["surface translucent"] = surface_parent(content, state, masked=False, translucent=True)
    if "decal" in wanted:
        parents["decal"] = decal_parent(content, state)
    if "glass" in wanted:
        parents["glass"] = glass_parent(content, state)
    if "beam" in wanted:
        parents["beam"] = beam_parent(content, state)
    for blend, name in (("opaque", "M_SwcOpaque"), ("masked", "M_SwcMasked"), ("translucent", "M_SwcTranslucent")):
        if "legacy " + blend in wanted:   # the base parents of stage 1.2 (import_content.py): an n_ instance of old rules
            parents["legacy " + blend] = assets.load_asset(f"{BASE}/{name}")
    manifest_path = os.path.join(content, "manifest.json")
    with open(manifest_path) as handle:
        manifest = json.load(handle)["assets"]
    cache = {}

    def texture_of(key):
        if key not in cache:
            record = textures.get(key) or manifest.get(key)
            cache[key] = first_asset(record["unreal"], "Texture2D") if record else None
        return cache[key]

    for name in todo:
        if out_of_time():
            break
        path = f"{ROOT}/Materials/{name}"
        if not assets.does_asset_exist(path) and specs[name].get("create"):
            # A key with no manifest record (a decal material): this module makes its instance.
            tools.create_asset(name, f"{ROOT}/Materials", unreal.MaterialInstanceConstant,
                               unreal.MaterialInstanceConstantFactoryNew())
        if not assets.does_asset_exist(path):
            report["instances"]["missing"] += 1   # not imported yet: tools/import.sh makes the instance first
            continue
        try:
            instance = assets.load_asset(path)
            apply(instance, specs[name], parents, texture_of)
            assets.save_loaded_asset(instance)
        except Exception as error:
            report["failed"].append(f"{name}: {str(error)[:160]}")
            continue
        state["materials:" + name] = spec_stamp(specs[name])
        report["instances"]["applied"] += 1
    log("apply_specs " + json.dumps({k: report[k] for k in ("textures", "instances", "incomplete")}))
    return report
