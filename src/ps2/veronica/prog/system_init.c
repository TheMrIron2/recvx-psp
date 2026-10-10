#include "../../../ps2/veronica/prog/system.h"
#include "../../../ps2/veronica/prog/bup_00.h"
#include "../../../ps2/veronica/prog/main.h"
#include "../../../ps2/veronica/prog/message.h"
#include "../../../ps2/veronica/prog/njplus.h"
#include "../../../ps2/veronica/prog/ps2_sg_maloc.h"
#include "../../../ps2/veronica/prog/sync.h"

unsigned char pl_sleep_cnt;
unsigned char Pause_Flag;
unsigned char NowLoadDisp;
unsigned char PauseBtn;

void bhInitSystem()
{
    unsigned int free;
    unsigned int size;

    bhClearVSync();
    sys->ss_flg &= ~0x40440;
    npSetMemory((unsigned char *)&sys->tk_flg, 175260, 0);
    sys->memp = freemem;
    sys->endp = njpmemp;
    sys->memp += (int)&sys->save_end - (int)&sys->version;
    TypewriterKeepMemory();
    keepmem = sys->mempb = sys->memp;
    sys->loop_ct = 1;
    sys->p1per = NULL;
    sys->pt_flg |= ~0x0;
    sys->sp_flg |= ~0x0;
    sys->ss_flg &= ~0x300;
    plp = &ply;
    sys->plp = plp;
    sys->ef_pbnk = 32;
    syMallocStat(&free, &size);
    sys->fsize = free;
    njClipZ(-2.0f, -20000.0f);
    if ((sys->ss_flg & 0x2))
        sys->fog_cngct = 3;
    else
        sys->fog_cngct = 2;
    bhInitVSync();
    pl_sleep_cnt = 0;
    bhFontScaleSet(1.0f, 1.0f, 1.0f);
    Pause_Flag = 0;
    NowLoadDisp = 0;
    PauseBtn = 0;
}

void bhSysCallInit()
{
    bhInitSystem();
    sys->tk_flg = 0x300002;
}
