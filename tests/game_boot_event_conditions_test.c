#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_event_core.h"

static SYS_WORK system_work;
static ROM_WORK room_work;
static BH_PWORK player;
static ETTY_WORK actors[2];
SYS_WORK *sys = &system_work;
ROM_WORK *rom = &room_work;
BH_PWORK *plp = &player;
BH_PWORK ene[128];
CAM_WORK cam;
static unsigned char section[64];
static unsigned int audio_slot;
static int audio_end;

int CheckPlayEndAdx(int slot)
{
    assert(slot >= 0 && slot < 2);
    audio_slot = (unsigned int)slot;
    return audio_end;
}

unsigned int (*bhScenarioJmpT[256])(void) = {
    [0] = bhEnd, [4] = bhCk, [5] = bhSet, [6] = bhCmpB, [7] = bhCmpW,
    [13] = bhDieCk, [27] = bhAdxCk,
    [129] = bhSubMapBusyCk, [188] = bhEvtKill
};

static unsigned int unsigned_compare(unsigned int value, unsigned int literal, unsigned int mode)
{
    switch (mode) {
    case 0: return value == literal;
    case 1: return value > literal;
    case 2: return value >= literal;
    case 3: return value < literal;
    case 4: return value <= literal;
    default: return value != literal;
    }
}

static unsigned int signed_compare(int value, int literal, unsigned int mode)
{
    switch (mode) {
    case 0: return value == literal;
    case 1: return value > literal;
    case 2: return value >= literal;
    case 3: return value < literal;
    case 4: return value <= literal;
    default: return value != literal;
    }
}

static void u32(unsigned char *p, unsigned int value)
{
    p[0] = value; p[1] = value >> 8;
    p[2] = value >> 16; p[3] = value >> 24;
}

static void setup(void)
{
    memset(&system_work, 0, sizeof(system_work));
    memset(&room_work, 0, sizeof(room_work));
    memset(&player, 0, sizeof(player));
    memset(actors, 0, sizeof(actors));
    memset(ene, 0, sizeof(ene));
    memset(section, 0, sizeof(section));
    sys = &system_work; rom = &room_work; plp = &player;
    rom->enep = actors; rom->ene_n = 2; actors[1].wrk_no = 127;
    u32(section, 12); u32(section + 4, 20); u32(section + 8, 28);
    recvxEventCoreReset();
    rom->evtp = (EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section, sizeof(section)));
}

static void expect_fault(enum RecvxEventCoreFaultCode code)
{
    unsigned int opcode = section[12];
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == code);
    assert(recvxEventCoreGetFault()->opcode == opcode);
    assert(bhScePtr == section + 12);
}

static void *flag_bank(unsigned int type, unsigned int *words)
{
    *words = 1;
    switch (type) {
    case 1: *words = 32; return sys->ev_flg;
    case 2: *words = 16; return sys->ky_flg;
    case 3: *words = 32; return sys->ed_flg;
    case 4: return &sys->rm_flg;
    case 5: return &sys->st_flg;
    case 6: return &sys->sp_flg;
    case 7: *words = 16; return sys->it_flg;
    case 8: *words = 8; return sys->mp_flg;
    case 9: *words = 16; return sys->ic_flg;
    case 10: return &sys->cb_flg;
    case 11: return &sys->gm_flg;
    case 12: return &sys->ts_flg;
    case 13: *words = sizeof(*plp) / 4; return &plp->flg;
    case 14: return &plp->stflg;
    case 15: return &plp->flg2;
    default: return &sys->ssd_flg;
    }
}

