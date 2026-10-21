#ifndef RECVX_GAME_BOOT_ENEMY_SOUND_H
#define RECVX_GAME_BOOT_ENEMY_SOUND_H
#include "psp/game_boot.h"
void recvxGameBootEnemySoundReset(void);
void recvxGameBootEnemySpatial(NJS_POINT3 *position,char *pan,char *volume,float *distance);
int recvxGameBootSoundInitScript(void);
#endif
