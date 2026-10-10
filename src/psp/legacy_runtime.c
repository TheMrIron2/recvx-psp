#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "psp/legacy_runtime.h"

#define RECVX_LEGACY_ARENA_SIZE (256u * 1024u)

SYS_WORK sysp;
SYS_WORK *sys = &sysp;
BH_PWORK ply;
BH_PWORK *plp = &ply;
unsigned char *freemem;
unsigned char *njpmemp;
unsigned char *keepmem;

static void *legacy_arena_allocation;
static unsigned char *legacy_arena;
static int legacy_init_complete;
static RecvxLegacyEventTask legacy_event_tasks[16];

extern void bhSysCallInit(void);

int recvxLegacyRuntimePrepare(void)
{
    uintptr_t aligned;

    if (legacy_arena != NULL)
        return 0;
    legacy_arena_allocation = malloc(RECVX_LEGACY_ARENA_SIZE + 63u);
    if (legacy_arena_allocation == NULL)
        return -1;
    aligned = ((uintptr_t)legacy_arena_allocation + 63u) & ~(uintptr_t)63u;
    legacy_arena = (unsigned char *)aligned;
    memset(&sysp, 0, sizeof(sysp));
    memset(&ply, 0, sizeof(ply));
    freemem = legacy_arena;
    njpmemp = legacy_arena + RECVX_LEGACY_ARENA_SIZE;
    keepmem = NULL;
    legacy_init_complete = 0;
    return 0;
}

int recvxLegacySystemInit(void)
{
    if (legacy_arena == NULL || legacy_init_complete)
        return -1;
    bhSysCallInit();
    legacy_init_complete = sys->tk_flg == 0x300002 &&
                           sys->typ_exp != NULL &&
                           sys->memp <= sys->endp;
    return legacy_init_complete ? 0 : -1;
}

void recvxLegacyRuntimeShutdown(void)
{
    free(legacy_arena_allocation);
    legacy_arena_allocation = NULL;
    legacy_arena = NULL;
    freemem = NULL;
    njpmemp = NULL;
    keepmem = NULL;
    memset(&sysp, 0, sizeof(sysp));
    memset(&ply, 0, sizeof(ply));
    memset(legacy_event_tasks, 0, sizeof(legacy_event_tasks));
    sys = &sysp;
    plp = &ply;
    legacy_init_complete = 0;
}

void recvxLegacyEventReset(void)
{
    memset(legacy_event_tasks, 0, sizeof(legacy_event_tasks));
}

int recvxLegacyEventStart(unsigned int task_level, unsigned int event_id,
                          unsigned int script_index, size_t script_bytes)
{
    unsigned int slot = task_level;

    if (slot >= 16) {
        for (slot = 0; slot < 15 && legacy_event_tasks[slot].active; ++slot)
            ;
    }
    if (slot >= 16 || script_index > UINT16_MAX || script_bytes > UINT16_MAX)
        return -1;
    legacy_event_tasks[slot].active = 1;
    legacy_event_tasks[slot].event_id = (uint8_t)event_id;
    legacy_event_tasks[slot].script_index = (uint16_t)script_index;
    legacy_event_tasks[slot].script_bytes = (uint16_t)script_bytes;
    return (int)slot;
}

const RecvxLegacyEventTask *recvxLegacyEventGet(unsigned int task_level)
{
    if (task_level >= 16 || !legacy_event_tasks[task_level].active)
        return NULL;
    return &legacy_event_tasks[task_level];
}

int recvxLegacyRuntimeReady(void)
{
    return legacy_init_complete;
}

uint32_t recvxLegacyTaskMask(void)
{
    return legacy_init_complete ? sys->tk_flg : 0;
}

uint32_t recvxLegacyPausedTaskMask(void)
{
    return legacy_init_complete ? sys->ts_flg : 0;
}

size_t recvxLegacyArenaUsed(void)
{
    if (legacy_arena == NULL || sys->memp < legacy_arena ||
        sys->memp > njpmemp)
        return 0;
    return (size_t)(sys->memp - legacy_arena);
}
