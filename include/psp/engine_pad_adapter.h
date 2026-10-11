#ifndef RECVX_PSP_ENGINE_PAD_ADAPTER_H
#define RECVX_PSP_ENGINE_PAD_ADAPTER_H

#include "psp/platform.h"

void recvxEnginePadAdapterReset(void);
int recvxEnginePadAdapterUpdate(const RecvxInputState *input);
int recvxEnginePadAdapterFaulted(void);
const char *recvxEnginePadAdapterFaultName(void);
void recvxEnginePadAdapterResetFault(void);

#endif
