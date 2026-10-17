#ifndef RECVX_GAME_BOOT_INVENTORY_CONTROL_H
#define RECVX_GAME_BOOT_INVENTORY_CONTROL_H
#include "psp/game_boot.h"
void recvxGameBootInventoryControlPrepare(void);
void recvxGameBootInventoryWorkPrepare(S_WORK *work);
void recvxGameBootInventorySpritePrepare(const PARTS *part);
void recvxGameBootInventoryNamePrepare(const NJS_POINT2 *, int, int, int, float);
void recvxGameBootInventoryActionPrepare(S_WORK *, int);
void recvxGameBootInventorySlotPrepare(const unsigned int *);
void recvxGameBootInventoryCombinePrepare(short, const unsigned int *, const unsigned int *, int);
void recvxGameBootInventoryEventItemPrepare(void);
#endif
