#include <math.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include "psp/game_boot.h"
#include "psp/game_boot_game_remaining.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_movie.h"
#include "psp/engine_original_view.h"

#define SHADOW_OFFSET (5u * 512u * 272u * 2u)
static void *scene_buffer;
static int frame_drawn, model_depth_drawn;
static float model_depth_a, model_depth_b;
static RecvxBootGuStats gu_stats;
RecvxBootGuStats recvxGameBootGuStats(void){return gu_stats;}
static unsigned int microseconds(void)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}
static unsigned int display_list[262144] __attribute__((aligned(16)));
typedef struct { float u, v; unsigned int color; float x, y, z; } Vertex;

static unsigned int abgr(unsigned int argb)
{ return (argb & 0xff00ff00u) | ((argb >> 16) & 255) | ((argb & 255) << 16); }

int recvxGameBootDisplayInit(void)
{
    gu_stats=(RecvxBootGuStats){0};
    scene_buffer = NULL;
    frame_drawn = 0;
    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, 512);
    sceGuDispBuffer(480, 272, (void *)(512 * 272 * 4), 512);
    sceGuDepthBuffer((void *)(2 * 512 * 272 * 4), 512);
    sceGuDepthRange(65535, 0);
    sceGuDepthFunc(GU_GEQUAL);
    /* Strip expansion preserves outward-facing triangle order. Selecting CW
       here exposes model interiors by culling the exterior faces. */
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
                   float u1, float v1, float u2, float v2, unsigned int color, float depth)
{
    Vertex *v = sceGuGetMemory(2 * sizeof(*v));
    v[0] = (Vertex){u1, v1, abgr(color), x1 * 0.75f, y1 * (272.0f / 480), depth};
    v[1] = (Vertex){u2, v2, abgr(color), x2 * 0.75f, y2 * (272.0f / 480), depth};
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 |
        GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, v);
}

/* UI priorities are view-space distances. Opaque UI and models share this
   reversed GE depth across ordering-table flushes. */
