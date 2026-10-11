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

typedef struct RecvxRoomDiskLightWork {
    uint32_t flags, type;
    int32_t animation_speed, lock_flags, lock_no, lock_object_no, source;
    float x, y, z;
    float direction_x, direction_y, direction_z;
    float velocity_x, velocity_y, velocity_z;
    float specular, diffuse, ambient;
    float red, green, blue, near_range, far_range;
    int32_t inner_angle, outer_angle, angle_x, angle_y, angle_z;
    uint32_t mode;
    int32_t counter[4];
    float world_x, world_y, world_z;
    float world_velocity_x, world_velocity_y, world_velocity_z;
    float world_specular, world_diffuse, world_ambient;
    float world_red, world_green, world_blue;
    float world_near_range, world_far_range;
    int32_t world_inner_angle, world_outer_angle;
    int32_t world_angle_x, world_angle_y, world_angle_z;
    RecvxDiskOffset32 lock_work_offset, extra_offset, light_offset;
} RecvxRoomDiskLightWork;

typedef struct RecvxRoomDiskCameraKeyFrame {
    uint32_t flags;
    int8_t stage, room, position, door_type;
    uint8_t mode[4];
    int32_t counter;
    int16_t angle_x, angle_y, angle_z, perspective;
    uint32_t hidden_objects[8];
    uint32_t hidden_lights[4];
    uint32_t fog_color;
    float fog_near, fog_far;
    int16_t lock_flags, lock_no, lock_object_no, next_no;
    float lock_x, lock_y, lock_z;
    float parameter[5];
    RecvxDiskOffset32 record_offset;
} RecvxRoomDiskCameraKeyFrame;

typedef struct RecvxRoomDiskEventCamera {
    uint16_t flags, type;
    int16_t next_no, keyframe_count;
    RecvxRoomDiskCameraKeyFrame keyframes[16];
} RecvxRoomDiskEventCamera;

typedef struct RecvxPlayerModelDiskHeader {
    uint8_t magic[3], flags;
    uint16_t header_size, object_count;
    RecvxDiskOffset32 texture_offset, object_offset;
} RecvxPlayerModelDiskHeader;

typedef struct RecvxChunkObjectDisk {
    uint32_t flags;
    RecvxDiskOffset32 model_offset;
    float position[3];
    uint32_t angles[3];
    float scale[3];
    RecvxDiskOffset32 child_offset, sibling_offset;
} RecvxChunkObjectDisk;

typedef struct RecvxPlayerMotionDiskHeader {
    uint8_t magic[3], flags;
    uint16_t header_size, object_count;
    RecvxDiskOffset32 motion_offset, attribute_offset;
} RecvxPlayerMotionDiskHeader;

typedef struct RecvxMotionDiskHeader {
    RecvxDiskOffset32 object_tracks_offset;
    uint32_t frame_count;
    uint16_t motion_type, interpolation;
} RecvxMotionDiskHeader;

typedef struct RecvxMotionObjectDiskTrack {
    RecvxDiskOffset32 position_offset, angle_offset;
} RecvxMotionObjectDiskTrack;

typedef struct RecvxMotionPositionDiskKey {
    float x, y, z;
} RecvxMotionPositionDiskKey;

typedef struct RecvxMotionAngleDiskKey {
    uint16_t x, y, z;
} RecvxMotionAngleDiskKey;

typedef struct RecvxGameSaveDiskRecord {
    uint32_t version;
    int32_t save_count;
    int8_t player_id, stage, room, scenario, position, cut, floor, reserved0;
    int32_t game_mode;
    uint32_t event_flags[32];
    uint32_t item_flags[16];
    uint32_t item_box_flags[16];
    uint32_t event_item_flags[32];
    uint32_t key_flags[16];
    uint32_t map_flags[8];
    uint32_t inventory[384];
    uint32_t player_state_flags[4];
    float player_x, player_y, player_z;
    int32_t player_yaw;
    int8_t weapon_numbers[4];
    int16_t player_health[4];
    uint8_t event_position_numbers[4];
    int32_t play_time, stv_time;
    int16_t spray_count, retry_count, clear_count, reserved1;
    uint32_t checksum;
} RecvxGameSaveDiskRecord;

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
_Static_assert(offsetof(RecvxRoomDiskActorPlacement, work_no) == 0x0a,
               "room actor work index offset");
