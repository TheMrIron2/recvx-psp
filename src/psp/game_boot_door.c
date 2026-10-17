#include <limits.h>
#include <math.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#include "psp/game_boot.h"
#include "psp/game_boot_audio.h"
#include "ps2/veronica/prog/door.h"

_door_wrk DoorWrk;

void recvxGameBootDoorPrepare(void)
{
    if (DoorWrk.spd_mde < DMO_SPD_1 || DoorWrk.spd_mde > DMO_SPD_3 ||
        DoorWrk.time < 0 || DoorWrk.time > INT_MAX-3)
        recvxGameBootUnsupported("invalid original door animation clock");
    for (unsigned int i=0; i<16; ++i)
        if (DoorWrk.BgmSet[i]==-1) return;
    recvxGameBootUnsupported("unterminated original door sound schedule");
}

void recvxGameBootDoorDrawPrepare(void)
{
    if (!isfinite(DoorWrk.vew_pos.x) || !isfinite(DoorWrk.vew_pos.y) || !isfinite(DoorWrk.vew_pos.z) ||
        !DoorWrk.dor_mdl.objP)
        recvxGameBootUnsupported("invalid original door drawing state");
    if ((DoorWrk.status & 0x100u) && (!isfinite(DoorWrk.fde_rate) ||
        DoorWrk.fde_rate < 0 || DoorWrk.fde_rate > 1))
        recvxGameBootUnsupported("invalid original door fade rate");
}

void CallDoorSe(unsigned int sound)
{
    /* Retail door sequences use MIDI slot 2, bank 1, centered pan and full
       gain. The existing native SAS sequencer owns compressed playback. */
    if ((sound & 0xffffff00u) != 0x80000100u)
        recvxGameBootUnsupported("invalid original door sound identifier");
    recvxAudioPlayMidi(2, (int)sound, 0, 0);
    const char *fault = recvxAudioFault();
    if (fault) recvxGameBootUnsupported(fault);
    if (recvxAudioNativeError()) recvxGameBootUnsupported("native door sound service failed");
}
