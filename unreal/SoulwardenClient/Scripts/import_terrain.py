"""The terrain material of the game in the shared plugin (world fidelity T1; converter/swc_convert/terrain_material.py,
research/terrain.md sections 2 and 6). Run through tools/import-terrain.sh (import.lock; the world lock for the steps
that change assets that runs stream).

Steps (SWC_TERRAIN_STEP), each with a time limit (SWC_TERRAIN_MAX_SECONDS, default 400: the call stops after the item
that passes it; call again):
- slices: the slices of the texture arrays (content/terrain/arrays/ROLE/NNN.png) as textures in /SwcGameContent/Terrain/Slices.
- arrays: the three texture arrays TA_SwcTerrainAlbedo (sRGB, DXT1), TA_SwcTerrainNormal (BC5), TA_SwcTerrainMasks
  (linear BC7: R gloss, G height, B specular) from the slices.
- material: the master material M_SwcTerrain: the base material of the region (alpha 1), then SLOTS slots L00 ... in the
  order of priority; each slot is a Landscape layer switch (pruned where a component has no such layer) around the tile
  composite of the game (TerrainTileCompositePS): the diffuse, gloss, specular, height, and normal of its slice, the
  macro overlay, and the height blend alpha of its raw weight (source-over). Then the distance fade to the macro maps
  (ApplyTerrainVirtualTexture) and the Unreal outputs: base color, roughness = 1 - gloss, specular = reflectance / 0.08
  (Unreal 0.5 = a reflectance of 0.04), the normal (tangent space: X east, Y south, as the Landscape).
- region:NAME: the macro maps of the region and its material instance MI_SwcTerrain_NAME (the parameters of the slots
  of regions/NAME/terrain-plan.json).
- mesh material: M_SwcTerrainMesh, the same composite for a mesh (the menu ground of docs/FRONTEND.md): MESH_SLOTS slots
  with no Landscape switch, the weights of the slots from the textures Weights 0 and Weights 1 (RGBA: four slots each),
  and the macro maps and the weights over a square of Ground Size metres from the corner (not a region of 2,048 m).
- menu: the pictures of the menu ground (content/frontend/terrain/menu: the weights, the macro maps of the square;
  converter/swc_convert/frontend.py ground_terrain) and the instance /SwcGameContent/Terrain/Menu/MI_SwcTerrainMenu.
"""
import json
import os
import sys
import time

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials  # noqa: E402  (the surface snow call of AST-257)

ROOT = "/SwcGameContent"
TERRAIN = ROOT + "/Terrain"
SLOTS = 57   # the largest region list of active layers (terrain_material.plan_all)
ROLES = {"albedo": "TA_SwcTerrainAlbedo", "normal": "TA_SwcTerrainNormal", "masks": "TA_SwcTerrainMasks"}
MATERIAL = TERRAIN + "/M_SwcTerrain"
MATERIAL_VERSION = "4"   # 4: the surface snow (AST-257, MF_SwcSnowSurface); 2: the default of the macro normal loads (unreal.load_asset: /Engine/EngineMaterials/FlatNormal);
                         # 3: the vector parameters give RGBA to the custom nodes (the default output is RGB)
MESH_MATERIAL = TERRAIN + "/M_SwcTerrainMesh"
MESH_MATERIAL_VERSION = "2"   # 2: the surface snow (AST-257, MF_SwcSnowSurface)
MESH_SLOTS = 8   # two weight textures
MENU = TERRAIN + "/Menu"

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
content = os.path.expanduser(os.environ.get("SWC_CONTENT", "~/Games/soulwarden-client/content"))
plugin = os.path.expanduser(os.environ.get("SWC_GAME_CONTENT", "~/Games/soulwarden-client/unreal/SwcGameContent"))
step = os.environ.get("SWC_TERRAIN_STEP", "")
MAX_SECONDS = float(os.environ.get("SWC_TERRAIN_MAX_SECONDS") or 400)
started = time.time()
state_path = os.path.join(plugin, "terrain-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) else {}
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)


def log(text):
    unreal.log("SWC terrain " + text)


