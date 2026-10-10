#ifndef RECVX_PSP_SERVICES_H
#define RECVX_PSP_SERVICES_H

#include <stdint.h>

enum RecvxUnsupportedServiceId {
    RECVX_UNSUPPORTED_DISPLAY_MODE,
    RECVX_UNSUPPORTED_MOVIE,
    RECVX_UNSUPPORTED_AUDIO,
    RECVX_UNSUPPORTED_MEMORY_CARD,
    RECVX_UNSUPPORTED_DISC_STREAMING,
    RECVX_UNSUPPORTED_VIBRATION,
    RECVX_UNSUPPORTED_SERVICE_COUNT
};

typedef struct RecvxUnsupportedServiceRecord {
    uint32_t calls;
    const char *last_caller;
} RecvxUnsupportedServiceRecord;

void recvxUnsupportedServiceRecord(enum RecvxUnsupportedServiceId service,
                                   const char *caller);
const RecvxUnsupportedServiceRecord *recvxUnsupportedServiceGet(
    enum RecvxUnsupportedServiceId service);
const char *recvxUnsupportedServiceName(enum RecvxUnsupportedServiceId service);

#endif
