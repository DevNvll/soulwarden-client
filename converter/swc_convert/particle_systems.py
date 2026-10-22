"""The systems of the particle effects for the importer (AST-210, docs/PARTICLES.md "The systems").

For each requested effect root: one system, one emitter for each Particles node of its tree that draws (a node with Alpha 0 and
no texture is a controller: it has no emitter, its children stay). The values are the resolved values of particles.params_of
(the defaults, then the own attributes) in the units of Unreal: cm, cm/s, cm/s2, degrees. A node keeps the list of its
attributes that the client does not express (particles.SUPPORT), with their raw text: the support report of the effect.

The rules of the map (research "The parameters"; [R] retail metadata, [I] inference of this stage):
- Axes: the local frame of the source (x, y, z) is the local frame of the component in Unreal as (x, -y, z) x 100 (space.py).
- The emit axis is local +Z ("Angle to vary focus from default (Z axis)" [R]); FocusGravityDir uses the world up [R]. EmitAngle
  is the cone; its Random sets the inner angle ("RandomVar determines min angle" [R]). A fixed focus rotates the axis
  (research/particle-rules3.md 5 [R]); a random focus widens the cone [I].
- A size is a half size: the sprite is 2 x Size x 100 cm (the source quad is -1..1 [R]). The width is SizeX, the height SizeY
  (MaintainAspectRatio: SizeY = SizeX). OrientToVelocity aligns the particle X axis to the velocity [R]: the length along the
  velocity is SizeX.
- A scalar random: [base x (1 - Random), base] [R]. The age curves: 32 samples of the source spline (particles.evaluate).
- Count and life: a continuous emitter spawns Count / life per second [R: the nominal rate, research "Timing and motion"]; a
  burst emitter spawns Count at its start, once with a finite life and no PulsePeriod (research/particles.md "Timing" [R]).
  EmitterLifeTime 0 is endless; PulsePeriod loops. A continuous emitter births its first particle at its start (particle-rules3.md 3).
- A LightSource adds a light twin record to the sprite (particle-rules3.md 4 [R]). A trail (EmitterShape TRAIL) is a ribbon of the
  client (Effects/SwcRibbons). A moving camera-facing particle with Stretch aligns to its velocity (particle-rules3.md 1 [R], [I]).
- The LOD bands: the base node shows below the first LOD distance, each LOD block from its distance; an emitter shows in the
  bands where it is enabled. The first pass keeps the outer bound of the enabled bands (an emitter that turns on again farther
  away is reported). CameraMinDistance and CameraMaxDistance are bounds too.
- Gravity: -9.8 x GravityScale m/s2 along the world Z [R]; Acceleration in m/s2.
- The color of a particle: Color base x the age color x EmissiveLighting (the HDR color of the sprite; the material multiplies
  the texture and the global emission scale). The alpha: Alpha base x the age alpha.
- The material: the .mtl of the Material attribute (its Diffuse texture, its Emittance RGB [linear, no sRGB decode: XML colors
  are linear, docs/MATERIALS.md]); else the Texture attribute. A .tif name is the .dds product. A texture file that is sRGB keeps
  its bytes; a linear file is encoded to sRGB for the import (the material samples all particle textures as sRGB) [I].
"""
from __future__ import annotations

import json
import math
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

from . import env, material_records, source
from .particles import (CONVERTER_VERSION, Libraries, field, flag, number, out_dir, params_of, parse_value, random_range, sample,
                        sample_color, color_keys, scalar_param, support_status, vector, write_json)

# The effects of stage 2 (the fire): the menu campfire and candles, the torches, the candles of the world.
STAGE2 = ["Buildables.Campfire.Idle_Medium", "Buildables.Torch.Idle", "Buildables.Global.Candle_Flame_01",
          "Bld_Global_Ambience.Candle01"]
SAMPLES = 32
TEXTURE_CAP = 2048
TEXTURE_VERSION = 1
GRAVITY = 9.8   # m/s2 [R: gpuparticleupdate, -9.8 x GravityScale]
# The view distance of an effect with no LOD end (cm) [I: the source view distance rule is not traced; docs/PARTICLES.md].
DEFAULT_VIEW = 5000.0


def product(name: str) -> str | None:
    """@return The pak product of a texture name of the data (.tif -> .dds), or None."""
    if not name:
        return None
    name = name.lower().replace("\\", "/")
    stem = name.rsplit(".", 1)[0] if "." in name.rsplit("/", 1)[-1] else name
    for candidate in (stem + ".dds", name):
        if source.has(candidate):
            return candidate
    return None


def material(path: str) -> dict | None:
    """@return The facts of a particle .mtl: the shader, the flags, the diffuse and glow products, the Diffuse and Emittance."""
    if not path:
        return None
    name = path.lower().replace("\\", "/")
    name = name if name.endswith(".mtl") else name + ".mtl"
    if not source.has(name):
        return {"path": name, "missing": True}
    root = ET.fromstring(source.read(name))
    if root.find("SubMaterials") is not None:
        root = root.find("SubMaterials")[0]
    textures = {t.get("Map", ""): t.get("File", "") for t in root.findall("Textures/Texture")}
    texmods = {}
    for t in root.findall("Textures/Texture"):
        mod = t.find("TexMod")
        if mod is not None:
            texmods[t.get("Map", "")] = {k: float(mod.get(k)) for k in ("TileU", "TileV", "OffsetU", "OffsetV") if mod.get(k) is not None}

    def floats(text: str) -> list[float]:
        return [float(v) for v in (text or "").split(",") if v.strip()]

    # The flags of the engine rule (material_records.flags_of): the StringGenMask text, else the bits of the numeric GenMask through the
    # .ext of the shader (AST-230: the refraction sprite of wFX_Bow_Huntsman.ExplosiveShot has only GenMask 404, %REFRACTION%NORMAL_MAP).
    flags = [f.lstrip("%") for f in material_records.flags_of(root, resources=False)[0]]
    return {"path": name, "shader": root.get("Shader", ""), "flags": flags,
            "diffuse": product(textures.get("Diffuse", "")), "textures": textures,
            "diffuseColor": floats(root.get("Diffuse", "1,1,1,1"))[:3], "emittance": floats(root.get("Emittance", "0,0,0,0")),
            "opacity": float(root.get("Opacity", "1")), "texmods": texmods}