def save_state():
    temporary = state_path + ".tmp"
    with open(temporary, "w") as out:
        json.dump(state, out, indent=1, sort_keys=True)
    os.replace(temporary, state_path)


def out_of_time():
    return time.time() - started > MAX_SECONDS


def import_png(path, folder, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    return assets.load_asset(f"{folder}/{name}")


def texture_settings(texture, kind):
    """kind: color (sRGB), normal (BC5), data (linear RGB, BC7)."""
    if kind == "color":
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    elif kind == "normal":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    elif kind == "weights":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    elif kind == "data fast":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    else:
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)


KINDS = {"albedo": "color", "normal": "normal", "masks": "data"}


def step_slices():
    index = json.load(open(os.path.join(content, "terrain", "arrays", "index.json")))
    count = len(index["slices"])
    done = 0
    for role in ROLES:
        for i in range(count):
            key = f"slice:{role}:{i:03d}"
            path = os.path.join(content, "terrain", "arrays", role, f"{i:03d}.png")
            stamp = f"{os.path.getsize(path)}:{os.stat(path).st_mtime_ns}"
            if state.get(key) == stamp:
                continue
            if out_of_time():
                return {"incomplete": True, "imported": done}
            texture = import_png(path, f"{TERRAIN}/Slices/{role}", f"S_{role}_{i:03d}")
            # The compression of the array comes from its own settings: the slices use a fast one (no BC7 here).
            texture_settings(texture, "data fast" if role == "masks" else KINDS[role])
            texture.set_editor_property("never_stream", True)   # a source of an array only
            assets.save_loaded_asset(texture)
            state[key] = stamp
            done += 1
            if done % 12 == 0:
                save_state()
                unreal.SystemLibrary.collect_garbage()
    save_state()
    return {"incomplete": False, "imported": done}


def step_arrays():
    index = json.load(open(os.path.join(content, "terrain", "arrays", "index.json")))
    count = len(index["slices"])
    made = []
    for role, name in ROLES.items():
        stamps = [state.get(f"slice:{role}:{i:03d}") for i in range(count)]
        if None in stamps:
            raise RuntimeError(f"{role}: slices missing; run the step slices first")
        key = f"array:{role}"
        wanted = str(hash(tuple(stamps)) & 0xFFFFFFFF)
        path = f"{TERRAIN}/{name}"
        if state.get(key) == wanted and assets.does_asset_exist(path):
            continue
        if made and out_of_time():
            return {"incomplete": True, "made": made}
        slices = [assets.load_asset(f"{TERRAIN}/Slices/{role}/S_{role}_{i:03d}") for i in range(count)]
        compression = {"color": unreal.TextureCompressionSettings.TC_DEFAULT, "normal": unreal.TextureCompressionSettings.TC_NORMALMAP,
                       "data": unreal.TextureCompressionSettings.TC_BC7}[KINDS[role]]
        array = unreal.SwcEditorLibrary.create_texture_array(path, slices, compression, KINDS[role] == "color")
        if array is None:
            raise RuntimeError(f"{name}: the array was not made")
        state[key] = wanted
        save_state()
        made.append(name)
        log(f"array {name}: {count} slices ({round(time.time() - started)} s)")
    return {"incomplete": False, "made": made}


