"""Tests of the skill effect export (converter/swc_convert/skill_effects.py and the table run of luabc.py, AST-230, docs/EFFECTS.md) with
no game data: a made-up fragment of the form of the animation databases, a made-up Lua 5.1 chunk of the form of the weapon effect
scripts, and the Euler rule of the mesh particles. The names and the numbers are made up."""
import math
import struct
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import luabc  # noqa: E402
from swc_convert import particle_systems as S  # noqa: E402
from swc_convert import skill_effects as E  # noqa: E402

FRAGMENT = """<Fragment Tags="1H_Melee+Sword">
 <ProcLayer>
  <Blend ExitTime="0.25" StartTime="0" Duration="0.1"/>
  <Procedural type="ParticleEffect"><ProceduralParams>
   <EffectName value="fx.Test.One"/><JointName value="aim_direction"/><AttachmentName value=""/>
   <PosOffset><Element value="0"/><Element value="-0.2"/><Element value="0"/></PosOffset>
   <RotOffset><Element value="1.5"/><Element value="0"/><Element value="3"/></RotOffset>
   <EmitterFollows value="true"/><KillOnExit value="false"/><KeepEmitterActive value="true"/><Scale value="1.5"/><Condition value=""/>
  </ProceduralParams></Procedural>
  <Blend ExitTime="0.2" StartTime="0" Duration="0"/>
  <Procedural type="" contextType=""/>
 </ProcLayer>
 <ProcLayer>
  <Blend ExitTime="0.5" StartTime="0" Duration="0"/>
  <Procedural type="CAGE-Named"><ProceduralParams><Name value="Mark"/></ProceduralParams></Procedural>
  <Blend ExitTime="0.1" StartTime="0" Duration="0"/>
  <Procedural type="WeaponEffects"><ProceduralParams>
   <ItemSlotAlias value="PaperdollSlotAlias::ACTIVE_MAIN_HAND_WEAPON"/><EffectGroupName value="test_group"/>
   <OptionOnEnter value="Enable"/><OptionOnExit value="Disable"/><Condition value=""/>
  </ProceduralParams></Procedural>
 </ProcLayer>
</Fragment>"""


class Chunk:
    """A writer of a Lua 5.1 main function (lundump.c of Lua 5.1.5: the header of 12 bytes, int 4, size_t 8, instruction 4, number 8)."""

    def __init__(self):
        self.code, self.constants = [], []

    def k(self, value):
        if value not in self.constants:
            self.constants.append(value)
        return self.constants.index(value)

    def op(self, op, a=0, b=0, c=0, bx=None):
        self.code.append(op | (a << 6) | ((bx << 14) if bx is not None else ((c << 14) | (b << 23))))

    def bytes(self):
        def string(text):
            data = text.encode() + b"\0"
            return struct.pack("<Q", len(data)) + data
        out = b"\x1bLua\x51\x00\x01\x04\x08\x04\x08\x00" + string("@test") + struct.pack("<ii", 0, 0) + bytes([0, 0, 2, 20])
        out += struct.pack("<i", len(self.code)) + struct.pack("<%dI" % len(self.code), *self.code)
        out += struct.pack("<i", len(self.constants))
        for value in self.constants:
            if isinstance(value, bool):
                out += bytes([1, int(value)])
            elif isinstance(value, float):
                out += bytes([3]) + struct.pack("<d", value)
            else:
                out += bytes([4]) + string(value)
        return out + struct.pack("<iiii", 0, 0, 0, 0)


