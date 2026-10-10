#ifndef RECVX_PSP_PLAYER_H
#define RECVX_PSP_PLAYER_H

#include <stddef.h>
#include <stdint.h>

typedef struct RecvxPlayerWall {
    uint32_t attr;
    float px, py, pz, w, h, d;
    uint8_t active, type, id, floor;
} RecvxPlayerWall;

typedef struct RecvxPlayerDoor {
    uint32_t attr;
    float x, y, z, width, height, depth;
    uint8_t id, floor;
    uint8_t target_stage, target_room, target_pos, door_type;
} RecvxPlayerDoor;

typedef struct RecvxPlayerRoom {
    RecvxPlayerWall *walls;
    uint32_t wall_count;
    RecvxPlayerDoor *doors;
    uint32_t door_count;
    float wall_height;
    float floor_heights[32];
} RecvxPlayerRoom;

typedef struct RecvxPlayer {
    float x, y, z, yaw;
    float vertical_speed;
    float walk_phase;
    int32_t floor;
    int grounded;
    int blocked;
    int walking_backward;
} RecvxPlayer;

int recvxPlayerRoomLoad(const void *data, size_t size, RecvxPlayerRoom *room);
void recvxPlayerRoomFree(RecvxPlayerRoom *room);
int recvxPlayerSpawn(RecvxPlayer *player, const RecvxPlayerRoom *room,
                     const void *data, size_t size, uint32_t position_index);
void recvxPlayerUpdate(RecvxPlayer *player, const RecvxPlayerRoom *room,
                       float forward, float turn, float dt);
int recvxPlayerDoorAt(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                      RecvxPlayerDoor *door);

#endif
