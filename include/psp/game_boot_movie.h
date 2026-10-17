#ifndef RECVX_GAME_BOOT_MOVIE_H
#define RECVX_GAME_BOOT_MOVIE_H
#include <stdint.h>

int recvxGameBootMovieInit(const char *directory);
void recvxGameBootMovieShutdown(void);
/* 480x272 ABGR8888, 512-pixel stride, backed by a 512x512 texture allocation.
 * The returned buffer is read-only until FrameConsumed, after GU sync.
 */
const uint32_t *recvxGameBootMovieGetFrame(void);
void recvxGameBootMovieFrameConsumed(void);
#endif
