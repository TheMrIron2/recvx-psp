#include <math.h>
#include <limits.h>
#include <float.h>
#include <stddef.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
#include <pspkernel.h>
#endif
#include "psp/game_boot_render.h"
#include "psp/game_boot_chunk.h"
#include "psp/game_boot_render_texture.h"
#include "psp/game_boot_fog.h"
#include "psp/game_boot_vector.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_lighting.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/ps2_NaView.h"

#define MAX_VERTICES 262144u
#define MAX_BATCHES 16384u
extern int recvxGameBootCaptureActive(void) __attribute__((weak));
extern void recvxGameBootDrawDiscard(void) __attribute__((weak));
extern unsigned int ulCnkCurrentDrawMode;
static RecvxBootRenderVertex *vertices;
static RecvxBootRenderBatch *batches;
static RecvxBootGpuVertex *gpu_vertices;
static size_t gpu_count,gpu_capacity;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
#define GPU_DEFAULT 1
#else
#define GPU_DEFAULT 0
#endif
static int gpu_enabled=GPU_DEFAULT,gpu_strips_enabled=1;
static int gpu_native_strips=GPU_DEFAULT,gpu_strip_cache_enabled=GPU_DEFAULT;
static void strip_cache_reset(void);
static int resident_enabled=1,resident_checked,resident_ok,distance_batches_enabled=1,group_bounds_enabled=1,group_cull_enabled=GPU_DEFAULT,overflow_fast_enabled=1,native_lighting_enabled=GPU_DEFAULT;
void recvxGameBootRenderGpuEnable(int enabled){gpu_enabled=!!enabled;}
void recvxGameBootRenderNativeLightingEnable(int enabled){native_lighting_enabled=!!enabled;strip_cache_reset();}
void recvxGameBootRenderGpuStripsEnable(int enabled){gpu_strips_enabled=!!enabled;}
void recvxGameBootRenderGpuNativeStripsEnable(int enabled){gpu_native_strips=!!enabled;}
void recvxGameBootRenderGpuStripCacheEnable(int enabled){gpu_strip_cache_enabled=!!enabled;}
void recvxGameBootRenderResidentEnable(int enabled){resident_enabled=!!enabled;}
void recvxGameBootRenderDistanceBatchesEnable(int enabled){distance_batches_enabled=!!enabled;}
void recvxGameBootRenderGroupBoundsEnable(int enabled){group_bounds_enabled=!!enabled;}
void recvxGameBootRenderGroupCullEnable(int enabled){group_cull_enabled=!!enabled;}
void recvxGameBootRenderOverflowFastEnable(int enabled){overflow_fast_enabled=!!enabled;}
static size_t vertex_capacity, batch_capacity, vertex_count, batch_count;
static int shadow_active;
static RecvxBootRenderStats render_stats;
RecvxBootRenderStats recvxGameBootRenderStats(void)
{RecvxBootRenderStats result=render_stats;result.chunk_join_skipped_vertices=recvxGameBootChunkJoinSkippedVertices();return result;}
#if defined(RECVX_GAME_BOOT_HOST_TEST)
extern void recvxGameBootRenderCullCheck(const float *,const float *,const RecvxGameChunkVertex *,unsigned int) __attribute__((weak));
#endif
static int user_scissor[4] = {0,0,480,272};

void njUserClipping(Int mode, NJS_POINT2 *points)
{
    if (mode == 0) {
        const int full[4] = {0,0,480,272};
        memcpy(user_scissor, full, sizeof(full));
        return;
    }
    if (mode != 2 || !points || !isfinite(points[0].x) || !isfinite(points[0].y) ||
        !isfinite(points[1].x) || !isfinite(points[1].y) || points[0].x < 0 || points[0].y < 0 ||
        points[1].x < points[0].x || points[1].y < points[0].y ||
        points[1].x > 640.0f/32 || points[1].y > 480.0f/32)
        recvxGameBootUnsupported("invalid original user clipping rectangle");
    /* Retail receives coordinates divided by32; the inclusive GS maximum
       expands by one32-pixel unit before conversion to PSP pixels. */
    user_scissor[0] = (int)(points[0].x*32*0.75f);
    user_scissor[1] = (int)(points[0].y*32*(272.0f/480));
    user_scissor[2] = (int)fminf(480, (points[1].x*32+33)*0.75f);
    user_scissor[3] = (int)fminf(272, (points[1].y*32+33)*(272.0f/480));
}
static RecvxBootTexture *texture_snapshots[256];
static size_t texture_snapshot_count;
static RecvxBootRenderFrame frame __attribute__((aligned(16)));

_Static_assert(sizeof(RecvxBootRenderVertex) == 24, "PSP GU vertex packing");
_Static_assert(sizeof(RecvxBootGpuVertex) == 36, "PSP GU normal vertex packing");

void recvxGameBootRenderBeginFrame(void)
{
    for (size_t i = 0; i < texture_snapshot_count; ++i) free(texture_snapshots[i]);
    texture_snapshot_count = 0;
    vertex_count = gpu_count = batch_count = 0; shadow_active = 0;
}
void recvxGameBootRenderConsume(void) { recvxGameBootRenderBeginFrame(); }
void Ps2ClearOT(void)
{
    /* Retail clears the ordering table and rewinds its primitive buffer.
       PSP equivalents are the pending 2D/3D queues, not loaded textures or
       render state. Present synchronizes GU before returning to the engine.
       Discard 2D users before freeing shared frame-owned palette snapshots. */
    if (recvxGameBootDrawDiscard) recvxGameBootDrawDiscard();
    recvxGameBootRenderBeginFrame();
}
int recvxGameBootRenderPending(void) { return vertex_count != 0 || gpu_count != 0; }
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *texture)
{
    if (!texture || !texture->source || !texture->pixels)
        recvxGameBootUnsupported("invalid original palette snapshot texture");
    if (texture_snapshot_count == sizeof(texture_snapshots) / sizeof(texture_snapshots[0]))
        recvxGameBootUnsupported("original palette snapshot capacity exceeded");
    RecvxBootTexture *snapshot = memalign(16, sizeof(*snapshot));
    if (!snapshot) recvxGameBootUnsupported("original palette snapshot allocation failed");
    *snapshot = *texture;
    texture_snapshots[texture_snapshot_count++] = snapshot;
    for (size_t i = 0; i < batch_count; ++i)
        if (batches[i].texture == texture) batches[i].texture = snapshot;
    return snapshot;
}
void recvxGameBootRenderReset(void)
{
    /* A fault may leave 2D overlays pointing into the same palette views.
       Discard those draws before freeing their frame-owned snapshots. */
    if (recvxGameBootDrawDiscard) recvxGameBootDrawDiscard();
    strip_cache_reset();gpu_strip_cache_enabled=GPU_DEFAULT;resident_enabled=1;resident_checked=resident_ok=0;distance_batches_enabled=1;group_bounds_enabled=1;group_cull_enabled=GPU_DEFAULT;overflow_fast_enabled=1;native_lighting_enabled=GPU_DEFAULT;
    memset(&render_stats,0,sizeof(render_stats));
    free(gpu_vertices);gpu_vertices=NULL;gpu_capacity=0;gpu_enabled=GPU_DEFAULT;gpu_strips_enabled=1;gpu_native_strips=GPU_DEFAULT;
    free(vertices); free(batches); vertices = NULL; batches = NULL;
    vertex_capacity = batch_capacity = 0;
    memset(&frame, 0, sizeof(frame));
    njUserClipping(0, NULL);
    recvxGameBootRenderBeginFrame();
    recvxGameBootRenderTextureReset(); recvxGameBootChunkReset();
}

static int owned(const void *pointer, size_t bytes)
{
    if (!sys || !freemem) return 0;
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return pointer && !(p & 3u) && p >= first && p <= end && bytes <= end - p;
}
static void checked_math(void)
{
    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());
}
static void reserve(size_t count)
{
    if (count > MAX_VERTICES || vertex_count > MAX_VERTICES - count || batch_count >= MAX_BATCHES)
        recvxGameBootUnsupported("original 3D draw queue exceeds capacity");
    if (vertex_count + count > vertex_capacity) {
        size_t capacity = vertex_capacity ? vertex_capacity * 2 : 4096;
        void *memory;
        if (capacity > MAX_VERTICES) capacity = MAX_VERTICES;
        memory = realloc(vertices, capacity * sizeof(*vertices));
        if (!memory) recvxGameBootUnsupported("original 3D vertex allocation failed");
        vertices = memory; vertex_capacity = capacity;
    }
    if (batch_count + 1 > batch_capacity) {
        size_t capacity = batch_capacity ? batch_capacity * 2 : 128;
        void *memory = realloc(batches, capacity * sizeof(*batches));
        if (!memory) recvxGameBootUnsupported("original 3D batch allocation failed");
        batches = memory; batch_capacity = capacity;
    }
}

static void capture_projection(void)
{
    float near, far, ax, ay, distance = recvxEngineOriginalViewProjectionDistance();
    recvxEngineOriginalViewClipPlanes(&near, &far);
    recvxEngineOriginalViewAspect(&ax, &ay);
    near = -near; far = -far;
    if (!isfinite(distance) || !(distance > 0) || !(near > 0) || !(far > near))
        recvxGameBootUnsupported("invalid original 3D projection");
    memset(frame.projection, 0, sizeof(frame.projection));
    int capture=recvxGameBootCaptureActive&&recvxGameBootCaptureActive();
    float width=capture?512.0f:640.0f,height=capture?_nj_screen_.h:480.0f;
    frame.projection[0] = 2.0f * distance * ax / width;
    frame.projection[5] = 2.0f * distance * ay / height;
    frame.projection[8] = (1728.0f + width*0.5f - fNaViwOffsetX) * 2 / width;
    frame.projection[9] = (fNaViwOffsetY - 1808.0f - height*0.5f) * 2 / height;
    frame.projection[10] = -(far + near) / (far - near);
    frame.projection[11] = -1;
    frame.projection[14] = -(2.0f * far * near) / (far - near);
}

const RecvxBootRenderFrame *recvxGameBootRenderFrame(void)
{
    frame.vertices = vertices; frame.batches = batches;
    frame.gpu_vertices=gpu_vertices;frame.gpu_vertex_count=gpu_count;
    frame.vertex_count = vertex_count; frame.batch_count = batch_count;
    return &frame;
}

static void append(const RecvxBootRenderVertex *output, unsigned int count, RecvxBootRenderKind kind,
                   unsigned int attributes, unsigned int sampler, int source, int destination,
                   const RecvxBootTexture *texture, unsigned int fog_color)
{
    /* GE fog blends after texturing, preserving the texture and alpha. The
       original reciprocal-depth table represents this near/far ramp with
       quantized interpolation; GE evaluates the ramp directly. Snapshot its
       parameters because events may change them before this queue is shown. */
    unsigned int fog_enabled = ulNaFogState && (kind == RECVX_RENDER_TRIANGLES || kind == RECVX_RENDER_LINES);
    float fog_near = fog_enabled ? fNaFogNear : 0;
    float fog_far = fog_enabled ? fNaFogFar : 0;
    if (fog_enabled) {
        if (!isfinite(fog_near) || !isfinite(fog_far) || fog_near < 0 || fog_far <= fog_near)
            recvxGameBootUnsupported("invalid original geometry fog range");
        fog_color = ulNaFogR | (ulNaFogG << 8) | (ulNaFogB << 16);
    }
    /* Event scripts run after model submission and may set the next camera.
       Keep this frame's projection with the view-space vertices it produced. */
    if (count && !vertex_count && !gpu_count) capture_projection();
    reserve(count);
    RecvxBootRenderBatch *batch = batch_count ? &batches[batch_count-1] : NULL;
    if (!count || (batch && batch->gpu) || kind == RECVX_RENDER_SHADOW_COMPOSITE || !batch || batch->count + count > 65535u || batch->kind != kind ||
        batch->fog_color != fog_color || batch->fog_enabled != fog_enabled ||
        batch->fog_near != fog_near || batch->fog_far != fog_far ||
        batch->texture != texture || batch->attributes != attributes || batch->sampler != sampler ||
        batch->blend_source != source || batch->blend_destination != destination ||
        memcmp(batch->scissor, user_scissor, sizeof(user_scissor))) {
        batch = &batches[batch_count++];
        *batch = (RecvxBootRenderBatch){.first=vertex_count, .attributes=attributes, .sampler=sampler,
            .blend_source=source, .blend_destination=destination, .texture=texture, .kind=kind, .fog_color=fog_color,
            .fog_enabled=fog_enabled, .fog_near=fog_near, .fog_far=fog_far};
        memcpy(batch->scissor, user_scissor, sizeof(user_scissor));
    }
    if (count) memcpy(vertices + vertex_count, output, count * sizeof(*output));
    vertex_count += count; batch->count += count;
}

static void local_vertices(const RecvxGameChunkVertex *input, RecvxBootRenderVertex *output, unsigned int count)
{
    NJS_MATRIX matrix;
    if (!input) recvxGameBootUnsupported("invalid original primitive vertices");
    njGetMatrix(&matrix); checked_math();
    for (unsigned int i = 0; i < count; ++i) {
        NJS_POINT3 local = {input[i].x, input[i].y, input[i].z}, view;
        njCalcPoint(&matrix, &local, &view); checked_math();
        if (!isfinite(view.x) || !isfinite(view.y) || !isfinite(view.z))
            recvxGameBootUnsupported("non-finite original primitive transform");
        unsigned int color = input[i].color;
        output[i] = (RecvxBootRenderVertex){0,0,
            (color & 0xff00ff00u) | ((color >> 16) & 255u) | ((color & 255u) << 16),
            view.x,-view.y,-view.z};
    }
}
static void screen_triangle(const RecvxGameChunkVertex input[3], int trans, int half, unsigned int offset)
{
    RecvxBootRenderVertex output[3];
    const RecvxGameBootRenderState *state = recvxGameBootRenderStateGet();
    const RecvxBootTexture *texture = state->selected_texture;
    float near, far;
    if (!input || (trans != 0 && trans != 1) || !texture || !texture->source ||
        !texture->pixels || texture->tile_count != 1)
        recvxGameBootUnsupported("invalid original screen sprite submission");
    recvxEngineOriginalViewClipPlanes(&near, &far);
    if (!isfinite(near) || !isfinite(far) || near >= 0 || far >= near)
        recvxGameBootUnsupported("invalid original screen sprite depth range");
    float a = (-65536.0f * near) / (far-near);
    float b = (65536.0f * near * far) / (far-near);
    for (unsigned int i=0; i<3; ++i) {
        if (!isfinite(input[i].x) || !isfinite(input[i].y) || !isfinite(input[i].z) ||
            !isfinite(input[i].u) || !isfinite(input[i].v) || input[i].z <= 0)
            recvxGameBootUnsupported("invalid original screen sprite vertex");
        float depth = a + b / (-1.0f / input[i].z);
        unsigned int color=input[i].color;
        output[i]=(RecvxBootRenderVertex){input[i].u*texture->width, input[i].v*texture->height,
            (color&0xff00ff00u)|((color>>16)&255u)|((color&255u)<<16),
            input[i].x*0.75f,input[i].y*(272.0f/480.0f),fmaxf(0,fminf(65534,depth))};
        if (!isfinite(output[i].x) || !isfinite(output[i].y) || !isfinite(output[i].u) || !isfinite(output[i].v))
            recvxGameBootUnsupported("screen sprite exceeds PSP coordinate range");
    }
    if (half) {
        RecvxBootRenderVertex constant[3];
        unsigned int rgb = ((offset & 0x00fefefeu) >> 1);
        rgb = ((rgb >> 16)&255u) | (rgb&0xff00u) | ((rgb&255u)<<16);
        for (unsigned int i=0;i<3;++i) {
            constant[i]=output[i];
            constant[i].color=(output[i].color&0xff000000u)|rgb;
            output[i].color=(output[i].color&0xff000000u)|((output[i].color&0x00fefefeu)>>1);
        }
        /* GE texture BLEND with equal vertex/environment RGB yields the
           authored offset while retaining texture*vertex alpha. Then add
           half the modulated base, weighted by the same source alpha. */
        append(constant,3,RECVX_RENDER_SCREEN_OFFSET,0x13u|(trans?8u:0),3u|(state->texture_filter?4u:0),8,6,texture,rgb);
        append(output,3,RECVX_RENDER_SCREEN_HALF_BASE,0x1bu,3u|(state->texture_filter?4u:0),trans?8:10,10,texture,0);
    } else append(output,3,RECVX_RENDER_SCREEN_TRIANGLES,0x13u|(trans?8u:0),3u|(state->texture_filter?4u:0),
        state->polygon_blend_source,state->polygon_blend_destination,texture,0);
}
void recvxGameBootRenderScreenTriangle(const RecvxGameChunkVertex input[3], int trans)
{ screen_triangle(input,trans,0,0); }
void recvxGameBootRenderHalfFogScreenTriangle(const RecvxGameChunkVertex input[3], unsigned int offset, int trans)
{ screen_triangle(input,trans,1,offset); }

