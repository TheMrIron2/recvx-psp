#ifndef RECVX_PSP_ACTORS_H
#define RECVX_PSP_ACTORS_H

#include "psp/mesh.h"

enum RecvxRoomEntityType {
    RECVX_ROOM_ENTITY_ENEMY = 1,
    RECVX_ROOM_ENTITY_OBJECT = 2,
    RECVX_ROOM_ENTITY_ITEM = 3
};

#define RECVX_ROOM_ENTITY_PART_LIMIT 4

typedef struct RecvxRoomEntity {
    uint32_t flags;
    uint32_t model_bytes;
    uint16_t source_index;
    uint8_t type;
    uint8_t part_count;
    uint8_t visible;
    int8_t hide[4];
    float position[3];
    int16_t angle_x, angle_z, angle_y;
    RecvxRoomMesh parts[RECVX_ROOM_ENTITY_PART_LIMIT];
} RecvxRoomEntity;

typedef struct RecvxRoomActors {
    RecvxRoomEntity *entities;
    uint32_t entity_count;
    uint32_t triangle_count;
    uint32_t unsupported_model_count;
    size_t storage_bytes;
} RecvxRoomActors;

int recvxBuildRoomActors(const void *room, size_t size, RecvxRoomActors *actors);
void recvxRoomActorsFree(RecvxRoomActors *actors);

#endif
