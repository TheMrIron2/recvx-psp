#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/message.h"
#include "ps2/veronica/prog/screen.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/system.h"
#include "ps2/veronica/prog/ps2_NaFog.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"
#include "psp/engine_pad_adapter.h"

static char unsupported_pad_dependency[48];

static void mark_pad_dependency(const char *name)
{
    if (unsupported_pad_dependency[0] == '\0')
        strncpy(unsupported_pad_dependency, name,
                sizeof(unsupported_pad_dependency) - 1);
}

int recvxEnginePadAdapterFaulted(void)
{
    return unsupported_pad_dependency[0] != '\0';
}

const char *recvxEnginePadAdapterFaultName(void)
{
    return unsupported_pad_dependency[0] != '\0' ?
           unsupported_pad_dependency : "none";
}

void recvxEnginePadAdapterResetFault(void)
{
    memset(unsupported_pad_dependency, 0, sizeof(unsupported_pad_dependency));
}

void njFogDisable(void) { mark_pad_dependency("njFogDisable"); }
void njSetBackColor(Uint32 a, Uint32 b, Uint32 c)
{
    (void)a; (void)b; (void)c;
    mark_pad_dependency("njSetBackColor");
}
void bhReturnTitle(void) { mark_pad_dependency("bhReturnTitle"); }
void bhSetScreenFade(unsigned int a, float b)
{
    (void)a; (void)b;
    mark_pad_dependency("bhSetScreenFade");
}
void RequestAllStopSoundEx(int a, int b, int c)
{
    (void)a; (void)b; (void)c;
    mark_pad_dependency("RequestAllStopSoundEx");
}
int bhDispMessageEx(float a, float b, float c, int d, int e, unsigned int f, int g)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
    mark_pad_dependency("bhDispMessageEx");
    return -1;
}
float bhMesLen(unsigned short *text)
{
    (void)text;
    mark_pad_dependency("bhMesLen");
    return 0.0f;
}
Float njSin(Angle angle)
{
    (void)angle;
    mark_pad_dependency("njSin");
    return 0.0f;
}