void recvxGameBootRenderLine(const RecvxGameChunkVertex input[2], int trans)
{
    RecvxBootRenderVertex output[2];
    const RecvxGameBootRenderState *state = recvxGameBootRenderStateGet();
    if (trans != 0 && trans != 1) recvxGameBootUnsupported("invalid original line transparency");
    local_vertices(input, output, 2);
    append(output, 2, RECVX_RENDER_LINES, 0x13u | (trans ? 8u : 0), 0,
        state->polygon_blend_source, state->polygon_blend_destination, NULL, 0);
}
void recvxGameBootRenderShadowStart(void)
{
    if (shadow_active) recvxGameBootUnsupported("nested original modifier draw");
    shadow_active = 1;
    append(NULL,0,RECVX_RENDER_SHADOW_START,0,0,0,0,NULL,0);
}
void recvxGameBootRenderShadowTriangle(const RecvxGameChunkVertex input[3], unsigned int pass)
{
    RecvxBootRenderVertex output[3];
    if (!shadow_active || pass > 1) recvxGameBootUnsupported("invalid original modifier pass");
    local_vertices(input, output, 3);
    for (unsigned int i = 0; i < 3; ++i) output[i].color = 0xff202020u;
    append(output,3,pass ? RECVX_RENDER_SHADOW_SUBTRACT : RECVX_RENDER_SHADOW_ADD,0,0,0,0,NULL,0);
}
void recvxGameBootRenderShadowComposite(const float bounds[4], unsigned int fog)
{
    if (!shadow_active || !bounds || fog > 255)
        recvxGameBootUnsupported("invalid original modifier composite");
    for (unsigned int i = 0; i < 4; ++i)
        if (!isfinite(bounds[i]) || bounds[i] < 0 || bounds[i] > (i & 1 ? 480 : 640))
            recvxGameBootUnsupported("invalid original modifier bounds");
    if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) return;
    unsigned int tint = 2u * ((fog * 3u) / 16u);
    unsigned int color = 0xff000000u | (tint * 0x10101u);
    RecvxBootRenderVertex output[2];
    for (unsigned int i = 0; i < 2; ++i) {
        float bx = bounds[2*i], by = bounds[2*i+1];
        float x = floorf(bx*16.0f)/16.0f * 0.75f;
        float y = floorf(by*16.0f)/16.0f * (272.0f/480.0f);
        output[i] = (RecvxBootRenderVertex){(floorf(bx)+0.5f)*0.75f,
            (floorf(by)+0.5f)*(272.0f/480.0f),color,x,y,0};
    }
    append(output,2,RECVX_RENDER_SHADOW_COMPOSITE,0,0,0,0,NULL,0);
}
void recvxGameBootRenderShadowEnd(void)
{
    if (!shadow_active) recvxGameBootUnsupported("unmatched original modifier end");
    append(NULL,0,RECVX_RENDER_SHADOW_END,0,0,0,0,NULL,0);
    shadow_active = 0;
}

static float dot(const NJS_VECTOR *a, const NJS_VECTOR *b)
{ return a->x*b->x + a->y*b->y + a->z*b->z; }
static int normalize(NJS_VECTOR *v)
{
    float length = sqrtf(dot(v, v));
    if (!isfinite(length) || length < 1e-20f) return 0;
    v->x /= length; v->y /= length; v->z /= length;
    return 1;
}
static float channel(unsigned int color, unsigned int shift)
{ return ((color >> shift) & 255u) / 255.0f; }
static unsigned int byte(float value)
{
    if (!isfinite(value)) recvxGameBootUnsupported("non-finite original model lighting");
    /* Allegrex otherwise calls libm twice for every shaded color channel. */
    if (value <= 0) return 0;
    if (value >= 1) return 255;
    return (unsigned int)(value * 255.0f + 0.5f);
}

static float positive(float value) { return value > 0 ? value : 0; }
static float light_unit(float value)
{
    /* Preserve fmaxf(0,fminf(1,x)), including its NaN -> one behavior. */
    if (!(value < 1)) return 1;
    return positive(value);
}

typedef struct VertexCache {
    unsigned int draw,index,material,color;
    unsigned int clip_code;
    NJS_POINT3 position;
    NJS_VECTOR normal;
    int normal_ready,normal_valid;
} VertexCache;
/* Bounded scratch space, invalidated per draw: animated/replaced streams never
   share results. Flat-shaded faces deliberately bypass the color cache. */
static VertexCache vertex_cache[256];
static unsigned int draw_serial;

typedef struct GpuOverflowLight {
    NJS_POINT3 vector,local;
    float rgb[3],near2,far2,gain;
    int point;
} GpuOverflowLight;
typedef struct GpuOverflowCache {
    unsigned int draw,index,material,color,classification_material;
    unsigned char light_mask,boundary_mask,input_checked,input_valid;
    struct LightResponse *response;
} __attribute__((aligned(32))) GpuOverflowCache;
_Static_assert(sizeof(GpuOverflowCache)==32,"GPU vertex cache must remain compact");
static GpuOverflowCache gpu_overflow_cache[256];

typedef struct DrawContext {
    NJS_MATRIX matrix __attribute__((aligned(16)));
    NJS_MATRIX normals __attribute__((aligned(16)));
    int sprite, textured, trans, half_fog;
    int custom_clip, reversed, vfpu_resident,distance_batch;
    NJS_POINT3 distance_origin;
    float distance_coefficients[16] __attribute__((aligned(16)));
    float distance_bound[4],metric_bound;
    int extra_native[8],distance_valid;
    unsigned int group_known,group_mask,group_extra_mask,group_extra_inside,group_color;
    int group_culled,cull_planes_ready;
    float group_cull_planes[6][4],group_cull_error[3][4];
    unsigned int distance_material,resident_material;
    unsigned int draw,material_serial,strip_ordinal,strip_generation,light_generation,strip_cache_store,light_cache_reusable;
    const NJS_CNK_MODEL *model;
    int persistent_allowed,model_cache_volatile,model_cache_bypassed;
    RecvxGameChunkMaterial material;
    int material_valid,lights_prepared;
    int gpu_prepared,gpu_compatible,gpu_normal_fix;
    float gpu_normal_matrix[9];
    unsigned int gpu_reason,gpu_extra_count;
    GpuOverflowLight gpu_extra[8];
    float gpu_ambient[3];
    int profile_sample,bulk_profile;
    unsigned int bulk_output_started;
    float gpu_metric[6];
    NJS_POINT3 gpu_local_lights[4];
    float gpu_far2[4];
    RecvxBootGpuState gpu_state;
    const RecvxBootTexture *material_texture;
    RecvxGameBootSampler material_sampler;
    NJS_VECTOR light_direction;
    int light_direction_valid;
    NJS_POINT3 light_positions[8];
    float clip_planes[6][4];
    unsigned int fog_color;
    const RecvxGameBootRenderState *state;
    const RecvxEngineOriginalLightingState *lighting;
} DrawContext;
_Static_assert(_Alignof(DrawContext)>=16 && offsetof(DrawContext,matrix)%16==0 &&
    offsetof(DrawContext,normals)%16==0,"model VFPU matrices must be aligned");

/* Cache prepared strip data, never pointers to live animated vertices. Exact
   byte comparisons retain deformation/UV/light changes without dirty hooks. */
#define STRIP_CACHE_SLOTS 2048u
#define STRIP_KEY_SLOTS 256u
#define STRIP_CACHE_BYTES (2u*1024u*1024u)
typedef struct StripLightKey {
    NJS_MATRIX matrix,normals;
    unsigned int diffuse,lit,light_count,point_mask;
    GpuOverflowLight extra[8];
    float normal_matrix[9],metric[6],ambient[3],far2[4];
    NJS_POINT3 local[4];
    unsigned int extra_count,normal_fix;
} StripLightKey;
typedef struct StripKeyEntry {
    const NJS_CNK_MODEL *model;
    unsigned int material,generation;
    StripLightKey key;
} StripKeyEntry;
typedef struct StripCacheEntry {
    const NJS_CNK_MODEL *model;
    unsigned int ordinal,generation,count,triangles,mask;
    size_t capacity;
    void *memory;
} StripCacheEntry;
static StripKeyEntry strip_keys[STRIP_KEY_SLOTS],light_keys[STRIP_KEY_SLOTS];
typedef struct ModelCachePolicy {
    const NJS_CNK_MODEL *model;
    NJS_MATRIX matrix;
    unsigned int changes;
} ModelCachePolicy;
static ModelCachePolicy model_cache_policy[256];
#define LIGHT_RESPONSE_SLOTS 4096u
typedef struct LightResponse {
    const NJS_CNK_MODEL *model;
    unsigned int index,generation,metric_ready,response_ready;
    float source[6],metric[8],distance[8],dot[8];
    NJS_POINT3 position;NJS_VECTOR normal;
    unsigned int position_ready,normal_ready,normal_valid;
    unsigned int classification_ready,light_mask,boundary_mask;
} LightResponse;
static LightResponse light_responses[LIGHT_RESPONSE_SLOTS];
static StripCacheEntry strip_cache[STRIP_CACHE_SLOTS];
static size_t strip_cache_bytes;
static unsigned int strip_generation;
static unsigned int strip_hash(const void *model,unsigned int ordinal)
{
    uintptr_t p=(uintptr_t)model;
    return (unsigned int)((p>>4)^(p>>13)) * 2654435761u + ordinal*2246822519u;
}
static void strip_cache_reset(void)
{
    for(unsigned int i=0;i<STRIP_CACHE_SLOTS;++i)free(strip_cache[i].memory);
    memset(strip_cache,0,sizeof(strip_cache));memset(strip_keys,0,sizeof(strip_keys));
    memset(light_keys,0,sizeof(light_keys));memset(light_responses,0,sizeof(light_responses));
    memset(model_cache_policy,0,sizeof(model_cache_policy));
    strip_cache_bytes=0;strip_generation=0;
}
static int persistent_cache_allowed(const DrawContext *c)
{
    ModelCachePolicy *policy=&model_cache_policy[strip_hash(c->model,0)&255u];
    if(policy->model!=c->model){
        policy->model=c->model;policy->changes=0;memcpy(policy->matrix,c->matrix,sizeof(policy->matrix));return 1;
    }
    if(!memcmp(policy->matrix,c->matrix,sizeof(policy->matrix)))policy->changes=0;
    else{
        memcpy(policy->matrix,c->matrix,sizeof(policy->matrix));
        if(policy->changes<3)++policy->changes;
    }
    if(policy->changes>=3)return 0;
    return 1;
}

static unsigned int strip_light_generation(DrawContext *c,int response)
{
    StripLightKey key={0};
    /* GE matrices/light colors are queued live and do not affect CPU-packed
       vertices. Key only actual CPU dependencies, retaining camera-independent
       local geometry when all its lighting runs on the GE. */
    key.diffuse=c->gpu_state.diffuse;key.lit=c->gpu_state.lit;
    key.light_count=c->gpu_state.light_count;
    key.extra_count=c->gpu_extra_count;key.normal_fix=c->gpu_normal_fix;
    if(c->gpu_normal_fix)memcpy(key.normal_matrix,c->gpu_normal_matrix,sizeof(key.normal_matrix));
    for(unsigned int i=0;i<c->gpu_state.light_count;++i)if(c->gpu_state.lights[i].point){
        key.point_mask|=1u<<i;key.local[i]=c->gpu_local_lights[i];key.far2[i]=c->gpu_far2[i];
    }
    if(key.point_mask)memcpy(key.metric,c->gpu_metric,sizeof(key.metric));
    if(c->gpu_extra_count){
        memcpy(key.matrix,c->matrix,sizeof(key.matrix));memcpy(key.normals,c->normals,sizeof(key.normals));
        memcpy(key.extra,c->gpu_extra,c->gpu_extra_count*sizeof(*key.extra));
        memcpy(key.ambient,c->gpu_ambient,sizeof(key.ambient));
        memcpy(key.metric,c->gpu_metric,sizeof(key.metric));
    }
    if(response){
        /* Distances and Lambert response depend on geometry/light placement,
           while flickering RGB, gain and ambient are evaluated each draw. */
        key.diffuse=0;memset(key.ambient,0,sizeof(key.ambient));
        for(unsigned int i=0;i<key.extra_count;++i){
            memset(key.extra[i].rgb,0,sizeof(key.extra[i].rgb));key.extra[i].gain=0;
        }
    }
    StripKeyEntry *table=response?light_keys:strip_keys;
    StripKeyEntry *entry=&table[strip_hash(c->model,c->material_serial)&(STRIP_KEY_SLOTS-1)];
    int matches=entry->model==c->model&&entry->material==c->material_serial&&!memcmp(&entry->key,&key,sizeof(key));
    /* Constantly flickering colors cannot reuse final packed colors. Avoid
       rewriting those snapshots until a shader has repeated unchanged. */
    if(!response)c->strip_cache_store=!entry->model||matches;
    else c->light_cache_reusable=!entry->model||matches;
    if(!matches){
        if(++strip_generation==0){strip_cache_reset();++strip_generation;}
        *entry=(StripKeyEntry){.model=c->model,.material=c->material_serial,.generation=strip_generation,.key=key};
    }
    return entry->generation;
}
static StripCacheEntry *strip_cache_entry(DrawContext *c,unsigned int ordinal)
{
    return &strip_cache[strip_hash(c->model,ordinal)&(STRIP_CACHE_SLOTS-1)];
}
static void strip_cache_store(StripCacheEntry *entry,const DrawContext *c,unsigned int ordinal,
    const RecvxGameChunkVertex *input,const RecvxBootGpuVertex *packed,unsigned int count,unsigned int triangles,unsigned int mask)
{
    size_t bytes=count*(sizeof(*input)+sizeof(*packed));
    if(entry->capacity<bytes){
        if(strip_cache_bytes-entry->capacity+bytes>STRIP_CACHE_BYTES)return;
        void *memory=realloc(entry->memory,bytes);if(!memory)return;
        strip_cache_bytes+=bytes-entry->capacity;entry->memory=memory;entry->capacity=bytes;
    }
    memcpy(entry->memory,input,count*sizeof(*input));
    memcpy((unsigned char *)entry->memory+count*sizeof(*input),packed,count*sizeof(*packed));
    entry->model=c->model;entry->ordinal=ordinal;entry->generation=c->strip_generation;
    entry->count=count;entry->triangles=triangles;entry->mask=mask;
}

