"""Tests of optiondefaults.constructor_stores (stage P2.3: the defaults of the options data of the client) with no game
data: constructor code made in the test, in the forms of the constructor of the client."""
import struct
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import optiondefaults  # noqa: E402


def value(stores: dict[int, int], offset: int, size: int) -> int | None:
    raw = [stores.get(offset + i) for i in range(size)]
    return None if None in raw else int.from_bytes(bytes(raw), "little")


class ConstructorStores(unittest.TestCase):
    def test_immediate_stores_through_rcx_and_r14(self):
        code = (b"\x4C\x8B\xF1"                              # mov r14, rcx
                + b"\xC7\x41\x38\x64\x00\x00\x00"           # mov dword [rcx+0x38], 100
                + b"\x41\xC7\x86\x90\x08\x00\x00" + struct.pack("<f", 0.08)  # mov dword [r14+0x890], 0.08f
                + b"\x41\xC6\x46\x10\x01"                   # mov byte [r14+0x10], 1
                + b"\x66\x41\xC7\x46\x20\x01\x01"           # mov word [r14+0x20], 0x101
                + b"\x49\xC7\x86\x00\x01\x00\x00\xFF\xFF\xFF\xFF")  # mov qword [r14+0x100], -1
        stores = optiondefaults.constructor_stores(code)
        self.assertEqual(value(stores, 0x38, 4), 100)
        self.assertAlmostEqual(struct.unpack("<f", bytes(stores[0x890 + i] for i in range(4)))[0], 0.08, places=6)
        self.assertEqual(value(stores, 0x10, 1), 1)
        self.assertEqual(value(stores, 0x20, 2), 0x101)
        self.assertEqual(value(stores, 0x100, 8), 0xFFFFFFFFFFFFFFFF)

    def test_zero_register_and_later_store_wins(self):
        code = (b"\x33\xED"                                  # xor ebp, ebp
                + b"\x41\xC6\x46\x30\x01"                   # mov byte [r14+0x30], 1
                + b"\x41\x88\x6E\x30"                       # mov byte [r14+0x30], bpl: the later store wins
                + b"\x49\x89\xAE\x40\x02\x00\x00"           # mov qword [r14+0x240], rbp
                + b"\x48\x89\x59\x50")                      # mov qword [rcx+0x50], rbx: not a known value
        stores = optiondefaults.constructor_stores(code)
        self.assertEqual(value(stores, 0x30, 1), 0)
        self.assertEqual(value(stores, 0x240, 8), 0)
        self.assertIsNone(value(stores, 0x50, 8))

    def test_other_bases_are_not_this(self):
        code = b"\xC7\x42\x38\x64\x00\x00\x00"              # mov dword [rdx+0x38], 100: not this
        self.assertEqual(optiondefaults.constructor_stores(code), {})

    def test_words_of_names(self):
        self.assertEqual(optiondefaults._words("m_cameraSensitivityADSHorizontal"),
                         optiondefaults._words("Hud.LocalPlayer.Options.Controls.CameraSensitivityHorizontalADS"))



class RegistrationCall(unittest.TestCase):
    """The call of a field registration after its `lea rdx, NAME` (the forms of the reflection of the client)."""

    def test_move_before_the_lea(self):
        # mov r8d, 0x23A; movdqa [rbp+0x7A0], xmm0; lea rdx, NAME; mov rcx, rax; call
        code = b"\x41\xB8\x3A\x02\x00\x00" + b"\x66\x0F\x7F\x85\xA0\x07\x00\x00" + b"\x48\x8D\x15\x00\x00\x00\x00" \
            + b"\x48\x8B\xC8" + b"\xE8\x00\x00\x00\x00"
        self.assertEqual(optiondefaults._registration_call(code, 21), 24)

    def test_move_after_the_lea(self):
        # lea rdx, NAME; mov r8d, 0x230; mov rcx, rax; call (m_hudShowVitalsValues)
        code = b"\x48\x8D\x15\x00\x00\x00\x00" + b"\x41\xB8\x30\x02\x00\x00" + b"\x48\x8B\xC8" + b"\xE8\x00\x00\x00\x00"
        self.assertEqual(optiondefaults._registration_call(code, 7), 16)

    def test_other_this_register_and_no_call(self):
        # lea rdx, NAME; mov rcx, rsi; call
        self.assertEqual(optiondefaults._registration_call(b"\x48\x8D\x15\x00\x00\x00\x00\x48\x8B\xCE\xE8\x00\x00\x00\x00", 7), 10)
        # lea rdx, NAME; ret: not a registration
        self.assertIsNone(optiondefaults._registration_call(b"\x48\x8D\x15\x00\x00\x00\x00\xC3\xCC\xCC\xCC\xCC", 7))

if __name__ == "__main__":
    unittest.main()
