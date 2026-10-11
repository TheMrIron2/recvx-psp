#ifndef RECVX_PSP_ENGINE_ORIGINAL_FAULT_H
#define RECVX_PSP_ENGINE_ORIGINAL_FAULT_H

void recvxEngineOriginalFaultReset(void);
void recvxEngineOriginalFaultSet(const char *message);
const char *recvxEngineOriginalFault(void);

#endif