typedef struct ClipVertex {
    float u,v,x,y,z,color[4];
} ClipVertex;

static float clip_distance(const ClipVertex *vertex,const float plane[4])
{
    /* Queued GU vertices have the original Ninja Y/Z signs reversed. */
    float result=plane[0]*vertex->x-plane[1]*vertex->y-plane[2]*vertex->z+plane[3];
    if(!isfinite(result))recvxGameBootUnsupported("original custom clip distance exceeds range");
    return result;
}
static unsigned int clip_outcode(const NJS_POINT3 *position,const float planes[6][4])
{
    unsigned int result=0;
    for(unsigned int i=0;i<6;++i){
        float distance=planes[i][0]*position->x+planes[i][1]*position->y+
            planes[i][2]*position->z+planes[i][3];
        if(!isfinite(distance))recvxGameBootUnsupported("original custom clip distance exceeds range");
        if(distance<0)result|=1u<<i;
    }
    return result;
}
static ClipVertex clip_intersection(const ClipVertex *a,const ClipVertex *b,float da,float db)
{
    float aa=fabsf(da),bb=fabsf(db),ratio,t;
    /* Opposite signs guarantee a crossing. Avoid overflowing da-db for large
       finite coordinates, and preserve UV/color interpolation in view space. */
    if(aa>bb){ratio=bb/aa;t=1/(1+ratio);}
    else{ratio=aa/bb;t=ratio/(1+ratio);}
    ClipVertex result={.u=(1-t)*a->u+t*b->u,.v=(1-t)*a->v+t*b->v,
        .x=(1-t)*a->x+t*b->x,.y=(1-t)*a->y+t*b->y,.z=(1-t)*a->z+t*b->z};
    for(unsigned int i=0;i<4;++i)result.color[i]=(1-t)*a->color[i]+t*b->color[i];
    if(!isfinite(result.u)||!isfinite(result.v)||!isfinite(result.x)||!isfinite(result.y)||!isfinite(result.z))
        recvxGameBootUnsupported("original custom clip interpolation exceeds range");
    return result;
}
static unsigned int clip_triangle(const RecvxBootRenderVertex input[3],const float planes[6][4],
                                  RecvxBootRenderVertex output[21])
{
    /* Intersecting a triangle with six convex half spaces produces at most
       nine polygon vertices, or seven fan triangles. Keep two spare slots. */
    ClipVertex polygon[2][12];
    unsigned int count=3,current=0;
    for(unsigned int i=0;i<3;++i){
        const RecvxBootRenderVertex *v=&input[i];
        polygon[0][i]=(ClipVertex){.u=v->u,.v=v->v,.x=v->x,.y=v->y,.z=v->z};
        for(unsigned int c=0;c<4;++c)polygon[0][i].color[c]=(v->color>>(c*8))&255u;
    }
    for(unsigned int plane=0;plane<6&&count;++plane){
        unsigned int next=current^1u,n=0;
        ClipVertex previous=polygon[current][count-1];
        float previous_distance=clip_distance(&previous,planes[plane]);
        for(unsigned int i=0;i<count;++i){
            ClipVertex vertex=polygon[current][i];
            float distance=clip_distance(&vertex,planes[plane]);
            if((previous_distance<0&&distance>0)||(previous_distance>0&&distance<0)){
                if(n>=12)recvxGameBootUnsupported("original custom clip polygon exceeds capacity");
                polygon[next][n++]=clip_intersection(&previous,&vertex,previous_distance,distance);
            }
            if(distance>=0){
                if(n>=12)recvxGameBootUnsupported("original custom clip polygon exceeds capacity");
                polygon[next][n++]=vertex;
            }
            previous=vertex;previous_distance=distance;
        }
        current=next;count=n;
    }
    if(count<3)return 0;
    if(count>9)recvxGameBootUnsupported("original custom clip fan exceeds capacity");
    unsigned int emitted=0;
    for(unsigned int i=2;i<count;++i){
        const ClipVertex *v[3]={&polygon[current][0],&polygon[current][i-1],&polygon[current][i]};
        NJS_VECTOR a={v[1]->x-v[0]->x,v[1]->y-v[0]->y,v[1]->z-v[0]->z};
        NJS_VECTOR b={v[2]->x-v[0]->x,v[2]->y-v[0]->y,v[2]->z-v[0]->z};
        NJS_VECTOR area={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        if(area.x==0&&area.y==0&&area.z==0)continue;
        for(unsigned int j=0;j<3;++j){
            unsigned int color=0;
            for(unsigned int c=0;c<4;++c)
                color|=(unsigned int)(fmaxf(0,fminf(255,v[j]->color[c]))+0.5f)<<(8*c);
            output[emitted++]=(RecvxBootRenderVertex){v[j]->u,v[j]->v,color,v[j]->x,v[j]->y,v[j]->z};
        }
    }
    return emitted;
}

static void prepare_lights(DrawContext *context)
{
    if(context->lights_prepared)return;
    int simple=context->state->current_draw_mode>=2;
    int multi=context->state->current_draw_mode&1;
    const RecvxEngineOriginalLightingState *lighting=context->lighting;
    const RecvxEngineOriginalLightValue *lights=simple?lighting->simple:lighting->easy;
    if(multi){
        int count=simple?lighting->simple_count:lighting->easy_count;
        if(count<0||count>7)recvxGameBootUnsupported("invalid original model light count");
        for(int i=1;i<=count;++i)if(lights[i].enabled){
            if(i==1){
                NJS_VECTOR direction=lights[0].direction;
                njCalcVector(cam.mtx,&direction,&context->light_direction);checked_math();
            }else{
                NJS_POINT3 point=lights[i].position;
                njCalcPoint(cam.mtx,&point,&context->light_positions[i]);checked_math();
                if(!isfinite(lights[i].near_range)||!isfinite(lights[i].far_range)||lights[i].near_range<0||lights[i].far_range<0)
                    recvxGameBootUnsupported("invalid original model point light range");
            }
        }
    }else context->light_direction=simple?lighting->simple_direction:lighting->easy_direction;
    context->light_direction.x=-context->light_direction.x;
    context->light_direction.y=-context->light_direction.y;
    context->light_direction.z=-context->light_direction.z;
    context->light_direction_valid=normalize(&context->light_direction);
    context->lights_prepared=1;
}

/* Retail textured color table: Easy uses an overbright ramp; Simple single
   uses power 17; Simple multi excludes ambient from the overbright ramp.
   ps2_Vu1Strip.c scales specular by 31 and diffuse by 128. */
static int specular_enabled(const DrawContext *context,const RecvxGameChunkMaterial *material,unsigned int attributes)
{
    return !context->sprite&&material->texture!=UINT_MAX&&!(attributes&3u)&&
        !(context->state->control_flags&0x10u)&&
        ((context->state->current_draw_mode&1u)||(material->specular&0xffffffu));
}
static float specular_weight(float illumination,int powered)
{
    float x=light_unit(powered?illumination:illumination-1);
    if(!powered)return x;
    float x2=x*x,x4=x2*x2,x8=x4*x4,x16=x8*x8;
    return x16*x;
}

static unsigned int shade(DrawContext *context, const RecvxGameChunkMaterial *material,
                           const RecvxGameChunkVertex *vertex, const NJS_POINT3 *position,
                           const NJS_VECTOR *normal, unsigned int attributes)
{
    int simple = context->state->current_draw_mode >= 2;
    int multi = context->state->current_draw_mode & 1;
    const RecvxEngineOriginalLightingState *lighting = context->lighting;
    const RecvxEngineOriginalLightValue *lights = simple ? lighting->simple : lighting->easy;
    const float *ambient = simple ? context->state->simple_ambient : context->state->easy_ambient;
    float diffuse[3] = {channel(material->diffuse, 16), channel(material->diffuse, 8), channel(material->diffuse, 0)};
    if (material->texture != UINT_MAX && !simple)
        diffuse[0] = diffuse[1] = diffuse[2] = 1; /* Retail Easy textured path uses NaCnkDefaultOne. */
    float illumination[3] = {0, 0, 0},ambient_value[3]={0,0,0};
    int specular=specular_enabled(context,material,attributes);
    float alpha = channel(material->diffuse, 24) * channel(vertex->color, 24);
    if (context->state->control_flags & 0x10u) {
        diffuse[0] = context->state->constant_material.r;
        diffuse[1] = context->state->constant_material.g;
        diffuse[2] = context->state->constant_material.b;
        alpha = context->state->constant_material.a * channel(vertex->color, 24);
    }
    if (attributes & 1u) illumination[0] = illumination[1] = illumination[2] = 1;
    else {
        prepare_lights(context);
        if (!(attributes & 4u)) for (unsigned int c = 0; c < 3; ++c)
            ambient_value[c] = illumination[c] = multi ? ambient[c] * channel(material->ambient, 16 - 8*c) :
                (simple ? lighting->simple_ambient : lighting->easy_ambient);
        if (multi) {
            int count = simple ? lighting->simple_count : lighting->easy_count;
            if (count < 0 || count > 7) recvxGameBootUnsupported("invalid original model light count");
            for (int i = 1; i <= count; ++i) if (lights[i].enabled) {
                NJS_VECTOR direction;
                float attenuation = 1;
                if (i == 1) {
                    direction=context->light_direction;
                } else {
                    NJS_POINT3 point=context->light_positions[i];
                    direction = (NJS_VECTOR){point.x-position->x, point.y-position->y, point.z-position->z};
                    float near = lights[i].near_range, far = lights[i].far_range;
                    float distance_squared = dot(&direction, &direction);
                    attenuation = distance_squared >= far*far ? 0 :
                        distance_squared <= near*near ? 1 : near*near / distance_squared;
                    /* A point outside this light contributes exactly zero. */
                    if (!attenuation) continue;
                }
                checked_math();
                if (i==1?context->light_direction_valid:normalize(&direction)) {
                    float intensity = positive(dot(normal, &direction)) * attenuation;
                    illumination[0] += lights[i].color.x * intensity;
                    illumination[1] += lights[i].color.y * intensity;
                    illumination[2] += lights[i].color.z * intensity;
                }
            }
        } else {
            NJS_VECTOR direction=context->light_direction;
            if(context->light_direction_valid){
                float intensity = positive(dot(normal, &direction)) *
                    (simple ? lighting->simple_intensity : lighting->easy_intensity);
                NJS_POINT3 color = simple ? lighting->simple_color : lighting->easy_color;
                illumination[0] += color.x * intensity;
                illumination[1] += color.y * intensity;
                illumination[2] += color.z * intensity;
            }
        }
    }
    unsigned int result = byte(alpha) << 24;
    for (unsigned int c = 0; c < 3; ++c){
        float value=diffuse[c]*illumination[c];
        if(specular){
            float incident=illumination[c]-(simple&&multi?ambient_value[c]:0);
            float highlight=specular_weight(incident,simple&&!multi);
            float tint=multi?1:channel(material->specular,16-8*c);
            value=diffuse[c]*light_unit(illumination[c])+highlight*tint*(31.0f/128.0f);
            /* Preserve PS2 overbright modulation without another geometry pass.
               GE doubles RGB after texturing; alpha remains at its full scale. */
            value*=0.5f;
        }
        result |= byte(value*channel(vertex->color,16-8*c)) << (16-8*c);
    }
    return result;
}

/* GE normals use the model matrix. Nonuniform models receive corrected
   local normals; original specular and flat face colors remain available. */
static int gpu_color(float r,float g,float b,float a,unsigned int *color)
{
    /* Quantization can put an authored unit color a few ULPs above one. */
    const float epsilon=1e-6f;
    if(!isfinite(r)||!isfinite(g)||!isfinite(b)||!isfinite(a)||
       r < -epsilon||g < -epsilon||b < -epsilon||a < -epsilon||
       r>1+epsilon||g>1+epsilon||b>1+epsilon||a>1+epsilon)return 0;
    *color=byte(r)|(byte(g)<<8)|(byte(b)<<16)|(byte(a)<<24);return 1;
}
static int gpu_nonnegative(float value)
{return isfinite(value)&&value>=-1e-6f;}
static void gpu_range_sample(unsigned int stage,const float values[4])
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    static unsigned int reported;
    if(!(reported&(1u<<stage))){
        reported|=1u<<stage;
        recvxGameBootLog("gpu light range fallback: stage=%u rgba=%g,%g,%g,%g",stage,
            values[0],values[1],values[2],values[3]);
    }
#else
    (void)stage;(void)values;
#endif
}
/* GE has four light slots and unit RGB registers. Select source lights once
   per material/model, preserving a directional slot and the strongest point
   lights at the model origin. Spare slots retain overbright light energy;
   point attenuation scaling preserves its inverse-square tail. Near-range
   saturation and hard PS2 far cutoffs are adapted to native GE behavior. */
