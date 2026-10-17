#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_event_core.h"

static SYS_WORK system_work;
static ROM_WORK room_work;
SYS_WORK *sys = &system_work;
ROM_WORK *rom = &room_work;
BH_PWORK *plp;
BH_PWORK ene[128];
static BH_PWORK player;
static ATR_WORK attributes[3];
static O_WRK items[4];
static unsigned char section[64];

unsigned int (*bhScenarioJmpT[256])(void) = {
    [0] = bhEnd, [14] = bhItmCk,
    [15] = bhUseItemClear, [16] = bhUseItemCheck,
    [38] = bhArmsItemCheck, [39] = bhArmsItemChange
};

static void u32(unsigned char *p, unsigned int value)
{
    p[0] = value; p[1] = value >> 8;
    p[2] = value >> 16; p[3] = value >> 24;
}

static void setup(unsigned int flag, unsigned int retain)
{
    memset(&system_work, 0, sizeof(system_work));
    memset(&room_work, 0, sizeof(room_work));
    memset(&player, 0, sizeof(player));
    plp = &player;
    memset(items, 0, sizeof(items));
    memset(attributes, 0, sizeof(attributes));
    memset(section, 0, sizeof(section));
    u32(section, 12); u32(section + 4, 20); u32(section + 8, 28);
    section[12] = 14; section[14] = flag; section[15] = flag >> 8;
    section[16] = 1; section[17] = retain;
    recvxEventCoreReset();
    rom->evtp = (EVT_WORK *)section;
    rom->etcp = attributes; rom->etc_n = 3;
    sys->itwp = items; sys->etc_idx = 1;
    attributes[1].flg = 0x85; attributes[1].prm0 = 2;
    items[2].stflg = 0x1234;
    assert(!recvxEventCoreBindSection(section, sizeof(section)));
}

