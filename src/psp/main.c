#include <pspctrl.h>
#include <pspdebug.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/room.h"
#include "psp/renderer.h"
#include "psp/selftest.h"

PSP_MODULE_INFO("RECVX PSP", PSP_MODULE_USER, 0, 3);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(16 * 1024);

static RecvxScene scene;
static char room_name[17];

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    sceKernelExitGame();
    return 0;
}

static void show_checks(const char *data_directory)
{
    int flags = recvxCheckFlags();
    int memory = recvxCheckMemory();
    int expand = recvxCheckExpand();

    recvxSceneFree(&scene);

    pspDebugScreenClear();
    pspDebugScreenPrintf("Resident Evil: Code Veronica X\n");
    pspDebugScreenPrintf("PSP bootstrap build\n\n");
    pspDebugScreenPrintf("Original game flag routines: %s (%d)\n",
                         flags ? "FAIL" : "PASS", flags);
    pspDebugScreenPrintf("Original Ninja memory copies: %s (%d)\n",
                         memory ? "FAIL" : "PASS", memory);
    pspDebugScreenPrintf("Portable asset decompression: %s (%d)\n\n",
                         expand ? "FAIL" : "PASS", expand);
    pspDebugScreenPrintf("Decompression checks include invalid input.\n");
    pspDebugScreenPrintf("Loading room file...\n");
    {
        RecvxRoomFileInfo info;
        unsigned char *room_data = NULL;
        int result = recvxLoadRoomFile(data_directory, &info, &room_data);
        if (result == 1) {
            pspDebugScreenPrintf("Room check: copy data/ from make assets.\n\n");
        } else if (result != 0) {
            pspDebugScreenPrintf("Room check: FAIL (%d)\n\n", result);
        } else {
            pspDebugScreenPrintf("Room %s: PASS\n", info.name);
            pspDebugScreenPrintf("Expanded %u -> %u bytes; CRC32 %08x\n",
                                 (unsigned int)info.packed_size,
                                 (unsigned int)info.expanded_size,
                                 (unsigned int)info.crc32);
            pspDebugScreenPrintf("Cameras %u, lights %u, enemies %u\n\n",
                                 (unsigned int)info.room.counts[0],
                                 (unsigned int)info.room.counts[1],
                                 (unsigned int)info.room.counts[2]);
            result = recvxBuildRoomScene(room_data, info.expanded_size, &scene);
            if (result == 0) {
                memcpy(room_name, info.name, sizeof(room_name));
                pspDebugScreenPrintf("Room viewer: READY (%u bounds, %u markers)\n\n",
                                     (unsigned int)scene.box_count,
                                     (unsigned int)scene.marker_count);
            } else {
                pspDebugScreenPrintf("Room viewer: FAIL (%d)\n\n", result);
            }
        }
        free(room_data);
    }
    pspDebugScreenPrintf("The game engine is not running yet.\n\n");
    pspDebugScreenPrintf("Press CROSS to repeat the checks.\n");
    if (scene.vertices != NULL)
        pspDebugScreenPrintf("Press SQUARE for the 3D room viewer.\n");
    pspDebugScreenPrintf("Press START or HOME to exit.\n");
}

int main(int argc, char **argv)
{
    SceCtrlData pad;
    int callback;
    unsigned int previous_buttons = 0;
    int viewing = 0;
    char data_directory[512] = "data";

    if (argc > 0 && argv != NULL && argv[0] != NULL) {
        const char *last_slash = strrchr(argv[0], '/');
        if (last_slash != NULL && last_slash - argv[0] < 500)
            snprintf(data_directory, sizeof(data_directory), "%.*s/data",
                     (int)(last_slash - argv[0]), argv[0]);
    }

    pspDebugScreenInit();
    callback = sceKernelCreateCallback("Exit", exit_callback, NULL);
    if (callback < 0 || sceKernelRegisterExitCallback(callback) < 0) {
        pspDebugScreenPrintf("Could not register HOME callback.\n");
    }
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    show_checks(data_directory);

    for (;;) {
        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            unsigned int pressed = pad.Buttons & ~previous_buttons;
            previous_buttons = pad.Buttons;
            if (pressed & PSP_CTRL_START)
                break;
            if (pressed & PSP_CTRL_SQUARE) {
                if (viewing) {
                    recvxRendererStop();
                    viewing = 0;
                    show_checks(data_directory);
                } else if (scene.vertices != NULL) {
                    if (recvxRendererStart(&scene) == 0)
                        viewing = 1;
                    else
                        pspDebugScreenPrintf("Could not initialize the renderer.\n");
                }
            }
            if (!viewing && (pressed & PSP_CTRL_CROSS))
                show_checks(data_directory);
            if (viewing)
                recvxRendererFrame(&scene, room_name, &pad, pressed);
        }
        if (!viewing)
            sceKernelDelayThreadCB(16667);
        else
            sceKernelCheckCallback();
    }

    recvxRendererStop();
    recvxSceneFree(&scene);
    sceKernelExitGame();
    return 0;
}
