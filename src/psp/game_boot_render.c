#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_render.h"
#include "psp/game_boot_chunk.h"
#include "psp/game_boot_render_texture.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_lighting.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"

#define MAX_VERTICES 262144u
#define MAX_BATCHES 16384u
static RecvxBootRenderVertex *vertices;
static RecvxBootRenderBatch *batches;
static size_t vertex_capacity, batch_capacity, vertex_count, batch_count;
static RecvxBootRenderFrame frame __attribute__((aligned(16)));

_Static_assert(sizeof(RecvxBootRenderVertex) == 24, "PSP GU vertex packing");

void recvxGameBootRenderBeginFrame(void) { vertex_count = batch_count = 0; }
void recvxGameBootRenderConsume(void) { recvxGameBootRenderBeginFrame(); }
int recvxGameBootRenderPending(void) { return vertex_count != 0; }
void recvxGameBootRenderReset(void)
{
    free(vertices); free(batches); vertices = NULL; batches = NULL;
    vertex_capacity = batch_capacity = 0;
    memset(&frame, 0, sizeof(frame));
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
static void reserve(void)
{
    if (vertex_count > MAX_VERTICES - 3 || batch_count >= MAX_BATCHES)
        recvxGameBootUnsupported("original 3D draw queue exceeds capacity");
    if (vertex_count + 3 > vertex_capacity) {
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

const RecvxBootRenderFrame *recvxGameBootRenderFrame(void)
{
    frame.vertices = vertices; frame.batches = batches;
    frame.vertex_count = vertex_count; frame.batch_count = batch_count;
    if (vertex_count) {
        float near, far, ax, ay, distance = recvxEngineOriginalViewProjectionDistance();
        recvxEngineOriginalViewClipPlanes(&near, &far);
        recvxEngineOriginalViewAspect(&ax, &ay);
        near = -near; far = -far;
        if (!isfinite(distance) || !(distance > 0) || !(near > 0) || !(far > near))
            recvxGameBootUnsupported("invalid original 3D projection");
        memset(frame.projection, 0, sizeof(frame.projection));
        frame.projection[0] = 2.0f * distance * ax / 640.0f;
        frame.projection[5] = 2.0f * distance * ay / 480.0f;
        frame.projection[10] = -(far + near) / (far - near);
        frame.projection[11] = -1;
        frame.projection[14] = -(2.0f * far * near) / (far - near);
    }
    return &frame;
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
    return (unsigned int)(fminf(1, fmaxf(0, value)) * 255.0f + 0.5f);
}

typedef struct DrawContext {
    NJS_MATRIX matrix, normals;
    const RecvxGameBootRenderState *state;
    const RecvxEngineOriginalLightingState *lighting;
} DrawContext;

static unsigned int shade(const DrawContext *context, const RecvxGameChunkMaterial *material,
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
    float illumination[3] = {0, 0, 0};
    float alpha = channel(material->diffuse, 24) * channel(vertex->color, 24);
    if (context->state->control_flags & 0x10u) {
        diffuse[0] = context->state->constant_material.r;
        diffuse[1] = context->state->constant_material.g;
        diffuse[2] = context->state->constant_material.b;
        alpha = context->state->constant_material.a * channel(vertex->color, 24);
    }
    if (attributes & 1u) illumination[0] = illumination[1] = illumination[2] = 1;
    else {
        if (!(attributes & 4u)) for (unsigned int c = 0; c < 3; ++c)
            illumination[c] = multi ? ambient[c] * channel(material->ambient, 16 - 8*c) :
                (simple ? lighting->simple_ambient : lighting->easy_ambient);
        if (multi) {
            int count = simple ? lighting->simple_count : lighting->easy_count;
            if (count < 0 || count > 7) recvxGameBootUnsupported("invalid original model light count");
            for (int i = 1; i <= count; ++i) if (lights[i].enabled) {
                NJS_VECTOR direction;
                float attenuation = 1;
                if (i == 1) {
                    NJS_VECTOR source = lights[0].direction;
                    njCalcVector(cam.mtx, &source, &direction);
                    direction.x = -direction.x; direction.y = -direction.y; direction.z = -direction.z;
                } else {
                    NJS_POINT3 source = lights[i].position, point;
                    njCalcPoint(cam.mtx, &source, &point);
                    direction = (NJS_VECTOR){point.x-position->x, point.y-position->y, point.z-position->z};
                    float near = lights[i].near_range, far = lights[i].far_range;
                    float distance_squared = dot(&direction, &direction);
                    if (!isfinite(far) || !isfinite(near) || far < 0 || near < 0)
                        recvxGameBootUnsupported("invalid original model point light range");
                    attenuation = distance_squared >= far*far ? 0 :
                        distance_squared <= near*near ? 1 : near*near / distance_squared;
                }
                checked_math();
                if (normalize(&direction)) {
                    float intensity = fmaxf(0, dot(normal, &direction)) * attenuation;
                    illumination[0] += lights[i].color.x * intensity;
                    illumination[1] += lights[i].color.y * intensity;
                    illumination[2] += lights[i].color.z * intensity;
                }
            }
        } else {
            NJS_VECTOR direction = simple ? lighting->simple_direction : lighting->easy_direction;
            direction.x = -direction.x; direction.y = -direction.y; direction.z = -direction.z;
            if (normalize(&direction)) {
                float intensity = fmaxf(0, dot(normal, &direction)) *
                    (simple ? lighting->simple_intensity : lighting->easy_intensity);
                NJS_POINT3 color = simple ? lighting->simple_color : lighting->easy_color;
                illumination[0] += color.x * intensity;
                illumination[1] += color.y * intensity;
                illumination[2] += color.z * intensity;
            }
        }
    }
    unsigned int result = byte(alpha) << 24;
    for (unsigned int c = 0; c < 3; ++c)
        result |= byte(diffuse[c] * channel(vertex->color, 16 - 8*c) * illumination[c]) << (16 - 8*c);
    return result;
}

static int emit(void *user, const RecvxGameChunkMaterial *material, const RecvxGameChunkVertex input[3])
{
    DrawContext *context = user;
    RecvxBootRenderVertex output[3];
    NJS_POINT3 positions[3]; NJS_VECTOR face;
    unsigned int attributes = recvxGameBootRenderStripAttributes(material->flags);
    const RecvxBootTexture *texture = NULL;
    RecvxGameBootSampler sampler = {0};
    if (!(attributes & 3u) && (material->specular & 0xffffffu))
        recvxGameBootUnsupported("unported original specular chunk lighting");
    if (attributes & 0x40u) recvxGameBootUnsupported("unported original environment-mapped chunk material");
    if (material->texture != UINT_MAX) {
        texture = recvxGameBootRenderTextureSelect(NULL, material->texture);
        recvxGameBootRenderTextureSampler(material->texture_bits, material->texture_flags, &sampler);
        if (texture->tile_count != 1) recvxGameBootUnsupported("unported tiled 3D material texture");
        if (sampler.mirror_u || sampler.mirror_v) recvxGameBootUnsupported("unported mirrored 3D material sampler");
    }
    for (unsigned int i = 0; i < 3; ++i) {
        NJS_POINT3 local = {input[i].x, input[i].y, input[i].z};
        njCalcPoint(&context->matrix, &local, &positions[i]);
    }
    checked_math();
    NJS_VECTOR a = {positions[1].x-positions[0].x, positions[1].y-positions[0].y, positions[1].z-positions[0].z};
    NJS_VECTOR b = {positions[2].x-positions[0].x, positions[2].y-positions[0].y, positions[2].z-positions[0].z};
    face = (NJS_VECTOR){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    if (!normalize(&face)) return 0; /* Zero-area triangles cannot cover a sample. */
    for (unsigned int i = 0; i < 3; ++i) {
        NJS_VECTOR normal = {input[i].nx, input[i].ny, input[i].nz}, transformed;
        njCalcVector(&context->normals, &normal, &transformed);
        checked_math();
        if ((attributes & 0x20u) || !normalize(&transformed)) transformed = face;
        output[i] = (RecvxBootRenderVertex){input[i].u, input[i].v,
            shade(context, material, &input[i], &positions[i], &transformed, attributes),
            positions[i].x, -positions[i].y, -positions[i].z};
        unsigned int color = output[i].color;
        output[i].color = (color & 0xff00ff00u) | ((color >> 16) & 255u) | ((color & 255u) << 16);
        if (!isfinite(output[i].x) || !isfinite(output[i].y) || !isfinite(output[i].z))
            recvxGameBootUnsupported("non-finite original model transform");
    }
    reserve();
    unsigned int flags = sampler.clamp_u | (sampler.clamp_v << 1) | (!!sampler.filter << 2);
    int source = context->state->control_flags & 0x8000u ? context->state->blend_source : material->blend_source;
    int destination = context->state->control_flags & 0x8000u ? context->state->blend_destination : material->blend_destination;
    RecvxBootRenderBatch *batch = batch_count ? &batches[batch_count-1] : NULL;
    if (!batch || batch->count >= 65535u || batch->texture != texture || batch->attributes != attributes || batch->sampler != flags ||
        batch->blend_source != source || batch->blend_destination != destination) {
        batch = &batches[batch_count++];
        *batch = (RecvxBootRenderBatch){vertex_count, 0, attributes, flags, source, destination, texture};
    }
    memcpy(vertices + vertex_count, output, sizeof(output));
    vertex_count += 3; batch->count += 3;
    return 0;
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
    DrawContext context;
    if (!model) return 0;
    if (!sys || !owned(model, sizeof(*model))) recvxGameBootUnsupported("invalid original chunk model pointer");
    if (!isfinite(model->center.x) || !isfinite(model->center.y) || !isfinite(model->center.z) || !isfinite(model->r))
        recvxGameBootUnsupported("invalid original chunk model bounds");
    njGetMatrix(&context.matrix); checked_math();
    context.state = recvxGameBootRenderStateGet(); context.lighting = recvxEngineOriginalLightingGet();
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
    normal_matrix(&context.normals, &context.matrix); checked_math();
    if (recvxGameBootChunkParse(model, emit, &context)) recvxGameBootUnsupported(recvxGameBootChunkFault());
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