def texture(product_path: str, out: Path) -> dict:
    """Writes the PNG of a texture product (sRGB bytes) and @return its record."""
    from PIL import Image

    from . import dds
    from .material_records import file_srgb
    name = product_path[:-4]
    png = out / f"t{TEXTURE_VERSION}" / (name + ".png")
    srgb = file_srgb(product_path)
    if not png.is_file():
        data, w, h, fmt = dds.join(source.read, product_path, TEXTURE_CAP)
        image = dds.to_image(data, w, h, fmt).convert("RGBA")
        if not srgb:
            a = np.asarray(image).astype(np.float64) / 255.0
            rgb = a[..., :3]
            a[..., :3] = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * np.power(rgb, 1 / 2.4) - 0.055)
            image = Image.fromarray(np.clip(np.round(a * 255.0), 0, 255).astype(np.uint8), "RGBA")
        png.parent.mkdir(parents=True, exist_ok=True)
        image.save(png)
    with Image.open(png) as image:
        size = image.size
    asset = "T_" + "_".join(Path(name).parts[-2:]).replace(".", "_").replace("-", "_")
    return {"product": product_path, "file": str(png.relative_to(env.content_dir())), "srgbSource": srgb, "size": list(size),
            "asset": asset}


def lod_bands(node: dict) -> tuple[list[list[float]], list[str]]:
    """@return The distance bands (m) in which a node shows, and the notes (research "The worked examples", LOD rule)."""
    edges = [0.0] + [lod["distance"] for lod in node["lods"]] + [math.inf]
    states = [flag(params_of(node["params"]).get("Enabled", "true"))]
    states += [flag(params_of(lod["params"]).get("Enabled", "true")) and lod.get("active", "1") != "0" for lod in node["lods"]]
    bands = [[edges[i], edges[i + 1]] for i, on in enumerate(states) if on]
    notes = []
    merged: list[list[float]] = []
    for band in bands:
        if merged and merged[-1][1] == band[0]:
            merged[-1][1] = band[1]
        else:
            merged.append(list(band))
    if len(merged) > 1:
        notes.append("the emitter turns on again beyond a disabled band: the first pass keeps the outer bound")
    return merged, notes


def range_cm(params: dict, name: str, scale: float = 100.0) -> list[float]:
    p = field(params, name)
    return random_range(p["base"] * scale, p["random"])


# The material shaders of the mesh particles that the mesh material (M_SwcParticleMesh, the Illum terms) does not draw (AST-230).
UNSUPPORTED_MESH_SHADERS = ("Fxmeshadvancedtransp", "Fxmeshadvanced", "Particleimposter")


