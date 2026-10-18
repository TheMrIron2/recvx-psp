#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_event_actor.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/face.h"

static int arena(const void *pointer,size_t size)
{
    uintptr_t first=(uintptr_t)freemem,end=sys?(uintptr_t)sys->endp:0,p=(uintptr_t)pointer;
    return pointer&&freemem&&!(p&3u)&&end>=first&&p>=first&&p<=end&&size<=end-p;
}
static int member(const void *pointer,const void *base,size_t count,size_t stride)
{
    uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)base;
    return pointer&&base&&p>=b&&p-b<count*stride&&!((p-b)%stride);
}
static int actor(const BH_PWORK *work)
{
    return work&&(work==plp||member(work,ene,128,sizeof(*ene))||
        member(work,eff,512,sizeof(*eff))||
        (member(work,sys->obwp,39936u/sizeof(O_WRK),sizeof(O_WRK))&&arena(work,sizeof(O_WRK)))||
        (member(work,sys->itwp,39936u/sizeof(O_WRK),sizeof(O_WRK))&&arena(work,sizeof(O_WRK))));
}
static BH_PWORK *enemy(unsigned int index)
{
    if(!sys||!rom||rom->ene_n<0||index>=(unsigned int)rom->ene_n||!rom->enep||
        recvxGameBootSourceRemaining(&rom->enep[index])<sizeof(*rom->enep)||rom->enep[index].wrk_no>=128)
        recvxGameBootUnsupported("invalid facial event enemy record");
    return &ene[rom->enep[index].wrk_no];
}
static void context(BH_PWORK *work)
{
    if(!sys||!actor(work)||!arena(work->exp0,1092))
        recvxGameBootUnsupported("invalid facial event context");
}
void recvxGameBootEventActorPrepare(unsigned int opcode,const unsigned char *command)
{
    if(!sys||!command)recvxGameBootUnsupported("missing actor event state");
    if(opcode==56){
        if(command[1]==1)(void)enemy(command[2]);
        if(command[1]==2&&!arena(&sys->obwp[command[2]],sizeof(O_WRK)))
            recvxGameBootUnsupported("invalid frame check object work");
    }else if(opcode==103||opcode==43){
        if(opcode==103&&(command[1]==0||command[1]==139))return;
        if(!bhCetask||!actor(bhCetask->work))
            recvxGameBootUnsupported("invalid actor event work");
        BH_PWORK *work=bhCetask->work;
        if(opcode==43&&work->id==1&&!actor((BH_PWORK *)work->exp1)&&!arena(work->exp1,sizeof(BH_PWORK)))
            recvxGameBootUnsupported("invalid motion pause companion");
        if(opcode==103&&command[1]==128&&(work->id>=128||!sys->emtp[work->id]))
            recvxGameBootUnsupported("actor event has no loaded motion table");
    }else context(enemy(opcode==154?command[1]:command[2]));
}

static uint32_t word(const unsigned char *p)
{return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void store(unsigned char *base,unsigned int index,uint32_t value)
{memcpy(base+index*4u,&value,4);}

int bhSetMask(BH_PWORK *work,int number,int frame)
{
    context(work);
    if(number<0||number>255||frame<0)recvxGameBootUnsupported("invalid facial animation selection");
    store(work->exp0,5,(uint32_t)frame);
    const unsigned char *data=sys->mspp;
    if(!data||!work->exp1)return 0;
    for(int index=0;index<=number;++index){
        size_t remaining=recvxGameBootSourceRemaining(data);
        if(remaining<4)recvxGameBootUnsupported("facial animation header exceeds room data");
        uint32_t size=word(data);
        if(size==UINT32_MAX)return 0;
        if((size&3u)||size>remaining-4)
            recvxGameBootUnsupported("facial animation record exceeds room data");
        data+=4;
        store(work->exp0,266,(uint32_t)(uintptr_t)data);
        if(index==number){
            if(size<8||!word(data+4)||word(data+4)>(size-8)/sizeof(PARAM_WORK))
                recvxGameBootUnsupported("invalid facial animation frame table");
            break;
        }
        data+=size;
    }
    store(work->exp0,2,(uint32_t)number);
    uint32_t flags;memcpy(&flags,work->exp0,4);store(work->exp0,0,flags|8u);
    return 1;
}
