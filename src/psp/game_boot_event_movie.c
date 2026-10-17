#include <stdint.h>

#include "psp/game_boot.h"
#include "psp/game_boot_event_movie.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/system.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/ps2_NaFog.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"

unsigned int bhMovieStart(void)
{
    unsigned int movie = bhScePtr[1];
    bhScePtr += 4;
    sys->mvi_no = movie;
    sys->cb_flg |= 0x4000000u;
    return 1;
}

unsigned int bhMovieStop(void)
{
    bhScePtr += 2;
    sys->cb_flg &= ~0x4000000u;
    return 1;
}

unsigned int bhMovieCk(void)
{
    bhScePtr += 6;
    /* Retail CheckPlayEndMovie returns ExecMovieSystemFlag, which is one
     * during playback. Keep this event condition's original polarity.
     */
    return CheckPlayEndMovie() != 0;
}

static void owned_cursor(const unsigned char *cursor)
{
    uintptr_t first = (uintptr_t)freemem;
    uintptr_t end = (uintptr_t)sys->endp;
    uintptr_t position = (uintptr_t)cursor;
    if (!freemem || end < first || position < first || position > end)
        recvxGameBootUnsupported("movie memory cursor exceeds original arena");
}

/* system.c::bhSysCallMovie retains its original seven stages. The PSP owns
 * texture surfaces in RAM, so its movie path has no PS2 texture-memory
 * evacuation transaction (the retail mvi_flg == 0 branch). Playback and its
 * wait/end/cancel states still come from the native movie service.
 */
void recvxGameBootSysCallMovie(void)
{
    int result;
    if (!sys || sys->mvi_md > 6)
        recvxGameBootUnsupported("invalid original movie task mode");
    switch (sys->mvi_md) {
    case 0: {
        uintptr_t position = (uintptr_t)sys->memp;
        owned_cursor(sys->memp);
        if (position > UINTPTR_MAX - 63u)
            recvxGameBootUnsupported("movie arena alignment overflow");
        position = (position + 63u) & ~(uintptr_t)63u;
        owned_cursor((const unsigned char *)position);
        sys->mvi_flg = 0;
        sys->memp = sys->mvi_memp = (unsigned char *)position;
        ++sys->mvi_md;
        break;
    }
    case 1:
        ++sys->mvi_md;
        break;
    case 2:
        if (sys->mvi_no < 0 || sys->mvi_no >= RECVX_GAME_BOOT_MOVIE_COUNT)
            recvxGameBootUnsupported("original movie ID exceeds movie table");
        if (PlayStartMovieEx(sys->mvi_no, sys->mvi_tp, 1) != 0)
            recvxGameBootUnsupported("native movie service failed to start");
        ++sys->mvi_md;
        break;
    case 3:
        result = WaitPrePlayMovie(0);
        switch (result) {
        case 0:
            sys->ts_flg |= 0x80;
            sys->ts_flg &= ~0x100;
            sys->sp_flg |= 0x30;
            njSetBackColor(0, 0, 0);
            ++sys->mvi_md;
            break;
        case 1:
            break;
        case 2:
            sys->ts_flg &= ~0x40;
            sys->mvi_md = 5;
            break;
        case 3:
            PlayStopMovieEx(0);
            RequestAllStopSoundEx(1, 1, 0);
            bhReturnTitle();
            sys->ss_flg &= ~0x1000;
            ClearSoftResetKeyFlag(-1);
            sys->ss_flg &= ~0x80000;
            break;
        default:
            recvxGameBootUnsupported("invalid native movie preparation status");
        }
        break;
    case 4:
        njFogDisable();
        njSetBackColor(0, 0, 0);
        result = PlayMovieMain(0);
        if (result < 0 || result > 3)
            recvxGameBootUnsupported("invalid native movie playback status");
        if (result != 0) {
            sys->ts_flg &= ~0x40;
            ++sys->mvi_md;
        }
        break;
    case 5:
        owned_cursor(sys->mvi_memp);
        if ((uintptr_t)sys->mvi_memp & 63u)
            recvxGameBootUnsupported("invalid saved movie memory alignment");
        njFogDisable();
        njSetBackColor(0, 0, 0);
        sys->memp = sys->mvi_memp;
        ++sys->mvi_md;
        break;
    case 6:
        sys->bcl_ct = 1;
        sys->gm_flg |= 0x8000;
        if (sys->st_flg & 2u) njFogEnable();
        sys->sp_flg = sys->mvi_spb;
        sys->ts_flg = sys->mvi_tsb;
        break;
    }
}
