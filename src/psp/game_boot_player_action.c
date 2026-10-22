#include <math.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"

extern const unsigned short PlMtnAct[2][3][7];

static int contains(const void *base, size_t capacity, const void *pointer, size_t bytes)
{
    uintptr_t first = (uintptr_t)base, address = (uintptr_t)pointer;
    return base && pointer && !(address & 3u) && address >= first &&
        address - first <= capacity && bytes <= capacity - (address - first);
}

static int arena(const void *pointer, size_t bytes)
{
    uintptr_t first = (uintptr_t)freemem, last = (uintptr_t)sys->endp;
    return last >= first && contains(freemem, last - first, pointer, bytes);
}

static EXP_WORK *base(void)
{
    if (!sys || !plp || !arena(plp->exp0, sizeof(EXP_WORK)) || !arena(plp->exp1, 124) ||
        !arena(sys->plmthp, 12288) ||
        !contains(sys->plmthp, 12288, plp->mnwP, (plp->mtn_no + 1u) * sizeof(MN_WORK)) ||
        plp->mtn_no >= 512 || plp->frm_no < 0 || sys->ply_id < 0 || sys->ply_id >= 4 ||
        plp->wpnr_no < 0 || plp->wpnr_no >= 23)
        recvxGameBootUnsupported("invalid original player action work");
    EXP_WORK *expression = (EXP_WORK *)plp->exp0;
    if (expression->wpntp < 0 || expression->wpntp >= 2 ||
        expression->dmlvl < 0 || expression->dmlvl >= 3 ||
        !isfinite(expression->spx) || !isfinite(expression->spz) ||
        !isfinite(expression->spxn) || !isfinite(expression->spzn) ||
        !isfinite(expression->rtspd) || !isfinite(plp->px) || !isfinite(plp->pz))
        recvxGameBootUnsupported("invalid original player action selectors");
    return expression;
}

static void motion(unsigned int index)
{
    if (index >= 512 || !contains(sys->plmthp, 12288, plp->mnwP,
            ((size_t)index + 1u) * sizeof(MN_WORK)) ||
        !plp->mnwP[index].frm_num || plp->mnwP[index].frm_num > 32767) {
        recvxGameBootLog("player action motion rejected: index=%u current=%u modes=%u/%u/%u/%u",
            index, plp->mtn_no, plp->mode0, plp->mode1, plp->mode2, plp->mode3);
        recvxGameBootUnsupported("invalid original player action motion");
    }
}

void recvxGameBootPlayerActionPrepare(int kind)
{
    EXP_WORK *expression = base();
    const ML_WORK *model = plp->mlwP;
    if (kind < 0 || kind > 8 || (kind >= 2 && plp->mode3 > (kind == 4 ? 2 : 1)))
        recvxGameBootUnsupported("invalid original player action phase");
    if (kind <= 1) return;
    unsigned int damage = (kind == 2 || kind == 3 || kind == 5 || kind == 6) &&
        (sys->gm_flg & 0x1000000u) ? 0 : (unsigned int)expression->dmlvl;
    static const unsigned int actions[] = {0, 0, 0, 1, 6, 2, 3, 4, 5};
    unsigned int selected = PlMtnAct[expression->wpntp][damage][actions[kind]];
    motion(selected);
    /* The retail entry phases replace the previous event/damage motion.
       Its number can refer to an empty slot in the restored player table.
       Only require that old animation when the controller actually keeps it
       or reads its frame count to preserve the phase of a moving animation. */
    int reads_current = plp->mode3 != 0;
    if (kind == 5 || kind == 7) reads_current = 0; /* Reassigned in phase 1 too. */
    if (plp->mode3 == 0)
        reads_current = kind == 6 || kind == 8 ||
            (kind == 5 && (plp->flg & 0x800000u)) ||
            (kind == 7 && (plp->flg & 0x2000000u));
    if (reads_current) motion(plp->mtn_no);
    if (kind == 4 && plp->mode3 < 2) motion(selected + 1u);
    if ((kind == 7 || kind == 8) && (sys->pad_ps & 0x400u))
        motion(PlMtnAct[expression->wpntp][damage][3]);
    if (kind >= 3 && (!contains(plp->mdl, sizeof(plp->mdl), model, sizeof(*model)) ||
        ((uintptr_t)model - (uintptr_t)plp->mdl) % sizeof(*model) ||
        model->obj_num < 22 || model->obj_num > 128 ||
        !arena(model->owP, model->obj_num * sizeof(*model->owP))))
        recvxGameBootUnsupported("invalid original player action model");
}

