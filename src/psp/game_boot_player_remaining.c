#include <math.h>
#include <stdint.h>
#include "psp/game_boot.h"
#include "psp/game_boot_player_remaining.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/playpch.h"
#include "ps2/veronica/prog/sub1.h"

static void require(int valid, const char *reason)
{ if (!valid) recvxGameBootUnsupported(reason); }

static int owned(const void *pointer, size_t bytes)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem;
    uintptr_t end = sys ? (uintptr_t)sys->endp : 0;
    return pointer && !(p & 3u) && ((freemem && p >= first && p <= end && bytes <= end-p) ||
        recvxGameBootSourceRemaining(pointer) >= bytes);
}

O_WORK *recvxPlayerPose(BH_PWORK *actor, unsigned int joint)
{
    require(actor && actor->mlwP, "missing player action model");
    uintptr_t p = (uintptr_t)actor->mlwP, first = (uintptr_t)actor->mdl;
    require((p >= first && p-first < sizeof(actor->mdl) && !((p-first)%sizeof(ML_WORK))) ||
        owned(actor->mlwP, sizeof(ML_WORK)), "invalid player action model pointer");
    ML_WORK *model = actor->mlwP;
    require(model->obj_num <= 64 && joint < model->obj_num &&
        owned(model->owP, model->obj_num * sizeof(O_WORK)) &&
        owned(model->objP, model->obj_num * sizeof(NJS_CNK_OBJECT)),
        "invalid player action model joints");
    return &model->owP[joint];
}

unsigned int recvxPlayerMotionFrames(unsigned int index)
{
    require(sys && plp && index < 512 && owned(plp->mnwP, (index+1u)*sizeof(MN_WORK)),
        "invalid player controller motion table");
    unsigned int count = plp->mnwP[index].frm_num;
    if (!count || count > 32767)
        recvxGameBootLog("player motion unavailable: index=%u frames=%u modes=%u/%u/%u/%u",
            index,count,plp->mode0,plp->mode1,plp->mode2,plp->mode3);
    require(count && count <= 32767, "invalid player controller motion frames");
    return count;
}

float recvxPlayerGround(int index)
{
    require(rom && index >= 0 && index < 32 && isfinite(rom->grand[index]),
        "invalid player controller floor");
    return rom->grand[index];
}

void recvxGameBootPlayerRemainingPrepare(unsigned int kind)
{
    require(sys && plp && owned(plp->exp0, sizeof(EXP_WORK)) && owned(plp->exp1,124) &&
        sys->ply_id >= 0 && sys->ply_id < 4 && isfinite(plp->px) && isfinite(plp->py) &&
        isfinite(plp->pz), "invalid remaining player controller work");
    const EXP_WORK *expression = (const EXP_WORK *)plp->exp0;
    require(expression->wpntp >= 0 && expression->wpntp < 2 && expression->dmlvl >= 0 &&
        expression->dmlvl < 3 && isfinite(expression->rtspd) && isfinite(expression->spx) &&
        isfinite(expression->spz), "invalid remaining player controller selectors");
    require(kind <= 36 && kind != 6, "invalid remaining player controller kind");
    /* Maxima are the retail state-machine cases, including the ladder exit
       phases 4..6. Dispatchers validate their own selectors below. */
    static const unsigned char max_phase[37] = {
        1,2,255,255,255,1,0,255,6,5,5,5,7,1,6,6,7,1,1,1,4,1,1,
        255,2,1,1,1,1,4,1,1,4,1,4,255,3};
    require(plp->mode3 <= max_phase[kind], "invalid remaining player controller phase");
    if (kind == 0 || kind == 1)
        require(plp->mode1 <= 1 && (kind != 0 || plp->mode2 < 4),
            "invalid player damage selector");
    if (kind == 7) {
        unsigned int mode = plp->mode2;
        require((mode >= 64 && mode <= 72) || mode == 164 || mode == 169 || mode == 197,
            "invalid player attack dispatch");
    }
    if (kind == 23) require(plp->mode2 < 10, "invalid player event dispatch");
    if (kind == 25) require(plp->mode1 <= 2, "invalid player event turn selector");
    if (kind >= 26 && kind <= 28)
        require(plp->mode1 == 1 || plp->mode1 == 2, "invalid player event direction");
    if (kind == 29) require(plp->mode1 < 32, "invalid player basic event motion");
    if (kind == 30 || kind == 31)
        require(plp->wpnr_no >= 0 && plp->wpnr_no < 2, "invalid player event weapon motion");
    if (kind == 7 || (kind >= 17 && kind <= 22) || kind >= 33) {
        require(plp->wpnr_no >= 0 && plp->wpnr_no < 23, "invalid player attack weapon");
        if (kind >= 33 && kind <= 35)
            require(owned(plp->exp2, sizeof(PP_WORK)), "invalid player pitch context");
    }
    if ((kind >= 8 && kind <= 11) || kind == 14 || kind == 15) {
        require(rom && owned(sys->pl_htp, sizeof(ATR_WORK)) && sys->pl_htp->prm1 < 4,
            "invalid player traversal collision");
    }
    if (kind == 1 || (kind >= 8 && kind <= 22) || kind >= 33) recvxPlayerPose(plp,21);
}

