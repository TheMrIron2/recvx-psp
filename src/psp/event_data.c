#include <stdlib.h>
#include <string.h>

#include "psp/event_data.h"

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

void recvxEventDataFree(RecvxEventData *events)
{
    if (events == NULL)
        return;
    free(events->bytes);
    memset(events, 0, sizeof(*events));
}

int recvxEventDataLoad(const void *room_bytes, size_t room_size,
                       RecvxEventData *events)
{
    const unsigned char *room = room_bytes;
    uint32_t event_offset, event_end, table_size, count, i, previous = 0;
    RecvxEventData loaded = {0};

    if (room == NULL || events == NULL || room_size < 36)
        return -1;
    event_offset = read_u32(room + 28);
    event_end = read_u32(room + 32);
    if (event_offset < 36 || event_end <= event_offset || event_end > room_size)
        return -1;
    loaded.size = event_end - event_offset;
    if (loaded.size < 4)
        return -1;
    table_size = read_u32(room + event_offset);
    if (table_size < 8 || (table_size & 3) != 0 || table_size > loaded.size)
        return -1;
    count = table_size / 4;
    for (i = 0; i < count; ++i) {
        uint32_t offset = read_u32(room + event_offset + i * 4);
        if (offset < table_size || offset >= loaded.size ||
            (i != 0 && offset < previous))
            return -1;
        previous = offset;
    }
    loaded.bytes = malloc(loaded.size);
    if (loaded.bytes == NULL)
        return -2;
    memcpy(loaded.bytes, room + event_offset, loaded.size);
    loaded.script_count = count;
    recvxEventDataFree(events);
    *events = loaded;
    return 0;
}

const unsigned char *recvxEventDataScript(const RecvxEventData *events,
                                          uint32_t index, size_t *size)
{
    uint32_t offset, end;

    if (size != NULL)
        *size = 0;
    if (events == NULL || events->bytes == NULL ||
        index >= events->script_count)
        return NULL;
    offset = read_u32(events->bytes + index * 4);
    end = index + 1 < events->script_count ?
          read_u32(events->bytes + (index + 1) * 4) : (uint32_t)events->size;
    if (end < offset || end > events->size)
        return NULL;
    if (size != NULL)
        *size = end - offset;
    return events->bytes + offset;
}