int main(void)
{
    unsigned int flag, i;
    /* Every valid bank boundary and bit position, including upper flag bits. */
    for (flag = 0; flag < 512; ++flag) {
        unsigned int word = flag / 32;
        uint32_t mask = UINT32_C(0x80000000) >> (flag % 32);
        setup(flag | 0xa400, 0);
        assert(!recvxEventCoreRunScenario(12));
        assert(sys->ic_flg[word] == mask && !sys->it_flg[word]);
        assert(attributes[1].flg == 0x85 && items[2].stflg == 0x1234);
        for (i = 0; i < 16; ++i)
            if (i != word) assert(!sys->it_flg[i] && !sys->ic_flg[i]);

        sys->cb_flg = 0x4800;
        assert(!recvxEventCoreRunScenario(12));
        assert(sys->it_flg[word] == mask && !sys->ic_flg[word]);
        assert(attributes[1].flg == 0x84 && items[2].stflg == 0x1001234);
        assert(sys->cb_flg == 0x4000);
        attributes[1].flg = 0x85;
        items[2].stflg = 0x1234;
        sys->cb_flg |= 0x800;
        assert(!recvxEventCoreRunScenario(12));
        assert(attributes[1].flg == 0x85 && items[2].stflg == 0x1234);
        assert(sys->cb_flg == 0x4000); /* Already collected remains unchanged. */
    }

    setup(31, 1); sys->cb_flg = 0x800;
    assert(!recvxEventCoreRunScenario(12));
    assert(attributes[1].flg == 0x85 && items[2].stflg == 0x1001234);

    setup(31, 0); sys->cb_flg = 0x800; sys->etc_idx = 2;
    assert(!recvxEventCoreRunScenario(12));
    assert(sys->cb_flg == 0x800 && !sys->it_flg[0] && !sys->ic_flg[0]);
    assert(attributes[1].flg == 0x85 && items[2].stflg == 0x1234);

    setup(512, 0);
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(0, 0); section[16] = 3;
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(0, 0); attributes[1].prm0 = 255;
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(0, 0); u32(section + 4, 17);
    /* Rebind the truncated SCD0; dispatch must fail before touching item work. */
    assert(!recvxEventCoreBindSection(section, sizeof(section)));
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_TRUNCATED);
    assert(attributes[1].flg == 0x85 && items[2].stflg == 0x1234);

    /* Condition handlers consume their operand regardless of their result. */
    setup(0, 0); section[12] = 16; section[13] = 72;
    sys->sb_id = 72; bhScePtr = section + 12;
    assert(!bhUseItemCheck() && bhScePtr == section + 14);
    sys->cb_flg = 0x1400; bhScePtr = section + 12;
    assert(bhUseItemCheck() == 1 && bhScePtr == section + 14);
    sys->sb_id = 73; bhScePtr = section + 12;
    assert(!bhUseItemCheck() && sys->cb_flg == 0x1400);
    bhScePtr = section + 12;
    assert(!bhUseItemClear() && sys->cb_flg == 0x1000 && bhScePtr == section + 14);

    for (i = 15; i <= 16; ++i) {
        setup(0, 0); section[12] = (unsigned char)i;
        u32(section + 4, 13); /* Operand lies beyond the SCD0 boundary. */
        assert(!recvxEventCoreBindSection(section, sizeof(section)));
        sys->cb_flg = 0x400;
        assert(recvxEventCoreRunScenario(12));
        assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_TRUNCATED);
        assert(recvxEventCoreGetFault()->opcode == i && sys->cb_flg == 0x400);
        assert(bhScePtr == section + 12);

        setup(0, 0); section[12] = (unsigned char)i;
        sys = NULL;
        assert(recvxEventCoreRunScenario(12));
        assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND);
        assert(recvxEventCoreGetFault()->opcode == i && bhScePtr == section + 12);
        sys = &system_work;
    }

    for (i = 0; i < 256; ++i) {
        setup(0, 0); section[12] = 38; section[13] = (unsigned char)i;
        plp->wpnr_no = (int)i; bhScePtr = section + 12;
        assert(bhArmsItemCheck() == 1 && bhScePtr == section + 14);
        plp->wpnr_no = -1; bhScePtr = section + 12;
        assert(!bhArmsItemCheck() && bhScePtr == section + 14);
        section[12] = 39;
        sys->ply_id = i % 4;
        memset(sys->ply_wno, 0x55, sizeof(sys->ply_wno));
        sys->mn_mode0 = sys->mn_mode1 = sys->mn_mode2 = sys->mn_mode3 = 0xff;
        bhScePtr = section + 12;
        assert(bhArmsItemChange() == 1 && bhScePtr == section + 14);
        assert(plp->wpnr_no == (int)i && (unsigned char)sys->ply_wno[(int)sys->ply_id] == i);
        assert(sys->mn_mode0 == 3 && !sys->mn_mode1 && !sys->mn_mode2 && !sys->mn_mode3);
        assert((unsigned char)sys->ply_wno[(sys->ply_id + 1) % 4] == 0x55);
    }
    setup(0, 0); section[12] = 38; section[13] = 7; plp->wpnr_no = 7;
    assert(!recvxEventCoreRunScenario(12));
    setup(0, 0); section[12] = 38; plp = NULL;
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(0, 0); section[12] = 38; u32(section + 4, 13);
    assert(!recvxEventCoreBindSection(section, sizeof(section)));
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_TRUNCATED);

    setup(0, 0); section[12] = 39; section[13] = 7;
    assert(!recvxEventCoreRunScenario(12));
    assert(plp->wpnr_no == 7 && sys->ply_wno[0] == 7 && sys->mn_mode0 == 3);
    for (i = 0; i < 5; ++i) {
        setup(0, 0); section[12] = 39; section[13] = 7;
        plp->wpnr_no = 3;
        if (i == 0) {
            u32(section + 4, 13);
            assert(!recvxEventCoreBindSection(section, sizeof(section)));
        } else if (i == 1) sys = NULL;
        else if (i == 2) plp = NULL;
        else sys->ply_id = i == 3 ? -1 : 4;
        assert(recvxEventCoreRunScenario(12));
        assert(recvxEventCoreGetFault()->code ==
            (i == 0 ? RECVX_EVENT_FAULT_TRUNCATED : RECVX_EVENT_FAULT_BAD_OPERAND));
        assert(recvxEventCoreGetFault()->opcode == 39 && bhScePtr == section + 12);
        assert(player.wpnr_no == 3 && !system_work.mn_mode0);
        sys = &system_work;
    }

    puts("PASS: all 512 item flags, item attribute/work updates, collected and mismatched items, use-item conditions, all 256 weapon operands, and bounded inventory dispatch");
    return 0;
}