static float overlay_depth(float distance)
{

    if (!isfinite(distance))
        recvxGameBootUnsupported("invalid UI overlay depth");
    if (!distance) return 65534;
    /* The title's pulsing plate starts at reciprocal Z zero; its shadow
       subtracts .001 and legitimately produces a signed distance. Map it
       through the same projection and clamp the result to the GE range. */
    return fmaxf(0, fminf(65534, 32767.5f * (1 + model_depth_a - model_depth_b / distance)));
}
static void gradient_polygon(const RecvxBootDraw *draw)
{
    static const unsigned int order[6] = {0, 1, 2, 0, 2, 3};
    Vertex *vertices = sceGuGetMemory(6 * sizeof(*vertices));
    for (unsigned int i = 0; i < 6; ++i) {
        unsigned int corner = order[i];
        vertices[i] = (Vertex){draw->uv[corner].x, draw->uv[corner].y, abgr(draw->vertex_color[corner]),
            draw->points[corner].x * 0.75f, draw->points[corner].y * (272.0f / 480), overlay_depth(draw->depth)};
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
    model_depth_drawn = 1;
    model_depth_a = frame->projection[10];
    model_depth_b = frame->projection[14];
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadMatrix((const ScePspFMatrix4 *)frame->projection);
    sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
    sceGuEnable(GU_DEPTH_TEST); sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0, 255);
    const RecvxBootTexture *bound_texture=NULL;
    for (size_t i = 0; i < frame->batch_count; ++i) {
        const RecvxBootRenderBatch *batch = &frame->batches[i];
        if (batch->kind >= RECVX_RENDER_SHADOW_START) {
            shadow(frame, batch);
            bound_texture=NULL; /* Shadow passes replace texture format/state. */
            continue;
        }
        sceGuScissor(batch->scissor[0], batch->scissor[1], batch->scissor[2], batch->scissor[3]);
        if (batch->kind == RECVX_RENDER_HALF_FOG) {
            /* Finite powers of two keep the GE's (far + viewZ)/(far-near)
               exactly 1/2 for the validated authored sprite depth range. */
            sceGuFog(-0x1p64f, 0x1p64f, batch->fog_color);
            sceGuEnable(GU_FOG);
        } else if (batch->fog_enabled) {
            sceGuFog(batch->fog_near, batch->fog_far, batch->fog_color);
            sceGuEnable(GU_FOG);
        } else sceGuDisable(GU_FOG);
        const RecvxBootTexture *texture = batch->texture;
        int screen_space = batch->kind == RECVX_RENDER_SCREEN_TRIANGLES ||
            batch->kind == RECVX_RENDER_SCREEN_OFFSET || batch->kind == RECVX_RENDER_SCREEN_HALF_BASE;
        sceGuEnable(GU_DEPTH_TEST); sceGuDepthMask(screen_space ? GU_TRUE : GU_FALSE);
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
            if (batch->kind == RECVX_RENDER_SCREEN_OFFSET) {
                sceGuTexEnvColor(batch->fog_color);
                sceGuTexFunc(GU_TFX_BLEND,GU_TCC_RGBA);
            } else {
                /* GE TEXFUNC bit 16 doubles RGB after modulation, independently
                   of alpha. sceGuTexFunc passes this bit through to TEXFUNC. */
                sceGuTexFunc(GU_TFX_MODULATE | (batch->sampler&RECVX_RENDER_COLOR_DOUBLE?0x10000:0),GU_TCC_RGBA);
            }
            sceGuEnable(GU_TEXTURE_2D);
            if(bound_texture!=texture){
                sceGuTexMode(texture->rgba8888?GU_PSM_8888:GU_PSM_T8,0,0,!texture->rgba8888);
                if(!texture->rgba8888){sceGuClutMode(GU_PSM_8888,0,255,0);sceGuClutLoad(32,texture->palette);}
                sceGuTexImage(0,texture->width,texture->height,texture->tile_width,texture->pixels);
                sceGuTexFlush();bound_texture=texture;
            }
            sceGuTexScale(texture->rgba8888?texture->capture_scale_u:1, texture->rgba8888?texture->capture_scale_v:1); sceGuTexOffset(0, 0);
            if(texture->rgba8888)sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGB);
            sceGuTexWrap(batch->sampler & 1 ? GU_CLAMP : GU_REPEAT, batch->sampler & 2 ? GU_CLAMP : GU_REPEAT);
            sceGuTexFilter(batch->sampler & 4 ? GU_LINEAR : GU_NEAREST, batch->sampler & 4 ? GU_LINEAR : GU_NEAREST);
        } else sceGuDisable(GU_TEXTURE_2D);
        if (screen_space) {
            /* Retail Ps2DrawOTag uses alpha-fail FB_ONLY: translucent pixels
               change color without writing Z. A near full-screen effect must
               not hide actors submitted after it. GE has no FB_ONLY mode,
               so draw color first, then write only fully opaque pixels' Z. */
            sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                batch->count, NULL, frame->vertices + batch->first);
            int opaque = 0;
            for (unsigned int j = 0; j < batch->count; ++j)
                if ((frame->vertices[batch->first + j].color >> 24) == 255u) { opaque = 1; break; }
            if (opaque) {
                sceGuDepthMask(GU_FALSE);
                sceGuAlphaFunc(GU_EQUAL, 255, 255);
                sceGuPixelMask(0xffffffffu);
                sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                    batch->count, NULL, frame->vertices + batch->first);
                sceGuPixelMask(0);
                sceGuAlphaFunc(GU_GREATER, 0, 255);
            }
        } else sceGumDrawArray(batch->kind == RECVX_RENDER_LINES ? GU_LINES : GU_TRIANGLES,
            GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            batch->count, NULL, frame->vertices + batch->first);
    }
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE); sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

static void movie_frame(const uint32_t *pixels)
{
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_FOG); sceGuDisable(GU_BLEND);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 512, 512, pixels);
    /* Decoder alpha is immaterial: movies replace the opaque display. */
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0); sceGuTexFlush();
    for (unsigned int x = 0; x < 480; x += 64) {
        unsigned int right = x + 64 < 480 ? x + 64 : 480;
        Vertex *vertices = sceGuGetMemory(2 * sizeof(*vertices));
        vertices[0] = (Vertex){x, 0, 0xffffffffu, x, 0, 0};
        vertices[1] = (Vertex){right, 272, 0xffffffffu, right, 272, 0};
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 |
            GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, vertices);
    }
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