static void prepare_native_lights(DrawContext *c,const RecvxGameChunkMaterial *m,unsigned int attributes,
    const float diffuse[4],const float ambient[3],int simple,int multi)
{
    RecvxBootGpuState *g=&c->gpu_state;
    const RecvxEngineOriginalLightValue *lights=simple?c->lighting->simple:c->lighting->easy;
    int count=multi?(simple?c->lighting->simple_count:c->lighting->easy_count):1;
    float rgb[8][3]={{0}},peak[8]={0},score[8]={0};unsigned int candidates=0,chosen=0;
    for(int i=1;i<=count;++i){
        if(multi&&!lights[i].enabled)continue;
        if(i==1&&!c->light_direction_valid)continue;
        if(i>1&&(!lights[i].near_range||!lights[i].far_range))continue;
        NJS_POINT3 color=multi?lights[i].color:(simple?c->lighting->simple_color:c->lighting->easy_color);
        float intensity=multi?1:(simple?c->lighting->simple_intensity:c->lighting->easy_intensity);
        float channels[3]={color.x,color.y,color.z};
        for(unsigned int k=0;k<3;++k){
            float value=diffuse[k]*channels[k]*intensity;
            if(!isfinite(value))recvxGameBootUnsupported("non-finite native model light");
            rgb[i][k]=ambient[k]>=1?0:positive(value);
            if(rgb[i][k]>peak[i])peak[i]=rgb[i][k];
        }
        if(peak[i]<=0)continue;
        candidates|=1u<<i;score[i]=peak[i];
        if(i>1){
            float x=c->light_positions[i].x-c->matrix[12],y=c->light_positions[i].y-c->matrix[13],z=c->light_positions[i].z-c->matrix[14];
            float d2=x*x+y*y+z*z,n2=lights[i].near_range*lights[i].near_range;
            if(d2>n2)score[i]*=n2/d2;
            if(!isfinite(score[i]))score[i]=0;
        }
    }
    if(candidates&2u)chosen=2u;
    unsigned int selected=!!chosen;
    while(selected<4){
        int best=0;
        for(int i=1;i<=count;++i)if((candidates&(1u<<i))&&!(chosen&(1u<<i))&&(!best||score[i]>score[best]))best=i;
        if(!best)break;
        chosen|=1u<<best;++selected;
    }
    for(int i=1;i<=count;++i)if((candidates&(1u<<i))&&!(chosen&(1u<<i)))++render_stats.gpu_native_omitted_lights;
    unsigned int future=selected;
    for(int i=1;i<=count;++i){
        if(!(chosen&(1u<<i)))continue;
        --future;
        unsigned int budget=4-g->light_count-future;
        unsigned int copies=peak[i]<=1?1:peak[i]<=2?2:peak[i]<=3?3:4;
        if(copies>budget)copies=budget;
        NJS_POINT3 point=i==1?c->light_direction:c->light_positions[i];
        if(!isfinite(point.x)||!isfinite(point.y)||!isfinite(point.z))recvxGameBootUnsupported("non-finite native light position");
        float gain=i>1&&peak[i]>copies?peak[i]/copies:1;
        float quadratic=i>1?(1/lights[i].near_range)/lights[i].near_range/gain:0;
        if(i>1&&(!isfinite(quadratic)||quadratic<=0))recvxGameBootUnsupported("invalid native point attenuation");
        if(peak[i]>1)++render_stats.gpu_native_hdr_lights;
        for(unsigned int copy=0;copy<copies;++copy){
            RecvxBootGpuLight *l=&g->lights[g->light_count++];
            l->position[0]=point.x;l->position[1]=-point.y;l->position[2]=-point.z;
            l->point=i>1;l->quadratic=quadratic;
            float mapped[3];
            for(unsigned int k=0;k<3;++k)mapped[k]=i>1?rgb[i][k]/(gain>1?peak[i]:copies):positive(rgb[i][k]-copy);
            gpu_color(light_unit(mapped[0]),light_unit(mapped[1]),light_unit(mapped[2]),1,&l->color);
        }
    }
    if(specular_enabled(c,m,attributes)){
        float tint[3];for(unsigned int k=0;k<3;++k)tint[k]=(multi?1:channel(m->specular,16-8*k))*(31.0f/128);
        gpu_color(tint[0],tint[1],tint[2],1,&g->specular);
        g->shininess=(float)(m->specular>>24);if(g->shininess<1)g->shininess=1;if(g->shininess>128)g->shininess=128;
    }
    g->native_lighting=1;g->vertex_ambient=0;c->gpu_extra_count=0;c->gpu_compatible=1;
}

static void prepare_gpu(DrawContext *c,const RecvxGameChunkMaterial *m,unsigned int attributes)
{
    c->gpu_prepared=1;c->gpu_compatible=0;c->gpu_extra_count=0;
    c->gpu_reason=1;if(!native_lighting_enabled&&specular_enabled(c,m,attributes))return;
    c->gpu_reason=2;
    RecvxBootGpuState *g=&c->gpu_state;
    memset(g,0,sizeof(*g));
    for(unsigned int i=0;i<16;++i){
        if(!isfinite(c->matrix[i]))return;
        g->model[i]=c->matrix[i]*((i%4==1||i%4==2)?-1:1);
    }
    if(c->matrix[3]!=0||c->matrix[7]!=0||c->matrix[11]!=0||c->matrix[15]!=1)return;
    int simple=c->state->current_draw_mode>=2,multi=c->state->current_draw_mode&1;
    float d[4]={channel(m->diffuse,16),channel(m->diffuse,8),channel(m->diffuse,0),channel(m->diffuse,24)};
    if(m->texture!=UINT_MAX&&!simple)d[0]=d[1]=d[2]=1;
    if(c->state->control_flags&0x10u){d[0]=c->state->constant_material.r;d[1]=c->state->constant_material.g;d[2]=c->state->constant_material.b;d[3]=c->state->constant_material.a;}
    c->gpu_reason=3;
    for(unsigned int i=0;i<4;++i)if(!isfinite(d[i])){gpu_range_sample(0,d);return;}
    if(attributes&1u){
        gpu_color(light_unit(d[0]),light_unit(d[1]),light_unit(d[2]),light_unit(d[3]),&g->diffuse);
        g->native_lighting=native_lighting_enabled;c->gpu_compatible=1;return;
    }
    for(unsigned int i=0;i<3;++i)if(!gpu_nonnegative(d[i])){gpu_range_sample(0,d);return;}
    /* Fold material RGB into ambient/light RGB before GE quantization. This
       preserves dim materials under brighter-than-unit lights and lets GE
       accumulate HDR contributions before its final vertex-color clamp. */
    gpu_color(1,1,1,light_unit(d[3]),&g->diffuse);
    c->gpu_reason=5;if(!native_lighting_enabled&&(attributes&0x20u))return;
    c->gpu_reason=2;
    float lengths[3]={0};
    for(unsigned int i=0;i<3;++i)for(unsigned int j=0;j<3;++j)lengths[i]+=c->matrix[i*4+j]*c->matrix[i*4+j];
    float scale=lengths[0];
    c->gpu_normal_fix=!isfinite(scale)||scale<1e-12f||fabsf(lengths[1]-scale)>scale*1e-3f||fabsf(lengths[2]-scale)>scale*1e-3f;
    for(unsigned int i=0;i<3;++i)for(unsigned int j=i+1;j<3;++j){
        float v=0;for(unsigned int k=0;k<3;++k)v+=c->matrix[i*4+k]*c->matrix[j*4+k];
        if(fabsf(v)>scale*1e-3f)c->gpu_normal_fix=1;
    }
    if(c->gpu_normal_fix){
        /* GE uses M for normals. Feed M^-1 * M^-T * n so that GE produces
           the original inverse-transpose normal even for nonuniform scale. */
        for(unsigned int i=0;i<3;++i)for(unsigned int j=0;j<3;++j){
            float value=0;
            for(unsigned int k=0;k<3;++k)value+=c->normals[i*4+k]*c->normals[j*4+k];
            if(!isfinite(value))return;
            c->gpu_normal_matrix[j*3+i]=value;
        }
    }
    /* Retail quarter-wave sine sampling leaves small scale discrepancies
       in otherwise rigid rotations. Bound them below 8-bit light precision;
       nonuniform scale and shear use a corrected input normal. Point cutoff tests below use the exact matrix metric. */
    for(unsigned int i=0;i<3;++i)c->gpu_metric[i]=lengths[i];
    unsigned int metric=3;
    for(unsigned int i=0;i<3;++i)for(unsigned int j=i+1;j<3;++j){
        float v=0;for(unsigned int k=0;k<3;++k)v+=c->matrix[i*4+k]*c->matrix[j*4+k];
        c->gpu_metric[metric++]=2*v;
    }
    g->lit=1;
    const float *ambient=simple?c->state->simple_ambient:c->state->easy_ambient;
    float a[3]={0};
    if(!(attributes&4u))for(unsigned int i=0;i<3;++i)a[i]=multi?ambient[i]*channel(m->ambient,16-8*i):(simple?c->lighting->simple_ambient:c->lighting->easy_ambient);
    c->gpu_reason=3;
    for(unsigned int i=0;i<3;++i){
        a[i]*=d[i];
        if(!gpu_nonnegative(a[i])){float sample[4]={a[0],a[1],a[2],1};gpu_range_sample(1,sample);return;}
    }
    gpu_color(light_unit(a[0]),light_unit(a[1]),light_unit(a[2]),1,&g->ambient);
    memcpy(c->gpu_ambient,a,sizeof(a));
    prepare_lights(c);
    if(native_lighting_enabled){prepare_native_lights(c,m,attributes,d,a,simple,multi);return;}
    const RecvxEngineOriginalLightValue *lights=simple?c->lighting->simple:c->lighting->easy;
    int count=multi?(simple?c->lighting->simple_count:c->lighting->easy_count):1;
    for(int i=1;i<=count;++i){
        if(multi&&!lights[i].enabled)continue;
        if(i==1&&!c->light_direction_valid)continue;
        if(i>1&&(!lights[i].near_range||!lights[i].far_range))continue;
        NJS_POINT3 color=multi?lights[i].color:(simple?c->lighting->simple_color:c->lighting->easy_color);
        float intensity=multi?1:(simple?c->lighting->simple_intensity:c->lighting->easy_intensity);
        float rgb[4]={d[0]*color.x*intensity,d[1]*color.y*intensity,d[2]*color.z*intensity,1};
        c->gpu_reason=3;
        float peak=0;
        for(unsigned int k=0;k<3;++k){
            if(!gpu_nonnegative(rgb[k])){gpu_range_sample(2,rgb);return;}
            /* Positive illumination cannot lower an already saturated channel. */
            if(a[k]>=1)rgb[k]=0;
            if(rgb[k]>peak)peak=rgb[k];
        }
        if(peak<=0)continue;
        /* GE colors are unit range. Repeated identical lights add their
           components before the final clamp, retaining retail HDR intensity.
           Whole unit components are exact; only the remainder is quantized. */
        unsigned int copies=peak<=1.000001f?1:peak<=2.000001f?2:peak<=3.000001f?3:4;
        unsigned int available=4-g->light_count,future=0;
        /* Keep a GE slot for each remaining source light before duplicating
           HDR components. A bright directional light must not displace all
           point lights and make their expensive attenuation run on the CPU. */
        if(multi)for(int j=i+1;j<=count;++j)
            if(lights[j].enabled&&(j==1||(lights[j].near_range&&lights[j].far_range)))++future;
        unsigned int budget=available>future?available-future:(available?1:0);
        if(copies>budget)copies=budget;
        c->gpu_reason=4;
        RecvxBootGpuLight light={0};
        NJS_POINT3 point=i==1?c->light_direction:c->light_positions[i],local_point={0};
        if(!isfinite(point.x)||!isfinite(point.y)||!isfinite(point.z))return;
        light.position[0]=point.x;light.position[1]=-point.y;light.position[2]=-point.z;
        float far2=0;
        if(i>1){
            light.point=1;light.quadratic=1/(lights[i].near_range*lights[i].near_range);
            if(!isfinite(light.quadratic)||light.quadratic<=0)return;
            far2=lights[i].far_range*lights[i].far_range;
            float delta[3]={point.x-c->matrix[12],point.y-c->matrix[13],point.z-c->matrix[14]};
            float local[3]={0};
            for(unsigned int j=0;j<3;++j)for(unsigned int k=0;k<3;++k)local[j]+=c->normals[j*4+k]*delta[k];
            local_point=(NJS_POINT3){local[0],local[1],local[2]};
        }
        /* Outside the enlarged near sphere a brighter point light fits in
           one GE slot by scaling inverse-square attenuation instead of RGB.
           Only vertices within that sphere need a saturation correction. */
        float gain=1;
        if(i>1&&peak>1.000001f&&copies){
            gain=peak;copies=1;light.quadratic/=gain;
        }
        for(unsigned int copy=0;copy<copies;++copy){
            unsigned int n=g->light_count++;
            g->lights[n]=light;
            gpu_color(light_unit(rgb[0]/gain-copy),light_unit(rgb[1]/gain-copy),light_unit(rgb[2]/gain-copy),1,&g->lights[n].color);
            c->gpu_local_lights[n]=local_point;c->gpu_far2[n]=far2;
        }
        float remainder[3];int overflow=0;
        for(unsigned int k=0;k<3;++k){remainder[k]=gain>1?rgb[k]/gain:positive(rgb[k]-copies);if(remainder[k]>1e-6f)overflow=1;}
        if(overflow){
            GpuOverflowLight *extra=&c->gpu_extra[c->gpu_extra_count++];
            *extra=(GpuOverflowLight){.vector=point,.local=local_point,.point=i>1,.gain=gain,
                .near2=i>1?lights[i].near_range*lights[i].near_range:0,.far2=far2};
            memcpy(extra->rgb,remainder,sizeof(remainder));
        }
    }
    g->vertex_ambient=c->gpu_extra_count!=0;
    c->gpu_compatible=1;
}
static void gpu_model_state(RecvxBootRenderBatch *b,const DrawContext *c,int local)
{
    if(local)for(unsigned int i=0;i<16;++i)b->gpu_state.model[i]=c->matrix[i]*((i%4==1||i%4==2)?-1:1);
    if(c->custom_clip){
        /* Original custom half spaces are an axis-aligned view cone. Scissor
           the projected rectangle; GE handles near/far and triangle clipping. */
        float cx=240*(1-frame.projection[8]),cy=136*(1+frame.projection[9]);
        float rx=240*fabsf(frame.projection[0]/c->clip_planes[0][0]);
        float ry=136*fabsf(frame.projection[5]/c->clip_planes[2][1]);
        float bounds[4]={cx-rx,cy-ry,cx+rx,cy+ry};
        for(unsigned int i=0;i<4;++i){
            float limit=i%2?272:480;
            int edge=(int)ceilf(fmaxf(0,fminf(limit,bounds[i]))-0.5f);
            if(i<2){if(edge>b->scissor[i])b->scissor[i]=edge;}
            else if(edge<b->scissor[i])b->scissor[i]=edge;
        }
    }
}
static float gpu_distance(DrawContext *c,const RecvxGameChunkVertex *p,const NJS_POINT3 *point)
{
    const float *q=c->gpu_metric;
    if(c->vfpu_resident){
        ++render_stats.gpu_vfpu_distances;
        return recvxRenderResidentDistance(q,&p->x,&point->x);
    }
    float x=p->x-point->x,y=p->y-point->y,z=p->z-point->z;
    return q[0]*x*x+q[1]*y*y+q[2]*z*z+q[3]*x*y+q[4]*x*z+q[5]*y*z;
}
static void prepare_distance_batch(DrawContext *c,const RecvxGameChunkVertex *input)
{
    c->distance_batch=0;
    if(c->gpu_state.native_lighting)return;
    if(!distance_batches_enabled||!c->gpu_state.lit)return;
    if(c->distance_material==c->material_serial){
        c->distance_batch=c->distance_valid;return;
    }
    c->distance_material=c->material_serial;c->distance_valid=0;
    c->distance_origin=(NJS_POINT3){input->x,input->y,input->z};
    const float *q=c->gpu_metric;
    c->metric_bound=0;for(unsigned int k=0;k<6;++k)c->metric_bound+=fabsf(q[k]);
    int points=0;memset(c->distance_coefficients,0,sizeof(c->distance_coefficients));
    for(unsigned int i=0;i<c->gpu_state.light_count;++i){
        if(!c->gpu_state.lights[i].point)continue;
        ++points;
        float x=c->gpu_local_lights[i].x-input->x,y=c->gpu_local_lights[i].y-input->y,z=c->gpu_local_lights[i].z-input->z;
        float v[3]={q[0]*x+q[3]*0.5f*y+q[4]*0.5f*z,
            q[3]*0.5f*x+q[1]*y+q[5]*0.5f*z,q[4]*0.5f*x+q[5]*0.5f*y+q[2]*z};
        for(unsigned int k=0;k<3;++k)c->distance_coefficients[k*4+i]=-2*v[k];
        c->distance_coefficients[12+i]=x*v[0]+y*v[1]+z*v[2];
        c->distance_bound[i]=c->metric_bound*(x*x+y*y+z*z)+1;
        if(!isfinite(c->distance_bound[i]))return;
        for(unsigned int k=0;k<4;++k)if(!isfinite(c->distance_coefficients[k*4+i]))return;
    }
    if(!points)return;
    for(unsigned int i=0;i<c->gpu_extra_count;++i){
        c->extra_native[i]=-1;
        if(c->gpu_extra[i].point)for(unsigned int j=0;j<c->gpu_state.light_count;++j)
            if(c->gpu_state.lights[j].point&&!memcmp(&c->gpu_extra[i].local,&c->gpu_local_lights[j],sizeof(NJS_POINT3))){c->extra_native[i]=(int)j;break;}
    }
    recvxRenderLoadLightDistances(c->distance_coefficients);c->distance_batch=c->distance_valid=1;
}

