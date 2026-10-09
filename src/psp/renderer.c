/* GU setup follows the PSPSDK samples. Geometry comes from the room's spatial
 * records, not the PS2 GS/VU renderer or the Ninja model chunk streams.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include "psp/renderer.h"

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define BUFFER_WIDTH 512
#define FRAME_BYTES (BUFFER_WIDTH * SCREEN_HEIGHT * 4)

static unsigned int command_list[16384] __attribute__((aligned(64)));
static void *draw_buffer;
static ScePspFVector3 center, focus;
static float yaw, pitch, distance, radius;
static uint64_t previous_time;
static int initialized;
static int display_mode;

static void reset_camera(const RecvxScene *scene, const RecvxRoomMesh *mesh)
{
    const float *position = display_mode != 3 && mesh && mesh->vertices ? mesh->center : scene->center;
    radius = display_mode != 3 && mesh && mesh->vertices ? mesh->radius : scene->radius;
    focus.x = position[0];
    focus.y = position[1];
    focus.z = position[2];
    center = focus;
    yaw = 0.85f;
    pitch = 0.6f;
    distance = radius * 3.0f;
}

int recvxRendererStart(const RecvxScene *scene, const RecvxRoomMesh *mesh)
{
    if (scene == NULL || scene->vertices == NULL || initialized)
        return -1;
    if (sceGuInit() < 0)
        return -1;
    initialized = 1;
    draw_buffer = NULL;
    sceGuStart(GU_DIRECT, command_list);
    sceGuDrawBuffer(GU_PSM_8888, draw_buffer, BUFFER_WIDTH);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void *)FRAME_BYTES, BUFFER_WIDTH);
    sceGuDepthBuffer((void *)(FRAME_BYTES * 2), BUFFER_WIDTH);
    sceGuOffset(2048 - SCREEN_WIDTH / 2, 2048 - SCREEN_HEIGHT / 2);
    sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuShadeModel(GU_FLAT);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceKernelDcacheWritebackRange(scene->vertices,
                                 scene->vertex_count * sizeof(*scene->vertices));
    if (mesh && mesh->vertices)
        sceKernelDcacheWritebackRange(mesh->vertices, mesh->vertex_count * 3 * sizeof(*mesh->vertices));
    sceDisplayWaitVblankStartCB();
    sceGuDisplay(GU_TRUE);
    pspDebugScreenSetColorMode(GU_PSM_8888);
    pspDebugScreenEnableBackColor(0);
    display_mode = mesh && mesh->vertices ? 1 : 3;
    reset_camera(scene, mesh);
    previous_time = sceKernelGetSystemTimeWide();
    return 0;
}

void recvxRendererFrame(const RecvxScene *scene, const RecvxRoomMesh *mesh, const char *room_name,
                        const SceCtrlData *pad, unsigned int pressed)
{
    uint64_t now = sceKernelGetSystemTimeWide();
    float dt = fminf(0.05f, (float)(now - previous_time) * 0.000001f);
    float analog_x = ((int)pad->Lx - 128) / 128.0f;
    float analog_y = ((int)pad->Ly - 128) / 128.0f;
    ScePspFVector3 eye, up = {0, 1, 0};
    previous_time = now;

    if ((pressed & PSP_CTRL_SELECT) && mesh && mesh->vertices) {
        display_mode = (display_mode + 1) % 4;
        reset_camera(scene, mesh);
    }
    if (pressed & PSP_CTRL_TRIANGLE)
        reset_camera(scene, mesh);
    if (pad->Buttons & PSP_CTRL_LEFT)
        yaw -= dt * 1.7f;
    if (pad->Buttons & PSP_CTRL_RIGHT)
        yaw += dt * 1.7f;
    if (pad->Buttons & PSP_CTRL_UP)
        pitch += dt;
    if (pad->Buttons & PSP_CTRL_DOWN)
        pitch -= dt;
    pitch = fmaxf(0.12f, fminf(1.4f, pitch));
    yaw = fmodf(yaw, 6.283185307f);
    if (pad->Buttons & PSP_CTRL_LTRIGGER)
        distance *= 1.0f + dt;
    if (pad->Buttons & PSP_CTRL_RTRIGGER)
        distance *= 1.0f - dt;
    distance = fmaxf(radius * 0.2f, fminf(radius * 12, distance));
    if (fabsf(analog_x) < 0.16f)
        analog_x = 0;
    if (fabsf(analog_y) < 0.16f)
        analog_y = 0;
    center.x += (analog_x * cosf(yaw) + analog_y * sinf(yaw)) * radius * dt;
    center.z += (-analog_x * sinf(yaw) + analog_y * cosf(yaw)) * radius * dt;
    /* Keep repeated panning within a useful range of the actual room. */
    center.x = fmaxf(focus.x - radius * 4, fminf(focus.x + radius * 4, center.x));
    center.z = fmaxf(focus.z - radius * 4, fminf(focus.z + radius * 4, center.z));
    eye.x = center.x + sinf(yaw) * cosf(pitch) * distance;
    eye.y = center.y + sinf(pitch) * distance;
    eye.z = center.z + cosf(yaw) * cosf(pitch) * distance;

    sceGuStart(GU_DIRECT, command_list);
    sceGuClearColor(0xff18120e);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(50, (float)SCREEN_WIDTH / SCREEN_HEIGHT,
                      radius * 0.01f, radius * 32);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &center, &up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    if ((display_mode == 0 || display_mode == 2) && mesh && mesh->vertices)
        sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        (int)mesh->vertex_count, NULL, mesh->vertices);
    if (display_mode == 1 && mesh && mesh->wire_vertices) {
        unsigned int offset = 0, remaining = mesh->vertex_count * 2;
        sceGuDisable(GU_DEPTH_TEST);
        /* GE primitive counts are 16-bit; retain line pairs across batches. */
        while (remaining != 0) {
            unsigned int batch = remaining > 32768 ? 32768 : remaining;
            sceGumDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                            (int)batch, NULL, mesh->wire_vertices + offset);
            offset += batch;
            remaining -= batch;
        }
        sceGuEnable(GU_DEPTH_TEST);
    }
    if (display_mode >= 2) {
        if (display_mode == 2)
            sceGuDisable(GU_DEPTH_TEST);
        sceGumDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        (int)scene->vertex_count, NULL, scene->vertices);
        sceGuEnable(GU_DEPTH_TEST);
    }
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

    /* Write the HUD into the finished draw buffer before swapping it. */
    pspDebugScreenSetOffset((int)(uintptr_t)draw_buffer);
    pspDebugScreenSetTextColor(0xffffffff);
    pspDebugScreenSetXY(1, 1);
    pspDebugScreenPrintf("%s   %s", room_name,
                         display_mode == 3 ? "Spatial viewer" :
                         display_mode == 1 ? "Room model wireframe" : "Room model solid");
    pspDebugScreenSetXY(1, 2);
    if (display_mode != 3 && mesh)
        pspDebugScreenPrintf("%u models  %u triangles%s", (unsigned int)mesh->model_count,
                             (unsigned int)mesh->triangle_count, display_mode == 2 ? " + spatial overlay" : "");
    else
        pspDebugScreenPrintf("%u bounds  %u markers  %u lines",
                         (unsigned int)scene->box_count,
                         (unsigned int)scene->marker_count,
                         (unsigned int)(scene->vertex_count / 2));
    pspDebugScreenSetXY(1, 29);
    pspDebugScreenPrintf("SELECT: solid / wire / model + bounds / spatial");
    pspDebugScreenSetXY(1, 31);
    pspDebugScreenPrintf("D-pad orbit  Stick pan  L/R zoom  TRIANGLE reset");
    pspDebugScreenSetXY(1, 32);
    pspDebugScreenPrintf("SQUARE checks  START/HOME exit");
    sceDisplayWaitVblankStartCB();
    draw_buffer = sceGuSwapBuffers();
}

void recvxRendererStop(void)
{
    if (!initialized)
        return;
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    initialized = 0;
    pspDebugScreenSetOffset(0);
    pspDebugScreenEnableBackColor(1);
    pspDebugScreenInit();
}
