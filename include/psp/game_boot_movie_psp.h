#ifndef RECVX_GAME_BOOT_MOVIE_PSP_H
#define RECVX_GAME_BOOT_MOVIE_PSP_H
#include <stdint.h>

/* Converted video-only PSMF: 480x272 AVC, baseline, no reordered pictures.
   AVCODEC must already be loaded by the audio backend. This adapter owns only
   its MPEGBASE reference and decoder/ring resources. One movie at a time. */
int recvxGameBootMoviePspOpen(const char *path);
/* Caller owns a 64-byte-aligned 512*272*4-byte ABGR8888 buffer. Returns 1 for
   a real decoded frame, 0 after EOF drain, -1 on failure. PTS follows the converted stream
   CFR timeline (3003 ticks per actual decoded picture), in 90 kHz units. No buffer is retained by the API. */
/* Internally supplies four firmware CSC destinations; extra pictures are
   returned in order on later calls. Shares the SAS lock for firmware access. */
int recvxGameBootMoviePspDecode(void *pixels, uint64_t *pts90k);
void recvxGameBootMoviePspClose(void);
int recvxGameBootMoviePspError(void);
const char *recvxGameBootMoviePspStage(void);

#define RECVX_MOVIE_PSP_STRIDE 512u
#define RECVX_MOVIE_PSP_WIDTH 480u
#define RECVX_MOVIE_PSP_HEIGHT 272u
#define RECVX_MOVIE_PSP_FRAME_BYTES (RECVX_MOVIE_PSP_STRIDE*RECVX_MOVIE_PSP_HEIGHT*4u)
#endif
