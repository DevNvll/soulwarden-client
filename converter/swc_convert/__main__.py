"""swc-convert: convert the content of the game copy of the user for the Unreal client.

Usage (through tools/convert.sh):
  mesh PATH... [--force]                            static meshes -> glTF (shared textures and materials)
  character CDF [--clip CAF]... [--force]           a character with its skins and clips -> glTF
  region NAME [--area X Y SIZE] [--meshes]          height map, macro map, placements (+ the meshes of the placements)
  proofs                                            the four proofs of the research AST-135 (exit check of stage P0)
  manifest                                          write the manifest again (the Unreal folder rule)
  checks                                            check the readers against the values of the research
  census [NAME...] [--workers N] [--force]          the world census (stage 1.2): placements of all regions, meshes,
                                                    textures, triangles (default: the 49 regions of the main world)
  budget [--ddc-bytes N]                            the disk budget of the world from the imported regions (stage 1.2)
  level NAME... | --playable | --all                the data of the world level (stage 1.3): Landscape heights with the
                                                    terrain cookies, macro color maps, the cell files of the instances
  world NAME... | --playable | --all [--workers N] [--force]
                                                    the meshes of regions with shared textures and materials (stage 1.2)
  space-proof [NAME --area X Y SIZE] [--sheet]      the proof of the space map (stage 1.1): marker, terrain patch, plan;
                                                    --sheet: the pictures side by side after Scripts/space_proof.py
  collision [PATH...]                               the game collision of the static meshes (P4.1): the physics file
                                                    of each mesh -> content/collision (default: all static meshes)
  overrides NAME... | --all                         the material overrides of the placements (P4.1): the slot maps of
                                                    the pairs (mesh, material), their records (after census)
  terrain-plan NAME... | --all                      the terrain material plan of each region (T1: the slots in the order
                                                    of priority, the parameters, the macro sets) and the slices of the
                                                    texture arrays (terrain_material.py; after terrain-layers)
  terrain-layers NAME... | --all                    the terrain materials of the game (world fidelity T0): the layer
                                                    lists, the splats (2,048 and 2,041 grids), the macro maps, the
                                                    layer materials and their full textures (terrain_layers.py)
  preview mesh PATH | character CDF [--clip CAF] | region NAME --area X Y SIZE
Output: SWC_CONTENT (default ~/Games/soulwarden-client/content). Previews: ~/Games/soulwarden-client/previews.
"""
from __future__ import annotations

import argparse
import json
import sys

from . import character, env, manifest, mesh, region, world

PROOF_MESH = "objects/props/tents/legion/legion_tent_b/legion_tent_b.cgf"
PROOF_CDF = "objects/characters/npc/natural/predator/wolf/wolf.cdf"
PROOF_CLIPS = ["animations/gameplay/character/npc/natural/predator/wolf/navigation/run/wolf_canter.caf",
               "animations/gameplay/character/npc/natural/predator/wolf/navigation/idle/wolf_idle.caf"]
PROOF_REGION = "r_+04_+02"
PROOF_AREA = (8780.0, 4100.0, 300.0)


