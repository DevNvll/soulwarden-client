"""The defaults of the options of the client (stage P2.3): a static read of the client program, for the original Options
screen (the data nodes Hud.LocalPlayer.Options.*). It does not run code. Output: gamedata/options.json (converted content,
never in git).

1. The fields: the AZ reflection of SerializedOptionsData registers each field with `mov r8d, OFFSET` and `lea rdx, NAME`
   (in either order), then `call Field<T>`. The call target gives the type (the targets of three known fields:
   m_cameraShake bool, m_masterVolume int, m_gamma float).
2. The defaults: the factory of the class (the first code address after the class name in the reflection) constructs a
   stack object with the constructor (its first call). The constructor stores the defaults: `mov [this+disp], imm`
   (byte, word, dword, qword) and stores of the zero register (rbp after `xor ebp, ebp`; r15 while it is a copy of
   ebp). `this` is rcx, then r14.
3. The data nodes: the component binds each node to a member (`lea rdx, NAME` ... `lea rdx, [rsi+X]`); X less the place of
   the struct in the component (the most common X - OFFSET of the nodes whose name is the field name) gives the field.
   A node with no binding takes the field whose name has the same words (CamelCase; "ADS" and "Horizontal" in any order).
Checks: tests/converter/test_option_defaults.py (the store decoder); the options fixture (tools/test-ui.sh).
"""
from __future__ import annotations

import re
import struct
from collections import Counter
from pathlib import Path

from .uienums import Program

CLASS = "SerializedOptionsData"
NODE_PREFIX = "Hud.LocalPlayer.Options."
ANCHORS = {"m_cameraShake": "bool", "m_masterVolume": "int", "m_gamma": "float"}
SIZES = {"bool": 1, "int": 4, "float": 4}


def _code_end(data: bytes, start: int, limit: int) -> int:
    """@return The offset after the first `ret; int3` of a function (or start + limit)."""
    for i in range(start, start + limit):
        if data[i] == 0xC3 and data[i + 1] == 0xCC:
            return i + 1
    return start + limit


def _registration_call(data: bytes, i: int) -> int | None:
    """@return The offset of the `call` after the `lea rdx, NAME` of a registration (i: the byte after the lea), or None.
    The instructions between them: `mov r8d, imm32` (41 B8), `mov rcx, REG` (48/49 8B C8-CF), `movdqa [rbp+disp32], xmm0`
    (66 0F 7F 85), `lea r9, [rbp+disp32]` (4C 8D 8D), `xorps xmm0, xmm0` (0F 57 C0)."""
    for _ in range(6):
        if data[i] == 0xE8:
            return i
        if data[i:i + 2] == b"\x41\xB8":
            i += 6
        elif data[i] in (0x48, 0x49) and data[i + 1] == 0x8B and 0xC8 <= data[i + 2] <= 0xCF:
            i += 3
        elif data[i:i + 3] == b"\x0F\x57\xC0":
            i += 3
        elif data[i:i + 4] == b"\x66\x0F\x7F\x85":
            i += 8
        elif data[i:i + 3] == b"\x4C\x8D\x8D":
            i += 7
        else:
            return None
    return None


def read_fields(program: Program) -> list[dict]:
    """@return The reflected fields of SerializedOptionsData: name, offset, and the call target of the registration."""
    data = program.data
    name_rva = program.name_rvas([CLASS])[CLASS]
    refs = program.lea_references({name_rva}).get(name_rva, [])
    names = {}
    fields: list[dict] = []
    for ref in refs:
        start = program.offset(ref)
        end = start + 0x5000
        # Each registration: `lea rdx, NAME` and `mov r8d, OFFSET` in either order (the compiler puts some moves after the
        # lea), then `call Field<T>`. The move of a field is between the call of the field before and its own call.
        previous_call_end = start
        j = start
        while j < end:
            if not (data[j] == 0x48 and data[j + 1] == 0x8D and data[j + 2] == 0x15):
                j += 1
                continue
            call_at = _registration_call(data, j + 7)
            if call_at is None:
                j += 1
                continue
            target = program.rva(j) + 7 + struct.unpack_from("<i", data, j + 3)[0]
            if target not in names:
                off = program.offset(target)
                names[target] = data[off:data.index(b"\0", off)].decode("latin1") if off else ""
            name = names[target]
            offset = None
            for m in re.finditer(rb"\x41\xB8(....)", data[previous_call_end:call_at], re.S):
                at = previous_call_end + m.start()
                if at + 6 <= j or at >= j + 7:  # not inside the lea
                    offset = struct.unpack_from("<I", m.group(1))[0]
            call = program.rva(call_at) + 5 + struct.unpack_from("<i", data, call_at + 1)[0]
            if name.startswith("m_") and offset is not None:
                fields.append({"name": name, "offset": offset, "call": call})
            previous_call_end = call_at + 5
            j = call_at + 5
        if len(fields) > 100:
            return fields
        fields = []
    raise ValueError("the reflection of " + CLASS + " was not found")


