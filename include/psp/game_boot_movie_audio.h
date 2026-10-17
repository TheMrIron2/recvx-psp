#ifndef RECVX_GAME_BOOT_MOVIE_AUDIO_H
#define RECVX_GAME_BOOT_MOVIE_AUDIO_H
#include <stddef.h>
#include <stdint.h>
/* Compressed RVAG streaming into firmware SAS voices 0/1. No sample decoding. */
int recvxMovieAudioStart(const char *path);
void recvxMovieAudioStop(void);
int recvxMovieAudioActive(void);
uint64_t recvxMovieAudioSamples(void);
void recvxMovieAudioVolume(int volume);
/* Called only while the sound service owns its native render lock. */
void recvxMovieAudioStopLocked(void);
int recvxMovieAudioRenderLocked(size_t frames);
#endif
