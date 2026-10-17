#include <math.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include "psp/game_boot.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_view.h"

#define SHADOW_OFFSET (5u * 512u * 272u * 2u)
static void *scene_buffer;
static unsigned int display_list[262144] __attribute__((aligned(16)));
typedef struct { float u, v; unsigned int color; float x, y, z; } Vertex;

static unsigned int abgr(unsigned int argb)
{ return (argb & 0xff00ff00u) | ((argb >> 16) & 255) | ((argb & 255) << 16); }

int recvxGameBootDisplayInit(void)
{
    scene_buffer = NULL;
    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, 512);
    sceGuDispBuffer(480, 272, (void *)(512 * 272 * 4), 512);
    sceGuDepthBuffer((void *)(2 * 512 * 272 * 4), 512);
    sceGuDepthRange(65535, 0);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuFrontFace(GU_CCW);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272); sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuFinish(); sceGuSync(0, 0);
    sceDisplayWaitVblankStart(); sceGuDisplay(GU_TRUE);
    return 0;
}

static void sprite(float x1, float y1, float x2, float y2,
                   float u1, float v1, float u2, float v2, unsigned int color)
{
    Vertex *v = sceGuGetMemory(2 * sizeof(*v));
    v[0] = (Vertex){u1, v1, abgr(color), x1 * 0.75f, y1 * (272.0f / 480), 0};
    v[1] = (Vertex){u2, v2, abgr(color), x2 * 0.75f, y2 * (272.0f / 480), 0};
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 |
        GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, v);
}

static void gradient_polygon(const RecvxBootDraw *draw)
{
    static const unsigned int order[6] = {0, 1, 2, 0, 2, 3};
    Vertex *vertices = sceGuGetMemory(6 * sizeof(*vertices));
    for (unsigned int i = 0; i < 6; ++i) {
        unsigned int corner = order[i];
        vertices[i] = (Vertex){0, 0, abgr(draw->vertex_color[corner]),
            draw->points[corner].x * 0.75f, draw->points[corner].y * (272.0f / 480), 0};
    }
    sceGuShadeModel(GU_SMOOTH);
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 |
        GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, NULL, vertices);
}

static int blend_factor(int factor)
{
    switch (factor) {
    case 3: case 9: return GU_OTHER_COLOR;
    case 5: case 7: return GU_ONE_MINUS_OTHER_COLOR;
    case 2: return GU_DST_ALPHA;
    case 4: return GU_ONE_MINUS_DST_ALPHA;
    case 8: return GU_SRC_ALPHA;
    case 6: return GU_ONE_MINUS_SRC_ALPHA;
    case 10: case 11: return GU_FIX;
    default: recvxGameBootUnsupported("unported original 3D blend factor");
    }
}

static void shadow_target(void)
{ sceGuDrawBufferList(GU_PSM_5551, (void *)SHADOW_OFFSET, 512); }
static void scene_target(void)
{ sceGuDrawBufferList(GU_PSM_8888, scene_buffer, 512); }
static void shadow(const RecvxBootRenderFrame *frame, const RecvxBootRenderBatch *batch)
{
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_CULL_FACE);
    sceGuDepthMask(GU_TRUE); sceGuDisable(GU_TEXTURE_2D);
    sceGuShadeModel(GU_FLAT);
    if (batch->kind == RECVX_RENDER_SHADOW_START) {
        shadow_target();
        sceGuClearColor(0); sceGuClear(GU_COLOR_BUFFER_BIT);
    } else if (batch->kind == RECVX_RENDER_SHADOW_END) {
        scene_target();
    } else if (batch->kind == RECVX_RENDER_SHADOW_COMPOSITE) {
        const RecvxBootRenderVertex *rectangle = frame->vertices + batch->first;
        scene_target();
        sceGuDisable(GU_DEPTH_TEST); sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexSync(); sceGuTexFlush();
        sceGuTexMode(GU_PSM_5551, 0, 0, 0);
        sceGuTexImage(0, 512, 512, 512, (void *)(0x04000000u + SHADOW_OFFSET));
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
        sceGuTexWrap(GU_CLAMP, GU_CLAMP);
        sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
        sceGuTexFilter(GU_NEAREST, GU_NEAREST);
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
            2, NULL, rectangle);
        /* The PS2 clears only the rectangle it composited, retaining the mask
           outside it until the next shadow group. Share the scene depth. */
        shadow_target();
        sceGuDisable(GU_BLEND); sceGuDisable(GU_TEXTURE_2D);
        Vertex *clear = sceGuGetMemory(2 * sizeof(*clear));
        clear[0] = (Vertex){0,0,0,rectangle[0].x,rectangle[0].y,0};
        clear[1] = (Vertex){0,0,0,rectangle[1].x,rectangle[1].y,0};
        sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TEXTURE_32BITF | GU_TRANSFORM_2D,
            2, NULL, clear);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    } else {
        shadow_target();
        sceGuEnable(GU_DEPTH_TEST); sceGuEnable(GU_BLEND);
        sceGuBlendFunc(batch->kind == RECVX_RENDER_SHADOW_ADD ? GU_ADD : GU_REVERSE_SUBTRACT,
            GU_FIX, GU_FIX, 0xffffff, 0xffffff);
        sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            batch->count, NULL, frame->vertices + batch->first);
    }
}

