#include <stddef.h>
#include <string.h>

// Match the original include order before Ninja aliases the peripheral record
#include "ps2/veronica/prog/types.h"
#include <ninja.h>

#include "psp/engine_pad_adapter.h"

_Static_assert(offsetof(NJS_PERIPHERAL, on) == 8,
               "original pad on offset changed");
_Static_assert(offsetof(NJS_PERIPHERAL, press) == 16,
               "original pad press offset changed");

// DualShock 2 masks follow the Vita portability shim
enum {
    DS_SELECT = 0x0001, DS_L3 = 0x0002, DS_R3 = 0x0004, DS_START = 0x0008,
    DS_UP = 0x0010, DS_RIGHT = 0x0020, DS_DOWN = 0x0040, DS_LEFT = 0x0080,
    DS_L2 = 0x0100, DS_R2 = 0x0200, DS_L1 = 0x0400, DS_R1 = 0x0800,
    DS_TRIANGLE = 0x1000, DS_CIRCLE = 0x2000, DS_CROSS = 0x4000,
    DS_SQUARE = 0x8000
};

static PDS_PERIPHERAL engine_peripheral;
static uint32_t previous_raw_buttons;

void recvxEnginePadAdapterReset(void)
{
    memset(&engine_peripheral, 0, sizeof(engine_peripheral));
    previous_raw_buttons = 0;
}

int recvxEnginePadAdapterUpdate(const RecvxInputState *input)
{
    uint32_t raw = 0;
    if (input == NULL)
        return -1;

    if (input->buttons & RECVX_BUTTON_SELECT) raw |= DS_SELECT;
    if (input->buttons & RECVX_BUTTON_START) raw |= DS_START;
    if (input->buttons & RECVX_BUTTON_UP) raw |= DS_UP;
    if (input->buttons & RECVX_BUTTON_RIGHT) raw |= DS_RIGHT;
    if (input->buttons & RECVX_BUTTON_DOWN) raw |= DS_DOWN;
    if (input->buttons & RECVX_BUTTON_LEFT) raw |= DS_LEFT;
    if (input->buttons & RECVX_BUTTON_LTRIGGER) raw |= DS_L1;
    if (input->buttons & RECVX_BUTTON_RTRIGGER) raw |= DS_R1;
    if (input->buttons & RECVX_BUTTON_TRIANGLE) raw |= DS_TRIANGLE;
    if (input->buttons & RECVX_BUTTON_CIRCLE) raw |= DS_CIRCLE;
    if (input->buttons & RECVX_BUTTON_CROSS) raw |= DS_CROSS;
    if (input->buttons & RECVX_BUTTON_SQUARE) raw |= DS_SQUARE;

    memset(&engine_peripheral, 0, sizeof(engine_peripheral));
    engine_peripheral.on = raw;
    engine_peripheral.press = raw & ~previous_raw_buttons;
    engine_peripheral.release = previous_raw_buttons & ~raw;
    engine_peripheral.off = engine_peripheral.release;
    engine_peripheral.old = previous_raw_buttons;
    engine_peripheral.x1 = (Sint16)((int)input->analog_x - 128);
    engine_peripheral.y1 = (Sint16)((int)input->analog_y - 128);
    engine_peripheral.l = (input->buttons & RECVX_BUTTON_LTRIGGER) ? 255 : 0;
    engine_peripheral.r = (input->buttons & RECVX_BUTTON_RTRIGGER) ? 255 : 0;
    previous_raw_buttons = raw;
    return 0;
}

const NJS_PERIPHERAL *njGetPeripheral(Uint32 port)
{
    return port == 0 ? &engine_peripheral : NULL;
}
