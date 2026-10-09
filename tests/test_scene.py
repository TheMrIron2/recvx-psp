"""Validate room geometry, finite coordinates and scene buffer ownership."""
import ctypes
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scene_api import Scene, bind_scene

lib = bind_scene(ctypes.CDLL(str(Path(sys.argv[1]).resolve())))
scene = Scene()


def room(count=1):
    data = bytearray(128 + 0x3EC + count * 0x24)
    struct.pack_into("<f", data, 0, 10.0)
    struct.pack_into("<I", data, 16, 128)
    struct.pack_into("<I", data, 128 + 6 * 4, 128 + 0x3EC)
    struct.pack_into("<I", data, 128 + 0x80 + 6 * 4, count)
    for index in range(count):
        offset = 128 + 0x3EC + index * 0x24
        data[offset] = 1
        struct.pack_into("<6f", data, offset + 8, 1, 2, 3, 4, 5, 6)
    return data


def build(data):
    source = ctypes.create_string_buffer(bytes(data))
    return lib.recvxBuildRoomScene(source, len(data), ctypes.byref(scene))


good = room()
assert build(good) == 0
assert scene.box_count == 1 and scene.marker_count == 0 and scene.vertex_count == 114
assert ctypes.cast(scene.vertices, ctypes.c_void_p).value % 16 == 0
assert list(scene.center) == [3, 4.5, 6]
assert math.isclose(scene.radius, math.sqrt(77) / 2, rel_tol=1e-6)
points = {(scene.vertices[i].x, scene.vertices[i].y, scene.vertices[i].z) for i in range(24)}
assert points == {(x, y, z) for x in (1, 5) for y in (2, 7) for z in (3, 9)}
for i in range(0, 24, 2):
    a, b = scene.vertices[i], scene.vertices[i + 1]
    assert sum(x != y for x, y in zip((a.x, a.y, a.z), (b.x, b.y, b.z))) == 1
old_storage = scene.storage
for size in range(len(good)):
    assert build(good[:size]) == -1 and scene.storage == old_storage
for value in (float("nan"), float("inf"), 1e30):
    bad = room()
    struct.pack_into("<f", bad, 128 + 0x3EC + 8, value)
    assert build(bad) == -1 and scene.storage == old_storage
assert build(room(2000)) == -2 and scene.storage == old_storage
assert lib.recvxBuildRoomScene(None, 0, ctypes.byref(scene)) == -1
assert lib.recvxBuildRoomScene(ctypes.create_string_buffer(bytes(good)), len(good), None) == -1
negative = room()
struct.pack_into("<6f", negative, 128 + 0x3EC + 8, 1, 2, 3, -4, -5, -6)
assert build(negative) == 0 and list(scene.center) == [-1, -0.5, 0]
flat = room()
struct.pack_into("<f", flat, 128 + 0x3EC + 24, 0)
assert build(flat) == 0 and all(scene.vertices[i].y == 2 for i in range(24))
inactive = room()
inactive[128 + 0x3EC] = 0
assert build(inactive) == 0 and scene.box_count == 0 and scene.vertex_count == 90
assert scene.radius == 10 and list(scene.center) == [0, 0, 0]
lib.recvxSceneFree(ctypes.byref(scene))
assert scene.storage is None and not scene.vertices and scene.vertex_count == 0
lib.recvxSceneFree(ctypes.byref(scene))
print("PASS: scene bounds, alignment, ownership, invalid floats and geometry limits")
