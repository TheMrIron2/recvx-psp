#ifndef RECVX_GAME_BOOT_EFFECT_SPRITES_H
#define RECVX_GAME_BOOT_EFFECT_SPRITES_H
#include "psp/game_boot.h"
void recvxGameBootEffectDrawQueuePrepare(O_WRK **objects, int count);
void recvxGameBootEffectSpritePrepare(O_WRK *object, int textured);
void recvxGameBootEffectSpriteCheckMath(void);
void recvxGameBootEffectSpriteNormal(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c, NJS_VECTOR *normal);
#endif
