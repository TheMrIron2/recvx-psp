#ifndef RECVX_PSP_GAME_BOOT_RENDER_H
#define RECVX_PSP_GAME_BOOT_RENDER_H
#include "psp/game_boot.h"
#include "psp/game_boot_chunk.h"

typedef struct RecvxBootRenderVertex {
    float u, v;
    uint32_t color; /* PSP ABGR. */
    float x, y, z; /* GU view coordinates, forward is negative Z. */
} RecvxBootRenderVertex;

/* Local-space geometry: the GE performs position/normal transforms and lighting. */
typedef struct RecvxBootGpuVertex {
    float u,v;
    unsigned int color; /* Ambient overflow, or unlit material color. */
    float nx,ny,nz,x,y,z;
} RecvxBootGpuVertex;
typedef struct RecvxBootGpuLight {
    float position[3], quadratic;
    unsigned int color, point;
} RecvxBootGpuLight;
typedef struct RecvxBootGpuState {
    float model[16];
    unsigned int diffuse, ambient, lit, light_count, vertex_ambient;
    unsigned int native_lighting,specular;
    float shininess;
    RecvxBootGpuLight lights[4];
} RecvxBootGpuState;

typedef enum RecvxBootRenderKind {
    RECVX_RENDER_TRIANGLES, RECVX_RENDER_LINES, RECVX_RENDER_HALF_FOG, RECVX_RENDER_SCREEN_TRIANGLES,
    RECVX_RENDER_SCREEN_OFFSET, RECVX_RENDER_SCREEN_HALF_BASE, RECVX_RENDER_SHADOW_START,
    RECVX_RENDER_SHADOW_ADD, RECVX_RENDER_SHADOW_SUBTRACT,
    RECVX_RENDER_SHADOW_COMPOSITE, RECVX_RENDER_SHADOW_END
} RecvxBootRenderKind;

/* Sampler bits 0/1 clamp U/V, bit 2 filters, bit 3 doubles textured RGB. */
#define RECVX_RENDER_COLOR_DOUBLE 8u
typedef struct RecvxBootRenderBatch {
    unsigned int first, count, attributes, sampler;
    int blend_source, blend_destination;
    const RecvxBootTexture *texture;
    RecvxBootRenderKind kind;
    unsigned int fog_color; /* PSP RGB for native depth fog or authored half fog. */
    unsigned int fog_enabled;
    float fog_near, fog_far; /* Positive view distances, captured at submission. */
    unsigned int gpu_strip; /* Native GE strip, including degenerate bridges. */
    unsigned int gpu, gpu_draw, gpu_material, gpu_mask; /* gpu: 1 normals, 2 prelit colors */
    RecvxBootGpuState gpu_state;
    int scissor[4]; /* PSP pixel bounds, snapshotted from Ninja user clipping. */
} RecvxBootRenderBatch;

typedef struct RecvxBootRenderFrame {
    const RecvxBootRenderVertex *vertices;
    const RecvxBootRenderBatch *batches;
    size_t vertex_count, batch_count;
    float projection[16];
    const RecvxBootGpuVertex *gpu_vertices;
    size_t gpu_vertex_count;
} RecvxBootRenderFrame;

