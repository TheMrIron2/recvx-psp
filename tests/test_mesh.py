"""Known MLB fixtures test transforms, strip winding, bounds and ownership."""
import ctypes
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scene_api import Mesh, bind_scene

lib = bind_scene(ctypes.CDLL(str(Path(sys.argv[1]).resolve())))
mesh = Mesh()
BODY = 0x510


def fixture(points=((0, 0, 0), (1, 0, 0), (0, 1, 0)), objects=1,
            strip_type=66, negative=False, attributes=0, first=0):
    model_offset = (max(128, objects * 52) + 15) & ~15
    vertex_offset = model_offset + 32
    polygon_offset = vertex_offset + 64 + len(points) * 32
    polygon = bytearray(struct.pack("<HHI", 17, 2, 0xFF123456))
    polygon.extend(struct.pack("<HH", 8, 0))
    payload = bytearray(struct.pack("<Hh", 1 | (attributes << 14),
                                    -len(points) if negative else len(points)))
    for i in range(len(points)):
        payload.extend(struct.pack("<H", first + i))
        if strip_type != 64:
            payload.extend(struct.pack("<hh", 0, 1023))
        if i >= 2:
            payload.extend(b"\0\0" * attributes)
    polygon.extend(struct.pack("<HH", strip_type, len(payload) // 2))
    polygon.extend(payload)
    polygon.extend(b"\xff\0")
    data = bytearray(BODY + polygon_offset + len(polygon))
    struct.pack_into("<f", data, 0, 10)
    struct.pack_into("<II", data, 16, 128, 0x480)
    struct.pack_into("<I", data, 0x480, 0x500)
    struct.pack_into("<4sHHII", data, 0x500, b"MDL\xbc", 16, objects, 0xFFFFFFFF, 0)
    for index in range(objects):
        struct.pack_into("<II3f3i3fII", data, BODY + index * 52,
                         7, model_offset, 0, 0, 0, 0, 0, 0, 1, 1, 1,
                         (index + 1) * 52 if index + 1 < objects else 0xFFFFFFFF,
                         0xFFFFFFFF)
    struct.pack_into("<II4f", data, BODY + model_offset, vertex_offset, polygon_offset, 0, 0, 0, -2)
    struct.pack_into("<HHHH", data, BODY + vertex_offset, 51, len(points) * 8 + 15, first, len(points))
    for index, point in enumerate(points):
        struct.pack_into("<8f", data, BODY + vertex_offset + 64 + index * 32,
                         *point, 1, 0, 0, 1, 0)
    data[BODY + polygon_offset:] = polygon
    return data, (BODY + model_offset, BODY + vertex_offset, BODY + polygon_offset)


def build(data):
    return lib.recvxBuildRoomMesh(ctypes.create_string_buffer(bytes(data)), len(data), ctypes.byref(mesh))


def positions():
    return [(v.x, v.y, v.z) for v in mesh.vertices[:mesh.vertex_count]]


good, (model, vertex, polygon) = fixture()
assert build(good) == 0
assert mesh.object_count == mesh.model_count == mesh.triangle_count == 1 and mesh.vertex_count == 3
assert positions() == [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
assert ctypes.cast(mesh.vertices, ctypes.c_void_p).value % 16 == 0
assert mesh.vertices[0].color == 0xFF39220C
assert [(v.x, v.y, v.z) for v in mesh.wire_vertices[:6]] == [
    (0,0,0), (1,0,0), (1,0,0), (0,1,0), (0,1,0), (0,0,0)]
old_storage = mesh.storage
for size in range(len(good)):
    assert build(good[:size]) == -1 and mesh.storage == old_storage
for offset, fmt, value, expected in (
        (0x480, "I", 0xFFFFFFFF, -1), (0x504, "H", 0xFFFF, -1),
        (0x506, "H", 513, -2), (0x50C, "I", 0xFFFFFF00, -1),
        (BODY + 44, "I", 0, -1), (BODY + 48, "I", 1, -1),
        (model, "I", 0xFFFFFF00, -1), (vertex, "B", 41, -4),
        (vertex + 2, "H", 0, -1), (vertex + 6, "H", 0xFFFF, -1),
        (vertex + 64, "f", float("nan"), -1),
        (polygon, "B", 4, -4), (polygon + 2, "H", 0xFFFF, -1),
        (polygon + 12, "B", 67, -4),
        (polygon + 18, "h", 2, -1), (polygon + 20, "H", 100, -1)):
    bad = bytearray(good)
    struct.pack_into("<" + fmt, bad, offset, value)
    assert build(bad) == expected and mesh.storage == old_storage, (offset, value)

for strip_type in (64, 65, 66):
    for attributes in range(4):
        for negative in (False, True):
            data, _ = fixture(((0,0,0), (1,0,0), (0,1,0), (1,1,0)),
                              strip_type=strip_type, attributes=attributes,
                              negative=negative, first=7)
            assert build(data) == 0 and mesh.triangle_count == 2
            expected = [(0,0,0), (1,0,0), (0,1,0), (0,1,0), (1,0,0), (1,1,0)]
            if negative:
                expected = [expected[1], expected[0], expected[2],
                            expected[4], expected[3], expected[5]]
            assert positions() == expected

data, _ = fixture()
struct.pack_into("<I", data, BODY, 2)  # Ignore angles, apply position and scale
struct.pack_into("<3f", data, BODY + 8, 10, 20, 30)
struct.pack_into("<3f", data, BODY + 32, 2, 3, 4)
assert build(data) == 0 and positions() == [(10,20,30), (12,20,30), (10,23,30)]
data, _ = fixture()
struct.pack_into("<I", data, BODY, 5)  # Apply only rotation
struct.pack_into("<i", data, BODY + 28, 16384)
assert build(data) == 0
for actual, expected in zip(positions(), [(0,0,0), (0,1,0), (-1,0,0)]):
    assert all(abs(a-b) < 1e-6 for a, b in zip(actual, expected))
for flags, expected in (
        (5, [(0,0,0), (0,0,-1), (0,1,0)]),
        (0x25, [(0,0,0), (1,0,0), (0,0,1)])):
    data, _ = fixture()
    struct.pack_into("<I", data, BODY, flags)
    struct.pack_into("<3i", data, BODY + 20, 16384, 16384, 16384)
    assert build(data) == 0
    for actual, wanted in zip(positions(), expected):
        assert all(abs(a-b) < 1e-6 for a, b in zip(actual, wanted))
# Parent transforms affect children; siblings use the original parent matrix.
data, _ = fixture(objects=2)
struct.pack_into("<I3f", data, BODY + 4, 0xFFFFFFFF, 10, 20, 30)
struct.pack_into("<I", data, BODY, 6)
struct.pack_into("<I", data, BODY + 52, 6)
struct.pack_into("<3f", data, BODY + 52 + 8, 1, 2, 3)
assert build(data) == 0 and positions() == [(11,22,33), (12,22,33), (11,23,33)]
struct.pack_into("<II", data, BODY + 44, 0xFFFFFFFF, 52)
assert build(data) == 0 and positions() == [(1,2,3), (2,2,3), (1,3,3)]
# Hide keeps traversing children; break suppresses them.
data, _ = fixture(objects=2)
struct.pack_into("<I", data, BODY, 15)
assert build(data) == 0 and mesh.model_count == 1
struct.pack_into("<I", data, BODY, 31)
assert build(data) == 0 and not mesh.vertices and mesh.triangle_count == 0
data, _ = fixture(objects=65)
assert build(data) == -2
data, _ = fixture()
struct.pack_into("<I", data, 20, 0)
assert build(data) == 0 and not mesh.vertices
assert lib.recvxBuildRoomMesh(None, 0, ctypes.byref(mesh)) == -1
lib.recvxMeshFree(ctypes.byref(mesh))
lib.recvxMeshFree(ctypes.byref(mesh))
print("PASS: MLB bounds, transforms, hierarchy, strip winding, attributes and ownership")
