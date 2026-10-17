#ifndef RECVX_GAME_BOOT_SKIN_H
#define RECVX_GAME_BOOT_SKIN_H

#include <stddef.h>

void recvxGameBootSkinReset(void);
/* Returns zero for pointers outside the current owned vertex stream storage. */
size_t recvxGameBootSkinRemaining(const void *pointer);

#endif
