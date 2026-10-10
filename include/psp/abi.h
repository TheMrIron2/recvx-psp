#ifndef RECVX_PSP_ABI_H
#define RECVX_PSP_ABI_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t RecvxGameInt32;
typedef uint32_t RecvxGameUInt32;
typedef uint32_t RecvxDiskOffset32;
typedef int32_t RecvxNinjaAngle;

typedef struct RecvxNinjaVector3 {
    float x, y, z;
} RecvxNinjaVector3;

typedef float RecvxNinjaMatrix[16];

typedef struct RecvxRoomDiskPosition {
    float x, y, z;
    RecvxNinjaAngle yaw;
} RecvxRoomDiskPosition;

typedef struct RecvxRoomDiskAttribute {
    uint8_t flags;
    uint8_t type;
    uint8_t id;
    int8_t floor;
    uint32_t attributes;
    float x, y, z, width, height, depth;
    uint8_t parameters[4];
} RecvxRoomDiskAttribute;

typedef struct RecvxRoomDiskActorPlacement {
    uint32_t flags;
    uint16_t id;
    uint16_t type;
    int8_t floor;
    uint8_t model_version;
    uint8_t work_no;
    int8_t parameter;
    float x, y, z;
    int16_t angle_x, angle_z, angle_y, angle_speed;
    int8_t hide[4];
} RecvxRoomDiskActorPlacement;

typedef struct RecvxRoomDiskEffect {
    uint32_t flags;
    uint16_t id;
    uint16_t type;
    int16_t floor;
    uint16_t model_version;
    float x, y, z, scale_x, scale_y, scale_z;
    int16_t angle_y, angle_x;
    int32_t lock_flags, lock_no, lock_object_no;
    float lock_x, lock_y, lock_z;
    int32_t parameter;
} RecvxRoomDiskEffect;

typedef struct RecvxRoomDiskCutWorkPrefix {
    uint8_t flags[4];
    RecvxDiskOffset32 cut_records_offset;
    float center_x, center_y, center_z, width, height, depth;
    uint8_t camera_and_extension[0x288];
} RecvxRoomDiskCutWorkPrefix;

typedef struct RecvxRoomDiskHeaderPrefix {
    uint32_t version_bits;
    uint32_t reserved[3];
    RecvxDiskOffset32 rom_work_offset;
    RecvxDiskOffset32 model_directory_offset;
    RecvxDiskOffset32 motion_directory_offset;
    RecvxDiskOffset32 event_offset;
    RecvxDiskOffset32 texture_directory_offset;
} RecvxRoomDiskHeaderPrefix;

typedef struct RecvxRoomDiskWork {
    RecvxDiskOffset32 array_offsets[32];
    uint32_t counts_and_reserved[32];
    uint32_t flags;
    uint32_t background_color;
    uint32_t fog_color;
    float fog_near;
    float fog_far;
    float width;
    float wall_height;
    float depth;
    float floor_heights[32];
    uint8_t model_work[0x18];
    float fog_table[128];
    int8_t ambient_type[4];
    float ambient_red[4];
    float ambient_green[4];
    float ambient_blue[4];
} RecvxRoomDiskWork;

enum RecvxRoomArrayOffsetSlot {
    RECVX_ROOM_ARRAY_CUT = 0,
    RECVX_ROOM_ARRAY_LIGHT,
    RECVX_ROOM_ARRAY_ENEMY,
    RECVX_ROOM_ARRAY_OBJECT,
    RECVX_ROOM_ARRAY_ITEM,
    RECVX_ROOM_ARRAY_EFFECT,
    RECVX_ROOM_ARRAY_WALL,
    RECVX_ROOM_ARRAY_ETC,
    RECVX_ROOM_ARRAY_FLOOR,
    RECVX_ROOM_ARRAY_POSITION,
    RECVX_ROOM_ARRAY_ROUTE,
    RECVX_ROOM_ARRAY_ROUTE_TABLE,
    RECVX_ROOM_ARRAY_EVENT,
    RECVX_ROOM_ARRAY_EVENT_CONDITION,
    RECVX_ROOM_ARRAY_MESSAGE,
    RECVX_ROOM_ARRAY_EVENT_LIGHT
};

enum RecvxRoomArrayCount {
    RECVX_ROOM_COUNT_CUT = 0,
    RECVX_ROOM_COUNT_LIGHT,
    RECVX_ROOM_COUNT_ENEMY,
    RECVX_ROOM_COUNT_OBJECT,
    RECVX_ROOM_COUNT_ITEM,
    RECVX_ROOM_COUNT_EFFECT,
    RECVX_ROOM_COUNT_WALL,
    RECVX_ROOM_COUNT_ETC,
    RECVX_ROOM_COUNT_FLOOR,
    RECVX_ROOM_COUNT_POSITION,
    RECVX_ROOM_COUNT_ROUTE,
    RECVX_ROOM_COUNT_EVENT_CONDITION,
    RECVX_ROOM_COUNT_MESSAGE,
    RECVX_ROOM_COUNT_EVENT_LIGHT,
    RECVX_ROOM_COUNT_TOTAL
};

