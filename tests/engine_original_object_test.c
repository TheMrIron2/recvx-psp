#include <stdio.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/objitm.h"

static SYS_WORK engine_sys;
static O_WRK object_pool[64];
static O_WRK item_pool[64];
SYS_WORK *sys = &engine_sys;

void npSetMemory(unsigned char *memory, unsigned int size, char value)
{
    memset(memory, (unsigned char)value, size);
}

static int check_placement(void)
{
    ETTY_WORK source;
    O_WRK *work;

    memset(&source, 0, sizeof(source));
    memset(&engine_sys, 0, sizeof(engine_sys));
    memset(object_pool, 0xa5, sizeof(object_pool));
    memset(item_pool, 0xa5, sizeof(item_pool));
    sys->obwp = object_pool;
    sys->itwp = item_pool;
    bhInitObjItm();
    source.flg = 0xa0000001u;
    source.id = 17;
    source.type = 0x1234;
    source.flr_no = 3;
    source.mdlver = 2;
    source.prm1 = 5;
    source.px = 1.25f;
    source.py = 2.5f;
    source.pz = -3.75f;
    source.ax = 100;
    source.ay = 200;
    source.az = 300;
    source.aspd = 4;
    source.hide[0] = 1;
    source.hide[1] = 2;
    source.hide[2] = 3;
    source.hide[3] = 4;
    work = bhSetObject(&source, 7, (unsigned char *)&engine_sys);
    if (work != &sys->obwp[7] || work->flg != source.flg ||
        work->mdflg != 0x50 || work->id != 17 || work->type != 0x34 ||
        work->param != 0x12 || work->flr_no != 3 || work->mdlver != 2 ||
        work->draw_tp != 5 || work->px != 1.25f || work->py != 2.5f ||
        work->pz != -3.75f || work->ax != 100 || work->ay != 200 ||
        work->az != 300 || work->aspd != 4 || work->hide[0] != 1 ||
        work->hide[3] != 4 || work->lkwkp != (unsigned char *)&engine_sys ||
        work->mtx != (void *)work->mtxbuf || work->clp_jno[0] != 0 ||
        work->clp_jno[1] != -1 || work->idx_ct != 7) {
        fprintf(stderr, "object setup: flags %08x/%08x id %u type %u param %u floor %d model %u draw %d pos %.2f %.2f %.2f angle %d %d %d speed %d hide %u %u link %p matrix %p/%p clip %d %d index %d\n",
                work->flg, work->mdflg, work->id, work->type, work->param,
                work->flr_no, work->mdlver, work->draw_tp, work->px,
                work->py, work->pz, work->ax, work->ay, work->az,
                work->aspd, work->hide[0], work->hide[3], (void *)work->lkwkp,
                work->mtx, (void *)work->mtxbuf, work->clp_jno[0],
                work->clp_jno[1], work->idx_ct);
        return -1;
    }

    source.flg = 1;
    source.id = 6;
    work = bhSetItem(&source, 2, NULL);
    if (work != &sys->itwp[2] || work->id != 6 || work->idx_ct != 2 ||
        work->flg != 1 || work->mdflg != 0 || work->mtx != (void *)work->mtxbuf)
        return -1;
    return 0;
}

int main(void)
{
    if (check_placement() != 0) {
        fputs("FAIL: original object/item placement fields\n", stderr);
        return 1;
    }
    puts("PASS: original object/item placement fields");
    return 0;
}
