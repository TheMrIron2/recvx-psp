#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_event_core.h"

#define RECVX_EVENT_TASK_COUNT 16u
#define RECVX_EVENT_TABLE_PREFIX_WORDS 2u
#define RECVX_EVENT_INSTRUCTION_LIMIT 4096u
#define RECVX_EVENT_NO_TASK UINT16_MAX

// Original event handlers share this state with the full engine table
#if defined(__PSP__) && !defined(RECVX_EVENT_CORE_HOST_TEST)
_Static_assert(sizeof(void *) == 4, "PSP pointer size changed");
_Static_assert(sizeof(EVT_WORK) == 12, "event table header layout changed");
_Static_assert(sizeof(BH_SCEWORK) == 0x134, "event task layout changed");
_Static_assert(offsetof(BH_SCEWORK, loop) == 5, "event loop offset changed");
_Static_assert(offsetof(BH_SCEWORK, data) == 0x0c, "event PC offset changed");
_Static_assert(offsetof(BH_SCEWORK, cnt) == 0x40, "event counter offset changed");
_Static_assert(offsetof(SYS_WORK, ky_flg) == 0x210, "key flag offset changed");
_Static_assert(sizeof(((SYS_WORK *)0)->ky_flg) == 16 * sizeof(uint32_t),
               "key flag bank size changed");
_Static_assert(CHAR_MIN < 0, "event task loop requires signed char");
#endif

unsigned char *bhScePtr;
unsigned short *bhScdPtr;
unsigned short *bhScd2Ptr;
unsigned char *bhEvdPtr;
unsigned int *G_Sp;
int bhIfelFlg;
BH_SCEWORK *bhCetask;
BH_SCEWORK bhEtask[RECVX_EVENT_TASK_COUNT];
unsigned int Event_T_timer;

static const unsigned char *event_section;
static size_t event_section_size;
static uint32_t event_table_size;
static uint32_t event_table_count;
static uint32_t task_start[RECVX_EVENT_TASK_COUNT];
static uint32_t task_end[RECVX_EVENT_TASK_COUNT];
static uint32_t task_event_id[RECVX_EVENT_TASK_COUNT];
static unsigned char task_bounds_valid[RECVX_EVENT_TASK_COUNT];
static unsigned int current_task = RECVX_EVENT_NO_TASK;
static uint32_t instruction_count;
static uint32_t instruction_count_this_call;
static uint32_t flag_write_count;
static RecvxEventCoreFault event_fault;

