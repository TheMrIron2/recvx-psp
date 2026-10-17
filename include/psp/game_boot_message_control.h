#ifndef RECVX_GAME_BOOT_MESSAGE_CONTROL_H
#define RECVX_GAME_BOOT_MESSAGE_CONTROL_H
#include "ps2/veronica/prog/types.h"
void recvxGameBootMessageControlPrepare(int mode);
void recvxGameBootItemNamePrepare(const NJS_POINT2 *position, int id, int count);
void recvxGameBootMessageChoicesReady(void);
void recvxGameBootMessagePositionCheck(const NJS_POINT2 *position);
#endif