void recvxGameBootPresent(void)
{
    unsigned int started=microseconds();
    size_t count, i;
    const RecvxBootDraw *draws = recvxGameBootDraws(&count);
    const RecvxBootRenderFrame *frame = recvxGameBootRenderFrame();
    if (65536u + frame->batch_count * 512u + count * 1024u > sizeof(display_list))
        recvxGameBootUnsupported("original GU command list exceeds capacity");
    sceKernelDcacheWritebackAll();
    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(frame->vertex_count ? abgr(recvxEngineOriginalViewBackgroundColor()) : 0xff000000);
    sceGuClearDepth(0);
    if (!frame_drawn || recvxGameBootCaptureActive()) {
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        model_depth_drawn = 0;
    }
    frame_drawn = 1;
    const uint32_t *video = recvxGameBootMovieGetFrame();
    if (video) movie_frame(video);
    else models(frame);
    sceGuScissor(0, 0, 480, 272);
    if (!model_depth_drawn) {
        float near, far;
        recvxEngineOriginalViewClipPlanes(&near, &far);
        near=-near;far=-far;
        if (!isfinite(near)||!isfinite(far)||near<=0||far<=near)
            recvxGameBootUnsupported("invalid UI depth projection");
        model_depth_a=-(far+near)/(far-near);
        model_depth_b=-2*far*near/(far-near);
        model_depth_drawn=1;
    }
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    /* Retail alpha-fail FB_ONLY writes color for translucent pixels and
       depth only for opaque pixels. The second pass masks color writes. */
    for (unsigned int pass=0;pass<2;++pass) {
        sceGuDepthMask(pass ? GU_FALSE : GU_TRUE);
        if(pass){sceGuEnable(GU_ALPHA_TEST);sceGuAlphaFunc(GU_EQUAL,255,255);sceGuPixelMask(0xffffffffu);}
        else {sceGuDisable(GU_ALPHA_TEST);sceGuPixelMask(0);}
        for (i = 0; i < count; ++i) {
            const RecvxBootDraw *d = &draws[i];
            const QUAD *q = &d->quad;
            const RecvxBootTexture *t = d->texture;
            if (!t) {
                sceGuDisable(GU_TEXTURE_2D);
                if (d->color_gradient) gradient_polygon(d);
                else sprite(q->x1, q->y1, q->x2, q->y2, 0, 0, 0, 0, d->color, overlay_depth(d->depth));
            } else {
                unsigned int tile;
                float u1 = q->u1 * t->width, u2 = q->u2 * t->width;
                float v1 = q->v1 * t->height, v2 = q->v2 * t->height;
                sceGuEnable(GU_TEXTURE_2D); sceGuTexMode(t->rgba8888?GU_PSM_8888:GU_PSM_T8, 0, 0, !t->rgba8888);
                sceGuTexFunc(GU_TFX_MODULATE,t->rgba8888?GU_TCC_RGB:GU_TCC_RGBA);
                sceGuTexScale(t->rgba8888?t->capture_scale_u:1,t->rgba8888?t->capture_scale_v:1);
                if(!t->rgba8888){sceGuClutMode(GU_PSM_8888, 0, 255, 0); sceGuClutLoad(32, t->palette);}
                sceGuTexFilter(d->filter ? GU_LINEAR : GU_NEAREST, d->filter ? GU_LINEAR : GU_NEAREST);
                if (d->color_gradient) {
                    if (t->tile_count != 1) recvxGameBootUnsupported("tiled rotated UI sprite is not ported");
                    sceGuTexImage(0,t->width,t->height,t->tile_width,t->pixels);
                    gradient_polygon(d);
                    continue;
                }
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
                    sprite(x1, q->y1, x2, q->y2, a - left, v1, b - left, v2, d->color, overlay_depth(d->depth));
                }
            }
        }
    }
    sceGuPixelMask(0);sceGuDisable(GU_ALPHA_TEST);sceGuDepthMask(GU_FALSE);
    sceGuFinish();
    unsigned int submitted=microseconds();
    sceGuSync(0,0);
    gu_stats.build_us+=submitted-started;
    gu_stats.wait_us+=microseconds()-submitted;
    recvxGameBootMovieFrameConsumed();
    recvxGameBootRenderConsume();
}
void recvxGameBootDisplayBeginFrame(void) { frame_drawn = model_depth_drawn = 0; }
void recvxGameBootDisplaySwap(void) { scene_buffer = sceGuSwapBuffers(); recvxGameBootDisplayBeginFrame(); }
void recvxGameBootDisplayShutdown(void) { sceGuTerm(); }

void recvxGameBootCaptureCopy(unsigned char *pixels)
{
    sceKernelDcacheWritebackInvalidateRange(pixels,512u*512u*4u);
    sceGuStart(GU_DIRECT,display_list);
    sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,(void *)(0x04000000u+(uintptr_t)scene_buffer),0,0,512,pixels);
    sceGuTexSync();sceGuTexFlush();sceGuFinish();sceGuSync(0,0);
    frame_drawn=0;
}