def emitter(node: dict, libs: Libraries, tex_out: Path, textures: dict) -> dict:
    """@return The importer record of one Particles node."""
    p = params_of(node["params"])
    unsupported = {}
    for name, raw in node["params"].items():
        status, _ = support_status(name)
        if status not in ("used", "editor"):
            unsupported[name] = {"status": status, "raw": raw}
    life = field(p, "ParticleLifeTime")
    life_range = random_range(life["base"] or 0.0, life["random"])
    count = field(p, "Count")
    count_base = count["base"]
    continuous = flag(p["Continuous"])
    mean_life = sum(life_range) / 2 if life_range[1] > 0 else 0.0
    spawn = ({"type": "rate", "rate": random_range(count_base / life["base"], count["random"]) if (life["base"] or 0) > 0 else [0.0, 0.0]}
             if continuous else {"type": "burst", "amount": [int(round(v)) for v in random_range(count_base, count["random"])]})
    facing = str(p["Facing"])
    velocity_aligned = flag(p.get("OrientToVelocity", "false"))
    alpha = field(p, "Alpha")
    alpha_base = alpha["base"]
    color = parse_value(p["Color"])
    base = color["base"]
    rgb = [base["vector"].get(c, 1.0) for c in "xyz"] if isinstance(base, dict) and "vector" in base else [float(base or 1.0)] * 3
    age = color["fields"].get("ParticleAge")
    color_rand = color["fields"].get("Random", 0.0)
    emissive = number(p["EmissiveLighting"], 0.0)
    # The atlas.
    tiling = parse_value(p.get("TextureTiling", ""))["fields"]
    tiles = [int(tiling.get("TilesX", 1) or 1), int(tiling.get("TilesY", 1) or 1)]
    frames = int(tiling.get("AnimFramesCount", 0) or 0)
    atlas = {"tiles": tiles, "firstTile": int(tiling.get("FirstTile", 0) or 0), "frames": frames,
             "variants": int(tiling.get("VariantCount", 1) or 1), "fps": float(tiling.get("AnimFramerate", 0) or 0),
             "cycle": str(tiling.get("AnimCycle", "Once")), "blend": flag(tiling.get("AnimBlend", "false")),
             "flipU": float(tiling.get("HorizontalFlipChance", 0) or 0), "flipV": float(tiling.get("VerticalFlipChance", 0) or 0)}
    # The material and the texture.
    mtl = material(p.get("Material", ""))
    diffuse = (mtl or {}).get("diffuse") or product(p.get("Texture", ""))
    tex = None
    if diffuse:
        if diffuse not in textures:
            textures[diffuse] = texture(diffuse, tex_out)
        tex = textures[diffuse]["asset"]
    # The size (cm, full sprite: the source size is a half size [R, particle-rules.md 3, particle-rules2.md c]) and its age curves. SizeX
    # is the width (U), SizeY the height (V); with OrientToVelocity the height follows the velocity [R]. The atlas cell aspect reduces one
    # axis: aspectX = min(cellW / cellH, 1), aspectY = min(cellH / cellW, 1) [R]. MaintainAspectRatio copies only the X random to Y [R]
    # (the client draws the two randoms apart: open).
    sx, sy = field(p, "SizeX"), field(p, "SizeY")
    cell = 1.0
    if tex and textures[diffuse]["size"][0] > 0:
        w, h = textures[diffuse]["size"]
        cell = (w / tiles[0]) / (h / tiles[1])
    aspect_x, aspect_y = min(cell, 1.0), min(1.0 / cell, 1.0)
    size = {"x": random_range(2 * 100 * sx["base"] * aspect_x, sx["random"]),
            "y": random_range(2 * 100 * sy["base"] * aspect_y, sy["random"]),
            "curveX": sample(sx["age"], SAMPLES), "curveY": sample(sy["age"], SAMPLES)}
    # The two color terms of the source particle shader (particle-rules.md 1) [R]: the ambient term Ambient = EmissiveLighting (+ the scene
    # light x DiffuseLighting: the lit material) x the particle color x the diffuse map, and the glow term diffuseAlpha x diffuse x glow map
    # (the Detail slot, at the same atlas frame) x ParticleEmissiveColor = 0.1 x Emittance.rgb x Emittance.a of the material (GLOW_MAP).
    glow_color = [0.0, 0.0, 0.0]
    glow_tex = None
    if mtl and not mtl.get("missing") and "GLOW_MAP" in mtl.get("flags", []) and len(mtl["emittance"]) >= 4:
        glow_color = [round(0.1 * c * mtl["emittance"][3], 6) for c in mtl["emittance"][:3]]
        detail = product(mtl["textures"].get("Detail", ""))
        if detail:
            if detail not in textures:
                textures[detail] = texture(detail, tex_out)
            glow_tex = textures[detail]["asset"]
    if mtl and not mtl.get("missing") and "SIGNED_DISTANCE_FIELD_2D" in mtl.get("flags", []):
        unsupported["Material"] = {"status": "approx", "raw": p.get("Material", ""),
                                   "note": "the 2D signed distance field (a small UV warp and a mask) and the source UV motion are not used"}
    blend = str(p["BlendType"])
    # The motion (cm, s).
    speed = range_cm(p, "Speed")
    emit = field(p, "EmitAngle")
    focus = field(p, "FocusAngle")
    azimuth = field(p, "FocusAzimuth")
    cone = (emit["base"] or 0.0) + (focus["base"] or 0.0)
    inner = (emit["base"] or 0.0) * (1.0 - emit["random"])
    axis = [0.0, 0.0, 1.0]
    if (focus["base"] or 0.0) > 0.0 and not focus["random"] and not azimuth["random"] and not flag(p.get("FocusGravityDir", "false")):
        # AST-230 (research/particle-rules3.md 5 [R]): a fixed focus rotates the emit axis: column 2 of Qxyz(FocusAngle, 0,
        # FocusAzimuth), (sin A sin F, -cos A sin F, cos F) in the source frame (F 90: A 0 -Y, 90 +X, 180 +Y, 270 -X); the cone is
        # EmitAngle about that axis. A random focus keeps the wider cone [I].
        tilt, turn = math.radians(focus["base"]), math.radians(azimuth["base"] or 0.0)
        game = [math.sin(turn) * math.sin(tilt), -math.cos(turn) * math.sin(tilt), math.cos(tilt)]
        axis = [round(game[0], 6), round(-game[1], 6), round(game[2], 6)]
        cone = emit["base"] or 0.0
    gravity = field(p, "GravityScale")
    accel = vector(p.get("Acceleration", ""))
    g = random_range(gravity["base"] or 0.0, gravity["random"])
    drag = field(p, "AirResistance")
    offset = vector(p.get("LocalSpawnPosOffset", ""))
    roff = vector(p.get("LocalSpawnPosRandomOffset", ""))
    angles = vector(p.get("LocalInitAngles", ""))
    rangles = vector(p.get("LocalRandomAngles", ""))
    rate_y = field(p, "RotationRateY")
    pivot = [number(p.get("PivotX", "0")), number(p.get("PivotY", "0"))]
    # The stretch of a velocity-aligned sprite (particle-rules2.md c) [R]: the long axis grows by 2 x speed x T, T = min(Stretch(age),
    # the particle age in s). The stateless emitter has no geometric stretch: the converter adds it to the height curve with the mean
    # speed of the source force rule (v = a/k + (v0 - a/k) e^-kt along the emit axis, S = 1) [I].
    stretch_p = field(p, "Stretch")
    if (not velocity_aligned and stretch_p["base"] > 0 and facing in ("Camera", "") and max(abs(v) for v in speed) > 0
            and not str(p.get("Geometry", "") or "")):
        # AST-230 (research/particle-rules3.md 1 [R]): the native stretch does not need OrientToVelocity: it stretches both render
        # axes of a camera-facing particle along its velocity. The stateless sprite has no such step: the particle aligns to its
        # velocity with the same stretch of its long axis [I] (the flames of the explosion of Explosive Arrow).
        velocity_aligned = True
        notes_stretch = "Stretch with no OrientToVelocity: aligned to the velocity with the stretch [I]"
    else:
        notes_stretch = ""
    if velocity_aligned and stretch_p["base"] > 0 and size["y"][1] > 0 and mean_life > 0:
        stretch_curve = sample(stretch_p["age"], SAMPLES)
        v0 = sum(speed) / 2 / 100.0
        a_up = (-GRAVITY * (g[0] + g[1]) / 2) + accel[2]
        k = sum(random_range(drag["base"] or 0.0, drag["random"])) / 2
        height_m = size["y"][1] / 100.0
        curve = []
        for i, value in enumerate(size["curveY"]):
            t = i / (SAMPLES - 1) * mean_life
            v = (a_up / k + (v0 - a_up / k) * math.exp(-k * t)) if k > 0 else v0 + a_up * t
            T = min(stretch_p["base"] * stretch_curve[i], t)
            curve.append(round(value + 2 * abs(v) * T / height_m, 5))
        size["curveY"] = curve
    diffuse = number(p["DiffuseLighting"], 1.0)
    lit = blend != "Additive" and diffuse > 0.0
    bands, notes = lod_bands(node)
    near = number(p.get("CameraMinDistance", "0"))
    far = number(p.get("CameraMaxDistance", "0"))
    max_m = bands[-1][1] if bands else 0.0
    if far > 0:
        max_m = min(max_m, far)
    record = {
        "key": node["key"], "name": node["name"], "enabled": bool(bands) and alpha_base > 0 and tex is not None,
        "spawn": spawn, "delay": number(p.get("SpawnDelay", "0")), "emitterLife": number(p.get("EmitterLifeTime", "0")),
        "pulse": number(p.get("PulsePeriod", "0")), "life": life_range, "meanLife": mean_life,
        "size": size, "alpha": {"base": random_range(alpha_base, alpha["random"]), "curve": sample(alpha["age"], SAMPLES)},
        "lit": lit, "diffuse": diffuse, "ambient": emissive, "glowColor": glow_color, "glowTexture": glow_tex,
        "tod": number(p.get("TodBrightnessInfluence", "1"), 1.0),
        # The largest half size (m) x ViewDistanceAdjust: M of the view distance of the emitter (particle-rules.md 4) [R].
        "maxHalfSize": max(sx["base"], sy["base"]),
        "viewAdjust": number(p.get("ViewDistanceAdjust", "1"), 1.0),
        "color": {"base": rgb, "random": float(color_rand) if isinstance(color_rand, float) else 0.0,
                  "curve": sample_color(color_keys(age["keys"]) if isinstance(age, dict) and "keys" in age else None, SAMPLES),
                  "emissive": 1.0, "glow": [1.0, 1.0, 1.0]},
        "blend": "additive" if blend == "Additive" else ("alpha" if blend in ("AlphaBased", "") else blend.lower()),
        "texture": tex, "atlas": atlas, "material": mtl,
        "facing": "velocity" if velocity_aligned else {"Camera": "camera", "CameraX": "upright", "Free": "free"}.get(facing, "camera"),
        "pivot": pivot, "stretch": stretch_p["base"] if velocity_aligned else 0.0,
        "soft": flag(p.get("SoftParticle", "false")), "sort": number(p.get("SortOffset", "0")),
        "cameraOffset": number(p.get("CameraDistanceOffset", "0")) * 100.0,
        "localSpace": str(p.get("MoveRelEmitter", "No")) == "Yes",
        "offset": [offset[0] * 100, -offset[1] * 100, offset[2] * 100],
        "randomOffset": [abs(roff[0]) * 100, abs(roff[1]) * 100, abs(roff[2]) * 100],
        "roundness": number(p.get("LocalOffsetRoundness", "0")),
        "speed": speed, "cone": cone, "innerCone": inner, "gravityUp": flag(p.get("FocusGravityDir", "false")),
        "gravity": {"min": [accel[0] * 100, -accel[1] * 100, (accel[2] - GRAVITY * g[1]) * 100],
                    "max": [accel[0] * 100, -accel[1] * 100, (accel[2] - GRAVITY * g[0]) * 100]},
        "drag": random_range(drag["base"] or 0.0, drag["random"]),
        "turbulence": number(p.get("Turbulence3DSpeed", "0")) * 100,
        # The Y angle and its rate turn a velocity-aligned particle about its own long axis (no turn on the screen) [I: the leaning
        # torch flame of check p2-torch1 had the turn on the screen].
        "rotation": [0.0, 0.0] if velocity_aligned else ([angles[1], angles[1]] if rangles[1] == 0 else [angles[1] - rangles[1], angles[1] + rangles[1]]),
        "rotationRate": [0.0, 0.0] if velocity_aligned else random_range(rate_y["base"] or 0.0, rate_y["random"]),
        "distance": {"min": near * 100, "max": (max_m * 100 if math.isfinite(max_m) and max_m > 0 else 0.0),
                     "bands": [[a, b if math.isfinite(b) else None] for a, b in bands]},
        "notes": notes + ([notes_stretch] if notes_stretch else []), "unsupported": unsupported,
    }
    if axis != [0.0, 0.0, 1.0]:
        record["axis"] = axis
    record["renderer"] = "sprite"
    # AST-230 (research/particle-impacts.md 1 [R]): the material Diffuse (PM_DiffuseColor; the raw XML floats, linear) multiplies the
    # atlas RGB in the lit and the glow terms: a separate parameter of the material instance. The fire of stage 2: Diffuse 1 or no material.
    record["matDiffuse"] = (list(mtl["diffuseColor"]) if mtl and not mtl.get("missing") and len(mtl.get("diffuseColor", [])) == 3
                            else [1.0, 1.0, 1.0])
    # AST-230: AlphaClip (research/particle-impacts.md 2 [R]): the authored ranges of Scale, SourceMin, SourceWidth (their Min and Max; the
    # omitted endpoints keep the defaults 0 to 1, 0, 1); the material makes the rows with K = Amax x L (the largest base alpha x AlphaScale).
    clip = parse_value(p.get("AlphaClip", ""))["fields"]

    def clip_range(name, low, high):
        keys = clip.get(name, {}).get("keys", [{}]) if isinstance(clip.get(name), dict) else [{}]
        values = keys[0] if keys else {}
        return [float(values.get("Min", low)), float(values.get("Max", high))]
    record["alphaClip"] = {"scale": clip_range("Scale", 0.0, 1.0), "sourceMin": clip_range("SourceMin", 0.0, 0.0),
                           "width": clip_range("SourceWidth", 1.0, 1.0), "amax": record["alpha"]["base"][1] or 1.0}
    unsupported.pop("AlphaClip", None)
    # AST-230 (research/particle-impacts.md 3 [R]): at zero visual velocity the alignment keeps the particle rotation. A velocity-aligned
    # emitter that never moves (no speed, no gravity, no acceleration: the blood spurts) keeps its source facing: CameraX (upright about the
    # local Z) or the camera.
    still = (max(abs(v) for v in record["speed"]) == 0.0 and not any(record["gravity"]["min"]) and not any(record["gravity"]["max"]))
    if record["facing"] == "velocity" and still:
        record["facing"] = {"CameraX": "upright"}.get(facing, "camera")
        record["stretch"] = 0.0
        record["notes"].append("OrientToVelocity with no motion: the source facing (the rotation is kept at zero velocity [R])")
    if mtl and not mtl.get("missing") and "REFRACTION" in mtl.get("flags", []):
        # A refraction material (the distortion of the swipes): no refraction path yet (plan step 6).
        record["enabled"] = False
        record["notes"].append("refraction material: not drawn (AST-230 plan step 6)")
    if str(p.get("EmitterShape", "") or "").upper() == "TRAIL":
        # AST-230: a trail emitter (EmitterShape TRAIL): its particles stay in the world where the emitter emits them and connect into a
        # ribbon that faces the camera, with the size (the width), the color, and the alpha of their age. The stateless emitters cannot
        # leave particles behind: the client draws it (Effects/SwcRibbons) with a ribbon material of the import; the system gets no
        # emitter for it. Connection: TextureMapping PerParticle (the texture repeats on each segment, the default) or PerStream (over
        # the whole ribbon), TextureFrequency (the repeats; default 1) [I: the names of the source parameters].
        import re as _re
        connection = str(p.get("Connection", "") or "")
        mapping = _re.search(r"TextureMapping=(\w+)", connection)
        frequency = _re.search(r"TextureFrequency=([0-9.]+)", connection)
        record["renderer"] = "ribbon"
        record["ribbon"] = {"uv": "stream" if mapping and mapping.group(1) == "PerStream" else "particle",
                            "frequency": float(frequency.group(1)) if frequency else 1.0}
        record["notes"].append("trail: a ribbon of the client (Effects/SwcRibbons)")
        for name in ("EmitterShape", "Connection"):
            unsupported.pop(name, None)
    if facing == "Decal":
        # AST-230 (plan step 6): a decal particle: the Niagara decal renderer projects the material down the local -Z of the emitter
        # [I]; its half extents are the half sizes of the data (SizeX, SizeY; the depth SizeZ) with the age curves; the alpha of the
        # particle is the opacity of the decal (DecalColor).
        record["renderer"] = "decal"
        sz = field(p, "SizeZ")
        record["decal"] = {"size": [100.0 * (sx["base"] or 1.0), 100.0 * (sy["base"] or sx["base"] or 1.0), 100.0 * (sz["base"] or 1.0)],
                           "curve": sample(sx["age"], SAMPLES)}
        record["enabled"] = bool(bands) and alpha_base > 0 and tex is not None
    light = str(p.get("LightSource", "") or "")
    if light:
        # AST-230 (plan step 6): a light particle (LightSource: Radius m, Intensity with its age curve; the color of the particle). The
        # Niagara light renderer with the inverse square falloff; the unit of Intensity is the DiffuseMultiplier of the point lights of
        # the world [I]: pi x 10,000 x Intensity cd (docs/PARTICLES.md, research/lights-emission.md; the bulb 0).
        import re as _re
        radius = _re.search(r"Radius=([0-9.]+)", light)
        intensity = _re.search(r"Intensity=\(([^()]*(?:\([^()]*\))?[^()]*)\)", light) or _re.search(r"Intensity=([0-9.]+)", light)
        value = parse_value(intensity.group(1)) if intensity else {"base": 1.0, "fields": {}}
        age = value["fields"].get("ParticleAge")
        unsupported.pop("LightSource", None)
        # AST-230 (research/particle-rules3.md 4 [R]): the light is an extra pass; it does not replace the sprite or the mesh. The light
        # is a twin emitter record with the same births and motion (the same values); the sprite keeps its own color and alpha.
        sprite = json.loads(json.dumps(record))
        record["renderer"] = "light"
        record["light"] = {"radius": 100.0 * float(radius.group(1)) if radius else 100.0, "intensity": float(value["base"] or 0.0),
                           "curve": sample(age["keys"] if isinstance(age, dict) and "keys" in age else None, SAMPLES)}
        record["enabled"] = bool(bands) and record["light"]["intensity"] > 0
        # The color of a Niagara light with the inverse square falloff is in the unit of a point light: cd x 100 x 100.
        record["lit"] = False
        record["color"]["emissive"] = math.pi * 10000.0 * record["light"]["intensity"] * 10000.0
        record["color"]["curve"] = [[c * k for c in rgb] for rgb, k in zip(record["color"]["curve"], record["light"]["curve"])]
        if sprite["renderer"] == "sprite" and sprite.get("texture"):
            record["spriteTwin"] = sprite
    geometry = str(p.get("Geometry", "") or "")
    if geometry:
        record.update(mesh_fields(p, geometry, mtl, textures, tex_out))
        unsupported.pop("Geometry", None)
        if record["mesh"] is None:
            record["enabled"] = False
            record["notes"].append(f"Geometry {geometry}: no mesh")
        elif mtl and not mtl.get("missing") and mtl.get("shader") in UNSUPPORTED_MESH_SHADERS:
            # AST-230: the FX mesh shaders (the fresnel glow, the intersection fade, the palette map, the imposter) are not in the mesh
            # material (Illum terms): a plain mesh is not the look (the white sphere of Wreckingball_impact_AOE). Not drawn (open point).
            record["enabled"] = False
            record["notes"].append(f"mesh shader {mtl['shader']}: not drawn yet (AST-230)")
        else:
            record["enabled"] = bool(bands) and alpha_base > 0 and tex is not None
            if tex is None:
                record["notes"].append("a mesh particle with no diffuse texture: not drawn")
    return record