#ifdef RECVX_EVENT_CORE_TEST
#define RECVX_EVENT_CORE(opcode, handler) [opcode] = handler,
#define RECVX_EVENT_FULL(opcode, handler)
unsigned int (*bhScenarioJmpT[256])() = {
#include "event_dispatch_table.inc"
};
#undef RECVX_EVENT_FULL
#undef RECVX_EVENT_CORE
#else
extern unsigned int (*bhScenarioJmpT[256])();
#endif

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t read_u16(const unsigned char *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void set_fault(enum RecvxEventCoreFaultCode code, uint8_t opcode,
                      const unsigned char *pc)
{
    uintptr_t address, base;

    if (event_fault.code != RECVX_EVENT_FAULT_NONE)
        return;
    event_fault.code = code;
    event_fault.task = current_task < RECVX_EVENT_TASK_COUNT ?
                       (uint16_t)current_task : RECVX_EVENT_NO_TASK;
    event_fault.event_id = current_task < RECVX_EVENT_TASK_COUNT ?
                           task_event_id[current_task] : UINT32_MAX;
    event_fault.opcode = opcode;
    event_fault.section_pc = UINT32_MAX;
    if (event_section == NULL || pc == NULL)
        return;
    address = (uintptr_t)pc;
    base = (uintptr_t)event_section;
    if (address >= base && address - base <= event_section_size)
        event_fault.section_pc = (uint32_t)(address - base);
}

static int task_index(const BH_SCEWORK *task, unsigned int *index)
{
    uintptr_t base = (uintptr_t)&bhEtask[0];
    uintptr_t address = (uintptr_t)task;
    uintptr_t offset;

    if (task == NULL || address < base ||
        address - base >= sizeof(bhEtask))
        return -1;
    offset = address - base;
    if (offset % sizeof(BH_SCEWORK) != 0)
        return -1;
    *index = (unsigned int)(offset / sizeof(BH_SCEWORK));
    return 0;
}

static uint32_t section_offset(const unsigned char *pc)
{
    uintptr_t address, base;

    if (event_section == NULL || pc == NULL)
        return UINT32_MAX;
    address = (uintptr_t)pc;
    base = (uintptr_t)event_section;
    if (address < base || address - base > event_section_size)
        return UINT32_MAX;
    return (uint32_t)(address - base);
}

static int active_task_count(void)
{
    unsigned int i;
    int count = 0;

    for (i = 0; i < RECVX_EVENT_TASK_COUNT; ++i)
        if (bhEtask[i].status != 0)
            ++count;
    return count;
}

void recvxEventCoreReset(void)
{
    memset(bhEtask, 0, sizeof(bhEtask));
    memset(task_start, 0, sizeof(task_start));
    memset(task_end, 0, sizeof(task_end));
    memset(task_event_id, 0, sizeof(task_event_id));
    memset(task_bounds_valid, 0, sizeof(task_bounds_valid));
    memset(&event_fault, 0, sizeof(event_fault));
    bhScePtr = NULL;
    bhScdPtr = NULL;
    bhScd2Ptr = NULL;
    bhEvdPtr = NULL;
    G_Sp = NULL;
    bhIfelFlg = 0;
    bhCetask = NULL;
    Event_T_timer = 0;
    event_section = NULL;
    event_section_size = 0;
    event_table_size = 0;
    event_table_count = 0;
    current_task = RECVX_EVENT_NO_TASK;
    instruction_count = 0;
    instruction_count_this_call = 0;
    flag_write_count = 0;
}

int recvxEventCoreBindSection(const void *bytes, size_t size)
{
    const unsigned char *section = bytes;
    uint32_t table_size, count, previous = 0, i;

    if (active_task_count() != 0) {
        set_fault(RECVX_EVENT_FAULT_ACTIVE_TASKS, 0, NULL);
        return -1;
    }
    if (bytes == NULL || size < 12 || size > UINT32_MAX) {
        set_fault(RECVX_EVENT_FAULT_BAD_SECTION, 0, NULL);
        return -1;
    }
    table_size = read_u32(section);
    if (table_size < 12 || (table_size & 3u) != 0 || table_size > size) {
        set_fault(RECVX_EVENT_FAULT_BAD_SECTION, 0, NULL);
        return -1;
    }
    count = table_size / sizeof(uint32_t);
    for (i = 0; i < count; ++i) {
        uint32_t offset = read_u32(section + i * sizeof(uint32_t));
        if (offset < table_size || offset >= size ||
            (i != 0 && offset < previous)) {
            set_fault(RECVX_EVENT_FAULT_BAD_SECTION, 0, NULL);
            return -1;
        }
        previous = offset;
    }

    event_section = section;
    event_section_size = size;
    event_table_size = table_size;
    event_table_count = count;
    return 0;
}

int recvxEventCoreUnbindSection(void)
{
    if (active_task_count() != 0) {
        set_fault(RECVX_EVENT_FAULT_ACTIVE_TASKS, 0, NULL);
        return -1;
    }
    event_section = NULL;
    event_section_size = 0;
    event_table_size = 0;
    event_table_count = 0;
    return 0;
}

const RecvxEventCoreFault *recvxEventCoreGetFault(void)
{
    return &event_fault;
}

const char *recvxEventCoreFaultName(enum RecvxEventCoreFaultCode code)
{
    switch (code) {
    case RECVX_EVENT_FAULT_NONE: return "none";
    case RECVX_EVENT_FAULT_BAD_SECTION: return "invalid event section";
    case RECVX_EVENT_FAULT_BAD_EVENT: return "event ID outside table";
    case RECVX_EVENT_FAULT_BAD_TASK: return "task/work index outside table";
    case RECVX_EVENT_FAULT_PC_RANGE: return "PC outside event body";
    case RECVX_EVENT_FAULT_TRUNCATED: return "truncated instruction";
    case RECVX_EVENT_FAULT_BAD_OPERAND: return "invalid instruction operand";
    case RECVX_EVENT_FAULT_UNSUPPORTED: return "unsupported opcode";
    case RECVX_EVENT_FAULT_WATCHDOG: return "instruction watchdog";
    case RECVX_EVENT_FAULT_ACTIVE_TASKS: return "active tasks block unload";
    default: return "unknown event fault";
    }
}

uint32_t recvxEventCoreTaskPc(unsigned int task)
{
    if (task >= RECVX_EVENT_TASK_COUNT)
        return UINT32_MAX;
    return section_offset(bhEtask[task].data);
}

uint32_t recvxEventCoreTaskStart(unsigned int task)
{
    if (task >= RECVX_EVENT_TASK_COUNT || !task_bounds_valid[task])
        return UINT32_MAX;
    return task_start[task];
}

uint32_t recvxEventCoreTaskBodyPc(unsigned int task)
{
    uint32_t pc = recvxEventCoreTaskPc(task);
    uint32_t start = recvxEventCoreTaskStart(task);

    if (pc == UINT32_MAX || start == UINT32_MAX || pc < start)
        return UINT32_MAX;
    return pc - start;
}

uint32_t recvxEventCoreInstructionCount(void)
{
    return instruction_count;
}

uint32_t recvxEventCoreFlagWriteCount(void)
{
    return flag_write_count;
}

static int event_span(unsigned int event_id, uint32_t *start, uint32_t *end)
{
    uint32_t index;

    if (event_section == NULL || rom == NULL || rom->evtp == NULL ||
        (const unsigned char *)rom->evtp != event_section ||
        event_id > UINT32_MAX - RECVX_EVENT_TABLE_PREFIX_WORDS) {
        set_fault(RECVX_EVENT_FAULT_BAD_EVENT, 0, NULL);
        return -1;
    }
    index = event_id + RECVX_EVENT_TABLE_PREFIX_WORDS;
    if (index >= event_table_count) {
        set_fault(RECVX_EVENT_FAULT_BAD_EVENT, 0, NULL);
        return -1;
    }
    *start = read_u32(event_section + index * sizeof(uint32_t));
    *end = index + 1 < event_table_count ?
           read_u32(event_section + (index + 1) * sizeof(uint32_t)) :
           (uint32_t)event_section_size;
    if (*start < event_table_size || *start >= event_section_size ||
        *end < *start || *end > event_section_size) {
        set_fault(RECVX_EVENT_FAULT_BAD_EVENT, 0, NULL);
        return -1;
    }
    return 0;
}

int Event_init(BH_SCEWORK *task, unsigned int event_id)
{
    unsigned int index;
    uint32_t start, end;

    if (task_index(task, &index) != 0) {
        set_fault(RECVX_EVENT_FAULT_BAD_TASK, 0, NULL);
        return -1;
    }
    if (event_span(event_id, &start, &end) != 0)
        return -1;

    task->status = 1;
    *(short *)&task->mode0 = 0;
    task->data = (unsigned char *)rom->evtp + start;
    task->loop = -1;
    task->ips[0][0] = 0;
    task->ips[0][1] = 0;
    task->ips[0][2] = 0;
    task->ips[1][0] = 0;
    task->ips[1][1] = 0;
    task->ips[1][2] = 0;
    task->ips[2][0] = 0;
    task->ips[2][1] = 0;
    task->ips[2][2] = 0;
    task->ips[3][0] = 0;
    task->ips[3][1] = 0;
    task->ips[3][2] = 0;
    task->ian[0][0] = 0;
    task->ian[0][1] = 0;
    task->ian[0][2] = 0;
    task->ian[1][0] = 0;
    task->ian[1][1] = 0;
    task->ian[1][2] = 0;
    task->ian[2][0] = 0;
    task->ian[2][1] = 0;
    task->ian[2][2] = 0;
    task->ian[3][0] = 0;
    task->ian[3][1] = 0;
    task->ian[3][2] = 0;
    task_start[index] = start;
    task_end[index] = end;
    task_event_id[index] = event_id;
    task_bounds_valid[index] = 1;
    return 0;
}

int Event_exec(unsigned int task_level, unsigned int event_id)
{
    if (event_fault.code != RECVX_EVENT_FAULT_NONE)
        return -1;
    if (task_level >= RECVX_EVENT_TASK_COUNT) {
        task_level = 0;
        while (bhEtask[task_level].status != 0 && task_level != 15)
            ++task_level;
    }
    return Event_init(&bhEtask[task_level], event_id);
}

static int has_task_bytes(const unsigned char *pc, size_t count)
{
    uintptr_t address, base;
    uint32_t offset;

    if (current_task >= RECVX_EVENT_TASK_COUNT ||
        !task_bounds_valid[current_task] || event_section == NULL || pc == NULL)
        return 0;
    address = (uintptr_t)pc;
    base = (uintptr_t)event_section;
    if (address < base || address - base > event_section_size)
        return 0;
    offset = (uint32_t)(address - base);
    return offset >= task_start[current_task] &&
           offset <= task_end[current_task] &&
           count <= task_end[current_task] - offset;
}

static size_t flag_bank_words(unsigned int type)
{
    switch (type) {
    case 1: return 32;
    case 2: return 16;
    case 3: return 32;
    case 4: case 5: case 6: case 10: case 11: case 12: return 1;
    case 7: return 16;
    case 8: return 8;
    case 9: return 16;
    case 13: return sizeof(BH_PWORK) / sizeof(uint32_t);
    case 14: case 15: case 16: return 1;
    default: return 0;
    }
}

static int validate_core_instruction(unsigned int opcode)
{
    size_t extent = 1;

    if (opcode == 5) {
        unsigned int type, word, operation;
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        type = bhScePtr[1];
        word = (read_u16(bhScePtr + 2) & 0x3e0u) >> 5;
        operation = read_u16(bhScePtr + 4) >> 8;
        if (word >= flag_bank_words(type) || operation > 5) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    }
    if (opcode == 248) {
        int next_loop;
        extent = 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        next_loop = (int)bhCetask->loop + 1;
        if (bhScePtr[1] != 249 || read_u16(bhScePtr + 2) == 0 ||
            bhCetask->loop < -1 || next_loop >= 4) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    }
    if (opcode == 249) {
        if (!has_task_bytes(bhScePtr, 1) || bhCetask->loop < 0 ||
            bhCetask->loop >= 4) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    }
    if (opcode == 254)
        extent = 1;
    else if (opcode == 255)
        extent = 2;
    else
        return 0;

    if (!has_task_bytes(bhScePtr, extent)) {
        set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
        return -1;
    }
    return 0;
}

static unsigned int dispatch_instruction(void)
{
    unsigned int opcode;
    unsigned int result;
    unsigned int (*handler)(void);

    if (event_fault.code != RECVX_EVENT_FAULT_NONE)
        return 0;
    if (!has_task_bytes(bhScePtr, 1)) {
        set_fault(RECVX_EVENT_FAULT_PC_RANGE, 0xff, bhScePtr);
        return 0;
    }
    if (instruction_count_this_call >= RECVX_EVENT_INSTRUCTION_LIMIT) {
        set_fault(RECVX_EVENT_FAULT_WATCHDOG, *bhScePtr, bhScePtr);
        return 0;
    }
    opcode = *bhScePtr;
    ++instruction_count_this_call;
    ++instruction_count;
    if (validate_core_instruction(opcode) != 0)
        return 0;
    handler = bhScenarioJmpT[opcode];
    if (handler == NULL) {
        set_fault(RECVX_EVENT_FAULT_UNSUPPORTED, (uint8_t)opcode, bhScePtr);
        return 0;
    }
    result = handler();
    return result;
}

unsigned int bhSet(void)
{
    int bit_index;
    int *a0;
    uint16_t command = read_u16(bhScePtr);
    uint16_t flag_spec = read_u16(bhScePtr + 2);
    uint16_t operation = read_u16(bhScePtr + 4);

    bhScePtr += 6;
    switch (command >> 8) {
    case 1: a0 = (int *)&sys->ev_flg; break;
    case 2: a0 = (int *)&sys->ky_flg; break;
    case 3: a0 = (int *)&sys->ed_flg; break;
    case 4: a0 = (int *)&sys->rm_flg; break;
    case 5: a0 = (int *)&sys->st_flg; break;
    case 6: a0 = (int *)&sys->sp_flg; break;
    case 7: a0 = (int *)sys->it_flg; break;
    case 8: a0 = (int *)sys->mp_flg; break;
    case 9: a0 = (int *)sys->ic_flg; break;
    case 10: a0 = (int *)&sys->cb_flg; break;
    case 11: a0 = (int *)&sys->gm_flg; break;
    case 12: a0 = (int *)&sys->ts_flg; break;
    case 13: a0 = (int *)plp; break;
    case 14: a0 = (int *)&plp->stflg; break;
    case 15: a0 = (int *)&plp->flg2; break;
    case 16: a0 = (int *)&sys->ssd_flg; break;
    default: return 0;
    }
    a0 += (flag_spec & 0x3e0) >> 5;
    bit_index = flag_spec & 0x1f;
    switch (operation >> 8) {
    case 0: *a0 |= 0x80000000 >> bit_index; break;
    case 1: *a0 &= ~(0x80000000 >> bit_index); break;
    case 2: *a0 ^= 0x80000000 >> bit_index; break;
    case 3: *a0 |= 0xffffffff >> bit_index; break;
    case 4: *a0 &= ~(0xffffffff >> bit_index); break;
    case 5: *a0 ^= 0xffffffff >> bit_index; break;
    default: return 0;
    }
    ++flag_write_count;
    return 1;
}

unsigned int bhSleep(void)
{
    unsigned int loop;

    bhCetask->loop++;
    loop = (unsigned char)bhCetask->loop;
    bhCetask->cnt[loop] = read_u16(bhScePtr + 2);
    bhScePtr++;
    if (--bhCetask->cnt[loop] == 0) {
        bhScePtr += 3;
        bhCetask->loop--;
    }
    return 0;
}

unsigned int bhSleeping(void)
{
    unsigned int loop = (unsigned char)bhCetask->loop;

    if (--bhCetask->cnt[loop] == 0) {
        bhScePtr += 3;
        bhCetask->loop--;
    }
    return 0;
}

unsigned int bhEvtNext(void)
{
    ++bhScePtr;
    return 0;
}

unsigned int bhEvtEnd(void)
{
    bhCetask->status = 0;
    bhScePtr += 2;
    return 0;
}

int bhEventScheduler2(void)
{
    unsigned int level;
    unsigned int t_time_flg = 0;
    BH_SCEWORK *scep;

    if (event_fault.code != RECVX_EVENT_FAULT_NONE)
        return -1;
    if (sys == NULL) {
        set_fault(RECVX_EVENT_FAULT_BAD_SECTION, 0, NULL);
        return -1;
    }
    if ((sys->sp_flg & 0x10) == 0)
        return 0;

    instruction_count_this_call = 0;
    scep = bhEtask;
    for (level = 0; level < RECVX_EVENT_TASK_COUNT; ++level, ++scep) {
        bhCetask = scep;
        current_task = level;
        if (scep->status == 0)
            continue;
        if (!task_bounds_valid[level]) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, 0, scep->data);
            break;
        }
        if (t_time_flg == 0)
            ++Event_T_timer;
        t_time_flg = 1;
        bhScePtr = scep->data;
        while (event_fault.code == RECVX_EVENT_FAULT_NONE) {
            while (event_fault.code == RECVX_EVENT_FAULT_NONE &&
                   dispatch_instruction() != 0)
                ;
            if (event_fault.code != RECVX_EVENT_FAULT_NONE || bhIfelFlg <= 0)
                break;
            if (G_Sp == NULL) {
                set_fault(RECVX_EVENT_FAULT_BAD_TASK, 0, bhScePtr);
                break;
            }
            bhScePtr = (unsigned char *)--G_Sp;
            --bhIfelFlg;
        }
        scep->data = bhScePtr;
        if (event_fault.code != RECVX_EVENT_FAULT_NONE)
            break;
    }
    current_task = RECVX_EVENT_NO_TASK;
    return event_fault.code == RECVX_EVENT_FAULT_NONE ? 0 : -1;
}
