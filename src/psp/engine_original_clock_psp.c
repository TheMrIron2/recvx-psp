#include "ps2/veronica/prog/ps2_sg_sytmr.h"
#include "psp/platform.h"

Uint32 syTmrGetCount(void)
{
    return (Uint32)recvxPlatformTimeUs();
}