def cry_quat(degrees: list[float]) -> list[float]:
    """@return The rotation of CryEngine Euler angles in degrees (x first, then y, then z: Quat::CreateRotationXYZ) as an Unreal quaternion
    (x, y, z, w) through the space map (x, -y, z): the axis (x, y, z) of the game is (-x, y, -z) in Unreal (space.py, stage 1.1)."""
    def axis(i: int, a: float) -> np.ndarray:
        q = np.zeros(4)
        q[i] = math.sin(a / 2)
        q[3] = math.cos(a / 2)
        return q

    def mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:   # a x b: b first (x, y, z, w)
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                         aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])
    r = [math.radians(v) for v in degrees]
    q = mul(axis(2, r[2]), mul(axis(1, r[1]), axis(0, r[0])))
    return [round(float(v), 6) for v in (-q[0], q[1], -q[2], q[3])]


PARTICLE_MESH_VERSION = 1


def particle_mesh(geometry: str, out: Path) -> dict | None:
    """Writes the glTF of the geometry of a mesh particle (all drawn subsets as one primitive with one material slot; positions,
    normals, UVs, vertex colors) and @return its record, or None when the file does not read."""
    from . import gltf, mesh as mesh_module
    path = geometry.lower().replace("\\", "/")
    if not source.has(path):
        return None
    name = Path(path).stem
    gltf_path = out / f"m{PARTICLE_MESH_VERSION}" / (path.rsplit(".", 1)[0] + ".gltf")
    asset = "SM_" + "_".join(Path(path).parts[-2:]).rsplit(".", 1)[0].replace(".", "_").replace("-", "_")
    if not gltf_path.is_file():
        try:
            _, meshes = mesh_module.load(path)
        except Exception:
            return None
        w = gltf.Writer()
        slot = w.material("particle")
        prims = []
        for m in meshes:
            pos = gltf.z_up_to_y_up(m.positions)
            nrm = gltf.z_up_to_y_up(m.normals) if m.normals is not None else None
            col = mesh_module.vertex_colors(m.colors)
            for subset in m.subsets:
                idx, (pp, nn, uv, _t, cc) = mesh_module.compact(m.indices[subset.first_index:subset.first_index + subset.num_indices],
                                                                pos, nrm, m.uvs, None, col)
                prims.append(w.primitive(pp, idx, nn, uv, None, material=slot, colors=cc))
        if not prims:
            return None
        w.doc["scenes"][0]["nodes"] = [w.node(name=name, mesh=w.mesh(name, prims))]
        gltf_path.parent.mkdir(parents=True, exist_ok=True)
        w.save(gltf_path)
    return {"source": path, "file": str(gltf_path.relative_to(env.content_dir())), "asset": asset}


