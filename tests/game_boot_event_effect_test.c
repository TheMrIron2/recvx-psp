#include <assert.h>
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
O_WRK eff[512];
static unsigned char section[64];

unsigned int (*bhScenarioJmpT[256])(void) = {
    [0] = bhEnd, [145] = bhEffModeSet, [173] = bhEffTypeSet
};

static void u32(unsigned char *p, unsigned int value)
{
    p[0] = value; p[1] = value >> 8;
    p[2] = value >> 16; p[3] = value >> 24;
}

static void setup(unsigned int opcode)
{
    unsigned int i;
    sys = &system_work;
    memset(sys, 0, sizeof(*sys));
    memset(&room_work, 0, sizeof(room_work));
    memset(eff, 0, sizeof(eff));
    memset(section, 0, sizeof(section));
    for (i = 0; i < 256; ++i) sys->efid[i] = -1;
    u32(section, 12); u32(section + 4, 20); u32(section + 8, 28);
    section[12] = opcode; section[13] = 9; section[14] = 123; section[15] = 255;
    rom->evtp = (EVT_WORK *)section;
    recvxEventCoreReset();
    assert(!recvxEventCoreBindSection(section, sizeof(section)));
}

static void fault(enum RecvxEventCoreFaultCode code)
{
    assert(recvxEventCoreRunScenario(12));
    assert(recvxEventCoreGetFault()->code == code);
    assert(recvxEventCoreGetFault()->opcode == section[12]);
    assert(bhScePtr == section + 12);
}

int main(void)
{
    unsigned int opcode, slot, mode, authored;
    O_WRK before, after;
    for (opcode = 145; opcode <= 173; opcode += 28) {
        setup(opcode);
        for (slot = 0; slot < 512; ++slot) {
            sys->efid[9] = slot;
            eff[slot].flg = 0x184; eff[slot].id = 127;
            eff[slot].mode0 = 4; eff[slot].mode1 = 5;
            eff[slot].mode2 = 6; eff[slot].mode3 = 7; eff[slot].type = 8;
            before = eff[slot];
            for (mode = 0; mode < 256; ++mode) {
                section[14] = mode;
                bhScePtr = section + 12;
                assert((opcode == 145 ? bhEffModeSet() : bhEffTypeSet()) == 1);
                assert(bhScePtr == section + 16);
                after = before;
                if (opcode == 145) after.mode1 = mode; else after.type = mode;
                assert(!memcmp(&after, &eff[slot], sizeof(after)));
            }
            assert(!recvxEventCoreRunScenario(12));
        }
        for (authored = 0; authored < 256; ++authored) {
            sys->efid[authored] = 511 - authored;
            section[13] = authored; section[14] = authored;
            bhScePtr = section + 12;
            assert((opcode == 145 ? bhEffModeSet() : bhEffTypeSet()) == 1);
            assert((opcode == 145 ? eff[511 - authored].mode1 : eff[511 - authored].type) == authored);
        }
        setup(opcode); sys->efid[9] = -1;
        fault(RECVX_EVENT_FAULT_BAD_OPERAND);
        setup(opcode); sys->efid[9] = 512;
        fault(RECVX_EVENT_FAULT_BAD_OPERAND);
        setup(opcode); sys = NULL;
        fault(RECVX_EVENT_FAULT_BAD_OPERAND);
        setup(opcode); sys->efid[9] = 0;
        u32(section + 4, 15);
        assert(!recvxEventCoreBindSection(section, sizeof(section)));
        fault(RECVX_EVENT_FAULT_TRUNCATED);
        assert(!eff[0].mode1 && !eff[0].type);
    }
    puts("PASS: mapped effect mode/type writes, all 512 work slots, 256 authored IDs/values, retained work fields, and bounded event dispatch");
    return 0;
}
