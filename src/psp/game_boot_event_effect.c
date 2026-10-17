#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

/* event.c::bhEffModeSet and bhEffTypeSet operate on the room's effect ID
 * mapping. The shared interpreter checks the mapped work slot before calling.
 * Keep the original four-byte instruction and all authored byte values.
 */
unsigned int bhEffModeSet(void)
{
    unsigned int effect = bhScePtr[1];
    unsigned int mode = bhScePtr[2];
    bhScePtr += 4;
    eff[sys->efid[effect]].mode1 = mode;
    return 1;
}

unsigned int bhEffTypeSet(void)
{
    unsigned int effect = bhScePtr[1];
    unsigned int type = bhScePtr[2];
    bhScePtr += 4;
    eff[sys->efid[effect]].type = type;
    return 1;
}
