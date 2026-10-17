#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_event_core.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/face.h"

static SYS_WORK state;
static ROM_WORK room;
static BH_PWORK player;
SYS_WORK *sys=&state;
ROM_WORK *rom=&room;
BH_PWORK *plp=&player;
BH_PWORK ene[128];
O_WRK eff[512];
static ETTY_WORK descriptors[2];
static unsigned char arena[8192] __attribute__((aligned(64)));
unsigned char *freemem=arena;
static unsigned char section[128],animation[4+8+164+4+8+164+4];
static MN_WORK motion;
static jmp_buf fault_jump;
static int rejecting;
static char reason[192];

unsigned int (*bhScenarioJmpT[256])(void)={
    [0]=bhEnd,[43]=bhMotionPauseSet,[60]=bhMaskSet,[62]=bhMaskStart,
    [103]=bhSubCtr,[154]=bhFaceRep,[255]=bhEvtEnd
};
void recvxGameBootUnsupported(const char *message)
{
    if(!rejecting){fprintf(stderr,"unexpected actor event fault: %s\n",message);abort();}
    snprintf(reason,sizeof(reason),"%s",message);longjmp(fault_jump,1);
}
#define REJECT(call,text) do{rejecting=1;reason[0]=0; \
    if(!setjmp(fault_jump)){call;assert(!"expected actor event fault");} \
    rejecting=0;assert(strstr(reason,text));}while(0)
size_t recvxGameBootSourceRemaining(const void *pointer)
{
    uintptr_t p=(uintptr_t)pointer,a=(uintptr_t)animation,d=(uintptr_t)descriptors;
    if(p>=a&&p<a+sizeof(animation))return a+sizeof(animation)-p;
    if(p>=d&&p<d+sizeof(descriptors))return d+sizeof(descriptors)-p;
    return 0;
}
static void u32(unsigned char *p,uint32_t value)
{memcpy(p,&value,4);}
static uint32_t ctx(unsigned int index)
{uint32_t value;memcpy(&value,ene[7].exp0+index*4,4);return value;}
static void setup(const unsigned char *body,size_t count)
{
    memset(&state,0,sizeof(state));memset(&room,0,sizeof(room));
    memset(&player,0,sizeof(player));memset(ene,0,sizeof(ene));
    memset(arena,0,sizeof(arena));memset(section,0,sizeof(section));
    memset(animation,0,sizeof(animation));memset(descriptors,0,sizeof(descriptors));
    descriptors[1].wrk_no=7;room.enep=descriptors;room.ene_n=2;
    ene[7].id=91;ene[7].exp0=arena;ene[7].exp1=arena+2048;
    state.endp=arena+sizeof(arena);state.emtp[91]=&motion;state.mspp=animation;
    u32(animation,172);u32(animation+8,1);
    u32(animation+176,172);u32(animation+184,1);u32(animation+352,UINT32_MAX);
    u32(section,12);u32(section+4,14);u32(section+8,16);
    assert(count<=sizeof(section)-16);memcpy(section+16,body,count);
    recvxEventCoreReset();room.evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,16+count));
    assert(!Event_exec(0,0));bhEtask[0].work=&ene[7];state.sp_flg=0x10;
}
static void tick(void){assert(!bhEventScheduler2());}
static void core_fault(enum RecvxEventCoreFaultCode code)
{assert(bhEventScheduler2()==-1);assert(recvxEventCoreGetFault()->code==code);}

int main(void)
{
    const unsigned char pause[]={43,0,255,0};
    setup(pause,sizeof(pause));ene[7].id=1;ene[7].exp1=(unsigned char *)&ene[8];
    tick();assert(ene[7].mode3==4&&ene[8].mode3==4);
    unsigned char command[8]={43,1,255,0};
    setup(command,4);tick();assert(ene[7].mode3==1);
    setup(pause,sizeof(pause));ene[7].id=1;ene[7].exp1=NULL;
    core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);assert(!ene[7].mode3);
    setup(pause,sizeof(pause));ene[7].id=1;ene[7].exp1=(unsigned char *)4;
    REJECT(bhEventScheduler2(),"companion");assert(!ene[7].mode3);

    const unsigned char handoff[]={103,128,255,0};
    setup(handoff,sizeof(handoff));ene[7].flg=ene[7].stflg=UINT32_MAX;
    ene[7].ct2=0x12345678;bhEtask[0].mode0=bhEtask[0].mode1=7;
    tick();assert(ene[7].mnwP==&motion&&ene[7].mode0==1&&!ene[7].mode2);
    assert(ene[7].ct2==0x12345600&&!(ene[7].flg&0x200000)&&!(ene[7].stflg&0x10000));
    assert(!bhEtask[0].mode0&&!bhEtask[0].mode1);
    command[0]=103;command[1]=139;command[2]=255;command[3]=0;
    setup(command,4);bhEtask[0].work=NULL;bhEtask[0].mode0=bhEtask[0].mode1=7;
    tick();assert(!bhEtask[0].mode0&&!bhEtask[0].mode1);
    command[1]=144;setup(command,4);tick();assert(ene[7].mode0==1&&ene[7].mode3==3);
    command[1]=149;command[2]=1;command[3]=0;command[4]=255;command[5]=0;
    setup(command,6);tick();assert(ene[7].flg&0x10000);
    command[2]=0;setup(command,6);ene[7].flg=0x10000;tick();assert(!ene[7].flg);
    setup(handoff,sizeof(handoff));sys->emtp[91]=NULL;core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);

    const unsigned char mask[]={60,1,1,1,62,1,1,0,154,1,0,0,255,0};
    setup(mask,sizeof(mask));u32(ene[7].exp0,0x80);tick();
    assert(ctx(0)==0x8b&&ctx(2)==1&&ctx(5)==0);
    assert(ctx(266)==(uint32_t)(uintptr_t)(animation+180));
    assert(bhSetMask(&ene[7],2,7)==0&&ctx(5)==7); /* Original sentinel return. */
    const unsigned char disable[]={62,1,1,1,154,1,1,0,255,0};
    setup(disable,sizeof(disable));u32(ene[7].exp0,0xff);tick();assert(ctx(0)==0xfc);
    setup(mask,sizeof(mask));sys->mspp=NULL;tick();assert(ctx(0)==3); /* No animation table: original bhSetMask returns zero. */

    for(unsigned int i=0;i<4;++i){
        unsigned char malformed[]={60,1,1,1,255,0};
        setup(malformed,sizeof(malformed));
        if(i==0)room.ene_n=1;else if(i==1)descriptors[1].wrk_no=128;
        else if(i==2)ene[7].exp0=NULL;else room.enep=NULL;
        core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
    }
    setup(mask,sizeof(mask));ene[7].exp0=arena+sizeof(arena)-4;
    REJECT(bhEventScheduler2(),"context");
    setup(mask,sizeof(mask));u32(animation,UINT32_MAX-3);
    REJECT(bhEventScheduler2(),"record exceeds");
    setup(mask,sizeof(mask));u32(animation+184,2);
    REJECT(bhEventScheduler2(),"frame table");
    const unsigned int opcodes[]={43,60,62,103,154},sizes[]={2,4,4,2,4};
    for(unsigned int i=0;i<5;++i){
        memset(command,0,sizeof(command));command[0]=opcodes[i];
        setup(command,sizes[i]-1);core_fault(RECVX_EVENT_FAULT_TRUNCATED);
        assert(!ene[7].mode3&&!ctx(0));
    }
    puts("PASS: original NPC handoff and pause/companion state, mask record selection/sentinel, facial flags, and bounded work/context/enemy/stream operands");
    return 0;
}