def find_constructor(program: Program) -> int:
    """@return The address of the constructor: the first call of the factory (the first code address after the class name
    in the reflection)."""
    data = program.data
    name_rva = program.name_rvas([CLASS])[CLASS]
    for ref in program.lea_references({name_rva}).get(name_rva, []):
        start = program.offset(ref)
        for m in re.finditer(rb"\x48\x8D\x05(....)", data[start:start + 0x200], re.S):
            target = program.rva(start + m.start()) + 7 + struct.unpack_from("<i", m.group(1))[0]
            if not program.function_start(target):
                continue
            calls, _ = program.references(target, 0x80)
            if calls:
                return calls[0]
    raise ValueError("the factory of " + CLASS + " was not found")


def constructor_stores(code: bytes) -> dict[int, int]:
    """@return The bytes that a constructor stores at [this+disp] (this: rcx, or r14 with a REX.B prefix), by offset.
    The later store of a byte wins. The zero register: rbp/ebp/bpl after `xor ebp, ebp` (33 ED or 31 ED); r15 while it
    is a copy of ebp (`mov r15d, ebp`, until `lea r15, [rip+...]`)."""
    out: dict[int, int] = {}
    zero_rbp = False
    zero_r15 = False
    i = 0
    n = len(code)

    def modrm(b: int, rex_b: bool) -> tuple[int, int] | None:
        """@return (the reg field, the displacement size) of a ModRM byte with the base this."""
        mod, reg, rm = b >> 6, (b >> 3) & 7, b & 7
        base = 6 if rex_b else 1
        if rm != base or mod not in (1, 2):
            return None
        return reg, 1 if mod == 1 else 4

    while i < n - 2:
        b0 = code[i]
        if code[i:i + 2] in (b"\x33\xED", b"\x31\xED"):
            zero_rbp = True
            i += 2
            continue
        if code[i:i + 3] == b"\x44\x8B\xFD":
            zero_r15 = zero_rbp
            i += 3
            continue
        if code[i:i + 3] == b"\x4C\x8D\x3D":
            zero_r15 = False
            i += 7
            continue
        prefix16 = b0 == 0x66
        j = i + 1 if prefix16 else i
        rex = code[j] if 0x40 <= code[j] <= 0x4F else 0
        k = j + 1 if rex else j
        if k + 2 >= n:
            break
        op = code[k]
        rex_w, rex_r, rex_b = bool(rex & 8), bool(rex & 4), bool(rex & 1)
        if op in (0xC7, 0xC6, 0x89, 0x88):
            mr = modrm(code[k + 1], rex_b)
            if mr is not None:
                reg, dsize = mr
                disp = code[k + 2] if dsize == 1 else struct.unpack_from("<I", code, k + 2)[0]
                at = k + 2 + dsize
                if op in (0xC7, 0xC6) and reg == 0:
                    if op == 0xC6:
                        value, size, isize = code[at], 1, 1
                    elif prefix16:
                        value, size, isize = struct.unpack_from("<H", code, at)[0], 2, 2
                    else:
                        value = struct.unpack_from("<i", code, at)[0]
                        size, isize = (8, 4) if rex_w else (4, 4)
                    for x in range(size):
                        out[disp + x] = (value >> (8 * x)) & 0xFF
                    i = at + isize
                    continue
                if op in (0x89, 0x88):
                    register = reg + (8 if rex_r else 0)
                    size = 1 if op == 0x88 else (2 if prefix16 else (8 if rex_w else 4))
                    if (register == 5 and zero_rbp) or (register == 15 and zero_r15):
                        for x in range(size):
                            out[disp + x] = 0
                    i = at
                    continue
        i += 1
    return out


