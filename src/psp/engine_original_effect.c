#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"

static void initialize_effect_quad(O_WRK *work)
{
    work->mtx = (float(*)[16])work->mtxbuf;
    work->pvp = work->pv;
    work->tvp = work->tv;
    work->pn = 4;
    work->tv[0].x = -1.0f;
    work->tv[0].y = -1.0f;
    work->tv[0].z = 0;
    work->tv[1].x = 1.0f;
    work->tv[1].y = -1.0f;
    work->tv[1].z = 0;
    work->tv[2].x = -1.0f;
    work->tv[2].y = 1.0f;
    work->tv[2].z = 0;
    work->tv[3].x = 1.0f;
    work->tv[3].y = 1.0f;
    work->tv[0].u = 0;
    work->tv[0].v = 0;
    work->tv[1].u = 1.0f;
    work->tv[1].v = 0;
    work->tv[2].u = 0;
    work->tv[2].v = 1.0f;
    work->tv[3].u = 1.0f;
    work->tv[3].v = 1.0f;
}

// Extracted from original effect.c::bhSetEffectTb
int bhSetEffectTb(EF_WORK *effect, NJS_POINT3 *offset,
                  unsigned char *link, int joint)
{
    int index;

    for (index = 0; index < 512; ++index) {
        O_WRK *work = &eff[index];

        if (work->flg & 0x3)
            continue;
        npSetMemoryL(&work->flg, sizeof(O_WRK) / 4, 0);
        work->flg = effect->flg;
        work->id = effect->id;
        work->type = effect->type;
        work->tex_id = -1;
        work->mdlver = effect->mdlver;
        work->flr_no = effect->flr_no;
        work->px = effect->px;
        work->py = effect->py;
        work->pz = effect->pz;
        work->sx = effect->sx;
        work->sy = effect->sy;
        work->sz = effect->sz;
        work->sxb = effect->sx;
        work->syb = effect->sy;
        work->szb = effect->sz;
        work->ax = effect->ax;
        work->ay = effect->ay;
        work->az = 0;
        work->mlwP = &sys->efm[effect->id];
        if (offset != NULL) {
            work->lox = offset->x;
            work->loy = offset->y;
            work->loz = offset->z;
        }
        work->lkwkp = link;
        if (link != NULL)
            work->flg |= 0x80;
        work->lkono = joint;
        initialize_effect_quad(work);
        return index;
    }
    return -1;
}

// Extracted from original effect.c::bhSetShadow
int bhSetShadow(char *joints, unsigned char *link, int joint,
                float sx, float sy, float sz)
{
    int index;

    for (index = 0; index < 512; ++index) {
        O_WRK *work = &eff[index];
        int i;

        if (work->flg & 0x3)
            continue;
        npSetMemoryL(&work->flg, sizeof(O_WRK) / 4, 0);
        work->flg = 0x240001;
        work->id = 1;
        work->type = joints != NULL ? 0 : 1;
        work->tex_id = -1;
        work->mdlver = 0;
        work->px = 0;
        work->py = 0;
        work->pz = 0;
        work->sx = sx;
        work->sy = sy;
        work->sz = sz;
        work->sxb = sx;
        work->syb = sy;
        work->szb = sz;
        work->ax = 0;
        work->ay = 0;
        work->az = 0;
        work->mlwP = &sys->efm[work->id];
        work->lkwkp = link;
        if (link != NULL)
            work->flg |= 0x80;
        work->lkono = joint;
        if (joints == NULL) {
            work->jno[0] = -1;
        } else {
            for (i = 0; i < 15; ++i) {
                work->jno[i] = *joints++;
                if (work->jno[i] == -1)
                    break;
            }
        }
        initialize_effect_quad(work);
        return index;
    }
    return -1;
}