#define RECVX_ROOM_ARRAY_DISK_OFFSET(slot) \
    (offsetof(RecvxRoomDiskWork, array_offsets) + (slot) * sizeof(uint32_t))
#define RECVX_ROOM_COUNT_DISK_OFFSET(index) \
    (offsetof(RecvxRoomDiskWork, counts_and_reserved) + (index) * sizeof(uint32_t))

#define RECVX_ROOM_DISK_MIN_HEADER_SIZE 128u

_Static_assert(sizeof(RecvxGameInt32) == 4, "game integers are 32-bit");
_Static_assert(sizeof(RecvxDiskOffset32) == 4, "disk offsets are 32-bit");
_Static_assert(sizeof(RecvxNinjaAngle) == 4, "Ninja angles are 32-bit");
_Static_assert(sizeof(RecvxNinjaVector3) == 12, "Ninja vectors are three floats");
_Static_assert(sizeof(RecvxNinjaMatrix) == 64, "Ninja matrices are sixteen floats");
_Static_assert(offsetof(RecvxRoomDiskPosition, yaw) == 0x0c,
               "room spawn angle offset");
_Static_assert(sizeof(RecvxRoomDiskPosition) == 0x10, "room spawn record size");
_Static_assert(offsetof(RecvxRoomDiskAttribute, attributes) == 0x04,
               "room collision attributes offset");
_Static_assert(offsetof(RecvxRoomDiskAttribute, x) == 0x08,
               "room collision position offset");
_Static_assert(offsetof(RecvxRoomDiskAttribute, parameters) == 0x20,
               "room ETC parameters offset");
_Static_assert(sizeof(RecvxRoomDiskAttribute) == 0x24,
               "room collision record size");
_Static_assert(offsetof(RecvxRoomDiskActorPlacement, x) == 0x0c,
               "room actor position offset");
_Static_assert(offsetof(RecvxRoomDiskActorPlacement, angle_x) == 0x18,
               "room actor angle offset");
_Static_assert(offsetof(RecvxRoomDiskActorPlacement, hide) == 0x20,
               "room actor hide flags offset");
_Static_assert(sizeof(RecvxRoomDiskActorPlacement) == 0x24,
               "room actor placement size");
_Static_assert(offsetof(RecvxRoomDiskEffect, lock_flags) == 0x28,
               "room effect lock flags offset");
_Static_assert(offsetof(RecvxRoomDiskEffect, parameter) == 0x40,
               "room effect parameter offset");
_Static_assert(sizeof(RecvxRoomDiskEffect) == 0x44,
               "room effect record size");
_Static_assert(offsetof(RecvxRoomDiskCutWorkPrefix, cut_records_offset) == 0x04,
               "room cut records offset");
_Static_assert(offsetof(RecvxRoomDiskCutWorkPrefix, center_x) == 0x08,
               "room cut center offset");
_Static_assert(offsetof(RecvxRoomDiskCutWorkPrefix, camera_and_extension) == 0x20,
               "room cut camera offset");
_Static_assert(sizeof(RecvxRoomDiskCutWorkPrefix) == 0x2a8,
               "room cut record size");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, rom_work_offset) == 0x10,
               "room header ROM_WORK offset");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, model_directory_offset) == 0x14,
               "room header model directory offset");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, motion_directory_offset) == 0x18,
               "room header motion directory offset");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, event_offset) == 0x1c,
               "room header event offset");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, texture_directory_offset) == 0x20,
               "room header texture directory offset");
_Static_assert(offsetof(RecvxRoomDiskWork, array_offsets) == 0x00,
               "ROM_WORK array offset table");
_Static_assert(offsetof(RecvxRoomDiskWork, counts_and_reserved) == 0x80,
               "ROM_WORK array counts");
_Static_assert(RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_WALL) == 0x18,
               "ROM_WORK wall array offset");
_Static_assert(RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_ETC) == 0x1c,
               "ROM_WORK ETC array offset");
_Static_assert(RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_POSITION) == 0x24,
               "ROM_WORK position array offset");
_Static_assert(RECVX_ROOM_COUNT_DISK_OFFSET(RECVX_ROOM_COUNT_WALL) == 0x98,
               "ROM_WORK wall count offset");
_Static_assert(offsetof(RecvxRoomDiskWork, wall_height) == 0x118,
               "ROM_WORK wall height");
_Static_assert(offsetof(RecvxRoomDiskWork, floor_heights) == 0x120,
               "ROM_WORK floor heights");
_Static_assert(offsetof(RecvxRoomDiskWork, ambient_type) == 0x3b8,
               "ROM_WORK ambient types");
_Static_assert(sizeof(RecvxRoomDiskWork) == 0x3ec, "ROM_WORK disk size");

#endif