# The HLSL of one slot (TerrainTileCompositePS and GetTerrainMaterialLayerBlendingValue of the game). A = (slice, TileU,
# TileV, gloss scale), B = (diffuse color, specular color red), C = (height scale, height offset, blend factor, blend
# falloff), D = (macro blend, macro gloss blend, macro saturation, macro normal scale); M = (macro color, macro gloss),
# MN = macro normal; L = the place in the region (m: X east, Y south from the north-west corner), O = the corner (m,
# Unreal axes: the tiling continues across the regions), Force = 1 for the base material (alpha 1). R = the readings of
# the color spaces (the upload of the game is not traced): R.x 1 = the diffuse maps decoded from sRGB (the DXGI format
# of the source; the array is sRGB), 0 = their raw values; R.y 1 = the specular maps decoded from sRGB, 0 = raw.
SLOT_CODE = r"""
float2 uv = frac(O * A.yz) + L * A.yz;
float3 diff = Alb.Sample(AlbSampler, float3(uv, A.x)).rgb;
float3 raw = lerp(diff * 12.92, 1.055 * pow(max(diff, 0.0), 1.0 / 2.4) - 0.055, step(0.0031308, diff));
diff = lerp(raw, diff, step(0.5, R.x)) * B.rgb;
float3 msk = Msk.Sample(MskSampler, float3(uv, A.x)).rgb;
float2 nxy = Nrm.Sample(NrmSampler, float3(uv, A.x)).rg * 2.0 - 1.0;
float gloss = A.w * msk.r;
float height = saturate(C.x * msk.g + C.y);
float specDecoded = msk.b <= 0.04045 ? msk.b / 12.92 : pow((msk.b + 0.055) / 1.055, 2.4);
float refl = B.a * lerp(msk.b, specDecoded, step(0.5, R.y));
float lum = dot(M.rgb, float3(0.2126, 0.7152, 0.0722));
float3 mc = lerp(lum.xxx, M.rgb, D.z);
float3 over = lerp(2.0 * mc * diff, 1.0 - 2.0 * (1.0 - mc) * (1.0 - diff), step(0.5, mc));
diff = lerp(diff, over, D.x);
float overGloss = lerp(2.0 * M.a * gloss, 1.0 - 2.0 * (1.0 - M.a) * (1.0 - gloss), step(0.5, M.a));
gloss = lerp(gloss, overGloss, D.y);
float3 n = normalize(float3(MN.xy * D.w + nxy, MN.z));
float blendValue = saturate(saturate(W * 2.0 - 1.0) + height);
float a = saturate(pow(max(W * blendValue * (1.0 + C.z), 0.0), C.w));
a = lerp(a, 1.0, step(0.5, Force));
S1Out = lerp(S1, float4(n.xy, refl, 0.0), a);
return lerp(S0, float4(diff, gloss), a);
"""
# The macro values (GetMacroColor, GetMacroGloss, GetMacroNormal): P = (color scale, gloss scale, normal scale, decode).
# The macro color map is imported raw (no sRGB view): decode 1 reads it as sRGB (the DXGI format of the source, BC1
# sRGB), 0 as the raw values. The color space of the upload of the values of the game is not traced: the reading that
# gives the retail ground colors in the matched views is the rule (the decision of the user, 2026-10-19).
MACRO_CODE = r"""
MNOut = normalize(float3(N.xy, 1.0 / max(P.z, 0.0001)));
float3 c = C.rgb;
float3 decoded = lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
c = lerp(c, decoded, step(0.5, P.w));
return float4(c * K.rgb * P.x, G * P.y);
"""
# The distance fade (ApplyTerrainVirtualTexture) and the Unreal outputs. F = (view distance m, blend distance m, far
# specular reflectance, 0); Dist in cm.
FINAL_CODE = r"""
float f = smoothstep(F.x - F.y, F.x, Dist / 100.0);
float3 color = lerp(S0.rgb, FarM.rgb, f);
float gloss = lerp(S0.a, FarM.a, f);
float2 nxy = lerp(S1.xy, FarMN.xy, f);
float refl = lerp(S1.z, F.z, f);
BaseOut = color;
RoughOut = saturate(1.0 - gloss);
SpecOut = saturate(refl / 0.08);
return float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
"""


def custom(material, code, inputs, output_type, extra, x, y, description):
    node = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("description", description)
    node.set_editor_property("output_type", output_type)
    items = []
    for name in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        items.append(item)
    node.set_editor_property("inputs", items)
    outs = []
    for name, kind in extra:
        item = unreal.CustomOutput()
        item.set_editor_property("output_name", name)
        item.set_editor_property("output_type", kind)
        outs.append(item)
    node.set_editor_property("additional_outputs", outs)
    return node


def vector(material, name, value, group, x, y):
    node = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*value))
    node.set_editor_property("group", group)
    return node


