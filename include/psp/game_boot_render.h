#ifndef RECVX_PSP_GAME_BOOT_RENDER_H
#define RECVX_PSP_GAME_BOOT_RENDER_H
#include "psp/game_boot.h"

typedef struct RecvxBootRenderVertex {
    float u, v;
    uint32_t color; /* PSP ABGR. */
    float x, y, z; /* GU view coordinates, forward is negative Z. */
} RecvxBootRenderVertex;

typedef struct RecvxBootRenderBatch {
    unsigned int first, count, attributes, sampler;
    int blend_source, blend_destination;
    const RecvxBootTexture *texture;
} RecvxBootRenderBatch;

typedef struct RecvxBootRenderFrame {
    const RecvxBootRenderVertex *vertices;
    const RecvxBootRenderBatch *batches;
    size_t vertex_count, batch_count;
    float projection[16];
} RecvxBootRenderFrame;

void recvxGameBootRenderReset(void);
void recvxGameBootRenderBeginFrame(void);
const RecvxBootRenderFrame *recvxGameBootRenderFrame(void);
void recvxGameBootRenderConsume(void);
int recvxGameBootRenderPending(void);
#endif
