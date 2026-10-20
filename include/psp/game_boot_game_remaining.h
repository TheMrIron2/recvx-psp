#ifndef RECVX_GAME_BOOT_GAME_REMAINING_H
#define RECVX_GAME_BOOT_GAME_REMAINING_H
#include "psp/game_boot.h"
void recvxGameBootFixedEventPrepare(int cut,int key);
void recvxGameBootEyeInitPrepare(void);
void recvxGameBootAntPrepare(BH_PWORK *actor);
void recvxGameBootScopePrepare(void);
void recvxGameBootSmallCapturePrepare(void);
void recvxGameBootDrawCaptureStrip(NJS_TEXTUREH_VTX *vertices,int count);
void recvxGameBootCaptureBegin(unsigned int height);
void recvxGameBootCaptureSubmitted(void);
void recvxGameBootCaptureCopy(unsigned char *pixels);
int recvxGameBootCaptureActive(void);
#endif
