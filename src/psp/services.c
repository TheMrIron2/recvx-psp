#include "psp/services.h"

#if defined(__PSP__)
#include <pspdebug.h>
#endif

static RecvxUnsupportedServiceRecord unsupported_services[
    RECVX_UNSUPPORTED_SERVICE_COUNT];

static const char *const service_names[RECVX_UNSUPPORTED_SERVICE_COUNT] = {
    "display mode", "movie", "audio", "memory card", "disc streaming",
    "vibration"
};

void recvxUnsupportedServiceRecord(enum RecvxUnsupportedServiceId service,
                                   const char *caller)
{
    RecvxUnsupportedServiceRecord *record;

    if ((unsigned int)service >= RECVX_UNSUPPORTED_SERVICE_COUNT)
        return;
    record = &unsupported_services[service];
    if (record->calls != UINT32_MAX)
        ++record->calls;
    record->last_caller = caller;
#if defined(__PSP__)
    pspDebugScreenPrintf("Unsupported %s from %s\n",
                         recvxUnsupportedServiceName(service),
                         caller != 0 ? caller : "unknown caller");
#endif
}

const RecvxUnsupportedServiceRecord *recvxUnsupportedServiceGet(
    enum RecvxUnsupportedServiceId service)
{
    if ((unsigned int)service >= RECVX_UNSUPPORTED_SERVICE_COUNT)
        return 0;
    return &unsupported_services[service];
}

const char *recvxUnsupportedServiceName(enum RecvxUnsupportedServiceId service)
{
    if ((unsigned int)service >= RECVX_UNSUPPORTED_SERVICE_COUNT)
        return "unknown service";
    return service_names[service];
}
