#include <stdio.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_event_runtime.h"
#include "psp/engine_event_test.h"
#include "psp/event_data.h"
#include "psp/room.h"
#include "psp/sha256.h"

static const uint32_t event_38_expected_flags[16] = {
    0x7b73cdf4, 0x4b7e06ff, 0x7b4f7f3f, 0xf1e74000,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char event_38_expected_sha256[] =
    "e6e8d61eba16c34eca97697e426a9719b329ff2cf5cbe8d8d06de8d5242f56b8";

static int expect(RecvxEngineEventTestReport *report, const char *stage,
                  int condition, const char *detail)
{
    if (!condition) {
        snprintf(report->stage, sizeof(report->stage), "%s", stage);
        snprintf(report->detail, sizeof(report->detail), "%s", detail);
        report->fault = *recvxEventCoreGetFault();
        return 0;
    }
    ++report->assertions_passed;
    return 1;
}

static void write_u32(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
}

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_flag_set(unsigned char *bytes, unsigned int operation)
{
    bytes[0] = 0x05;
    bytes[1] = 0x02;
    bytes[2] = 0x00;
    bytes[3] = 0x00;
    bytes[4] = 0x00;
    bytes[5] = (unsigned char)operation;
}

static void build_single_event(unsigned char *section,
                               const unsigned char *body, size_t body_size)
{
    memset(section, 0, 12 + body_size);
    write_u32(section, 12);
    write_u32(section + 4, 12);
    write_u32(section + 8, 12);
    memcpy(section + 12, body, body_size);
}

static void set_fixture_section(const unsigned char *section)
{
    rom->evtp = (EVT_WORK *)section;
}

static int bind_fixture(const unsigned char *section, size_t size)
{
    set_fixture_section(section);
    return recvxEventCoreBindSection(section, size);
}

static int check_sha256_vectors(RecvxEngineEventTestReport *report)
{
    static const uint8_t empty_expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    static const uint8_t abc_expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    uint8_t digest[32];

    recvxSha256(NULL, 0, digest);
    if (!expect(report, "SHA-256 empty input",
                memcmp(digest, empty_expected, sizeof(digest)) == 0,
                "empty digest mismatch"))
        return -1;
    recvxSha256("abc", 3, digest);
    if (!expect(report, "SHA-256 abc vector",
                memcmp(digest, abc_expected, sizeof(digest)) == 0,
                "abc digest mismatch"))
        return -1;
    return 0;
}

static int test_disabled_scheduler(RecvxEngineEventTestReport *report)
{
    unsigned char body[8];
    unsigned char section[20];

    write_flag_set(body, 0);
    body[6] = 0xff;
    body[7] = 0x00;
    recvxEngineEventRuntimeReset();
    build_single_event(section, body, sizeof(body));
    if (!expect(report, "disabled scheduler bind",
                bind_fixture(section, sizeof(section)) == 0,
                "valid disabled-scheduler fixture was rejected"))
        goto fail;
    if (!expect(report, "disabled scheduler start",
                Event_exec(7, 0) == 0, "Event_exec rejected event 0"))
        goto fail;
    if (!expect(report, "disabled scheduler gate",
                bhEventScheduler2() == 0 && bhEtask[7].status == 1 &&
                sys->ky_flg[0] == 0 && Event_T_timer == 0 &&
                recvxEventCoreTaskBodyPc(7) == 0,
                "disabled scheduler changed the task, PC, flags or timer"))
        goto fail;
    sys->sp_flg |= 0x10;
    if (!expect(report, "enabled scheduler resume",
                bhEventScheduler2() == 0 && bhEtask[7].status == 0 &&
                sys->ky_flg[0] == 0x80000000 && Event_T_timer == 1,
                "task did not resume after scheduling was enabled"))
        goto fail;
    recvxEngineEventRuntimeReset();
    return 0;

fail:
    recvxEngineEventRuntimeReset();
    return -1;
}

static int test_scenario_runner(RecvxEngineEventTestReport *report)
{
    unsigned char section[25];
    unsigned char *body = section + 12;

    memset(section, 0, sizeof(section));
    write_u32(section, 12);
    write_u32(section + 4, 20);
    write_u32(section + 8, 24);
    write_flag_set(body, 0);
    body[6] = 0;
    body[12] = 0;
    body[13] = 0;
    recvxEngineEventRuntimeReset();
    if (!expect(report, "scenario section bind",
                bind_fixture(section, sizeof(section)) == 0,
                "valid SCD0/SCD1/event table was rejected"))
        goto fail;
    bhCetask = &bhEtask[0];
    if (!expect(report, "original scenario runner",
                recvxEventCoreRunScenario(12) == 0,
                "bounded SCD0 command stream faulted"))
        goto fail;
    if (!expect(report, "scenario flag write and terminus",
                sys->ky_flg[0] == 0x80000000u && bhScePtr == body + 6 &&
                bhIfelFlg == 0,
                "scenario did not execute original bhSet/bhEnd state"))
        goto fail;
    if (!expect(report, "scenario does not advance event timer",
                Event_T_timer == 0 &&
                recvxEventCoreFlagWriteCount() == 1 &&
                recvxEventCoreInstructionCount() == 2,
                "scenario setup changed task timer or command counts"))
        goto fail;
    if (!expect(report, "SCD1 scenario span",
                recvxEventCoreRunScenario(20) == 0 &&
                bhScePtr == body + 8 && Event_T_timer == 0,
                "bounded SCD1 terminator did not preserve timer"))
        goto fail;

    memset(section, 0, sizeof(section));
    write_u32(section, 12);
    write_u32(section + 4, 15);
    write_u32(section + 8, 16);
    body = section + 12;
    body[0] = 5;
    body[1] = 2;
    recvxEngineEventRuntimeReset();
    if (!expect(report, "truncated scenario section bind",
                bind_fixture(section, 17) == 0,
                "truncated SCD0 fixture was rejected before execution"))
        goto fail;
    if (!expect(report, "truncated scenario operand fault",
                recvxEventCoreRunScenario(12) != 0 &&
                recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_TRUNCATED &&
                recvxEventCoreGetFault()->section_pc == 12 &&
                recvxEventCoreGetFault()->opcode == 5,
                "truncated scenario operand was not reported at its opcode"))
        goto fail;

    memset(section, 0, sizeof(section));
    write_u32(section, 12);
    write_u32(section + 4, 13);
    write_u32(section + 8, 14);
    section[12] = 42;
    recvxEngineEventRuntimeReset();
    if (!expect(report, "unsupported scenario section bind",
                bind_fixture(section, 15) == 0,
                "valid unsupported-opcode fixture was rejected at bind"))
        goto fail;
    if (!expect(report, "unsupported scenario opcode fault",
                recvxEventCoreRunScenario(12) != 0 &&
                recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_UNSUPPORTED &&
                recvxEventCoreGetFault()->section_pc == 12 &&
                recvxEventCoreGetFault()->opcode == 42,
                "unsupported scenario opcode was not reported precisely"))
        goto fail;
    recvxEngineEventRuntimeReset();
    return 0;

fail:
    recvxEngineEventRuntimeReset();
    return -1;
}

static int test_unaligned_event_bytes(RecvxEngineEventTestReport *report)
{
    unsigned char storage[32];
    unsigned char body[12];
    unsigned char *section = storage +
        ((4u - ((uintptr_t)storage & 3u)) & 3u);

    body[0] = 0xf8;
    body[1] = 0xf9;
    body[2] = 2;
    body[3] = 0;
    write_flag_set(body + 4, 0);
    body[10] = 0xff;
    body[11] = 0;
    recvxEngineEventRuntimeReset();
    memset(section, 0, 25);
    write_u32(section, 12);
    write_u32(section + 4, 12);
    write_u32(section + 8, 13);
    memcpy(section + 13, body, sizeof(body));
    if (!expect(report, "unaligned event bytes",
                ((uintptr_t)section & 3u) == 0 &&
                ((uintptr_t)(section + 13) & 1u) != 0,
                "alignment fixture did not produce an odd event body"))
        goto fail;
    if (!expect(report, "unaligned event bind",
                bind_fixture(section, 25) == 0,
                "unaligned event fixture was rejected"))
        goto fail;
    sys->sp_flg = 0x10;
    if (!expect(report, "unaligned sleep start",
                Event_exec(0, 0) == 0 && bhEventScheduler2() == 0 &&
                bhEtask[0].status == 1 &&
                recvxEventCoreTaskBodyPc(0) == 1 &&
                Event_T_timer == 1,
                "unaligned sleep did not preserve its continuation"))
        goto fail;
    {
        int scheduler_result = bhEventScheduler2();
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "result %d status %u PC %u flag %08x timer %u fault %u",
                 scheduler_result, bhEtask[0].status,
                 (unsigned int)recvxEventCoreTaskBodyPc(0),
                 (unsigned int)sys->ky_flg[0], (unsigned int)Event_T_timer,
                 (unsigned int)recvxEventCoreGetFault()->code);
        if (!expect(report, "unaligned sleep resume",
                    scheduler_result == 0 && bhEtask[0].status == 1 &&
                    recvxEventCoreTaskBodyPc(0) == 4 &&
                    bhEtask[0].cnt[0] == 0 && Event_T_timer == 2,
                    detail))
            goto fail;
    }
    {
        int scheduler_result = bhEventScheduler2();
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "result %d status %u PC %u flag %08x timer %u fault %u",
                 scheduler_result, bhEtask[0].status,
                 (unsigned int)recvxEventCoreTaskBodyPc(0),
                 (unsigned int)sys->ky_flg[0], (unsigned int)Event_T_timer,
                 (unsigned int)recvxEventCoreGetFault()->code);
        if (!expect(report, "unaligned flag resume",
                    scheduler_result == 0 && bhEtask[0].status == 0 &&
                    recvxEventCoreTaskBodyPc(0) == sizeof(body) &&
                    sys->ky_flg[0] == 0x80000000 && Event_T_timer == 3,
                    detail))
            goto fail;
    }
    recvxEngineEventRuntimeReset();
    return 0;

