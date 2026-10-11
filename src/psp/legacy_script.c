#include <stddef.h>
#include <stdint.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "psp/event_data.h"
#include "psp/legacy_runtime.h"

#define RECVX_SCRIPT_MAX_DEPTH 32u
#define RECVX_SCRIPT_MAX_INSTRUCTIONS 4096u

static uint16_t read_u16(const unsigned char *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static int has_bytes(size_t size, size_t offset, size_t count)
{
    return offset <= size && count <= size - offset;
}

static unsigned int *flag_word(unsigned int type, unsigned int word)
{
    switch (type) {
    case 1: return word < 32 ? &sys->ev_flg[word] : NULL;
    case 2: return word < 16 ? &sys->ky_flg[word] : NULL;
    case 3: return word < 32 ? &sys->ed_flg[word] : NULL;
    case 4: return word == 0 ? &sys->rm_flg : NULL;
    case 5: return word == 0 ? &sys->st_flg : NULL;
    case 6: return word == 0 ? &sys->sp_flg : NULL;
    case 7: return word < 16 ? &sys->it_flg[word] : NULL;
    case 8: return word < 8 ? &sys->mp_flg[word] : NULL;
    case 9: return word < 16 ? &sys->ic_flg[word] : NULL;
    case 10: return word == 0 ? &sys->cb_flg : NULL;
    case 11: return word == 0 ? &sys->gm_flg : NULL;
    case 12: return word == 0 ? &sys->ts_flg : NULL;
    case 13: return word == 0 ? &plp->flg : NULL;
    case 14: return word == 0 ? &plp->stflg : NULL;
    case 15: return word == 0 ? &plp->flg2 : NULL;
    case 16: return word == 0 ? &sys->ssd_flg : NULL;
    default: return NULL;
    }
}

static unsigned int *set_flag_word(unsigned int type, unsigned int word)
{
    if (type == 13)
        return word < sizeof(*plp) / sizeof(unsigned int) ?
               &((unsigned int *)plp)[word] : NULL;
    return flag_word(type, word);
}

static int compare_values(uint32_t left, unsigned int operation,
                          uint32_t right)
{
    switch (operation) {
    case 0: return left == right;
    case 1: return left > right;
    case 2: return left >= right;
    case 3: return left < right;
    case 4: return left <= right;
    case 5: return left != right;
    default: return -1;
    }
}

static int compare_byte_value(unsigned int variable, unsigned int operation,
                              unsigned int value, int *result)
{
    uint32_t current;

    switch (variable) {
    case 0: current = (unsigned char)sys->stg_no; break;
    case 1: current = (unsigned char)sys->rom_no; break;
    case 8: current = sys->sb_id; break;
    case 15: current = (unsigned char)sys->pos_no; break;
    case 21: current = (unsigned char)sys->rcase; break;
    case 23: current = (uint32_t)sys->gm_mode; break;
    case 24: current = (unsigned char)sys->ply_id; break;
    case 25: current = sys->costume; break;
    default: return -1;
    }
    *result = compare_values(current, operation, value);
    return *result < 0 ? -1 : 0;
}

static int compare_word_value(unsigned int variable, unsigned int operation,
                              uint32_t value, int *result)
{
    uint32_t current;

    switch (variable) {
    case 5: current = (uint16_t)plp->hp; break;
    case 7: current = (uint16_t)sys->mes_sel; break;
    case 9: current = (uint32_t)sys->stv_tm; break;
    default: return -1;
    }
    *result = compare_values(current, operation, value);
    return *result < 0 ? -1 : 0;
}

static int store_byte_value(unsigned int variable, unsigned int value)
{
    switch (variable) {
    case 0: sys->stg_no = (char)value; break;
    case 1: sys->rom_no = (char)value; break;
    case 8: sys->sb_id = value; break;
    case 15: sys->pos_no = (char)value; break;
    case 21: sys->rcase = (char)value; break;
    case 23: sys->gm_mode = (int)value; break;
    case 24: sys->ply_id = (char)value; break;
    case 25: sys->costume = value; break;
    default: return -1;
    }
    return 0;
}

static int store_word_value(unsigned int variable, uint32_t value)
{
    switch (variable) {
    case 5: plp->hp = (short)value; break;
    case 9: sys->stv_tm = (int)value; break;
    default: return -1;
    }
    return 0;
}

static int execute_simple(const unsigned char *script, size_t size,
                          const RecvxEventData *events,
                          size_t *pc, size_t *targets, unsigned int *depth,
                          uint32_t *instructions, uint32_t *writes,
                          RecvxLegacyScriptResult *result,
                          unsigned char *opcode)
{
    unsigned int *word;
    uint16_t a, b, c;
    unsigned int type, bit, operation;
    int condition;

    if (!has_bytes(size, *pc, 1))
        return -2;
    *opcode = script[*pc];
    ++*instructions;
    switch (*opcode) {
    case 0:
        *depth = 0;
        return 2;
    case 1:
        if (!has_bytes(size, *pc, 2) || *depth == RECVX_SCRIPT_MAX_DEPTH)
            return -2;
        a = read_u16(script + *pc);
        *pc += 2;
        targets[(*depth)++] = *pc + (a >> 8) + sizeof(unsigned int);
        if (targets[*depth - 1] > size)
            return -2;
        return 1;
    case 2:
        if (!has_bytes(size, *pc, 2) || *depth == 0)
            return -2;
        bit = script[*pc + 1];
        if (bit < 2 || !has_bytes(size, *pc, bit))
            return -2;
        --*depth;
        *pc += bit;
        return 1;
    case 3:
        if (!has_bytes(size, *pc, 2) || *depth == 0)
            return -2;
        --*depth;
        *pc += 2;
        return 1;
    case 4:
        if (!has_bytes(size, *pc, 6))
            return -2;
        a = read_u16(script + *pc);
        type = a >> 8;
        if (type == 10)
            return -1;
        b = read_u16(script + *pc + 2);
        c = read_u16(script + *pc + 4);
        word = flag_word(type, (b & 0x3ffu) >> 5);
        if (word == NULL)
            return -1;
        condition = (c >> 8) ^ ((*word & (0x80000000u >> (b & 31u))) != 0);
        *pc += 6;
        return condition != 0;
    case 5:
        if (!has_bytes(size, *pc, 6))
            return -2;
        a = read_u16(script + *pc);
        b = read_u16(script + *pc + 2);
        c = read_u16(script + *pc + 4);
        type = a >> 8;
        word = set_flag_word(type, (b & 0x3e0u) >> 5);
        if (word == NULL)
            return -1;
        bit = b & 31u;
        operation = c >> 8;
        switch (operation) {
        case 0: *word |= 0x80000000u >> bit; break;
        case 1: *word &= ~(0x80000000u >> bit); break;
        case 2: *word ^= 0x80000000u >> bit; break;
        case 3: *word |= 0xffffffffu >> bit; break;
        case 4: *word &= ~(0xffffffffu >> bit); break;
        case 5: *word ^= 0xffffffffu >> bit; break;
        default: return -1;
        }
        ++*writes;
        *pc += 6;
        return 1;
    case 6:
        if (!has_bytes(size, *pc, 4))
            return -2;
        if (compare_byte_value(script[*pc + 1], script[*pc + 2],
                               script[*pc + 3], &condition) != 0)
            return -1;
        *pc += 4;
        return condition != 0;
    case 7:
        if (!has_bytes(size, *pc, 6))
            return -2;
        if (compare_word_value(script[*pc + 2], script[*pc + 3],
                               read_u16(script + *pc + 4), &condition) != 0)
            return -1;
        *pc += 6;
        return condition != 0;
    case 8:
        if (!has_bytes(size, *pc, 4))
            return -2;
        if (store_byte_value(script[*pc + 1], script[*pc + 2]) != 0)
            return -1;
        ++*writes;
        *pc += 4;
        return 1;
    case 9:
        if (!has_bytes(size, *pc, 4))
            return -2;
        if (store_word_value(script[*pc + 1],
                             read_u16(script + *pc + 2)) != 0)
            return -1;
        ++*writes;
        *pc += 4;
        return 1;
    case 20: {
        size_t event_size;
        unsigned int task_level, event_id, script_index;
        const unsigned char *event_script;
        int task;

        if (!has_bytes(size, *pc, 4) || events == NULL)
            return -2;
        task_level = script[*pc + 2];
        event_id = script[*pc + 3];
        script_index = event_id + 2;
        event_script = recvxEventDataScript(events, script_index, &event_size);
        if (event_script == NULL || event_size == 0)
            return -2;
        task = recvxLegacyEventStart(task_level, event_id, script_index,
                                     event_size);
        if (task < 0)
            return -1;
        ++result->events_queued;
        result->last_event_task = (uint8_t)task;
        result->last_event_id = (uint8_t)event_id;
        result->last_event_bytes = (uint16_t)event_size;
        *pc += 4;
        return 1;
    }
    case 36:
        if (!has_bytes(size, *pc, 4) || script[*pc + 1] >= 4)
            return -2;
        recvxLegacyModelVisibilitySet(script[*pc + 1], script[*pc + 2],
                                      script[*pc + 3] != 0);
        *pc += 4;
        return 1;
    case 47:
        if (!has_bytes(size, *pc, 4))
            return -2;
        *pc += 4;
        return 1;
    default:
        return -1;
    }
}

RecvxLegacyScriptResult recvxLegacyScenarioRun(const unsigned char *script,
                                               size_t script_size,
                                               const RecvxEventData *events)
{
    RecvxLegacyScriptResult result = {0};
    size_t targets[RECVX_SCRIPT_MAX_DEPTH];
    size_t pc = 0;
    unsigned int depth = 0;
    int step;

    if (script == NULL || script_size == 0) {
        result.status = RECVX_LEGACY_SCRIPT_INVALID;
        return result;
    }
    while (result.instructions < RECVX_SCRIPT_MAX_INSTRUCTIONS) {
        result.stop_offset = (uint32_t)pc;
        step = execute_simple(script, script_size, events, &pc, targets, &depth,
                              &result.instructions, &result.writes,
                              &result,
                              &result.stop_opcode);
        if (step == -1) {
            result.status = RECVX_LEGACY_SCRIPT_UNSUPPORTED;
            return result;
        }
        if (step == -2) {
            result.status = RECVX_LEGACY_SCRIPT_INVALID;
            return result;
        }
        if (step == 2) {
            result.status = RECVX_LEGACY_SCRIPT_COMPLETE;
            return result;
        }
        if (step == 0) {
            if (depth == 0) {
                result.status = RECVX_LEGACY_SCRIPT_YIELDED;
                return result;
            }
            pc = targets[--depth];
        }
    }
    result.status = RECVX_LEGACY_SCRIPT_LIMIT;
    return result;
}
