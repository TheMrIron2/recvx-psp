#include <math.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_item_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/item.h"
#include "ps2/veronica/prog/njplus.h"

static int arena(const void *pointer, size_t bytes)
{
    uintptr_t p = (uintptr_t)pointer, end = sys ? (uintptr_t)sys->endp : 0;
    return pointer && freemem && !(p & 3u) && p >= (uintptr_t)freemem && p <= end && bytes <= end-p;
}
static void tree(NJS_CNK_OBJECT *object, unsigned int depth, unsigned int *count)
{
    if (depth > 60) recvxGameBootUnsupported("inventory model hierarchy exceeds depth");
    for (; object; object = object->sibling) {
        if (++*count > 128 || !arena(object, sizeof(*object)))
            recvxGameBootUnsupported("invalid or cyclic inventory model hierarchy");
        for (unsigned int i=0;i<3;++i)
            if (!isfinite(object->pos[i]) || !isfinite(object->scl[i]))
                recvxGameBootUnsupported("invalid inventory model transform");
        if (object->model) {
            if (!arena(object->model, sizeof(*object->model)) || !arena(object->model->vlist, 8))
                recvxGameBootUnsupported("invalid inventory model vertex header");
            HDR_PS header;
            memcpy(&header, object->model->vlist, 8);
            size_t offset = header.ucType == 51 ? 64 : 8;
            size_t stride = header.ucType == 51 ? 32 : 24;
            if (!arena(object->model->vlist, offset + stride * header.usIndexMax))
                recvxGameBootUnsupported("inventory model vertices exceed arena");
        }
        tree(object->child, depth+1, count);
    }
}
void recvxGameBootItemViewTreePrepare(NJS_CNK_OBJECT *object)
{
    unsigned int count=0;
    if (!object) recvxGameBootUnsupported("missing inventory model hierarchy");
    tree(object,0,&count);
}
void recvxGameBootItemViewPrepare(int phase)
{
    if (!sys || swork.itemid >= 160 || swork.subcsr > 3 || swork.testmode > 9 ||
        dsptbl[swork.itemid].hide < 0 || dsptbl[swork.itemid].hide >= 41)
        recvxGameBootUnsupported("invalid inventory model selection");
    const unsigned char *item = itemflg[dsptbl[swork.itemid].hide];
    if (item[2] >= 10 || item[6] >= 16)
        recvxGameBootUnsupported("invalid inventory model presentation data");
    if (phase == 2 && (sys->ply_id < 0 || sys->ply_id > 4 || swork.ips1 >= 156 || swork.mesid >= 160 ||
        swork.pip != &sys->itm[(unsigned char)sys->ply_id * 16] ||
        ((item[4]&15u) && (!(item[4]>>4) || (item[4]&15u)+(item[4]>>4)>14 || item[5]+(item[4]>>4)>14))))
        recvxGameBootUnsupported("invalid inventory inspection action state");
    if (phase == 4) {
        NJS_CNK_OBJECT *child = sitem.mdl.objP->child;
        if ((item[3] && !child) || (swork.itemid == 55 && (!child || !child->child)) ||
            (swork.itemid == 139 && (!child || !child->sibling || !child->sibling->sibling)))
            recvxGameBootUnsupported("invalid inventory model special hierarchy");
    }
    if (phase == 0 && (sitem.mw.flg & 1u) &&
        (sitem.mw.rdid < 1 || sitem.mw.rdid > 144 || !arena(sitem.keep, 0x20000)))
        recvxGameBootUnsupported("invalid inventory model read buffer");
    if (phase == 0 && sys->sb_ppp && (sitem.mw.flg & 2u) &&
        (!arena(sitem.keep, sys->sb_rdsz) || !arena(sys->sb_ppp, 4) || !arena(sys->sb_mlb, 4)))
        recvxGameBootUnsupported("invalid inventory model read result");
    if (phase == 1 && (swork.statusflg & 0x400u)) {
        size_t bytes = recvxGameBootSourceRemaining(sitem.mw.mdl_p);
        if (recvxGameBootValidateModel(sitem.mw.mdl_p, bytes) ||
            recvxGameBootPvpTextureCount(sitem.mw.tex_p, recvxGameBootSourceRemaining(sitem.mw.tex_p)) <= 0)
            recvxGameBootUnsupported("invalid inventory model or texture resource");
    }
    if (phase == 3) {
        recvxGameBootItemViewTreePrepare(sitem.mdl.objP);
        if (!isfinite(st_cam.pos_0.z) || !isfinite(st_cam.pos_1.z))
            recvxGameBootUnsupported("invalid inventory model camera");
        if (sitem.mw.rdid == 139 && (!sitem.mdl.objP->child ||
            !sitem.mdl.objP->child->sibling || !sitem.mdl.objP->child->sibling->sibling))
            recvxGameBootUnsupported("invalid inventory file model hierarchy");
    }
}

void recvxGameBootItemViewActionPrepare(SITEM *item, int action)
{
    recvxGameBootItemViewPrepare(3);
    if (item!=&sitem || action<0 || action>2)
        recvxGameBootUnsupported("invalid inventory model action");
    unsigned int depth=action==1 && swork.itemid==128 ? 5 : action==1 && swork.itemid==63 ? 4 : 3;
    NJS_CNK_OBJECT *object=item->mdl.objP;
    for(unsigned int i=0;i<depth;++i){
        object=object->child;
        if(!object)recvxGameBootUnsupported("missing inventory model action joint");
    }
}