/* Bound actual decoded vertices, including deformed geometry. Squared
   metric intervals avoid square roots and keep uncertain groups conservative. */
static void group_light_interval(DrawContext *c,const float *center,const float *extent,
    float radius2,float coordinate_bound,const NJS_POINT3 *light,float range[2])
{
    if(c->vfpu_resident)recvxRenderResidentBoxDistance(c->gpu_metric,center,&light->x,extent,range);
    else{
        const float *q=c->gpu_metric;
        float x=center[0]-light->x,y=center[1]-light->y,z=center[2]-light->z;
        float g[3]={q[0]*x+q[3]*0.5f*y+q[4]*0.5f*z,
            q[3]*0.5f*x+q[1]*y+q[5]*0.5f*z,q[4]*0.5f*x+q[5]*0.5f*y+q[2]*z};
        float d=x*g[0]+y*g[1]+z*g[2];
        float width=2*(fabsf(g[0])*extent[0]+fabsf(g[1])*extent[1]+fabsf(g[2])*extent[2]);
        range[0]=d-width;range[1]=d+width;
    }
    float light_bound=light->x*light->x+light->y*light->y+light->z*light->z;
    float error=512*FLT_EPSILON*c->metric_bound*(coordinate_bound+light_bound+1);
    range[0]-=error;range[1]+=radius2+error;
    ++render_stats.gpu_group_range_queries;
}
static int group_light_side(const float range[2],float limit)
{
    if(!isfinite(range[0])||!isfinite(range[1])||!isfinite(limit)||limit<=0)return 0;
    float margin=(512*FLT_EPSILON+1e-5f)*limit;
    if(range[1]<limit-margin)return 1;
    if(range[0]>limit+margin)return -1;
    return 0;
}
/* Test actual decoded geometry against the projection used by GE. An
   enlarged viewport and outward roundoff margin retain edge-crossing groups;
   GE still clips all geometry that can contribute a screen pixel. */
static int group_outside_view(DrawContext *c,const float center[3],const float extent[3],
    const float lo[3],const float hi[3])
{
    if(!c->cull_planes_ready){
        if(!gpu_count&&!vertex_count)capture_projection();
        const float *p=frame.projection,*m=c->matrix;
        float wx=1+4.0f/480,wy=1+4.0f/272;
        for(unsigned int k=0;k<4;++k){
            float x=m[k*4],y=m[k*4+1],z=m[k*4+2];
            c->group_cull_planes[0][k]=(wx-p[8])*z+p[0]*x;
            c->group_cull_planes[1][k]=(wx+p[8])*z-p[0]*x;
            c->group_cull_planes[2][k]=(wy-p[9])*z-p[5]*y;
            c->group_cull_planes[3][k]=(wy+p[9])*z+p[5]*y;
            c->group_cull_planes[4][k]=(1-p[10])*z+(k==3?p[14]:0);
            c->group_cull_planes[5][k]=(1+p[10])*z-(k==3?p[14]:0);
            c->group_cull_error[0][k]=(wx+fabsf(p[8]))*fabsf(z)+fabsf(p[0]*x);
            c->group_cull_error[1][k]=(wy+fabsf(p[9]))*fabsf(z)+fabsf(p[5]*y);
            c->group_cull_error[2][k]=(1+fabsf(p[10]))*fabsf(z)+(k==3?fabsf(p[14]):0);
        }
        c->cull_planes_ready=1;
    }
    for(unsigned int plane=0;plane<6;++plane){
        const float *p=c->group_cull_planes[plane];
        const float *error=c->group_cull_error[plane/2];
        float upper=p[3],magnitude=error[3];
        for(unsigned int k=0;k<3;++k){
            upper+=p[k]*center[k]+fabsf(p[k])*extent[k];
            magnitude+=error[k]*(fabsf(lo[k])+fabsf(hi[k]));
        }
        /* GE matrices are uploaded as float24. Bound their quantization as
           well as CPU plane/support arithmetic, including depth cancellation. */
        float margin=(64.0f/65536)*(magnitude+1);
        if(isfinite(upper)&&isfinite(margin)&&upper < -margin)return 1;
    }
    return 0;
}
static void prepare_group_bounds(DrawContext *c,const RecvxGameChunkVertex *input,unsigned int count)
{
    c->group_known=0;c->group_culled=0;c->group_extra_inside=0;c->group_extra_mask=(1u<<c->gpu_extra_count)-1;
    if(c->gpu_state.native_lighting&&!group_cull_enabled){
        c->group_known=1;c->group_mask=(1u<<c->gpu_state.light_count)-1;c->group_extra_mask=0;return;
    }
    if((!group_bounds_enabled&&!group_cull_enabled)||!count)return;
    if(!c->gpu_state.lit&&!group_cull_enabled){c->group_known=1;c->group_mask=0;return;}
    float lo[3]={input->x,input->y,input->z},hi[3]={input->x,input->y,input->z};
    if(c->vfpu_resident){
        recvxRenderResidentBounds(&input->x,count,sizeof(*input),lo,hi);
        render_stats.gpu_vfpu_bound_vertices+=count;
    }else for(unsigned int i=1;i<count;++i){
        float p[3]={input[i].x,input[i].y,input[i].z};
        for(unsigned int k=0;k<3;++k){if(p[k]<lo[k])lo[k]=p[k];if(p[k]>hi[k])hi[k]=p[k];}
    }
    float e[3],center[3],coordinate_bound=0;
    for(unsigned int k=0;k<3;++k){
        center[k]=lo[k]*0.5f+hi[k]*0.5f;
        e[k]=(hi[k]-lo[k])*0.5f;
        coordinate_bound+=lo[k]*lo[k]+hi[k]*hi[k];
    }
    if(group_cull_enabled&&group_outside_view(c,center,e,lo,hi)){c->group_culled=1;return;}
    if(c->gpu_state.native_lighting){c->group_known=1;c->group_mask=(1u<<c->gpu_state.light_count)-1;c->group_extra_mask=0;return;}
    if(!group_bounds_enabled)return;
    if(!c->gpu_state.lit){c->group_known=1;c->group_mask=0;return;}
    const float *q=c->gpu_metric;
    c->metric_bound=0;for(unsigned int k=0;k<6;++k)c->metric_bound+=fabsf(q[k]);
    float radius2=q[0]*e[0]*e[0]+q[1]*e[1]*e[1]+q[2]*e[2]*e[2]+
        fabsf(q[3]*e[0]*e[1])+fabsf(q[4]*e[0]*e[2])+fabsf(q[5]*e[1]*e[2]);
    if(!isfinite(radius2)||radius2<0||!isfinite(coordinate_bound))return;
    float native_ranges[4][2];
    c->group_known=1;c->group_mask=0;
    for(unsigned int i=0;i<c->gpu_state.light_count;++i){
        int side=1;
        if(c->gpu_state.lights[i].point){
            group_light_interval(c,center,e,radius2,coordinate_bound,&c->gpu_local_lights[i],native_ranges[i]);
            side=group_light_side(native_ranges[i],c->gpu_far2[i]);
        }
        if(!side)c->group_known=0;
        if(side>0)c->group_mask|=1u<<i;
    }
    for(unsigned int i=0;i<c->gpu_extra_count;++i){
        const GpuOverflowLight *l=&c->gpu_extra[i];
        if(!l->point)continue;
        /* The vertex path's expanded rejection thresholds must also be
           exceeded here; a skipped contribution is exactly zero. */
        float limit=l->far2*1.00001f;
        if(l->gain>1&&l->near2*l->gain*1.00001f<limit)limit=l->near2*l->gain*1.00001f;
        const float *range=NULL;float extra_range[2];
        for(unsigned int j=0;j<c->gpu_state.light_count;++j)
            if(c->gpu_state.lights[j].point&&!memcmp(&l->local,&c->gpu_local_lights[j],sizeof(l->local))){range=native_ranges[j];break;}
        if(!range){group_light_interval(c,center,e,radius2,coordinate_bound,&l->local,extra_range);range=extra_range;}
        int side=group_light_side(range,limit);
        if(side<0)c->group_extra_mask&=~(1u<<i);
        if(side>0)c->group_extra_inside|=1u<<i;
    }
    c->group_color=byte(c->gpu_ambient[0])|(byte(c->gpu_ambient[1])<<8)|
        (byte(c->gpu_ambient[2])<<16)|(c->gpu_state.diffuse&0xff000000u);
}

static LightResponse *light_response(DrawContext *c,const RecvxGameChunkVertex *p)
{
    LightResponse *response=&light_responses[strip_hash(c->model,p->source_index)&(LIGHT_RESPONSE_SLOTS-1)];
    if(response->model!=c->model||response->index!=p->source_index||response->generation!=c->light_generation||
       memcmp(response->source,&p->x,sizeof(response->source))){
        response->model=c->model;response->index=p->source_index;response->generation=c->light_generation;
        response->metric_ready=response->response_ready=response->classification_ready=0;
        response->position_ready=response->normal_ready=response->normal_valid=0;
        memcpy(response->source,&p->x,sizeof(response->source));
        ++render_stats.gpu_light_cache_misses;
    }else ++render_stats.gpu_light_cache_hits;
    return response;
}

/* Moving models have no reusable per-light response. Evaluate their spill
   directly, retaining only the shared point/normal and final source color.
   This avoids the generic eight-light metric/dot/distance arrays and flags. */
static __attribute__((noinline)) int gpu_overflow_uncached(DrawContext *c,const RecvxGameChunkVertex *p,
    unsigned int *color,GpuOverflowCache *cache,const float *native_distances,const float *native_errors)
{
    float value[3]={c->gpu_ambient[0],c->gpu_ambient[1],c->gpu_ambient[2]};
    NJS_POINT3 position;NJS_VECTOR normal;
    int position_ready=0,normal_ready=0;
    for(unsigned int i=0;i<c->gpu_extra_count;++i){
        const GpuOverflowLight *l=&c->gpu_extra[i];unsigned int bit=1u<<i;
        if(c->group_known&&!(c->group_extra_mask&bit))continue;
        if(l->point&&!(c->group_known&&(c->group_extra_inside&bit))){
            int native=c->distance_batch?c->extra_native[i]:-1;
            float near_limit=l->near2*l->gain*1.00001f,d2;
            if(native_distances&&native>=0&&(l->gain<=1||fabsf(native_distances[native]-near_limit)>native_errors[native])){
                d2=native_distances[native];++render_stats.gpu_distance_reuse;
            }else d2=gpu_distance(c,p,&l->local);
            if(d2>=l->far2*1.00001f||(l->gain>1&&d2>=near_limit))continue;
        }
        if(l->point&&c->group_known&&(c->group_extra_inside&bit))++render_stats.gpu_group_metric_skips;
        NJS_VECTOR direction=l->vector;float d2=0;
        if(l->point){
            if(!position_ready){
                if(c->vfpu_resident)recvxRenderResidentPoint(c->matrix,&p->x,&position.x);
                else recvxFaceTransformAligned(c->matrix,&p->x,&position.x);
                position_ready=1;++render_stats.position_transforms;
            }
            direction.x-=position.x;direction.y-=position.y;direction.z-=position.z;
            d2=recvxVectorUnit3(&direction.x);
            if(!isfinite(d2)||d2<1e-40f||!isfinite(direction.x)||!isfinite(direction.y)||!isfinite(direction.z)||d2>=l->far2)continue;
        }
        if(!normal_ready){
            float squared=c->vfpu_resident?recvxRenderResidentNormal(c->normals,&p->nx,&normal.x):
                recvxSkinUnitDirectionAligned(c->normals,&p->nx,&normal.x);
            ++render_stats.normal_transforms;
            if(!isfinite(squared)||squared<1e-40f||!isfinite(normal.x)||!isfinite(normal.y)||!isfinite(normal.z))return 0;
            normal_ready=1;
        }
        float attenuation=1;
        if(l->point){
            attenuation=d2>=l->far2?0:d2<=l->near2?1:l->near2/d2;
            if(l->gain>1)attenuation=positive(l->gain*attenuation-1);
        }
        float intensity=positive(dot(&normal,&direction))*attenuation;
        for(unsigned int k=0;k<3;++k)value[k]+=intensity*l->rgb[k];
    }
    cache->color=byte(value[0])|(byte(value[1])<<8)|(byte(value[2])<<16)|(c->gpu_state.diffuse&0xff000000u);
    cache->material=c->material_serial;*color=cache->color;
    ++render_stats.gpu_overflow_vertices;++render_stats.gpu_fast_overflow_vertices;
    return 1;
}

/* Only excess light energy runs here. GE retains model transforms, normal
   transforms, the main lights, final color clamping, clipping and texturing. */
