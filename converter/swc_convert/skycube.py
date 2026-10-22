"""The source cloud layers of the water sky cube (AST-200 part 5; research/water-sky-cube-list.md, research/e5-distance-clouds.md).

The retail water sky cube has three parts in this order: the base SkyHDR sky (no base sky fog on the normal frame), the draw list 0x19
(the Distanceclouds subsets of the active sky slices: at the starting beach `objects/sky/skydome_a.cgf` with the override
`objects/climax/materials/sky/ftue_sky.mtl`), then the cube fog once. This module writes the cloud part: one glTF for each subset of
the dome (mesh rule 8: the vertex RGBA and the tangent frame of the source), the spec of each layer of the override material, and the
provisional draw order (the CGF subset order 2, 1, 0 [U]: the final retail material sort order is not proved).

Usage: python3 -B -m swc_convert.skycube write
Output: SWC_CONTENT/water/skycube.json and SWC_CONTENT/water/skycube/LAYER.gltf.
"""
from __future__ import annotations

import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from . import env, manifest, material_records, materials, mesh, source

DOME = "objects/sky/skydome_a.cgf"
# The sky slice of the starting beach (POI_TOD_StartingBeach_A_(40mOcean): slices/sky/newworld01_skybox_ftuevista.dynamicslice) overrides
# the dome material with this file (research/e5-distance-clouds.md "Clear reference slice").
MATERIAL = "objects/climax/materials/sky/ftue_sky.mtl"
UNREAL_FOLDER = "/SwcGameContent/Water/SkyCube"


def out_dir() -> Path:
    return env.content_dir() / "water"


def numbers(text: str | None) -> list[float]:
    return [float(v) for v in (text or "").split(",") if v.strip()]


def oscillator(mod: dict, axis: str) -> list[float]:
    """@return (type, rate, amplitude, phase) of a TexMod oscillator; a missing type is 0 (no motion: a saved rate does not move)."""
    kind = int(mod.get(f"TexMod_{axis}OscillatorType", 0))
    return [float(kind), float(mod.get(f"TexMod_{axis}OscillatorRate", 0.0)), float(mod.get(f"TexMod_{axis}OscillatorAmplitude", 0.0)),
            float(mod.get(f"TexMod_{axis}OscillatorPhase", 0.0))]


def layer_spec(element: ET.Element, textures: dict) -> dict:
    """@return The spec of one Distanceclouds layer (parent "distance clouds", M_SwcDistanceClouds): the mask (the red channel: density
    and opacity), the UV rule of its TexMod (tile, offset, the U and V oscillators: type 2 a constant scroll t x rate x amplitude, type
    4 a sine pan amplitude x sin(2 pi (frac(t x rate) + phase))), and the public parameters."""
    params = dict(element.find("PublicParams").attrib) if element.find("PublicParams") is not None else {}
    texture = next((t for t in element.iter("Texture") if t.get("Map") == "Diffuse"), None)
    spec = {"name": element.get("Name", ""), "shader": element.get("Shader", ""), "parent": "distance clouds", "textures": {},
            "scalars": {}, "vectors": {}}
    if texture is not None:
        product = materials.texture_product(texture)
        record = material_records.data_texture(product, decode=material_records.file_srgb(product))
        textures[record["asset"]] = {"file": record["file"], "color": record["color"], "cap": record["cap"],
                                     "unreal": manifest.unreal_folder("texture", record["asset"])}
        spec["textures"]["Mask"] = record["asset"]
        mod = texture.find("TexMod")
        m = dict(mod.attrib) if mod is not None else {}
        spec["vectors"]["UVTile"] = [float(m.get("TileU", 1.0)), float(m.get("TileV", 1.0)), float(m.get("OffsetU", 0.0)),
                                     float(m.get("OffsetV", 0.0))]
        spec["vectors"]["UOscillator"] = oscillator(m, "U")
        spec["vectors"]["VOscillator"] = oscillator(m, "V")
    for name, default in (("StepSize", 0.01), ("Attenuation", 0.5), ("AlphaSaturation", 1.0), ("SunColorMultiplier", 1.0),
                          ("SkyColorMultiplier", 1.0)):
        spec["scalars"][name] = float(params.get(name, default))
    spec["scalars"]["Opacity"] = float(element.get("Opacity", 1.0))
    return spec


def write() -> dict:
    """Writes SWC_CONTENT/water/skycube.json and the glTF of each dome subset. @return A report."""
    folder = out_dir() / "skycube"
    folder.mkdir(parents=True, exist_ok=True)
    full = folder / "skydome_a_all.gltf"
    mesh.convert(DOME, force=True, write_manifest=False, rule=mesh.RULE_BEAMS, out=full)
    _f, parts = mesh.load(DOME)
    subsets = [s.material for p in parts for s in p.subsets]    # the material id of each primitive, in the CGF order
    doc = json.loads(full.read_text())
    prims = doc["meshes"][0]["primitives"]
    if len(prims) != len(subsets):
        raise RuntimeError(f"{DOME}: {len(prims)} primitives, {len(subsets)} subsets")
    layers_xml = [m for m in ET.fromstring(source.read(MATERIAL)).find("SubMaterials")]
    textures: dict = {}
    layers = []
    for order, (prim, material_id) in enumerate(zip(prims, subsets)):
        one = json.loads(json.dumps(doc))
        one["meshes"][0]["primitives"] = [prim]
        name = f"skydome_a_layer{material_id}"
        out = folder / (name + ".gltf")
        out.write_text(json.dumps(one))
        spec = layer_spec(layers_xml[material_id], textures)
        layers.append({"name": name, "material id": material_id, "order": order, "file": str(out.relative_to(env.content_dir())),
                       "unreal": f"{UNREAL_FOLDER}/Meshes/{name}", "triangles": doc["accessors"][prim["indices"]]["count"] // 3,
                       "spec": spec})
    full.unlink()
    result = {"dome": DOME, "material": MATERIAL, "folder": UNREAL_FOLDER,
              "order": "the CGF subset order (material ids 2, 1, 0); provisional [U]: the retail material sort order is not proved",
              "cloud sky": "0: the proved startup value of CLOUDSHADING_SKYCOLOR; a live writer is not proved [U]",
              "layers": layers, "textures": textures}
    (out_dir() / "skycube.json").write_text(json.dumps(result, indent=1))
    return {"file": str(out_dir() / "skycube.json"), "layers": [(l["name"], l["triangles"], l["spec"]["name"]) for l in layers],
            "textures": len(textures)}


def main(argv: list[str]) -> int:
    if argv[:1] == ["write"]:
        print(json.dumps(write(), indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
