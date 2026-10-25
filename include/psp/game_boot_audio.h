#ifndef RECVX_GAME_BOOT_AUDIO_H
#define RECVX_GAME_BOOT_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#define RECVX_AUDIO_RATE 44100
#define RECVX_AUDIO_FRAMES 256
int recvxAudioInit(const char *directory);
void recvxAudioShutdown(void);
typedef struct RecvxAudioOutputStats {
    uint64_t cpu_us;
    uint32_t grains,render_us,wait_us;
} RecvxAudioOutputStats;
RecvxAudioOutputStats recvxAudioOutputStats(void);
int recvxAudioOutputInit(void);
void recvxAudioOutputShutdown(void);
void recvxAudioLock(void);
void recvxAudioUnlock(void);
void recvxAudioRender(int16_t *stereo, size_t frames);
int recvxAudioMount(void);
const char *recvxAudioFault(void);
void recvxAudioService(void);
int recvxAudioAdx(unsigned int slot, unsigned int partition, unsigned int file);
void recvxAudioStop(unsigned int slot);
void recvxAudioPause(unsigned int slot, int paused);
void recvxAudioVolume(unsigned int slot, int volume, int frames);
void recvxAudioPan(unsigned int slot, unsigned int channel, int pan);
int recvxAudioStatus(unsigned int slot);
unsigned int recvxAudioTime(unsigned int slot);
void recvxAudioStopAll(int frames);
void recvxAudioStopSounds(int frames);
int recvxAudioRequestBank(unsigned int key);
int recvxAudioBankBusy(void);
int recvxAudioPlaySe(int slot, int sound, int volume, int pan);
int recvxAudioPlayMidi(int slot, int sound, int volume, int pan);
int recvxAudioPlayMidiFade(int slot, int sound, int volume, int pan, int frames);
void recvxAudioStopSe(int slot);
int recvxAudioSePlaying(int slot);
void recvxAudioSeVolume(int slot,int volume,int frames);
/* Script overrides: flag 2 controls gain, flag 4 controls pan; MIDI=1. */
void recvxAudioSoundControl(int is_midi, int slot, unsigned int flags, int volume, int pan);
void recvxAudioUpdateSe(int slot, int volume, int pan, int pitch);
int recvxAudioEnvironment(unsigned int field);
void recvxAudioStopMidi(int slot);
void recvxAudioMidiVolume(int slot, int volume, int frames, int stop_when_done);
void recvxAudioAdvanceFades(void);
/* Native compressed playback. Host tests implement a call/ownership model,
   never an ADPCM decoder or software audio mixer. */
int recvxAudioNativeStart(unsigned int voice, const void *data, size_t size,
    unsigned int rate, int loop, int envelope1, int envelope2, int volume, int pan);
void recvxAudioNativeStop(unsigned int voice);
void recvxAudioNativeFlush(const void *data, size_t size);
void recvxAudioNativePause(unsigned int voice, int paused);
void recvxAudioNativeVolume(unsigned int voice, int volume, int pan);
void recvxAudioNativePitch(unsigned int voice, unsigned int rate);
int recvxAudioNativeRender(int16_t *output, size_t frames);
unsigned int recvxAudioNativeEnded(void);
int recvxAudioNativeError(void);
const char *recvxAudioNativeErrorCall(void);
#endif
