#include "psp/engine_original_fault.h"

static const char *engine_fault;

void recvxEngineOriginalFaultReset(void)
{
    engine_fault = 0;
}

void recvxEngineOriginalFaultSet(const char *message)
{
    if (engine_fault == 0)
        engine_fault = message != 0 ? message : "original engine fault";
}

const char *recvxEngineOriginalFault(void)
{
    return engine_fault;
}