def mesh_fields(p: dict, geometry: str, mtl: dict | None, textures: dict, tex_out: Path) -> dict:
    """@return The fields of a mesh particle (Geometry): the mesh, its scale (SizeX, SizeY, SizeZ: the scale of the mesh [I]; no
    half size), the start orientation (InitAngles, degrees) and the rotation rate (RotationRateX/Y/Z, degrees/s, with the age curve
    of the Z rate), and the Illum values of its material: the diffuse map, the emittance map (its TexMod tiling and offset; no
    oscillator), the Diffuse and Emittance colors, and the Opacity."""
    mesh = particle_mesh(geometry, tex_out.parent / "meshes")
    sizes = {axis: field(p, "Size" + axis.upper()) for axis in "xyz"}
    rates = [field(p, f"RotationRate{a}") for a in "XYZ"]
    angles = vector(p.get("InitAngles", "") or p.get("LocalInitAngles", ""))
    out = {"renderer": "mesh", "mesh": mesh,
           "meshScale": {axis: random_range(sizes[axis]["base"] if sizes[axis]["base"] else 1.0, sizes[axis]["random"]) for axis in "xyz"},
           "meshScaleCurve": {axis: sample(sizes[axis]["age"], SAMPLES) for axis in "xyz"},
           "meshQuat": cry_quat(angles),
           # The Unreal rotator rates (roll, pitch, yaw) of the game rates about x, y, z: all three change sign under the space map.
           "meshRate": [-(r["base"] or 0.0) for r in rates],
           "meshRateCurve": sample(rates[2]["age"], SAMPLES)}
    illum = {"shader": (mtl or {}).get("shader", ""), "diffuseColor": (mtl or {}).get("diffuseColor", [1.0, 1.0, 1.0]),
             "emittance": ((mtl or {}).get("emittance", []) + [0.0, 0.0, 0.0, 0.0])[:4], "opacity": (mtl or {}).get("opacity", 1.0),
             "emittanceMap": None, "emittanceTransform": [1.0, 1.0, 0.0, 0.0], "diffuseTransform": [1.0, 1.0, 0.0, 0.0]}
    if mtl and not mtl.get("missing"):
        for slot, key in (("Emittance", "emittanceMap"),):
            product_path = product(mtl["textures"].get(slot, ""))
            if product_path:
                if product_path not in textures:
                    textures[product_path] = texture(product_path, tex_out)
                illum[key] = textures[product_path]["asset"]
        for slot, key in (("Emittance", "emittanceTransform"), ("Diffuse", "diffuseTransform")):
            mod = mtl.get("texmods", {}).get(slot, {})
            illum[key] = [mod.get("TileU", 1.0), mod.get("TileV", 1.0), mod.get("OffsetU", 0.0), mod.get("OffsetV", 0.0)]
    out["illum"] = illum
    return out


