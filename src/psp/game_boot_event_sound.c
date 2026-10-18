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
    unsigned int kind, index, joint=0;
    uintptr_t model, models;
    if (!sys || !bhScePtr) recvxGameBootUnsupported("invalid event sound state");
    if (opcode==118||opcode==119||opcode==127||opcode==134) {
        unsigned int type=bhScePtr[1],slot=bhScePtr[2];
        unsigned int limit=type==1||type==6?3:type==5?6:type==7?5:type==8?16:256;
        if(type>11||slot>=limit)recvxGameBootUnsupported("invalid event sound type or slot");
        if(opcode==134&&((type==1&&((bhScePtr[12]<2&&bhScePtr[8]>4)||bhScePtr[12]>3))||
           (type==3&&slot>1)))recvxGameBootUnsupported("invalid easy sound floor or weapon mode");
    }
    if(opcode==71&&bhScePtr[1]>=2)recvxGameBootUnsupported("invalid next-room background slot");
    if((opcode==146||opcode==148)&&bhScePtr[1]>=3)recvxGameBootUnsupported("invalid background sound slot");
    if((opcode==121||opcode==122)&&bhScePtr[1]>=6)recvxGameBootUnsupported("invalid enemy sound slot");
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
    if (opcode != 23 && opcode != 25 && opcode != 134 && opcode != 121) return;
    kind = opcode==134?bhScePtr[9]:opcode==121?1:bhScePtr[opcode == 23 ? 2 : 1];
    index = opcode==134?bhScePtr[10]:opcode==121?bhScePtr[2]:bhScePtr[opcode == 23 ? 3 : 6];
    joint = opcode==134?bhScePtr[11]:opcode==121?bhScePtr[3]:0;
    if (kind > (opcode==134?3u:2u) || (opcode == 25 && bhScePtr[4] > 2))
        recvxGameBootUnsupported("invalid event spatial sound mode");
    if (kind == 0) {
        actor = plp;
    } else if (kind == 1) {
        if (!rom || rom->ene_n < 0 || index >= (unsigned int)rom->ene_n ||
            !owned(rom->enep, (index + 1u) * sizeof(*rom->enep)) || rom->enep[index].wrk_no >= 128)
            recvxGameBootUnsupported("invalid event sound enemy index");
        actor = &ene[rom->enep[index].wrk_no];
    } else {
        O_WRK *base=kind==3?sys->itwp:sys->obwp;
        if (index >= 39936u / sizeof(O_WRK) ||
            !owned(base, (index + 1u) * sizeof(*base)))
            recvxGameBootUnsupported("invalid event sound object index");
        actor = (BH_PWORK *)&base[index];
    }
    if (!actor || !actor->mlwP) recvxGameBootUnsupported("missing event sound actor model");
    model = (uintptr_t)actor->mlwP; models = (uintptr_t)actor->mdl;
    if ((model < models || model - models >= sizeof(actor->mdl) ||
         (model - models) % sizeof(actor->mdl[0])) && !owned(actor->mlwP, sizeof(*actor->mlwP)))
        recvxGameBootUnsupported("invalid event sound model work");
    if ((opcode==134||opcode==121)&&joint>=actor->mlwP->obj_num)
        recvxGameBootUnsupported("invalid event sound joint index");
    const float *position;
    if(opcode==121){
        if(!owned(actor->mlwP->objP,(joint+1u)*sizeof(*actor->mlwP->objP)))
            recvxGameBootUnsupported("invalid enemy sound joint position");
        position=actor->mlwP->objP[joint].pos;
    }else{
        if(!owned(actor->mlwP->owP,(joint+1u)*sizeof(*actor->mlwP->owP)))
            recvxGameBootUnsupported("invalid event sound actor position");
        position=&actor->mlwP->owP[joint].mtx[12];
    }
    if(!isfinite(position[0])||!isfinite(position[1])||!isfinite(position[2]))
        recvxGameBootUnsupported("invalid event sound actor position");
}
