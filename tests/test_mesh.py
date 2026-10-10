"""Known MLB fixtures test transforms, strip winding, bounds and ownership."""
import ctypes
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scene_api import Mesh, RoomCut, Texture, TextureSet, bind_scene

lib = bind_scene(ctypes.CDLL(str(Path(sys.argv[1]).resolve())))
mesh = Mesh()
BODY = 0x510

empty_stream = ctypes.create_string_buffer(b"\xff\xff\xff\xff")
speed_bytes = struct.pack("<IffI", 8, 0.0, 0.5, 0xFFFFFFFF)
speed_stream = ctypes.create_string_buffer(speed_bytes)
player_assets_buffer = ctypes.create_string_buffer(65536)
player_speed = ctypes.c_float()
lib.recvxPlayerAssetsParse.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_void_p]
lib.recvxPlayerSpeedAt.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                   ctypes.c_uint32, ctypes.POINTER(ctypes.c_float)]
lib.recvxBlendPlayerMeshPose.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                         ctypes.c_uint32, ctypes.c_void_p,
                                         ctypes.c_uint32, ctypes.c_float,
                                         ctypes.c_void_p]
lib.recvxRoomCutForPosition.argtypes = [ctypes.POINTER(Mesh), ctypes.c_float,
                                        ctypes.c_float, ctypes.c_int,
                                        ctypes.c_int]
lib.recvxRoomCutForPosition.restype = ctypes.c_int
assert lib.recvxPlayerAssetsParse(empty_stream, 4, empty_stream, 4,
                                 speed_stream, len(speed_bytes),
                                 player_assets_buffer) == 0
assert lib.recvxPlayerSpeedAt(player_assets_buffer, 0, 0,
                              ctypes.byref(player_speed)) == 1
assert player_speed.value == 0.5
assert lib.recvxPlayerSpeedAt(player_assets_buffer, 0, 1,
                              ctypes.byref(player_speed)) == 0


class PlayerModelAsset(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("skin", ctypes.c_void_p),
                ("size", ctypes.c_uint32), ("skin_size", ctypes.c_uint32),
                ("record_slot", ctypes.c_uint32), ("model_index", ctypes.c_uint32),
                ("flags", ctypes.c_uint32), ("header_offset", ctypes.c_uint32),
                ("object_offset", ctypes.c_uint32), ("texture_offset", ctypes.c_uint32),
                ("object_count", ctypes.c_uint16), ("chunk_model", ctypes.c_uint8)]


