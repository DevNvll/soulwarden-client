"""The particle systems of the converter (AST-210, docs/PARTICLES.md "The systems"), into /SwcGameContent/Particles.

Run through tools/import-particles.sh (UnrealEditor-Cmd -run=pythonscript, no window). The input is SWC_CONTENT/particles/systems.json
(tools/convert-particles.sh systems): the textures of the systems (sRGB PNG) and, for each effect root, its emitters with the resolved
source values. The assets are made from the game copy: they go to the shared content plugin, not into git.

- Textures/T_*: the atlases (sRGB, the default compression).
- M_SwcParticleAlpha (Translucent) and M_SwcParticleAdditive (Additive): unlit sprite materials (the stage 2 fire). The color of a
  sprite is the atlas sample (ParticleSubUV: the frame blend of the renderer) x the particle color (the source color x the age color x
  EmissiveLighting x the glow of the .mtl: the HDR color of the builder) x Tint (the Color of the placement: a material binding of
  User.SwcTint) x EmissionUnit (0.1: the source emission unit, docs/MATERIALS.md M4) x EmissiveScale of MPC_SwcMaterials (10,000 cd/m2 x
  swc.LightScale at run time). The opacity is the atlas alpha x the particle alpha x AlphaScale x the soft fade (static switch Soft:
  1 - exp(-d^2 K) of the scene depth gap d in m, the source rule of ApplySoftIntersection, K = SoftK). The additive material
  multiplies the color by the opacity (the non-alpha path of the source).
- Materials/MI_*: one instance for each (texture, blend, soft) of the emitters.
- Systems/NS_*: one Niagara system for each effect root (SwcEditorLibrary.BuildParticleSystem: stateless emitters). A system is made
  again when its hash in systems.json changes, or with SWC_IMPORT_FORCE=1.
"""
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swc_materials as sm  # noqa: E402

content = os.environ["SWC_CONTENT"]
force = os.environ.get("SWC_IMPORT_FORCE", "") == "1"
ROOT = "/SwcGameContent/Particles"
PARENT_VERSION = "2"

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/SwcGameContent"], True)

index = json.load(open(os.path.join(content, "particles", "systems.json")))
state_path = os.path.join(content, "particles", "import-state.json")
state = json.load(open(state_path)) if os.path.isfile(state_path) and not force else {}
report = {"textures": 0, "materials": 0, "instances": 0, "systems": 0, "kept": 0, "failed": []}

SOFT_HLSL = """float d = max(min(SceneDepth - PixelDepth, PixelDepth), 0.0) * 0.01;
return 1.0 - exp(-d * d * K);"""


def import_texture(record):
    path = os.path.join(content, record["file"])
    stamp = sm.file_stamp(path)
    folder = f"{ROOT}/Textures"
    asset = f"{folder}/{record['asset']}"
    if state.get("texture:" + record["asset"]) == stamp and assets.does_asset_exist(asset):
        return
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", record["asset"])
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(asset)
    if texture is None:
        raise RuntimeError(f"{record['asset']}: no texture after the import")
    sm.texture_settings(texture, "srgb")
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    assets.save_loaded_asset(texture, only_if_is_dirty=False)
    state["texture:" + record["asset"]] = stamp
    report["textures"] += 1


