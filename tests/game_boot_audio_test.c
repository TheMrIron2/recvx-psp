#define _XOPEN_SOURCE 700
/* Exercises the production PSP adapter against SAS API state rules.
   No ADPCM decoding: firmware calls and compressed-buffer lifetime only. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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
SceUInt sceKernelGetSystemTimeLow(void) { static SceUInt clock; return ++clock; }
int sceKernelReferThreadStatus(SceUID id,SceKernelThreadInfo *info)
{ assert(id==2&&info->size==sizeof(*info));info->runClocks.low=123;info->runClocks.hi=1;return 0; }

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
static unsigned int fixture_u32(const unsigned char *p)
{ return p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24); }
static unsigned char *fixture_read(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);
    rewind(f);unsigned char *data=malloc((size_t)n);assert(data&&fread(data,1,(size_t)n,f)==(size_t)n);
    fclose(f);*size=(size_t)n;return data;
}
static void test_sequence_loops(const char *assets)
{
    char root[]="/tmp/recvx-audio-loops-XXXXXX",source[4096],path[8192],target[8192];
    assert(realpath(assets,source)&&mkdtemp(root));
    snprintf(path,sizeof(path),"%s/files",root);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/archives",root);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/archives/MULTSPQ1",root);assert(!mkdir(path,0700));
    const char *links[]={"COMMON.MLT","MULTSPQ1.IDX"};
    for(int i=0;i<2;++i){
        snprintf(path,sizeof(path),"%s/files/%s",root,links[i]);
        snprintf(target,sizeof(target),"%s/files/%s",source,links[i]);assert(!symlink(target,path));
    }
    size_t bytes;snprintf(path,sizeof(path),"%s/files/MULTSPQ1.IDX",source);
    unsigned char *index=fixture_read(path,&bytes);unsigned int pack_id=0;
    while(pack_id<bytes/2 && (index[pack_id*2]|(index[pack_id*2+1]<<8))!=0x14)++pack_id;
    assert(pack_id<bytes/2);free(index);
    snprintf(path,sizeof(path),"%s/archives/MULTSPQ1/%u.bin",source,pack_id);
    unsigned char *pack=fixture_read(path,&bytes);unsigned char *sequence=NULL;size_t capacity=0;
    for(size_t h=0;h+16<=bytes;h+=16){
        unsigned int off=fixture_u32(pack+h),length=fixture_u32(pack+h+4);
        if(!off)break;
        assert(off<=bytes&&length<=bytes-off);
        if(fixture_u32(pack+h+8)||fixture_u32(pack+h+12)!=2)continue;
        unsigned char *sq=pack+off;unsigned int base=fixture_u32(sq+36);
        unsigned int start=base+fixture_u32(sq+base+16+3*4),end=length;
        for(unsigned int j=0;j<=fixture_u32(sq+base+12);++j){
            unsigned int next=base+fixture_u32(sq+base+16+j*4);if(next>start&&next<end)end=next;
        }
        sequence=sq+start;capacity=end-start;break;
    }
    /* Channel 3, loop ID 127, running controller status and an immediate
       loop start. These bytes also test restoring compressed delta state. */
    static const unsigned char events[]={6,0,0,0,0xe0,1,0,
        0xc3,0x83,0xb3,99,0x80,6,0xff,0x93,60,127,
        1,0x83,0xbc,0xb3,99,0x81,6,0xff,38,0,0,0xff,0x2f,0};
    assert(sequence&&capacity>=sizeof(events));
    snprintf(path,sizeof(path),"%s/archives/MULTSPQ1/%u.bin",root,pack_id);
    for(int variant=0;variant<7;++variant){
        memset(sequence,0,capacity);memcpy(sequence,events,sizeof(events));
        sequence[26]=variant<=3?(unsigned char)variant:0;
        if(variant==4)sequence[11]=0x82; /* Unsupported loop command. */
        if(variant==5)sequence[11]=0x81; /* End without any start marker. */
        if(variant==6)sequence[17]=0; /* Zero-time endless loop: bounded watchdog. */
        FILE *f=fopen(path,"wb");assert(f&&fwrite(pack,1,bytes,f)==bytes&&!fclose(f));
        assert(!recvxAudioInit(root));bank(0x14);
        unsigned int before=starts;assert(!recvxAudioPlayMidi(3,0x203,0,0));
        if(variant>=4){
            int16_t output[RECVX_AUDIO_FRAMES*2];
            for(int step=0;step<10&&!recvxAudioFault();++step){
                recvxAudioLock();recvxAudioRender(output,RECVX_AUDIO_FRAMES);recvxAudioUnlock();
            }
            const char *fault=recvxAudioFault();assert(fault);
            assert(strstr(fault,variant==4?"loop command":variant==5?"no start":"watchdog"));
        }else{
            render(10);
            if(variant){assert(starts-before==(unsigned int)variant);before=starts;render(10);assert(starts==before);}
            else {assert(starts-before>=9);before=starts;render(10);assert(starts>before);}
            recvxAudioStopMidi(3);before=starts;render(10);assert(starts==before);
        }
    }
    assert(!recvxAudioInit(assets));free(pack);assert(!unlink(path));
    for(int i=0;i<2;++i){snprintf(path,sizeof(path),"%s/files/%s",root,links[i]);assert(!unlink(path));}
    snprintf(path,sizeof(path),"%s/archives/MULTSPQ1",root);assert(!rmdir(path));
    snprintf(path,sizeof(path),"%s/archives",root);assert(!rmdir(path));
    snprintf(path,sizeof(path),"%s/files",root);assert(!rmdir(path));assert(!rmdir(root));
    puts("PASS: Sony numbered loops, finite/endless playback, compressed delta state, stop/restart and invalid/zero-time loop rejection");
}
int main(int argc, char **argv)
{
    unsigned int before, time;
    assert(argc == 2); assert(!recvxAudioInit(argv[1])); assert(!recvxAudioOutputInit());
    assert(recvxAudioOutputStats().cpu_us==((UINT64_C(1)<<32)|123));
    /* The reported 80420016 regression: idle stops and paused replacements. */
    recvxAudioStopAll(0); assert(!invalid_keyoffs && !recvxAudioNativeError());
    assert(!recvxAudioMount());
    for (int sound = 0; sound < 11; ++sound) {
        assert(recvxAudioPlaySe(17, sound, 0, 0) > 0);
        assert(voice[4].envelope1 == 0x80ff && voice[4].envelope2 == 0x5fe6);
        render(1);
    }
    assert(starts == 11 && stops >= 10);
    /* Sony sample ADSR is at 18/20, not 19/21. The old shifted attack
       (0xe680) advances only 10 envelope units/sample on SAS and is silent
       over these short sounds. Real COMMON uses a near-instant attack. */
    assert(((voice[4].envelope1>>8)&127)==0);

    /* Script controls survive starts, mute existing voices without key-off,
       preserve the sample gain, and release back to spatial gain/pan. */
    recvxAudioStopSounds(0);
    recvxAudioSoundControl(0,17,6,-127,100);
    assert(recvxAudioPlaySe(17,0,-20,-40)>0);
    assert(voice[4].keyed&&voice[4].volume==0);
    unsigned int keyed_before=starts,stopped_before=stops;
    recvxAudioSoundControl(0,17,6,0,100);
    assert(voice[4].volume>0&&voice[4].pan>0&&starts==keyed_before&&stops==stopped_before);
    int full=(voice[4].volume+abs(voice[4].pan))/2;
    recvxAudioSoundControl(0,17,0,0,0);
    assert((voice[4].volume+abs(voice[4].pan))/2<full&&voice[4].pan<0);
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
    recvxAudioStopSounds(0);
    for(int floor=0;floor<5;++floor){
        assert(recvxAudioPlaySe(12,0x200|floor,0,0)>0);
        assert(voice[4].envelope1==0x80ff&&voice[4].envelope2==0x5fe6&&voice[4].volume>0);
        render(1);assert(voice[4].keyed&&!voice[4].paused);
    }
    recvxAudioStopSounds(0);
    assert(recvxAudioPlaySe(6,0x200,0,0)>0);
    assert(voice[4].envelope1==0x80ff&&voice[4].envelope2==0x5fe6);
    int foot_full=voice[4].volume;
    assert(foot_full>0);
    recvxAudioUpdateSe(6,0,0,0);assert(voice[4].volume==foot_full);
    recvxAudioUpdateSe(6,-64,0,0);
    assert(abs(voice[4].volume-foot_full*64/127)<=2);
    recvxAudioUpdateSe(6,-127,0,0);assert(!voice[4].volume);
    recvxAudioStopSounds(0);
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
    /* Actual opening music entry 114: script -45 maps to -14.2 dB.
       Dialogue -32 maps to -6.4 dB. Neither uses the SE level curve. */
    assert(!recvxAudioAdx(0,0,114));
    recvxAudioVolume(0,-45,0);
    int music_gain=(int)(4096.0*pow(10.0,-142.0/200.0));
    assert(voice[0].volume==music_gain&&voice[1].volume==music_gain);
    recvxAudioVolume(0,-127,0);assert(!voice[0].volume&&!voice[1].volume);
    recvxAudioVolume(0,-45,2);fade();assert(voice[0].volume>0);
    fade();assert(voice[0].volume==music_gain&&voice[1].volume==music_gain);

    assert(!recvxAudioAdx(1, 1, 0)); assert(voice[2].keyed && !voice[3].keyed);
    recvxAudioVolume(1,-32,0);
    assert(voice[2].volume==2*(int)(4096.0*pow(10.0,-64.0/200.0)));

    recvxAudioPan(1, 0, 80); assert(voice[2].pan > 0);
    recvxAudioStop(1); assert(!recvxAudioAdx(1, 1, 0));
    assert(voice[2].pan > 0); /* Position survives a compressed stream restart. */
    recvxAudioStopSounds(0); render(1); assert(recvxAudioStatus(0) && recvxAudioStatus(1));
    recvxAudioVolume(0, -127, 2);
    for (int i = 0; i < 2; ++i) { recvxAudioLock(); recvxAudioAdvanceFades(); recvxAudioUnlock(); }
    assert(!recvxAudioStatus(0) && recvxAudioStatus(1));
    /* The stairs-room sequence uses Sony's numbered loop controllers
       (99/6/38). It must continue producing native notes past the first pass. */
    recvxAudioStopAll(0);bank(0x14);
    before=starts;assert(!recvxAudioPlayMidi(3,0x203,0,0));render(600);
    assert(starts>=before+3);
    unsigned int loop_starts=starts;render(600);assert(starts>loop_starts);
    recvxAudioStopMidi(3);loop_starts=starts;render(600);assert(starts==loop_starts);
    assert(!recvxAudioPlayMidi(3,0x203,0,0));render(600);assert(starts>=loop_starts+3);
    recvxAudioStopMidi(3);
    /* Typewriter programs are room-local: room 1/8 supplies all three. */
    bank(1080);
    for(unsigned int sequence=0x42;sequence<=0x44;++sequence) {
        before=starts;
        assert(!recvxAudioPlayMidi(2,(int)(0x80000200u|sequence),0,0));
        render(300);assert(starts>before);recvxAudioStopMidi(2);
    }
    test_sequence_loops(argv[1]);
    recvxAudioOutputShutdown(); assert(!invalid_keyoffs && waits == 1);
    fail_thread_start = 1; assert(recvxAudioOutputInit() == -1); assert(waits == 1);
    fail_thread_start = 0; assert(!recvxAudioInit(argv[1])); assert(!recvxAudioOutputInit());
    assert(!recvxAudioRequestBank(0x1234)); recvxAudioService(); assert(recvxAudioFault() && recvxAudioBankBusy());
    recvxAudioOutputShutdown();
    puts("PASS: production SAS adapter, idle/paused/repeated stops, native stereo/mono/loop voices, real banks/compact sequences, fades, ownership and failed-thread cleanup; no software decoder");
    return 0;
}
