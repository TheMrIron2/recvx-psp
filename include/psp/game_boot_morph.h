#ifndef RECVX_GAME_BOOT_MORPH_H
#define RECVX_GAME_BOOT_MORPH_H

#include <stddef.h>

/* Morph output remains live until the next use of that object slot or reset.
   Range queries expose only initialized stream bytes, never allocator padding. */
void recvxGameBootMorphReset(void);
size_t recvxGameBootMorphRemaining(const void *pointer);

#endif