def particle_collection():
    """@return MPC_SwcParticles: EmissionUnit (the scale of the source emission values to engine units; default 0.1, the M4 rule of
    docs/MATERIALS.md; at run time swc.Effects.Emission, Effects/SwcParticleEffects)."""
    name = "MPC_SwcParticles"
    path = f"{ROOT}/{name}"
    if assets.does_asset_exist(path):
        return assets.load_asset(path)
    mpc = tools.create_asset(name, ROOT, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    value = unreal.CollectionScalarParameter()
    value.set_editor_property("parameter_name", "EmissionUnit")
    value.set_editor_property("default_value", 0.1)
    mpc.set_editor_property("scalar_parameters", [value])
    assets.save_loaded_asset(mpc)
    return mpc


def parent(name, additive, lit=False):
    path = f"{ROOT}/{name}"
    if assets.does_asset_exist(path) and state.get("parent:" + name) == PARENT_VERSION:
        return assets.load_asset(path)
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        editing.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT if lit else unreal.MaterialShadingModel.MSM_UNLIT)
    if lit:
        # The light of the scene on the sprite (the diffuse term of the source particle shader: the light volume and the probe);
        # Unreal: the translucency light volume at each vertex, no direction (an approximation, docs/PARTICLES.md).
        material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_PER_VERTEX_NON_DIRECTIONAL)
    material.set_editor_property("two_sided", True)
    editing.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    atlas = sm.node(material, unreal.MaterialExpressionTextureSampleParameterSubUV, -1200, 0)
    atlas.set_editor_property("parameter_name", "Atlas")
    atlas.set_editor_property("blend", True)
    atlas.set_editor_property("texture", assets.load_asset(f"{ROOT}/Textures/{index['textures'][next(iter(index['textures']))]['asset']}"))
    color = sm.node(material, unreal.MaterialExpressionParticleColor, -1200, 400)
    tint = sm.vector(material, "Tint", (1.0, 1.0, 1.0, 1.0), -1200, 600)
    alpha_scale = sm.scalar(material, "AlphaScale", 1.0, -1200, 800)
    unit = sm.node(material, unreal.MaterialExpressionCollectionParameter, -1200, 950)
    unit.set_editor_property("collection", particle_collection())
    unit.set_editor_property("parameter_name", "EmissionUnit")
    scale = sm.node(material, unreal.MaterialExpressionCollectionParameter, -1200, 1100)
    scale.set_editor_property("collection", sm.collection(content, state))
    scale.set_editor_property("parameter_name", "EmissiveScale")
    # The soft fade (static switch Soft).
    soft_k = sm.scalar(material, "SoftK", 1.0, -1200, 1300)
    depth = sm.node(material, unreal.MaterialExpressionSceneDepth, -1200, 1450)
    pixel = sm.node(material, unreal.MaterialExpressionPixelDepth, -1200, 1600)
    fade = sm.custom(material, SOFT_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("SceneDepth", "PixelDepth", "K"), -900, 1400)
    editing.connect_material_expressions(depth, "", fade, "SceneDepth")
    editing.connect_material_expressions(pixel, "", fade, "PixelDepth")
    editing.connect_material_expressions(soft_k, "", fade, "K")
    one = sm.node(material, unreal.MaterialExpressionConstant, -900, 1600)
    one.set_editor_property("r", 1.0)
    soft = sm.switch(material, "Soft", (fade, ""), (one, ""), -650, 1400)
    # The color and the opacity.
    rgb = sm.multiply(material, (atlas, "RGB"), (color, ""), -800, 100)
    rgb = sm.multiply(material, (rgb, ""), (tint, "RGB"), -600, 150)
    rgb = sm.multiply(material, (rgb, ""), (unit, ""), -450, 200)
    rgb = sm.multiply(material, (rgb, ""), (scale, ""), -300, 250)
    alpha = sm.multiply(material, (atlas, "A"), (color, "A"), -800, 500)
    alpha = sm.multiply(material, (alpha, ""), (alpha_scale, ""), -600, 550)
    alpha = sm.multiply(material, (alpha, ""), (soft, ""), -450, 600)
    clamp = sm.node(material, unreal.MaterialExpressionSaturate, -300, 600)
    editing.connect_material_expressions(alpha, "", clamp, "")
    if additive:
        rgb = sm.multiply(material, (rgb, ""), (clamp, ""), -150, 300)
    if lit:
        # Lit: the base color is the sprite color x Diffuse (DiffuseLighting); the emission x Emissive (EmissiveLighting).
        base = sm.multiply(material, (atlas, "RGB"), (color, ""), -800, -200)
        base = sm.multiply(material, (base, ""), (tint, "RGB"), -600, -200)
        base = sm.multiply(material, (base, ""), (sm.scalar(material, "Diffuse", 1.0, -800, -350), ""), -450, -200)
        editing.connect_material_property(base, "", MP.MP_BASE_COLOR)
        rgb = sm.multiply(material, (rgb, ""), (sm.scalar(material, "Emissive", 0.0, -450, 350), ""), -150, 300)
    editing.connect_material_property(rgb, "", MP.MP_EMISSIVE_COLOR)
    editing.connect_material_property(clamp, "", MP.MP_OPACITY)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["parent:" + name] = PARENT_VERSION
    report["materials"] += 1
    return material


