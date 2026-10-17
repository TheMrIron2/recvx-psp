#include <math.h>
#include <stdint.h>
#include "psp/game_boot.h"
#include "psp/game_boot_event_sound.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

static int owned(const void *pointer, size_t bytes)
{
    uintptr_t start = (uintptr_t)freemem, end = (uintptr_t)sys->endp, p = (uintptr_t)pointer;
    return pointer && !(p & 3) &&
        ((freemem && end >= start && p >= start && p <= end && bytes <= end - p) ||
         recvxGameBootSourceRemaining(pointer) >= bytes);
}

void recvxGameBootEventSoundPrepare(unsigned int opcode)
{
    BH_PWORK *actor;
    unsigned int kind, index;
    uintptr_t model, models;
    if (!sys || !bhScePtr) recvxGameBootUnsupported("invalid event sound state");
    if (opcode == 25 || opcode == 26) {
        uintptr_t task = (uintptr_t)bhCetask, first = (uintptr_t)bhEtask;
        if (task < first || task - first >= sizeof(bhEtask) || (task - first) % sizeof(bhEtask[0]))
            recvxGameBootUnsupported("invalid event voice task");
    }
    if (opcode == 23 || opcode == 24) {
        if (bhScePtr[1] >= 5 || (opcode == 23 && bhScePtr[6] > 1))
            recvxGameBootUnsupported("invalid event sound slot or mode");
    }
    if ((opcode == 28 || opcode == 29) && bhScePtr[1] >= 3)
        recvxGameBootUnsupported("invalid event background sound slot");
    if (opcode != 23 && opcode != 25) return;
    kind = bhScePtr[opcode == 23 ? 2 : 1];
    index = bhScePtr[opcode == 23 ? 3 : 6];
    if (kind > 2 || (opcode == 25 && bhScePtr[4] > 2))
        recvxGameBootUnsupported("invalid event spatial sound mode");
    if (kind == 0) {
        actor = plp;
    } else if (kind == 1) {
        if (!rom || rom->ene_n < 0 || index >= (unsigned int)rom->ene_n ||
            !owned(rom->enep, (index + 1u) * sizeof(*rom->enep)) || rom->enep[index].wrk_no >= 128)
            recvxGameBootUnsupported("invalid event sound enemy index");
        actor = &ene[rom->enep[index].wrk_no];
    } else {
        if (index >= 39936u / sizeof(O_WRK) ||
            !owned(sys->obwp, (index + 1u) * sizeof(*sys->obwp)))
            recvxGameBootUnsupported("invalid event sound object index");
        actor = (BH_PWORK *)&sys->obwp[index];
    }
    if (!actor || !actor->mlwP) recvxGameBootUnsupported("missing event sound actor model");
    model = (uintptr_t)actor->mlwP; models = (uintptr_t)actor->mdl;
    if ((model < models || model - models >= sizeof(actor->mdl) ||
         (model - models) % sizeof(actor->mdl[0])) && !owned(actor->mlwP, sizeof(*actor->mlwP)))
        recvxGameBootUnsupported("invalid event sound model work");
    if (!owned(actor->mlwP->owP, sizeof(*actor->mlwP->owP)) ||
        !isfinite(actor->mlwP->owP->mtx[12]) || !isfinite(actor->mlwP->owP->mtx[13]) ||
        !isfinite(actor->mlwP->owP->mtx[14]))
        recvxGameBootUnsupported("invalid event sound actor position");
}