void recvxGameBootPlayerKegaPrepare(int weapon, int damage, int action)
{
    base();
    if (weapon < 0 || weapon >= 2 || damage < 0 || damage >= 3 || action < 0 || action >= 7)
        recvxGameBootUnsupported("invalid original player injury action");
    int next_damage = plp->hp >= 120 ? !!(plp->stflg & 0x280000u) : (plp->hp >= 30 ? 1 : 2);
    motion(PlMtnAct[weapon][damage][action]);
    motion(PlMtnAct[weapon][next_damage][action]);
}

void recvxGameBootPlayerFramePrepare(unsigned int old_frames, unsigned int new_frames, int frame)
{
    if (!old_frames || old_frames > 32767 || !new_frames || new_frames > 32767 ||
        frame < 0 || (unsigned int)frame / 65536u >= old_frames)
        recvxGameBootUnsupported("invalid original player frame conversion");
}

void recvxGameBootPlayerTransPrepare(int index)
{
    size_t cursor = 0;
    base();
    if (index < 0 || index >= 32 || !arena(sys->plzmtp, 8192))
        recvxGameBootUnsupported("invalid original player translation stream");
    for (int i = 0; i <= index; ++i) {
        uint32_t length;
        if (cursor > 8192u - 4u)
            recvxGameBootUnsupported("invalid original player translation record");
        memcpy(&length, sys->plzmtp + cursor, 4); cursor += 4;
        if (length == UINT32_MAX || (length & 3u) || length > 8192u - cursor)
            recvxGameBootUnsupported("invalid original player translation record");
        if (i == index) {
            size_t frame = (unsigned int)plp->frm_no / 65536u + 1u;
            float speed;
            if (frame >= length / 4u)
                recvxGameBootUnsupported("invalid original player translation frame");
            memcpy(&speed, sys->plzmtp + cursor + frame * 4u, 4);
            if (!isfinite(speed)) recvxGameBootUnsupported("invalid original player translation speed");
            return;
        }
        cursor += length;
    }
}

void recvxGameBootPlayerBackSearchPrepare(const NJS_POINT3 *position, float length)
{
    base();
    if (!position || ((uintptr_t)position & 3u) || !isfinite(position->x) || !isfinite(position->y) ||
        !isfinite(position->z) || !isfinite(length) || length < 0 ||
        sys->ewk_n < 0 || sys->ewk_n > 128)
        recvxGameBootUnsupported("invalid original backward enemy search");
    for (int i = 0; i < sys->ewk_n; ++i) {
        const BH_PWORK *enemy = &ene[i];
        if ((enemy->flg & 0x21u) != 0x21u || (enemy->flg & 0x8002u) ||
            (enemy->flg2 & 0x10u) || (enemy->stflg & 0x41000000u)) continue;
        if (enemy->flg & 0x80u) {
            uintptr_t address = (uintptr_t)enemy->lkwkp, first = (uintptr_t)ene;
            uintptr_t effects = (uintptr_t)eff;
            int valid_enemy = address >= first && address - first < sizeof(ene) &&
                !((address - first) % sizeof(*ene));
            int valid_effect = address >= effects && address - effects < sizeof(eff) &&
                !((address - effects) % sizeof(*eff));
            if (!enemy->lkwkp || (enemy->lkwkp != (unsigned char *)plp &&
                !valid_enemy && !valid_effect && !arena(enemy->lkwkp, sizeof(O_WRK))))
                recvxGameBootUnsupported("invalid original backward search linked actor");
            if (((const O_WRK *)enemy->lkwkp)->stflg & 0x1000000u) continue;
        }
        const float *point = &enemy->watr.c1.x;
        for (unsigned int j = 0; j < 6; ++j)
            if (!isfinite(point[j])) recvxGameBootUnsupported("invalid original backward search capsule");
    }
}
