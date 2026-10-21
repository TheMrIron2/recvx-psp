#ifndef RECVX_GAME_BOOT_EFFECT_CALLBACKS_H
#define RECVX_GAME_BOOT_EFFECT_CALLBACKS_H
#include "ps2/veronica/prog/main.h"
void recvxGameBootEffectCallbackPrepare(unsigned int kind,O_WRK *effect);
void recvxGameBootEffectQueueCheck(int *count,size_t capacity);
void recvxGameBootEffectCustomDrawPrepare(unsigned int kind,O_WRK *effect);
#endif
