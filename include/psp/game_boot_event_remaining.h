#ifndef RECVX_GAME_BOOT_EVENT_REMAINING_H
#define RECVX_GAME_BOOT_EVENT_REMAINING_H
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
O_WORK *recvxEventPose(BH_PWORK *actor,unsigned int joint);
const WPN_TAB *recvxEventWeapon(unsigned int index);
void recvxGameBootEventRemainingPrepare(unsigned int opcode);
ETTY_WORK *recvxEventEnemyRecord(unsigned int index);
BH_PWORK *recvxEventEnemyWork(unsigned int index);
O_WRK *recvxEventObject(unsigned int index,int item);
O_WRK *recvxEventEffect(unsigned int index);
unsigned int recvxEventEffectId(unsigned int index);
BH_SCEWORK *recvxEventTask(unsigned int index);
static inline unsigned int recvxEventRead16(const unsigned char *p)
{return p[0]|((unsigned int)p[1]<<8);}
#endif
