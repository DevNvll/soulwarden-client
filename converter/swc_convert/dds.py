#!/usr/bin/env python3
"""Scratch reader: join the split DDS textures of the client and decode them to PNG.

Proved on the game copy: NAME.dds = DDS header (148 bytes with DX10) + the smallest mips;
NAME.dds.N = one mip, N = 1 is the smallest split mip, the highest N is mip 0.
NAME.dds.a and NAME.dds.Na = a second texture (the alpha part, for example the gloss of a _ddna).
"""
from __future__ import annotations

import io
import struct
import sys

import numpy as np
from PIL import Image

sys.dont_write_bytecode = True

BLOCK_BYTES = {71: 8, 72: 8, 74: 16, 75: 16, 77: 16, 78: 16, 80: 8, 81: 8, 83: 16, 84: 16, 95: 16, 96: 16, 98: 16, 99: 16}


def mip_size(fmt: int, w: int, h: int) -> int:
    b = BLOCK_BYTES.get(fmt)
    if b is None:
        raise ValueError(f"DXGI format {fmt} not handled")
    return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * b


def join(read, base: str, max_size: int = 1024, alpha: bool = False) -> tuple[bytes, int, int, int]:
    """Join the split DDS of `base` (a pak path). `read(path)` gives the bytes of a pak entry.

    @return (dds bytes starting at the mip whose width <= max_size, width, height, dxgi format)
    """
    head_path = base + (".a" if alpha else "")
    head = read(head_path)
    if head[:4] != b"DDS ":
        raise ValueError("not a DDS file")
    height, width = struct.unpack_from("<II", head, 12)
    mips = struct.unpack_from("<I", head, 28)[0] or 1
    fourcc = head[84:88]
    if fourcc != b"DX10":
        raise ValueError(f"fourcc {fourcc!r} not handled in this scratch reader")
    fmt = struct.unpack_from("<I", head, 128)[0]
    header_size = 148
    tail = head[header_size:]
    # Mip sizes of the full chain
    sizes = []
    w, h = width, height
    for _ in range(mips):
        sizes.append((w, h, mip_size(fmt, w, h)))
        w, h = max(1, w // 2), max(1, h // 2)
    # Number of mips in the tail of the base file
    tail_mips = 0
    total = 0
    for w, h, s in reversed(sizes):
        if total + s > len(tail):
            break
        total += s
        tail_mips += 1
    split = mips - tail_mips          # mips 0 .. split-1 are in .dds.N files: mip i in .dds.(split - i)
    parts = []
    start = None
    for i in range(split):
        w, h, s = sizes[i]
        if w > max_size and i < mips - 1:
            continue
        if start is None:
            start = i
        suffix = f".{split - i}" + ("a" if alpha else "")
        data = read(base + suffix)
        if len(data) != s:
            raise ValueError(f"{base}{suffix}: {len(data)} bytes, expected {s}")
        parts.append(data)
    if start is None:
        start = split
    parts.append(tail[:total])
    w0, h0, _ = sizes[start]
    new = bytearray(head[:header_size])
    struct.pack_into("<II", new, 12, h0, w0)
    struct.pack_into("<I", new, 28, mips - start)
    return bytes(new) + b"".join(parts), w0, h0, fmt


def bc4_block(block: bytes, signed: bool) -> np.ndarray:
    a = np.frombuffer(block[:2], dtype=np.int8 if signed else np.uint8).astype(np.float32)
    a0, a1 = a[0], a[1]
    if a0 > a1:
        pal = [a0, a1] + [((6 - i) * a0 + i * a1) / 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((4 - i) * a0 + i * a1) / 5 for i in range(1, 5)] + [(-127 if signed else 0), (127 if signed else 255)]
    bits = int.from_bytes(block[2:8], "little")
    idx = [(bits >> (3 * i)) & 7 for i in range(16)]
    return np.array([pal[i] for i in idx], dtype=np.float32).reshape(4, 4)


def decode_bc45(data: bytes, w: int, h: int, fmt: int) -> np.ndarray:
    """Small numpy decoder for BC4/BC5 (unsigned and signed). @return float array in 0..1."""
    signed = fmt in (81, 84)
    channels = 2 if fmt in (83, 84) else 1
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    out = np.zeros((bh * 4, bw * 4, channels), dtype=np.float32)
    step = 8 * channels
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * step
            for c in range(channels):
                out[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4, c] = bc4_block(data[o + 8 * c:o + 8 * c + 8], signed)
    if signed:
        out = (out / 127.0 + 1) / 2
    else:
        out = out / 255.0
    return out[:h, :w]


def to_image(dds: bytes, w: int, h: int, fmt: int) -> Image.Image:
    if fmt in (80, 81, 83, 84):
        data = dds[148:148 + mip_size(fmt, w, h)]
        arr = decode_bc45(data, w, h, fmt)
        if arr.shape[2] == 1:
            return Image.fromarray((arr[:, :, 0] * 255).clip(0, 255).astype(np.uint8), "L")
        x, y = arr[:, :, 0] * 2 - 1, arr[:, :, 1] * 2 - 1
        z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
        rgb = np.stack([(x + 1) / 2, (y + 1) / 2, (z + 1) / 2], 2)
        return Image.fromarray((rgb * 255).clip(0, 255).astype(np.uint8), "RGB")
    # Pillow decodes the UNORM formats; an _SRGB format has the same block bits.
    srgb_to_unorm = {72: 71, 75: 74, 78: 77, 99: 98}
    if fmt in srgb_to_unorm:
        dds = bytearray(dds)
        struct.pack_into("<I", dds, 128, srgb_to_unorm[fmt])
        dds = bytes(dds)
    img = Image.open(io.BytesIO(dds))
    img.load()
    return img.convert("RGBA")
