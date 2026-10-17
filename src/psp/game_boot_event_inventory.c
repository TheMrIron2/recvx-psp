#include <stdint.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

/* Original event.c handlers. The shared event interpreter validates instruction
 * extents, flag banks, room attributes, and item-work indices before dispatch.
 * Unsigned masks preserve the PS2 bit tests without signed-shift overflow.
 */
unsigned int bhItmCk(void)
{
    unsigned int flag = (unsigned int)bhScePtr[2] |
                        ((unsigned int)bhScePtr[3] << 8);
    unsigned int index = bhScePtr[4];
    unsigned int retain_attribute = bhScePtr[5];
    unsigned int word = (flag & 0x3e0u) >> 5;
    uint32_t mask = UINT32_C(0x80000000) >> (flag & 31u);

    bhScePtr += 6;
    if (sys->etc_idx != index)
        return 1;

    if (sys->cb_flg & 0x800u) {
        if (!(sys->it_flg[word] & mask)) {
            ATR_WORK *attribute = &rom->etcp[index];
            O_WRK *item = &sys->itwp[attribute->prm0];
            if (!retain_attribute)
                attribute->flg &= ~1u;
            item->stflg |= 0x1000000u;
            sys->it_flg[word] |= mask;
            sys->ic_flg[(flag & 0x1e0u) >> 5] &= ~mask;
        }
    } else if (!(sys->it_flg[word] & mask)) {
        sys->ic_flg[word] |= mask;
    }

    sys->cb_flg &= ~0x800u;
    return 1;
}

unsigned int bhUseItemClear(void)
{
    bhScePtr += 2;
    sys->cb_flg &= ~0x400u;
    return 0;
}

unsigned int bhUseItemCheck(void)
{
    int item = bhScePtr[1];
    bhScePtr += 2;
    return (sys->cb_flg & 0x400u) ? sys->sb_id == item : 0;
}

unsigned int bhArmsItemCheck(void)
{
    int weapon = bhScePtr[1];
    bhScePtr += 2;
    return plp->wpnr_no == weapon;
}

unsigned int bhArmsItemChange(void)
{
    int weapon = bhScePtr[1];
    uint32_t mode = 3;
    bhScePtr += 2;
    plp->wpnr_no = weapon;
    sys->ply_wno[(int)sys->ply_id] = weapon;
    memcpy(&sys->mn_mode0, &mode, sizeof(mode));
    return 1;
}
