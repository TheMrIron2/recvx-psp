#include <pspctrl.h>
#include <pspdebug.h>
#include <pspkernel.h>

#include "flag.h"

PSP_MODULE_INFO("Resident Evil: Code Veronica X", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(1024);

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    sceKernelExitGame();
    return 0;
}

// check an unchanged decompiled module
static int check_game_flags(void)
{
    unsigned int flags[2] = {0, 0};
    unsigned int bit;

    for (bit = 0; bit < 64; ++bit) {
        bhStFlg(flags, bit);
        if (!bhCkFlg(flags, bit))
            return 0;
    }
    if (flags[0] != 0xffffffffu || flags[1] != 0xffffffffu)
        return 0;

    for (bit = 0; bit < 64; ++bit) {
        bhCrFlg(flags, bit);
        if (bhCkFlg(flags, bit))
            return 0;
    }
    if (flags[0] != 0 || flags[1] != 0)
        return 0;

    bhStFlg(flags, 0);
    bhStFlg(flags, 63);
    return flags[0] == 0x80000000u && flags[1] == 1u;
}

int main(void)
{
    SceCtrlData pad;
    int callback;

    pspDebugScreenInit();
    callback = sceKernelCreateCallback("Exit", exit_callback, NULL);
    if (callback < 0 || sceKernelRegisterExitCallback(callback) < 0) {
        pspDebugScreenPrintf("Could not register HOME callback.\n");
    }
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    pspDebugScreenPrintf("Resident Evil: Code Veronica X\n");
    pspDebugScreenPrintf("PSP bootstrap build\n\n");
    pspDebugScreenPrintf("PSP startup and display initialized.\n");
    pspDebugScreenPrintf("Original game flag routines: %s\n\n", check_game_flags() ? "PASS" : "FAIL");
    pspDebugScreenPrintf("The game engine is not running yet.\n");
    pspDebugScreenPrintf("Press START or HOME to exit.\n");

    for (;;) {
        if (sceCtrlPeekBufferPositive(&pad, 1) > 0 &&
            (pad.Buttons & PSP_CTRL_START))
            break;
        sceKernelDelayThreadCB(16667);
    }

    sceKernelExitGame();
    return 0;
}