static void models(const RecvxBootRenderFrame *frame)
{
    if (!frame->vertex_count) return;
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadMatrix((const ScePspFMatrix4 *)frame->projection);
    sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
    sceGuEnable(GU_DEPTH_TEST); sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0, 255);
    for (size_t i = 0; i < frame->batch_count; ++i) {
        const RecvxBootRenderBatch *batch = &frame->batches[i];
        if (batch->kind >= RECVX_RENDER_SHADOW_START) {
            shadow(frame, batch);
            continue;
        }
        if (batch->kind == RECVX_RENDER_HALF_FOG) {
            /* Finite powers of two keep the GE's (far + viewZ)/(far-near)
               exactly 1/2 for the validated authored sprite depth range. */
            sceGuFog(-0x1p64f, 0x1p64f, batch->fog_color);
            sceGuEnable(GU_FOG);
        } else sceGuDisable(GU_FOG);
        const RecvxBootTexture *texture = batch->texture;
        sceGuEnable(GU_DEPTH_TEST); sceGuDepthMask(GU_FALSE);
        sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0, 255);
        sceGuShadeModel(batch->attributes & 0x20u ? GU_FLAT : GU_SMOOTH);
        if (batch->attributes & 0x10u) sceGuDisable(GU_CULL_FACE);
        else sceGuEnable(GU_CULL_FACE);
        if (batch->attributes & 8u) {
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, blend_factor(batch->blend_source), blend_factor(batch->blend_destination),
                batch->blend_source == 10 ? 0xffffff : 0, batch->blend_destination == 10 ? 0xffffff : 0);
        } else sceGuDisable(GU_BLEND);
        if (texture) {
            sceGuEnable(GU_TEXTURE_2D); sceGuTexMode(GU_PSM_T8, 0, 0, 1);
            sceGuClutMode(GU_PSM_8888, 0, 255, 0); sceGuClutLoad(32, texture->palette);
            sceGuTexImage(0, texture->width, texture->height, texture->tile_width, texture->pixels);
            sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
            sceGuTexWrap(batch->sampler & 1 ? GU_CLAMP : GU_REPEAT, batch->sampler & 2 ? GU_CLAMP : GU_REPEAT);
            sceGuTexFilter(batch->sampler & 4 ? GU_LINEAR : GU_NEAREST, batch->sampler & 4 ? GU_LINEAR : GU_NEAREST);
            sceGuTexFlush();
        } else sceGuDisable(GU_TEXTURE_2D);
        if (batch->kind == RECVX_RENDER_SCREEN_TRIANGLES)
            sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                batch->count, NULL, frame->vertices + batch->first);
        else sceGumDrawArray(batch->kind == RECVX_RENDER_LINES ? GU_LINES : GU_TRIANGLES,
            GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            batch->count, NULL, frame->vertices + batch->first);
    }
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE); sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

void recvxGameBootPresent(void)
{
    size_t count, i;
    const RecvxBootDraw *draws = recvxGameBootDraws(&count);
    const RecvxBootRenderFrame *frame = recvxGameBootRenderFrame();
    if (65536u + frame->batch_count * 512u + count * 512u > sizeof(display_list))
        recvxGameBootUnsupported("original GU command list exceeds capacity");
    sceKernelDcacheWritebackAll();
    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(frame->vertex_count ? abgr(recvxEngineOriginalViewBackgroundColor()) : 0xff000000);
    sceGuClearDepth(0); sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    models(frame);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    for (i = 0; i < count; ++i) {
        const RecvxBootDraw *d = &draws[i];
        const QUAD *q = &d->quad;
        const RecvxBootTexture *t = d->texture;
        if (!t) {
            sceGuDisable(GU_TEXTURE_2D);
            if (d->color_gradient) gradient_polygon(d);
            else sprite(q->x1, q->y1, q->x2, q->y2, 0, 0, 0, 0, d->color);
        } else {
            unsigned int tile;
            float u1 = q->u1 * t->width, u2 = q->u2 * t->width;
            float v1 = q->v1 * t->height, v2 = q->v2 * t->height;
            sceGuEnable(GU_TEXTURE_2D); sceGuTexMode(GU_PSM_T8, 0, 0, 1);
            sceGuClutMode(GU_PSM_8888, 0, 255, 0); sceGuClutLoad(32, t->palette);
            sceGuTexFilter(d->filter ? GU_LINEAR : GU_NEAREST, d->filter ? GU_LINEAR : GU_NEAREST);
            /* PSP textures stop at 512 pixels. Split the original 1024-wide
               artwork at the tile boundary, preserving authored screen UVs. */
            for (tile = 0; tile < t->tile_count; ++tile) {
                float left = tile * t->tile_width, right = left + t->tile_width;
                float a = fmaxf(u1, left), b = fminf(u2, right);
                float x1, x2;
                if (b <= a || u2 <= u1) continue;
                x1 = q->x1 + (q->x2 - q->x1) * (a - u1) / (u2 - u1);
                x2 = q->x1 + (q->x2 - q->x1) * (b - u1) / (u2 - u1);
                sceGuTexImage(0, t->tile_width, t->height, t->tile_width,
                    t->pixels + tile * t->tile_width * t->height);
                sprite(x1, q->y1, x2, q->y2, a - left, v1, b - left, v2, d->color);
            }
        }
    }
    sceGuFinish(); sceGuSync(0, 0);
    recvxGameBootRenderConsume();
}
void recvxGameBootDisplaySwap(void) { scene_buffer = sceGuSwapBuffers(); }
void recvxGameBootDisplayShutdown(void) { sceGuTerm(); }
