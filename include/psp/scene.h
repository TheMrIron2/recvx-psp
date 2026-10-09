#ifndef RECVX_PSP_SCENE_H
#define RECVX_PSP_SCENE_H

#include <stddef.h>
#include <stdint.h>

/* GU_COLOR_8888 | GU_VERTEX_32BITF: ABGR color followed by XYZ floats. */
typedef struct RecvxSceneVertex {
    uint32_t color;
    float x, y, z;
} RecvxSceneVertex;

typedef struct RecvxScene {
    void *storage;
    RecvxSceneVertex *vertices;
    uint32_t vertex_count;
    uint32_t box_count;
    uint32_t marker_count;
    float center[3];
    float radius;
} RecvxScene;

/* Scene must initially be zeroed. Replaces existing geometry on success only.
 * Copies validated spatial fields into owned, 16-byte-aligned line vertices;
 * no pointers into the expanded room are retained. Returns 0 on success,
 * -1 for invalid room/coordinates, -2 for excessive geometry, -3 for no memory.
 * Collision shapes are shown as bounds; zero-height volumes are footprints.
 */
int recvxBuildRoomScene(const void *room, size_t size, RecvxScene *scene);
void recvxSceneFree(RecvxScene *scene);

#endif
