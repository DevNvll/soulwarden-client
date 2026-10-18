"""The enum values of the client (open point O1 of the UI research): a static read of the client program.

The client registers each enum value of the script context with one call: a LEA of the name text, then a call of a
template function for that value. Down that call chain, a LEA points to a getter of 3 to 11 bytes that gives the value
(for example `mov eax, 2; ret`). The functions are shared by all enums with the same value and type: the getter is the
first one in the chain. This reader follows the chain to a depth of 4 and reads the getter. It does not run code.

Checks (tools/test-ui.sh, the enum fixture): the paperdoll slots of the protocol notes (Head 0, HeartGem 45, Num 50) and
the LyShine enums of the runtime (Source/SwcUi/Public/SwcUi/Model.h, the order of O3DE).
"""
from __future__ import annotations

import re
import struct
from pathlib import Path

BASE = 0x140000000


class Program:
    """The sections and the code of the client program (a PE file), read only."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        data = self.data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        optional = struct.unpack_from("<H", data, pe + 20)[0]
        table = pe + 24 + optional
        self.sections = []
        for i in range(count):
            name, _, va, size, raw = struct.unpack_from("<8sIIII", data, table + 40 * i)
            self.sections.append((name.rstrip(b"\0").decode(), va, size, raw))
        self.text = next(s for s in self.sections if s[0] == ".text")

    def offset(self, rva: int) -> int | None:
        for _, va, size, raw in self.sections:
            if va <= rva < va + size:
                return raw + rva - va
        return None

    def rva(self, offset: int) -> int | None:
        for _, va, size, raw in self.sections:
            if raw <= offset < raw + size:
                return va + offset - raw
        return None

    def in_code(self, rva: int) -> bool:
        _, va, size, _ = self.text
        return va <= rva < va + size

    def name_rvas(self, names: list[str]) -> dict[str, int]:
        """@return The address of the text of each name (a zero-terminated text with a zero before it)."""
        out = {}
        for name in names:
            found = self.data.find(b"\0" + name.encode() + b"\0")
            if found >= 0:
                rva = self.rva(found + 1)
                if rva is not None:
                    out[name] = rva
        return out

    def lea_references(self, targets: set[int]) -> dict[int, list[int]]:
        """@return For each target, the code addresses of the RIP-relative LEA instructions that point to it."""
        _, va, size, raw = self.text
        code = self.data[raw:raw + size]
        hits: dict[int, list[int]] = {}
        for match in re.finditer(rb"[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", code):
            i = match.start()
            rva = va + i + 7 + struct.unpack_from("<i", code, i + 3)[0]
            if rva in targets:
                hits.setdefault(rva, []).append(va + i)
        return hits

    def function_start(self, rva: int) -> bool:
        """@return True when an address is in the code and has padding or a ret before it (a function start)."""
        start = self.offset(rva)
        return start is not None and self.in_code(rva) and self.data[start - 1] in (0xCC, 0xC3)

    def references(self, rva: int, limit: int = 0x1400) -> tuple[list[int], list[int]]:
        """@return (call targets, LEA targets) of a function, up to the int3 padding after its first ret."""
        calls: list[int] = []
        leas: list[int] = []
        start = self.offset(rva)
        if start is None or not self.in_code(rva):
            return calls, leas
        data = self.data
        for i in range(limit):
            b = data[start + i]
            if b == 0xC3 and data[start + i + 1] == 0xCC:
                break
            if b == 0xE8:
                target = rva + i + 5 + struct.unpack_from("<i", data, start + i + 1)[0]
                if self.function_start(target):
                    calls.append(target)
            elif b in (0x48, 0x4C) and data[start + i + 1] == 0x8D and data[start + i + 2] in b"\x05\x0D\x15\x1D\x25\x2D\x35\x3D":
                leas.append(rva + i + 7 + struct.unpack_from("<i", data, start + i + 3)[0])
        return calls, leas

    def getter(self, rva: int) -> int | None:
        """@return The value of a getter (a constant return), or None when the code is not a getter."""
        start = self.offset(rva)
        if start is None:
            return None
        b = self.data[start:start + 12]
        if b[:3] in (b"\x33\xC0\xC3", b"\x32\xC0\xC3"):
            return 0
        if b[:4] == b"\x48\x33\xC0\xC3":
            return 0
        if b[0] == 0xB8 and b[5] == 0xC3:
            return struct.unpack_from("<i", b, 1)[0]
        if b[:4] == b"\x83\xC8\xFF\xC3":
            return -1
        if b[0] == 0xB0 and b[2] == 0xC3:
            return b[1]
        if b[:2] == b"\x66\xB8" and b[4] == 0xC3:
            return struct.unpack_from("<h", b, 2)[0]
        if b[:3] == b"\x48\xC7\xC0" and b[7] == 0xC3:
            return struct.unpack_from("<i", b, 3)[0]
        if b[:2] == b"\x48\xB8" and b[10] == 0xC3:
            return struct.unpack_from("<q", b, 2)[0]
        return None

    def is_registration(self, reference: int) -> bool:
        """@return True when the LEA has the form of a registration: `lea rdx, name`, `mov rcx, reg`, `call`."""
        start = self.offset(reference)
        b = self.data[start:start + 13]
        return (b[:3] == b"\x48\x8D\x15" and b[7] in (0x48, 0x49) and b[8] == 0x8B and (b[9] & 0xF8) == 0xC8
                and b[10] == 0xE8)

    def registered_value(self, reference: int) -> int | None:
        """@return The value of the registration at a LEA of a name text: the first getter down the call chain."""
        start = self.offset(reference)
        first = None
        for i in range(7, 40):
            if self.data[start + i] == 0xE8:
                target = reference + i + 5 + struct.unpack_from("<i", self.data, start + i + 1)[0]
                if self.function_start(target):
                    first = target
                    break
        if first is None:
            return None
        seen: set[int] = set()

        def search(function: int, depth: int) -> int | None:
            if function in seen or depth > 4:
                return None
            seen.add(function)
            calls, leas = self.references(function)
            for target in leas:
                value = self.getter(target)
                if value is not None:
                    return value
            for call in calls:
                value = search(call, depth + 1)
                if value is not None:
                    return value
            return None

        return search(first, 0)


def enum_values(program_path: Path, names: list[str]) -> tuple[dict[str, int], dict[str, str]]:
    """@return (name -> value, name -> the cause when the value is not known)."""
    program = Program(program_path)
    rvas = program.name_rvas(names)
    refs = program.lea_references(set(rvas.values()))
    values: dict[str, int] = {}
    unknown: dict[str, str] = {}
    for name in names:
        if name not in rvas:
            unknown[name] = "no text in the client"
            continue
        candidates = refs.get(rvas[name], [])
        strict = sorted(r for r in candidates if program.is_registration(r))
        if strict:
            # A name text with several registrations (ePaperDollSlotTypes_QuickSlot6 has 5, values 19 to 23): the
            # script context keeps the first name (an insert does not replace it).
            found = [v for v in (program.registered_value(r) for r in strict) if v is not None][:1]
        else:
            found = sorted({program.registered_value(r) for r in candidates} - {None})
        if not found:
            unknown[name] = "no registration with a getter"
        elif len(found) > 1:
            unknown[name] = "different values: " + ", ".join(str(v) for v in found)
        else:
            values[name] = found[0]
    return values, unknown
