#include <string.h>

#include "psp/engine_event_core.h"
#include "psp/engine_event_runtime.h"
#include "ps2/veronica/prog/main.h"

SYS_WORK recvxEngineEventSys;
SYS_WORK *sys = &recvxEngineEventSys;
ROM_WORK recvxEngineEventRoom;
ROM_WORK *rom = &recvxEngineEventRoom;
BH_PWORK recvxEngineEventPlayer;
BH_PWORK *plp = &recvxEngineEventPlayer;

void recvxEngineEventRuntimeReset(void)
{
    memset(&recvxEngineEventSys, 0, sizeof(recvxEngineEventSys));
    memset(&recvxEngineEventRoom, 0, sizeof(recvxEngineEventRoom));
    memset(&recvxEngineEventPlayer, 0, sizeof(recvxEngineEventPlayer));
    recvxEventCoreReset();
    sys = &recvxEngineEventSys;
    rom = &recvxEngineEventRoom;
    plp = &recvxEngineEventPlayer;
}
