#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/effect.h"

static int first_update;
void recvxGameBootEffectControlReset(void) { first_update = 1; }
void recvxGameBootEffectUpdated(void)
{
    if (first_update && ((sys->sp_flg & 8u) || (sys->ef_flg & 1u))) {
        first_update = 0;
        recvxGameBootLog("effects updated: active=%d sprites=%d shadows=%d overlays=%d callbacks=%d",
            sys->ef_ctrn, sys->ef_trsn, sys->ef_mdfn, sys->ef_trs2dn, sys->ef_fncn);
    }
}

static int actor_array(const void *pointer, const void *base, size_t stride, size_t count)
{
    uintptr_t address = (uintptr_t)pointer, first = (uintptr_t)base;
    return base && address >= first && address - first < stride * count &&
        (address - first) % stride == 0;
}

void recvxGameBootEffectValidate(O_WRK *effect)
{
    BH_PWORK *parent;
    if (!(effect->flg & 0x80)) return;
    parent = (BH_PWORK *)effect->lkwkp;
    if (!parent || (parent != plp && !actor_array(parent, ene, sizeof(*ene), 128) &&
        !actor_array(parent, sys->obwp, sizeof(O_WRK), 39936u / sizeof(O_WRK)) &&
        !actor_array(parent, sys->itwp, sizeof(O_WRK), 39936u / sizeof(O_WRK)) &&
        !actor_array(parent, eff, sizeof(*eff), 512)))
        recvxGameBootUnsupported("effect links an invalid actor");
    if (((parent->flg & 1u) || effect->id == 1) && (!parent->mlwP || !parent->mlwP->owP ||
        effect->lkono < 0 || (unsigned int)effect->lkono >= parent->mlwP->obj_num))
        recvxGameBootUnsupported("effect links an invalid actor joint");
}

void recvxGameBootEffectQueueCheck(int *count, size_t capacity)
{
    if (*count < 0 || (size_t)*count >= capacity)
        recvxGameBootUnsupported("original effect render queue is full");
}

/* Preserve the original effect texture selection using owned descriptors.
   The reconstructed alternate-list cast in bhGetGidx is not a TEXLIST. */
unsigned int bhGetGidx(O_WRK *effect)
{
    NJS_TEXLIST *list;
    unsigned int index;
    if (effect->tex_id < 0 || effect->ani_ct < 0)
        recvxGameBootUnsupported("invalid original effect texture selection");
    if (effect->stflg & 0x20) {
        list = effect->txp[0];
        index = (unsigned int)effect->tex_id;
    } else {
        if (effect->tex_id >= 450 || sys->ef_tn[effect->tex_id] < 0)
            recvxGameBootUnsupported("invalid original effect texture mapping");
        list = &sys->ef_tlist;
        index = (unsigned int)sys->ef_tn[effect->tex_id];
    }
    index += (unsigned int)effect->ani_ct;
    if (!recvxGameBootTextureGet(list, index)->source)
        recvxGameBootUnsupported("effect texture has no owned surface");
    return ((NJS_TEXMEMLIST *)(uintptr_t)list->textures[index].texaddr)->globalIndex;
}