def system(key: str, libs: Libraries, tex_out: Path, textures: dict) -> dict:
    hit = libs.find(key)
    if hit is None:
        return {"key": key, "missing": True}
    file_name, lib, index, exact = hit
    emitters = []
    for i in libs.subtree(lib, index):
        node = lib["nodes"][i]
        if node["tag"] != "Particles":
            continue
        record = emitter(node, libs, tex_out, textures)
        # A LightSource particle with a sprite: the sprite record, then its light twin (research/particle-rules3.md 4 [R]).
        twin = record.pop("spriteTwin", None)
        for item in ([twin] if twin is not None else []) + [record]:
            if node["params"].get("SpawnIndirection", "Direct") not in ("Direct", ""):
                item["enabled"] = False
                item["notes"].append(f"SpawnIndirection {node['params']['SpawnIndirection']}: stage 7")
            if lib["nodes"][i]["parent"] not in (-1, index) and i != index:
                item["notes"].append("a grandchild: its parent trigger is not used yet")
            emitters.append(item)
    shown = [e for e in emitters if e["enabled"]]
    view = max([e["distance"]["max"] for e in shown] + [0.0])
    if any(e["distance"]["max"] == 0.0 for e in shown):
        view = max(view, DEFAULT_VIEW)
    asset = "NS_" + key.replace(".", "_").replace("-", "_")
    # M: the largest particle size of the effect x its ViewDistanceAdjust (m), for Dmax = (screen height / vertical FOV) x P x M x S x V.
    max_size = max([e["maxHalfSize"] * e["viewAdjust"] for e in shown] + [0.0])
    return {"key": lib["nodes"][index]["key"], "library": lib["path"], "exact": exact, "asset": asset, "view": view, "maxSize": round(max_size, 5),
            "emitters": emitters, "hash": None}


