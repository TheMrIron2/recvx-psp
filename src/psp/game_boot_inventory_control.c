#include <stdint.h>
#include <math.h>
#include "psp/game_boot_inventory_control.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/item.h"

void recvxGameBootInventoryControlPrepare(void)
{
    unsigned int i;
    unsigned int mode;
    if (!sys || !plp || (unsigned char)sys->ply_id > 4)
        recvxGameBootUnsupported("invalid inventory player");
    mode = swork.subscreenmode & 0xbf;
    if (mode != 1 && mode != 2 && mode != 4 && mode != 8 && mode != 0x10 && mode != 0x80)
        recvxGameBootUnsupported("invalid inventory screen state");
    if (mode != 1 && swork.pip != &sys->itm[(unsigned char)sys->ply_id * 16])
        recvxGameBootUnsupported("inventory screen requires initialized item storage");
    if (swork.itemid >= 156 || (unsigned char)sys->sb_id >= 156 ||
        swork.listcsr_0 >= 16 || swork.listcsr_1 >= 16 ||
        swork.listcsr_2 >= 16 || swork.listcsr_3 >= 16)
        recvxGameBootUnsupported("invalid inventory selection");
    for (i = 0; i < 383; ++i)
        if (((sys->itm[i] >> 16) & 255) >= 156)
            recvxGameBootUnsupported("invalid inventory item descriptor");
    if (swork.sprflg > 2 || swork.wn > 2 || swork.dflg < 0 || swork.dflg > 4 ||
        swork.hp > 4 || swork.abc > 4)
        recvxGameBootUnsupported("invalid inventory window animation");
}

void recvxGameBootInventoryWorkPrepare(S_WORK *work)
{
    unsigned int *inventory;
    if (!sys || !plp || work != &swork || (unsigned char)sys->ply_id > 4)
        recvxGameBootUnsupported("invalid inventory work");
    inventory = &sys->itm[(unsigned char)sys->ply_id * 16];
    if ((work->pip && work->pip != inventory) ||
        (work->bxp && work->bxp != &sys->itm[64] && work->bxp != &sys->itm[192] && work->bxp != &sys->itm[224]))
        recvxGameBootUnsupported("invalid inventory item storage");
    if (work->listcsr_0 >= 16 || work->listcsr_1 >= 16 || work->listcsr_2 >= 16 || work->listcsr_3 >= 16 ||
        work->itemid >= 156 || work->maincsr > 5 || work->subcsr > 11)
        recvxGameBootUnsupported("invalid inventory work selection");
}

void recvxGameBootInventorySpritePrepare(const PARTS *part)
{
    static const unsigned int count[23] = {8,9,8,16,5,8,4,4,4,23,20,4,4,4,24,8,16,4,16,32,16,8,8};
    if (!part || part->parts_num >= 23 || part->anim < 0 ||
        (unsigned int)part->anim >= count[part->parts_num] || part->cen_no >= 12 ||
        !isfinite(part->pos[0]) || !isfinite(part->pos[1]) || !isfinite(part->pos[2]))
        recvxGameBootUnsupported("invalid inventory sprite descriptor");
}

void recvxGameBootInventoryNamePrepare(const NJS_POINT2 *position, int id,
                                     int color, int count, float priority)
{
    const unsigned char *data;
    size_t size, offset;
    uint32_t encoded;
    unsigned int i;
    if (!sys || !position || id < 0 || id >= 160 || color < 0 || color > 10 || count < 0 ||
        !isfinite(position->x) || !isfinite(position->y) || !isfinite(priority))
        recvxGameBootUnsupported("invalid inventory item-name request");
    data = (const unsigned char *)sys->mes_ip;
    size = recvxGameBootSourceRemaining(data);
    if (!data || size < ((size_t)id + 2) * 4)
        recvxGameBootUnsupported("missing inventory item-name table");
    encoded = sys->mes_ip[id + 1];
    offset = encoded;
    if ((offset & 1) || offset >= size)
        recvxGameBootUnsupported("invalid inventory item-name offset");
    for (i = 0; i < 1024 && offset + 1 < size; ++i, offset += 2) {
        unsigned int code = data[offset] | ((unsigned int)data[offset + 1] << 8);
        if (code == 0xffff) return;
        if (count && i + 1 == (unsigned int)count) return;
    }
    recvxGameBootUnsupported("unterminated inventory item name");
}

void recvxGameBootInventorySlotPrepare(const unsigned int *slot)
{
    if (!sys) recvxGameBootUnsupported("missing inventory item storage");
    uintptr_t p=(uintptr_t)slot, first=(uintptr_t)sys->itm;
    if (!slot || p<first || p-first>=383*sizeof(unsigned int) || (p-first)%sizeof(unsigned int))
        recvxGameBootUnsupported("invalid inventory item slot pointer");
    if (((*slot>>16)&255u)>=156)
        recvxGameBootUnsupported("invalid inventory item slot descriptor");
}

void recvxGameBootInventoryActionPrepare(S_WORK *work, int action)
{
    recvxGameBootInventoryWorkPrepare(work);
    if (!work->pip || !work->bxp || sys->gm_mode<0 || sys->gm_mode>=4 || work->testmode>32)
        recvxGameBootUnsupported("invalid inventory item action state");
    if ((unsigned char)sys->sb_id>=156 || work->ips1>=156)
        recvxGameBootUnsupported("invalid inventory action item selector");
    if (action==1) {
        unsigned int count=0;
        for (;count<14;++count)
            if (!(itemdata[(work->pip[count+2]>>16)&255u].type&0x100)) break;
        if(count==14)recvxGameBootUnsupported("item box inventory has no ordinary slot");
    }
}

void recvxGameBootInventoryCombinePrepare(short position, const unsigned int *source,
                                        const unsigned int *destination, int action)
{
    recvxGameBootInventoryActionPrepare(&swork,2);
    recvxGameBootInventorySlotPrepare(source);
    recvxGameBootInventorySlotPrepare(destination);
    if (source==destination || position<0 || position>=409)
        recvxGameBootUnsupported("invalid item combination operands");
    if (action==3 && (combidata[position+1]<0 || combidata[position+1]>=156))
        recvxGameBootUnsupported("invalid item combination result");
    if (action==5 && (!(*source&65535u) || (*source&65535u)>4))
        recvxGameBootUnsupported("invalid item combination charge count");
}

void recvxGameBootInventoryEventItemPrepare(void)
{
    if (!(sys->cb_flg&0x200u)) return;
    uintptr_t p=rom?(uintptr_t)rom->flrp:0, end=(uintptr_t)sys->endp;
    if (!rom || !freemem || !p || (p&3u) || p<(uintptr_t)freemem || p>end ||
        rom->flr_n<0 || (size_t)rom->flr_n>(end-p)/sizeof(ATR_WORK) ||
        sys->flr_idx>=(unsigned int)rom->flr_n)
        recvxGameBootUnsupported("invalid event item trigger floor");
}
