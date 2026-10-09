#ifndef RECVX_PSP_MESH_H
#define RECVX_PSP_MESH_H

#include "psp/scene.h"
#include "psp/texture.h"

typedef struct RecvxTexturedVertex {
    float u, v;
    uint32_t color;
    float x, y, z;
} RecvxTexturedVertex;

typedef struct RecvxMeshBatch {
    uint32_t first_vertex, vertex_count, texture_index, flags;
} RecvxMeshBatch;

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
    RecvxTexturedVertex *textured_vertices;
    RecvxMeshBatch *batches;
    uint32_t batch_count, storage_bytes;
} RecvxRoomMesh;

int recvxBuildRoomMesh(const void *room, size_t size, RecvxRoomMesh *mesh);
void recvxMeshFree(RecvxRoomMesh *mesh);
int recvxValidateMeshTextures(const RecvxRoomMesh *mesh, const RecvxTextureSet *textures);

#endif
