"""P2.1: the materials of the UI runtime, in the shared content plugin (made here: no game content).

- /SwcGameContent/SwcUi/M_SwcUiMasked: an image under a sprite mask (the LyShine mask component with a sprite). Color:
  the texture at UV 0 times the vertex color. Alpha: the alpha of the texture times the vertex alpha times the alpha of
  the mask texture at UV 1 (0 outside 0..1; with AlphaTest > 0 the mask alpha is 0 or 1 at that threshold, as the
  option UseAlphaTest of the mask). User interface domain, translucent.
- /SwcGameContent/SwcUi/M_SwcUiMaskedAdditive: the same for an additive image (the LyShine blend mode Add).

The client (Source/SoulwardenClient/UI/SwcUiSlate.cpp) makes one dynamic instance for each texture, mask texture, and
alpha test, and gives the mesh UVs in TexCoord 0 and the mask UVs in TexCoord 1 (the custom vertices of Slate). With
no material the client draws the image with no mask (one log line).
Run: tools/editor-script.sh ui_materials.py (SWC_UI_MATERIALS_FORCE=1 makes the materials again).
"""
import os

import unreal

ROOT = "/SwcGameContent"
FOLDER = ROOT + "/SwcUi"
WHITE = "/Engine/EngineResources/WhiteSquareTexture"
force = os.environ.get("SWC_UI_MATERIALS_FORCE", "") == "1"
assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)

CODE = """
float inside = (MaskUV.x >= 0 && MaskUV.x <= 1 && MaskUV.y >= 0 && MaskUV.y <= 1) ? 1 : 0;
float mask = MaskColor.a * inside;
if (AlphaTest > 0)
{
    mask = mask >= AlphaTest ? 1 : 0;
}
return float4(Base.rgb * Tint.rgb, Base.a * TintAlpha * mask);
"""


def log(text):
    unreal.log("SWC " + text)


def masked_material(name, blend):
    path = FOLDER + "/" + name
    if assets.does_asset_exist(path):
        if not force:
            log("ui material %s: present" % path)
            return
        assets.delete_asset(path)
    material = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", blend)
    white = unreal.load_asset(WHITE)

    base = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, -200)
    base.set_editor_property("parameter_name", "Texture")
    base.set_editor_property("texture", white)
    uv0 = editing.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1200, -200)
    uv0.set_editor_property("coordinate_index", 0)
    editing.connect_material_expressions(uv0, "", base, "UVs")

    mask = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, 100)
    mask.set_editor_property("parameter_name", "Mask")
    mask.set_editor_property("texture", white)
    uv1 = editing.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1200, 100)
    uv1.set_editor_property("coordinate_index", 1)
    editing.connect_material_expressions(uv1, "", mask, "UVs")

    test = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, 400)
    test.set_editor_property("parameter_name", "AlphaTest")
    test.set_editor_property("default_value", 0.0)
    tint = editing.create_material_expression(material, unreal.MaterialExpressionVertexColor, -900, 550)

    custom = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -500, 100)
    custom.set_editor_property("code", CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", "SwcUiMask")
    inputs = []
    for input_name in ("Base", "MaskColor", "MaskUV", "AlphaTest", "Tint", "TintAlpha"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    editing.connect_material_expressions(base, "RGBA", custom, "Base")
    editing.connect_material_expressions(mask, "RGBA", custom, "MaskColor")
    editing.connect_material_expressions(uv1, "", custom, "MaskUV")
    editing.connect_material_expressions(test, "", custom, "AlphaTest")
    editing.connect_material_expressions(tint, "", custom, "Tint")
    editing.connect_material_expressions(tint, "A", custom, "TintAlpha")

    rgb = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -250, 0)
    for channel in ("r", "g", "b"):
        rgb.set_editor_property(channel, True)
    alpha = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -250, 200)
    alpha.set_editor_property("a", True)
    editing.connect_material_expressions(custom, "", rgb, "")
    editing.connect_material_expressions(custom, "", alpha, "")
    editing.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    editing.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    log("ui material %s: made" % path)


masked_material("M_SwcUiMasked", unreal.BlendMode.BLEND_TRANSLUCENT)
masked_material("M_SwcUiMaskedAdditive", unreal.BlendMode.BLEND_ADDITIVE)