static __attribute__((noinline)) int gpu_overflow_color(DrawContext *c,const RecvxGameChunkVertex *p,unsigned int *color,GpuOverflowCache *cache,const float *native_distances,const float *native_errors)
{
    if(cache->material!=c->material_serial){
        LightResponse temporary,*response=cache->response;
        if(!response){
            response=&temporary;
            temporary.metric_ready=temporary.response_ready=0;
            temporary.position_ready=temporary.normal_ready=temporary.normal_valid=0;
        }
        float value[3];memcpy(value,c->gpu_ambient,sizeof(value));
        for(unsigned int i=0;i<c->gpu_extra_count;++i){
            const GpuOverflowLight *l=&c->gpu_extra[i];
            unsigned int bit=1u<<i;
            if(c->group_known&&!(c->group_extra_mask&bit))continue;
            if(l->point&&!(c->group_known&&(c->group_extra_inside&bit))){
                if(!(response->metric_ready&bit)){
                    int native=c->distance_batch?c->extra_native[i]:-1;
                    float near_limit=l->near2*l->gain*1.00001f;
                    if(native_distances&&native>=0&&
                       (l->gain<=1||fabsf(native_distances[native]-near_limit)>native_errors[native])){
                        response->metric[i]=native_distances[native];++render_stats.gpu_distance_reuse;
                    }else response->metric[i]=gpu_distance(c,p,&l->local);
                    response->metric_ready|=bit;
                }
                float d2=response->metric[i];
                if(d2>=l->far2*1.00001f||(l->gain>1&&d2>=l->near2*l->gain*1.00001f))continue;
            }
            if(l->point&&c->group_known&&(c->group_extra_inside&bit))++render_stats.gpu_group_metric_skips;
            if(!(response->response_ready&bit)){
                NJS_VECTOR direction=l->vector;
                float d2=0;
                if(l->point){
                    if(!response->position_ready){
                        if(c->vfpu_resident)recvxRenderResidentPoint(c->matrix,&p->x,&response->position.x);
                        else recvxFaceTransformAligned(c->matrix,&p->x,&response->position.x);
                        response->position_ready=1;++render_stats.position_transforms;
                    }
                    direction.x-=response->position.x;direction.y-=response->position.y;direction.z-=response->position.z;
                    d2=recvxVectorUnit3(&direction.x);
                    response->distance[i]=d2;
                    if(!isfinite(d2)||d2<1e-40f||!isfinite(direction.x)||!isfinite(direction.y)||!isfinite(direction.z)){
                        response->dot[i]=0;response->response_ready|=bit;continue;
                    }
                    if(d2>=l->far2){response->dot[i]=0;response->response_ready|=bit;continue;}
                }
                if(!response->normal_ready){
                    float squared=c->vfpu_resident?recvxRenderResidentNormal(c->normals,&p->nx,&response->normal.x):
                        recvxSkinUnitDirectionAligned(c->normals,&p->nx,&response->normal.x);
                    response->normal_valid=isfinite(squared)&&squared>=1e-40f&&isfinite(response->normal.x)&&isfinite(response->normal.y)&&isfinite(response->normal.z);
                    response->normal_ready=1;++render_stats.normal_transforms;
                }
                if(!response->normal_valid)return 0;
                response->dot[i]=positive(dot(&response->normal,&direction));
                response->response_ready|=bit;
            }
            float attenuation=1;
            if(l->point){
                float d2=response->distance[i];
                attenuation=d2>=l->far2?0:d2<=l->near2?1:l->near2/d2;
                if(l->gain>1)attenuation=positive(l->gain*attenuation-1);
            }
            float intensity=response->dot[i]*attenuation;
            for(unsigned int k=0;k<3;++k)value[k]+=intensity*l->rgb[k];
        }
        cache->color=byte(value[0])|(byte(value[1])<<8)|(byte(value[2])<<16)|(c->gpu_state.diffuse&0xff000000u);
        cache->material=c->material_serial;++render_stats.gpu_overflow_vertices;
    }
    *color=cache->color;return 1;
}

static inline int gpu_overflow_eval(DrawContext *c,const RecvxGameChunkVertex *p,unsigned int *color,
    GpuOverflowCache *cache,const float *native_distances,const float *native_errors)
{
    if(overflow_fast_enabled&&!cache->response)return gpu_overflow_uncached(c,p,color,cache,native_distances,native_errors);
    return gpu_overflow_color(c,p,color,cache,native_distances,native_errors);
}

static void ensure_gpu_prepared(DrawContext *c,const RecvxGameChunkMaterial *m,unsigned int attributes)
{
    if(!c->gpu_prepared){
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
        unsigned int prepared=sceKernelGetSystemTimeLow();
#endif
        prepare_gpu(c,m,attributes);
        if(c->gpu_state.native_lighting){
            c->persistent_allowed=0;c->strip_generation=c->light_generation=c->strip_cache_store=c->light_cache_reusable=0;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
            render_stats.gpu_prepare_us+=sceKernelGetSystemTimeLow()-prepared;
#endif
            return;
        }
        int cpu_depends_on_model=c->gpu_extra_count||c->gpu_normal_fix;
        for(unsigned int i=0;i<c->gpu_state.light_count;++i)cpu_depends_on_model|=c->gpu_state.lights[i].point;
        c->persistent_allowed=!(c->model_cache_volatile&&cpu_depends_on_model);
        if(!c->persistent_allowed){
            c->strip_generation=c->light_generation=c->strip_cache_store=c->light_cache_reusable=0;
            if(!c->model_cache_bypassed){++render_stats.gpu_model_cache_bypass;c->model_cache_bypassed=1;}
        }
        if(gpu_strip_cache_enabled&&c->persistent_allowed&&c->gpu_compatible){
            c->strip_generation=strip_light_generation(c,0);
            c->light_generation=strip_light_generation(c,1);
        }
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
        render_stats.gpu_prepare_us+=sceKernelGetSystemTimeLow()-prepared;
#endif
    }
}

static int gpu_vertex(DrawContext *c,const RecvxGameChunkVertex *p,unsigned int *mask,
                      unsigned int *boundary,unsigned int *color)
{
    int result=1;
    ++render_stats.gpu_vertex_calls;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    int profile_vertex=(render_stats.gpu_vertex_calls*2654435761u>>26)==0;
    unsigned int profile_started=profile_vertex?sceKernelGetSystemTimeLow():0,spill_started=0;
#endif
    *color=c->gpu_state.diffuse;
    float native_distances[4] __attribute__((aligned(16))),native_errors[4];
    int native_ready=0;
    GpuOverflowCache *cache=&gpu_overflow_cache[p->source_index&255u];
    if(cache->draw!=c->draw||cache->index!=p->source_index)
        *cache=(GpuOverflowCache){.draw=c->draw,.index=p->source_index};
    if(c->gpu_state.lit){
        if(!cache->input_checked){
            float n2=p->nx*p->nx+p->ny*p->ny+p->nz*p->nz;
            cache->input_valid=isfinite(n2)&&n2>=1e-20f;cache->input_checked=1;
        }
        c->gpu_reason=5;if(!cache->input_valid){result=0;goto profile_vertex_done;}
    }
    if(cache->classification_material!=c->material_serial){
        LightResponse *response=gpu_strip_cache_enabled&&c->light_cache_reusable?light_response(c,p):NULL;
        if(gpu_strip_cache_enabled&&!c->light_cache_reusable)++render_stats.gpu_light_cache_bypass;
        cache->response=response;
        unsigned int inside=0,boundary=0;
        if(response&&response->classification_ready){
            inside=response->light_mask;boundary=response->boundary_mask;
        }else{
            if(c->distance_batch){
                float norm=recvxRenderResidentDistances(c->gpu_metric,c->distance_coefficients,&p->x,&c->distance_origin.x,native_distances);
                float shared_bound=norm*c->metric_bound;
                for(unsigned int i=0;i<c->gpu_state.light_count;++i)if(c->gpu_state.lights[i].point){
                    native_errors[i]=256*FLT_EPSILON*(shared_bound+c->distance_bound[i]);
                    float d2=native_distances[i],far=c->gpu_far2[i];
                    /* Refine cancellation and hard-cutoff neighborhoods using
                       the original stable translated-distance calculation. */
                    if(!isfinite(d2)||d2<native_errors[i]||fabsf(d2-far)<=native_errors[i]+far*1e-5f){
                        native_distances[i]=gpu_distance(c,p,&c->gpu_local_lights[i]);++render_stats.gpu_distance_refinements;
                    }
                }
                native_ready=1;++render_stats.gpu_distance_batches;
            }
            for(unsigned int i=0;i<c->gpu_state.light_count;++i){
                if(!c->gpu_state.lights[i].point){inside|=1u<<i;continue;}
                NJS_POINT3 point=c->gpu_local_lights[i];
                float d2=native_ready?native_distances[i]:gpu_distance(c,p,&point);
                if(fabsf(d2-c->gpu_far2[i])<=c->gpu_far2[i]*1e-5f)boundary|=1u<<i;
                if(d2<c->gpu_far2[i])inside|=1u<<i;
            }
            ++render_stats.gpu_classifications;
            if(response){response->classification_ready=1;response->light_mask=inside;response->boundary_mask=boundary;}
        }
        cache->classification_material=c->material_serial;
        cache->light_mask=inside;cache->boundary_mask=boundary;
    }
    /* Copy classification before another source index aliases this slot. */
    *mask=cache->light_mask;*boundary=cache->boundary_mask;
    if(c->gpu_state.vertex_ambient){
        if(cache->material==c->material_serial)*color=cache->color;
        else{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
            if(profile_vertex)spill_started=sceKernelGetSystemTimeLow();
#endif
            if(!gpu_overflow_eval(c,p,color,cache,native_ready?native_distances:NULL,native_ready?native_errors:NULL)){
                c->gpu_reason=5;result=0;goto profile_vertex_done;
            }
        }
    }
profile_vertex_done:
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    if(profile_vertex){
        unsigned int ended=sceKernelGetSystemTimeLow();
        unsigned int overflow=spill_started?ended-spill_started:0;
        render_stats.gpu_classify_us+=(ended-profile_started-overflow)*64u;
        render_stats.gpu_overflow_us+=overflow*64u;
    }
#endif
    return result;
}
/* Uniform light masks need only the spill color. Keep this separate from
   the full classifier: no native-distance arrays, mask cache or classifier
   stack frame are needed, and the source cache is probed just once. */
static int gpu_group_overflow_vertex(DrawContext *c,const RecvxGameChunkVertex *p,unsigned int *color)
{
    int result=1;
    ++render_stats.gpu_vertex_calls;++render_stats.gpu_group_vertices;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    int sampled=(render_stats.gpu_vertex_calls*2654435761u>>26)==0;
    unsigned int started=sampled?sceKernelGetSystemTimeLow():0,spill_started=0;
#endif
    GpuOverflowCache *cache=&gpu_overflow_cache[p->source_index&255u];
    if(cache->draw!=c->draw||cache->index!=p->source_index)
        *cache=(GpuOverflowCache){.draw=c->draw,.index=p->source_index};
    if(!cache->input_checked){
        float n2=p->nx*p->nx+p->ny*p->ny+p->nz*p->nz;
        cache->input_valid=isfinite(n2)&&n2>=1e-20f;cache->input_checked=1;
    }
    c->gpu_reason=5;
    if(!cache->input_valid)result=0;
    else if(cache->material==c->material_serial)*color=cache->color;
    else{
        cache->response=gpu_strip_cache_enabled&&c->light_cache_reusable?light_response(c,p):NULL;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
        if(sampled)spill_started=sceKernelGetSystemTimeLow();
#endif
        result=gpu_overflow_eval(c,p,color,cache,NULL,NULL);
    }
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    if(sampled){
        unsigned int ended=sceKernelGetSystemTimeLow(),overflow=spill_started?ended-spill_started:0;
        render_stats.gpu_classify_us+=(ended-started-overflow)*64u;
        render_stats.gpu_overflow_us+=overflow*64u;
    }
#endif
    return result;
}
typedef uint32_t RenderVertexWord __attribute__((__may_alias__));
_Static_assert(sizeof(RecvxBootGpuVertex)==36&&sizeof(RecvxGameChunkVertex)==40,"packed GPU/source vertex layout");
static inline __attribute__((always_inline)) void pack_gpu_vertex(RecvxBootGpuVertex *restrict out,DrawContext *c,const RecvxGameChunkVertex *restrict p,unsigned int color)
{
    /* These fields are bit copies. Integer loads/stores avoid FPU copy
       interlocks and an intermediate normal/vertex structure on Allegrex. */
    RenderVertexWord *d=(RenderVertexWord *)out;
    const RenderVertexWord *s=(const RenderVertexWord *)p;
    d[0]=s[6];d[1]=s[7];d[2]=color;
    if(c->gpu_normal_fix){
        float *n=&out->nx;
        for(unsigned int k=0;k<3;++k)n[k]=c->gpu_normal_matrix[k]*p->nx+c->gpu_normal_matrix[k+3]*p->ny+c->gpu_normal_matrix[k+6]*p->nz;
    }else{d[3]=s[3];d[4]=s[4];d[5]=s[5];}
    d[6]=s[0];d[7]=s[1];d[8]=s[2];
}

static RecvxBootRenderBatch *gpu_queue(DrawContext *c,const RecvxGameChunkMaterial *m,
    unsigned int attributes,const RecvxBootTexture *texture,RecvxGameBootSampler sampler,unsigned int mask,unsigned int count,unsigned int strip)
{
    if(gpu_count>MAX_VERTICES-count)recvxGameBootUnsupported("original GPU draw queue exceeds capacity");
    if(gpu_count+count>gpu_capacity){
        size_t capacity=gpu_capacity?gpu_capacity*2:4096;
        while(capacity<gpu_count+count)capacity*=2;
        if(capacity>MAX_VERTICES)capacity=MAX_VERTICES;
        void *memory=realloc(gpu_vertices,capacity*sizeof(*gpu_vertices));
        if(!memory)recvxGameBootUnsupported("original GPU vertex allocation failed");
        gpu_vertices=memory;gpu_capacity=capacity;
    }
    RecvxBootRenderBatch *b=batch_count?&batches[batch_count-1]:NULL;
    if(!b||b->gpu!=1||b->gpu_strip!=strip||b->gpu_draw!=c->draw||b->gpu_material!=c->material_serial||b->gpu_mask!=mask||b->count+count>65535u){
        if(!gpu_count&&!vertex_count)capture_projection();
        unsigned int flags=sampler.clamp_u|(sampler.clamp_v<<1)|(!!sampler.filter<<2);
        int source=c->state->control_flags&0x8000u?c->state->blend_source:m->blend_source;
        int destination=c->state->control_flags&0x8000u?c->state->blend_destination:m->blend_destination;
        append(NULL,0,RECVX_RENDER_TRIANGLES,attributes,flags,source,destination,texture,0);
        b=&batches[batch_count-1];b->first=gpu_count;b->gpu=1;b->gpu_strip=strip;b->gpu_draw=c->draw;
        b->gpu_material=c->material_serial;b->gpu_mask=mask;b->gpu_state=c->gpu_state;
        gpu_model_state(b,c,0);
    }
    return b;
}

static int emit_native_triangle(DrawContext *c,const RecvxGameChunkMaterial *m,const RecvxGameChunkVertex input[3],
    unsigned int attributes,const RecvxBootTexture *texture,RecvxGameBootSampler sampler)
{
    unsigned int face_mask=0;
    if(c->gpu_state.lit)for(unsigned int i=0;i<3;++i){
        float n2=input[i].nx*input[i].nx+input[i].ny*input[i].ny+input[i].nz*input[i].nz;
        if((attributes&0x20u)||!isfinite(n2)||n2<1e-20f)face_mask|=1u<<i;
    }
    NJS_VECTOR face={0};
    if(face_mask){
        float ax=input[1].x-input[0].x,ay=input[1].y-input[0].y,az=input[1].z-input[0].z;
        float bx=input[2].x-input[0].x,by=input[2].y-input[0].y,bz=input[2].z-input[0].z;
        face=(NJS_VECTOR){ay*bz-az*by,az*bx-ax*bz,ax*by-ay*bx};
        float area=dot(&face,&face);
        if(!isfinite(area))recvxGameBootUnsupported("non-finite native face normal");
        if(area<1e-40f)return 1;
        if(c->reversed){face.x=-face.x;face.y=-face.y;face.z=-face.z;}
    }
    unsigned int mask=(1u<<c->gpu_state.light_count)-1;
    RecvxBootRenderBatch *b=gpu_queue(c,m,attributes,texture,sampler,mask,3,0);
    for(unsigned int v=0;v<3;++v){
        unsigned int index=c->reversed&&v?3-v:v;
        const RecvxGameChunkVertex *p=&input[index];
        if(face_mask&(1u<<index)){
            RecvxGameChunkVertex flat=*p;flat.nx=face.x;flat.ny=face.y;flat.nz=face.z;
            pack_gpu_vertex(&gpu_vertices[gpu_count++],c,&flat,c->gpu_state.diffuse);
        }else pack_gpu_vertex(&gpu_vertices[gpu_count++],c,p,c->gpu_state.diffuse);
    }
    b->count+=3;render_stats.gpu_vertices+=3;render_stats.gpu_direct_vertices+=3;
    if(c->gpu_state.lit){render_stats.gpu_lit_vertices+=3;render_stats.gpu_native_light_vertices+=3;}
    return 1;
}

