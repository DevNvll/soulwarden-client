"""A decoder of BC6H blocks to floats (the HDR probe cubemaps of the environment, stage E5).

The block layout of the public D3D11 BC6H format: 14 modes, the endpoint bits of each mode in their stream order, the 32 two-region
partitions of BC7, the unquantization, and the interpolation weights. Unsigned (DXGI 95) and signed (DXGI 96) blocks. Pure Python
and numpy: a probe cubemap of 6 x 256 x 256 texels decodes in some seconds.
"""
from __future__ import annotations

import numpy as np

# The endpoint bits of each mode in the order of the stream (after the mode bits): (field, bit). The fields: w, x, y, z are the
# endpoints (w base, x its pair; y and z the second region), each with r, g, b. A range (field, hi, lo) has its bit lo first.
_LAYOUTS = {
    1: [("gy", 4), ("by", 4), ("bz", 4), ("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 4, 0), ("gz", 4), ("gy", 3, 0), ("gx", 4, 0),
        ("bz", 0), ("gz", 3, 0), ("bx", 4, 0), ("bz", 1), ("by", 3, 0), ("ry", 4, 0), ("bz", 2), ("rz", 4, 0), ("bz", 3)],
    2: [("gy", 5), ("gz", 4), ("gz", 5), ("rw", 6, 0), ("bz", 0), ("bz", 1), ("by", 4), ("gw", 6, 0), ("by", 5), ("bz", 2), ("gy", 4),
        ("bw", 6, 0), ("bz", 3), ("bz", 5), ("bz", 4), ("rx", 5, 0), ("gy", 3, 0), ("gx", 5, 0), ("gz", 3, 0), ("bx", 5, 0),
        ("by", 3, 0), ("ry", 5, 0), ("rz", 5, 0)],
    3: [("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 4, 0), ("rw", 10), ("gy", 3, 0), ("gx", 3, 0), ("gw", 10), ("bz", 0),
        ("gz", 3, 0), ("bx", 3, 0), ("bw", 10), ("bz", 1), ("by", 3, 0), ("ry", 4, 0), ("bz", 2), ("rz", 4, 0), ("bz", 3)],
    4: [("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 3, 0), ("rw", 10), ("gz", 4), ("gy", 3, 0), ("gx", 4, 0), ("gw", 10),
        ("gz", 3, 0), ("bx", 3, 0), ("bw", 10), ("bz", 1), ("by", 3, 0), ("ry", 3, 0), ("bz", 0), ("bz", 2), ("rz", 3, 0), ("gy", 4),
        ("bz", 3)],
    5: [("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 3, 0), ("rw", 10), ("by", 4), ("gy", 3, 0), ("gx", 3, 0), ("gw", 10),
        ("bz", 0), ("gz", 3, 0), ("bx", 4, 0), ("bw", 10), ("by", 3, 0), ("ry", 3, 0), ("bz", 1), ("bz", 2), ("rz", 3, 0), ("bz", 4),
        ("bz", 3)],
    6: [("rw", 8, 0), ("by", 4), ("gw", 8, 0), ("gy", 4), ("bw", 8, 0), ("bz", 4), ("rx", 4, 0), ("gz", 4), ("gy", 3, 0), ("gx", 4, 0),
        ("bz", 0), ("gz", 3, 0), ("bx", 4, 0), ("bz", 1), ("by", 3, 0), ("ry", 4, 0), ("bz", 2), ("rz", 4, 0), ("bz", 3)],
    7: [("rw", 7, 0), ("gz", 4), ("by", 4), ("gw", 7, 0), ("bz", 2), ("gy", 4), ("bw", 7, 0), ("bz", 3), ("bz", 4), ("rx", 5, 0),
        ("gy", 3, 0), ("gx", 4, 0), ("bz", 0), ("gz", 3, 0), ("bx", 4, 0), ("bz", 1), ("by", 3, 0), ("ry", 5, 0), ("rz", 5, 0)],
    8: [("rw", 7, 0), ("bz", 0), ("by", 4), ("gw", 7, 0), ("gy", 5), ("gy", 4), ("bw", 7, 0), ("gz", 5), ("bz", 4), ("rx", 4, 0),
        ("gz", 4), ("gy", 3, 0), ("gx", 5, 0), ("gz", 3, 0), ("bx", 4, 0), ("bz", 1), ("by", 3, 0), ("ry", 4, 0), ("bz", 2),
        ("rz", 4, 0), ("bz", 3)],
    9: [("rw", 7, 0), ("bz", 1), ("by", 4), ("gw", 7, 0), ("by", 5), ("gy", 4), ("bw", 7, 0), ("bz", 5), ("bz", 4), ("rx", 4, 0),
        ("gz", 4), ("gy", 3, 0), ("gx", 4, 0), ("bz", 0), ("gz", 3, 0), ("bx", 5, 0), ("by", 3, 0), ("ry", 4, 0), ("bz", 2),
        ("rz", 4, 0), ("bz", 3)],
    10: [("rw", 5, 0), ("gz", 4), ("bz", 0), ("bz", 1), ("by", 4), ("gw", 5, 0), ("gy", 5), ("by", 5), ("bz", 2), ("gy", 4),
         ("bw", 5, 0), ("gz", 5), ("bz", 3), ("bz", 5), ("bz", 4), ("rx", 5, 0), ("gy", 3, 0), ("gx", 5, 0), ("gz", 3, 0),
         ("bx", 5, 0), ("by", 3, 0), ("ry", 5, 0), ("rz", 5, 0)],
    11: [("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 9, 0), ("gx", 9, 0), ("bx", 9, 0)],
    12: [("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 8, 0), ("rw", 10), ("gx", 8, 0), ("gw", 10), ("bx", 8, 0), ("bw", 10)],
    # Modes 13 and 14 store the high bits of the base in the reverse order (the highest bit first).
    13: [("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 7, 0), ("rw", 11), ("rw", 10), ("gx", 7, 0), ("gw", 11), ("gw", 10),
         ("bx", 7, 0), ("bw", 11), ("bw", 10)],
    14: [("rw", 9, 0), ("gw", 9, 0), ("bw", 9, 0), ("rx", 3, 0)] + [("rw", b) for b in range(15, 9, -1)] + [("gx", 3, 0)]
        + [("gw", b) for b in range(15, 9, -1)] + [("bx", 3, 0)] + [("bw", b) for b in range(15, 9, -1)],
}
# The mode of the 5-bit mode values (the 2-bit values 0 and 1 are the modes 1 and 2).
_MODE_OF = {2: 3, 6: 4, 10: 5, 14: 6, 18: 7, 22: 8, 26: 9, 30: 10, 3: 11, 7: 12, 11: 13, 15: 14}
# (transformed, regions, endpoint bits, delta bits r g b)
_MODES = {1: (True, 2, 10, (5, 5, 5)), 2: (True, 2, 7, (6, 6, 6)), 3: (True, 2, 11, (5, 4, 4)), 4: (True, 2, 11, (4, 5, 4)),
          5: (True, 2, 11, (4, 4, 5)), 6: (True, 2, 9, (5, 5, 5)), 7: (True, 2, 8, (6, 5, 5)), 8: (True, 2, 8, (5, 6, 5)),
          9: (True, 2, 8, (5, 5, 6)), 10: (False, 2, 6, (6, 6, 6)), 11: (False, 1, 10, (10, 10, 10)), 12: (True, 1, 11, (9, 9, 9)),
          13: (True, 1, 12, (8, 8, 8)), 14: (True, 1, 16, (4, 4, 4))}
_PARTITIONS = [
    "0011001100110011", "0001000100010001", "0111011101110111", "0001001100110111", "0000000100010011", "0011011101111111",
    "0001001101111111", "0000000100110111", "0000000000010011", "0011011111111111", "0000000101111111", "0000000000010111",
    "0001011111111111", "0000000011111111", "0000111111111111", "0000000000001111", "0000100011101111", "0111000100000000",
    "0000000010001110", "0111001100010000", "0011000100000000", "0000100011001110", "0000000010001100", "0111001100110001",
    "0011000100010000", "0000100010001100", "0110011001100110", "0011011001101100", "0001011111101000", "0000111111110000",
    "0111000110001110", "0011100110011100"]
_ANCHOR2 = [15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2]
_WEIGHTS3 = [0, 9, 18, 27, 37, 46, 55, 64]
_WEIGHTS4 = [0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64]


def _sign_extend(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def _unquantize(value: int, bits: int, signed: bool) -> int:
    if not signed:
        if bits >= 15 or value == 0:
            return value
        if value == (1 << bits) - 1:
            return 0xFFFF
        return ((value << 16) + 0x8000) >> bits
    if bits >= 16:
        return value
    negative = value < 0
    magnitude = -value if negative else value
    if magnitude == 0:
        out = 0
    elif magnitude >= (1 << (bits - 1)) - 1:
        out = 0x7FFF
    else:
        out = ((magnitude << 15) + 0x4000) >> (bits - 1)
    return -out if negative else out


def _finish(value: int, signed: bool) -> int:
    if not signed:
        return (value * 31) >> 6
    value = (-value * 31) >> 5 if value < 0 else (value * 31) >> 5
    return value  # the sign is kept by the caller


def decode_block(block: bytes, signed: bool = False) -> np.ndarray:
    """@return The 4 x 4 x 3 float texels of one 16-byte block."""
    bits = int.from_bytes(block, "little")
    pos = 0

    def read(count: int) -> int:
        nonlocal pos
        value = (bits >> pos) & ((1 << count) - 1)
        pos += count
        return value

    low = read(2)
    if low < 2:
        mode = 1 + low
    else:
        mode = _MODE_OF.get(low | (read(3) << 2))
        if mode is None:
            return np.zeros((4, 4, 3), np.float32)
    fields: dict[str, int] = {}
    for item in _LAYOUTS[mode]:
        name = item[0]
        if len(item) == 2:
            fields[name] = fields.get(name, 0) | (read(1) << item[1])
        else:
            hi, lo = item[1], item[2]
            fields[name] = fields.get(name, 0) | (read(hi - lo + 1) << lo)
    transformed, regions, prec, delta = _MODES[mode]
    partition = read(5) if regions == 2 else 0
    ends = []
    for letter in "wxyz"[:2 * regions]:
        ends.append([fields.get(c + letter, 0) for c in "rgb"])
    if signed:
        for c in range(3):
            ends[0][c] = _sign_extend(ends[0][c], prec)
    for e in range(1, len(ends)):
        for c in range(3):
            if transformed:
                d = _sign_extend(ends[e][c], delta[c])
                v = (ends[0][c] + d) & ((1 << prec) - 1)
                ends[e][c] = _sign_extend(v, prec) if signed else v
            elif signed:
                ends[e][c] = _sign_extend(ends[e][c], prec)
    ends = [[_unquantize(v, prec, signed) for v in e] for e in ends]
    # The indices: one bit less for each anchor texel.
    out = np.zeros((16, 3), np.float32)
    if regions == 1:
        subset = [0] * 16
        anchors = {0}
        index_bits = 4
        weights = _WEIGHTS4
    else:
        subset = [int(ch) for ch in _PARTITIONS[partition]]
        anchors = {0, _ANCHOR2[partition]}
        index_bits = 3
        weights = _WEIGHTS3
    halves = np.zeros((16, 3), np.uint16)
    for texel in range(16):
        index = read(index_bits - 1 if texel in anchors else index_bits)
        w = weights[index]
        e0, e1 = ends[2 * subset[texel]], ends[2 * subset[texel] + 1]
        for c in range(3):
            value = ((64 - w) * e0[c] + w * e1[c] + 32) >> 6
            if signed:
                negative = value < 0
                magnitude = _finish(-value if negative else value, True)
                halves[texel, c] = magnitude | (0x8000 if negative else 0)
            else:
                halves[texel, c] = _finish(value, False)
    out = halves.view(np.float16).astype(np.float32)
    return out.reshape(4, 4, 3)


def decode(data: bytes, width: int, height: int, signed: bool = False) -> np.ndarray:
    """@return The height x width x 3 float texels of a BC6H surface (blocks in rows)."""
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    out = np.zeros((bh * 4, bw * 4, 3), np.float32)
    for by in range(bh):
        for bx in range(bw):
            offset = (by * bw + bx) * 16
            out[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4] = decode_block(data[offset:offset + 16], signed)
    return out[:height, :width]
