#!/usr/bin/env python3
"""Scratch reader: one animation clip (.caf) of the client.

Proved on the game copy (wolf canter): chunk 0x3002 v0x925 = motion parameters (flags, compression,
ticksPerFrame, secsPerTick, start, end, ...); chunk 0x100D v0x831 = one controller (one bone):
controllerId (CRC-32 of the bone name), flags, numRotKeys u16, numPosKeys u16, rotFormat u8,
rotTimeFormat u8, posFormat u8, posKeysInfo u8, posTimeFormat u8, tracksAligned u8, 2 bytes;
then the tracks (rotation keys, rotation times, position keys, position times), each track
starting at a 4-byte boundary when tracksAligned is 1.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass

import numpy as np

T_CONTROLLER = 0x100D
T_MOTION = 0x3002

ROT_SIZE = {1: 16, 3: 6, 4: 4, 5: 6, 6: 8, 8: 8}
POS_SIZE = {0: 12, 2: 12}
TIME_SIZE = {0: 4, 1: 2, 2: 1}
RANGE = math.sqrt(2.0)
HALF = RANGE / 2


def _restore(index: int, c: list[float]) -> np.ndarray:
    s = max(0.0, 1.0 - sum(v * v for v in c))
    q = [0.0, 0.0, 0.0, 0.0]
    j = 0
    for i in range(4):
        if i == index:
            q[i] = math.sqrt(s)
        else:
            q[i] = c[j]; j += 1
    return np.array(q)  # x, y, z, w


def quat48(b: bytes) -> np.ndarray:
    v = int.from_bytes(b, "little")
    index = v >> 46
    c = [((v >> (15 * k)) & 0x7FFF) / 32767.0 * RANGE - HALF for k in range(3)]
    return _restore(index, c)


def quat64(b: bytes) -> np.ndarray:
    v = int.from_bytes(b, "little")
    index = v >> 62
    c = [((v >> (20 * k)) & 0xFFFFF) / 1048575.0 * RANGE - HALF for k in range(3)]
    return _restore(index, c)


def quat64ext(b: bytes) -> np.ndarray:
    v = int.from_bytes(b, "little")
    index = v >> 62
    c0 = (v & 0x1FFFFF) / 2097151.0 * RANGE - HALF
    c1 = ((v >> 21) & 0x1FFFFF) / 2097151.0 * RANGE - HALF
    c2 = ((v >> 42) & 0xFFFFF) / 1048575.0 * RANGE - HALF
    return _restore(index, [c0, c1, c2])


def quat_none(b: bytes) -> np.ndarray:
    return np.array(struct.unpack("<4f", b), dtype=np.float64)


ROT_DECODE = {1: quat_none, 5: quat48, 6: quat64, 8: quat64ext}


@dataclass
class Controller:
    controller_id: int
    rot_times: np.ndarray
    rotations: np.ndarray    # (n, 4) x y z w, parent space
    pos_times: np.ndarray
    positions: np.ndarray    # (n, 3)


@dataclass
class Clip:
    start: int
    end: int
    secs_per_frame: float
    controllers: dict[int, Controller]
    move_speed: float


def _align(o: int, aligned: bool) -> int:
    return (o + 3) & ~3 if aligned else o


def _times(d: bytes, o: int, fmt: int, n: int):
    if fmt == 0:
        return np.frombuffer(d, dtype=np.float32, count=n, offset=o).astype(np.float64), o + 4 * n
    if fmt == 1:
        return np.frombuffer(d, dtype=np.uint16, count=n, offset=o).astype(np.float64), o + 2 * n
    if fmt == 2:
        return np.frombuffer(d, dtype=np.uint8, count=n, offset=o).astype(np.float64), o + n
    raise ValueError(f"key time format {fmt} not handled in this scratch reader")


def read_clip(f) -> Clip:
    motion = [c for c in f.chunks if c.type == T_MOTION][0].data
    _flags, _compression, ticks, secs, start, end = struct.unpack_from("<IIifii", motion, 0)
    move_speed = struct.unpack_from("<f", motion, 24)[0]
    controllers = {}
    for c in f.chunks:
        if c.type != T_CONTROLLER:
            continue
        if c.version != 0x831:
            raise ValueError(f"controller version {c.version:#x} not handled")
        d = c.data
        cid, _cflags, nrot, npos = struct.unpack_from("<IIHH", d, 0)
        rfmt, rtfmt, pfmt, pinfo, ptfmt, aligned = struct.unpack_from("<6B", d, 12)
        o = 20
        rots = np.zeros((nrot, 4)); rt = np.zeros(0)
        if nrot:
            size = ROT_SIZE[rfmt]
            dec = ROT_DECODE[rfmt]
            rots = np.array([dec(d[o + i * size:o + (i + 1) * size]) for i in range(nrot)])
            o += nrot * size
            o = _align(o, aligned)
            rt, o = _times(d, o, rtfmt, nrot)
            o = _align(o, aligned)
        pos = np.zeros((npos, 3)); pt = np.zeros(0)
        if npos:
            pos = np.frombuffer(d, dtype=np.float32, count=npos * 3, offset=o).reshape(npos, 3).astype(np.float64)
            o += npos * 12
            o = _align(o, aligned)
            if pinfo == 1:
                pt, o = _times(d, o, ptfmt, npos)
            else:
                pt = rt if len(rt) == npos else np.linspace(start, end, npos)
        if o > len(d) + 3:
            raise ValueError(f"controller {cid:#x}: read past the chunk ({o} > {len(d)})")
        controllers[cid] = Controller(cid, rt, rots, pt, pos)
    return Clip(start, end, ticks * secs, controllers, move_speed)


def quat_to_matrix(q: np.ndarray) -> np.ndarray:
    x, y, z, w = q / (np.linalg.norm(q) + 1e-12)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def slerp(a, b, t):
    if np.dot(a, b) < 0:
        b = -b
    d = np.dot(a, b)
    if d > 0.9995:
        r = a + t * (b - a)
        return r / np.linalg.norm(r)
    th = math.acos(min(1.0, d))
    return (math.sin((1 - t) * th) * a + math.sin(t * th) * b) / math.sin(th)


def sample(times, values, frame, is_quat):
    if len(times) == 0:
        return None
    if frame <= times[0]:
        return values[0]
    if frame >= times[-1]:
        return values[-1]
    i = int(np.searchsorted(times, frame)) - 1
    t = (frame - times[i]) / max(times[i + 1] - times[i], 1e-9)
    if is_quat:
        return slerp(values[i], values[i + 1], t)
    return values[i] * (1 - t) + values[i + 1] * t


def pose(clip: Clip, bones, frame: float) -> list[np.ndarray]:
    """@return The bone-to-world matrix of each bone at `frame` (bones from cgf.read_bones of the skeleton)."""
    world = []
    for b in bones:
        parent_w = world[b.parent] if b.parent >= 0 else np.eye(4)
        bind_local = (np.linalg.inv(bones[b.parent].b2w) @ b.b2w) if b.parent >= 0 else b.b2w.copy()
        local = bind_local.copy()
        c = clip.controllers.get(b.controller_id)
        if c is not None:
            q = sample(c.rot_times, c.rotations, frame, True)
            if q is not None:
                local[:3, :3] = quat_to_matrix(q)
            p = sample(c.pos_times, c.positions, frame, False)
            if p is not None:
                local[:3, 3] = p
        world.append(parent_w @ local)
    return world
