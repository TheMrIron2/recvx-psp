#ifndef RECVX_PSP_MESH_H
#define RECVX_PSP_MESH_H

#include "psp/scene.h"

typedef struct RecvxRoomMesh {
    void *storage;
    RecvxSceneVertex *vertices;
    uint32_t vertex_count;
    uint32_t object_count;
    uint32_t model_count;
    uint32_t triangle_count;
    float center[3];
    float radius;
    RecvxSceneVertex *wire_vertices;
} RecvxRoomMesh;

int recvxBuildRoomMesh(const void *room, size_t size, RecvxRoomMesh *mesh);
void recvxMeshFree(RecvxRoomMesh *mesh);

#endif
