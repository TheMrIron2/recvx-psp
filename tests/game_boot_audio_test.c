/* Exercises the production PSP adapter against SAS API state rules.
   No ADPCM decoding: firmware calls and compressed-buffer lifetime only. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pspaudio.h>
#include <pspkernel.h>
#include <psputility.h>
#include "psp/game_boot_audio.h"

typedef struct TestVoice {
    const unsigned char *data;
    size_t size;
    int keyed, paused, loop, volume, pan, pitch;
    unsigned int envelope1, envelope2;
} TestVoice;
static TestVoice voice[32];
static unsigned int starts, stops, invalid_keyoffs, ended = 0xffffffffu;
static int locked, fail_thread_start, waits;
static volatile unsigned char observed;
int __sceSasInit(void *core, int grain, int voices, int mode, int rate)
{ assert(!((uintptr_t)core & 63)); assert(grain == 256 && voices == 32 && mode == 0 && rate == 44100); memset(voice, 0, sizeof(voice)); return 0; }
int __sceSasCore(void *core, void *out)
{
    (void)core; memset(out, 0, RECVX_AUDIO_FRAMES * 4);
    for (int i = 0; i < 32; ++i) if (voice[i].keyed && !voice[i].paused) {
        observed = voice[i].data[0]; observed = voice[i].data[voice[i].size - 1];
    }
    return 0;
}
unsigned int __sceSasGetEndFlag(void *core) { (void)core; return ended; }
int __sceSasSetVoice(void *core, int i, const void *data, int size, int loop)
{ (void)core; assert(i >= 0 && i < 32 && !((uintptr_t)data & 15) && size > 0 && !(size & 15)); voice[i].data = data; voice[i].size = (size_t)size; voice[i].loop = loop; return 0; }
int __sceSasSetPitch(void *core, int i, int pitch) { (void)core; assert(pitch >= 0 && pitch <= 16384); voice[i].pitch = pitch; return 0; }
int __sceSasSetVolume(void *core, int i, int left, int right, int el, int er)
{ (void)core; assert(left >= 0 && left <= 4096 && right >= 0 && right <= 4096 && !el && !er); voice[i].volume = left + right; voice[i].pan = right - left; return 0; }
int __sceSasSetKeyOn(void *core, int i)
{ (void)core; if (voice[i].paused || voice[i].keyed) return (int)0x80420016u; voice[i].keyed = 1; ended &= ~(1u << i); ++starts; return 0; }
int __sceSasSetKeyOff(void *core, int i)
{ (void)core; if (voice[i].paused || !voice[i].keyed) { ++invalid_keyoffs; return (int)0x80420016u; } voice[i].keyed = 0; ++stops; return 0; }
int __sceSasSetPause(void *core, unsigned int mask, int pause)
{ (void)core; for (int i = 0; i < 32; ++i) if (mask & (1u << i)) voice[i].paused = pause; return 0; }
int __sceSasSetSimpleADSR(void *core, int i, unsigned int a, unsigned int b)
{ (void)core; assert(!(b & 0x2000)); voice[i].envelope1 = a; voice[i].envelope2 = b; return 0; }
int __sceSasSetADSRmode(void *core, int i, unsigned int flags, int a, int d, int s, int r)
{ (void)core; (void)i; assert(flags == 15 && a == 0 && d == 1 && s == 0 && r == 1); return 0; }
int __sceSasSetADSR(void *core, int i, unsigned int flags, int a, int d, int s, int r)
{ (void)core; (void)i; assert(flags == 15 && a >= 0 && d >= 0 && s >= 0 && r >= 0); return 0; }
int __sceSasSetSL(void *core, int i, int level) { (void)core; (void)i; assert(level == 0x40000000); return 0; }
void sceKernelDcacheWritebackRange(const void *data, unsigned int size) { assert(data && size); }
int sceUtilityLoadModule(int id) { (void)id; return 0; }
int sceUtilityUnloadModule(int id) { (void)id; return 0; }
SceUID sceKernelCreateSema(const char *name, SceUInt attr, int init, int max, SceKernelSemaOptParam *option)
{ (void)name; (void)attr; (void)option; assert(init == 1 && max == 1); return 1; }
int sceKernelWaitSema(SceUID id, int count, SceUInt *timeout) { (void)id; (void)count; (void)timeout; assert(!locked); locked = 1; return 0; }
int sceKernelSignalSema(SceUID id, int count) { (void)id; (void)count; assert(locked); locked = 0; return 0; }
int sceKernelDeleteSema(SceUID id) { (void)id; return 0; }
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority, int stack, SceUInt attr, SceKernelThreadOptParam *option)
{ (void)name; (void)entry; (void)priority; (void)stack; (void)attr; (void)option; return 2; }
int sceKernelStartThread(SceUID id, SceSize size, void *args) { (void)id; (void)size; (void)args; return fail_thread_start ? -123 : 0; }
int sceKernelWaitThreadEnd(SceUID id, SceUInt *timeout) { (void)id; (void)timeout; ++waits; return 0; }
int sceKernelDeleteThread(SceUID id) { (void)id; return 0; }
int sceAudioChReserve(int channel, int samples, int format) { (void)channel; (void)samples; (void)format; return 0; }
int sceAudioChRelease(int channel) { (void)channel; return 0; }
int sceAudioOutputBlocking(int channel, int volume, void *data) { (void)channel; (void)volume; (void)data; return 0; }
static void render(unsigned int count)
{
    int16_t output[RECVX_AUDIO_FRAMES * 2];
    while (count--) { recvxAudioLock(); recvxAudioRender(output, RECVX_AUDIO_FRAMES); recvxAudioUnlock(); }
    if (recvxAudioFault()) fprintf(stderr, "audio test fault: %s\n", recvxAudioFault());
    assert(!recvxAudioFault() && !recvxAudioNativeError());
}
static void bank(unsigned int key)
{ assert(!recvxAudioRequestBank(key)); assert(recvxAudioBankBusy()); recvxAudioService(); assert(!recvxAudioFault()); assert(!recvxAudioBankBusy()); }
static void fade(void)
{ recvxAudioLock(); recvxAudioAdvanceFades(); recvxAudioUnlock(); }
int main(int argc, char **argv)
{
    unsigned int before, time;
    assert(argc == 2); assert(!recvxAudioInit(argv[1])); assert(!recvxAudioOutputInit());
    /* The reported 80420016 regression: idle stops and paused replacements. */
    recvxAudioStopAll(0); assert(!invalid_keyoffs && !recvxAudioNativeError());
    assert(!recvxAudioMount());
    for (int sound = 0; sound < 4; ++sound) {
        assert(recvxAudioPlaySe(17, sound, 0, 0) > 0);
        assert(voice[4].envelope1 == 0xe680 && voice[4].envelope2 == 0x005f);
        render(1);
    }
    assert(starts == 4 && stops >= 3);
    /* Script controls survive starts, mute existing voices without key-off,
       preserve the sample gain, and release back to spatial gain/pan. */
    recvxAudioStopSounds(0);
    recvxAudioSoundControl(0,17,6,-127,100);
    assert(recvxAudioPlaySe(17,0,-20,-40)>0);
    assert(voice[4].keyed&&voice[4].volume==0);
    unsigned int keyed_before=starts,stopped_before=stops;
    recvxAudioSoundControl(0,17,6,0,100);
    assert(voice[4].volume>0&&voice[4].pan>0&&starts==keyed_before&&stops==stopped_before);
    int full=voice[4].volume;
    recvxAudioSoundControl(0,17,0,0,0);
    assert(voice[4].volume<full&&voice[4].pan<0);
    recvxAudioSoundControl(0,17,6,0,100);full=voice[4].volume;
    recvxAudioStopSounds(2);fade();
    assert(voice[4].keyed&&voice[4].volume>0&&voice[4].volume<full);
    fade();assert(!voice[4].keyed);
    recvxAudioSoundControl(0,17,0,0,0);
    recvxAudioStopSounds(0);
    bank(0); bank(0xfff0); bank(0x4000);
    assert(recvxAudioEnvironment(9) == 16);
    /* The room's MIDI section likewise has highest ID 113, not 113 entries. */
    assert(!recvxAudioPlayMidi(7,0x271,0,0)); render(1); recvxAudioStopMidi(7);
    assert(recvxAudioPlaySe(6, 0x200, 0, 0) > 0);
    assert(recvxAudioPlaySe(8, 0x100, 0, 0) > 0);
    /* Sony's program header has highest ID 3: all four Claire voices,
       including pain voice 0x403, must be selectable with native ADPCM. */
    for (int program=0;program<=3;++program) {
        assert(recvxAudioPlaySe(7, 0x400 | program, 0, 0) > 0); render(1);
    }
    /* Replacing a bank stops its voices before releasing compressed storage. */
    bank(0); render(1);
    before = starts; assert(!recvxAudioPlayMidi(0, 0x200, 0, 0)); render(6); assert(starts > before);
    recvxAudioStopMidi(0);
    /* New notes and existing notes share the same native sequence envelope. */
    recvxAudioStopSounds(0);
    assert(!recvxAudioPlayMidiFade(7, 0x200, 0, 0, 2));
    render(6);
    unsigned int midi_voice = 32;
    for (unsigned int i = 4; i < 32; ++i) if (voice[i].keyed) { midi_voice = i; break; }
    assert(midi_voice < 32 && voice[midi_voice].volume == 0);
    fade();
    assert(voice[midi_voice].volume > 0);
    int half_volume = voice[midi_voice].volume;
    fade();
    assert(voice[midi_voice].volume >= half_volume);
    recvxAudioSoundControl(1,7,6,-127,-100);
    assert(voice[midi_voice].volume==0);
    recvxAudioSoundControl(1,7,6,0,-100);
    assert(voice[midi_voice].volume>0&&voice[midi_voice].pan<0);
    recvxAudioSoundControl(1,7,0,0,0);
    /* A muted sequence must also mute notes scheduled after the change. */
    recvxAudioStopMidi(7);
    assert(!recvxAudioPlayMidi(7, 0x200, 0, 0));
    recvxAudioMidiVolume(7, -127, 0, 0);
    before = starts;
    render(6);
    assert(starts > before);
    for (unsigned int i = 4; i < 32; ++i)
        if (voice[i].keyed) { assert(voice[i].volume == 0); midi_voice = i; }
    recvxAudioMidiVolume(7, -127, 2, 1);
    fade(); assert(voice[midi_voice].keyed);
    fade(); assert(!voice[midi_voice].keyed);
    /* COMMON.MLT supplies all four shared door sequences. Door packs
       contain only MIDI instruments, so mounting those alone is insufficient. */
    recvxAudioStopSounds(0);
    bank(0x8016);
    for (int sequence = 0; sequence < 4; ++sequence) {
        before = starts;
        assert(!recvxAudioPlayMidi(2, (int)(0x80000100u | sequence), 0, 0));
        render(8);
        assert(starts > before);
        recvxAudioStopMidi(2);
    }
    bank(0x8019); /* Replacing door instruments preserves shared sequences. */
    before = starts;
    assert(!recvxAudioPlayMidi(2, (int)0x80000100u, 0, 0));
    render(8); assert(starts > before); recvxAudioStopMidi(2);
    assert(!recvxAudioAdx(0, 3, 3)); assert(voice[0].pan < 0 && voice[1].pan > 0);
    render(2); time = recvxAudioTime(0); assert(time > 0);
    recvxAudioPause(0, 1); render(2); assert(recvxAudioTime(0) == time);
    /* COMMON.MLT supplies all four shared door sequences. Door packs
       contain only MIDI instruments, so mounting those alone is insufficient. */
    recvxAudioStopSounds(0);
    bank(0x8016);
    for (int sequence = 0; sequence < 4; ++sequence) {
        before = starts;
        assert(!recvxAudioPlayMidi(2, (int)(0x80000100u | sequence), 0, 0));
        render(8);
        assert(starts > before);
        recvxAudioStopMidi(2);
    }
    bank(0x8019); /* Replacing door instruments preserves shared sequences. */
    before = starts;
    assert(!recvxAudioPlayMidi(2, (int)0x80000100u, 0, 0));
    render(8); assert(starts > before); recvxAudioStopMidi(2);
    assert(!recvxAudioAdx(0, 3, 3)); render(1); /* Replace paused stereo stream. */
    recvxAudioPause(0, 1); recvxAudioStop(0); render(1); assert(!recvxAudioStatus(0));
    assert(!invalid_keyoffs);
    assert(!recvxAudioAdx(0, 0, 0)); assert(voice[0].loop && voice[1].loop);
    assert(!recvxAudioAdx(1, 1, 0)); assert(voice[2].keyed && !voice[3].keyed);
    recvxAudioPan(1, 0, 80); assert(voice[2].pan > 0);
    recvxAudioStop(1); assert(!recvxAudioAdx(1, 1, 0));
    assert(voice[2].pan > 0); /* Position survives a compressed stream restart. */
    recvxAudioStopSounds(0); render(1); assert(recvxAudioStatus(0) && recvxAudioStatus(1));
    recvxAudioVolume(0, -127, 2);
    for (int i = 0; i < 2; ++i) { recvxAudioLock(); recvxAudioAdvanceFades(); recvxAudioUnlock(); }
    assert(!recvxAudioStatus(0) && recvxAudioStatus(1));
    recvxAudioOutputShutdown(); assert(!invalid_keyoffs && waits == 1);
    fail_thread_start = 1; assert(recvxAudioOutputInit() == -1); assert(waits == 1);
    fail_thread_start = 0; assert(!recvxAudioInit(argv[1])); assert(!recvxAudioOutputInit());
    assert(!recvxAudioRequestBank(0x1234)); recvxAudioService(); assert(recvxAudioFault() && recvxAudioBankBusy());
    recvxAudioOutputShutdown();
    puts("PASS: production SAS adapter, idle/paused/repeated stops, native stereo/mono/loop voices, real banks/compact sequences, fades, ownership and failed-thread cleanup; no software decoder");
    return 0;
}
