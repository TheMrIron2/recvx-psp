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
    RecvxEventDataView view;
    RecvxEventData loaded = {0};

    if (events == NULL || recvxEventDataInspect(room_bytes, room_size, &view) != 0)
        return -1;
    loaded.size = view.section_size;
    loaded.bytes = malloc(loaded.size);
    if (loaded.bytes == NULL)
        return -2;
    memcpy(loaded.bytes, room + view.section_offset, loaded.size);
    loaded.script_count = view.script_count;
    recvxEventDataFree(events);
    *events = loaded;
    return 0;
}

int recvxEventDataInspect(const void *room_bytes, size_t room_size,
                          RecvxEventDataView *view)
{
    const unsigned char *room = room_bytes;
    RecvxEventDataView parsed;
    uint32_t event_end, table_size, count, i, previous = 0;

    if (room == NULL || view == NULL || room_size < 36 || room_size > UINT32_MAX)
        return -1;
    parsed.section_offset = read_u32(room + 28);
    event_end = read_u32(room + 32);
    if (parsed.section_offset < 36 || event_end <= parsed.section_offset ||
        event_end > room_size)
        return -1;
    parsed.section_size = event_end - parsed.section_offset;
    if (parsed.section_size < 4)
        return -1;
    table_size = read_u32(room + parsed.section_offset);
    if (table_size < 8 || (table_size & 3) != 0 ||
        table_size > parsed.section_size)
        return -1;
    count = table_size / 4;
    for (i = 0; i < count; ++i) {
        uint32_t offset = read_u32(room + parsed.section_offset + i * 4);
        if (offset < table_size || offset >= parsed.section_size ||
            (i != 0 && offset < previous))
            return -1;
        previous = offset;
    }
    parsed.script_count = count;
    *view = parsed;
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

const unsigned char *recvxEventDataViewScript(const void *room_bytes,
                                              size_t room_size,
                                              const RecvxEventDataView *view,
                                              uint32_t index, size_t *size)
{
    const unsigned char *room = room_bytes;
    RecvxEventDataView checked;
    uint32_t offset, end;

    if (size != NULL)
        *size = 0;
    if (room == NULL || view == NULL || index >= view->script_count ||
        view->section_offset > room_size ||
        view->section_size > room_size - view->section_offset ||
        view->section_size < 4 ||
        (size_t)view->script_count > view->section_size / 4 ||
        recvxEventDataInspect(room, room_size, &checked) != 0 ||
        checked.section_offset != view->section_offset ||
        checked.section_size != view->section_size ||
        checked.script_count != view->script_count)
        return NULL;
    offset = read_u32(room + view->section_offset + index * 4);
    end = index + 1 < view->script_count ?
          read_u32(room + view->section_offset + (index + 1) * 4) :
          (uint32_t)view->section_size;
    if (offset > end || end > view->section_size)
        return NULL;
    if (size != NULL)
        *size = end - offset;
    return room + view->section_offset + offset;
}
