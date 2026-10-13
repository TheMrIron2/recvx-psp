#include <stdio.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_event_core.h"

SYS_WORK *sys;
ROM_WORK *rom;
BH_PWORK *plp;
BH_PWORK ene[128];

static SYS_WORK system_work;
static ROM_WORK room_work;
static BH_PWORK player_work;
static O_WRK item_work[256];
static ATR_WORK wall_attributes[1];
static ATR_WORK floor_attributes[1];
static unsigned char section[64];
static unsigned int common_command_size;
static unsigned int failures;

static void write_u32(unsigned char *bytes, unsigned int value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
}

static void expect(const char *name, int condition)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static void setup_scenario(const unsigned char *body, unsigned int body_size)
{
    unsigned int end = 12 + body_size;

    memset(section, 0, sizeof(section));
    write_u32(section, 12);
    write_u32(section + 4, end);
    write_u32(section + 8, end + 4);
    memcpy(section + 12, body, body_size);
    recvxEventCoreReset();
    room_work.evtp = (EVT_WORK *)section;
    if (recvxEventCoreBindSection(section, sizeof(section)) != 0)
        ++failures;
}

#define PASS_HANDLER(name) unsigned int name(void) { return 0; }

PASS_HANDLER(bhElseCk)
PASS_HANDLER(bhEndif)
PASS_HANDLER(bhSv)
PASS_HANDLER(bhEvtOn)
PASS_HANDLER(bhItmCk)
PASS_HANDLER(bhItmSetCk)
PASS_HANDLER(bhEtcAtariSet2)
PASS_HANDLER(bhFrameCheck)
PASS_HANDLER(bhLoadWork)
PASS_HANDLER(bhWhile)
PASS_HANDLER(bhEwhile)

unsigned int bhIfelCk(void)
{
    unsigned int displacement = bhScePtr[1];

    bhScePtr += 2;
    G_Sp = (unsigned int *)(void *)(bhScePtr + displacement);
    ++G_Sp;
    ++bhIfelFlg;
    return 1;
}

unsigned int bhCk(void)
{
    bhScePtr += 6;
    return 0;
}

unsigned int bhArmsItemCheck(void)
{
    int weapon;

    ++bhScePtr;
    weapon = *bhScePtr++;
    return plp->wpnr_no == weapon;
}

unsigned int bhPlItemCheck(void)
{
    unsigned int item;
    unsigned int count;
    unsigned int player_base = (unsigned int)sys->ply_id * 16u;

    ++bhScePtr;
    item = *bhScePtr++;
    for (count = player_base + 1; count < player_base + 12; ++count) {
        if (item == (unsigned char)(sys->itm[count] >> 16))
            return 1;
    }
    return 0;
}

unsigned int bhItmAspdSet(void)
{
    unsigned int item;
    unsigned int speed;

    ++bhScePtr;
    item = *bhScePtr++;
    speed = *bhScePtr;
    bhScePtr += 2;
    sys->itwp[item].aspd = (int)speed;
    return 1;
}

unsigned int bhWalAtariSet(void)
{
    bhScePtr += 4;
    return 1;
}

unsigned int bhFlrAtariSet(void)
{
    bhScePtr += 4;
    return 1;
}

unsigned int bhVoiceOff(void)
{
    bhScePtr += 2;
    return 1;
}

unsigned int bhRoomCaseNo(void)
{
    bhScePtr += 2;
    return 1;
}

unsigned int bhRoomSoundCase(void)
{
    bhScePtr += 2;
    return 1;
}

unsigned int bhCommonCtr(void)
{
    bhScePtr += common_command_size;
    return 1;
}

