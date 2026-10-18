#!/usr/bin/env python3
"""Scratch reader: the chunk files of the client (.cgf, .skin, .chr, .caf) with the .cgfheap data.

Proved on the game copy (see unreal-port.md): file := "CrCh" version:u32 (0x746) count:u32
tableOffset:u32; table entry := type:u16 version:u16 id:u32 size:u32 offset:u32.
"""
from __future__ import annotations

import struct
import uuid
from dataclasses import dataclass, field

import numpy as np

T_MESH = 0x1000
T_HELPER = 0x1001
T_NODE = 0x100B
T_MTLNAME = 0x1014
T_EXPORTFLAGS = 0x1015
T_DATASTREAM = 0x1016
T_SUBSETS = 0x1017
T_PHYSICS = 0x1018
T_LODINFO = 0x300A        # Azoth: LOD distances and sizes (inference from the values)
T_HEAPSTREAM = 0x300B     # Azoth: a data stream whose bytes are in the .cgfheap file

# Stream slots of the mesh chunk 0x802 (CryEngine order; the slot of each stream is proved by its content).
S_POSITIONS, S_NORMALS, S_TEXCOORDS, S_COLORS, S_COLORS2, S_INDICES, S_TANGENTS = 0, 1, 2, 3, 4, 5, 6
S_BONEMAPPING, S_FACEMAP, S_VERTMATS, S_QTANGENTS, S_SKINDATA, S_P3S_C4B_T2S = 9, 10, 11, 12, 13, 15


@dataclass
class Chunk:
    type: int
    version: int
    id: int
    size: int
    offset: int
    data: bytes


@dataclass
class ChunkFile:
    version: int
    chunks: list[Chunk]
    heap: bytes | None = None
    by_id: dict[int, Chunk] = field(default_factory=dict)

    def of_type(self, kind: int) -> list[Chunk]:
        return [c for c in self.chunks if c.type == kind]


def read_chunks(data: bytes, heap: bytes | None = None) -> ChunkFile:
    magic, version, count, table = struct.unpack_from("<4sIII", data, 0)
    if magic != b"CrCh":
        raise ValueError(f"not a chunk file: {magic!r}")
    chunks = []
    for i in range(count):
        kind, ver, cid, size, offset = struct.unpack_from("<HHIII", data, table + 16 * i)
        chunks.append(Chunk(kind, ver, cid, size, offset, data[offset:offset + size]))
    f = ChunkFile(version, chunks, heap)
    f.by_id = {c.id: c for c in chunks}
    return f


def stream_bytes(f: ChunkFile, cid: int) -> tuple[bytes, int]:
    """@return The bytes of one stream chunk and its element size."""
    c = f.by_id[cid]
    if c.type == T_HEAPSTREAM:
        _flags, _kind, offset, size, element = struct.unpack_from("<5I", c.data, 0)
        if f.heap is None:
            raise ValueError("the stream is in the .cgfheap file, which is not given")
        return f.heap[offset:offset + size], element
    if c.type == T_DATASTREAM:
        if c.version == 0x801:   # flags, type, index, count, elementSize, reserved[2], data
            _flags, _kind, _index, count, element = struct.unpack_from("<5I", c.data, 0)
            return c.data[28:28 + count * element], element
        # CryEngine 0x800: flags, type, count, elementSize, reserved[2], data
        _flags, _kind, count, element = struct.unpack_from("<4I", c.data, 0)
        return c.data[24:24 + count * element], element
    raise ValueError(f"chunk {cid:#x} of type {c.type:#x} is not a stream")


@dataclass
class Subset:
    first_index: int
    num_indices: int
    first_vertex: int
    num_vertices: int
    material: int


@dataclass
class Mesh:
    positions: np.ndarray            # (n, 3) float32, engine space (Z up)
    normals: np.ndarray | None
    tangents: np.ndarray | None      # (n, 4)
    uvs: np.ndarray | None
    colors: np.ndarray | None
    indices: np.ndarray
    subsets: list[Subset]
    joints: np.ndarray | None = None   # (n, 4) uint16 (indexes into the skeleton)
    weights: np.ndarray | None = None  # (n, 4) float32
    stream_ids: dict = field(default_factory=dict)
    bbox: tuple = ()


def half(a: np.ndarray) -> np.ndarray:
    return a.view(np.float16).astype(np.float32)