fail:
    recvxEngineEventRuntimeReset();
    return -1;
}

static int test_sleep_and_task_order(RecvxEngineEventTestReport *report)
{
    unsigned char section[40];
    unsigned char *first = section + 16;
    unsigned char *second = section + 28;
    unsigned int pass;

    memset(section, 0, sizeof(section));
    write_u32(section, 16);
    write_u32(section + 4, 16);
    write_u32(section + 8, 16);
    write_u32(section + 12, 28);
    first[0] = 0xf8;
    first[1] = 0xf9;
    first[2] = 3;
    first[3] = 0;
    write_flag_set(first + 4, 0);
    first[10] = 0xff;
    first[11] = 0;
    second[0] = 0xf8;
    second[1] = 0xf9;
    second[2] = 3;
    second[3] = 0;
    write_flag_set(second + 4, 1);
    second[10] = 0xff;
    second[11] = 0;

    recvxEngineEventRuntimeReset();
    if (!expect(report, "sleep fixture bind",
                bind_fixture(section, sizeof(section)) == 0,
                "valid two-event fixture was rejected"))
        goto fail;
    sys->sp_flg = 0x10;
    if (!expect(report, "sleep fixture start",
                Event_exec(0, 0) == 0 && Event_exec(1, 1) == 0,
                "could not start both scheduler tasks"))
        goto fail;
    for (pass = 0; pass < 3; ++pass) {
        char detail[160];
        int scheduler_result = bhEventScheduler2();
        snprintf(detail, sizeof(detail),
                 "pass %u result %d status %u/%u PC %u/%u count %d/%d timer %u",
                 pass + 1, scheduler_result, bhEtask[0].status,
                 bhEtask[1].status,
                 (unsigned int)recvxEventCoreTaskBodyPc(0),
                 (unsigned int)recvxEventCoreTaskBodyPc(1),
                 bhEtask[0].cnt[0], bhEtask[1].cnt[0],
                 (unsigned int)Event_T_timer);
        if (!expect(report, "persistent sleep scheduler pass",
                    scheduler_result == 0 && bhEtask[0].status == 1 &&
                    bhEtask[1].status == 1 &&
                    recvxEventCoreTaskBodyPc(0) == (pass == 2 ? 4u : 1u) &&
                    recvxEventCoreTaskBodyPc(1) == (pass == 2 ? 4u : 1u) &&
                    bhEtask[0].cnt[0] == (int)(2 - pass) &&
                    bhEtask[1].cnt[0] == (int)(2 - pass) &&
                    Event_T_timer == pass + 1, detail))
            goto fail;
    }
    if (!expect(report, "sleep completion PC",
                recvxEventCoreTaskBodyPc(0) == 4 &&
                recvxEventCoreTaskBodyPc(1) == 4 &&
                bhEtask[0].cnt[0] == 0 && bhEtask[1].cnt[0] == 0,
                "sleep handlers did not resume after the embedded opcode"))
        goto fail;
    {
        char detail[160];
        int scheduler_result = bhEventScheduler2();
        snprintf(detail, sizeof(detail),
                 "result %d status %u/%u flag %08x PC %u/%u timer %u",
                 scheduler_result, bhEtask[0].status, bhEtask[1].status,
                 sys->ky_flg[0],
                 (unsigned int)recvxEventCoreTaskBodyPc(0),
                 (unsigned int)recvxEventCoreTaskBodyPc(1),
                 (unsigned int)Event_T_timer);
        if (!expect(report, "ascending task order",
                    scheduler_result == 0 && bhEtask[0].status == 0 &&
                    bhEtask[1].status == 0 && sys->ky_flg[0] == 0 &&
                    recvxEventCoreTaskBodyPc(0) == 12 &&
                    recvxEventCoreTaskBodyPc(1) == 12 && Event_T_timer == 4,
                    detail))
            goto fail;
    }
    recvxEngineEventRuntimeReset();
    return 0;

fail:
    recvxEngineEventRuntimeReset();
    return -1;
}