_Static_assert(offsetof(RecvxRoomDiskActorPlacement, hide) == 0x20,
               "room actor hide flags offset");
_Static_assert(sizeof(RecvxRoomDiskActorPlacement) == 0x24,
               "room actor placement size");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, rom_work_offset) == 0x10,
               "room ROM_WORK offset");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, event_offset) == 0x1c,
               "room event-directory offset");
_Static_assert(offsetof(RecvxRoomDiskHeaderPrefix, texture_directory_offset) == 0x20,
               "room texture-directory offset");
_Static_assert(offsetof(RecvxRoomDiskEffect, lock_flags) == 0x28,
               "room effect lock flags offset");
_Static_assert(offsetof(RecvxRoomDiskEffect, parameter) == 0x40,
               "room effect parameter offset");
_Static_assert(sizeof(RecvxRoomDiskEffect) == 0x44,
               "room effect record size");
_Static_assert(offsetof(RecvxRoomDiskLightWork, x) == 0x1c,
               "room light position offset");
_Static_assert(offsetof(RecvxRoomDiskLightWork, world_inner_angle) == 0xc0,
               "room light world angle offset");
_Static_assert(offsetof(RecvxRoomDiskLightWork, light_offset) == 0xdc,
               "room light pointer offset");
_Static_assert(sizeof(RecvxRoomDiskLightWork) == 0xe0,
               "room light record size");
_Static_assert(offsetof(RecvxRoomDiskCameraKeyFrame, angle_x) == 0x10,
               "event camera angle offset");
_Static_assert(offsetof(RecvxRoomDiskCameraKeyFrame, hidden_objects) == 0x18,
               "event camera hidden objects offset");
_Static_assert(offsetof(RecvxRoomDiskCameraKeyFrame, record_offset) == 0x7c,
               "event camera record pointer offset");
_Static_assert(sizeof(RecvxRoomDiskCameraKeyFrame) == 0x80,
               "event camera keyframe size");
_Static_assert(offsetof(RecvxRoomDiskEventCamera, keyframes) == 0x08,
               "event camera keyframe table offset");
_Static_assert(sizeof(RecvxRoomDiskEventCamera) == 0x808,
               "event camera record size");
_Static_assert(offsetof(RecvxPlayerModelDiskHeader, object_offset) == 0x0c,
               "player model object table offset");
_Static_assert(sizeof(RecvxPlayerModelDiskHeader) == 0x10,
               "player model header size");
_Static_assert(offsetof(RecvxChunkObjectDisk, position) == 0x08,
               "chunk object position offset");
_Static_assert(offsetof(RecvxChunkObjectDisk, angles) == 0x14,
               "chunk object angles offset");
_Static_assert(offsetof(RecvxChunkObjectDisk, scale) == 0x20,
               "chunk object scale offset");
_Static_assert(offsetof(RecvxChunkObjectDisk, child_offset) == 0x2c,
               "chunk object child offset");
_Static_assert(sizeof(RecvxChunkObjectDisk) == 0x34,
               "chunk object record size");
_Static_assert(offsetof(RecvxPlayerMotionDiskHeader, attribute_offset) == 0x0c,
               "player motion attributes offset");
_Static_assert(sizeof(RecvxPlayerMotionDiskHeader) == 0x10,
               "player motion header size");
_Static_assert(offsetof(RecvxMotionDiskHeader, motion_type) == 0x08,
               "motion type offset");
_Static_assert(sizeof(RecvxMotionDiskHeader) == 0x0c,
               "motion header size");
_Static_assert(sizeof(RecvxMotionObjectDiskTrack) == 0x08,
               "motion object track size");
_Static_assert(sizeof(RecvxMotionPositionDiskKey) == 0x0c,
               "motion position key size");
_Static_assert(sizeof(RecvxMotionAngleDiskKey) == 0x06,
               "motion angle key size");
_Static_assert(offsetof(RecvxGameSaveDiskRecord, event_flags) == 0x14,
               "save event flags offset");
_Static_assert(offsetof(RecvxGameSaveDiskRecord, inventory) == 0x1f4,
               "save inventory offset");
_Static_assert(offsetof(RecvxGameSaveDiskRecord, player_x) == 0x804,
               "save player position offset");
_Static_assert(offsetof(RecvxGameSaveDiskRecord, checksum) == 0x834,
               "save checksum offset");
_Static_assert(sizeof(RecvxGameSaveDiskRecord) == 0x838,
               "save record size");
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
