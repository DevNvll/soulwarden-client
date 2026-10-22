"""Tests of the names of the creature clip assets (AST-249; no game data): Interchange wrote a sequence STEM_DIGITS as STEM +
"Null" + _DIGITS, thus no clip name has digits only."""
import hashlib
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import creature  # noqa: E402


def clip_with_digit_hash() -> str:
    """@return A clip name whose first 8 hexadecimal digits of the SHA-1 are digits only."""
    for index in range(100000):
        name = f"clip_{index}"
        if hashlib.sha1(name.encode()).hexdigest()[:8].isdigit():
            return name
    raise AssertionError("no clip name with a digit hash")


class ClipNames(unittest.TestCase):
    def test_digit_hash_gets_a_letter(self):
        name = clip_with_digit_hash()
        digest = hashlib.sha1(name.encode()).hexdigest()[:8]
        self.assertEqual(creature.clip_asset_name(name), "_c" + digest[1:])
        self.assertTrue(creature.legacy_digit_name(name))

    def test_hex_hash_stays(self):
        for index in range(200):
            name = f"other_{index}"
            digest = hashlib.sha1(name.encode()).hexdigest()[:8]
            if not digest.isdigit():
                self.assertEqual(creature.clip_asset_name(name), "_" + digest)
                self.assertFalse(creature.legacy_digit_name(name))

    def test_no_name_of_digits_only(self):
        for index in range(20000):
            self.assertFalse(creature.clip_asset_name(f"name_{index}")[1:].isdigit())


if __name__ == "__main__":
    unittest.main()
