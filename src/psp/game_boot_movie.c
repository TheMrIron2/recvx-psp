#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_audio.h"
#include "psp/game_boot_event_movie.h"
#include "psp/game_boot_movie.h"
#include "psp/game_boot_movie_audio.h"
#include "psp/game_boot_movie_psp.h"
#include "psp/platform.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/screen.h"
#include "ps2/veronica/prog/sdfunc.h"

#define FRAME_ALLOCATION (512u*512u*4u)
#define DEFAULT_INTERVAL UINT64_C(3003)
#define CATCHUP_LIMIT 8u

static const unsigned char movie_types[RECVX_GAME_BOOT_MOVIE_COUNT] = {
    2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,0x80,0x42,0x82,0,0x80,0,0x82
};
static struct {
    char directory[512], audio_path[576];
    uint32_t *pixels[2];
    uint64_t pts[2], interval, clock, wall_origin, pts_origin;
    int initialized, active, playing, front, future, eof, pinned;
    int decoder_open, audio_started, fallback, visible, deferred_free;
    int movie, cancel_allowed, fade_allowed, fading, fade_steps;
    float fade_volume;
    unsigned int cancel_button;
} movie = {.front=0,.future=-1,.pinned=-1};

static void free_frames(void)
{
    free(movie.pixels[0]); free(movie.pixels[1]);
    movie.pixels[0]=movie.pixels[1]=NULL;
    movie.deferred_free=0;
}

void PlayStopMovieEx(int mode)
{
    (void)mode;
    int was_active=movie.active || movie.decoder_open || movie.audio_started;
    if (movie.audio_started) recvxMovieAudioStop();
    if (movie.decoder_open) recvxGameBootMoviePspClose();
    movie.active=movie.playing=movie.audio_started=movie.decoder_open=0;
    movie.visible=movie.fading=0; movie.future=-1;
    if (was_active && sys) sys->cb_flg&=~2u;
}
void PlayStopMovie(void) { PlayStopMovieEx(0); }

void recvxGameBootMovieShutdown(void)
{
    PlayStopMovieEx(0);
    movie.initialized=0;
    movie.directory[0]=movie.audio_path[0]=0;
    if (movie.pinned>=0) movie.deferred_free=1;
    else free_frames();
}

int recvxGameBootMovieInit(const char *directory)
{
    recvxGameBootMovieShutdown();
    if (!directory || !*directory || strlen(directory)>=sizeof(movie.directory) || movie.pinned>=0)
        return -1;
    strcpy(movie.directory,directory);
    movie.initialized=1; movie.front=0; movie.future=-1; movie.pinned=-1;
    return 0;
}

const uint32_t *recvxGameBootMovieGetFrame(void)
{
    if (!movie.visible) return NULL;
    if (movie.pinned>=0 && movie.pinned!=movie.front)
        recvxGameBootUnsupported("movie frame changed before GU consumption");
    movie.pinned=movie.front;
    return movie.pixels[movie.front];
}
void recvxGameBootMovieFrameConsumed(void)
{
    movie.pinned=-1;
    if (movie.deferred_free) free_frames();
}

static void fail(const char *stage, int code)
{
    char reason[224];
    const char *native_stage=recvxGameBootMoviePspStage();
    snprintf(reason,sizeof(reason),"movie %d %s failed: code=%08x stage=%s",movie.movie,
        stage,(unsigned int)code,native_stage?native_stage:"none");
    PlayStopMovieEx(0);
    recvxGameBootUnsupported(reason);
}

static int decode(int index, int first)
{
    uint64_t pts;
    int result=recvxGameBootMoviePspDecode(movie.pixels[index],&pts);
    if (result<0) fail("AVC decode",recvxGameBootMoviePspError());
    if (result==0) {
        if (first) fail("empty AVC stream",-1);
        movie.eof=1;
        return 0;
    }
    if (result!=1 || (first ? pts!=0 : pts<=movie.pts[movie.front]))
        fail("non-monotonic AVC timestamp",-1);
    movie.pts[index]=pts;
    return 1;
}

static void start_audio(void)
{
    if (movie.playing) return;
    if (recvxMovieAudioStart(movie.audio_path)!=0) {
        movie.audio_started=1; /* Stop a partially constructed native stream. */
        int code=recvxAudioNativeError();
        fail("native ADPCM start",code ? code : errno ? -errno : -1);
    }
    movie.audio_started=movie.playing=1;
    movie.fallback=0; movie.clock=0;
    movie.wall_origin=recvxPlatformTimeUs(); movie.pts_origin=0;
    recvxMovieAudioVolume(4096);
}

