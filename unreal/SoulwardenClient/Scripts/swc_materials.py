"""The parent materials of the materials track and the rules of its material instances and textures (docs/MATERIALS.md).

A module for the editor scripts (Scripts/apply_materials.py; later the import pipeline can call it). The specs come
from the converter (converter/swc_convert/material_records.py: SWC_CONTENT/materials/instances.json, textures.json).

Parents (in /SwcGameContent/Base, made by this module, versioned by PARENT_VERSION in import-state.json):
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
PARENT_VERSION = "1"   # the rule of the parents of this module
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


def white_linear(content, state):
    """@return The default texture of the one-channel parameters: 4 x 4 white, TC_Alpha (a sampler of type Alpha needs
    a default texture of the same type)."""
    path = f"{BASE}/T_SwcWhiteLinear"
    if assets.does_asset_exist(path) and state.get("base:T_SwcWhiteLinear") == PARENT_VERSION:
        return assets.load_asset(path)
    png = os.path.join(content, "materials", "base", "T_SwcWhiteLinear.png")
    os.makedirs(os.path.dirname(png), exist_ok=True)
    if not os.path.isfile(png):
        # A grayscale PNG of 4 x 4 white pixels, written with no extra Python module (zlib and the PNG chunks).
        import struct
        import zlib

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        raw = b"".join(b"\x00" + b"\xff" * 4 for _ in range(4))
        with open(png, "wb") as handle:
            handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 0, 0, 0, 0))
                         + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", BASE)
    task.set_editor_property("destination_name", "T_SwcWhiteLinear")
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(path)
    texture_settings(texture, "linear")
    assets.save_loaded_asset(texture)
    state["base:T_SwcWhiteLinear"] = PARENT_VERSION
    return texture


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


def foliage_parent(content, state):
    """@return M_SwcFoliage (made again when PARENT_VERSION changes; its instances keep their parameters)."""
    name = "M_SwcFoliage"
    path = f"{BASE}/{name}"
    if assets.does_asset_exist(path) and state.get("base:" + name) == PARENT_VERSION:
        return assets.load_asset(path)
    white = white_linear(content, state)
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        editing.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, BASE, unreal.Material, unreal.MaterialFactoryNew())
    st = unreal.MaterialSamplerType
    color = sampler(material, "BaseColor", st.SAMPLERTYPE_COLOR,
                    unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), -900, -300)
    diffuse = vector(material, "DiffuseColor", (1.0, 1.0, 1.0, 1.0), -900, -50)
    albedo = node(material, unreal.MaterialExpressionMultiply, -500, -200)
    editing.connect_material_expressions(color, "RGB", albedo, "A")
    editing.connect_material_expressions(diffuse, "RGB", albedo, "B")
    editing.connect_material_property(albedo, "", MP.MP_BASE_COLOR)
    editing.connect_material_property(color, "A", MP.MP_OPACITY_MASK)
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
    editing.connect_material_property(specular, "", MP.MP_SPECULAR)
    transmission = sampler(material, "Transmission", st.SAMPLERTYPE_ALPHA, white, -900, 850)
    back = scalar(material, "BackDiffuseMultiplier", 1.0, -900, 1100)
    amount = node(material, unreal.MaterialExpressionMultiply, -600, 900)
    editing.connect_material_expressions(transmission, "R", amount, "A")
    editing.connect_material_expressions(back, "", amount, "B")
    clamp = node(material, unreal.MaterialExpressionSaturate, -450, 900)
    editing.connect_material_expressions(amount, "", clamp, "")
    tint = vector(material, "TransmittanceColor", (1.0, 1.0, 1.0, 1.0), -600, 1100)
    subsurface = node(material, unreal.MaterialExpressionMultiply, -300, 950)
    editing.connect_material_expressions(clamp, "", subsurface, "A")
    editing.connect_material_expressions(tint, "RGB", subsurface, "B")
    editing.connect_material_property(subsurface, "", MP.MP_SUBSURFACE_COLOR)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_nanite", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)
    state["base:" + name] = PARENT_VERSION
    return material


def spec_stamp(spec):
    return INSTANCE_VERSION + ":" + PARENT_VERSION + ":" + hashlib.sha1(
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
    if parent is not None:
        overrides = instance.get_editor_property("base_property_overrides")
        overrides.set_editor_property("override_two_sided", True)
        overrides.set_editor_property("two_sided", bool(spec.get("two sided")))
        if "cutoff" in spec:
            overrides.set_editor_property("override_opacity_mask_clip_value", True)
            overrides.set_editor_property("opacity_mask_clip_value", float(spec["cutoff"]))
        instance.set_editor_property("base_property_overrides", overrides)
    editing.update_material_instance(instance)


def load_specs(content):
    """@return (the specs, the texture records) of SWC_CONTENT/materials (empty when the converter wrote none)."""
    folder = os.path.join(content, "materials")
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


def apply_specs(keys, state, force=True, content=None, max_seconds=0.0, report=None):
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
    specs, textures = load_specs(content)
    todo = plan(keys, specs, state, force)
    report["instances"]["up to date"] += len({k for k in keys if (k.split(":", 1)[-1]) in specs}) - len(todo)
    if not todo:
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
    parents = {"foliage": foliage_parent(content, state)}
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