def read_mesh(f: ChunkFile, chunk: Chunk) -> Mesh:
    d = chunk.data
    flags, flags2, nverts, nindices, nsubsets, subsets_id, _vertanim = struct.unpack_from("<7i", d, 0)
    slots = struct.unpack_from("<128i", d, 28)
    streams = {t: slots[t * 8] for t in range(16) if slots[t * 8]}
    bbox = struct.unpack_from("<6f", d, 28 + 512 + 16)
    positions = normals = tangents = uvs = colors = None
    joints = weights = None
    if nverts == 0 or S_INDICES not in streams:
        raise ValueError("a mesh chunk with no geometry (a node of a group or a proxy)")
    # Indices
    raw, el = stream_bytes(f, streams[S_INDICES])
    indices = np.frombuffer(raw, dtype=np.uint16 if el == 2 else np.uint32)[:nindices].astype(np.uint32)
    # Vertices: either the interleaved P3S_C4B_T2S stream or separate streams.
    pid = streams.get(S_POSITIONS) or streams.get(S_P3S_C4B_T2S)
    raw, el = stream_bytes(f, pid)
    v = np.frombuffer(raw, dtype=np.uint8)[:nverts * el].reshape(nverts, el)
    if el == 16:      # P3S_C4B_T2S: half x, y, z, w; color BGRA; half u, v
        positions = half(v[:, 0:6].copy())
        colors = v[:, 8:12].copy()
        uvs = half(v[:, 12:16].copy())
    elif el == 8:     # half x, y, z, w only; UVs and colors in their own streams (checked with the bounding box)
        positions = half(v[:, 0:6].copy())
    elif el == 12:    # float positions
        positions = v.copy().view(np.float32).reshape(nverts, 3)
    elif el == 24:    # float x, y, z; color; float u, v (merged meshes of a slice; checked with the bounding box)
        positions = v[:, 0:12].copy().view(np.float32).reshape(nverts, 3)
        colors = v[:, 12:16].copy()
        uvs = v[:, 16:24].copy().view(np.float32).reshape(nverts, 2)
    else:
        raise ValueError(f"position element size {el} is not known")
    if uvs is None and S_TEXCOORDS in streams and streams[S_TEXCOORDS] != pid:
        raw, el = stream_bytes(f, streams[S_TEXCOORDS])
        if el == 8:
            uvs = np.frombuffer(raw, dtype=np.float32)[:nverts * 2].reshape(nverts, 2)
        else:
            uvs = half(np.frombuffer(raw, dtype=np.uint8)[:nverts * 4].reshape(nverts, 4).copy())
    if S_TANGENTS in streams:
        raw, el = stream_bytes(f, streams[S_TANGENTS])
        t = np.frombuffer(raw, dtype=np.int16)[:nverts * 8].reshape(nverts, 8).astype(np.float32) / 32767.0
        tan, bit = t[:, 0:4], t[:, 4:8]
        n = np.cross(tan[:, :3], bit[:, :3]) * np.sign(tan[:, 3:4] + 1e-9)
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        normals = n.astype(np.float32)
        tl = tan[:, :3] / (np.linalg.norm(tan[:, :3], axis=1, keepdims=True) + 1e-12)
        tangents = np.concatenate([tl, np.sign(tan[:, 3:4] + 1e-9)], axis=1).astype(np.float32)
    elif S_QTANGENTS in streams:
        raw, el = stream_bytes(f, streams[S_QTANGENTS])
        q = np.frombuffer(raw, dtype=np.int16)[:nverts * 4].reshape(nverts, 4).astype(np.float32) / 32767.0
        x, y, z, w = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
        tan = np.stack([1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)], 1)
        bit = np.stack([2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)], 1)
        n = np.cross(tan, bit) * np.sign(w)[:, None]
        normals = (n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-12)).astype(np.float32)
    if S_SKINDATA in streams or S_BONEMAPPING in streams:
        raw, el = stream_bytes(f, streams.get(S_BONEMAPPING) or streams.get(S_SKINDATA))
        b = np.frombuffer(raw, dtype=np.uint8)[:nverts * el].reshape(nverts, el)
        if el == 12:  # 4 x u16 bone ids, 4 x u8 weights (CryEngine SVF_W4B_I4S)
            joints = b[:, 0:8].copy().view(np.uint16).reshape(nverts, 4)
            weights = b[:, 8:12].astype(np.float32) / 255.0
        elif el == 8:  # 4 x u8 bone ids, 4 x u8 weights
            joints = b[:, 0:4].astype(np.uint16)
            weights = b[:, 4:8].astype(np.float32) / 255.0
        elif el == 24:  # 8 influences: 8 x u16 ids, 8 x u8 weights (inference); keep the 4 largest
            ids = b[:, 0:16].copy().view(np.uint16).reshape(nverts, 8)
            ws = b[:, 16:24].astype(np.float32) / 255.0
            order = np.argsort(-ws, axis=1)[:, :4]
            joints = np.take_along_axis(ids, order, 1)
            weights = np.take_along_axis(ws, order, 1)
        else:
            raise ValueError(f"bone mapping element size {el} is not known")
        s = weights.sum(1, keepdims=True)
        weights = np.where(s > 0, weights / np.maximum(s, 1e-6), weights)
    # Some source vertices have NaN half floats (for example 116 UV values of one building): make them 0.
    positions = np.nan_to_num(positions, nan=0.0, posinf=0.0, neginf=0.0)
    if uvs is not None:
        uvs = np.nan_to_num(uvs, nan=0.0, posinf=0.0, neginf=0.0)
    if normals is not None:
        normals = np.nan_to_num(normals, nan=0.0)
    sub = f.by_id[subsets_id].data
    _sflags, count = struct.unpack_from("<2i", sub, 0)
    subsets = []
    for i in range(count):
        fi, ni, fv, nv, mat = struct.unpack_from("<5i", sub, 16 + i * 36)
        subsets.append(Subset(fi, ni, fv, nv, mat))
    return Mesh(positions, normals, tangents, uvs, colors, indices, subsets, joints, weights, streams, bbox)


