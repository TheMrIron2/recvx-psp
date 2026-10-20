#ifndef RECVX_GAME_BOOT_PLAYER_REMAINING_H
#define RECVX_GAME_BOOT_PLAYER_REMAINING_H
#include "ps2/veronica/prog/types.h"
void recvxGameBootPlayerRemainingPrepare(unsigned int kind);
O_WORK *recvxPlayerPose(BH_PWORK *actor, unsigned int joint);
unsigned int recvxPlayerMotionFrames(unsigned int index);
float recvxPlayerGround(int index);
void recvxPlayerFixPrepare(BH_PWORK *actor, const char *route);
void recvxPlayerEyePrepare(void);
void recvxPlayerHitPrepare(const GA_WORK *attack);
void recvxPlayerEnemiesPrepare(void);
void recvxPlayerBulletPrepare(void);
void recvxPlayerCapsulesPrepare(BH_PWORK *actor);
#endif