static int test_yield(RecvxEngineEventTestReport *report)
{
    unsigned char body[9];
    unsigned char section[21];

    body[0] = 0xfe;
    write_flag_set(body + 1, 0);
    body[7] = 0xff;
    body[8] = 0;
    recvxEngineEventRuntimeReset();
    build_single_event(section, body, sizeof(body));
    if (!expect(report, "yield fixture bind",
                bind_fixture(section, sizeof(section)) == 0,
                "valid yield fixture was rejected"))
        goto fail;
    sys->sp_flg = 0x10;
    if (!expect(report, "yield fixture start",
                Event_exec(0, 0) == 0, "Event_exec rejected yield event"))
        goto fail;
    if (!expect(report, "event yield",
                bhEventScheduler2() == 0 && bhEtask[0].status == 1 &&
                recvxEventCoreTaskBodyPc(0) == 1 && sys->ky_flg[0] == 0 &&
                Event_T_timer == 1,
                "yield did not preserve its continuation PC"))
        goto fail;
    if (!expect(report, "event yield resume",
                bhEventScheduler2() == 0 && bhEtask[0].status == 0 &&
                recvxEventCoreTaskBodyPc(0) == sizeof(body) &&
                sys->ky_flg[0] == 0x80000000 && Event_T_timer == 2,
                "yielded task did not resume and terminate"))
        goto fail;
    recvxEngineEventRuntimeReset();
    return 0;

fail:
    recvxEngineEventRuntimeReset();
    return -1;
}

