"""The particle systems of the converter (AST-210, docs/PARTICLES.md "The systems"), into /SwcGameContent/Particles.

Run through tools/import-particles.sh (UnrealEditor-Cmd -run=pythonscript, no window). The input is SWC_CONTENT/particles/systems.json
(tools/convert-particles.sh systems): the textures of the systems (sRGB PNG) and, for each effect root, its emitters with the resolved
source values. The assets are made from the game copy: they go to the shared content plugin, not into git.

- Textures/T_*: the atlases (sRGB, the default compression).
- M_SwcParticleAlpha (Translucent) and M_SwcParticleAdditive (Additive): unlit sprite materials (the stage 2 fire). The color of a
  sprite is the atlas sample (ParticleSubUV: the frame blend of the renderer) x the particle color (the source color x the age color x
  EmissiveLighting x the glow of the .mtl: the HDR color of the builder) x Tint (the Color of the placement: a material binding of
  User.SwcTint) x Ambient (EmissiveLighting), plus the glow term; x EmissionUnit (1: source shader units) x EmissiveScale of MPC_SwcMaterials (10,000 cd/m2 x
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
PARENT_VERSION = "6"

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
    """@return MPC_SwcParticles: EmissionUnit (the scale of the source shader units to the engine units of EmissiveScale; default 1:
    the two color terms are in source shader units, particle-rules.md 1; at run time swc.Effects.Emission)."""
    name = "MPC_SwcParticles"
    path = f"{ROOT}/{name}"
    if assets.does_asset_exist(path) and state.get("collection:" + name) == "3":
        return assets.load_asset(path)
    mpc = assets.load_asset(path) if assets.does_asset_exist(path) else \
        tools.create_asset(name, ROOT, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    state["collection:" + name] = "3"
    scalars = []
    for parameter, default in (("EmissionUnit", 1.0), ("TodBrightness", 1.0)):
        value = unreal.CollectionScalarParameter()
        value.set_editor_property("parameter_name", parameter)
        value.set_editor_property("default_value", default)
        scalars.append(value)
    mpc.set_editor_property("scalar_parameters", scalars)
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
    # The opacity: the clip of the atlas alpha by the particle alpha (one fade, particle-rules.md 2; AST-230: the AlphaClip rows, x = Scale,
    # y = SourceMin, z = SourceWidth between their Min at the particle alpha 0 and their Max at 1; the defaults 0 to 1, 0, 1 give the atlas
    # alpha x the particle alpha) x AlphaScale x the soft fade. The "" output of a vector parameter is its RGB: SourceMin Max is its A.
    # The rows of research/particle-impacts.md 2 [R]: K = Amax x L (Amax: the largest base alpha, ClipW.z; L: AlphaScale), p = the particle
    # alpha / Amax; q = row0 + row1 x p; w = min(max(q.z, 0.001), 1 - q.y); a = atlas alpha - q.y (0 at or below 0.01); alpha =
    # saturate(min(a, w) x q.x / w). K stays inside the saturate (no second fade).
    clip_a = sm.vector(material, "ClipA", (0.0, 1.0, 0.0, 0.0), -1200, 700)
    clip_w = sm.vector(material, "ClipW", (1.0, 1.0, 1.0, 0.0), -1200, 850)
    alpha = sm.custom(material, "float amax = max(W.z, 1e-4); float p = saturate(P / amax);\n"
                      "float qx = amax * L * lerp(C.x, C.y, p); float qy = lerp(C.z, CA, p); float qz = lerp(W.x, W.y, p);\n"
                      "float w = min(max(qz, 0.001), 1.0 - qy); if (w <= 0.0) return 0.0;\n"
                      "float a = T - qy; return a <= 0.01 ? 0.0 : saturate(min(a, w) * qx / w);",
                      unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("T", "P", "C", "CA", "W", "L"), -800, 500)
    for source_node, output, pin in ((atlas, "A", "T"), (color, "A", "P"), (clip_a, "", "C"), (clip_a, "A", "CA"), (clip_w, "", "W"),
                                     (alpha_scale, "", "L")):
        editing.connect_material_expressions(source_node, output, alpha, pin)
    alpha = sm.multiply(material, (alpha, ""), (soft, ""), -450, 600)
    clamp = sm.node(material, unreal.MaterialExpressionSaturate, -300, 600)
    editing.connect_material_expressions(alpha, "", clamp, "")
    # The two terms of the source shader (particle-rules.md 1), in source units x EmissionUnit x EmissiveScale:
    # ambient = the atlas RGB x the particle color x Tint x Ambient (EmissiveLighting); glow = the opacity x the atlas RGB x the glow map
    # (the Detail slot at the same atlas frame) x GlowColor (0.1 x Emittance.rgb x Emittance.a; no particle color, no tint).
    # The material Diffuse (research/particle-impacts.md 1 [R]: PM_DiffuseColor on the atlas RGB of the lit and the glow terms).
    mat_diffuse = sm.vector(material, "MatDiffuse", (1.0, 1.0, 1.0, 1.0), -1200, -800)
    diffuse_rgb = sm.multiply(material, (atlas, "RGB"), (mat_diffuse, ""), -1000, -100)
    ambient = sm.multiply(material, (diffuse_rgb, ""), (color, ""), -800, 100)
    ambient = sm.multiply(material, (ambient, ""), (tint, "RGB"), -600, 150)
    ambient = sm.multiply(material, (ambient, ""), (sm.scalar(material, "Ambient", 0.0, -800, 250), ""), -450, 150)
    glow_map = sm.node(material, unreal.MaterialExpressionTextureSampleParameterSubUV, -1200, -500)
    glow_map.set_editor_property("parameter_name", "Glow")
    glow_map.set_editor_property("blend", True)
    glow_map.set_editor_property("texture", atlas.get_editor_property("texture"))
    glow = sm.multiply(material, (diffuse_rgb, ""), (glow_map, "RGB"), -800, -500)
    glow = sm.multiply(material, (glow, ""), (sm.vector(material, "GlowColor", (0.0, 0.0, 0.0, 0.0), -1000, -650), "RGB"), -600, -500)
    glow = sm.multiply(material, (glow, ""), (clamp, ""), -450, -500)
    rgb = sm.node(material, unreal.MaterialExpressionAdd, -300, 100)
    editing.connect_material_expressions(ambient, "", rgb, "A")
    editing.connect_material_expressions(glow, "", rgb, "B")
    # The time-of-day factor F = 1 + (B - 1) x TodInfluence on both terms (r_ParticlesTOD 2; particle-rules2.md a): B is TodBrightness of
    # MPC_SwcParticles (the evaluated "Particle Brightness Multiplier" of the scene preset, set at run time), TodInfluence of the instance.
    tod_b = sm.node(material, unreal.MaterialExpressionCollectionParameter, -1200, 1750)
    tod_b.set_editor_property("collection", particle_collection())
    tod_b.set_editor_property("parameter_name", "TodBrightness")
    tod_f = sm.custom(material, "return 1.0 + (B - 1.0) * I;", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ("B", "I"), -900, 1750)
    editing.connect_material_expressions(tod_b, "", tod_f, "B")
    editing.connect_material_expressions(sm.scalar(material, "TodInfluence", 1.0, -1200, 1900), "", tod_f, "I")
    rgb = sm.multiply(material, (rgb, ""), (tod_f, ""), -250, 180)
    rgb = sm.multiply(material, (rgb, ""), (unit, ""), -200, 200)
    rgb = sm.multiply(material, (rgb, ""), (scale, ""), -100, 250)
    if additive:
        # The non-alpha path of the source multiplies the final RGB by the final alpha [R].
        rgb = sm.multiply(material, (rgb, ""), (clamp, ""), -50, 300)
    if lit:
        # Lit: the scene light term, the base color = the sprite color x Tint x Diffuse (DiffuseLighting; the light volume of Unreal at
        # each vertex in place of the source light volume and probe, an approximation).
        base = sm.multiply(material, (diffuse_rgb, ""), (color, ""), -800, -200)
        base = sm.multiply(material, (base, ""), (tint, "RGB"), -600, -200)
        base = sm.multiply(material, (base, ""), (sm.scalar(material, "Diffuse", 1.0, -800, -350), ""), -450, -200)
        base = sm.multiply(material, (base, ""), (tod_f, ""), -350, -200)
        editing.connect_material_property(base, "", MP.MP_BASE_COLOR)
    editing.connect_material_property(rgb, "", MP.MP_EMISSIVE_COLOR)
    editing.connect_material_property(clamp, "", MP.MP_OPACITY)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["parent:" + name] = PARENT_VERSION
    report["materials"] += 1
    return material


MESH_PARENT_VERSION = "2"


def mesh_parent():
    """M_SwcParticleMesh (AST-230): the Illum material of a mesh particle (the swipes). Translucent, lit by the translucency light
    volume at each vertex (no direction; as M_SwcParticleLit), two-sided. The source Illum terms (docs/MATERIALS.md, docs/EFFECTS.md):
    base color = the diffuse map x Diffuse x the vertex color x the particle color x Tint; emission = Emittance RGB x Emittance A x 0.1 x
    the emittance map RGB x EmissiveScale of MPC_SwcMaterials (the unit rule of the world materials, M4); opacity = the diffuse alpha x
    Opacity x the vertex alpha x the particle alpha x AlphaScale. The UV of each map: TexCoord x tile + offset of its TexMod (no
    oscillator). Not done: the dissolve (MaterialDissolve), the Illum specular."""
    name = "M_SwcParticleMesh"
    path = f"{ROOT}/{name}"
    if assets.does_asset_exist(path) and state.get("parent:" + name) == MESH_PARENT_VERSION:
        return assets.load_asset(path)
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        editing.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_PER_VERTEX_NON_DIRECTIONAL)
    material.set_editor_property("two_sided", True)
    editing.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
    default_tex = assets.load_asset(f"{ROOT}/Textures/{index['textures'][next(iter(index['textures']))]['asset']}")
    uv = sm.node(material, unreal.MaterialExpressionTextureCoordinate, -1700, 0)

    def mapped(parameter, transform, y):
        xf = sm.vector(material, transform, (1.0, 1.0, 0.0, 0.0), -1700, y + 150)
        # The "" output of a vector parameter is its RGB: the offset V comes from its A output.
        code = sm.custom(material, "return UV * T.xy + float2(T.z, W);", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ("UV", "T", "W"),
                         -1450, y)
        editing.connect_material_expressions(uv, "", code, "UV")
        editing.connect_material_expressions(xf, "", code, "T")
        editing.connect_material_expressions(xf, "A", code, "W")
        sample = sm.sampler(material, parameter, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, default_tex, -1200, y)
        editing.connect_material_expressions(code, "", sample, "UVs")
        return sample
    diffuse = mapped("Diffuse", "DiffuseTransform", 0)
    emit_map = mapped("Emittance", "EmittanceTransform", 500)
    color = sm.node(material, unreal.MaterialExpressionParticleColor, -1200, 900)
    vertex = sm.node(material, unreal.MaterialExpressionVertexColor, -1200, 1100)
    tint = sm.vector(material, "Tint", (1.0, 1.0, 1.0, 1.0), -1200, 1300)
    diffuse_color = sm.vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -1200, 1450)
    emit_color = sm.vector(material, "EmittanceColor", (0.0, 0.0, 0.0, 0.0), -1200, 1600)
    opacity = sm.scalar(material, "Opacity", 1.0, -1200, 1750)
    alpha_scale = sm.scalar(material, "AlphaScale", 1.0, -1200, 1850)
    scale = sm.node(material, unreal.MaterialExpressionCollectionParameter, -1200, 1950)
    scale.set_editor_property("collection", sm.collection(content, state))
    scale.set_editor_property("parameter_name", "EmissiveScale")
    base = sm.multiply(material, (diffuse, "RGB"), (diffuse_color, "RGB"), -800, 0)
    base = sm.multiply(material, (base, ""), (vertex, ""), -650, 50)
    base = sm.multiply(material, (base, ""), (color, ""), -500, 100)
    base = sm.multiply(material, (base, ""), (tint, "RGB"), -350, 150)
    emit = sm.custom(material, "return C * I * 0.1 * M * S;", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ("C", "I", "M", "S"), -700, 500)
    editing.connect_material_expressions(emit_color, "", emit, "C")
    editing.connect_material_expressions(emit_color, "A", emit, "I")
    editing.connect_material_expressions(emit_map, "RGB", emit, "M")
    editing.connect_material_expressions(scale, "", emit, "S")
    alpha = sm.custom(material, "return saturate(D * O * V * P * A);", unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                      ("D", "O", "V", "P", "A"), -700, 900)
    for source_node, output, pin in ((diffuse, "A", "D"), (opacity, "", "O"), (vertex, "A", "V"), (color, "A", "P"), (alpha_scale, "", "A")):
        editing.connect_material_expressions(source_node, output, alpha, pin)
    editing.connect_material_property(base, "", MP.MP_BASE_COLOR)
    editing.connect_material_property(emit, "", MP.MP_EMISSIVE_COLOR)
    editing.connect_material_property(alpha, "", MP.MP_OPACITY)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["parent:" + name] = MESH_PARENT_VERSION
    report["materials"] += 1
    return material


def mesh_instance(parent_material, spec):
    """One instance of M_SwcParticleMesh for each look of a mesh particle."""
    import hashlib
    illum = spec["illum"]
    values = {"diffuse": spec["texture"], "emittance": illum.get("emittanceMap") or spec["texture"],
              "emit": illum["emittance"] if illum.get("emittanceMap") else [0.0, 0.0, 0.0, 0.0],
              "diffuseColor": illum["diffuseColor"], "opacity": illum["opacity"],
              "dt": illum["diffuseTransform"], "et": illum["emittanceTransform"]}
    digest = hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()[:8]
    name = "MI_" + spec["texture"][2:] + "_Mesh_" + digest
    path = f"{ROOT}/Materials/{name}"
    key = f"{MESH_PARENT_VERSION}:{digest}"
    if assets.does_asset_exist(path) and state.get("instance:" + name) == key:
        return path + "." + name
    if not assets.does_asset_exist(path):
        tools.create_asset(name, f"{ROOT}/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = assets.load_asset(path)
    editing.set_material_instance_parent(mi, parent_material)
    editing.set_material_instance_texture_parameter_value(mi, "Diffuse", assets.load_asset(f"{ROOT}/Textures/{values['diffuse']}"))
    editing.set_material_instance_texture_parameter_value(mi, "Emittance", assets.load_asset(f"{ROOT}/Textures/{values['emittance']}"))
    editing.set_material_instance_vector_parameter_value(mi, "EmittanceColor", unreal.LinearColor(*values["emit"]))
    editing.set_material_instance_vector_parameter_value(mi, "DiffuseColor", unreal.LinearColor(*values["diffuseColor"], 1.0))
    editing.set_material_instance_vector_parameter_value(mi, "DiffuseTransform", unreal.LinearColor(*values["dt"]))
    editing.set_material_instance_vector_parameter_value(mi, "EmittanceTransform", unreal.LinearColor(*values["et"]))
    editing.set_material_instance_scalar_parameter_value(mi, "Opacity", float(values["opacity"]))
    editing.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    state["instance:" + name] = key
    report["instances"] += 1
    return path + "." + name


def import_mesh(record):
    """The static mesh of a mesh particle, in its own folder /SwcGameContent/Particles/Meshes/SM_* (the mesh pipeline P_SwcMesh of the
    content import: no Nanite, no new material, no texture; the Niagara mesh renderer sets the material). Interchange builds the mesh
    on worker threads: the save of the folder waits for it (the rule of import_content.py). @return Its object path."""
    folder = f"{ROOT}/Meshes/{record['asset']}"
    path = os.path.join(content, record["file"])
    stamp = sm.file_stamp(path)
    mesh = sm.first_asset(folder, "StaticMesh") if assets.does_directory_exist(folder) else None
    if mesh is None or state.get("mesh:" + record["asset"]) != stamp:
        parameters = unreal.ImportAssetParameters()
        parameters.set_editor_property("is_automated", True)
        parameters.set_editor_property("replace_existing", True)
        parameters.set_editor_property("override_pipelines", [unreal.SoftObjectPath(sm.BASE + "/P_SwcMesh")])
        unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
            folder, unreal.InterchangeManager.create_source_data(path), parameters)
        assets.save_directory(folder, only_if_is_dirty=False, recursive=True)
        mesh = sm.first_asset(folder, "StaticMesh")
        if mesh is None:
            raise RuntimeError(f"{record['asset']}: no static mesh after the import")
        state["mesh:" + record["asset"]] = stamp
        report["meshes"] = report.get("meshes", 0) + 1
    return mesh.get_path_name()


def instance(parent_material, spec, additive):
    """One instance for each look: the atlas, the glow map, the blend, the soft fade, and the values of the two color terms."""
    import hashlib
    values = {"atlas": spec["texture"], "glow": spec.get("glowTexture") or spec["texture"], "soft": bool(spec["soft"]),
              "ambient": spec["ambient"], "glowColor": spec["glowColor"], "diffuse": spec["diffuse"] if spec.get("lit") else None,
              "tod": spec.get("tod", 1.0), "clip": spec.get("alphaClip"), "matDiffuse": spec.get("matDiffuse", [1.0, 1.0, 1.0]),
              "parent": parent_material.get_name()}
    digest = hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()[:8]
    name = "MI_" + spec["texture"][2:] + ("_Add" if additive else ("_Lit" if spec.get("lit") else "_Alpha")) + "_" + digest
    path = f"{ROOT}/Materials/{name}"
    key = f"{PARENT_VERSION}:{digest}"
    if assets.does_asset_exist(path) and state.get("instance:" + name) == key:
        return path + "." + name
    if not assets.does_asset_exist(path):
        tools.create_asset(name, f"{ROOT}/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = assets.load_asset(path)
    editing.set_material_instance_parent(mi, parent_material)
    editing.set_material_instance_texture_parameter_value(mi, "Atlas", assets.load_asset(f"{ROOT}/Textures/{values['atlas']}"))
    editing.set_material_instance_texture_parameter_value(mi, "Glow", assets.load_asset(f"{ROOT}/Textures/{values['glow']}"))
    editing.set_material_instance_static_switch_parameter_value(mi, "Soft", values["soft"])
    editing.set_material_instance_scalar_parameter_value(mi, "Ambient", float(values["ambient"]))
    editing.set_material_instance_vector_parameter_value(mi, "GlowColor", unreal.LinearColor(*values["glowColor"], 0.0))
    editing.set_material_instance_scalar_parameter_value(mi, "TodInfluence", float(values["tod"]))
    clip = values["clip"] or {"scale": [0.0, 1.0], "sourceMin": [0.0, 0.0], "width": [1.0, 1.0], "amax": 1.0}
    editing.set_material_instance_vector_parameter_value(mi, "ClipA", unreal.LinearColor(clip["scale"][0], clip["scale"][1], clip["sourceMin"][0],
                                                                                          clip["sourceMin"][1]))
    editing.set_material_instance_vector_parameter_value(mi, "ClipW", unreal.LinearColor(clip["width"][0], clip["width"][1], clip.get("amax", 1.0), 0.0))
    editing.set_material_instance_vector_parameter_value(mi, "MatDiffuse", unreal.LinearColor(*values["matDiffuse"], 1.0))
    if values["diffuse"] is not None:
        editing.set_material_instance_scalar_parameter_value(mi, "Diffuse", float(values["diffuse"]))
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
        if spec["enabled"] and spec.get("renderer") == "mesh":
            spec["meshPath"] = import_mesh(spec["mesh"])
            spec["materialPath"] = mesh_instance(parents["mesh"], spec)
        elif spec["enabled"]:
            additive = spec["blend"] == "additive"
            spec["materialPath"] = instance(parents["lit" if spec.get("lit") else additive], spec, additive)
        if spec["enabled"]:
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


# AST-230: a time budget for each call (the import lock is shared; SWC_IMPORT_SECONDS, default 480 s): the work left waits for the next
# call (the state file keeps what is done).
import time  # noqa: E402
deadline = time.time() + float(os.environ.get("SWC_IMPORT_SECONDS", "480"))
report["deferred"] = 0
try:
    for record in index["textures"].values():
        if time.time() > deadline:
            report["deferred"] += 1
            continue
        try:
            import_texture(record)
        except Exception as error:
            report["failed"].append(f"texture {record['asset']}: {str(error)[:200]}")
    parents = {False: parent("M_SwcParticleAlpha", False), True: parent("M_SwcParticleAdditive", True),
               "lit": parent("M_SwcParticleLit", False, lit=True), "mesh": mesh_parent()}
    for record in index["systems"]:
        if record.get("missing"):
            continue
        if time.time() > deadline:
            report["deferred"] += 1
            continue
        try:
            build_system(record, parents)
        except Exception as error:
            report["failed"].append(f"{record['asset']}: {str(error)[:300]}")
finally:
    json.dump(state, open(state_path, "w"), indent=1)
    json.dump(report, open(os.path.join(content, "particles", "import-report.json"), "w"), indent=1)
    sm.log(f"particles: {report}")