static int emit_gpu(DrawContext *c,const RecvxGameChunkMaterial *m,const RecvxGameChunkVertex input[3],
                    unsigned int attributes,const RecvxBootTexture *texture,RecvxGameBootSampler sampler)
{
    if(!gpu_enabled||c->sprite)return 0;
    ensure_gpu_prepared(c,m,attributes);
    if(!c->gpu_compatible)goto fallback;
    if(c->gpu_state.native_lighting)return emit_native_triangle(c,m,input,attributes,texture,sampler);
    unsigned int masks[3]={0},boundaries=0;
    unsigned int colors[3]={c->gpu_state.diffuse,c->gpu_state.diffuse,c->gpu_state.diffuse};
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    unsigned int overflow_started=c->profile_sample?sceKernelGetSystemTimeLow():0;
#endif
    /* Supported normal-only chunk streams supply white source colors. */
    for(unsigned int v=0;v<3;++v){
        unsigned int boundary;
        if(!gpu_vertex(c,&input[v],&masks[v],&boundary,&colors[v]))goto fallback;
        boundaries|=boundary;
    }
    unsigned int mask=masks[0]&masks[1]&masks[2];
    if(boundaries||((masks[0]|masks[1]|masks[2])!=mask)){c->gpu_reason=6;goto fallback;}
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    if(c->profile_sample)render_stats.gpu_vertex_us+=(sceKernelGetSystemTimeLow()-overflow_started)*32u;
#endif
    RecvxBootRenderBatch *b=gpu_queue(c,m,attributes,texture,sampler,mask,3,0);
    for(unsigned int v=0;v<3;++v){
        const RecvxGameChunkVertex *p=&input[c->reversed&&v?3-v:v];
        pack_gpu_vertex(&gpu_vertices[gpu_count++],c,p,colors[c->reversed&&v?3-v:v]);
        ++render_stats.gpu_direct_vertices;
    }
    b->count+=3;render_stats.gpu_vertices+=3;
    if(c->gpu_state.lit)render_stats.gpu_lit_vertices+=3;
    return 1;
fallback:
    ++render_stats.gpu_fallback_triangles;++render_stats.gpu_fallback[c->gpu_reason];return 0;
}

static VertexCache *cached_vertex(DrawContext *context,const RecvxGameChunkVertex *input)
{
    VertexCache *cache=&vertex_cache[input->source_index&255u];
    if(cache->draw!=context->draw||cache->index!=input->source_index){
        *cache=(VertexCache){.draw=context->draw,.index=input->source_index};
        recvxFaceTransformAligned(context->matrix,&input->x,&cache->position.x);
        if(context->custom_clip&&!gpu_enabled)cache->clip_code=clip_outcode(&cache->position,context->clip_planes);
        ++render_stats.position_transforms;
    }else ++render_stats.cache_hits;
    return cache;
}

static unsigned int prepare_material(DrawContext *context,const RecvxGameChunkMaterial *material,
    const RecvxBootTexture **out_texture,RecvxGameBootSampler *out_sampler)
{
    int material_changed=!context->sprite&&
        (!context->material_valid||memcmp(&context->material,material,sizeof(*material)));
    if(material_changed){
        context->material=*material;context->material_valid=1;++context->material_serial;context->gpu_prepared=0;
    }
    unsigned int attributes = context->sprite ? (0x13u | (context->trans ? 8u : 0)) :
        recvxGameBootRenderStripAttributes(material->flags);
    const RecvxBootTexture *texture = NULL;
    RecvxGameBootSampler sampler = {0};
    if (attributes & 0x40u) recvxGameBootUnsupported("unported original environment-mapped chunk material");
    if (context->sprite && context->textured) {
        texture = context->state->selected_texture;
        if (!texture || !texture->source || !texture->pixels || texture->tile_count != 1)
            recvxGameBootUnsupported("invalid original sprite texture selection");
        sampler.clamp_u = sampler.clamp_v = 1;
        sampler.filter = context->state->texture_filter;
    } else if (!context->sprite) {
        if(material_changed){
            if(material->texture != UINT_MAX){
                texture = recvxGameBootRenderTextureSelect(NULL, material->texture);
                recvxGameBootRenderTextureSampler(material->texture_bits, material->texture_flags, &sampler);
                if (texture->tile_count != 1) recvxGameBootUnsupported("unported tiled 3D material texture");
                if (sampler.mirror_u || sampler.mirror_v) recvxGameBootUnsupported("unported mirrored 3D material sampler");
            }
            context->material_texture=texture;context->material_sampler=sampler;
            ++render_stats.material_preparations;
        }else{texture=context->material_texture;sampler=context->material_sampler;}
    }
    *out_texture=texture;*out_sampler=sampler;return attributes;
}

/* Preflight each source vertex once. Native strips keep authored triangle
   order; repeated vertices bridge compatible strips without new fragments. */
static int emit_gpu_strip_inner(void *user,const RecvxGameChunkMaterial *material,
    const RecvxGameChunkVertex *input,unsigned int count,unsigned int reversed)
{
    DrawContext *c=user;
    const RecvxBootTexture *texture;RecvxGameBootSampler sampler;
    unsigned int attributes=prepare_material(c,material,&texture,&sampler);
    ensure_gpu_prepared(c,material,attributes);
    if(!c->gpu_compatible)return 0;
    if(c->gpu_state.native_lighting&&(attributes&0x20u))return 0;
    unsigned int ordinal=c->strip_ordinal++;
    StripCacheEntry *cached=gpu_strip_cache_enabled&&c->persistent_allowed?strip_cache_entry(c,ordinal):NULL;
    RecvxBootGpuVertex scratch[RECVX_GAME_CHUNK_STRIP_MAX];
    unsigned int colors[RECVX_GAME_CHUNK_STRIP_MAX];
    int direct=0;
    const RecvxBootGpuVertex *packed=scratch;
    unsigned int mask=0,triangles=0;
    if(cached&&cached->model==c->model&&cached->ordinal==ordinal&&cached->generation==c->strip_generation&&
       cached->count==count&&!memcmp(cached->memory,input,count*sizeof(*input))){
        packed=(const RecvxBootGpuVertex *)((const unsigned char *)cached->memory+count*sizeof(*input));
        mask=cached->mask;triangles=cached->triangles;++render_stats.gpu_strip_cache_hits;
    }else{
        if(cached)++render_stats.gpu_strip_cache_misses;
        direct=!c->gpu_normal_fix&&(!cached||!c->strip_cache_store);
        if(resident_enabled&&resident_ok){
            /* Parser, copies and triangle fallback only use M6/M7. Retain
               M2-M5 across this model's groups until preparation changes. */
            if(c->resident_material!=c->material_serial){
                recvxRenderLoadMatrices(c->matrix,c->normals,c->gpu_metric);
                c->resident_material=c->material_serial;
            }
            c->vfpu_resident=1;
        }
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
        unsigned int bounds_started=c->bulk_profile?sceKernelGetSystemTimeLow():0;
#endif
        prepare_group_bounds(c,input,count);
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
        if(c->bulk_profile)render_stats.gpu_group_bounds_us+=(sceKernelGetSystemTimeLow()-bounds_started)*8u;
#endif
        if(c->group_culled){
#if defined(RECVX_GAME_BOOT_HOST_TEST)
            if(recvxGameBootRenderCullCheck)recvxGameBootRenderCullCheck(c->matrix,frame.projection,input,count);
#endif
            for(unsigned int v=2;v<count;++v)
                if(input[v-2].source_index!=input[v-1].source_index&&input[v].source_index!=input[v-2].source_index&&input[v].source_index!=input[v-1].source_index)++triangles;
            ++render_stats.gpu_culled_groups;render_stats.gpu_culled_vertices+=count;
            render_stats.gpu_culled_triangles+=triangles;render_stats.triangles+=triangles;
            return 1;
        }
        if(c->vfpu_resident&&!c->group_known)prepare_distance_batch(c,input);
        unsigned int color_sources[32];
        if(c->group_known&&c->gpu_state.vertex_ambient&&c->group_extra_mask)memset(color_sources,0,sizeof(color_sources));
        for(unsigned int v=0;v<count;++v){
            unsigned int vertex_mask,boundary,color;
            if(c->group_known&&(!c->gpu_state.vertex_ambient||!c->group_extra_mask)){
                /* No source-cache lookup, distance batch or spill-color call
                   is needed for this proven constant group. Invalid normals
                   must still select the original face-normal fallback. */
                if(c->gpu_state.lit){
                    float n2=input[v].nx*input[v].nx+input[v].ny*input[v].ny+input[v].nz*input[v].nz;
                    c->gpu_reason=5;if(!isfinite(n2)||n2<1e-20f)return 0;
                }
                vertex_mask=c->group_mask;color=c->gpu_state.vertex_ambient?c->group_color:c->gpu_state.diffuse;
                ++render_stats.gpu_vertex_calls;++render_stats.gpu_group_vertices;++render_stats.gpu_group_constant_vertices;
            }else if(c->group_known){
                vertex_mask=c->group_mask;
                unsigned int slot=input[v].source_index&31u,previous=color_sources[slot];
                if(previous&&input[previous-1].source_index==input[v].source_index){
                    color=colors[previous-1];
                    ++render_stats.gpu_vertex_calls;++render_stats.gpu_group_vertices;++render_stats.gpu_group_reused_vertices;
                }else{
                    if(!gpu_group_overflow_vertex(c,&input[v],&color))return 0;
                    color_sources[slot]=v+1;
                }
            }else if(!gpu_vertex(c,&input[v],&vertex_mask,&boundary,&color)||boundary)return 0;
            if(v&&vertex_mask!=mask)return 0;
            mask=vertex_mask;colors[v]=color;
            if(!direct)pack_gpu_vertex(&scratch[v],c,&input[v],color);
        }
        for(unsigned int v=2;v<count;++v)
            if(input[v-2].source_index!=input[v-1].source_index&&input[v].source_index!=input[v-2].source_index&&input[v].source_index!=input[v-1].source_index)++triangles;
        if(cached&&c->strip_cache_store)strip_cache_store(cached,c,ordinal,input,scratch,count,triangles,mask);
    }
    if(!triangles)return 1;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    if(c->bulk_profile)c->bulk_output_started=sceKernelGetSystemTimeLow();
#endif
    unsigned int strip_flip=!!reversed^!!c->reversed;
    if(gpu_native_strips&&count+strip_flip+3<triangles*3){
        /* A leading duplicate flips strip parity for encoded/model reflection.
           Bridge parity starts every subsequent authored strip on an even face. */
        unsigned int flip=strip_flip;
        RecvxBootRenderBatch *batch=gpu_queue(c,material,attributes,texture,sampler,mask,count+flip+3,1);
        unsigned int start=gpu_count;
        if(batch->count){
            RecvxBootGpuVertex last=gpu_vertices[gpu_count-1];
            gpu_vertices[gpu_count++]=last;
            if(batch->count&1u)gpu_vertices[gpu_count++]=last;
            if(direct)pack_gpu_vertex(&gpu_vertices[gpu_count++],c,&input[0],colors[0]);
            else gpu_vertices[gpu_count++]=packed[0];
        }
        if(flip){
            if(direct)pack_gpu_vertex(&gpu_vertices[gpu_count++],c,&input[0],colors[0]);
            else gpu_vertices[gpu_count++]=packed[0];
        }
        if(direct){
            for(unsigned int v=0;v<count;++v)pack_gpu_vertex(&gpu_vertices[gpu_count++],c,&input[v],colors[v]);
            render_stats.gpu_direct_vertices+=count;
        }else{memcpy(gpu_vertices+gpu_count,packed,count*sizeof(*packed));gpu_count+=count;}
        unsigned int submitted=gpu_count-start;
        batch->count+=submitted;render_stats.triangles+=triangles;
        render_stats.gpu_vertices+=submitted;render_stats.gpu_bulk_triangles+=triangles;
        if(c->gpu_state.lit)render_stats.gpu_lit_vertices+=submitted;
        if(c->gpu_state.native_lighting&&c->gpu_state.lit)render_stats.gpu_native_light_vertices+=submitted;
        return 1;
    }
    RecvxBootRenderBatch *batch=gpu_queue(c,material,attributes,texture,sampler,mask,triangles*3,0);
    for(unsigned int v=2;v<count;++v){
        if(input[v-2].source_index==input[v-1].source_index||input[v].source_index==input[v-2].source_index||input[v].source_index==input[v-1].source_index)continue;
        unsigned int a=v-2,b=v-1,d=v;
        if(((v-2)&1u)^reversed){unsigned int swap=a;a=b;b=swap;}
        if(c->reversed){unsigned int swap=b;b=d;d=swap;}
        if(direct){
            pack_gpu_vertex(&gpu_vertices[gpu_count++],c,&input[a],colors[a]);
            pack_gpu_vertex(&gpu_vertices[gpu_count++],c,&input[b],colors[b]);
            pack_gpu_vertex(&gpu_vertices[gpu_count++],c,&input[d],colors[d]);
            render_stats.gpu_direct_vertices+=3;
        }else{gpu_vertices[gpu_count++]=packed[a];gpu_vertices[gpu_count++]=packed[b];gpu_vertices[gpu_count++]=packed[d];}
    }
    batch->count+=triangles*3;render_stats.triangles+=triangles;
    render_stats.gpu_vertices+=triangles*3;render_stats.gpu_bulk_triangles+=triangles;
    if(c->gpu_state.lit)render_stats.gpu_lit_vertices+=triangles*3;
    if(c->gpu_state.native_lighting&&c->gpu_state.lit)render_stats.gpu_native_light_vertices+=triangles*3;
    return 1;
}
static int emit_gpu_strip(void *user,const RecvxGameChunkMaterial *material,
    const RecvxGameChunkVertex *input,unsigned int count,unsigned int reversed)
{
    ++render_stats.gpu_bulk_attempts;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    int sample=(render_stats.gpu_bulk_attempts&7u)==0;
    unsigned int started=sample?sceKernelGetSystemTimeLow():0;
#endif
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    ((DrawContext *)user)->bulk_profile=sample;((DrawContext *)user)->bulk_output_started=0;
#endif
    int handled=emit_gpu_strip_inner(user,material,input,count,reversed);
    ((DrawContext *)user)->vfpu_resident=0;((DrawContext *)user)->distance_batch=0;
    ((DrawContext *)user)->group_known=0;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    if(sample){
        unsigned int ended=sceKernelGetSystemTimeLow();
        render_stats.gpu_bulk_us+=(ended-started)*8u;
        unsigned int output_started=((DrawContext *)user)->bulk_output_started;
        if(output_started)render_stats.gpu_output_us+=(ended-output_started)*8u;
    }
#endif
    return handled;
}