def connect(a, a_out, b, b_in):
    if not editing.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError(f"no connection {a.get_name()}.{a_out} -> {b.get_name()}.{b_in}")


def step_material():
    return build_material(MATERIAL, "material", MATERIAL_VERSION, SLOTS, mesh=False)


def step_mesh_material():
    return build_material(MESH_MATERIAL, "mesh material", MESH_MATERIAL_VERSION, MESH_SLOTS, mesh=True)


def build_material(path, key, version, slots, mesh):
    """M_SwcTerrain (mesh False: the Landscape layer weights and switches, the macro maps of a region of 2,048 m) or
    M_SwcTerrainMesh (mesh True: the weights from two textures, the macro maps over Ground Size metres)."""
    if state.get(key) == version and assets.does_asset_exist(path):
        return {"made": False}
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    material = tools.create_asset(path.rsplit("/", 1)[1], TERRAIN, unreal.Material, unreal.MaterialFactoryNew())
    F4, F3, F1 = (unreal.CustomMaterialOutputType.CMOT_FLOAT4, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                  unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    arrays = {}
    for i, (role, name) in enumerate(ROLES.items()):
        node = editing.create_material_expression(material, unreal.MaterialExpressionTextureObject, -3000, 200 * i)
        node.set_editor_property("texture", assets.load_asset(f"{TERRAIN}/{name}"))
        arrays[role] = node
    # The place in the region: (absolute world position - the corner) / 100, a float (TruncateLWC after the subtract).
    world = editing.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -3600, -800)
    corner = vector(material, "Region Corner", (0, 0, 0, 0), "Region", -3600, -650)   # m: x0, Unreal Y of the north edge
    corner_cm = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -3400, -650)
    corner_cm.set_editor_property("const_b", 100.0)
    connect(corner, "", corner_cm, "A")
    corner_xy = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -3400, -550)
    for c, v in (("r", True), ("g", True), ("b", False), ("a", False)):
        corner_xy.set_editor_property(c, v)
    connect(corner, "", corner_xy, "")
    world_xy = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -3400, -800)
    for c, v in (("r", True), ("g", True), ("b", False), ("a", False)):
        world_xy.set_editor_property(c, v)
    connect(world, "", world_xy, "")
    corner_cm_xy = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -3200, -650)
    for c, v in (("r", True), ("g", True), ("b", False), ("a", False)):
        corner_cm_xy.set_editor_property(c, v)
    connect(corner_cm, "", corner_cm_xy, "")
    local_cm = editing.create_material_expression(material, unreal.MaterialExpressionSubtract, -3000, -800)
    connect(world_xy, "", local_cm, "A")
    connect(corner_cm_xy, "", local_cm, "B")
    truncate = editing.create_material_expression(material, unreal.MaterialExpressionTruncateLWC, -2850, -800)
    connect(local_cm, "", truncate, "")
    local = editing.create_material_expression(material, unreal.MaterialExpressionDivide, -2700, -800)
    local.set_editor_property("const_b", 100.0)
    connect(truncate, "", local, "A")
    macro_uv = editing.create_material_expression(material, unreal.MaterialExpressionDivide, -2550, -900)
    connect(local, "", macro_uv, "A")
    if mesh:
        ground_size = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -2700, -950)
        ground_size.set_editor_property("parameter_name", "Ground Size")
        ground_size.set_editor_property("default_value", 2048.0)
        ground_size.set_editor_property("group", "Region")
        connect(ground_size, "", macro_uv, "B")
    else:
        macro_uv.set_editor_property("const_b", 2048.0)
    # The macro maps of the region.
    st = unreal.MaterialSamplerType
    samples = {}
    for i, (name, kind, default) in enumerate((("Macro Color", st.SAMPLERTYPE_MASKS, ROOT + "/Base/T_SwcWhiteData"),
                                               ("Macro Normal", st.SAMPLERTYPE_NORMAL, "/Engine/EngineMaterials/FlatNormal"),
                                               ("Macro Gloss", st.SAMPLERTYPE_MASKS, ROOT + "/Base/T_SwcWhiteData"))):
        node = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -2400, -1100 + 220 * i)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("sampler_type", kind)
        texture = unreal.load_asset(default)   # EditorAssetLibrary.load_asset gave None for FlatNormal
        if texture is None:
            raise RuntimeError(f"{name}: the default texture {default} does not load")
        node.set_editor_property("texture", texture)
        node.set_editor_property("group", "Region")
        connect(macro_uv, "", node, "UVs")
        samples[name] = node
    macros = {}
    for i, (label, color, params) in enumerate((("Near", (0.7294118, 0.7294118, 0.7294118, 1), (1, 1, 1, 1)),
                                               ("Far", (0.3019608, 0.3019608, 0.3019608, 1), (1, 1, 1, 1)))):
        k = vector(material, f"Macro {label} Color", color, "Macro", -2150, -1200 + 400 * i)
        p = vector(material, f"Macro {label} Scales", params, "Macro", -2150, -1050 + 400 * i)
        node = custom(material, MACRO_CODE, ["C", "G", "N", "K", "P"], F4, [("MNOut", F3)], -1900, -1150 + 400 * i, f"macro {label}")
        connect(samples["Macro Color"], "RGB", node, "C")
        connect(samples["Macro Gloss"], "R", node, "G")
        connect(samples["Macro Normal"], "RGB", node, "N")
        connect(k, "RGBA", node, "K")
        connect(p, "RGBA", node, "P")
        macros[label] = node
    # The base material, then the slots.
    readings = vector(material, "Readings", (1, 1, 0, 0), "Macro", -1900, -400)   # the literal reading (see SLOT_CODE)
    x = -1500
    previous = None
    corner_m = corner_xy

    def layer(name, force, weight, previous_nodes, x):
        a = vector(material, f"{name} A", (0, 0.25, 0.25, 1), name, x, 900)
        b = vector(material, f"{name} B", (0.497, 0.497, 0.497, 0.1), name, x, 1050)
        c = vector(material, f"{name} C", (1, 0, 1, 1), name, x, 1200)
        d = vector(material, f"{name} D", (1, 1, 1, 1), name, x, 1350)
        node = custom(material, SLOT_CODE, ["S0", "S1", "W", "A", "B", "C", "D", "M", "MN", "L", "O", "Force", "R", "Alb", "Nrm", "Msk"],
                      F4, [("S1Out", F4)], x, 600, f"{name}")
        if previous_nodes is None:
            zero = editing.create_material_expression(material, unreal.MaterialExpressionConstant4Vector, x - 200, 400)
            zero.set_editor_property("constant", unreal.LinearColor(0, 0, 0, 0))
            connect(zero, "", node, "S0")
            connect(zero, "", node, "S1")
        else:
            connect(previous_nodes[0][0], previous_nodes[0][1], node, "S0")
            connect(previous_nodes[1][0], previous_nodes[1][1], node, "S1")
        connect(weight, "", node, "W")
        for name_, param in (("A", a), ("B", b), ("C", c), ("D", d)):
            connect(param, "RGBA", node, name_)
        connect(macros["Near"], "", node, "M")
        connect(macros["Near"], "MNOut", node, "MN")
        connect(local, "", node, "L")
        connect(corner_m, "", node, "O")
        connect(force, "", node, "Force")
        connect(readings, "RGBA", node, "R")
        connect(arrays["albedo"], "", node, "Alb")
        connect(arrays["normal"], "", node, "Nrm")
        connect(arrays["masks"], "", node, "Msk")
        return node

    one = editing.create_material_expression(material, unreal.MaterialExpressionConstant, x - 300, 300)
    one.set_editor_property("r", 1.0)
    zero1 = editing.create_material_expression(material, unreal.MaterialExpressionConstant, x - 300, 200)
    zero1.set_editor_property("r", 0.0)
    base = layer("Base", one, one, None, x)
    current = ((base, ""), (base, "S1Out"))
    weight_maps = []
    if mesh:
        # The weights of the mesh: four slots in each texture (R, G, B, A), on the grid of the macro maps.
        for i in range((slots + 3) // 4):
            node = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -2400, -400 + 220 * i)
            node.set_editor_property("parameter_name", f"Weights {i}")
            node.set_editor_property("sampler_type", st.SAMPLERTYPE_LINEAR_COLOR)
            node.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black"))
            node.set_editor_property("group", "Region")
            connect(macro_uv, "", node, "UVs")
            weight_maps.append(node)
    for slot in range(slots):
        name = f"L{slot:02d}"
        x += 400
        if mesh:
            weight = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, x, 300)
            for c in ("r", "g", "b", "a"):
                weight.set_editor_property(c, c == "rgba"[slot % 4])
            connect(weight_maps[slot // 4], "RGBA", weight, "")
            node = layer(name, zero1, weight, current, x)
            current = ((node, ""), (node, "S1Out"))
            continue
        weight = editing.create_material_expression(material, unreal.MaterialExpressionLandscapeLayerSample, x, 300)
        weight.set_editor_property("parameter_name", name)
        weight.set_editor_property("preview_weight", 0.0)
        node = layer(name, zero1, weight, current, x)
        switches = []
        for output, (prev_node, prev_out) in zip(("", "S1Out"), current):
            switch = editing.create_material_expression(material, unreal.MaterialExpressionLandscapeLayerSwitch, x + 200, 100 if not output else 200)
            switch.set_editor_property("parameter_name", name)
            connect(node, output, switch, "LayerUsed")
            connect(prev_node, prev_out, switch, "LayerNotUsed")
            switches.append((switch, ""))
        current = tuple(switches)
    # The fade and the outputs.
    camera = editing.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, x + 300, -300)
    to_camera = editing.create_material_expression(material, unreal.MaterialExpressionSubtract, x + 500, -300)
    connect(camera, "", to_camera, "A")
    connect(world, "", to_camera, "B")
    to_camera_f = editing.create_material_expression(material, unreal.MaterialExpressionTruncateLWC, x + 650, -300)
    connect(to_camera, "", to_camera_f, "")
    distance = editing.create_material_expression(material, unreal.MaterialExpressionLength, x + 800, -300)
    connect(to_camera_f, "", distance, "")
    fade = vector(material, "Fade", (500, 50, 0, 0), "Macro", x + 800, -150)   # preset 7 (sys_spec_terrain.cfg)
    final = custom(material, FINAL_CODE, ["S0", "S1", "FarM", "FarMN", "F", "Dist"], F3,
                   [("BaseOut", F3), ("RoughOut", F1), ("SpecOut", F1)], x + 1100, 0, "fade and outputs")
    connect(current[0][0], "", final, "S0")
    connect(current[1][0], "", final, "S1")
    connect(macros["Far"], "", final, "FarM")
    connect(macros["Far"], "MNOut", final, "FarMN")
    connect(fade, "RGBA", final, "F")
    connect(distance, "", final, "Dist")
    mp = unreal.MaterialProperty
    # The surface snow and frost of the Snow volumes (AST-257; swc_materials.snow_surface): the terrain has no metal (0).
    no_metal = editing.create_material_expression(material, unreal.MaterialExpressionConstant, x + 1100, 300)
    snow = swc_materials.snow_surface(material, ((final, "BaseOut"), (no_metal, ""), (final, "SpecOut"), (final, "RoughOut"), (final, "")),
                                      x + 1400, 0)
    editing.connect_material_property(snow, "BaseColor", mp.MP_BASE_COLOR)
    editing.connect_material_property(snow, "Roughness", mp.MP_ROUGHNESS)
    editing.connect_material_property(snow, "Specular", mp.MP_SPECULAR)
    editing.connect_material_property(snow, "Normal", mp.MP_NORMAL)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state[key] = version
    save_state()
    return {"made": True, "slots": slots}


def import_macro(name, role, kind):
    path = os.path.join(content, "regions", name, "macro", role + ".png")
    folder = f"{TERRAIN}/Regions/{name}"
    texture = import_png(path, folder, f"T_Macro_{role}")
    texture_settings(texture, kind)
    assets.save_loaded_asset(texture)
    return texture


def step_region(name):
    plan = json.load(open(os.path.join(content, "regions", name, "terrain-plan.json")))
    if not plan["slots"]:
        return {"region": name, "slots": 0}
    textures = {"Macro Color": import_macro(name, "color", "data fast"), "Macro Normal": import_macro(name, "normal", "normal"),
                "Macro Gloss": import_macro(name, "gloss", "data")}
    index = json.load(open(os.path.join(content, "terrain", "arrays", "index.json")))["slices"]
    path = f"{TERRAIN}/Regions/{name}/MI_SwcTerrain_{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        instance = tools.create_asset(f"MI_SwcTerrain_{name}", f"{TERRAIN}/Regions/{name}", unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property("parent", assets.load_asset(MATERIAL))
    set_vector = editing.set_material_instance_vector_parameter_value
    for label, texture in textures.items():
        editing.set_material_instance_texture_parameter_value(instance, label, texture)
    rx, ry = (int(v) for v in name[2:].split("_"))
    set_vector(instance, "Region Corner", unreal.LinearColor(rx * 2048.0, -(ry * 2048.0 + 2048.0), 0, 0))
    params = plan["macro params"]
    for label, key in (("Near", "foreground"), ("Far", "background")):
        p = params[key]
        set_vector(instance, f"Macro {label} Color", unreal.LinearColor(*p["color"], 1.0))
        decode = float(os.environ.get("SWC_TERRAIN_MACRO_DECODE", "1"))   # the reading of the macro color map (1: sRGB)
        set_vector(instance, f"Macro {label} Scales", unreal.LinearColor(p["color scale"], p["gloss scale"], p["normal scale"], decode))
    set_vector(instance, "Fade", unreal.LinearColor(500.0, 50.0, params["background"]["specular reflectance"], 0.0))
    readings = [float(v) for v in os.environ.get("SWC_TERRAIN_READINGS", "1 1").split()]   # the detail and specular decode
    set_vector(instance, "Readings", unreal.LinearColor(readings[0], readings[1], 0.0, 0.0))
    near = os.environ.get("SWC_TERRAIN_NEAR_COLOR")   # another reading of the macro color of the foreground set
    if near:
        set_vector(instance, "Macro Near Color", unreal.LinearColor(float(near), float(near), float(near), 1.0))
    for slot in ([dict(plan["base"], slot="Base")] if plan.get("base") else []) + plan["slots"]:
        s = slot["slot"]
        set_vector(instance, f"{s} A", unreal.LinearColor(float(index[slot["material"]]), slot["tile"][0], slot["tile"][1], slot["gloss scale"]))
        set_vector(instance, f"{s} B", unreal.LinearColor(*slot["diffuse color"], slot["specular color"][0]))
        set_vector(instance, f"{s} C", unreal.LinearColor(slot["height scale"], slot["height offset"], slot["blend factor"], slot["blend falloff"]))
        set_vector(instance, f"{s} D", unreal.LinearColor(slot["macro blend"], slot["macro gloss blend"], slot["macro saturation"],
                                                         params["foreground"]["normal scale"]))
    editing.update_material_instance(instance)
    assets.save_loaded_asset(instance)
    state["region:" + name] = plan["version"]
    save_state()
    return {"region": name, "slots": len(plan["slots"]), "instance": path}


def step_menu():
    """The instance of the menu ground: MI_SwcTerrainMenu on M_SwcTerrainMesh with the pictures and values of the ground
    record of frontend/nighthaven.json (terrain)."""
    step_mesh_material()
    record = json.load(open(os.path.join(content, "frontend", "nighthaven.json")))["ground"]["terrain"]
    index = json.load(open(os.path.join(content, "terrain", "arrays", "index.json")))["slices"]
    if len(record["slots"]) > MESH_SLOTS:
        raise RuntimeError(f"the menu ground has {len(record['slots'])} slots, the mesh material {MESH_SLOTS}")
    textures = {}
    for i, path in enumerate(record["weights"]):
        textures[f"Weights {i}"] = (path, "weights", f"T_MenuWeights_{i}")
    for role, kind in (("color", "data fast"), ("normal", "normal"), ("gloss", "data")):
        textures[f"Macro {role.capitalize()}"] = (record["macro"][role], kind, f"T_MenuMacro_{role}")
    path = f"{MENU}/MI_SwcTerrainMenu"
    instance = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        "MI_SwcTerrainMenu", MENU, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property("parent", assets.load_asset(MESH_MATERIAL))
    for label, (picture, kind, name) in textures.items():
        texture = import_png(os.path.join(content, picture), MENU, name)
        texture_settings(texture, kind)
        assets.save_loaded_asset(texture)
        editing.set_material_instance_texture_parameter_value(instance, label, texture)
    set_vector = editing.set_material_instance_vector_parameter_value
    x0, north = record["corner"]
    set_vector(instance, "Region Corner", unreal.LinearColor(float(x0), -float(north), 0, 0))
    editing.set_material_instance_scalar_parameter_value(instance, "Ground Size", float(record["size"]))
    params = record["macro params"]
    for label, key in (("Near", "foreground"), ("Far", "background")):
        p = params[key]
        set_vector(instance, f"Macro {label} Color", unreal.LinearColor(*p["color"], 1.0))
        set_vector(instance, f"Macro {label} Scales", unreal.LinearColor(p["color scale"], p["gloss scale"], p["normal scale"], 1.0))
    set_vector(instance, "Fade", unreal.LinearColor(500.0, 50.0, params["background"]["specular reflectance"], 0.0))
    set_vector(instance, "Readings", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))
    named = ([dict(record["base"], slot="Base")] if record.get("base") else []) + [dict(s, slot=f"L{i:02d}") for i, s in enumerate(record["slots"])]
    for slot in named:
        s = slot["slot"]
        set_vector(instance, f"{s} A", unreal.LinearColor(float(index[slot["material"]]), slot["tile"][0], slot["tile"][1], slot["gloss scale"]))
        set_vector(instance, f"{s} B", unreal.LinearColor(*slot["diffuse color"], slot["specular color"][0]))
        set_vector(instance, f"{s} C", unreal.LinearColor(slot["height scale"], slot["height offset"], slot["blend factor"], slot["blend falloff"]))
        set_vector(instance, f"{s} D", unreal.LinearColor(slot["macro blend"], slot["macro gloss blend"], slot["macro saturation"],
                                                         params["foreground"]["normal scale"]))
    editing.update_material_instance(instance)
    assets.save_loaded_asset(instance)
    return {"menu": path, "slots": len(record["slots"])}


