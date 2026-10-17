#include <string.h>
#include "psp/game_boot_audio.h"
#if !defined(RECVX_GAME_BOOT_HOST_TEST)
#include <pspaudio.h>
#include <pspkernel.h>
#include <psputility.h>
#include <psputility_modules.h>
/* Opaque firmware state, matching PSPSDK's SceSasCore. */
static unsigned char core[3616] __attribute__((aligned(64)));
extern int __sceSasInit(void *, int, int, int, int);
extern int __sceSasCore(void *, void *);
extern unsigned int __sceSasGetEndFlag(void *);
extern int __sceSasSetVoice(void *, int, const void *, int, int);
extern int __sceSasSetPitch(void *, int, int);
extern int __sceSasSetVolume(void *, int, int, int, int, int);
extern int __sceSasSetKeyOn(void *, int);
extern int __sceSasSetKeyOff(void *, int);
extern int __sceSasSetPause(void *, unsigned int, int);
extern int __sceSasSetADSR(void *, int, unsigned int, int, int, int, int);
extern int __sceSasSetADSRmode(void *, int, unsigned int, int, int, int, int);
extern int __sceSasSetSL(void *, int, int);
extern int __sceSasSetSimpleADSR(void *, int, unsigned int, unsigned int);
static int semaphore = -1, audio_channel = -1, audio_thread = -1, ready, last_error;
static int thread_started, loaded_codec, loaded_sas;
static unsigned int keyed_voices;
static const char *error_call;
static volatile int running;
static int16_t buffers[2][RECVX_AUDIO_FRAMES * 2] __attribute__((aligned(64)));
static int checked_at(int result, const char *call)
{
    if (result < 0 && !last_error) { error_call = call; last_error = result; }
    return result < 0 ? -1 : 0;
}
#define checked(result) checked_at((result), #result)
void recvxAudioLock(void) { if (semaphore >= 0) sceKernelWaitSema(semaphore, 1, NULL); }
void recvxAudioUnlock(void) { if (semaphore >= 0) sceKernelSignalSema(semaphore, 1); }
void recvxAudioNativeFlush(const void *data, size_t size)
{ sceKernelDcacheWritebackRange(data, (unsigned int)size); }
void recvxAudioNativeVolume(unsigned int voice, int volume, int pan)
{
    if (!ready) return;
    if (volume < 0) volume = 0;
    if (volume > 4096) volume = 4096;
    if (pan < -127) pan = -127;
    if (pan > 127) pan = 127;
    checked(__sceSasSetVolume(core, (int)voice, volume * (pan > 0 ? 127 - pan : 127) / 127,
        volume * (pan < 0 ? 127 + pan : 127) / 127, 0, 0));
}
int recvxAudioNativeStart(unsigned int voice, const void *data, size_t size,
    unsigned int rate, int loop, int e1, int e2, int volume, int pan)
{
    int pitch = (int)((uint64_t)rate * 4096 / RECVX_AUDIO_RATE);
    if (!ready || voice >= 32 || !data || ((uintptr_t)data & 15) || !size || (size & 15) ||
        pitch < 1 || pitch > 16384) return -1;
    recvxAudioNativeStop(voice);
    sceKernelDcacheWritebackRange(data, (unsigned int)size);
    if (checked(__sceSasSetPause(core, 1u << voice, 1)) ||
        checked(__sceSasSetVoice(core, (int)voice, data, (int)size, loop)) ||
        checked(__sceSasSetPitch(core, (int)voice, pitch))) return -1;
    if (e1 >= 0) {
        /* SPU2 and SAS both encode the attack rate in bits 8..14. */
        if (checked(__sceSasSetSimpleADSR(core, (int)voice, (unsigned int)e1, (unsigned int)e2))) return -1;
    } else {
        if (checked(__sceSasSetADSRmode(core, (int)voice, 15, 0, 1, 0, 1)) ||
            checked(__sceSasSetADSR(core, (int)voice, 15, 0x7fffffff, 0, 0, 0x7fffffff)) ||
            checked(__sceSasSetSL(core, (int)voice, 0x40000000))) return -1;
    }
    recvxAudioNativeVolume(voice, volume, pan);
    if (checked(__sceSasSetPause(core, 1u << voice, 0)) || checked(__sceSasSetKeyOn(core, (int)voice))) return -1;
    keyed_voices |= 1u << voice;
    return 0;
}
void recvxAudioNativeStop(unsigned int voice)
{
    if (!ready || voice >= 32) return;
    /* SAS rejects KeyOff on a paused or already released voice. Unpause and
       release keyed voices while the render lock is held, then pause before
       the caller can free the waveform. No render runs between these calls. */
    if (keyed_voices & (1u << voice)) {
        int result;
        if (checked(__sceSasSetPause(core, 1u << voice, 0))) return;
        result = __sceSasSetKeyOff(core, (int)voice);
        /* Natural completion can already have released the voice. */
        if ((unsigned int)result != 0x80420016u) checked_at(result, "__sceSasSetKeyOff");
    }
    checked(__sceSasSetPause(core, 1u << voice, 1));
    keyed_voices &= ~(1u << voice);
}
void recvxAudioNativePitch(unsigned int voice, unsigned int rate)
{ if (ready) checked(__sceSasSetPitch(core, (int)voice, (int)((uint64_t)rate * 4096 / RECVX_AUDIO_RATE))); }
void recvxAudioNativePause(unsigned int voice, int paused)
{ if (ready) checked(__sceSasSetPause(core, 1u << voice, paused)); }
unsigned int recvxAudioNativeEnded(void) { return ready ? __sceSasGetEndFlag(core) : 0xffffffffu; }
int recvxAudioNativeRender(int16_t *output, size_t frames)
{ return !ready || frames != RECVX_AUDIO_FRAMES ? -1 : checked(__sceSasCore(core, output)); }
int recvxAudioNativeError(void) { return last_error; }
const char *recvxAudioNativeErrorCall(void) { return error_call ? error_call : "none"; }
static int output_thread(SceSize size, void *args)
{
    unsigned int buffer = 0;
    (void)size; (void)args;
    while (running) {
        int result;
        recvxAudioLock(); recvxAudioRender(buffers[buffer], RECVX_AUDIO_FRAMES); recvxAudioUnlock();
        result = sceAudioOutputBlocking(audio_channel, PSP_AUDIO_VOLUME_MAX, buffers[buffer]);
        if (result < 0) { checked(result); break; }
        buffer ^= 1;
    }
    return 0;
}
int recvxAudioOutputInit(void)
{
    int result;
    if (ready) return 0;
    last_error = 0; error_call = NULL; keyed_voices = 0;
    result = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    if (result < 0 && (unsigned int)result != 0x80111102u) return checked(result);
    loaded_codec = result >= 0;
    result = sceUtilityLoadModule(PSP_MODULE_AV_SASCORE);
    if (result < 0 && (unsigned int)result != 0x80111102u) { checked(result); recvxAudioOutputShutdown(); return -1; }
    loaded_sas = result >= 0;
    if (checked(__sceSasInit(core, RECVX_AUDIO_FRAMES, 32, 0, RECVX_AUDIO_RATE))) { recvxAudioOutputShutdown(); return -1; }
    ready = 1;
    semaphore = sceKernelCreateSema("RECVX SAS", 0, 1, 1, NULL);
    audio_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, RECVX_AUDIO_FRAMES, PSP_AUDIO_FORMAT_STEREO);
    audio_thread = sceKernelCreateThread("RECVX ADPCM", output_thread, 0x12, 32768, PSP_THREAD_ATTR_USER, NULL);
    if (semaphore < 0 || audio_channel < 0 || audio_thread < 0) {
        checked(semaphore < 0 ? semaphore : audio_channel < 0 ? audio_channel : audio_thread);
        recvxAudioOutputShutdown(); return -1;
    }
    running = 1;
    result = sceKernelStartThread(audio_thread, 0, NULL);
    if (checked(result)) { running = 0; recvxAudioOutputShutdown(); return -1; }
    thread_started = 1;
    return 0;
}
void recvxAudioOutputShutdown(void)
{
    running = 0;
    if (audio_thread >= 0) {
        if (thread_started) sceKernelWaitThreadEnd(audio_thread, NULL);
        sceKernelDeleteThread(audio_thread); audio_thread = -1; thread_started = 0;
    }
    recvxAudioShutdown();
    if (audio_channel >= 0) { sceAudioChRelease(audio_channel); audio_channel = -1; }
    if (semaphore >= 0) { sceKernelDeleteSema(semaphore); semaphore = -1; }
    ready = 0;
    if (loaded_sas) { sceUtilityUnloadModule(PSP_MODULE_AV_SASCORE); loaded_sas = 0; }
    if (loaded_codec) { sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC); loaded_codec = 0; }
}
#else
/* Call/ownership test double: advances voice durations, does not decode. */
void recvxAudioNativeFlush(const void *data, size_t size) { (void)data; (void)size; }
static uint64_t remaining[32];
static unsigned int rates[32], ended;
static int looping[32], paused[32];
void recvxAudioLock(void) {}
void recvxAudioUnlock(void) {}
int recvxAudioOutputInit(void) { return 0; }
void recvxAudioOutputShutdown(void) { recvxAudioShutdown(); }
int recvxAudioNativeStart(unsigned int voice, const void *data, size_t size, unsigned int rate,
    int loop, int e1, int e2, int volume, int pan)
{
    (void)e1; (void)e2; (void)volume; (void)pan;
    if (voice >= 32 || !data || ((uintptr_t)data & 15) || !size || (size & 15)) return -1;
    remaining[voice] = size / 16 * 28; rates[voice] = rate; looping[voice] = loop; paused[voice] = 0; ended &= ~(1u << voice); return 0;
}
void recvxAudioNativeStop(unsigned int voice) { remaining[voice] = 0; ended |= 1u << voice; }
void recvxAudioNativePause(unsigned int voice, int pause) { paused[voice] = pause; }
void recvxAudioNativeVolume(unsigned int voice, int volume, int pan) { (void)voice; (void)volume; (void)pan; }
void recvxAudioNativePitch(unsigned int voice, unsigned int rate) { rates[voice] = rate; }
int recvxAudioNativeRender(int16_t *output, size_t frames)
{
    memset(output, 0, frames * 4);
    for (unsigned int i = 0; i < 32; ++i) if (!paused[i] && !looping[i] && remaining[i]) {
        uint64_t advance = frames * rates[i] / RECVX_AUDIO_RATE;
        if (advance >= remaining[i]) { remaining[i] = 0; ended |= 1u << i; } else remaining[i] -= advance;
    }
    return 0;
}
unsigned int recvxAudioNativeEnded(void) { return ended; }
int recvxAudioNativeError(void) { return 0; }
const char *recvxAudioNativeErrorCall(void) { return "host call model"; }
#endif
