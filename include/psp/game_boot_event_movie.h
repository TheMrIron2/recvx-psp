#ifndef RECVX_GAME_BOOT_EVENT_MOVIE_H
#define RECVX_GAME_BOOT_EVENT_MOVIE_H

#define RECVX_GAME_BOOT_MOVIE_COUNT 22

/* PSP entry for original system.c::bhSysCallMovie. Texture surfaces remain
 * owned in RAM while the native movie service presents its decoded frames.
 */
void recvxGameBootSysCallMovie(void);

#endif