typedef struct RecvxBootRenderStats {
    unsigned int models,triangles,position_transforms,normal_transforms,shaded_vertices,cache_hits;
    unsigned int material_preparations,face_normalizations;
    unsigned int chunk_join_skipped_vertices,gpu_matrix_us;
    unsigned int chunk_us; /* PSP time decoding, transforming and shading models. */
    unsigned int clip_inside,clip_rejected,clip_intersections;
    unsigned int gpu_vertices,gpu_lit_vertices,gpu_colored_vertices,gpu_fallback_triangles;
    unsigned int gpu_prepare_us,gpu_emit_us,gpu_vertex_us; /* Emit/overflow sampled every 32 triangles. */
    unsigned int gpu_vertex_calls,gpu_classify_us,gpu_overflow_us,gpu_output_us,gpu_model_cache_bypass;
    unsigned int gpu_bulk_attempts,gpu_bulk_triangles,gpu_bulk_us; /* Bulk time sampled every eight strips. */
    unsigned int gpu_strip_cache_hits,gpu_strip_cache_misses;
    unsigned int gpu_vfpu_distances,gpu_direct_vertices;
    unsigned int gpu_distance_batches,gpu_distance_refinements,gpu_distance_reuse;
    unsigned int gpu_group_vertices,gpu_group_constant_vertices,gpu_group_metric_skips,gpu_group_bounds_us;
    unsigned int gpu_group_range_queries,gpu_group_reused_vertices;
    unsigned int gpu_culled_groups,gpu_culled_vertices,gpu_culled_triangles;
    unsigned int gpu_light_cache_hits,gpu_light_cache_misses,gpu_light_cache_bypass;
    unsigned int gpu_classifications; /* Cached source-vertex light masks. */
    unsigned int gpu_fast_overflow_vertices,gpu_vfpu_bound_vertices;
    unsigned int gpu_native_light_vertices,gpu_native_omitted_lights,gpu_native_hdr_lights;
    unsigned int gpu_overflow_vertices; /* Unique VFPU ambient-overflow evaluations. */
    unsigned int gpu_fallback[8]; /* clip, specular, matrix, color, lights, normal, cutoff, vertex color */
} RecvxBootRenderStats;
/* Cumulative counters; presentation/ordering-table flushes do not reset them. */
RecvxBootRenderStats recvxGameBootRenderStats(void);
typedef struct RecvxBootGuStats { unsigned int build_us,wait_us,list_splits; } RecvxBootGuStats;
RecvxBootGuStats recvxGameBootGuStats(void);
/* Submit one unlit, double-sided local-space sprite triangle. */
void recvxGameBootRenderSpriteTriangle(const RecvxGameChunkVertex input[3], int textured, int trans);
void recvxGameBootRenderHalfFogTriangle(const RecvxGameChunkVertex input[3], unsigned int offset_argb, int trans);
void recvxGameBootRenderScreenTriangle(const RecvxGameChunkVertex input[3], int trans);
void recvxGameBootRenderHalfFogScreenTriangle(const RecvxGameChunkVertex input[3], unsigned int offset, int trans);
void recvxGameBootRenderLine(const RecvxGameChunkVertex input[2], int trans);
void recvxGameBootRenderShadowStart(void);
void recvxGameBootRenderShadowTriangle(const RecvxGameChunkVertex input[3], unsigned int pass);
void recvxGameBootRenderShadowComposite(const float bounds[4], unsigned int fog);
void recvxGameBootRenderShadowEnd(void);
void recvxGameBootRenderReset(void);
/* Defaults on for PSP, off for the host CPU reference renderer. */
void recvxGameBootRenderGpuEnable(int enabled);
void recvxGameBootRenderNativeLightingEnable(int enabled);
void recvxGameBootRenderGpuStripsEnable(int enabled);
void recvxGameBootRenderGpuNativeStripsEnable(int enabled);
void recvxGameBootRenderGpuStripCacheEnable(int enabled);
void recvxGameBootRenderResidentEnable(int enabled);
void recvxGameBootRenderDistanceBatchesEnable(int enabled);
void recvxGameBootRenderGroupBoundsEnable(int enabled);
void recvxGameBootRenderGroupCullEnable(int enabled);
void recvxGameBootRenderOverflowFastEnable(int enabled);
/* Preserve already queued users before changing the live texture's palette.
   The view shares pixel storage and lives until the submitted frame is consumed. */
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *texture);
void recvxGameBootRenderBeginFrame(void);
const RecvxBootRenderFrame *recvxGameBootRenderFrame(void);
void recvxGameBootRenderConsume(void);
int recvxGameBootRenderPending(void);
#endif
