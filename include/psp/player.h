#ifndef RECVX_PSP_PLAYER_H
#define RECVX_PSP_PLAYER_H

#include <stddef.h>
#include <stdint.h>

#include "psp/player_asset.h"

typedef struct RecvxPlayerWall {
    uint32_t attr;
    float px, py, pz, w, h, d;
    uint8_t active, type, id;
    int8_t floor;
    uint8_t parameters[4];
} RecvxPlayerWall;

typedef RecvxPlayerWall RecvxPlayerFloor;

typedef struct RecvxPlayerDoor {
    uint32_t attr;
    float x, y, z, width, height, depth;
    uint8_t id;
    int8_t floor;
    uint8_t target_stage, target_room, target_pos, door_type;
} RecvxPlayerDoor;

typedef struct RecvxPlayerRoom {
    RecvxPlayerWall *walls;
    uint32_t wall_count;
    RecvxPlayerDoor *doors;
    uint32_t door_count;
    float wall_height;
    float floor_heights[32];
    RecvxPlayerFloor *floors;
    uint32_t floor_count;
    RecvxPlayerWall *stairs;
    uint32_t stair_count;
    RecvxPlayerWall *stair_transitions;
    uint32_t stair_transition_count;
} RecvxPlayerRoom;

typedef struct RecvxPlayer {
    float x, y, z, yaw;
    float vertical_speed;
    float walk_phase;
    int32_t floor;
    int grounded;
    int blocked;
    uint32_t game_flags;
    uint32_t surface_flags;
    uint8_t floor_sound;
    uint8_t foot_effect;
    int32_t stair_index;
    int32_t floor_target_index;
    int32_t stair_transition_index;
    int32_t stair_target_floor;
    int32_t stair_motion_slot;
    uint32_t stair_motion_frame;
    uint32_t stair_stop_frame;
    uint32_t stair_recovery_ticks;
    float stair_target_yaw;
    float stair_target_y;
    float stair_last_motion[3];
    uint8_t stair_phase;
    uint8_t stair_root_position_valid;
} RecvxPlayer;

#define RECVX_PLAYER_SURFACE_SLOW 0x1u
#define RECVX_PLAYER_SURFACE_WATER 0x2u
#define RECVX_PLAYER_SURFACE_HAZARD 0x4u

#define RECVX_PLAYER_STAIR_NONE 0u
#define RECVX_PLAYER_STAIR_TURN 1u
#define RECVX_PLAYER_STAIR_MOVE 2u
#define RECVX_PLAYER_STAIR_RECOVER 3u

int recvxPlayerRoomLoad(const void *data, size_t size, RecvxPlayerRoom *room);
void recvxPlayerRoomFree(RecvxPlayerRoom *room);
int recvxPlayerSpawn(RecvxPlayer *player, const RecvxPlayerRoom *room,
                     const void *data, size_t size, uint32_t position_index);
void recvxPlayerUpdate(RecvxPlayer *player, const RecvxPlayerRoom *room,
                       float forward, float turn, float dt);
void recvxPlayerUpdateAtSpeed(RecvxPlayer *player, const RecvxPlayerRoom *room,
                              float forward, float turn, float speed,
                              float turn_speed, float dt);
int recvxPlayerTryBeginStair(RecvxPlayer *player,
                             const RecvxPlayerRoom *room,
                             const RecvxPlayerAssets *assets);
int recvxPlayerAdvanceStair(RecvxPlayer *player,
                            const RecvxPlayerRoom *room,
                            const RecvxPlayerAssets *assets);
int recvxPlayerDoorAt(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                      RecvxPlayerDoor *door);

#endif
