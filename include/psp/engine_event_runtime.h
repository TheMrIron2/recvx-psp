#ifndef RECVX_PSP_ENGINE_EVENT_RUNTIME_H
#define RECVX_PSP_ENGINE_EVENT_RUNTIME_H

#include "ps2/veronica/prog/types.h"

extern SYS_WORK recvxEngineEventSys;
extern ROM_WORK recvxEngineEventRoom;
extern BH_PWORK recvxEngineEventPlayer;

void recvxEngineEventRuntimeReset(void);

#endif
