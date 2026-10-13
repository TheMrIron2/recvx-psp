#include <math.h>
#include <pspgu.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include "psp/game_boot.h"

static unsigned int display_list[65536] __attribute__((aligned(16)));
typedef struct { float u, v; unsigned int color; float x, y, z; } Vertex;

static unsigned int abgr(unsigned int argb)
{ return (argb & 0xff00ff00u) | ((argb >> 16) & 255) | ((argb & 255) << 16); }

int recvxGameBootDisplayInit(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, 512);
    sceGuDispBuffer(480, 272, (void *)(512 * 272 * 4), 512);
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

void recvxGameBootPresent(void)
{
    size_t count, i;
    const RecvxBootDraw *draws = recvxGameBootDraws(&count);
    sceKernelDcacheWritebackAll();
    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(0xff000000); sceGuClear(GU_COLOR_BUFFER_BIT);
    for (i = 0; i < count; ++i) {
        const RecvxBootDraw *d = &draws[i];
        const QUAD *q = &d->quad;
        const RecvxBootTexture *t = d->texture;
        if (!t) {
            sceGuDisable(GU_TEXTURE_2D);
            sprite(q->x1, q->y1, q->x2, q->y2, 0, 0, 0, 0, d->color);
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
}
void recvxGameBootDisplaySwap(void) { sceGuSwapBuffers(); }
void recvxGameBootDisplayShutdown(void) { sceGuTerm(); }
