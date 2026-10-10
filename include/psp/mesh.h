#ifndef RECVX_PSP_MESH_H
#define RECVX_PSP_MESH_H

#include "psp/scene.h"
#include "psp/texture.h"
#include "psp/player_asset.h"

typedef struct RecvxMeshVertex {
    float u, v;
    uint32_t color;
    float nx, ny, nz;
    float x, y, z;
} RecvxMeshVertex;

typedef RecvxMeshVertex RecvxTexturedVertex;

typedef struct RecvxRoomLight {
    uint32_t flags, type, source;
    float position[3], direction[3], color[3], near_range, far_range;
} RecvxRoomLight;

typedef struct RecvxCutArea {
    uint32_t attr;
    float min_x, min_z, max_x, max_z;
    int8_t floor;
    uint8_t type;
} RecvxCutArea;

#define RECVX_BLEND_ALPHA 0x10000u
#define RECVX_BLEND_DOUBLE_SIDED 0x20000u

typedef struct RecvxRoomCut {
    uint32_t hide_objects[2][16];
    uint32_t hide_lights[2][8];
    uint32_t area_first, area_count;
    uint8_t active;
} RecvxRoomCut;

typedef struct RecvxRoomCamera {
    uint16_t flags;
    uint8_t light_clip, speed;
    float position[3], lens, width, height, depth;
    float active_y[4], angle_speed;
    int32_t angles[3], angle_limits[2];
    int16_t roll_limits[4];
    uint8_t angle_speed8, filter_number, filter_rate, reserved;
    uint32_t perspective;
    float cut_center_x, cut_center_z, cut_width, cut_depth;
} RecvxRoomCamera;

typedef struct RecvxMeshBatch {
    uint32_t first_vertex, vertex_count, texture_index, flags, object_index;
    uint32_t blend_state;
} RecvxMeshBatch;

typedef struct RecvxRoomMesh {
    void *storage;
    RecvxMeshVertex *vertices;
    uint32_t vertex_count;
    uint32_t object_count;
    uint32_t model_count;
    uint32_t triangle_count;
    float center[3];
    float radius;
    RecvxSceneVertex *wire_vertices;
    uint32_t *material_colors;
    RecvxMeshVertex *textured_vertices;
    RecvxMeshBatch *batches;
    uint32_t batch_count, storage_bytes;
    RecvxRoomCut *cuts;
    uint32_t cut_count;
    RecvxRoomCamera *cameras;
    RecvxCutArea *cut_areas;
    uint32_t cut_area_count;
    RecvxRoomLight *lights;
    uint32_t light_count;
    float ambient[3];
    void *wire_storage;
} RecvxRoomMesh;

int recvxBuildRoomMesh(const void *room, size_t size, RecvxRoomMesh *mesh);
int recvxBuildPlayerMesh(const RecvxPlayerModelAsset *model, RecvxRoomMesh *mesh);
int recvxMeshSetWireframe(RecvxRoomMesh *mesh, int enabled);
int recvxApplyRoomLighting(RecvxRoomMesh *mesh, uint32_t cut, uint32_t camera);
int recvxRoomCutForPosition(const RecvxRoomMesh *mesh, float x, float z,
                            int floor, int current_cut);
uint32_t recvxRoomCutAttributeForPosition(const RecvxRoomMesh *mesh, uint32_t cut,
                                         float x, float z, int floor);
uint32_t recvxRoomCameraForPosition(const RecvxRoomMesh *mesh, uint32_t cut,
                                    float x, float z, int floor);
void recvxMeshFree(RecvxRoomMesh *mesh);
int recvxValidateMeshTextures(const RecvxRoomMesh *mesh, const RecvxTextureSet *textures);

#endif