class EffectClipTests(unittest.TestCase):
    def test_clip_times_and_fields(self):
        clips = E.effects_of(ET.fromstring(FRAGMENT))
        self.assertEqual([c["type"] for c in clips], ["ParticleEffect", "WeaponEffects"])
        particle, group = clips
        # The start is the sum of the ExitTime values; Duration is kept apart; the next procedural item ends the clip.
        self.assertAlmostEqual(particle["start"], 0.25)
        self.assertAlmostEqual(particle["end"], 0.45)
        self.assertEqual(particle["blend"], [0.25, 0.0, 0.1])
        self.assertEqual(particle["effect"], "fx.Test.One")
        self.assertEqual(particle["position"], [0.0, -0.2, 0.0])
        self.assertEqual((particle["follow"], particle["kill"], particle["keep"], particle["scale"]), (True, False, True, 1.5))
        # An open last clip ends with the fragment.
        self.assertAlmostEqual(group["start"], 0.6)
        self.assertIsNone(group["end"])
        self.assertEqual((group["group"], group["enter"], group["exit"]), ("test_group", "Enable", "Disable"))
        self.assertEqual((group["layer"], group["clip"]), (1, 1))


class LuaTableTests(unittest.TestCase):
    def test_group_table(self):
        # r0 = RequireScript("Base"); r1 = {}; r2 = {}; r3 = {}; r4 = {}; r4.type = r0.EffectTypes.Particle;
        # r4.particleName = "fx.Test"; r4.positionOffset = Vector3(0, 1.5, -0.5); r4.killOnStop = false;
        # r3[1...] = {r4}; r2.swing = r3; r1.effectGroups = r2; return r1
        c = Chunk()
        c.op(5, 0, bx=c.k("RequireScript"))
        c.op(1, 1, bx=c.k("Base"))
        c.op(28, 0, 2, 2)
        for reg in (1, 2, 3, 4):
            c.op(10, reg)
        c.op(6, 5, 0, 0x100 | c.k("EffectTypes"))
        c.op(6, 5, 5, 0x100 | c.k("Particle"))
        c.op(9, 4, 0x100 | c.k("type"), 5)
        c.op(9, 4, 0x100 | c.k("particleName"), 0x100 | c.k("fx.Test"))
        c.op(5, 5, bx=c.k("Vector3"))
        c.op(1, 6, bx=c.k(0.0))
        c.op(1, 7, bx=c.k(1.5))
        c.op(1, 8, bx=c.k(-0.5))
        c.op(28, 5, 4, 2)
        c.op(9, 4, 0x100 | c.k("positionOffset"), 5)
        c.op(2, 5, 0, 0)
        c.op(9, 4, 0x100 | c.k("killOnStop"), 5)
        c.op(0, 5, 4)
        c.op(34, 3, 1, 1)
        c.op(9, 2, 0x100 | c.k("swing"), 3)
        c.op(9, 1, 0x100 | c.k("effectGroups"), 2)
        c.op(30, 1, 2)
        result = luabc.run_tables(luabc.load(b"\x00\x00" + c.bytes()),
                                  {"Vector3": lambda *a: list(a), "RequireScript": lambda name: luabc.Symbol(name)})
        groups = result[0].get("effectGroups").plain()
        self.assertEqual(groups["swing"], [{"type": "Base.EffectTypes.Particle", "particleName": "fx.Test",
                                            "positionOffset": [0.0, 1.5, -0.5], "killOnStop": False}])


class EulerTests(unittest.TestCase):
    def test_single_axes(self):
        # A rotation about one game axis: x and z change their sign under the space map, y keeps it.
        half = math.sin(math.radians(45))
        self.assertEqual(S.cry_quat([90, 0, 0]), [round(-half, 6), 0.0, 0.0, round(half, 6)])
        self.assertEqual(S.cry_quat([0, 90, 0]), [0.0, round(half, 6), 0.0, round(half, 6)])
        self.assertEqual(S.cry_quat([0, 0, 90]), [0.0, 0.0, round(-half, 6), round(half, 6)])

    def test_order_x_first(self):
        # x first, then z: a unit vector along y goes to z (x 90), then stays (z about z).
        q = S.cry_quat([90, 0, 90])
        self.assertAlmostEqual(sum(v * v for v in q), 1.0, places=5)


if __name__ == "__main__":
    unittest.main()
