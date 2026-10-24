"""A static test of unreal/SoulwardenClient/Scripts/swc_materials.py (no engine): each connection from a VertexColor node uses a pin
name that the node has. The outputs of MaterialExpressionVertexColor have no names (RGB, R, G, B, A by their masks), and
UMaterialEditingLibrary.ConnectMaterialExpressions finds an unnamed output only by "" (output 0, RGB) or "R", "G", "B", "A". A pin
"RGB" or "RGBA" connects nothing, and the input keeps its default (AST-278: the Lerp of the vertex tint used ConstB = 1)."""
import re
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().parents[2] / "unreal" / "SoulwardenClient" / "Scripts" / "swc_materials.py"
VERTEX_PINS = {"", "R", "G", "B", "A"}


class MaterialPins(unittest.TestCase):
    def test_vertex_color_pins(self):
        text = SCRIPT.read_text()
        names = set(re.findall(r"(\w+)\s*=\s*node\(material,\s*unreal\.MaterialExpressionVertexColor", text))
        self.assertTrue(names, "no VertexColor node found")
        uses = []
        for name in names:
            uses += re.findall(r"\(\s*%s\s*,\s*\"([A-Za-z]*)\"\s*\)" % name, text)
            uses += re.findall(r"connect_material_expressions\(\s*%s\s*,\s*\"([A-Za-z]*)\"" % name, text)
        self.assertTrue(uses)
        self.assertEqual(sorted(set(uses) - VERTEX_PINS), [], "a VertexColor pin name that the node does not have")


class CustomInputs(unittest.TestCase):
    def test_custom_input_names_differ_without_case(self):
        """The inputs of a Custom node are FNames: two names that differ only in case are one input, and the second stays empty
        (AST-275: M_SwcFxMesh version 1 had PAL and Pal and did not compile)."""
        text = SCRIPT.read_text()
        checked = 0
        for match in re.finditer(r"custom\(material,[^\n]*?,\s*\(([^)]*)\)", text):
            names = re.findall(r'"([A-Za-z0-9_]+)"', match[1])
            checked += 1
            lower = [n.lower() for n in names]
            self.assertEqual(len(lower), len(set(lower)), f"names that clash without case: {names}")
        pins = re.findall(r"pins = \{(.*?)\}", text, re.S) + re.findall(r"pins\.update\(\{(.*?)\}", text, re.S)
        for block in pins:
            names = re.findall(r'"([A-Za-z0-9_]+)":', block)
            lower = [n.lower() for n in names]
            self.assertEqual(len(lower), len(set(lower)), f"pin names that clash without case: {names}")
        self.assertGreater(checked, 10)


if __name__ == "__main__":
    unittest.main()
