#include <float.h>
#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/playpch.h"
#include "ps2/veronica/prog/Motion.h"

extern const char PlyFlip[23];

static int contains(const void *base, size_t capacity, const void *pointer,
                    size_t size, unsigned int alignment)
{
    uintptr_t first = (uintptr_t)base, address = (uintptr_t)pointer;
    return base && pointer && !(address & (alignment - 1u)) && address >= first &&
        address - first <= capacity && size <= capacity - (address - first);
}

static int arena(const void *pointer, size_t size, unsigned int alignment)
{
    uintptr_t first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return end >= first && contains(freemem, end - first, pointer, size, alignment);
}

static int motion_data(const void *pointer, size_t size, unsigned int alignment)
{
    return (arena(sys->plbmtp, 393216, 4) &&
            contains(sys->plbmtp, 393216, pointer, size, alignment)) ||
        (arena(sys->plwmtp, 65536, 4) &&
            contains(sys->plwmtp, 65536, pointer, size, alignment)) ||
        (arena(sys->plzmtp, 8192, 4) &&
            contains(sys->plzmtp, 8192, pointer, size, alignment));
}

void recvxGameBootArmIkPrepare(BH_PWORK *actor, int base, NJS_POINT3 *target)
{
    if (!sys || !plp || actor != plp || !target || ((uintptr_t)target & 3u))
        recvxGameBootUnsupported("invalid original arm IK actor or target");
    ML_WORK *model = actor->mlwP;
    if (!contains(actor->mdl,sizeof(actor->mdl),model,sizeof(*model),4) ||
        ((uintptr_t)model-(uintptr_t)actor->mdl)%sizeof(*model) ||
        model->obj_num<4 || model->obj_num>128 || base<0 ||
        (unsigned int)base>model->obj_num-4 ||
        !arena(model->objP,model->obj_num*sizeof(*model->objP),4) ||
        !arena(model->owP,model->obj_num*sizeof(*model->owP),4))
        recvxGameBootUnsupported("invalid original arm IK joint chain");
    const float *point=&target->x;
    const float *matrix=model->owP[base].mtx;
    for(unsigned int i=0;i<3;++i)
        if(!isfinite(point[i]))recvxGameBootUnsupported("non-finite original arm IK target");
    for(unsigned int i=0;i<16;++i)
        if(!isfinite(matrix[i]))recvxGameBootUnsupported("non-finite original arm IK matrix");
    NJS_CNK_OBJECT *joints=&model->objP[base];
    for(unsigned int i=0;i<3;++i)
        if(!isfinite(joints[1].pos[i]))recvxGameBootUnsupported("invalid original arm IK shoulder");
    float upper=joints[2].pos[0],lower=joints[3].pos[0];
    const float limit=sqrtf(FLT_MAX)/8;
    if(!isfinite(upper)||!isfinite(lower)||fabsf(upper)<1e-6f||fabsf(lower)<1e-6f||
        fabsf(upper)>limit||fabsf(lower)>limit)
        recvxGameBootUnsupported("invalid original arm IK bone length");
    NJS_MATRIX inverse;
    memcpy(inverse,matrix,sizeof(inverse));
    if(!njInvertMatrix(&inverse))recvxGameBootUnsupported("singular original arm IK matrix");
    NJS_POINT3 local;
    njCalcPoint(&inverse,target,&local);
    if(recvxEngineOriginalFault())recvxGameBootUnsupported(recvxEngineOriginalFault());
    for(unsigned int i=0;i<3;++i){
        float distance=(&local.x)[i]-joints[1].pos[i];
        if(!isfinite(distance)||fabsf(distance)>limit)
            recvxGameBootUnsupported("invalid original arm IK local target");
    }
}

/* Asset installation validates the original motion streams before relocation.
   This boundary also bounds each mutable frame and motion-table selection. */
void recvxGameBootPlayerPitchPrepare(BH_PWORK *actor)
{
    static const unsigned int joints[] = {4, 5, 7, 11};
    PP_WORK *pitch;
    ML_WORK *model;
    MN_WORK *motion;
    unsigned int i, frame;
    if (!sys || !actor || actor != plp || actor->wpnr_no < 0 || actor->wpnr_no >= 23 ||
        !arena(actor->exp2, sizeof(PP_WORK), 4))
        recvxGameBootUnsupported("invalid original player pitch work");
    if (!(WpnTab[actor->wpnr_no].flg & 0x800)) return;
    model = actor->mlwP;
    if (!contains(actor->mdl, sizeof(actor->mdl), model, sizeof(*model), 4) ||
        ((uintptr_t)model - (uintptr_t)actor->mdl) % sizeof(*model) ||
        model->obj_num < 12 || model->obj_num > 128 ||
        !arena(model->objP, model->obj_num * sizeof(*model->objP), 4) ||
        !arena(model->owP, model->obj_num * sizeof(*model->owP), 4))
        recvxGameBootUnsupported("invalid original player pitch model");
    if (actor->mode0 != 1 || actor->mode1 == 0) return;
    pitch = (PP_WORK *)actor->exp2;
    if (!arena(actor->exp1, 4, 4) || pitch->count < 0 ||
        pitch->hed_rate < 0 || pitch->hed_rate > 65536 ||
        pitch->arm_dl < -32768 || pitch->arm_dl > 32767 ||
        pitch->arm_dr < -32768 || pitch->arm_dr > 32767 ||
        actor->hokan_rate < 0 || actor->hokan_rate > 65536 ||
        actor->mtn_no >= 512 || actor->frm_no < 0 ||
        !arena(sys->plmthp, 12288, 4) ||
        !contains(sys->plmthp, 12288, actor->mnwP,
                  (actor->mtn_no + 1u) * sizeof(MN_WORK), 4))
        recvxGameBootUnsupported("invalid original player pitch motion state");
    motion = &actor->mnwP[actor->mtn_no];
    frame = (unsigned int)actor->frm_no / 65536u;
    if (motion->obj_num < 12 || motion->obj_num > 128 || frame >= motion->frm_num ||
        !motion_data(motion->md2P, motion->obj_num * sizeof(*motion->md2P), 4) ||
        ((actor->mtn_md & 2) && actor->mtn_tp != (unsigned char *)PlyFlip))
        recvxGameBootUnsupported("invalid original player pitch motion records");
    for (i = 0; i < sizeof(joints) / sizeof(joints[0]); ++i) {
        unsigned int joint = joints[i];
        const void *keys;
        if (actor->mtn_md & 2) joint = (unsigned char)PlyFlip[joint];
        if (joint >= motion->obj_num)
            recvxGameBootUnsupported("invalid original player pitch joint");
        keys = motion->md2P[joint].p[1];
        if (keys && !motion_data(keys, ((size_t)frame + 1) * sizeof(NJS_MKEY_A_MOD), 2))
            recvxGameBootUnsupported("invalid original player pitch rotation keys");
    }
}
