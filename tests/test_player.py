import ctypes
import struct
import sys
from pathlib import Path

lib = ctypes.CDLL(str(Path(sys.argv[1]).resolve()))


class PlayerWall(ctypes.Structure):
    _fields_ = [("attr", ctypes.c_uint32), ("px", ctypes.c_float),
                ("py", ctypes.c_float), ("pz", ctypes.c_float),
                ("w", ctypes.c_float), ("h", ctypes.c_float),
                ("d", ctypes.c_float), ("active", ctypes.c_uint8),
                ("type", ctypes.c_uint8), ("id", ctypes.c_uint8),
                ("floor", ctypes.c_int8), ("parameters", ctypes.c_uint8 * 4)]


class PlayerRoom(ctypes.Structure):
    _fields_ = [("walls", ctypes.POINTER(PlayerWall)),
                ("wall_count", ctypes.c_uint32), ("doors", ctypes.c_void_p),
                ("door_count", ctypes.c_uint32), ("wall_height", ctypes.c_float),
                ("floor_heights", ctypes.c_float * 32),
                ("floors", ctypes.POINTER(PlayerWall)),
                ("floor_count", ctypes.c_uint32),
                ("stairs", ctypes.POINTER(PlayerWall)),
                ("stair_count", ctypes.c_uint32),
                ("stair_transitions", ctypes.POINTER(PlayerWall)),
                ("stair_transition_count", ctypes.c_uint32)]


class Player(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float),
                ("z", ctypes.c_float), ("yaw", ctypes.c_float),
                ("vertical_speed", ctypes.c_float), ("walk_phase", ctypes.c_float),
                ("floor", ctypes.c_int32), ("grounded", ctypes.c_int),
                ("blocked", ctypes.c_int), ("game_flags", ctypes.c_uint32),
                ("surface_flags", ctypes.c_uint32),
                ("floor_sound", ctypes.c_uint8), ("foot_effect", ctypes.c_uint8),
                ("stair_index", ctypes.c_int32),
                ("floor_target_index", ctypes.c_int32),
                ("stair_transition_index", ctypes.c_int32),
                ("stair_target_floor", ctypes.c_int32),
                ("stair_motion_slot", ctypes.c_int32),
                ("stair_motion_frame", ctypes.c_uint32),
                ("stair_stop_frame", ctypes.c_uint32),
                ("stair_recovery_ticks", ctypes.c_uint32),
                ("stair_target_yaw", ctypes.c_float),
                ("stair_target_y", ctypes.c_float),
                ("stair_last_motion", ctypes.c_float * 3),
                ("stair_phase", ctypes.c_uint8),
                ("stair_root_position_valid", ctypes.c_uint8)]


lib.recvxPlayerUpdateAtSpeed.argtypes = [ctypes.POINTER(Player),
                                         ctypes.POINTER(PlayerRoom),
                                         ctypes.c_float, ctypes.c_float,
                                         ctypes.c_float, ctypes.c_float,
                                         ctypes.c_float]
lib.recvxPlayerRoomLoad.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                    ctypes.POINTER(PlayerRoom)]
lib.recvxPlayerRoomFree.argtypes = [ctypes.POINTER(PlayerRoom)]


def update_wall(attributes=0, floor=0, wall_y=0.0, wall_height=20.0,
                speed=10.0):
    wall_buffer = (PlayerWall * 1)(PlayerWall(
        attributes, -5.0, wall_y, -5.0, 10.0, wall_height, 1.0,
        1, 0, 0, floor))
    room = PlayerRoom()
    room.walls = wall_buffer
    room.wall_count = 1
    room.wall_height = 10000.0
    player = Player(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 1, 0)
    lib.recvxPlayerUpdateAtSpeed(ctypes.byref(player), ctypes.byref(room),
                                 1.0, 0.0, speed, 2.4, 0.1)
    return player


player = update_wall(speed=20.0)
assert player.blocked == 1 and abs(player.z + 0.5) < 0.005

player = update_wall(wall_y=15.0, wall_height=1.0, speed=20.0)
assert player.blocked == 1 and abs(player.z + 0.5) < 0.005

player = update_wall(attributes=0x4)
assert player.blocked == 0 and player.z == -0.5

player = update_wall(attributes=0x1, floor=1)
assert player.blocked == 0 and player.z == -0.5

room_data = bytearray(1132 + 3 * 36)
struct.pack_into("<f", room_data, 0, 10.0)
struct.pack_into("<I", room_data, 16, 128)
struct.pack_into("<I", room_data, 128 + 8 * 4, 1132)
struct.pack_into("<I", room_data, 128 + 0x80 + 8 * 4, 3)
struct.pack_into("<4BI6f4B", room_data, 1132,
                 1, 3, 0, 0, 0, -5.0, 0.0, -5.0, 10.0, 2.0, 10.0,
                 3, 0, 0, 0)
struct.pack_into("<4BI6f4B", room_data, 1168,
                 1, 3, 0, 0, 0, -5.0, 0.0, -5.0, 10.0, 2.0, 10.0,
                 1, 0, 0, 0)
struct.pack_into("<4BI6f4B", room_data, 1204,
                 1, 1, 0, 0, 0, -5.0, 0.0, -5.0, 10.0, 2.0, 10.0,
                 7, 0, 0, 0)
loaded_room = PlayerRoom()
room_buffer = ctypes.create_string_buffer(bytes(room_data))
assert lib.recvxPlayerRoomLoad(room_buffer, len(room_data),
                               ctypes.byref(loaded_room)) == 0
assert loaded_room.floor_count == 3 and loaded_room.floors[0].parameters[0] == 3
assert loaded_room.floors[0].px == -5.0 and loaded_room.floors[0].d == 10.0
player = Player(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 1, 0)
lib.recvxPlayerUpdateAtSpeed(ctypes.byref(player), ctypes.byref(loaded_room),
                             1.0, 0.0, 10.0, 2.4, 0.1)
assert player.surface_flags & 0x3 == 0x3
assert player.floor_sound == 7 and player.foot_effect == 2
assert abs(player.z + 0.4) < 1e-6
lib.recvxPlayerRoomFree(ctypes.byref(loaded_room))

print("PASS: original Claire collider size, wall height and floor/attribute filters")