static int test_faults(RecvxEngineEventTestReport *report)
{
    unsigned char invalid_section[16] = {0};
    unsigned char truncated_body[5] = {5, 0, 2, 0, 0};
    unsigned char unsupported_body[9] = {3};
    unsigned char bad_operand_body[8] = {0, 2, 0, 2, 0, 0xff, 0xff, 0};
    unsigned char valid_body[2] = {0xff, 0};
    unsigned char section[32];

    write_u32(invalid_section, 12);
    write_u32(invalid_section + 4, 12);
    write_u32(invalid_section + 8, 16);
    recvxEngineEventRuntimeReset();
    if (!expect(report, "out-of-section offset",
                recvxEventCoreBindSection(invalid_section,
                                          sizeof(invalid_section)) != 0 &&
                recvxEventCoreGetFault()->code ==
                    RECVX_EVENT_FAULT_BAD_SECTION,
                "out-of-section event offset was accepted"))
        goto fail;

    recvxEngineEventRuntimeReset();
    build_single_event(section, truncated_body, sizeof(truncated_body));
    if (!expect(report, "truncated operand bind",
                bind_fixture(section, 12 + sizeof(truncated_body)) == 0,
                "truncated fixture table was rejected before dispatch"))
        goto fail;
    sys->sp_flg = 0x10;
    if (!expect(report, "truncated operand start",
                Event_exec(0, 0) == 0, "Event_exec rejected truncated fixture"))
        goto fail;
    if (!expect(report, "truncated operand fault",
                bhEventScheduler2() != 0 &&
                recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_TRUNCATED &&
                recvxEventCoreGetFault()->opcode == 5 &&
                recvxEventCoreGetFault()->section_pc == 12 &&
                sys->ky_flg[0] == 0,
                "truncated flag write did not stop at its PC"))
        goto fail;

    recvxEngineEventRuntimeReset();
    unsupported_body[0] = 3;
    write_flag_set(unsupported_body + 1, 0);
    unsupported_body[7] = 0xff;
    unsupported_body[8] = 0;
    build_single_event(section, unsupported_body, sizeof(unsupported_body));
    if (!expect(report, "unsupported opcode bind",
                bind_fixture(section, 12 + sizeof(unsupported_body)) == 0,
                "valid unsupported-opcode fixture was rejected"))
        goto fail;
    sys->sp_flg = 0x10;
    if (!expect(report, "unsupported opcode start",
                Event_exec(0, 0) == 0, "Event_exec rejected unsupported fixture"))
        goto fail;
    if (!expect(report, "unsupported opcode fault",
                bhEventScheduler2() != 0 &&
                recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_UNSUPPORTED &&
                recvxEventCoreGetFault()->opcode == 3 &&
                recvxEventCoreTaskBodyPc(0) == 0 &&
                bhEtask[0].status == 1 && sys->ky_flg[0] == 0,
                "unsupported opcode was skipped or later flags were written"))
        goto fail;

    recvxEngineEventRuntimeReset();
    build_single_event(section, valid_body, sizeof(valid_body));
    if (!expect(report, "bad event ID bind",
                bind_fixture(section, 14) == 0,
                "valid bad-event fixture was rejected"))
        goto fail;
    if (!expect(report, "bad event ID fault",
                Event_exec(0, 1) != 0 &&
                recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_EVENT &&
                bhEtask[0].status == 0,
                "out-of-table event ID modified a task"))
        goto fail;

    recvxEngineEventRuntimeReset();
    build_single_event(section, valid_body, sizeof(valid_body));
    if (!expect(report, "slot 15 fallback bind",
                bind_fixture(section, 14) == 0,
                "valid slot-fallback fixture was rejected"))
        goto fail;
    for (unsigned int task = 0; task < 15; ++task)
        bhEtask[task].status = 1;
    if (!expect(report, "original slot 15 fallback",
                Event_exec(16, 0) == 0 && bhEtask[15].status == 1 &&
                recvxEventCoreTaskStart(15) == 12,
                "Event_exec did not preserve its slot 15 fallback"))
        goto fail;

    recvxEngineEventRuntimeReset();
    build_single_event(section, valid_body, sizeof(valid_body));
    if (!expect(report, "bad work index bind",
                bind_fixture(section, 14) == 0,
                "valid bad-work fixture was rejected"))
        goto fail;
    if (!expect(report, "bad work index fault",
                Event_init((BH_SCEWORK *)plp, 0) != 0 &&
                recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_TASK,
                "Event_init accepted work outside bhEtask"))
        goto fail;

    recvxEngineEventRuntimeReset();
    build_single_event(section, valid_body, sizeof(valid_body));
    if (!expect(report, "active task bind",
                bind_fixture(section, 14) == 0,
                "valid active-task fixture was rejected"))
        goto fail;
    if (!expect(report, "active task start",
                Event_exec(0, 0) == 0, "Event_exec rejected active-task fixture"))
        goto fail;
    if (!expect(report, "active task unload guard",
                recvxEventCoreUnbindSection() != 0 &&
                recvxEventCoreGetFault()->code ==
                    RECVX_EVENT_FAULT_ACTIVE_TASKS && bhEtask[0].status == 1,
                "event section could unload while its task was active"))
        goto fail;

    recvxEngineEventRuntimeReset();
    write_flag_set(bad_operand_body, 0);
    bad_operand_body[2] = 0x00;
    bad_operand_body[3] = 0x02;
    build_single_event(section, bad_operand_body, sizeof(bad_operand_body));
    if (!expect(report, "bad flag index bind",
                bind_fixture(section, 12 + sizeof(bad_operand_body)) == 0,
                "valid bad-flag fixture was rejected"))
        goto fail;
    sys->sp_flg = 0x10;
    if (!expect(report, "bad flag index start",
                Event_exec(0, 0) == 0, "Event_exec rejected bad-flag fixture"))
        goto fail;
    if (!expect(report, "bad flag index fault",
                bhEventScheduler2() != 0 &&
                recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND &&
                recvxEventCoreTaskBodyPc(0) == 0 && sys->ky_flg[0] == 0,
                "invalid flag bank index was not rejected before the write"))
        goto fail;

    recvxEngineEventRuntimeReset();
    return 0;

fail:
    recvxEngineEventRuntimeReset();
    return -1;
}

