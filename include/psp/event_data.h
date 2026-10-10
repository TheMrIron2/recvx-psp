#ifndef RECVX_PSP_EVENT_DATA_H
#define RECVX_PSP_EVENT_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct RecvxEventData {
    unsigned char *bytes;
    size_t size;
    uint32_t script_count;
} RecvxEventData;

/* Initialize with {0}; release owned bytes with recvxEventDataFree */
int recvxEventDataLoad(const void *room_bytes, size_t room_size,
                       RecvxEventData *events);
void recvxEventDataFree(RecvxEventData *events);
const unsigned char *recvxEventDataScript(const RecvxEventData *events,
                                          uint32_t index, size_t *size);

#endif