def instance(parent_material, texture_asset, additive, soft, lit=None):
    """lit: None, or (Diffuse, Emissive) of a lit emitter."""
    name = "MI_" + texture_asset[2:] + ("_Add" if additive else ("_Lit" if lit else "_Alpha")) + ("_Soft" if soft else "")
    if lit:
        name += "_D%g_E%g" % lit
        name = name.replace(".", "p")
    path = f"{ROOT}/Materials/{name}"
    key = f"{PARENT_VERSION}:{texture_asset}:{additive}:{soft}:{lit}"
    if assets.does_asset_exist(path) and state.get("instance:" + name) == key:
        return path + "." + name
    if not assets.does_asset_exist(path):
        tools.create_asset(name, f"{ROOT}/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = assets.load_asset(path)
    editing.set_material_instance_parent(mi, parent_material)
    editing.set_material_instance_texture_parameter_value(mi, "Atlas", assets.load_asset(f"{ROOT}/Textures/{texture_asset}"))
    editing.set_material_instance_static_switch_parameter_value(mi, "Soft", bool(soft))
    if lit:
        editing.set_material_instance_scalar_parameter_value(mi, "Diffuse", float(lit[0]))
        editing.set_material_instance_scalar_parameter_value(mi, "Emissive", float(lit[1]))
    editing.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    state["instance:" + name] = key
    report["instances"] += 1
    return path + "." + name


def build_system(record, parents):
    name = record["asset"]
    path = f"{ROOT}/Systems/{name}"
    if assets.does_asset_exist(path) and state.get("system:" + name) == record["hash"]:
        report["kept"] += 1
        return
    emitters = []
    used = set()
    for emitter in record["emitters"]:
        spec = dict(emitter)
        if spec["enabled"]:
            additive = spec["blend"] == "additive"
            lit = (spec["diffuse"], spec["color"]["emissive"]) if spec.get("lit") else None
            spec["materialPath"] = instance(parents["lit" if lit else additive], spec["texture"], additive, spec["soft"], lit)
            base = "".join(c if c.isalnum() else "_" for c in spec["name"])
            unique = base
            n = 1
            while unique in used:
                n += 1
                unique = f"{base}_{n}"
            used.add(unique)
            spec["emitterName"] = unique
        spec.pop("material", None)
        emitters.append(spec)
    # An existing system keeps its asset: the builder removes its emitters first.
    if assets.does_asset_exist(path):
        system = assets.load_asset(path)
    else:
        system = tools.create_asset(name, f"{ROOT}/Systems", unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())
    errors = unreal.SwcEditorLibrary.build_particle_system(system, json.dumps({"key": record["key"], "emitters": emitters}))
    assets.save_loaded_asset(system, only_if_is_dirty=False)
    if errors:
        report["failed"].append(f"{name}: {errors}")
        return
    state["system:" + name] = record["hash"]
    report["systems"] += 1


try:
    for record in index["textures"].values():
        try:
            import_texture(record)
        except Exception as error:
            report["failed"].append(f"texture {record['asset']}: {str(error)[:200]}")
    parents = {False: parent("M_SwcParticleAlpha", False), True: parent("M_SwcParticleAdditive", True),
               "lit": parent("M_SwcParticleLit", False, lit=True)}
    for record in index["systems"]:
        if record.get("missing"):
            continue
        try:
            build_system(record, parents)
        except Exception as error:
            report["failed"].append(f"{record['asset']}: {str(error)[:300]}")
finally:
    json.dump(state, open(state_path, "w"), indent=1)
    json.dump(report, open(os.path.join(content, "particles", "import-report.json"), "w"), indent=1)
    sm.log(f"particles: {report}")
