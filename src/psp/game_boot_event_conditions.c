#include <stdint.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "psp/engine_event_core.h"

unsigned int bhCk(void)
{
    unsigned char *command = bhScePtr;
    const void *bank;
    unsigned int type = command[1];
    unsigned int flag = command[2] | ((unsigned int)command[3] << 8);
    uint32_t value;
    bhScePtr += 6;
    switch (type) {
    case 1: bank = sys->ev_flg; break;
    case 2: bank = sys->ky_flg; break;
    case 3: bank = sys->ed_flg; break;
    case 4: bank = &sys->rm_flg; break;
    case 5: bank = &sys->st_flg; break;
    case 6: bank = &sys->sp_flg; break;
    case 7: bank = sys->it_flg; break;
    case 8: bank = sys->mp_flg; break;
    case 9: bank = sys->ic_flg; break;
    case 10:
        bank = &sys->cb_flg;
        if ((command[2] == 23 && sys->etc_idx != command[4]) ||
            (command[2] == 22 && sys->flr_idx != command[4]))
            return 0;
        break;
    case 11: bank = &sys->gm_flg; break;
    case 12: bank = &sys->ts_flg; break;
    case 13: bank = &plp->flg; break;
    case 14: bank = &plp->stflg; break;
    case 15: bank = &plp->flg2; break;
    case 16: bank = &sys->ssd_flg; break;
    default:
        recvxEventCoreFailUnsupportedCommand(4, command);
        return 0;
    }
    memcpy(&value, (const unsigned char *)bank + ((flag & 0x3ffu) >> 5) * sizeof(value), sizeof(value));
    return command[5] ^ ((value >> (31u - (flag & 31u))) & 1u);
}

/* Original event.c::bhCmpB compares an unsigned value, even when its source
 * is a signed character. bhCmpW instead uses signed integer comparisons.
 * The shared interpreter validates selector, operand, and task/actor bounds.
 */
unsigned int bhCmpB(void)
{
    unsigned char *command = bhScePtr;
    unsigned int value, literal = command[3];
    bhScePtr += 4;
    switch (command[1]) {
    case 0: value = sys->stg_no; break;
    case 1: value = sys->rom_no; break;
    case 2: value = cam.ncut; break;
    case 8: value = sys->sb_id; break;
    case 15: value = sys->pos_no; break;
    case 17: value = bhEtask[0].wpnl_no; break;
    case 21: value = sys->rcase; break;
    case 23: value = sys->gm_mode; break;
    case 24: value = sys->ply_id; break;
    case 25: value = sys->costume; break;
    default:
        recvxEventCoreFailUnsupportedCommand(6, command);
        return 0;
    }
    switch (command[2]) {
    case 0: return value == literal;
    case 1: return value > literal;
    case 2: return value >= literal;
    case 3: return value < literal;
    case 4: return value <= literal;
    case 5: return value != literal;
    default:
        recvxEventCoreFailUnsupportedCommand(6, command);
        return 0;
    }
}

unsigned int bhCmpW(void)
{
    unsigned char *command = bhScePtr;
    int value, literal = command[4] | ((unsigned int)command[5] << 8);
    bhScePtr += 6;
    switch (command[2]) {
    case 5: value = plp->hp; break;
    case 7: value = sys->mes_sel; break;
    case 9: value = sys->stv_tm; break;
    case 10: value = ene[rom->enep[command[1]].wrk_no].hp; break;
    default:
        recvxEventCoreFailUnsupportedCommand(7, command);
        return 0;
    }
    switch (command[3]) {
    case 0: return value == literal;
    case 1: return value > literal;
    case 2: return value >= literal;
    case 3: return value < literal;
    case 4: return value <= literal;
    case 5: return value != literal;
    default:
        recvxEventCoreFailUnsupportedCommand(7, command);
        return 0;
    }
}

unsigned int bhEvtKill(void)
{
    unsigned int task = bhScePtr[1];
    bhScePtr += 2;
    bhEtask[task].status = 0;
    return 1;
}

unsigned int bhSubMapBusyCk(void)
{
    unsigned int desired = bhScePtr[1];
    bhScePtr += 2;
    return (sys->st_flg & 0x40008u) ? !desired : !!desired;
}

unsigned int bhDieCk(void)
{
    unsigned int actor = bhScePtr[1];
    unsigned int flag = bhScePtr[2] | ((unsigned int)bhScePtr[3] << 8);
    unsigned int word = (flag & 0x3ffu) >> 5;
    uint32_t mask = UINT32_C(0x80000000) >> (flag & 31u);
    bhScePtr += 4;
    if (!(sys->ed_flg[word] & mask) && (ene[rom->enep[actor].wrk_no].flg & 2u))
        sys->ed_flg[word] |= mask;
    return 1;
}

unsigned int bhAdxCk(void)
{
    unsigned int slot = bhScePtr[2];
    bhScePtr += 6;
    return CheckPlayEndAdx((int)slot);
}
