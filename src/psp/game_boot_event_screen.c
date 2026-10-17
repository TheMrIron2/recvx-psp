#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_event_camera.h"
#include "psp/game_boot_event_screen.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

static void room_message(unsigned int index)
{
    uintptr_t start = sys ? (uintptr_t)sys->rdtp : 0;
    uintptr_t table = rom ? (uintptr_t)rom->mesp : 0;
    size_t bytes;
    uint32_t count, offset;
    if (!start || !table || (table & 3u) || sys->rdtsz <= 0 || table < start ||
        table - start > (size_t)sys->rdtsz)
        recvxGameBootUnsupported("original room message table is not loaded");
    bytes = (size_t)sys->rdtsz - (table - start);
    if (bytes < 4) recvxGameBootUnsupported("original room message table is truncated");
    memcpy(&count, (const void *)table, 4);
    if (count > bytes / 4 - 1 || index >= count)
        recvxGameBootUnsupported("original room message index exceeds table");
    memcpy(&offset, (const void *)(table + ((size_t)index + 1) * 4), 4);
    if ((offset & 1u) || offset < ((size_t)count + 1) * 4 || offset > bytes - 2)
        recvxGameBootUnsupported("original room message offset exceeds table");
}

/* The event interpreter checks instruction spans before these original leaves. */
void recvxGameBootEventScreenPrepare(unsigned int opcode)
{
    if (!sys || !bhScePtr) recvxGameBootUnsupported("invalid original screen event state");
    if (opcode == 31 && bhScePtr[1] == 0) room_message(bhScePtr[2]);
    if (opcode == 19 && bhScePtr[1] == 0)
        recvxGameBootEventCameraPrepare(bhScePtr[2], bhScePtr[3]);
}