def systems(keys: list[str] | None = None) -> dict:
    """Writes systems.json and the textures of the systems."""
    import hashlib
    libs = Libraries()
    out = out_dir()
    textures: dict[str, dict] = {}
    result = []
    # The default list: the stage 2 fire and the resolved keys of skill-effects.json (AST-230, tools/convert-particles.sh skills).
    from .skill_effects import build_keys
    for key in keys or list(dict.fromkeys(STAGE2 + build_keys())):
        record = system(key, libs, out / "textures", textures)
        record["hash"] = hashlib.sha256((CONVERTER_VERSION + json.dumps(record, sort_keys=True, default=str)).encode()).hexdigest()[:16]
        result.append(record)
    write_json(out / "systems.json", {"format": 1, "version": CONVERTER_VERSION, "systems": result, "textures": textures}, 1)
    return {k["key"]: {"emitters": [(e["name"], e["enabled"], sorted(e["unsupported"])) for e in k.get("emitters", [])],
                       "view m": k.get("view", 0) / 100} for k in result}


# ------------------------------------------------------------------------------------------------- the checks of the examples

def check() -> dict:
    """The worked examples of the research (particles.md): the resolved values of the converter against the values of the study.
    @return {name: "ok" or the difference}."""
    libs = Libraries()
    results = {}

    def node(key: str) -> dict:
        file_name, lib, index, exact = libs.find(key)
        return lib["nodes"][index]

    def expect(name: str, value, wanted, tol: float = 1e-4) -> None:
        if isinstance(wanted, (list, tuple)) and all(isinstance(w, float) for w in wanted):
            ok = len(value) == len(wanted) and all(abs(a - b) <= tol for a, b in zip(value, wanted))
        elif isinstance(wanted, float):
            ok = abs(value - wanted) <= tol
        else:
            ok = value == wanted
        results[name] = "ok" if ok else f"got {value}, wanted {wanted}"

    camp = libs.find("Buildables.Campfire.Idle_Medium")
    _, lib, index, _ = camp
    children = [lib["nodes"][i]["name"] for i in libs.subtree(lib, index)[1:] if lib["nodes"][i]["parent"] == index]
    expect("campfire children", children, ["Flame_1", "Flame_2", "Embers_01", "Smoke_01"])
    root = params_of(lib["nodes"][index]["params"])
    expect("campfire root alpha", number(root["Alpha"], 1.0), 0.0)
    flame1 = node("Buildables.Campfire.Idle_Medium.Flame_1")
    p = params_of(flame1["params"])
    expect("flame_1 life range", random_range(1.0, field(p, "ParticleLifeTime")["random"]), [0.7, 1.0])
    expect("flame_1 size x range", random_range(0.6, scalar_param(p["SizeX"])["random"]), [0.36, 0.6])
    expect("flame_1 size curve start", sample(scalar_param(p["SizeX"])["age"])[0], round(191 / 255, 5), 2e-3)
    expect("flame_1 alpha at 0.5", sample(field(p, "Alpha")["age"], 3)[1], 1.0, 2e-3)
    expect("flame_1 alpha end", sample(field(p, "Alpha")["age"])[-1], 0.0)
    expect("flame_1 velocity aligned", flag(p.get("OrientToVelocity", "false")), True)
    expect("flame_1 tiles", [parse_value(p["TextureTiling"])["fields"][k] for k in ("TilesX", "TilesY")], [8.0, 4.0])
    embers = params_of(node("Buildables.Campfire.Idle_Medium.Embers_01")["params"])
    expect("embers blend", embers["BlendType"], "Additive")
    expect("embers nominal rate", number(embers["Count"]) / number(embers["ParticleLifeTime"]), 100.0)
    expect("campfire LOD distances", [l["distance"] for l in node("Buildables.Campfire.Idle_Medium")["lods"]], [20.0, 40.0, 70.0])
    expect("embers bands", lod_bands(node("Buildables.Campfire.Idle_Medium.Embers_01"))[0], [[0.0, 20.0]])
    expect("flame_2 bands", lod_bands(node("Buildables.Campfire.Idle_Medium.Flame_2"))[0], [[0.0, 40.0]])
    expect("smoke bands", lod_bands(node("Buildables.Campfire.Idle_Medium.Smoke_01"))[0], [[0.0, 70.0]])
    smoke = params_of(node("Buildables.Campfire.Idle_Medium.Smoke_01")["params"])
    expect("smoke camera max", number(smoke["CameraMaxDistance"]), 25.0)
    torch = params_of(node("Buildables.Torch.Idle")["params"])
    expect("torch offset", vector(torch["LocalSpawnPosOffset"]), [0.0, -0.05, -0.125])
    expect("torch tiling", [parse_value(torch["TextureTiling"])["fields"][k] for k in ("TilesX", "TilesY", "FirstTile",
                                                                                       "AnimFramesCount", "AnimFramerate")],
           [8.0, 8.0, 32.0, 32.0, 48.0])
    expect("torch material", torch["Material"], "materials/vfx/fire/fire_torch_idle")
    expect("torch blend default", torch["BlendType"], "AlphaBased")
    expect("torch emittance", number(torch["EmissiveLighting"]), 100.0)
    expect("torch bands", lod_bands(node("Buildables.Torch.Idle"))[0], [[0.0, 100.0]])
    expect("torch embers bands", lod_bands(node("Buildables.Torch.Idle.Embers_01"))[0], [[0.0, 20.0]])
    expect("torch gravity range", random_range(-0.15, 0.2), [-0.15, -0.12])
    shore = node("Env_Island.Water.ShoreFoamWaveDecal.splashes")
    sp = params_of(shore["params"])
    expect("shore splash trigger default", sp.get("SpawnIndirection"), "Direct")
    expect("shore splash speed range", random_range(2.0, field(sp, "Speed")["random"]), [-1.0, 2.0])
    expect("shore splash size y default base", field(sp, "SizeY")["base"], 1.0)
    expect("shore splash tiles", [parse_value(sp["TextureTiling"])["fields"][k] for k in ("TilesX", "TilesY", "VariantCount")],
           [8.0, 4.0, 8.0])
    expect("shore splash bands", lod_bands(shore)[0], [[0.0, 30.0]])
    mtl = material(torch["Material"])
    expect("torch material diffuse", mtl["diffuse"], "textures/vfx/fire/fire_torch_colorlessb_8x8.dds")
    expect("torch material emittance", mtl["emittance"], [1.0, 0.17144112, 0.0, 50.0])
    failed = {k: v for k, v in results.items() if v != "ok"}
    return {"checks": len(results), "failed": failed}
