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


# The uncompressed formats: 4 bytes for each pixel (28, 29: R8G8B8A8 UNORM and SRGB; 87, 91: B8G8R8A8 UNORM and SRGB).
RGBA8 = (28, 29)
BGRA8 = (87, 91)


def mip_size(fmt: int, w: int, h: int) -> int:
    if fmt in RGBA8 or fmt in BGRA8:
        return max(1, w) * max(1, h) * 4
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


def bc4_blocks(blocks: np.ndarray, signed: bool) -> np.ndarray:
    """Decodes many BC4 blocks at once: two end values, then 6 or 4 values between them (and 0 and 1 in the
    4-value mode), and 16 indices of 3 bits. @return (n, 16) float32 values."""
    a = blocks[:, :2].copy().view(np.int8 if signed else np.uint8).astype(np.float32)
    a0, a1 = a[:, 0:1], a[:, 1:2]
    six = a0 > a1
    i = np.arange(1, 7, dtype=np.float32)[None, :]
    k = np.arange(1, 5, dtype=np.float32)[None, :]
    low, high = (-127.0 if signed else 0.0), (127.0 if signed else 255.0)
    pal6 = np.concatenate([a0, a1, ((6 - i) * a0 + i * a1) / 7], axis=1)
    pal4 = np.concatenate([a0, a1, ((4 - k) * a0 + k * a1) / 5, np.full_like(a0, low), np.full_like(a0, high)], axis=1)
    pal = np.where(six, pal6, pal4)
    bits = np.zeros(len(blocks), dtype=np.uint64)
    for b in range(6):
        bits |= blocks[:, 2 + b].astype(np.uint64) << np.uint64(8 * b)
    idx = ((bits[:, None] >> (np.arange(16, dtype=np.uint64) * np.uint64(3))[None, :]) & np.uint64(7)).astype(np.int64)
    return np.take_along_axis(pal, idx, axis=1)


def decode_bc45(data: bytes, w: int, h: int, fmt: int) -> np.ndarray:
    """Numpy decoder for BC4/BC5 (unsigned and signed). @return float array in 0..1."""
    signed = fmt in (81, 84)
    channels = 2 if fmt in (83, 84) else 1
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    raw = np.frombuffer(data, dtype=np.uint8, count=bw * bh * 8 * channels).reshape(bh * bw, channels, 8)
    out = np.zeros((bh * 4, bw * 4, channels), dtype=np.float32)
    for c in range(channels):
        values = bc4_blocks(raw[:, c, :], signed).reshape(bh, bw, 4, 4)
        out[:, :, c] = values.transpose(0, 2, 1, 3).reshape(bh * 4, bw * 4)
    if signed:
        out = (out / 127.0 + 1) / 2
    else:
        out = out / 255.0
    return out[:h, :w]


def to_image(dds: bytes, w: int, h: int, fmt: int) -> Image.Image:
    if fmt in RGBA8 or fmt in BGRA8:
        data = dds[148:148 + mip_size(fmt, w, h)]
        img = Image.frombuffer("RGBA", (w, h), data, "raw", "RGBA" if fmt in RGBA8 else "BGRA", 0, 1)
        return img.copy()
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
