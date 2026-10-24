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


if __name__ == "__main__":
    unittest.main()