void recvxPlayerFixPrepare(BH_PWORK *actor, const char *route)
{
    require(actor && route, "missing player position fix route");
    recvxPlayerPose(actor,0);
    unsigned int count = 0;
    while (count < 64 && (signed char)route[count] != -1) {
        require((unsigned char)route[count] < actor->mlwP->obj_num,
            "invalid player position fix joint");
        ++count;
    }
    require(count > 0 && count < 64, "invalid player position fix route");
    if (actor->flg & 0x80u) recvxPlayerPose((BH_PWORK *)actor->lkwkp, actor->lkono);
}

void recvxPlayerEyePrepare(void)
{
    require(sys && rom && plp && cam.ncut >= 0 && cam.ncut < rom->cut_n &&
        cam.camver >= 0 && cam.camver < 2 && owned(rom->cutp, (cam.ncut+1u)*sizeof(CUT_WORK)) &&
        cam.mode0 <= 2 && cam.ct0 >= 0, "invalid player eye camera state");
    require(rom->cutp[cam.ncut].cam[cam.camver].spd >= 0,
        "invalid player eye camera speed");
    recvxPlayerPose(plp,5);
}

void recvxPlayerEnemiesPrepare(void)
{
    require(sys && sys->ewk_n >= 0 && sys->ewk_n <= 128, "invalid player enemy count");
    for (int i=0; i<sys->ewk_n; ++i) {
        BH_PWORK *enemy = &ene[i];
        if ((enemy->flg & 0x21u) != 0x21u || (enemy->flg & 2u)) continue;
        if (enemy->flg & 0x80u) {
            uintptr_t p=(uintptr_t)enemy->lkwkp, first=(uintptr_t)ene;
            require(enemy->lkwkp == (unsigned char *)plp ||
                (p >= first && p-first < sizeof(ene) && !((p-first)%sizeof(BH_PWORK))) ||
                owned(enemy->lkwkp,sizeof(O_WRK)), "invalid player linked enemy");
        }
        const float *capsule = &enemy->watr.c1.x;
        for (int j=0;j<7;++j) require(isfinite(capsule[j]), "invalid player enemy capsule");
    }
}

void recvxPlayerHitPrepare(const GA_WORK *attack)
{
    require(plp && attack && attack->wpn_no >= 0 && attack->wpn_no < 22,
        "invalid player hit weapon");
    recvxPlayerEnemiesPrepare();
    for (int i=0;i<sys->ewk_n;++i) {
        BH_PWORK *enemy=&ene[i];
        if ((enemy->flg & 0x21u) != 0x21u || (enemy->flg & 2u)) continue;
        require(enemy->id < 31, "invalid player hit enemy type");
        recvxPlayerPose(enemy,0);
    }
}

void recvxPlayerBulletPrepare(void)
{
    uintptr_t p=(uintptr_t)swork.pip, first=sys?(uintptr_t)sys->itm:0;
    require(sys && plp && p>=first && p-first<=sizeof(sys->itm)-16*sizeof(unsigned int) &&
        !((p-first)%(16*sizeof(unsigned int))), "invalid player weapon inventory bank");
    require((short)swork.pip[0]>=2 && (short)swork.pip[0]<=11, "invalid player equipped inventory slot");
}

void recvxPlayerCapsulesPrepare(BH_PWORK *actor)
{
    recvxPlayerPose(actor,0);
    if (!actor->cpcl) return;
    for (unsigned int i=0;i<128;++i) {
        require(owned(actor->cpcl,(i+1u)*sizeof(CPCL)), "invalid player hit capsule list");
        CPCL *c=&actor->cpcl[i];
        if (!c->cap_r) return;
        require(c->cap_r>0 && c->jnt_a>=0 && c->jnt_b>=0 &&
            c->jnt_a<(int)actor->mlwP->obj_num && c->jnt_b<(int)actor->mlwP->obj_num,
            "invalid player hit capsule joint");
        if (c->jnt_a==c->jnt_b) {
            require(owned(actor->cpcl,(i+2u)*sizeof(CPCL)), "invalid player hit capsule offset");
            ++i;
        }
    }
    recvxGameBootUnsupported("unterminated player hit capsule list");
}
