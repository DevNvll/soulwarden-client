#!/usr/bin/env python3
"""A listing of a script of the game (Lua 5.1 bytecode) for the research of the original UI scripts (stage P2.10).

Each function prototype with its name (the table key that the main chunk gives to its closure: "RespawnScreen:OnInit"),
then its instructions with the constants and the global names in clear text. No code of the script runs.

Usage: python3 -B tools/lib/lua_listing.py SCRIPT.luac [FUNCTION_NAME_PART]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "converter"))
from swc_convert import luabc  # noqa: E402

OPS = ["MOVE", "LOADK", "LOADBOOL", "LOADNIL", "GETUPVAL", "GETGLOBAL", "GETTABLE", "SETGLOBAL", "SETUPVAL", "SETTABLE",
       "NEWTABLE", "SELF", "ADD", "SUB", "MUL", "DIV", "MOD", "POW", "UNM", "NOT", "LEN", "CONCAT", "JMP", "EQ", "LT", "LE",
       "TEST", "TESTSET", "CALL", "TAILCALL", "RETURN", "FORLOOP", "FORPREP", "TFORLOOP", "SETLIST", "CLOSE", "CLOSURE", "VARARG"]
ABX = {"LOADK", "GETGLOBAL", "SETGLOBAL", "CLOSURE"}
ASBX = {"JMP", "FORLOOP", "FORPREP"}


def rk(proto: luabc.Proto, value: int) -> str:
    if value & 256:
        constant = proto.constants[value - 256]
        return repr(constant)
    return f"r{value}"


def text(proto: luabc.Proto, index: int, ins: int) -> str:
    op = OPS[ins & 0x3F]
    a = (ins >> 6) & 0xFF
    c = (ins >> 14) & 0x1FF
    b = (ins >> 23) & 0x1FF
    bx = ins >> 14
    if op in ABX:
        detail = repr(proto.constants[bx]) if op != "CLOSURE" else f"function {bx}"
        return f"{index:4} {op:9} r{a} {detail}"
    if op in ASBX:
        return f"{index:4} {op:9} r{a} to {index + 1 + bx - 131071}"
    if op in ("GETTABLE", "SELF"):
        return f"{index:4} {op:9} r{a} = r{b}[{rk(proto, c)}]"
    if op == "SETTABLE":
        return f"{index:4} {op:9} r{a}[{rk(proto, b)}] = {rk(proto, c)}"
    if op in ("EQ", "LT", "LE"):
        return f"{index:4} {op:9} {a} {rk(proto, b)} {rk(proto, c)}"
    if op in ("ADD", "SUB", "MUL", "DIV", "MOD", "POW"):
        return f"{index:4} {op:9} r{a} = {rk(proto, b)} {rk(proto, c)}"
    if op in ("GETUPVAL", "SETUPVAL"):
        return f"{index:4} {op:9} r{a} upvalue {b}"
    return f"{index:4} {op:9} {a} {b} {c}"


def names(main: luabc.Proto) -> dict[int, str]:
    """The name of each closure of the main chunk: the string key of the SETTABLE that stores it."""
    out: dict[int, str] = {}
    table_names: dict[int, str] = {}
    pending: dict[int, int] = {}
    for ins in main.code:
        op = OPS[ins & 0x3F]
        a = (ins >> 6) & 0xFF
        if op == "GETGLOBAL":
            table_names[a] = str(main.constants[ins >> 14])
        elif op == "CLOSURE":
            pending[a] = ins >> 14
        elif op == "SETTABLE":
            b = (ins >> 23) & 0x1FF
            c = (ins >> 14) & 0x1FF
            if (c & 256) == 0 and c in pending and b & 256:
                out[pending.pop(c)] = f"{table_names.get(a, 't')}:{main.constants[b - 256]}"
        elif op == "SETGLOBAL" and a in pending:
            out[pending.pop(a)] = str(main.constants[ins >> 14])
    return out


def main() -> int:
    data = Path(sys.argv[1]).read_bytes()
    chunk = luabc.load(data)
    wanted = sys.argv[2] if len(sys.argv) > 2 else ""
    labels = names(chunk)
    for index, proto in enumerate(chunk.protos):
        label = labels.get(index, f"function {index}")
        if wanted and wanted.lower() not in label.lower():
            continue
        print(f"== {label}")
        for position, ins in enumerate(proto.code):
            print(text(proto, position, ins))
        for child_index, child in enumerate(proto.protos):
            print(f"  -- closure {child_index} of {label}")
            for position, ins in enumerate(child.code):
                print("  " + text(child, position, ins))
    return 0


if __name__ == "__main__":
    sys.exit(main())