int main(void)
{
    static const unsigned int byte_selectors[] = {0, 1, 2, 8, 15, 17, 21, 23, 24, 25};
    static const unsigned int word_selectors[] = {5, 7, 9, 10};
    static const int values[] = {-1, 0, 1, 17, 127, 255, 256, 65535, INT_MAX, INT_MIN};
    unsigned int i, j, mode, literal, actual;
    /* Every original flag bank, both ends of its extent, and every bit. */
    for (i = 1; i <= 16; ++i) {
        unsigned int words, word;
        unsigned char *bank;
        setup(); bank = flag_bank(i, &words);
        for (word = 0; word < words && word < 32; ++word) {
            for (j = 0; j < 32; ++j) {
                unsigned int flag = word * 32 + j;
                uint32_t value = UINT32_C(0x80000000) >> j;
                memcpy(bank + word * 4, &value, 4);
                section[12] = 4; section[13] = i;
                section[14] = flag; section[15] = flag >> 8;
                section[16] = 7;
                sys->etc_idx = sys->flr_idx = 7;
                section[17] = 0; bhScePtr = section + 12;
                assert(bhCk() == 1 && bhScePtr == section + 18);
                section[17] = 1; bhScePtr = section + 12;
                assert(!bhCk() && bhScePtr == section + 18);
                value = 0; memcpy(bank + word * 4, &value, 4);
                bhScePtr = section + 12;
                assert(bhCk() == 1 && bhScePtr == section + 18);
                assert(!recvxEventCoreRunScenario(12));
            }
        }
    }
    for (i = 22; i <= 23; ++i) {
        setup(); section[12] = 4; section[13] = 10;
        section[14] = i; section[16] = 7;
        sys->cb_flg = UINT32_C(0x80000000) >> i;
        sys->etc_idx = sys->flr_idx = 6;
        bhScePtr = section + 12;
        assert(!bhCk() && bhScePtr == section + 18);
        section[17] = 1; bhScePtr = section + 12;
        assert(!bhCk()); /* Index mismatch ignores the inversion byte. */
        sys->etc_idx = sys->flr_idx = 7; section[17] = 0;
        bhScePtr = section + 12; assert(bhCk() == 1);
    }
    setup(); section[12] = 4; section[13] = 10; section[14] = 3;
    sys->cb_flg = 0x10000000;
    assert(!recvxEventCoreRunScenario(12)); /* Shipped SCD1 instruction at 1008. */
    setup(); section[12] = 4; section[13] = 10; section[14] = 32;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    for (i = 4; i <= 5; ++i) {
        setup(); section[12] = i; section[13] = 16; section[14] = 3;
        sys->ssd_flg = 0x10000000; plp = NULL;
        assert(!recvxEventCoreRunScenario(12));
        setup(); section[12] = i; section[13] = 16; sys = NULL;
        expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    }
    for (i = 0; i < sizeof(byte_selectors) / sizeof(byte_selectors[0]); ++i) {
        for (j = 0; j < sizeof(values) / sizeof(values[0]); ++j) {
            int value = values[j];
            setup();
            sys->stg_no = sys->rom_no = sys->pos_no = sys->rcase = sys->ply_id = (char)value;
            sys->sb_id = sys->gm_mode = sys->costume = cam.ncut = value;
            bhEtask[0].wpnl_no = value;
            bhEtask[5].wpnl_no = ~value; bhCetask = &bhEtask[5];
            if (byte_selectors[i] == 0 || byte_selectors[i] == 1 ||
                byte_selectors[i] == 15 || byte_selectors[i] == 21 || byte_selectors[i] == 24)
                value = (char)value;
            for (literal = 0; literal < 256; literal += 17) {
                for (mode = 0; mode < 6; ++mode) {
                    section[12] = 6; section[13] = byte_selectors[i];
                    section[14] = mode; section[15] = literal;
                    bhScePtr = section + 12; actual = bhCmpB();
                    assert(actual == unsigned_compare((unsigned int)value, literal, mode));
                    assert(bhScePtr == section + 16);
                }
            }
        }
    }

    for (i = 0; i < sizeof(word_selectors) / sizeof(word_selectors[0]); ++i) {
        for (j = 0; j < sizeof(values) / sizeof(values[0]); ++j) {
            int value = values[j];
            setup(); plp->hp = ene[127].hp = sys->mes_sel = sys->stv_tm = value;
            for (literal = 0; literal <= 65535; literal += 21845) {
                for (mode = 0; mode < 6; ++mode) {
                    section[12] = 7; section[13] = 1; section[14] = word_selectors[i];
                    section[15] = mode; section[16] = literal; section[17] = literal >> 8;
                    bhScePtr = section + 12; actual = bhCmpW();
                    assert(actual == signed_compare(value, (int)literal, mode));
                    assert(bhScePtr == section + 18);
                }
            }
        }
    }

    for (i = 0; i < 16; ++i) {
        setup();
        for (j = 0; j < 16; ++j) { bhEtask[j].status = j + 1; bhEtask[j].wpnl_no = 123; }
        section[12] = 188; section[13] = i;
        assert(!recvxEventCoreRunScenario(12));
        assert(bhEtask[i].status == 0);
        for (j = 0; j < 16; ++j) {
            assert(bhEtask[j].wpnl_no == 123);
            if (j != i) assert(bhEtask[j].status == j + 1);
        }
    }

    for (i = 0; i < 256; ++i) {
        for (j = 0; j < 4; ++j) {
            setup(); section[12] = 129; section[13] = i;
            sys->st_flg = 0x100 | ((j & 1) ? 8 : 0) | ((j & 2) ? 0x40000 : 0);
            bhScePtr = section + 12;
            assert(bhSubMapBusyCk() == (unsigned int)(j ? i == 0 : i != 0));
            assert(bhScePtr == section + 14);
        }
    }
    for (i = 0; i < 4; ++i) {
        unsigned int opcode = i == 0 ? 6 : i == 1 ? 7 : i == 2 ? 129 : 188;
        unsigned int size = opcode == 6 ? 4 : opcode == 7 ? 6 : 2;
        setup(); section[12] = opcode; section[14] = opcode == 7 ? 5 : 0;
        u32(section + 4, 12 + size - 1);
        assert(!recvxEventCoreBindSection(section, sizeof(section)));
        expect_fault(RECVX_EVENT_FAULT_TRUNCATED);
    }

    setup(); section[12] = 6; section[13] = 3;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(); section[12] = 6; section[14] = 6;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(); section[12] = 6; sys = NULL;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    /* Camera and task-zero selectors retain their independence from SYS_WORK. */
    setup(); section[12] = 6; section[13] = 2; sys = NULL;
    assert(!recvxEventCoreRunScenario(12));
    setup(); section[12] = 6; section[13] = 17; sys = NULL;
    assert(!recvxEventCoreRunScenario(12));

    setup(); section[12] = 7; section[14] = 6;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(); section[12] = 7; section[14] = 5; section[15] = 6;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(); section[12] = 7; section[14] = 5; plp = NULL;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    for (i = 7; i <= 9; i += 2) {
        setup(); section[12] = 7; section[14] = i; sys = NULL;
        expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    }
    for (i = 0; i < 5; ++i) {
        setup(); section[12] = 7; section[13] = 1; section[14] = 10;
        if (i == 0) rom = NULL;
        else if (i == 1) rom->enep = NULL;
        else if (i == 2) rom->ene_n = -1;
        else if (i == 3) section[13] = 2;
        else actors[1].wrk_no = 128;
        expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    }
    setup(); section[12] = 129; sys = NULL;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(); section[12] = 188; section[13] = 16;
    bhEtask[0].status = 1;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    assert(bhEtask[0].status == 1);

    for (i = 0; i < 1024; ++i) {
        unsigned int word = i / 32;
        uint32_t mask = UINT32_C(0x80000000) >> (i % 32);
        setup(); section[12] = 13; section[13] = 1;
        section[14] = i; section[15] = (i >> 8) | 0xa4;
        bhScePtr = section + 12;
        assert(bhDieCk() == 1 && bhScePtr == section + 16 && !sys->ed_flg[word]);
        ene[127].flg = 2;
        assert(!recvxEventCoreRunScenario(12));
        assert(sys->ed_flg[word] == mask);
        ene[127].flg = 0;
        assert(!recvxEventCoreRunScenario(12));
        assert(sys->ed_flg[word] == mask);
        for (j = 0; j < 32; ++j) if (j != word) assert(!sys->ed_flg[j]);
    }
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 2; ++j) {
            setup(); section[12] = 27; section[13] = 255;
            section[14] = i; section[15] = 255; section[16] = 255; section[17] = 255;
            audio_end = j; audio_slot = 99;
            bhScePtr = section + 12;
            assert(bhAdxCk() == j && bhScePtr == section + 18 && audio_slot == i);
            assert(!recvxEventCoreRunScenario(12));
            assert(audio_slot == i);
        }
    }

    for (i = 0; i < 2; ++i) {
        setup(); section[12] = i ? 27 : 13;
        u32(section + 4, i ? 17 : 15);
        assert(!recvxEventCoreBindSection(section, sizeof(section)));
        expect_fault(RECVX_EVENT_FAULT_TRUNCATED);
    }
    for (i = 0; i < 6; ++i) {
        setup(); section[12] = 13; section[13] = 1;
        if (i == 0) sys = NULL;
        else if (i == 1) rom = NULL;
        else if (i == 2) rom->enep = NULL;
        else if (i == 3) rom->ene_n = -1;
        else if (i == 4) section[13] = 2;
        else actors[1].wrk_no = 128;
        expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    }
    setup(); section[12] = 27; section[14] = 2; audio_slot = 99;
    expect_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    assert(audio_slot == 99);

    puts("PASS: original flag/byte/word conditions, sign boundaries, all death flags, native ADPCM completion queries, map busy checks, sixteen task kills, and bounded dispatch");
    return 0;
}
