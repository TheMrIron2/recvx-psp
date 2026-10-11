#include <assert.h>
#include <stdint.h>

#include "psp/live_task_dispatch.h"

static volatile uint32_t active_mask;
static volatile uint32_t paused_mask;
static unsigned int trace[8];
static unsigned int trace_count;

static void task_enable_later(void)
{
    trace[trace_count++] = 0;
    active_mask |= UINT32_C(1) << 2;
}

static void task_disable_later(void)
{
    trace[trace_count++] = 1;
    active_mask &= ~(UINT32_C(1) << 2);
}

static void task_later(void)
{
    trace[trace_count++] = 2;
}

static void task_unpause_later(void)
{
    trace[trace_count++] = 3;
    paused_mask &= ~(UINT32_C(1) << 4);
}

static void task_paused(void)
{
    trace[trace_count++] = 4;
}

int main(void)
{
    RecvxLiveTaskProc table[5] = {
        task_enable_later, task_disable_later, task_later,
        task_unpause_later, task_paused
    };

    active_mask = UINT32_C(1) << 0;
    paused_mask = 0;
    assert(recvxRunLiveTaskSlots(&active_mask, &paused_mask, table, 5) == 0);
    assert(trace_count == 2 && trace[0] == 0 && trace[1] == 2);

    trace_count = 0;
    active_mask = (UINT32_C(1) << 0) | (UINT32_C(1) << 1) |
                  (UINT32_C(1) << 2);
    assert(recvxRunLiveTaskSlots(&active_mask, &paused_mask, table, 5) == 0);
    assert(trace_count == 2 && trace[0] == 0 && trace[1] == 1);

    trace_count = 0;
    active_mask = (UINT32_C(1) << 3) | (UINT32_C(1) << 4);
    paused_mask = UINT32_C(1) << 4;
    assert(recvxRunLiveTaskSlots(&active_mask, &paused_mask, table, 5) == 0);
    assert(trace_count == 2 && trace[0] == 3 && trace[1] == 4);

    paused_mask = 0;
    table[4] = 0;
    assert(recvxRunLiveTaskSlots(&active_mask, &paused_mask, table, 5) ==
           (UINT32_C(1) << 4));

    trace_count = 0;
    active_mask = (UINT32_C(1) << 0) | (UINT32_C(1) << 1) |
                  (UINT32_C(1) << 2);
    table[1] = 0;
    assert(recvxRunLiveTaskSlots(&active_mask, &paused_mask, table, 5) ==
           (UINT32_C(1) << 1));
    assert(trace_count == 1 && trace[0] == 0);
    assert(recvxRunLiveTaskSlots(&active_mask, &paused_mask, table, 33) ==
           UINT32_MAX);
    return 0;
}