def _value(raw: list[int | None], kind: str):
    if any(b is None for b in raw):
        return None
    data = bytes(raw)
    if kind == "bool":
        return data[0] != 0
    if kind == "int":
        return struct.unpack("<i", data)[0]
    return round(struct.unpack("<f", data)[0], 6)


def _words(name: str) -> tuple[str, ...]:
    """The words of a CamelCase name, lower case, in sorted order (m_ and the node path are dropped)."""
    name = name[2:] if name.startswith("m_") else name.split(".")[-1]
    return tuple(sorted(w.lower() for w in re.findall(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+", name[0].upper() + name[1:])))


def read_option_defaults(program_path: Path) -> dict:
    """@return The fields (offset, type, default) and the data nodes (the field and its default) of the options."""
    program = Program(program_path)
    data = program.data
    fields = read_fields(program)
    by_name = {f["name"]: f for f in fields}
    types = {by_name[name]["call"]: kind for name, kind in ANCHORS.items() if name in by_name}
    ctor = find_constructor(program)
    start = program.offset(ctor)
    stores = constructor_stores(data[start:_code_end(data, start, 0x4000)])
    out_fields = {}
    for f in fields:
        kind = types.get(f["call"])
        if kind is None:
            continue
        raw = [stores.get(f["offset"] + x) for x in range(SIZES[kind])]
        out_fields[f["name"]] = {"offset": f["offset"], "type": kind, "default": _value(raw, kind)}

    # The data nodes: each text Hud.LocalPlayer.Options.* of the program, and the member that the component binds to it.
    node_names = sorted({m.decode() for m in re.findall(rb"Hud\.LocalPlayer\.Options\.[A-Za-z0-9_.]+(?=\x00)", data)})
    rvas = program.name_rvas(node_names)
    refs = program.lea_references(set(rvas.values()))
    bound: dict[str, int] = {}
    for node, rva in rvas.items():
        for ref in refs.get(rva, []):
            off = program.offset(ref)
            window = data[off + 7:off + 0x40]
            m = re.search(rb"\x48\x8D\x96(....)|\x48\x8D\x56(.)", window, re.S)
            if m:
                bound[node] = struct.unpack("<I", m.group(1))[0] if m.group(1) else m.group(2)[0]
                break
    by_offset = {f["offset"]: name for name, f in out_fields.items()}
    by_words: dict[tuple, list[str]] = {}
    for name in out_fields:
        by_words.setdefault(_words(name), []).append(name)
    deltas = Counter()
    for node, x in bound.items():
        for name in by_words.get(_words(node), []):
            deltas[x - out_fields[name]["offset"]] += 1
    delta = deltas.most_common(1)[0][0] if deltas else None
    nodes = {}
    unknown = []
    for node in node_names:
        name, how = None, None
        x = bound.get(node)
        if x is not None and delta is not None and (x - delta) in by_offset:
            name, how = by_offset[x - delta], "binding"
        else:
            candidates = by_words.get(_words(node), [])
            if len(candidates) == 1:
                name, how = candidates[0], "name"
        if name is None or out_fields[name]["default"] is None:
            unknown.append(node[len(NODE_PREFIX):])
            continue
        nodes[node[len(NODE_PREFIX):]] = {"field": name, "how": how, "type": out_fields[name]["type"],
                                          "default": out_fields[name]["default"]}
    return {"program": {"file": "Bin64/NewWorld.exe", "size": len(data)},
            "constructor": hex(0x140000000 + ctor), "structInComponent": delta,
            "fields": out_fields, "nodes": nodes, "unknown": unknown}
