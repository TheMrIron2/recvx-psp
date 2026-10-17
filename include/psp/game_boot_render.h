#ifndef RECVX_PSP_GAME_BOOT_RENDER_H
#define RECVX_PSP_GAME_BOOT_RENDER_H
#include "psp/game_boot.h"
#include "psp/game_boot_chunk.h"

typedef struct RecvxBootRenderVertex {
    float u, v;
    uint32_t color; /* PSP ABGR. */
    float x, y, z; /* GU view coordinates, forward is negative Z. */
} RecvxBootRenderVertex;

typedef enum RecvxBootRenderKind {
    RECVX_RENDER_TRIANGLES, RECVX_RENDER_LINES, RECVX_RENDER_HALF_FOG, RECVX_RENDER_SCREEN_TRIANGLES, RECVX_RENDER_SHADOW_START,
    RECVX_RENDER_SHADOW_ADD, RECVX_RENDER_SHADOW_SUBTRACT,
    RECVX_RENDER_SHADOW_COMPOSITE, RECVX_RENDER_SHADOW_END
} RecvxBootRenderKind;

typedef struct RecvxBootRenderBatch {
    unsigned int first, count, attributes, sampler;
    int blend_source, blend_destination;
    const RecvxBootTexture *texture;
    RecvxBootRenderKind kind;
    unsigned int fog_color; /* PSP RGB for authored half-fog sprites. */
} RecvxBootRenderBatch;

typedef struct RecvxBootRenderFrame {
    const RecvxBootRenderVertex *vertices;
    const RecvxBootRenderBatch *batches;
    size_t vertex_count, batch_count;
    float projection[16];
} RecvxBootRenderFrame;

/* Submit one unlit, double-sided local-space sprite triangle. */
void recvxGameBootRenderSpriteTriangle(const RecvxGameChunkVertex input[3], int textured, int trans);
void recvxGameBootRenderHalfFogTriangle(const RecvxGameChunkVertex input[3], unsigned int offset_argb, int trans);
void recvxGameBootRenderScreenTriangle(const RecvxGameChunkVertex input[3], int trans);
void recvxGameBootRenderLine(const RecvxGameChunkVertex input[2], int trans);
void recvxGameBootRenderShadowStart(void);
void recvxGameBootRenderShadowTriangle(const RecvxGameChunkVertex input[3], unsigned int pass);
void recvxGameBootRenderShadowComposite(const float bounds[4], unsigned int fog);
void recvxGameBootRenderShadowEnd(void);
void recvxGameBootRenderReset(void);
/* Preserve already queued users before changing the live texture's palette.
   The view shares pixel storage and lives until the submitted frame is consumed. */
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *texture);
void recvxGameBootRenderBeginFrame(void);
const RecvxBootRenderFrame *recvxGameBootRenderFrame(void);
void recvxGameBootRenderConsume(void);
int recvxGameBootRenderPending(void);
#endif
