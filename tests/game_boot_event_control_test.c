#include <assert.h>
#include <setjmp.h>
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
static unsigned char section[128];
static jmp_buf fault_jump;
static unsigned int scenario_calls, checks;

unsigned int (*bhScenarioJmpT[256])(void) = {
    [0] = bhEnd, [5] = bhSet, [248] = bhSleep,
    [249] = bhSleeping, [254] = bhEvtNext, [255] = bhEvtEnd
};

void recvxGameBootEventCheck(void)
{
    ++checks;
    if (recvxEventCoreGetFault()->code != RECVX_EVENT_FAULT_NONE)
        longjmp(fault_jump, 1);
}

void bhScenarioCheck(unsigned char *offset)
{
    ++scenario_calls;
    if (recvxEventCoreRunScenario((uint32_t)(uintptr_t)offset))
        recvxGameBootEventCheck();
}

static void u32(unsigned char *p, unsigned int value)
{
    p[0] = value; p[1] = value >> 8;
    p[2] = value >> 16; p[3] = value >> 24;
}

static void setup(const unsigned char *task, size_t size)
{
    /* SCD1 sets room flag 0 on every call; SCD0 is never executed here. */
    static const unsigned char scenario1[] = {5, 4, 31, 0, 0, 0, 0, 0};
    memset(&system_work, 0, sizeof(system_work));
    memset(&room_work, 0, sizeof(room_work));
    memset(section, 0, sizeof(section));
    u32(section, 12); u32(section + 4, 16); u32(section + 8, 24);
    memcpy(section + 16, scenario1, sizeof(scenario1));
    assert(size <= sizeof(section) - 24);
    memcpy(section + 24, task, size);
    recvxEventCoreReset();
    rom->evtp = (EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section, 24 + size));
    bhScd2Ptr = (unsigned short *)(uintptr_t)16;
    assert(!Event_exec(0, 0));
    sys->sp_flg = 0x10;
    scenario_calls = checks = 0;
}

int main(void)
{
    static const unsigned char task[] = {
        /* XOR flag 0 proves that SCD1 ran before the task. */
        5, 4, 31, 0, 0, 2, 254,
        5, 4, 30, 0, 0, 0, 255, 0
    };
    static const unsigned char sleep_task[] = {
        248, 249, 3, 0, 5, 4, 30, 0, 0, 0, 255, 0
    };
    static const unsigned char unsupported[] = {123};
    static const unsigned char end[] = {255, 0};
    const RecvxEventCoreFault *fault;

    setup(task, sizeof(task));
    bhControlEvent();
    assert(!sys->rm_flg && Event_T_timer == 1 && bhEtask[0].status == 1);
    assert(recvxEventCoreTaskPc(0) == 31 && scenario_calls == 1 && checks == 2);
    bhControlEvent();
    assert(sys->rm_flg == 3 && Event_T_timer == 2 && !bhEtask[0].status);
    sys->rm_flg = 0;
    bhControlEvent();
    assert(sys->rm_flg == 1 && Event_T_timer == 2 && scenario_calls == 3);

    setup(task, sizeof(task));
    sys->sp_flg = 0;
    bhControlEvent();
    assert(sys->rm_flg == 1 && !Event_T_timer && recvxEventCoreTaskPc(0) == 24);
    sys->sp_flg = 0x10;
    bhControlEvent();
    assert(!sys->rm_flg && Event_T_timer == 1);

    setup(sleep_task, sizeof(sleep_task));
    bhControlEvent();
    assert(bhEtask[0].cnt[0] == 2 && bhEtask[0].loop == 0);
    bhControlEvent();
    assert(bhEtask[0].cnt[0] == 1 && recvxEventCoreTaskPc(0) == 25);
    bhControlEvent();
    assert(!bhEtask[0].cnt[0] && bhEtask[0].loop == -1);
    bhControlEvent();
    assert(sys->rm_flg == 3 && !bhEtask[0].status && Event_T_timer == 4);

    setup(end, sizeof(end));
    assert(!Event_exec(5, 0));
    assert(!Event_exec(15, 0));
    bhControlEvent();
    assert(!bhEtask[0].status && !bhEtask[5].status && !bhEtask[15].status);
    assert(Event_T_timer == 1); /* Timer counts ticks, rather than tasks. */

    setup(end, sizeof(end));
    section[16] = 123;
    if (!setjmp(fault_jump)) { bhControlEvent(); assert(0); }
    fault = recvxEventCoreGetFault();
    assert(fault->code == RECVX_EVENT_FAULT_UNSUPPORTED && fault->section_pc == 16);
    assert(!Event_T_timer && bhEtask[0].status && recvxEventCoreTaskPc(0) == 24);

    setup(unsupported, sizeof(unsupported));
    if (!setjmp(fault_jump)) { bhControlEvent(); assert(0); }
    fault = recvxEventCoreGetFault();
    assert(fault->code == RECVX_EVENT_FAULT_UNSUPPORTED && fault->opcode == 123);
    assert(fault->task == 0 && fault->section_pc == 24 && Event_T_timer == 1);
    assert(sys->rm_flg == 1 && bhEtask[0].status);

    setup(end, sizeof(end));
    bhScd2Ptr = (unsigned short *)(uintptr_t)17;
    if (!setjmp(fault_jump)) { bhControlEvent(); assert(0); }
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_SECTION);
    assert(!Event_T_timer && bhEtask[0].status);

    puts("PASS: original SCD1-before-task control, repeated scenario checks, pause/resume, sleep/yield, all task levels, and immediate scenario/task fault propagation");
    return 0;
}