class PlayerMotionAsset(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("size", ctypes.c_uint32),
                ("record_slot", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                ("frame_count", ctypes.c_uint32), ("motion_offset", ctypes.c_uint32),
                ("data_offset", ctypes.c_uint32), ("attribute_offset", ctypes.c_uint32),
                ("object_count", ctypes.c_uint16), ("motion_type", ctypes.c_uint16),
                ("interpolation", ctypes.c_uint16), ("present", ctypes.c_uint8)]


class CutArea(ctypes.Structure):
    _fields_ = [("attr", ctypes.c_uint32), ("min_x", ctypes.c_float),
                ("min_z", ctypes.c_float), ("max_x", ctypes.c_float),
                ("max_z", ctypes.c_float), ("floor", ctypes.c_int8),
                ("type", ctypes.c_uint8)]


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
assert mesh.vertices[0].color == 0xFF563412
assert not mesh.wire_vertices
assert lib.recvxMeshSetWireframe(ctypes.byref(mesh), 1) == 0
assert ctypes.cast(mesh.wire_vertices, ctypes.c_void_p).value % 16 == 0
assert [(v.x, v.y, v.z) for v in mesh.wire_vertices[:6]] == [
    (0,0,0), (1,0,0), (1,0,0), (0,1,0), (0,1,0), (0,0,0)]
assert mesh.batch_count == 1 and mesh.batches[0].vertex_count == 3
assert mesh.batches[0].texture_index == 0
assert [(v.u,v.v) for v in mesh.textured_vertices[:3]] == [(0,1)]*3
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
data, (_,_,polygon) = fixture()
data[polygon+9] = 0x30
struct.pack_into("<H",data,polygon+10,0x4002)
for i,uv in enumerate(((0,0),(1023,0),(0,1023))):
    struct.pack_into("<HH",data,polygon+22+i*6,*uv)
assert build(data) == 0
assert mesh.batches[0].texture_index == 2 and mesh.batches[0].flags == 7
assert [(v.u,v.v) for v in mesh.textured_vertices[:3]] == [(0,0),(1,0),(0,1)]
struct.pack_into("<h",data,polygon+18,-3)
assert build(data) == 0
assert [(v.u,v.v) for v in mesh.textured_vertices[:3]] == [(1,0),(0,0),(0,1)]
data[polygon+9] = 0xF0
assert build(data) == 0
assert [(v.u,v.v) for v in mesh.textured_vertices[:3]] == [(0,1),(1,1),(1,0)]
dummy = (Texture*3)()
textures = TextureSet(dummy,3,0)
assert lib.recvxValidateMeshTextures(ctypes.byref(mesh),ctypes.byref(textures)) == 0
textures.count = 2
assert lib.recvxValidateMeshTextures(ctypes.byref(mesh),ctypes.byref(textures)) == -1
data, _ = fixture(strip_type=64)
assert build(data) == 0 and mesh.batches[0].texture_index == 0xFFFFFFFF
assert lib.recvxValidateMeshTextures(ctypes.byref(mesh),ctypes.byref(TextureSet())) == 0
data, _ = fixture(objects=2)
assert build(data) == 0 and mesh.batch_count == 2
assert [mesh.batches[i].object_index for i in range(2)] == [0, 1]
assert [mesh.batches[i].vertex_count for i in range(2)] == [3, 3]

# Authored position tracks, including the root, move the posed hierarchy
player_data, _ = fixture()
player_buffer = ctypes.create_string_buffer(bytes(player_data))
player_model = PlayerModelAsset(
    ctypes.addressof(player_buffer) + 0x500, None, len(player_data) - 0x500, 0,
    0, 0, 0, 16, 0, 0xFFFFFFFF, 1, 1)
motion_data = bytearray(104)
struct.pack_into("<4sHHII", motion_data, 0, b"MTN\x80", 16, 1, 16, 0xFFFFFFFF)
struct.pack_into("<IIHH", motion_data, 32, 44, 2, 3, 2)
struct.pack_into("<II", motion_data, 60, 52, 76)
struct.pack_into("<3f3f", motion_data, 68, 2, 3, 4, -1, 5, 2)
struct.pack_into("<3H3H", motion_data, 92, 65535, 0, 0, 1, 0, 0)
motion_buffer = ctypes.create_string_buffer(bytes(motion_data))
player_motion = PlayerMotionAsset(
    ctypes.cast(motion_buffer, ctypes.c_void_p), len(motion_data), 0, 0,
    2, 16, 44, 0xFFFFFFFF, 1, 3, 2, 1)
assert lib.recvxBuildPlayerMesh(ctypes.byref(player_model), ctypes.byref(mesh)) == 0
assert lib.recvxPosePlayerMesh(ctypes.byref(player_model), ctypes.byref(player_motion),
                               0, ctypes.byref(mesh)) == 0
for actual, expected in zip(positions(), [(2,3,4), (3,3,4), (2,4,4)]):
    assert all(abs(a-b) < 1.1e-4 for a, b in zip(actual, expected))
assert lib.recvxPosePlayerMesh(ctypes.byref(player_model), ctypes.byref(player_motion),
                               1, ctypes.byref(mesh)) == 0
for actual, expected in zip(positions(), [(-1,5,2), (0,5,2), (-1,6,2)]):
    assert all(abs(a-b) < 1.1e-4 for a, b in zip(actual, expected))
assert lib.recvxBlendPlayerMeshPose(ctypes.byref(player_model),
                                    ctypes.byref(player_motion), 0,
                                    ctypes.byref(player_motion), 1, 0.5,
                                    ctypes.byref(mesh)) == 0
for actual, expected in zip(positions(), [(0.5,4,3), (1.5,4,3), (0.5,5,3)]):
    assert all(abs(a-b) < 1e-6 for a, b in zip(actual, expected))
lib.recvxMeshFree(ctypes.byref(mesh))
lib.recvxMeshFree(ctypes.byref(mesh))

# Cut selection keeps camera choice stable at a shared edge
camera_mesh = Mesh()
camera_areas = (CutArea * 2)(
    CutArea(0x8000, 0, 0, 10, 10, 0, 0),
    CutArea(0, 10, 0, 20, 10, 0, 0))
camera_cuts = (RoomCut * 2)()
for index in range(2):
    camera_cuts[index].area_first = index
    camera_cuts[index].area_count = 1
    camera_cuts[index].active = 1
camera_mesh.cuts = camera_cuts
camera_mesh.cut_count = 2
camera_mesh.cut_areas = ctypes.cast(camera_areas, ctypes.c_void_p)
camera_mesh.cut_area_count = 2
assert lib.recvxRoomCutForPosition(ctypes.byref(camera_mesh), 5, 5, 0, -1) == 0
assert lib.recvxRoomCameraForPosition(ctypes.byref(camera_mesh), 0, 5, 5, 0) == 1
assert lib.recvxRoomCutForPosition(ctypes.byref(camera_mesh), 10, 5, 0, 0) == 0
assert lib.recvxRoomCutForPosition(ctypes.byref(camera_mesh), 12, 5, 0, 0) == 1
assert lib.recvxRoomCameraForPosition(ctypes.byref(camera_mesh), 1, 15, 5, 0) == 0
print("PASS: MLB transforms, UVs, skinning, camera cuts and ownership")