def node_matrices(f: ChunkFile) -> dict[int, np.ndarray]:
    """@return For each object chunk id of a node: the 4 x 4 matrix (column vectors, meters) from the space of the
    object to the space of the file (the product of the parent chain).

    Node chunk 0x824 (CryEngine NODE_CHUNK_DESC_0824): name[64], object id, parent id, child count, material id,
    4 bytes, tm float[4][4] at offset 84 (rows = the images of the X, Y, Z axes, then the translation in
    centimeters), then obsolete pos, rot, scale, controller ids, and a property text. Proved on the game copy: the
    physics proxy of a ceiling beam is at -443 cm on the 8.9 m beam (the middle), and the ornament of a claim totem
    is at 465 cm, on the top of its pole.
    """
    local: dict[int, tuple[int, int, np.ndarray]] = {}
    for c in f.of_type(T_NODE):
        if c.version not in (0x823, 0x824) or len(c.data) < 148:
            continue
        obj, parent = struct.unpack_from("<2i", c.data, 64)
        tm = np.array(struct.unpack_from("<16f", c.data, 84), dtype=np.float64).reshape(4, 4)
        m = np.eye(4)
        m[:3, :3] = tm[:3, :3].T
        m[:3, 3] = tm[3, :3] * 0.01
        local[c.id] = (obj, parent, m)
    by_object: dict[int, np.ndarray] = {}
    for obj, parent, m in local.values():
        world, depth = m, 0
        while parent in local and depth < 64:
            _obj, parent, pm = local[parent]
            world = pm @ world
            depth += 1
        by_object[obj] = world
    return by_object


def transform_mesh(mesh: Mesh, m: np.ndarray) -> Mesh:
    """Moves a mesh by a 4 x 4 matrix (column vectors): positions, normals, tangents, and the triangle order of a
    mirror."""
    linear = m[:3, :3]
    mesh.positions = (mesh.positions.astype(np.float64) @ linear.T + m[:3, 3]).astype(np.float32)
    if mesh.normals is not None:
        n = mesh.normals.astype(np.float64) @ np.linalg.inv(linear)
        mesh.normals = (n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-12)).astype(np.float32)
    mirror = np.linalg.det(linear) < 0
    if mesh.tangents is not None:
        t = mesh.tangents[:, :3].astype(np.float64) @ linear.T
        t /= np.linalg.norm(t, axis=1, keepdims=True) + 1e-12
        w = mesh.tangents[:, 3:4] * (-1.0 if mirror else 1.0)
        mesh.tangents = np.concatenate([t, w], axis=1).astype(np.float32)
    if mirror:
        mesh.indices = mesh.indices.reshape(-1, 3)[:, ::-1].reshape(-1).copy()
    return mesh


def material_guid(f: ChunkFile) -> str | None:
    for c in f.of_type(T_MTLNAME):
        text = c.data[:128].split(b"\0")[0].decode("latin-1")
        if text.startswith("{"):
            return text
        return text
    return None


def guid_bytes(text: str) -> bytes:
    return uuid.UUID(text.strip("{}").split("}")[0]).bytes


@dataclass
class Bone:
    index: int
    name: str
    controller_id: int
    parent: int          # index of the parent bone, -1 for the root
    b2w: np.ndarray      # (4, 4) bone to world (bind pose), engine space
    w2b: np.ndarray


T_COMPILED_BONES = 0x2000
BONE_SIZE = 584


def m34(values) -> np.ndarray:
    m = np.eye(4)
    m[:3, :4] = np.array(values, dtype=np.float64).reshape(3, 4)
    return m


def read_bones(f: ChunkFile) -> list[Bone]:
    c = f.of_type(T_COMPILED_BONES)[0]
    d = c.data
    header = (len(d)) % BONE_SIZE
    count = (len(d) - header) // BONE_SIZE
    bones = []
    for i in range(count):
        o = header + i * BONE_SIZE
        cid = struct.unpack_from("<I", d, o)[0]
        # controllerID, physInfo[2] (2 x 104), mass, DefaultW2B (3x4), DefaultB2W (3x4), name[256], limb, offsetParent, numChildren, offsetChild
        w2b = m34(struct.unpack_from("<12f", d, o + 4 + 208 + 4))
        b2w = m34(struct.unpack_from("<12f", d, o + 4 + 208 + 4 + 48))
        name = d[o + 312:o + 312 + 256].split(b"\0")[0].decode("latin-1")
        _limb, off_parent, nchild, off_child = struct.unpack_from("<4i", d, o + 568)
        parent = i + off_parent if off_parent != 0 else -1
        bones.append(Bone(i, name, cid, parent, b2w, w2b))
    return bones