def region_meshes(name: str, area, force: bool) -> dict:
    data = json.loads(region.placement_file(name, area).read_text())
    unique = sorted({p["mesh"] for p in data["placements"]})
    return world.convert_meshes(unique, workers=1, force=force)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="swc-convert", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("mesh"); p.add_argument("paths", nargs="+"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("character"); p.add_argument("cdf"); p.add_argument("--clip", action="append", default=[]); p.add_argument("--force", action="store_true")
    p = sub.add_parser("region"); p.add_argument("name"); p.add_argument("--area", type=float, nargs=3); p.add_argument("--meshes", action="store_true"); p.add_argument("--force", action="store_true")
    sub.add_parser("proofs")
    sub.add_parser("manifest")
    sub.add_parser("checks")
    p = sub.add_parser("census"); p.add_argument("names", nargs="*"); p.add_argument("--workers", type=int, default=4); p.add_argument("--force", action="store_true")
    p = sub.add_parser("budget"); p.add_argument("--ddc-bytes", type=int)
    p = sub.add_parser("level"); p.add_argument("names", nargs="*"); p.add_argument("--playable", action="store_true"); p.add_argument("--all", action="store_true")
    p = sub.add_parser("world"); p.add_argument("names", nargs="*"); p.add_argument("--playable", action="store_true"); p.add_argument("--all", action="store_true")
    p.add_argument("--workers", type=int, default=4); p.add_argument("--force", action="store_true")
    p = sub.add_parser("space-proof"); p.add_argument("name", nargs="?", default=PROOF_REGION); p.add_argument("--area", type=float, nargs=3, default=list(PROOF_AREA)); p.add_argument("--sheet", action="store_true")
    p = sub.add_parser("collision"); p.add_argument("paths", nargs="*")
    p = sub.add_parser("overrides"); p.add_argument("names", nargs="*"); p.add_argument("--all", action="store_true")
    p = sub.add_parser("terrain-layers"); p.add_argument("names", nargs="*"); p.add_argument("--all", action="store_true")
    p = sub.add_parser("terrain-plan"); p.add_argument("names", nargs="*"); p.add_argument("--all", action="store_true")
    p = sub.add_parser("preview"); p.add_argument("kind", choices=["mesh", "character", "region"]); p.add_argument("target")
    p.add_argument("--clip"); p.add_argument("--area", type=float, nargs=3); p.add_argument("--frames", type=float, nargs="*", default=[0, 6.5, 13, 19.5])
    args = parser.parse_args(argv)
    result: object
    if args.command == "mesh":
        result = world.convert_meshes(args.paths, workers=1, force=args.force)
    elif args.command == "character":
        result = character.convert(args.cdf, args.clip, force=args.force)
    elif args.command == "region":
        area = tuple(args.area) if args.area else None
        result = region.convert(args.name, area)
        if args.meshes:
            result["meshes"] = region_meshes(args.name, area, args.force)
    elif args.command == "checks":
        from . import checks
        result = checks.run()
    elif args.command == "census":
        from . import census
        result = census.run(args.names or None, args.workers, args.force)
    elif args.command == "budget":
        from . import budget
        result = budget.run(args.ddc_bytes)
    elif args.command == "level":
        from . import census, terrain
        names = census.world_regions() if args.all else census.playable_regions() if args.playable else args.names
        if not names:
            parser.error("level needs region names, --playable, or --all")
        result = terrain.level_data(names)
    elif args.command == "world":
        from . import census
        names = census.world_regions() if args.all else census.playable_regions() if args.playable else args.names
        if not names:
            parser.error("world needs region names, --playable, or --all")
        result = world.convert_regions(names, args.workers, args.force)
    elif args.command == "space-proof":
        from . import space
        result = space.sheet() if args.sheet else space.proof(args.name, tuple(args.area))
    elif args.command == "terrain-plan":
        from . import census, terrain_material
        names = census.world_regions() if args.all else args.names
        if not names:
            parser.error("terrain-plan needs region names or --all")
        result = {"plans": terrain_material.plan_all(names), "arrays": terrain_material.write_arrays()}
    elif args.command == "terrain-layers":
        from . import census, terrain_layers
        names = census.world_regions() if args.all else args.names
        if not names:
            parser.error("terrain-layers needs region names or --all")
        result = [terrain_layers.export_region(n) for n in names]
    elif args.command == "overrides":
        from . import census, overrides
        names = census.world_regions() if args.all else args.names
        if not names:
            parser.error("overrides needs region names or --all")
        result = overrides.convert(names)
    elif args.command == "collision":
        from . import collision
        result = collision.convert_all([p.lower() for p in args.paths] or None)
    elif args.command == "manifest":
        result = manifest.refresh()
    elif args.command == "proofs":
        result = {"mesh": mesh.convert(PROOF_MESH), "character": character.convert(PROOF_CDF, PROOF_CLIPS),
                  "region": region.convert(PROOF_REGION, PROOF_AREA)}
        result["region"]["meshes"] = region_meshes(PROOF_REGION, PROOF_AREA, False)
    else:
        from . import preview   # numba: loaded only for a preview
        if args.kind == "mesh":
            result = str(preview.mesh_preview(args.target))
        elif args.kind == "character":
            result = str(preview.character_preview(args.target, args.clip, args.frames))
        else:
            if not args.area:
                parser.error("preview region needs --area X Y SIZE")
            result = str(preview.region_preview(args.target, tuple(args.area)))
    print(json.dumps(result, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