if step == "slices":
    report = step_slices()
elif step == "arrays":
    report = step_arrays()
elif step == "material":
    report = step_material()
elif step == "mesh material":
    report = step_mesh_material()
elif step == "menu":
    report = step_menu()
elif step.startswith("region:"):
    report = step_region(step.split(":", 1)[1])
elif step.startswith("regions:"):
    # Many regions in one call (an editor start costs about 40 s): the ones with no instance of the current plan first,
    # until the time limit.
    done, regions = [], step.split(":", 1)[1].split(",")
    report = {"incomplete": False}
    for name in regions:
        plan_file = os.path.join(content, "regions", name, "terrain-plan.json")
        stamp = f"{os.stat(plan_file).st_mtime_ns}" if os.path.isfile(plan_file) else ""
        if state.get("region plan:" + name) == stamp and assets.does_asset_exist(f"{TERRAIN}/Regions/{name}/MI_SwcTerrain_{name}"):
            continue
        if done and out_of_time():
            report["incomplete"] = True
            break
        step_region(name)
        state["region plan:" + name] = stamp
        save_state()
        done.append(name)
        unreal.SystemLibrary.collect_garbage()
    report["regions"] = done
else:
    raise RuntimeError(f"unknown step {step!r}")
report["seconds"] = round(time.time() - started, 1)
report["step"] = step
log("REPORT " + json.dumps(report))
