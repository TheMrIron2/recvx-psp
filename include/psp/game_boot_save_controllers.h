#ifndef RECVX_GAME_BOOT_SAVE_CONTROLLERS_H
#define RECVX_GAME_BOOT_SAVE_CONTROLLERS_H
#include "ps2/veronica/prog/types.h"
void recvxGameBootRankingPrepare(int stage);
void recvxGameBootRankingScorePrepare(void);
void recvxGameBootRankingNumberPrepare(const NJS_POINT2 *position, int number);
void recvxGameBootTypewriterPrepare(int stage);
void recvxGameBootSaveMessagePrepare(void);
void recvxGameBootRankingRestorePrepare(void);
void recvxGameBootRankingTimePrepare(const NJS_POINT2 *position, unsigned int flags, int time, float priority);
#endif
