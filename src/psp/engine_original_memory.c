#include <stdint.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_memory.h"

static uintptr_t arena_start;
static uintptr_t arena_high_water;

void recvxEngineOriginalMemoryReset(void)
{
    arena_start = (uintptr_t)sys->memp;
    arena_high_water = arena_start;
}

size_t recvxEngineOriginalMemoryHighWater(void)
{
    return arena_high_water >= arena_start ? arena_high_water - arena_start : 0;
}

void recvxEngineOriginalMemoryObserve(void)
{
    uintptr_t current = (uintptr_t)sys->memp;
    uintptr_t limit = (uintptr_t)sys->endp;

    if (current < arena_start || current > limit) {
        recvxEngineOriginalFaultSet("original memory cursor escaped its arena");
        return;
    }
    if (current > arena_high_water)
        arena_high_water = current;
}

void npSetMemory(unsigned char *memory, unsigned int size, char value)
{
    if (size == 0)
        return;
    if (memory == NULL && size != 0) {
        recvxEngineOriginalFaultSet("original memory clear received a null range");
        return;
    }
    memset(memory, (unsigned char)value, size);
}

void npCopyMemory(unsigned char *destination, unsigned char *source,
                  unsigned int size)
{
    if ((destination == NULL || source == NULL) && size != 0) {
        recvxEngineOriginalFaultSet("original memory copy received a null range");
        return;
    }
    while (size-- != 0)
        *destination++ = *source++;
}

void npSetMemoryL(unsigned int *memory, unsigned int words, int value)
{
    if (words == 0)
        return;
    if (memory == NULL && words != 0) {
        recvxEngineOriginalFaultSet("original word clear received a null range");
        return;
    }
    while (words-- != 0)
        *memory++ = (unsigned int)value;
}

unsigned char *bhGetFreeMemory(int size, int alignment)
{
    uintptr_t current, aligned, limit;
    unsigned char *memory;

    (void)alignment;
    if (size < 0 || sys == NULL || sys->memp == NULL || sys->endp == NULL) {
        recvxEngineOriginalFaultSet("original arena allocation has invalid bounds");
        return NULL;
    }
    current = (uintptr_t)sys->memp;
    limit = (uintptr_t)sys->endp;
    aligned = (current + 63u) & ~(uintptr_t)63u;
    if (aligned > limit || (uintptr_t)size > limit - aligned) {
        recvxEngineOriginalFaultSet("original arena exhausted or pointer escaped");
        return NULL;
    }
    memory = (unsigned char *)aligned;
    npSetMemory(memory, (unsigned int)size, 0);
    sys->memp = memory + size;
    if ((uintptr_t)sys->memp > arena_high_water)
        arena_high_water = (uintptr_t)sys->memp;
    return memory;
}