static void test_attribute_commands(void)
{
    static const unsigned char commands[] = {
        10, 0, 0, 0,
        12, 0, 0, 0,
        0
    };
    const RecvxEventCoreFault *fault;

    room_work.wal_n = 1;
    room_work.walp = wall_attributes;
    room_work.flr_n = 1;
    room_work.flrp = floor_attributes;
    setup_scenario(commands, sizeof(commands));
    expect("valid wall/floor scenario", recvxEventCoreRunScenario(12) == 0);
    expect("valid attributes have no event fault",
           recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE);

    setup_scenario(commands, sizeof(commands));
    section[13] = 1;
    expect("bad wall index is rejected", recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("bad wall index diagnostic",
           fault->code == RECVX_EVENT_FAULT_BAD_OPERAND &&
           fault->section_pc == 12 && fault->opcode == 10);

    room_work.wal_n = 0;
    setup_scenario(commands, 3);
    expect("truncated wall command is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("truncated wall command diagnostic",
           fault->code == RECVX_EVENT_FAULT_TRUNCATED &&
           fault->section_pc == 12 && fault->opcode == 10);
    room_work.wal_n = 1;
}

static void test_common_motion_extent(void)
{
    static const unsigned char valid[] = {
        105, 24, 1, 1, 0, 0, 1, 16, 0xfc, 0x08, 0
    };
    const RecvxEventCoreFault *fault;

    common_command_size = 10;
    setup_scenario(valid, sizeof(valid));
    bhCetask = &bhEtask[0];
    bhCetask->work = &player_work;
    expect("original common motion command extent",
           recvxEventCoreRunScenario(12) == 0);
    expect("common motion command has no event fault",
           recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE);

    setup_scenario(valid, sizeof(valid) - 2);
    bhCetask = &bhEtask[0];
    bhCetask->work = &player_work;
    expect("truncated common motion command is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("truncated common motion diagnostic",
           fault->code == RECVX_EVENT_FAULT_TRUNCATED &&
           fault->section_pc == 12 && fault->opcode == 105);

    setup_scenario(valid, sizeof(valid));
    bhCetask = &bhEtask[0];
    bhCetask->work = &player_work;
    section[14] = 26;
    expect("unsupported original motion rate is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("unsupported motion rate diagnostic",
           fault->code == RECVX_EVENT_FAULT_BAD_OPERAND &&
           fault->section_pc == 12 && fault->opcode == 105);
}

static void test_unsupported_door_command(void)
{
    static const unsigned char door_call[] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const RecvxEventCoreFault *fault;

    setup_scenario(door_call, sizeof(door_call));
    expect("door command remains unsupported", recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("door command fault names source PC",
           fault->code == RECVX_EVENT_FAULT_UNSUPPORTED &&
           fault->section_pc == 12 && fault->opcode == 51);
}

static void test_fixed_extent_commands(void)
{
    static const unsigned char truncated_sv[] = {8, 0};
    static const unsigned char truncated_endif[] = {3};
    static const unsigned char valid_sv[] = {8, 1, 2, 0};
    static const unsigned char unsupported_sv[] = {8, 99, 2, 0};
    const RecvxEventCoreFault *fault;

    setup_scenario(truncated_sv, 1);
    expect("truncated original state command is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("state command truncation has exact opcode",
           fault->code == RECVX_EVENT_FAULT_TRUNCATED &&
           fault->section_pc == 12 && fault->opcode == 8);

    setup_scenario(truncated_endif, sizeof(truncated_endif));
    expect("truncated conditional terminator is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("conditional truncation has exact opcode",
           fault->code == RECVX_EVENT_FAULT_TRUNCATED &&
           fault->section_pc == 12 && fault->opcode == 3);

    setup_scenario(valid_sv, sizeof(valid_sv));
    expect("supported original state command is admitted",
           recvxEventCoreRunScenario(12) == 0);
    expect("supported state command has no validation fault",
           recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE);

    setup_scenario(unsupported_sv, sizeof(unsupported_sv));
    expect("unsupported original state selector is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("unsupported selector diagnostic",
           fault->code == RECVX_EVENT_FAULT_BAD_OPERAND &&
           fault->section_pc == 12 && fault->opcode == 8);
}

static void test_original_weapon_check(void)
{
    static const unsigned char matching[] = {38, 1, 0};
    static const unsigned char mismatch[] = {38, 1, 0};
    static const unsigned char truncated[] = {38};
    const RecvxEventCoreFault *fault;
    RecvxEventCoreDispatch dispatch;

    plp->wpnr_no = 1;
    setup_scenario(matching, sizeof(matching));
    expect("weapon check dispatches original handler",
           recvxEventCoreRunScenario(12) == 0);
    expect("weapon check match reaches original scenario terminator",
           recvxEventCoreInstructionCount() == 2 &&
           recvxEventCoreGetLastDispatch(&dispatch) == 0 &&
           dispatch.opcode == 0);
    expect("weapon check match has no event fault",
           recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE);

    plp->wpnr_no = 0;
    setup_scenario(mismatch, sizeof(mismatch));
    expect("weapon check mismatch ends the original scenario",
           recvxEventCoreRunScenario(12) == 0);
    expect("weapon check mismatch stops after its operand",
           recvxEventCoreInstructionCount() == 1 && bhScePtr == section + 14);

    setup_scenario(truncated, sizeof(truncated));
    expect("truncated weapon check is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("weapon check truncation diagnostic",
           fault->code == RECVX_EVENT_FAULT_TRUNCATED &&
           fault->section_pc == 12 && fault->opcode == 38);

    plp = NULL;
    setup_scenario(matching, sizeof(matching));
    expect("weapon check rejects missing player state",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("missing player state diagnostic",
           fault->code == RECVX_EVENT_FAULT_BAD_OPERAND &&
           fault->section_pc == 12 && fault->opcode == 38);
    plp = &player_work;
}

static void test_original_item_speed_set(void)
{
    static const unsigned char valid[] = {66, 0, 7, 0, 0};
    static const unsigned char truncated[] = {66, 0, 7};
    static const unsigned char invalid_index[] = {66, 255, 7, 0, 0};
    const RecvxEventCoreFault *fault;
    RecvxEventCoreDispatch dispatch;

    memset(item_work, 0, sizeof(item_work));
    sys->itwp = item_work;
    setup_scenario(valid, sizeof(valid));
    expect("item speed command dispatches original handler",
           recvxEventCoreRunScenario(12) == 0);
    expect("original item speed operand is applied",
           item_work[0].aspd == 7 &&
           recvxEventCoreInstructionCount() == 2 &&
           recvxEventCoreGetLastDispatch(&dispatch) == 0 &&
           dispatch.opcode == 0);

    setup_scenario(truncated, sizeof(truncated));
    expect("truncated item speed command is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("item speed truncation diagnostic",
           fault->code == RECVX_EVENT_FAULT_TRUNCATED &&
           fault->section_pc == 12 && fault->opcode == 66);

    setup_scenario(invalid_index, sizeof(invalid_index));
    expect("item speed rejects index beyond original item work",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("item speed index diagnostic",
           fault->code == RECVX_EVENT_FAULT_BAD_OPERAND &&
           fault->section_pc == 12 && fault->opcode == 66);
}

static void test_original_player_item_check(void)
{
    static const unsigned char check[] = {17, 0x1f, 0};
    static const unsigned char truncated[] = {17};
    static const unsigned char invalid_player[] = {17, 0x1f, 0};
    const RecvxEventCoreFault *fault;

    memset(sys->itm, 0, sizeof(sys->itm));
    sys->ply_id = 0;
    sys->itm[3] = 0x001f0000;
    setup_scenario(check, sizeof(check));
    expect("player inventory check dispatches original handler",
           recvxEventCoreRunScenario(12) == 0);
    expect("player inventory check finds original item ID",
           recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE &&
           recvxEventCoreInstructionCount() == 2);

    setup_scenario(truncated, sizeof(truncated));
    expect("truncated player inventory check is rejected",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("player inventory check truncation diagnostic",
           fault->code == RECVX_EVENT_FAULT_TRUNCATED &&
           fault->section_pc == 12 && fault->opcode == 17);

    sys->ply_id = 24;
    setup_scenario(invalid_player, sizeof(invalid_player));
    expect("player inventory check rejects invalid player bank",
           recvxEventCoreRunScenario(12) != 0);
    fault = recvxEventCoreGetFault();
    expect("player inventory bank diagnostic",
           fault->code == RECVX_EVENT_FAULT_BAD_OPERAND &&
           fault->section_pc == 12 && fault->opcode == 17);
    sys->ply_id = 0;
}

static void test_conditional_branch_at_section_end(void)
{
    static const unsigned char branch[] = {
        1, 6,
        4, 4, 0, 0, 0, 0,
        0, 0, 0, 0
    };

    setup_scenario(branch, sizeof(branch));
    expect("conditional branch to final in-range opcode is accepted",
           recvxEventCoreRunScenario(12) == 0);
    expect("conditional branch consumes IF, check and terminator",
           recvxEventCoreInstructionCount() == 3 &&
           recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE);
}

static void test_room_startup_operands(void)
{
    static const unsigned char ambient[] = {68, 2, 2, 2, 4, 0};
    static const unsigned char link[] = {50, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0};
    static const unsigned char truncated_link[] = {83, 0, 0};
    static const unsigned char model_part[] = {59, 0, 0, 1, 0, 0};
    const unsigned char *cases[] = {ambient, link, truncated_link, model_part};
    const unsigned int sizes[] = {sizeof(ambient), sizeof(link), sizeof(truncated_link), sizeof(model_part)};
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        const RecvxEventCoreFault *fault;
        /* These handlers must never run: bounds validation precedes dispatch. */
        bhScenarioJmpT[cases[i][0]] = bhSv;
        setup_scenario(cases[i], sizes[i]);
        expect("room startup rejects invalid operands", recvxEventCoreRunScenario(12) != 0);
        fault = recvxEventCoreGetFault();
        expect("room startup fault identifies operand and PC", fault->opcode == cases[i][0] &&
            fault->section_pc == 12 && fault->code == (i == 2 ?
                RECVX_EVENT_FAULT_TRUNCATED : RECVX_EVENT_FAULT_BAD_OPERAND));
        bhScenarioJmpT[cases[i][0]] = NULL;
    }
}

int main(void)
{
    sys = &system_work;
    rom = &room_work;
    plp = &player_work;
    memset(sys, 0, sizeof(*sys));
    memset(rom, 0, sizeof(*rom));
    memset(plp, 0, sizeof(*plp));
    test_attribute_commands();
    test_common_motion_extent();
    test_unsupported_door_command();
    test_fixed_extent_commands();
    test_original_weapon_check();
    test_original_item_speed_set();
    test_original_player_item_check();
    test_conditional_branch_at_section_end();
    test_room_startup_operands();
    if (failures != 0)
        return 1;
    puts("PASS: engine event opcode bounds and unsupported-command faults");
    return 0;
}
