#ifndef RECVX_GAME_BOOT_SKIN_H
#define RECVX_GAME_BOOT_SKIN_H

#include <stddef.h>

typedef struct RecvxBootSkinStats { unsigned int prepare_us,blend_us,vertices; } RecvxBootSkinStats;
/* Cumulative across room resets, for frame-window profiling. */
RecvxBootSkinStats recvxGameBootSkinStats(void);
void recvxGameBootSkinReset(void);
/* Returns zero for pointers outside the current owned vertex stream storage. */
size_t recvxGameBootSkinRemaining(const void *pointer);

#endif
