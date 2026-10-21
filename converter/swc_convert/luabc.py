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


# --------------------------------------------------------------------------------------------- the run of a table script

class Table:
    """A Lua table of run_tables: the hash part and the array part (index 1 is items[0])."""

    def __init__(self) -> None:
        self.hash: dict = {}
        self.items: list = []

    def get(self, key):
        if isinstance(key, float) and key.is_integer() and 1 <= key <= len(self.items):
            return self.items[int(key) - 1]
        return self.hash.get(key)

    def set(self, key, value) -> None:
        if isinstance(key, float) and key.is_integer() and 1 <= key <= len(self.items) + 1:
            if int(key) == len(self.items) + 1:
                self.items.append(value)
            else:
                self.items[int(key) - 1] = value
            return
        self.hash[key] = value

    def plain(self):
        """@return The table as Python values: a list (array part only) or a dict (the hash part, then the items by index)."""
        def conv(v):
            return v.plain() if isinstance(v, Table) else v
        if self.items and not self.hash:
            return [conv(v) for v in self.items]
        out = {str(k): conv(v) for k, v in self.hash.items()}
        out.update({str(i + 1): conv(v) for i, v in enumerate(self.items)})
        return out


class Symbol(str):
    """A value that the run does not know (a global or a field of an unknown value): its dotted name."""


def run_tables(main: Proto, calls: dict | None = None, limit: int = 200000):
    """Runs the main function of a chunk that builds tables (the weapon effect scripts: no loop, no branch on data). The known
    calls: `calls` maps a global name to a Python function of the arguments; any other global is a Symbol; a field of a Symbol is
    the Symbol "NAME.FIELD"; a call of a Symbol returns nothing; a closure is the Symbol "function". Only the opcodes of such
    scripts are run (lopcodes.h of Lua 5.1.5); another opcode raises ValueError. @return The values of the first RETURN."""
    calls = calls or {}
    regs: list = [None] * 256
    consts = main.constants
    globals_: dict = {}
    pc = 0
    steps = 0

    def rk(x):
        return consts[x & 0xFF] if x & 0x100 else regs[x]

    def index(obj, key):
        if isinstance(obj, Table):
            return obj.get(key)
        if isinstance(obj, Symbol):
            return Symbol(f"{obj}.{key}")
        return None

    while pc < len(main.code):
        steps += 1
        if steps > limit:
            raise ValueError("Lua run: too many steps")
        ins = main.code[pc]
        pc += 1
        op, a = ins & 0x3F, (ins >> 6) & 0xFF
        c, b, bx = (ins >> 14) & 0x1FF, (ins >> 23) & 0x1FF, ins >> 14
        if op == 0:                                    # MOVE
            regs[a] = regs[b]
        elif op == 1:                                  # LOADK
            regs[a] = consts[bx]
        elif op == 2:                                  # LOADBOOL
            regs[a] = bool(b)
            if c:
                pc += 1
        elif op == 3:                                  # LOADNIL
            for r in range(a, b + 1):
                regs[r] = None
        elif op == 5:                                  # GETGLOBAL
            name = consts[bx]
            regs[a] = globals_[name] if name in globals_ else Symbol(name)
        elif op == 6:                                  # GETTABLE
            regs[a] = index(regs[b], rk(c))
        elif op == 7:                                  # SETGLOBAL
            globals_[consts[bx]] = regs[a]
        elif op == 9:                                  # SETTABLE
            if isinstance(regs[a], Table):
                regs[a].set(rk(b), rk(c))
        elif op == 10:                                 # NEWTABLE
            regs[a] = Table()
        elif op == 11:                                 # SELF
            regs[a + 1] = regs[b]
            regs[a] = index(regs[b], rk(c))
        elif op == 18:                                 # UNM
            regs[a] = -regs[b] if isinstance(regs[b], float) else None
        elif op == 28:                                 # CALL
            func = regs[a]
            count = (b - 1) if b else 0
            args = [regs[a + 1 + i] for i in range(count)]
            result = None
            if isinstance(func, Symbol) and str(func) in calls:
                result = calls[str(func)](*args)
            regs[a] = result
            for r in range(a + 1, a + max(c - 1, 1)):
                regs[r] = None
        elif op == 30:                                 # RETURN
            return [regs[a + i] for i in range(b - 1)] if b else [regs[a]]
        elif op == 34:                                 # SETLIST (50 fields to a flush)
            if c == 0:
                c = main.code[pc]
                pc += 1
            for i in range(1, b + 1):
                regs[a].set(float((c - 1) * 50 + i), regs[a + i])
        elif op == 35:                                 # CLOSE
            pass
        elif op == 36:                                 # CLOSURE (its upvalue moves follow; a function value is a Symbol)
            regs[a] = Symbol("function")
        else:
            raise ValueError(f"Lua run: opcode {op} at {pc - 1}")
    return []


