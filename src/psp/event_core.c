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

typedef unsigned int (*RecvxEventHandler)(void);

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
/* Distinguish counted loops from sleep/while frames sharing the retail stack. */
static unsigned char *for_body[RECVX_EVENT_TASK_COUNT][4];
#ifdef RECVX_GAME_BOOT
static unsigned char *while_condition[RECVX_EVENT_TASK_COUNT][4];
static unsigned char *while_body[RECVX_EVENT_TASK_COUNT][4];
#endif
static unsigned int current_task = RECVX_EVENT_NO_TASK;
static uint32_t instruction_count;
static uint32_t instruction_count_this_call;
static uint32_t flag_write_count;
static RecvxEventCoreDispatch last_dispatch;
static int last_dispatch_valid;
static RecvxEventCoreFault event_fault;
static uint32_t scenario_start;
static uint32_t scenario_end;
static int scenario_active;

static int has_task_bytes(const unsigned char *pc, size_t count);
static int pop_conditional_branch(void);
static unsigned int dispatch_instruction(void);

static void clear_while(unsigned int task, unsigned int depth)
{
#ifdef RECVX_GAME_BOOT
    while_condition[task][depth] = NULL;
    while_body[task][depth] = NULL;
#else
    (void)task; (void)depth;
#endif
}

#ifdef RECVX_EVENT_CORE_TEST
#define RECVX_EVENT_CORE(opcode, handler) [opcode] = handler,
#define RECVX_EVENT_ENGINE(opcode, handler)
#define RECVX_EVENT_FULL(opcode, handler)
unsigned int (*bhScenarioJmpT[256])() = {
#include "event_dispatch_table.inc"
};
#undef RECVX_EVENT_FULL
#undef RECVX_EVENT_ENGINE
#undef RECVX_EVENT_CORE
#elif defined(RECVX_EVENT_ENGINE_TEST)
#define RECVX_EVENT_CORE(opcode, handler) [opcode] = handler,
#define RECVX_EVENT_ENGINE(opcode, handler) [opcode] = handler,
#define RECVX_EVENT_FULL(opcode, handler)
unsigned int (*bhScenarioJmpT[256])() = {
#include "event_dispatch_table.inc"
};
#undef RECVX_EVENT_FULL
#undef RECVX_EVENT_ENGINE
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
    event_fault.handler_at_fault = (uintptr_t)bhScenarioJmpT[opcode];
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

