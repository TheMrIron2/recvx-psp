#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include "psp/game_boot.h"
#include "psp/original_clock.h"
#include "ps2/veronica/prog/main.h"

PSP_MODULE_INFO("RECVX GAME", PSP_MODULE_USER, 0, 7);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024);
static volatile int exit_requested;
static int exit_callback(int a, int b, void *context)
{ (void)a; (void)b; (void)context; exit_requested = 1; return 0; }

int main(int argc, char **argv)
{
    const char *data_directory = argc > 1 ? argv[1] : "data";
    void *arena = memalign(64, 12u * 1024u * 1024u);
    int callback;
    unsigned int previous_vblank;
    pspDebugScreenInit();
    callback = sceKernelCreateCallback("RECVX Exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(callback);
    recvxPlatformSetClockFrequency(333, 333, 166);
    recvxPlatformInputInit();
    if (!arena || recvxGameBootInit(data_directory, arena, 12u * 1024u * 1024u)) {
        pspDebugScreenPrintf("Game startup failed: engine arena unavailable.\n");
    } else {
        recvxGameBootDisplayInit();
        previous_vblank = sceDisplayGetVcount();
        while (!exit_requested) {
            RecvxInputState input;
            if (recvxPlatformReadInput(&input) <= 0) {
                recvxPlatformCheckCallbacks(); recvxPlatformWaitVblank(); continue;
            }
            if (recvxGameBootTick(&input)) break;
            /* Original PS2 presentation advances once per two vblanks. */
            recvxPlatformWaitVblank(); recvxPlatformWaitVblank();
            recvxGameBootDisplaySwap();
            {
                unsigned int vblank = sceDisplayGetVcount();
                recvxOriginalEngineClockAdvance(vblank - previous_vblank, 1);
                previous_vblank = vblank;
            }
            recvxPlatformCheckCallbacks();
        }
        recvxGameBootTexturesShutdown(); recvxGameBootDisplayShutdown();
        if (recvxGameBootFault()) {
            pspDebugScreenInit(); pspDebugScreenClear();
            pspDebugScreenPrintf("Resident Evil: Code Veronica X\n\nGame boot stopped at:\n%s\n\n", recvxGameBootFault());
            pspDebugScreenPrintf("Task mask: %08x  paused: %08x\n", sys->tk_flg, sys->ts_flg);
            pspDebugScreenPrintf("Next engine service needs a PSP implementation.\nSee boot.log beside EBOOT.PBP.\n\nHOME to exit.\n");
        }
    }
    while (!exit_requested) { sceKernelDelayThreadCB(20000); }
    free(arena); sceKernelExitGame(); return 0;
}
