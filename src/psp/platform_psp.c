#include <stddef.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>

#include "psp/platform.h"

static uint32_t previous_buttons;

void recvxPlatformInputInit(void)
{
    previous_buttons = 0;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

int recvxPlatformReadInput(RecvxInputState *input)
{
    SceCtrlData pad;
    uint32_t buttons = 0;
    int result;

    if (input == NULL)
        return -1;
    result = sceCtrlPeekBufferPositive(&pad, 1);
    if (result <= 0)
        return result;

    if (pad.Buttons & PSP_CTRL_SELECT) buttons |= RECVX_BUTTON_SELECT;
    if (pad.Buttons & PSP_CTRL_START) buttons |= RECVX_BUTTON_START;
    if (pad.Buttons & PSP_CTRL_UP) buttons |= RECVX_BUTTON_UP;
    if (pad.Buttons & PSP_CTRL_RIGHT) buttons |= RECVX_BUTTON_RIGHT;
    if (pad.Buttons & PSP_CTRL_DOWN) buttons |= RECVX_BUTTON_DOWN;
    if (pad.Buttons & PSP_CTRL_LEFT) buttons |= RECVX_BUTTON_LEFT;
    if (pad.Buttons & PSP_CTRL_LTRIGGER) buttons |= RECVX_BUTTON_LTRIGGER;
    if (pad.Buttons & PSP_CTRL_RTRIGGER) buttons |= RECVX_BUTTON_RTRIGGER;
    if (pad.Buttons & PSP_CTRL_TRIANGLE) buttons |= RECVX_BUTTON_TRIANGLE;
    if (pad.Buttons & PSP_CTRL_CIRCLE) buttons |= RECVX_BUTTON_CIRCLE;
    if (pad.Buttons & PSP_CTRL_CROSS) buttons |= RECVX_BUTTON_CROSS;
    if (pad.Buttons & PSP_CTRL_SQUARE) buttons |= RECVX_BUTTON_SQUARE;

    input->buttons = buttons;
    input->pressed = buttons & ~previous_buttons;
    input->released = previous_buttons & ~buttons;
    input->repeat = input->pressed;
    input->analog_x = pad.Lx;
    input->analog_y = pad.Ly;
    previous_buttons = buttons;
    return 1;
}

int recvxPlatformSetClockFrequency(int cpu_mhz, int pll_mhz, int bus_mhz)
{
    return scePowerSetClockFrequency(cpu_mhz, pll_mhz, bus_mhz);
}

int recvxPlatformGetCpuClockFrequency(void)
{
    return scePowerGetCpuClockFrequencyInt();
}

int recvxPlatformGetBusClockFrequency(void)
{
    return scePowerGetBusClockFrequencyInt();
}

uint64_t recvxPlatformTimeUs(void)
{
    return sceKernelGetSystemTimeWide();
}

void recvxPlatformDelayUs(uint32_t duration_us)
{
    sceKernelDelayThreadCB(duration_us);
}

void recvxPlatformWaitVblank(void)
{
    sceDisplayWaitVblankStart();
}

void recvxPlatformCheckCallbacks(void)
{
    sceKernelCheckCallback();
}

void recvxPlatformFlushCaches(void)
{
    sceKernelDcacheWritebackInvalidateAll();
    sceKernelIcacheInvalidateAll();
}
