"""A reader of Lua 5.1 bytecode (the scripts of the game: 2 bytes, then a stock 5.1 chunk; research ui.md 3.1).

It reads the function prototypes with their code and constants. The UI tools use it for facts about the scripts: the
global names that they read and write (the native surface, the enum names) and their string constants (the screen
paths of the loaders). Format: lundump.c of Lua 5.1.5 (header of 12 bytes; int 4, size_t 8, instruction 4, number 8).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

OP_LOADK = 1
OP_GETGLOBAL = 5
OP_SETGLOBAL = 7
HEADER = b"\x1bLua\x51\x00\x01\x04\x08\x04\x08\x00"


@dataclass
class Proto:
    source: str
    code: list[int] = field(default_factory=list)
    constants: list = field(default_factory=list)
    protos: list["Proto"] = field(default_factory=list)

    def walk(self):
        yield self
        for child in self.protos:
            yield from child.walk()


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, size: int) -> bytes:
        out = self.data[self.pos:self.pos + size]
        if len(out) != size:
            raise ValueError("Lua bytecode: the chunk ends early")
        self.pos += size
        return out

    def byte(self) -> int:
        return self.take(1)[0]

    def int(self) -> int:
        return struct.unpack("<i", self.take(4))[0]

    def string(self) -> str | None:
        size = struct.unpack("<Q", self.take(8))[0]
        if size == 0:
            return None
        return self.take(size)[:-1].decode("utf-8", "replace")

    def function(self) -> Proto:
        proto = Proto(self.string() or "")
        self.int()                        # line defined
        self.int()                        # last line defined
        self.take(4)                      # upvalues, parameters, vararg flag, stack size
        count = self.int()
        proto.code = list(struct.unpack("<%dI" % count, self.take(4 * count)))
        for _ in range(self.int()):
            kind = self.byte()
            if kind == 0:
                proto.constants.append(None)
            elif kind == 1:
                proto.constants.append(self.byte() != 0)
            elif kind == 3:
                proto.constants.append(struct.unpack("<d", self.take(8))[0])
            elif kind == 4:
                proto.constants.append(self.string())
            else:
                raise ValueError(f"Lua bytecode: constant type {kind}")
        proto.protos = [self.function() for _ in range(self.int())]
        self.take(4 * self.int())         # line info
        for _ in range(self.int()):       # local names
            self.string()
            self.take(8)
        for _ in range(self.int()):       # upvalue names
            self.string()
        return proto


def load(data: bytes) -> Proto:
    """@return The main function of a chunk. A script file of the game has 2 bytes before the chunk."""
    start = data.find(HEADER[:4])
    if start < 0 or data[start:start + len(HEADER)] != HEADER:
        raise ValueError("Lua bytecode: not a Lua 5.1 chunk with the expected sizes")
    reader = Reader(data)
    reader.pos = start + len(HEADER)
    return reader.function()


def globals_used(main: Proto) -> tuple[set[str], set[str]]:
    """@return (the global names that the chunk reads, the global names that it writes)."""
    read: set[str] = set()
    written: set[str] = set()
    for proto in main.walk():
        for ins in proto.code:
            op = ins & 0x3F
            if op in (OP_GETGLOBAL, OP_SETGLOBAL):
                name = proto.constants[ins >> 14]
                (read if op == OP_GETGLOBAL else written).add(name)
    return read, written


def string_constants(main: Proto) -> list[str]:
    """@return Each string constant of the chunk and its functions, in order."""
    return [k for proto in main.walk() for k in proto.constants if isinstance(k, str)]
