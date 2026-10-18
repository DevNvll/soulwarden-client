"""Checks of the converter against the values of the research AST-135 (they need the game copy).

Each check reads the game copy, not the converted files, so it needs no earlier conversion.
"""
from __future__ import annotations

from . import anim, cgf, character, mesh, region, source

PROOF_MESH = "objects/props/tents/legion/legion_tent_b/legion_tent_b.cgf"
PROOF_CDF = "objects/characters/npc/natural/predator/wolf/wolf.cdf"
PROOF_CLIP = "animations/gameplay/character/npc/natural/predator/wolf/navigation/run/wolf_canter.caf"


def run() -> dict:
    results = {}

    def check(name, value, expected):
        results[name] = {"ok": value == expected, "value": value, "expected": expected}

    _f, meshes = mesh.load(PROOF_MESH)
    check("static mesh vertices", sum(len(m.positions) for m in meshes), 4521)
    check("static mesh triangles", sum(len(m.indices) // 3 for m in meshes), 7067)
    ch = character.Character(PROOF_CDF)
    check("skeleton bones", len(ch.bones), 134)
    check("skin vertices", sorted(len(m.positions) for _p, m, _j, _mt in ch.skins), [1274, 4990])
    check("controller ids are CRC-32 of the names", all(b.controller_id == __import__("zlib").crc32(b.name.encode()) for b in ch.bones), True)
    clip = anim.read_clip(cgf.read_chunks(source.read(PROOF_CLIP)))
    check("clip frames", clip.end - clip.start, 26)
    check("clip controllers matched", sum(1 for c in clip.controllers if c in {b.controller_id for b in ch.bones}), 52)
    reg = region.Region("r_+04_+02")
    h = reg.heights()
    check("region height min m", round(float(h.min()) * region.HEIGHT_SCALE, 2), 42.13)
    check("region height max m", round(float(h.max()) * region.HEIGHT_SCALE, 2), 310.6)
    results["all ok"] = all(r["ok"] for r in results.values())
    return results
