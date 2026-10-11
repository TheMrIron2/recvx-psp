#include "ps2/veronica/prog/face.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_original_fault.h"

// Preserves original maskless actor setup and faults on unsupported mask data
void bhInitMask(BH_PWORK *player)
{
    if (player == 0 || player->mlwP == 0 || player->mlwP->owP == 0) {
        recvxEngineOriginalFaultSet("original actor mask setup has invalid model work");
        return;
    }
    if (player->mskp != 0) {
        recvxEngineOriginalFaultSet("original actor mask realization is unsupported");
        return;
    }
    player->mdflg &= ~0x100;
    player->mlwP->owP[6].flg |= 0x4;
    player->mlwP->owP[7].flg |= 0x4;
    player->mlwP->owP[8].flg |= 0x4;
    player->mlwP->owP[9].flg |= 0x4;
    player->mlwP->owP[10].flg |= 0x4;
}