static int emit(void *user, const RecvxGameChunkMaterial *material, const RecvxGameChunkVertex input[3])
{
    DrawContext *context = user;
    RecvxBootRenderVertex output[3];
    NJS_POINT3 positions[3]; NJS_VECTOR face;
    unsigned int clip_codes[3]={0};
    ++render_stats.triangles;
    const RecvxBootTexture *texture;
    RecvxGameBootSampler sampler;
    unsigned int attributes=prepare_material(context,material,&texture,&sampler);
    context->profile_sample=(render_stats.triangles&31u)==0;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    unsigned int gpu_started=context->profile_sample?sceKernelGetSystemTimeLow():0;
#endif
    int emitted_gpu=emit_gpu(context,material,input,attributes,texture,sampler);
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    if(context->profile_sample)render_stats.gpu_emit_us+=(sceKernelGetSystemTimeLow()-gpu_started)*32u;
#endif
    if(emitted_gpu)return 0;
    for (unsigned int i = 0; i < 3; ++i) {
        NJS_POINT3 local = {input[i].x, input[i].y, input[i].z};
        if(context->sprite)njCalcPoint(&context->matrix, &local, &positions[i]);
        else {
            VertexCache *cache=cached_vertex(context,&input[i]);
            positions[i]=cache->position;clip_codes[i]=cache->clip_code;
        }
    }
    checked_math();
    int needs_clip=context->custom_clip&&!gpu_enabled;
    if(needs_clip){
        unsigned int a=clip_codes[0],b=clip_codes[1],c=clip_codes[2];
        /* Most triangles are wholly inside or outside. Only boundary
           crossings need polygon copies, intersections and color/UV rebuilds. */
        if(a&b&c){++render_stats.clip_rejected;return 0;}
        needs_clip=!!(a|b|c);
        if(needs_clip)++render_stats.clip_intersections;
        else ++render_stats.clip_inside;
    }
    NJS_VECTOR a = {positions[1].x-positions[0].x, positions[1].y-positions[0].y, positions[1].z-positions[0].z};
    NJS_VECTOR b = {positions[2].x-positions[0].x, positions[2].y-positions[0].y, positions[2].z-positions[0].z};
    face = (NJS_VECTOR){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    float area_squared=dot(&face,&face);
    if (!isfinite(area_squared)||area_squared==0||area_squared<1e-40f) return 0;
    int face_ready=0;
    for (unsigned int i = 0; i < 3; ++i) {
        NJS_VECTOR normal = {input[i].nx, input[i].ny, input[i].nz}, transformed;
        unsigned int color;
        if(context->sprite)color=input[i].color;
        else{
            VertexCache *cache=cached_vertex(context,&input[i]);
            int flat=attributes&0x20u;
            if(!flat&&cache->material==context->material_serial){color=cache->color;}
            else{
                transformed=face;
                int needs_face=!(attributes&1u);
                if(!(attributes&1u)&&!flat){
                    if(!cache->normal_ready){
                        recvxSkinDirectionAligned(context->normals,&normal.x,&cache->normal.x);
                        ++render_stats.normal_transforms;
                        cache->normal_valid=normalize(&cache->normal);cache->normal_ready=1;
                    }
                    if(cache->normal_valid){transformed=cache->normal;needs_face=0;}
                }
                /* Smooth geometry uses its vertex normals. Normalize the
                   face only for flat lighting or a missing vertex normal. */
                if(needs_face){
                    if(!face_ready){
                        if(!normalize(&face))return 0;
                        face_ready=1;++render_stats.face_normalizations;
                    }
                    transformed=face;
                }
                color=shade(context,material,&input[i],&positions[i],&transformed,attributes);
                ++render_stats.shaded_vertices;
                if(!flat&&((attributes&1u)||cache->normal_valid)){cache->color=color;cache->material=context->material_serial;}
            }
        }
        output[i] = (RecvxBootRenderVertex){input[i].u, input[i].v, color,
            positions[i].x, -positions[i].y, -positions[i].z};
        color = output[i].color;
        output[i].color = (color & 0xff00ff00u) | ((color >> 16) & 255u) | ((color & 255u) << 16);
        if (context->half_fog && fabsf(output[i].z) >= 0x1p39f)
            recvxGameBootUnsupported("authored sprite fog depth exceeds PSP constant range");
        if (!isfinite(output[i].x) || !isfinite(output[i].y) || !isfinite(output[i].z))
            recvxGameBootUnsupported("non-finite original model transform");
    }
    if(context->reversed){RecvxBootRenderVertex swap=output[1];output[1]=output[2];output[2]=swap;}
    unsigned int flags = sampler.clamp_u | (sampler.clamp_v << 1) | (!!sampler.filter << 2);
    if(specular_enabled(context,material,attributes))flags|=RECVX_RENDER_COLOR_DOUBLE;
    int source = context->sprite ? context->state->polygon_blend_source : context->state->control_flags & 0x8000u ? context->state->blend_source : material->blend_source;
    int destination = context->sprite ? context->state->polygon_blend_destination : context->state->control_flags & 0x8000u ? context->state->blend_destination : material->blend_destination;
    RecvxBootRenderVertex clipped[21];
    unsigned int count=needs_clip?clip_triangle(output,context->clip_planes,clipped):3;
    if(!count)return 0;
    int gpu_colored=gpu_enabled&&!context->sprite;
    if(gpu_colored){
        /* Preserve original specular/flat/far-boundary colors, while the GE
           applies the model transform and clips the submitted local vertices. */
        for(unsigned int v=0;v<3;++v){
            const RecvxGameChunkVertex *p=&input[context->reversed&&v?3-v:v];
            output[v].x=p->x;output[v].y=p->y;output[v].z=p->z;
        }
        /* Merge only adjacent prelit triangles from the same model/material. */
        RecvxBootRenderBatch *b=batch_count?&batches[batch_count-1]:NULL;
        if(!b||b->gpu!=2||b->gpu_draw!=context->draw||b->gpu_material!=context->material_serial||b->count>65532u){
            append(NULL,0,RECVX_RENDER_TRIANGLES,attributes,flags,source,destination,texture,context->fog_color);
            b=&batches[batch_count-1];b->gpu=2;b->gpu_draw=context->draw;b->gpu_material=context->material_serial;
            if(!vertex_count&&!gpu_count)capture_projection();
            b->gpu_state.lit=0;gpu_model_state(b,context,1);
        }
        reserve(3);b=&batches[batch_count-1];
        b->count+=3;memcpy(vertices+vertex_count,output,sizeof(output));vertex_count+=3;
        render_stats.gpu_colored_vertices+=3;
    }else append(needs_clip?clipped:output, count, context->half_fog ? RECVX_RENDER_HALF_FOG : RECVX_RENDER_TRIANGLES,
        attributes, flags, source, destination, texture, context->fog_color);
    return 0;
}

void recvxGameBootRenderSpriteTriangle(const RecvxGameChunkVertex input[3], int textured, int trans)
{
    DrawContext context = {.sprite = 1, .textured = textured, .trans = trans};
    RecvxGameChunkMaterial material = {.texture = UINT_MAX};
    if (!input || (textured != 0 && textured != 1) || (trans != 0 && trans != 1))
        recvxGameBootUnsupported("invalid original sprite submission");
    for (unsigned int i = 0; i < 3; ++i)
        if (!isfinite(input[i].x) || !isfinite(input[i].y) || !isfinite(input[i].z) ||
            !isfinite(input[i].u) || !isfinite(input[i].v))
            recvxGameBootUnsupported("non-finite original sprite vertex");
    njGetMatrix(&context.matrix); checked_math();
    memcpy(context.normals, context.matrix, sizeof(context.normals));
    context.state = recvxGameBootRenderStateGet();
    emit(&context, &material, input);
}

void recvxGameBootRenderHalfFogTriangle(const RecvxGameChunkVertex input[3], unsigned int offset_argb, int trans)
{
    DrawContext context = {.sprite=1, .textured=1, .trans=trans, .half_fog=1};
    RecvxGameChunkMaterial material = {.texture=UINT_MAX};
    if (!input || (trans != 0 && trans != 1))
        recvxGameBootUnsupported("invalid authored fog sprite submission");
    for (unsigned int i=0; i<3; ++i)
        if (!isfinite(input[i].x) || !isfinite(input[i].y) || !isfinite(input[i].z) ||
            !isfinite(input[i].u) || !isfinite(input[i].v))
            recvxGameBootUnsupported("non-finite authored fog sprite vertex");
    njGetMatrix(&context.matrix); checked_math();
    memcpy(context.normals, context.matrix, sizeof(context.normals));
    context.state = recvxGameBootRenderStateGet();
    context.fog_color = ((offset_argb >> 16) & 255u) | (offset_argb & 0xff00u) | ((offset_argb & 255u) << 16);
    emit(&context, &material, input);
}

static void normal_matrix(NJS_MATRIX *output, NJS_MATRIX *input)
{
    memcpy(*output, *input, sizeof(NJS_MATRIX));
    if (njInvertMatrix(output)) njTransposeMatrix(output);
    else checked_math();
    (*output)[12] = (*output)[13] = (*output)[14] = 0;
}

int njCnkDrawModelLocal(NJS_CNK_MODEL *model)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    unsigned int started=sceKernelGetSystemTimeLow();
#endif
    DrawContext context = {.model=model};
    if (!model) return 0;
    if (!sys || !owned(model, sizeof(*model))) recvxGameBootUnsupported("invalid original chunk model pointer");
    if (!isfinite(model->center.x) || !isfinite(model->center.y) || !isfinite(model->center.z) || !isfinite(model->r))
        recvxGameBootUnsupported("invalid original chunk model bounds");
    njGetMatrix(&context.matrix); checked_math();
    context.state = recvxGameBootRenderStateGet(); context.lighting = recvxEngineOriginalLightingGet();
    const float *m=context.matrix;
    context.reversed=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2])<0;
    if ((context.state->control_flags & 0x100u) && isfinite(model->r) && model->r > 0) {
        NJS_POINT3 center; float near, far;
        njCalcPoint(&context.matrix, &model->center, &center); checked_math();
        recvxEngineOriginalViewClipPlanes(&near, &far);
        float scale = 0;
        for (unsigned int c = 0; c < 3; ++c) {
            float length = sqrtf(context.matrix[c*4]*context.matrix[c*4] +
                context.matrix[c*4+1]*context.matrix[c*4+1] + context.matrix[c*4+2]*context.matrix[c*4+2]);
            scale = fmaxf(scale, length);
        }
        float radius = model->r * scale;
        if (center.z + radius < -near || center.z - radius > -far) return 0;
        if (center.z != 0) {
            float distance = fabsf(recvxEngineOriginalViewProjectionDistance() / center.z), ax, ay;
            recvxEngineOriginalViewAspect(&ax, &ay);
            if (center.x*distance + radius*distance*ax < -320 ||
                center.x*distance - radius*distance*ax > 320 ||
                center.y*distance + radius*distance*ay < -240 ||
                center.y*distance - radius*distance*ay > 240) return 0;
        }
    }
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    unsigned int matrix_started=sceKernelGetSystemTimeLow();
#endif
    normal_matrix(&context.normals, &context.matrix); checked_math();
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    render_stats.gpu_matrix_us+=sceKernelGetSystemTimeLow()-matrix_started;
#endif
    if(gpu_enabled&&resident_enabled){
        if(!resident_checked){
            resident_ok=recvxRenderResidentCheck();resident_checked=1;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
            recvxGameBootLog("resident VFPU model/normal/metric check: %s",resident_ok?"passed":"failed; using existing math");
#endif
        }
        recvxRenderVectorPrepare();
    }else recvxFaceVectorPrepare();
    context.model_cache_volatile=gpu_enabled&&gpu_strip_cache_enabled&&!native_lighting_enabled?!persistent_cache_allowed(&context):0;
    context.draw=++draw_serial;
    if(!context.draw){memset(vertex_cache,0,sizeof(vertex_cache));memset(gpu_overflow_cache,0,sizeof(gpu_overflow_cache));context.draw=++draw_serial;}
    ++render_stats.models;
    context.custom_clip=recvxEngineOriginalViewClipVolumePlanes(context.clip_planes);checked_math();
    if (recvxGameBootChunkParseJoinedStrips(model, emit, gpu_enabled&&gpu_strips_enabled?emit_gpu_strip:NULL, &context)) recvxGameBootUnsupported(recvxGameBootChunkFault());
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    render_stats.chunk_us+=sceKernelGetSystemTimeLow()-started;
#endif
    return 1;
}
void njCnkEasyDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(0); njCnkDrawModelLocal(model); }
void njCnkEasyMultiDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(1); njCnkDrawModelLocal(model); }
void njCnkSimpleDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(2); njCnkDrawModelLocal(model); }
void njCnkSimpleMultiDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(3); njCnkDrawModelLocal(model); }

static void tree(NJS_CNK_OBJECT *object, unsigned int depth, unsigned int *visited)
{
    if (depth > 64) recvxGameBootUnsupported("original draw object tree exceeds depth");
    for (; object; object = object->sibling) {
        if (++*visited > 512 || !owned(object, sizeof(*object)))
            recvxGameBootUnsupported("invalid or cyclic original draw object tree");
        unsigned int flags = object->evalflags;
        for (unsigned int i = 0; i < 3; ++i)
            if ((!(flags & 1u) && !isfinite(object->pos[i])) ||
                (!(flags & 4u) && !isfinite(object->scl[i])))
                recvxGameBootUnsupported("invalid original draw object transform");
        if (!njPushMatrixEx()) checked_math();
        if (!(flags & 1u)) njTranslate(NULL, object->pos[0], object->pos[1], object->pos[2]);
        if (!(flags & 2u)) njRotateEx(object->ang, flags & 0x20u);
        if (!(flags & 4u)) njScale(NULL, object->scl[0], object->scl[1], object->scl[2]);
        checked_math();
        int visible = flags & 8u ? 0 : njCnkDrawModelLocal(object->model);
        if (!(flags & 0x10u) && (!(flags & 0x100u) || visible)) tree(object->child, depth+1, visited);
        if (!njPopMatrixEx()) checked_math();
    }
}
void njCnkEasyDrawObject(NJS_CNK_OBJECT *object)
{ unsigned int visited = 0; njCnkSetCurrentDrawMode(0); tree(object, 0, &visited); }
void njCnkEasyMultiDrawObject(NJS_CNK_OBJECT *object)
{ unsigned int visited = 0; njCnkSetCurrentDrawMode(1); tree(object, 0, &visited); }
void njCnkEasyMultiDrawObjectI(NJS_CNK_OBJECT *object)
{ unsigned int visited = 0; njCnkSetCurrentDrawMode(1); ulCnkCurrentDrawMode |= 4; tree(object, 0, &visited); }