def calls_of(proto: Proto, params: int, upvalues: list | None = None, limit: int = 20000) -> list[tuple[str, list]]:
    """@return The calls of a function of a script in the order of its code (the hit functions of scripts/combatimpact: a list of
    particle calls, some under a nil test). The run is linear: the branches and the loops are not followed (each instruction runs
    once), so the calls of both sides of a test are in the list. The parameters are the Symbols "a0" ... (a0 is self of a method);
    an upvalue is the Symbol of `upvalues` or "upvalue N"; a product of a Symbol and a number is the Symbol "S*N"; a call returns
    the Symbol "NAME()". @return [(the name of the called Symbol, its arguments)]."""
    regs: list = [None] * 256
    for i in range(params):
        regs[i] = Symbol(f"a{i}")
    consts = proto.constants
    out: list[tuple[str, list]] = []
    top = 0   # the top of the stack after a call with open results (CALL C 0), for a call with open arguments (CALL B 0)

    def rk(x):
        return consts[x & 0xFF] if x & 0x100 else regs[x]

    def index(obj, key):
        if isinstance(obj, Table):
            return obj.get(key)
        if isinstance(obj, Symbol):
            return Symbol(f"{obj}.{key}")
        return None

    for ins in proto.code[:limit]:
        op, a = ins & 0x3F, (ins >> 6) & 0xFF
        c, b, bx = (ins >> 14) & 0x1FF, (ins >> 23) & 0x1FF, ins >> 14
        if op == 0:
            regs[a] = regs[b]
        elif op == 1:
            regs[a] = consts[bx]
        elif op == 2:
            regs[a] = bool(b)
        elif op == 3:
            for r in range(a, b + 1):
                regs[r] = None
        elif op == 4:
            regs[a] = Symbol(upvalues[b]) if upvalues and b < len(upvalues) else Symbol(f"upvalue {b}")
        elif op == 5:
            regs[a] = Symbol(consts[bx])
        elif op == 6:
            regs[a] = index(regs[b], rk(c))
        elif op == 9:
            if isinstance(regs[a], Table):
                regs[a].set(rk(b), rk(c))
        elif op == 10:
            regs[a] = Table()
        elif op == 11:
            regs[a + 1] = regs[b]
            regs[a] = index(regs[b], rk(c))
        elif 12 <= op <= 17:                           # ADD SUB MUL DIV MOD POW
            x, y = rk(b), rk(c)
            sign = "+-*/%^"[op - 12]
            if isinstance(x, float) and isinstance(y, float):
                regs[a] = {12: x + y, 13: x - y, 14: x * y}.get(op)
            else:
                regs[a] = Symbol(f"{x}{sign}{y:g}" if isinstance(y, float) else f"{x}{sign}{y}")
        elif op == 18:
            regs[a] = -regs[b] if isinstance(regs[b], float) else Symbol(f"-{regs[b]}")
        elif op in (28, 29):                           # CALL, TAILCALL
            func = regs[a]
            count = (b - 1) if b else max(top - a - 1, 0)
            args = [regs[a + 1 + i] for i in range(count)]
            name = str(func) if isinstance(func, Symbol) else ""
            if name == "Vector3":
                regs[a] = list(args)
            else:
                if name:
                    out.append((name, args))
                regs[a] = Symbol(f"{name}()")
            top = a + 1
        elif op == 34:
            if c == 0:
                continue
            if isinstance(regs[a], Table):
                for i in range(1, b + 1):
                    regs[a].set(float((c - 1) * 50 + i), regs[a + i])
        elif op == 36:
            regs[a] = Symbol("function")
        # The other opcodes (tests, jumps, loops, returns, concatenation, upvalue writes) change no value of this list.
    return out


def assigned_functions(main: Proto) -> dict[str, int]:
    """@return The functions that the main function of a chunk puts into the fields of a global table (NAME.FIELD = function): the
    dotted name -> the index of the function in main.protos."""
    regs: list = [None] * 256
    consts = main.constants
    out: dict[str, int] = {}
    for ins in main.code:
        op, a = ins & 0x3F, (ins >> 6) & 0xFF
        c, b, bx = (ins >> 14) & 0x1FF, (ins >> 23) & 0x1FF, ins >> 14
        if op == 5:
            regs[a] = Symbol(consts[bx])
        elif op == 6:
            key = consts[c & 0xFF] if c & 0x100 else None
            regs[a] = Symbol(f"{regs[b]}.{key}") if isinstance(regs[b], Symbol) and key is not None else None
        elif op == 36:
            regs[a] = ("function", bx)
        elif op == 9:
            key = consts[b & 0xFF] if b & 0x100 else None
            value = consts[c & 0xFF] if c & 0x100 else regs[c]
            if isinstance(regs[a], Symbol) and isinstance(key, str) and isinstance(value, tuple):
                out[f"{regs[a]}.{key}"] = value[1]
        elif op == 0:
            regs[a] = regs[b]
    return out
