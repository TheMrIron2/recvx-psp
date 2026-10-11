#include <string.h>

#include "psp/engine_event_core.h"
#include "psp/engine_event_runtime.h"
#include "ps2/veronica/prog/main.h"

SYS_WORK recvxEngineEventSys;
SYS_WORK *sys = &recvxEngineEventSys;
ROM_WORK recvxEngineEventRoom;
ROM_WORK *rom = &recvxEngineEventRoom;
CAM_WORK cam;
BH_PWORK ply;
BH_PWORK *plp = &ply;
BH_PWORK ene[128];
O_WRK eff[512];
unsigned char *freemem;
unsigned char *njpmemp;
unsigned char *keepmem;
NJS_TEXMEMLIST tbuf[256];
HWS_WORK hwsp;
HWS_WORK *hws = &hwsp;
NJS_MATRIX *cmat;
int pd_port;
float BHD_ASPECT_X = 1.174f;
float BHD_ASPECT_Y = 1.0f;

void recvxEngineEventRuntimeReset(void)
{
    memset(&recvxEngineEventSys, 0, sizeof(recvxEngineEventSys));
    memset(&recvxEngineEventRoom, 0, sizeof(recvxEngineEventRoom));
    memset(&cam, 0, sizeof(cam));
    memset(&ply, 0, sizeof(ply));
    memset(ene, 0, sizeof(ene));
    memset(eff, 0, sizeof(eff));
    memset(&hwsp, 0, sizeof(hwsp));
    memset(tbuf, 0, sizeof(tbuf));
    recvxEventCoreReset();
    sys = &recvxEngineEventSys;
    rom = &recvxEngineEventRoom;
    plp = &ply;
    hws = &hwsp;
    freemem = NULL;
    njpmemp = NULL;
    keepmem = NULL;
    cmat = NULL;
    pd_port = -1;
}