static int valid_task_record(void)
{
    unsigned int index;

    return task_index(bhCetask, &index) == 0;
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
    memset(for_body, 0, sizeof(for_body));
#ifdef RECVX_GAME_BOOT
    memset(while_condition, 0, sizeof(while_condition));
    memset(while_body, 0, sizeof(while_body));
#endif
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
    memset(&last_dispatch, 0, sizeof(last_dispatch));
    last_dispatch_valid = 0;
    scenario_start = 0;
    scenario_end = 0;
    scenario_active = 0;
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

int recvxEventCoreRunScenario(uint32_t offset)
{
    uint32_t start0, start1, end;

    if (event_fault.code != RECVX_EVENT_FAULT_NONE || event_section == NULL ||
        event_table_count < 3 || scenario_active)
        return -1;
    start0 = read_u32(event_section);
    start1 = read_u32(event_section + sizeof(uint32_t));
    if (offset == start0) {
        end = start1;
    } else if (offset == start1) {
        end = read_u32(event_section + 2 * sizeof(uint32_t));
    } else {
        set_fault(RECVX_EVENT_FAULT_BAD_SECTION, 0, NULL);
        return -1;
    }
    if (end < offset || end > event_section_size) {
        set_fault(RECVX_EVENT_FAULT_BAD_SECTION, 0, NULL);
        return -1;
    }

    scenario_start = offset;
    scenario_end = end;
    scenario_active = 1;
    current_task = RECVX_EVENT_NO_TASK;
    instruction_count_this_call = 0;
    bhIfelFlg = 0;
    bhScePtr = (unsigned char *)event_section + offset;
    while (event_fault.code == RECVX_EVENT_FAULT_NONE) {
        while (event_fault.code == RECVX_EVENT_FAULT_NONE &&
               dispatch_instruction() != 0)
            ;
        if (event_fault.code != RECVX_EVENT_FAULT_NONE || bhIfelFlg <= 0)
            break;
        if (pop_conditional_branch() != 0)
            break;
        --bhIfelFlg;
    }
    current_task = RECVX_EVENT_NO_TASK;
    scenario_active = 0;
    return event_fault.code == RECVX_EVENT_FAULT_NONE ? 0 : -1;
}

const RecvxEventCoreFault *recvxEventCoreGetFault(void)
{
    return &event_fault;
}

int recvxEventCoreGetLastDispatch(RecvxEventCoreDispatch *dispatch)
{
    if (dispatch == NULL || !last_dispatch_valid)
        return -1;
    *dispatch = last_dispatch;
    return 0;
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
    memset(for_body[index], 0, sizeof(for_body[index]));
    for (unsigned int depth = 0; depth < 4; ++depth) clear_while(index, depth);
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

    if (event_section == NULL || pc == NULL)
        return 0;
    address = (uintptr_t)pc;
    base = (uintptr_t)event_section;
    if (address < base || address - base > event_section_size)
        return 0;
    offset = (uint32_t)(address - base);
    if (scenario_active)
        return offset >= scenario_start && offset <= scenario_end &&
               count <= scenario_end - offset;
    if (current_task >= RECVX_EVENT_TASK_COUNT ||
        !task_bounds_valid[current_task])
        return 0;
    return offset >= task_start[current_task] &&
           offset <= task_end[current_task] &&
           count <= task_end[current_task] - offset;
}

static int pop_conditional_branch(void)
{
    uintptr_t stack, destination;

    if (G_Sp == NULL) {
        set_fault(RECVX_EVENT_FAULT_BAD_TASK, 1, bhScePtr);
        return -1;
    }
    stack = (uintptr_t)G_Sp;
    if (stack < sizeof(*G_Sp)) {
        set_fault(RECVX_EVENT_FAULT_BAD_TASK, 1, bhScePtr);
        return -1;
    }
    destination = stack - sizeof(*G_Sp);
    if (!has_task_bytes((const unsigned char *)destination, 1)) {
        set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, 1, bhScePtr);
        return -1;
    }
    bhScePtr = (unsigned char *)destination;
    G_Sp = (unsigned int *)destination;
    return 0;
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

    if (opcode == 0) {
        extent = 1;
    } else if (opcode == 1) {
        uint16_t command;
        uintptr_t target;
        extent = 2;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        command = read_u16(bhScePtr);
        target = (uintptr_t)bhScePtr + sizeof(uint16_t) + (command >> 8);
        if (!has_task_bytes((const unsigned char *)target, 1)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
    } else if (opcode == 2) {
        uintptr_t target;
        if (!has_task_bytes(bhScePtr, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (bhIfelFlg <= 0 || G_Sp == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        target = (uintptr_t)bhScePtr + bhScePtr[1];
        if ((scenario_active &&
             (target < (uintptr_t)event_section + scenario_start ||
              target > (uintptr_t)event_section + scenario_end)) ||
            (!scenario_active &&
             (target < (uintptr_t)event_section + task_start[current_task] ||
              target > (uintptr_t)event_section + task_end[current_task]))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 3) {
        extent = 2;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (bhIfelFlg <= 0 || G_Sp == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 4) {
        unsigned int type, word;
        uint16_t flag_spec;
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        type = bhScePtr[1];
        flag_spec = read_u16(bhScePtr + 2);
        word = (flag_spec & 0x3ffu) >> 5;
        if (flag_bank_words(type) == 0 ||
            word >= flag_bank_words(type) ||
            ((type <= 12 || type == 16) && sys == NULL) ||
            (type >= 13 && type <= 15 && plp == NULL)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 5) {
        unsigned int type, word, operation;
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        type = bhScePtr[1];
        word = (read_u16(bhScePtr + 2) & 0x3e0u) >> 5;
        operation = read_u16(bhScePtr + 4) >> 8;
        if (word >= flag_bank_words(type) || operation > 5 ||
            ((type <= 12 || type == 16) && sys == NULL) ||
            (type >= 13 && type <= 15 && plp == NULL)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 6) {
        if (!has_task_bytes(bhScePtr, 4)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        unsigned int selector = bhScePtr[1];
        if (!(selector == 0 || selector == 1 || selector == 2 || selector == 8 ||
              selector == 15 || selector == 17 || selector == 21 || selector == 23 ||
              selector == 24 || selector == 25) || bhScePtr[2] > 5 ||
            ((selector != 2 && selector != 17) && sys == NULL)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 7) {
        if (!has_task_bytes(bhScePtr, 6)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        unsigned int selector = bhScePtr[2], index = bhScePtr[1];
        if (!(selector == 5 || selector == 7 || selector == 9 || selector == 10) ||
            bhScePtr[3] > 5 || (selector == 5 && plp == NULL) ||
            ((selector == 7 || selector == 9) && sys == NULL) ||
            (selector == 10 && (rom == NULL || rom->enep == NULL || rom->ene_n < 0 ||
                index >= (unsigned int)rom->ene_n || rom->enep[index].wrk_no >= 128))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 8) {
        extent = 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL ||
            !(bhScePtr[1] == 0 || bhScePtr[1] == 1 ||
              bhScePtr[1] == 2 || bhScePtr[1] == 8 ||
              bhScePtr[1] == 15 || bhScePtr[1] == 16 ||
              bhScePtr[1] == 18 || bhScePtr[1] == 19 ||
              bhScePtr[1] == 20 || bhScePtr[1] == 21 ||
              bhScePtr[1] == 22 || bhScePtr[1] == 25) ||
            ((bhScePtr[1] == 16 || bhScePtr[1] == 18) && plp == NULL) ||
            ((bhScePtr[1] == 18 || bhScePtr[1] == 22) &&
             (sys->ply_id < 0 || sys->ply_id >= 4))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 10 || opcode == 11 || opcode == 12) {
        unsigned int index;
        int count = opcode == 10 ? (rom != NULL ? rom->wal_n : 0) :
                                   opcode == 11 ? (rom != NULL ? rom->etc_n : 0) : (rom != NULL ? rom->flr_n : 0);
        const ATR_WORK *attributes = opcode == 10 ?
            (rom != NULL ? rom->walp : NULL) :
            opcode == 11 ? (rom != NULL ? rom->etcp : NULL) : (rom != NULL ? rom->flrp : NULL);
        extent = 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        index = bhScePtr[1];
        if (count < 0 || index >= (unsigned int)count || attributes == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 13) {
        if (!has_task_bytes(bhScePtr, 4)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        unsigned int index = bhScePtr[1];
        if (sys == NULL || rom == NULL || rom->enep == NULL || rom->ene_n < 0 ||
            index >= (unsigned int)rom->ene_n || rom->enep[index].wrk_no >= 128) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 14) {
        unsigned int item_flag, etc_index;
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        item_flag = read_u16(bhScePtr + 2);
        etc_index = bhScePtr[4];
        if (rom == NULL || rom->etc_n < 0 || etc_index >= (unsigned int)rom->etc_n ||
            rom->etcp == NULL || sys == NULL || sys->itwp == NULL ||
            (item_flag & 0x3ffu) >= 512u ||
            rom->etcp[etc_index].prm0 >= 39936u / sizeof(O_WRK)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 15 || opcode == 16) {
        if (!has_task_bytes(bhScePtr, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 94 || opcode == 95 || opcode == 155) {
        extent = opcode == 94 ? 4 : opcode == 95 ? 2 : 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || (opcode == 94 && bhScePtr[1] >= 22)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 31 || opcode == 91 || opcode == 92) {
        if (!has_task_bytes(bhScePtr, opcode == 31 ? 4 : 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || (opcode == 91 && plp == NULL)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 18 || opcode == 19 || opcode == 54) {
        extent = opcode == 18 ? 2 : opcode == 19 ? 4 : 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || (opcode == 18 && bhScePtr[1] > 5) ||
            (opcode == 19 && bhScePtr[1] > 1)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 23 || opcode == 24 || opcode == 25 || opcode == 28 || opcode == 29) {
        extent = opcode == 23 || opcode == 25 ? 8 : opcode == 24 ? 2 : opcode == 28 ? 6 : 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || (opcode == 25 && !valid_task_record())) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 21 || opcode == 22 || opcode == 166) {
        extent = opcode == 22 ? 2 : 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 20) {
        unsigned int event_id;
        extent = 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        event_id = bhScePtr[3];
        if (event_table_count <= RECVX_EVENT_TABLE_PREFIX_WORDS ||
            event_id >= event_table_count - RECVX_EVENT_TABLE_PREFIX_WORDS) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 17) {
        extent = 2;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || sys->ply_id < 0 || sys->ply_id >= 24) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 32 || opcode == 36 || opcode == 47 || opcode == 50 || opcode == 83) {
        unsigned int kind, index;
        extent = (opcode == 50 || opcode == 83) ? 12 : 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        kind = opcode == 36 ? bhScePtr[1] : opcode == 32 ? bhScePtr[2] : 1;
        index = opcode == 36 ? bhScePtr[2] : bhScePtr[1];
        if (kind > 3 || sys == NULL ||
            (kind == 0 && plp == NULL) ||
            (kind == 1 && (rom == NULL || rom->enep == NULL || rom->ene_n < 0 ||
                index >= (unsigned int)rom->ene_n || rom->enep[index].wrk_no >= 128)) ||
            (kind == 2 && (sys->obwp == NULL || index >= 39936u / sizeof(O_WRK))) ||
            (kind == 3 && (sys->itwp == NULL || index >= 39936u / sizeof(O_WRK))) ||
            (opcode == 47 && bhScePtr[2] > 4) ||
            (opcode == 50 && (sys->obwp == NULL || bhScePtr[2] >= 39936u / sizeof(O_WRK))) ||
            (opcode == 83 && (sys->itwp == NULL || bhScePtr[2] >= 39936u / sizeof(O_WRK)))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 59) {
        BH_PWORK *actor;
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || bhScePtr[1] > 2 ||
            (bhScePtr[1] == 0 && plp == NULL) ||
            (bhScePtr[1] == 1 && (rom == NULL || rom->enep == NULL || rom->ene_n < 0 ||
                bhScePtr[2] >= (unsigned int)rom->ene_n || rom->enep[bhScePtr[2]].wrk_no >= 128)) ||
            (bhScePtr[1] == 2 && (sys->obwp == NULL || bhScePtr[2] >= 39936u / sizeof(O_WRK)))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        actor = bhScePtr[1] == 0 ? plp : bhScePtr[1] == 1 ?
            &ene[rom->enep[bhScePtr[2]].wrk_no] : (BH_PWORK *)&sys->obwp[bhScePtr[2]];
        if (actor->mlwP == NULL || actor->mlwP->objP == NULL || bhScePtr[3] >= actor->mlwP->obj_num) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 145 || opcode == 173) {
        if (!has_task_bytes(bhScePtr, 4)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || sys->efid[bhScePtr[1]] < 0 || sys->efid[bhScePtr[1]] >= 512) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 67 || opcode == 53) {
        extent = 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || rom == NULL ||
            (opcode == 67 && (sys->efid[bhScePtr[1]] < 0 ||
                sys->efid[bhScePtr[1]] >= 512)) ||
            (opcode == 53 && (bhScePtr[3] > 1 ||
                (bhScePtr[3] == 0 && (rom->lgtp == NULL || rom->lgt_n < 0 ||
                    bhScePtr[2] >= (unsigned int)rom->lgt_n)) ||
                (bhScePtr[3] == 1 && (rom->evlp == NULL || rom->evl_n < 0 ||
                    bhScePtr[2] >= (unsigned int)rom->evl_n))))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 68) {
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (rom == NULL || bhScePtr[4] >= 4) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 38) {
        extent = 2;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (plp == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 39) {
        if (!has_task_bytes(bhScePtr, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || plp == NULL || sys->ply_id < 0 || sys->ply_id >= 4) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 66) {
        extent = 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (sys == NULL || sys->itwp == NULL ||
            bhScePtr[1] >= 39936u / sizeof(O_WRK)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 27) {
        if (!has_task_bytes(bhScePtr, 6)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (bhScePtr[2] >= 2) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 129 || opcode == 188) {
        if (!has_task_bytes(bhScePtr, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if ((opcode == 129 && sys == NULL) || (opcode == 188 && bhScePtr[1] >= 16)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 26 || opcode == 55 || opcode == 182) {
        extent = 2;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if ((opcode == 26 && !valid_task_record()) ||
            (opcode == 55 && sys == NULL)) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 35) {
        unsigned int item_work, flag, item;
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        item_work = bhScePtr[1];
        flag = read_u16(bhScePtr + 2);
        item = bhScePtr[4];
        if (rom == NULL || rom->etcp == NULL || sys == NULL ||
            sys->itwp == NULL || rom->etc_n < 0 ||
            item_work >= 39936u / sizeof(O_WRK) ||
            item >= (unsigned int)rom->etc_n || flag >= 512u) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 37) {
        extent = 10;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (rom == NULL || rom->etcp == NULL || rom->etc_n < 0 ||
            bhScePtr[1] >= (unsigned int)rom->etc_n) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 56) {
        extent = 6;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (bhScePtr[1] > 2 || sys == NULL ||
            (bhScePtr[1] == 0 && plp == NULL) ||
            (bhScePtr[1] == 1 &&
             (rom == NULL || rom->enep == NULL ||
              rom->ene_n < 0 ||
              bhScePtr[2] >= (unsigned int)rom->ene_n ||
              rom->enep[bhScePtr[2]].wrk_no >= 128)) ||
            (bhScePtr[1] == 2 &&
             (sys->obwp == NULL ||
              bhScePtr[2] >= 39936u / sizeof(O_WRK)))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 43 || opcode == 103) {
        if (!has_task_bytes(bhScePtr, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        unsigned int command = bhScePtr[1];
        if (!valid_task_record() || sys == NULL ||
            (bhCetask->work == NULL && !(opcode == 103 && (command == 0 || command == 139)))) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if ((opcode == 43 && command > 1) ||
            (opcode == 103 && command != 0 && command != 128 && command != 139 && command != 144 && command != 149)) {
            set_fault(RECVX_EVENT_FAULT_UNSUPPORTED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (opcode == 103 && command == 149 && !has_task_bytes(bhScePtr, 4)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if ((opcode == 103 && command == 128 && (bhCetask->work->id >= 128 || !sys->emtp[bhCetask->work->id])) ||
            (opcode == 43 && bhCetask->work->id == 1 && bhCetask->work->exp1 == NULL)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 60 || opcode == 62 || opcode == 154) {
        if (!has_task_bytes(bhScePtr, 4)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        unsigned int index = bhScePtr[opcode == 154 ? 1 : 2];
        if (sys == NULL || rom == NULL || rom->enep == NULL || rom->ene_n < 0 ||
            index >= (unsigned int)rom->ene_n || rom->enep[index].wrk_no >= 128 ||
            ene[rom->enep[index].wrk_no].exp0 == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 101) {
        extent = 4;
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (!valid_task_record() || sys == NULL || bhScePtr[1] > 4 ||
            (bhScePtr[1] == 0 && plp == NULL) ||
            (bhScePtr[1] == 1 && (rom == NULL || rom->enep == NULL || rom->ene_n < 0 ||
                bhScePtr[2] >= (unsigned int)rom->ene_n || rom->enep[bhScePtr[2]].wrk_no >= 128)) ||
            (bhScePtr[1] == 2 && (sys->obwp == NULL || bhScePtr[2] >= 39936u / sizeof(O_WRK))) ||
            (bhScePtr[1] == 3 && (sys->itwp == NULL || bhScePtr[2] >= 39936u / sizeof(O_WRK))) ||
            (bhScePtr[1] == 4 && (sys->efid[bhScePtr[2]] < 0 ||
                sys->efid[bhScePtr[2]] >= 512))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    } else if (opcode == 105) {
        unsigned int command;
        if (!has_task_bytes(bhScePtr, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (!valid_task_record() || bhCetask->work == NULL) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        command = bhScePtr[1];
        switch (command) {
        case 2: extent = 3; break;
        case 5: extent = 6; break;
        case 7: extent = 8; break;
        case 11: case 12: case 13: extent = 6; break;
        case 23: extent = 4; break;
        case 36: extent = 2; break;
        case 24:
            if (!has_task_bytes(bhScePtr, 4)) {
                set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode,
                          bhScePtr);
                return -1;
            }
            if (bhScePtr[2] > 25 || bhScePtr[3] > 3) {
                set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode,
                          bhScePtr);
                return -1;
            }
            extent = bhScePtr[3] == 1 ? 10 : 8;
            break;
        case 47: extent = 2; break;
        default:
            set_fault(RECVX_EVENT_FAULT_UNSUPPORTED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (!has_task_bytes(bhScePtr, extent)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if ((command == 23 && bhScePtr[2] > 3) ||
            (command == 36 && (bhCetask->work->mlwP == NULL ||
                bhCetask->work->mlwP->objP == NULL || !bhCetask->work->mlwP->obj_num ||
                bhCetask->work->mlwP->obj_num > 128))) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    }
    if (opcode == 250) {
        if (current_task >= RECVX_EVENT_TASK_COUNT || !valid_task_record()) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (!has_task_bytes(bhScePtr, 4)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (bhCetask->loop < -1 || bhCetask->loop >= 3 ||
            !read_u16(bhScePtr + 2) || read_u16(bhScePtr + 2) > INT16_MAX ||
            !has_task_bytes(bhScePtr + 4, 1)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
    }
    if (opcode == 251) {
        int loop = bhCetask != NULL ? bhCetask->loop : -1;
        if (current_task >= RECVX_EVENT_TASK_COUNT || !valid_task_record()) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (!has_task_bytes(bhScePtr, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        if (loop < 0 || loop >= 4 || !for_body[current_task][loop] ||
            for_body[current_task][loop] != bhCetask->lstack[loop] ||
            !has_task_bytes(for_body[current_task][loop], 1) ||
            for_body[current_task][loop] > bhScePtr || bhCetask->cnt[loop] <= 0) {
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
    if (opcode == 252) {
#ifdef RECVX_GAME_BOOT
        unsigned char *command = bhScePtr;
        if (current_task >= RECVX_EVENT_TASK_COUNT || !valid_task_record()) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, command);
            return -1;
        }
        if (!has_task_bytes(command, 2)) {
            set_fault(RECVX_EVENT_FAULT_TRUNCATED, (uint8_t)opcode, command);
            return -1;
        }
        if (bhCetask->loop < -1 || bhCetask->loop >= 3 || command[1] < 8 ||
            !has_task_bytes(command + 2, 6) ||
            !has_task_bytes((unsigned char *)((uintptr_t)command + command[1]), 1)) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, command);
            return -1;
        }
        if (command[2] != 4 && command[2] != 56 && command[2] != 155) {
            set_fault(RECVX_EVENT_FAULT_UNSUPPORTED, command[2], command + 2);
            return -1;
        }
        bhScePtr = command + 2;
        int result = validate_core_instruction(command[2]);
        bhScePtr = command;
        return result;
#else
        uintptr_t target;
        extent = 2;
        if (!has_task_bytes(bhScePtr, extent) || bhCetask->loop < -1 ||
            bhCetask->loop >= 3) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        target = (uintptr_t)bhScePtr + bhScePtr[1];
        if (!has_task_bytes(bhScePtr + 2, 1) ||
            target > (uintptr_t)event_section +
                (scenario_active ? scenario_end : task_end[current_task])) {
            set_fault(RECVX_EVENT_FAULT_BAD_OPERAND, (uint8_t)opcode, bhScePtr);
            return -1;
        }
        return 0;
#endif
    }
    if (opcode == 253
#ifdef RECVX_GAME_BOOT
        || opcode == 243
#endif
    ) {
#ifdef RECVX_GAME_BOOT
        if (current_task >= RECVX_EVENT_TASK_COUNT || !valid_task_record()) {
            set_fault(RECVX_EVENT_FAULT_BAD_TASK, (uint8_t)opcode, bhScePtr);
            return -1;
        }
#endif
        int loop = bhCetask->loop;
        if (loop < 0 || loop >= 4 ||
            bhCetask->lcondition[(unsigned int)loop] == NULL
#ifdef RECVX_GAME_BOOT
            || !while_condition[current_task][loop] || !while_body[current_task][loop] ||
            while_condition[current_task][loop] != bhCetask->lcondition[loop] ||
            while_body[current_task][loop] != bhCetask->lstack[loop] ||
            for_body[current_task][loop] != NULL ||
            !has_task_bytes(while_condition[current_task][loop], 6) ||
            !has_task_bytes(while_body[current_task][loop], 1) ||
            (uintptr_t)while_body[current_task][loop] > (uintptr_t)bhScePtr
#endif
        ) {
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
    handler = bhScenarioJmpT[opcode];
    ++instruction_count_this_call;
    ++instruction_count;
    if (handler == NULL) {
        set_fault(RECVX_EVENT_FAULT_UNSUPPORTED, (uint8_t)opcode, bhScePtr);
        return 0;
    }
    if (validate_core_instruction(opcode) != 0)
        return 0;
    last_dispatch.section_pc = section_offset(bhScePtr);
    last_dispatch.handler = (uintptr_t)handler;
    last_dispatch.opcode = (uint8_t)opcode;
    last_dispatch_valid = 1;
    result = handler();
    return result;
}

void recvxEventCoreFailUnsupportedCommand(uint8_t opcode,
                                          const unsigned char *pc)
{
    set_fault(RECVX_EVENT_FAULT_UNSUPPORTED, opcode, pc);
}

int recvxEventCoreDispatchCondition(unsigned int *result)
{
    unsigned char *condition;

    if (result == NULL || current_task >= RECVX_EVENT_TASK_COUNT ||
        bhCetask == NULL || bhCetask->loop < 0 || bhCetask->loop >= 4) {
        set_fault(RECVX_EVENT_FAULT_BAD_TASK, 253, bhScePtr);
        return -1;
    }
    condition = bhCetask->lcondition[(unsigned int)bhCetask->loop];
#ifdef RECVX_GAME_BOOT
    if (!has_task_bytes(bhScePtr, 1)) {
        set_fault(RECVX_EVENT_FAULT_PC_RANGE, 253, bhScePtr);
        return -1;
    }
    if (validate_core_instruction(*bhScePtr) != 0)
        return -1;
#endif
    if (!has_task_bytes(condition, 1) || (*condition != 56
#ifdef RECVX_GAME_BOOT
        && *condition != 155 && *condition != 4
#endif
    )) {
        set_fault(RECVX_EVENT_FAULT_UNSUPPORTED,
                  condition != NULL && has_task_bytes(condition, 1) ?
                      *condition : 253,
                  condition);
        return -1;
    }
    bhScePtr = condition;
    *result = dispatch_instruction();
    return event_fault.code == RECVX_EVENT_FAULT_NONE ? 0 : -1;
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

// Extracted from original event.c::bhEnd
unsigned int bhEnd(void)
{
    bhIfelFlg = 0;
    return 0;
}

unsigned int bhSleep(void)
{
    unsigned int loop;

    bhCetask->loop++;
    loop = (unsigned char)bhCetask->loop;
    if (current_task < RECVX_EVENT_TASK_COUNT) {
        for_body[current_task][loop] = NULL;
        clear_while(current_task, loop);
    }
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

unsigned int bhFor(void)
{
    unsigned int loop = (unsigned int)++bhCetask->loop;
    clear_while(current_task, loop);
    bhCetask->cnt3 = bhCetask->cnt2 = bhCetask->cnt[loop] = read_u16(bhScePtr + 2);
    bhScePtr += 4;
    bhCetask->lstack[loop] = bhScePtr;
    for_body[current_task][loop] = bhScePtr;
    return 1;
}

#ifdef RECVX_GAME_BOOT
unsigned int bhWhile(void)
{
    unsigned char *condition = bhScePtr + 2;
    unsigned char *body = bhScePtr + bhScePtr[1];
    unsigned int loop = (unsigned int)++bhCetask->loop;
    bhCetask->lcondition[loop] = condition;
    bhScePtr = bhCetask->lstack[loop] = body;
    while_condition[current_task][loop] = condition;
    while_body[current_task][loop] = body;
    for_body[current_task][loop] = NULL;
    return 1;
}

static unsigned int end_while(unsigned int yield_on_false)
{
    unsigned int condition_result;
    unsigned int loop = (unsigned char)bhCetask->loop;
    bhCetask->data = bhScePtr;
    if (recvxEventCoreDispatchCondition(&condition_result) != 0)
        return 0;
    if (condition_result != 0) {
        bhScePtr = bhCetask->lstack[loop];
    } else {
        bhScePtr = bhCetask->data + 1;
        clear_while(current_task, loop);
        --bhCetask->loop;
        return !yield_on_false;
    }
    return 1;
}
unsigned int bhEwhile(void) { return end_while(1); }
unsigned int bhEwhile2(void) { return end_while(0); }
#endif

unsigned int bhNext(void)
{
    unsigned int loop = (unsigned char)bhCetask->loop;
    if (--bhCetask->cnt[loop] != 0) {
        bhCetask->cnt2 = bhCetask->cnt[loop];
        bhScePtr = bhCetask->lstack[loop];
    } else {
        bhScePtr += 2;
        for_body[current_task][loop] = NULL;
        bhCetask->loop--;
    }
    return 1;
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
            if (pop_conditional_branch() != 0)
                break;
            --bhIfelFlg;
        }
        scep->data = bhScePtr;
        if (event_fault.code != RECVX_EVENT_FAULT_NONE)
            break;
    }
    current_task = RECVX_EVENT_NO_TASK;
    return event_fault.code == RECVX_EVENT_FAULT_NONE ? 0 : -1;
}