static void digest_hex(const uint8_t digest[32], char output[65])
{
    static const char digits[] = "0123456789abcdef";
    unsigned int i;

    for (i = 0; i < 32; ++i) {
        output[i * 2] = digits[digest[i] >> 4];
        output[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
    output[64] = '\0';
}

static int test_original_event(const void *room_bytes, size_t room_size,
                               RecvxEngineEventTestReport *report)
{
    RecvxEventData events = {0};
    const unsigned char *body;
    uint8_t digest[32];
    size_t body_size;
    uint32_t event_offset, event_end;
    unsigned int i;
    int result = -1;

    if (room_bytes == NULL)
        return 0;
    snprintf(report->stage, sizeof(report->stage), "RM_0000 event bytes");
    if (room_size < 36) {
        snprintf(report->detail, sizeof(report->detail), "expanded room is truncated");
        goto done;
    }
    event_offset = read_u32((const unsigned char *)room_bytes + 28);
    event_end = read_u32((const unsigned char *)room_bytes + 32);
    if (!expect(report, "event section location",
                event_offset == 0x18bec0 && event_end - event_offset == 8248,
                "RM_0000 event section location or size changed"))
        goto done;
    if (recvxEventDataLoad(room_bytes, room_size, &events) != 0) {
        snprintf(report->detail, sizeof(report->detail),
                 "event section parser rejected RM_0000");
        goto done;
    }
    if (!expect(report, "event table shape",
                events.size == 8248 && events.script_count == 41 &&
                read_u32(events.bytes) == 164,
                "RM_0000 event table size or count changed"))
        goto done;
    body = recvxEventDataScript(&events, 40, &body_size);
    if (!expect(report, "event 38 span",
                body != NULL && body_size == 352 &&
                (size_t)(body - events.bytes) == 0x1ed8,
                "event 38 table entry or span changed"))
        goto done;
    recvxSha256(body, body_size, digest);
    digest_hex(digest, report->event_span_sha256);
    if (!expect(report, "event 38 source hash",
                strcmp(report->event_span_sha256,
                       event_38_expected_sha256) == 0,
                "event 38 bytes do not match the source-disc fixture"))
        goto done;

    recvxEngineEventRuntimeReset();
    set_fixture_section(events.bytes);
    if (!expect(report, "event 38 bounded section",
                recvxEventCoreBindSection(events.bytes, events.size) == 0,
                "event core rejected validated event section"))
        goto done;
    sys->sp_flg = 0x10;
    if (!expect(report, "Event_exec event 38",
                Event_exec(7, 38) == 0 && bhEtask[7].status == 1,
                "original Event_exec did not start task 7/event 38"))
        goto done;
    if (!expect(report, "original scheduler event 38",
                bhEventScheduler2() == 0,
                "original event scheduler faulted during event 38"))
        goto done;
    for (i = 0; i < 16; ++i)
        report->event_flags[i] = sys->ky_flg[i];
    for (i = 0; i < 16; ++i) {
        if (!expect(report, "event 38 full key-flag result",
                    sys->ky_flg[i] == event_38_expected_flags[i],
                    "one of the 16 key-flag words differs from the oracle"))
            goto done;
    }
    if (!expect(report, "event 38 task termination and PC",
                bhEtask[7].status == 0 &&
                recvxEventCoreTaskBodyPc(7) == 0x10a,
                "task 7 did not terminate at body PC 0x10a"))
        goto done;
    if (!expect(report, "event 38 timer",
                Event_T_timer == 1 && recvxEventCoreInstructionCount() == 45,
                "event timer or instruction count differs from the oracle"))
        goto done;
    report->event_flag_writes = recvxEventCoreFlagWriteCount();
    if (!expect(report, "event 38 write count",
                report->event_flag_writes == 44,
                "event 38 did not execute all 44 bhSet handlers"))
        goto done;
    if (!expect(report, "idle event timer",
                bhEventScheduler2() == 0 && Event_T_timer == 1,
                "scheduler advanced the timer without active work"))
        goto done;

    report->original_event_ran = 1;
    report->event_instructions = recvxEventCoreInstructionCount();
    report->event_timer = Event_T_timer;
    report->event_pc_section = recvxEventCoreTaskPc(7);
    report->event_pc_body = recvxEventCoreTaskBodyPc(7);
    result = 0;

done:
    if (result != 0) {
        const RecvxEventCoreFault *fault = recvxEventCoreGetFault();
        if (fault->code != RECVX_EVENT_FAULT_NONE)
            report->fault = *fault;
    }
    recvxEngineEventRuntimeReset();
    recvxEventDataFree(&events);
    return result;
}

int recvxEngineEventTestsRun(const void *expanded_room, size_t room_size,
                             RecvxEngineEventTestReport *report)
{
    if (report == NULL)
        return -1;
    memset(report, 0, sizeof(*report));
    if (check_sha256_vectors(report) != 0 ||
        test_disabled_scheduler(report) != 0 ||
        test_unaligned_event_bytes(report) != 0 ||
        test_sleep_and_task_order(report) != 0 ||
        test_yield(report) != 0 || test_faults(report) != 0 ||
        test_scenario_runner(report) != 0)
        return -1;
    if (test_original_event(expanded_room, room_size, report) != 0)
        return -1;
    report->passed = 1;
    snprintf(report->stage, sizeof(report->stage), "complete");
    snprintf(report->detail, sizeof(report->detail), "all executed checks passed");
    return 0;
}