int PlayStartMovieEx(int number, int type, int pause)
{
    char path[576];
    if (movie.active) return 2;
    if (!movie.initialized || number<0 || number>=RECVX_GAME_BOOT_MOVIE_COUNT ||
        (pause!=0 && pause!=1) || movie.pinned>=0)
        recvxGameBootUnsupported("invalid native movie start state");
    movie.movie=number;
    if (snprintf(path,sizeof(path),"%s/movies/MV_%03u.pmf",movie.directory,(unsigned int)number)>=(int)sizeof(path) ||
        snprintf(movie.audio_path,sizeof(movie.audio_path),"%s/movies/MV_%03u.vag",movie.directory,(unsigned int)number)>=(int)sizeof(movie.audio_path))
        recvxGameBootUnsupported("native movie asset path exceeds capacity");
    /* The opening BGM alone owns 2.7 MiB. Release old stream storage before
       allocating the decoder and movie surfaces on the PSP heap. */
    StopBgm(0); StopVoice(0);
    for (unsigned int i=0;i<2;++i) {
        if (!movie.pixels[i]) movie.pixels[i]=memalign(64,FRAME_ALLOCATION);
        if (!movie.pixels[i]) fail("framebuffer allocation",-1);
        memset(movie.pixels[i],0,FRAME_ALLOCATION);
    }
    if (recvxGameBootMoviePspOpen(path)!=0) fail("AVC open",recvxGameBootMoviePspError());
    movie.decoder_open=1;
    movie.front=0; movie.future=-1; movie.eof=0; movie.interval=DEFAULT_INTERVAL;
    movie.clock=movie.fading=movie.fade_steps=movie.fallback=0;
    movie.cancel_allowed=!(movie_types[number]&0x80u) || (type&1);
    movie.fade_allowed=!!((movie_types[number]&0x40u) || (type&2));
    movie.cancel_button=number==16 ? 0x800u : 0x100u;
    decode(movie.front,1);
    movie.active=movie.visible=1;
    if (!pause) start_audio();
    recvxGameBootLog("native movie prepared: id=%d AVC + SAS ADPCM",number);
    return 0;
}

int CheckPlayEndMovie(void) { return movie.active; }

/* The audio service counts rendered samples. One hardware output block is
 * queued ahead of the audible clock. The wall clock extends the last sample
 * timestamp only after the native stream has ended, to drain any video tail.
 */
static uint64_t playback_clock(void)
{
    if (!movie.playing) return 0;
    uint64_t samples=recvxMovieAudioSamples();
    if (samples>RECVX_AUDIO_FRAMES) samples-=RECVX_AUDIO_FRAMES; else samples=0;
    uint64_t timestamp=(samples/RECVX_AUDIO_RATE)*90000u+
        (samples%RECVX_AUDIO_RATE)*90000u/RECVX_AUDIO_RATE;
    uint64_t now=recvxPlatformTimeUs();
    if (recvxMovieAudioActive()) {
        if (timestamp>movie.clock) movie.clock=timestamp;
        movie.wall_origin=now; movie.pts_origin=movie.clock;
    } else {
        if (!movie.fallback) {
            movie.fallback=1;
            if (timestamp>movie.clock) movie.clock=timestamp;
            movie.pts_origin=movie.clock; movie.wall_origin=now;
        }
        uint64_t elapsed=now>=movie.wall_origin ? now-movie.wall_origin : 0;
        timestamp=movie.pts_origin+(elapsed/1000u)*90u+(elapsed%1000u)*90u/1000u;
        if (timestamp>movie.clock) movie.clock=timestamp;
    }
    return movie.clock;
}

int GetTimeMoive(void)
{
    if (!movie.active) return 0;
    uint64_t centiseconds=playback_clock()/900u;
    return centiseconds>INT_MAX ? INT_MAX : (int)centiseconds;
}

static int reset_requested(void)
{
    if (CurrentPortId < 0 || CurrentPortId >= 4)
        recvxGameBootUnsupported("invalid native movie controller");
    if (CheckSoftResetKeyFlag(-1)) {
        PlayStopMovieEx(0);
        return 1;
    }
    return 0;
}
static int cancel_requested(void)
{ return sys && movie.cancel_allowed && (Pad[CurrentPortId].press&movie.cancel_button); }

int WaitPrePlayMovie(int mode)
{
    (void)mode;
    if (!movie.active) return 0;
    if (reset_requested()) return 3;
    if (cancel_requested()) { PlayStopMovieEx(0); return 2; }
    start_audio();
    return 0;
}

int PlayMovieMain(int mode)
{
    (void)mode;
    if (!movie.active) return 0;
    if (reset_requested()) return 3;
    if (!movie.playing) start_audio();
    if (cancel_requested()) {
        if (!movie.fade_allowed) { PlayStopMovieEx(0); return 2; }
        if (!movie.fading) {
            movie.fading=1; movie.fade_steps=0; movie.fade_volume=0;
            bhSetScreenFade(0xff000000u,30);
        }
    }
    if (movie.fading) {
        if (!(sys->cb_flg&2u)) { PlayStopMovieEx(0); return 2; }
        bhControlScreenFade();
        if (sys->fade_an>0) bhDrawScreenFade();
        if (movie.fade_steps<30) ++movie.fade_steps;
        /* Retail ps2mwPlySetOutVol maps the nominal 0..-999 movie volume
         * linearly with changeInputVolume(32.8 * (vol + 999)). Preserve its
         * float progression and integer truncation before converting the
         * PS2 0..32767 gain to the native SAS 0..4096 range.
         */
        movie.fade_volume-=999.0f/30.0f;
        int original=(int)movie.fade_volume;
        unsigned int ps2_volume=original<=-999 ? 0 : (unsigned int)(32.8*(original+999));
        int volume=(int)(((uint64_t)ps2_volume*4096u+16383u)/32767u);
        recvxMovieAudioVolume(volume);
    }
    uint64_t now=playback_clock();
    for (unsigned int count=0;count<CATCHUP_LIMIT && movie.pinned<0;++count) {
        if (movie.future<0 && !movie.eof) {
            int next=1-movie.front;
            if (decode(next,0)) movie.future=next;
        }
        if (movie.future<0 || movie.pts[movie.future]>now) break;
        movie.interval=movie.pts[movie.future]-movie.pts[movie.front];
        movie.front=movie.future; movie.future=-1;
    }
    if (movie.eof && movie.future<0 && !recvxMovieAudioActive() &&
        now>=movie.pts[movie.front] && now-movie.pts[movie.front]>=movie.interval) {
        PlayStopMovieEx(0);
        return 1;
    }
    return 0;
}
