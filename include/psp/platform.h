#ifndef RECVX_PSP_PLATFORM_H
#define RECVX_PSP_PLATFORM_H

#include <stdint.h>

enum RecvxButton {
    RECVX_BUTTON_SELECT = 1u << 0,
    RECVX_BUTTON_START = 1u << 1,
    RECVX_BUTTON_UP = 1u << 2,
    RECVX_BUTTON_RIGHT = 1u << 3,
    RECVX_BUTTON_DOWN = 1u << 4,
    RECVX_BUTTON_LEFT = 1u << 5,
    RECVX_BUTTON_LTRIGGER = 1u << 6,
    RECVX_BUTTON_RTRIGGER = 1u << 7,
    RECVX_BUTTON_TRIANGLE = 1u << 8,
    RECVX_BUTTON_CIRCLE = 1u << 9,
    RECVX_BUTTON_CROSS = 1u << 10,
    RECVX_BUTTON_SQUARE = 1u << 11
};

typedef struct RecvxInputState {
    uint32_t buttons;
    uint32_t pressed;
    uint32_t released;
    uint32_t repeat;
    uint8_t analog_x;
    uint8_t analog_y;
} RecvxInputState;

void recvxPlatformInputInit(void);
int recvxPlatformReadInput(RecvxInputState *input);
int recvxPlatformSetClockFrequency(int cpu_mhz, int pll_mhz, int bus_mhz);
int recvxPlatformGetCpuClockFrequency(void);
int recvxPlatformGetBusClockFrequency(void);
uint64_t recvxPlatformTimeUs(void);
void recvxPlatformDelayUs(uint32_t duration_us);
void recvxPlatformWaitVblank(void);
void recvxPlatformCheckCallbacks(void);
void recvxPlatformFlushCaches(void);

#endif
