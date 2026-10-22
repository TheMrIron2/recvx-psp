#include <assert.h>
#include <math.h>
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
static int light_calls,light_flag;
static BH_PWORK *light_actor;
void bhEne11_LightControl(BH_PWORK *work,int flag)
{++light_calls;light_flag=flag;light_actor=work;}
static jmp_buf fault_jump;
static int rejecting;
static char reason[192];

unsigned int (*bhScenarioJmpT[256])(void)={
    [0]=bhEnd,[52]=bhObjLinkSetPly,[56]=bhFrameCheck,[252]=bhWhile,[243]=bhEwhile2,[254]=bhEvtNext,[43]=bhMotionPauseSet,[60]=bhMaskSet,[62]=bhMaskStart,
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

static void test_frame_check(void)
{
    /* Exact fixed-point comparison for all actor selectors and nonzero half flags.
       Offset 17 deliberately exercises an unaligned 16-bit script operand. */
    unsigned char body[]={254,56,0,1,0,7,0,255,0};
    for(unsigned int kind=0;kind<3;++kind){
        for(unsigned int half=0;half<256;++half){
            body[2]=kind;body[4]=half;
            setup(body,sizeof(body));sys->obwp=(O_WRK *)arena;
            int *frame=kind==0?&plp->frm_no:kind==1?&ene[7].frm_no:&sys->obwp[1].frm_no;
            int threshold=(7<<16)+(half?32768:0);
            for(int delta=-1;delta<=1;++delta){
                *frame=threshold+delta;bhScePtr=section+17;
                assert(bhFrameCheck()==(unsigned int)(delta<0));
                assert(bhScePtr==section+23&&*frame==threshold+delta);
            }
        }
    }
    const unsigned char wait[]={252,8,56,0,0,1,7,0,254,243,255,0};
    setup(wait,sizeof(wait));plp->frm_no=(7<<16)+32767;
    tick();assert(recvxEventCoreTaskPc(0)==25&&bhEtask[0].loop==0);
    tick();assert(recvxEventCoreTaskPc(0)==25&&bhEtask[0].status);
    plp->frm_no++;tick();assert(!bhEtask[0].status&&bhEtask[0].loop==-1);
    assert(recvxEventCoreTaskPc(0)==28);
    unsigned char bad[]={56,0,1,0,7,0,255,0};
    for(unsigned int length=1;length<6;++length){
        setup(bad,length);core_fault(RECVX_EVENT_FAULT_TRUNCATED);
        assert(recvxEventCoreTaskPc(0)==16);
    }
    for(unsigned int variant=0;variant<7;++variant){
        bad[1]=variant==0?3:variant==1?0:variant<5?1:2;
        setup(bad,sizeof(bad));
        if(variant==1)plp=NULL;
        if(variant==2)rom->ene_n=1;
        if(variant==3)descriptors[1].wrk_no=128;
        if(variant==4)rom->enep=NULL;
        if(variant==6){sys->obwp=(O_WRK *)arena;section[18]=255;}
        core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
        assert(recvxEventCoreTaskPc(0)==16);plp=&player;
    }
    bad[1]=2;setup(bad,sizeof(bad));sys->obwp=(O_WRK *)(arena+sizeof(arena)-4);
    REJECT(bhEventScheduler2(),"object work");assert(recvxEventCoreTaskPc(0)==16);
}

static void test_player_object_link(void)
{
    unsigned char command[]={52,255,1,1,0,0,100,0,200,0,255,255,255,0};
    for(unsigned int signs=0;signs<256;++signs){
        setup(command,sizeof(command));sys->obwp=(O_WRK *)arena;
        plp->mlwP=plp->mdl;plp->mdl[0].obj_num=2;plp->mdl[0].owP=(O_WORK *)(arena+6000);
        sys->obwp[1].flg=0x12340001u;section[21]=signs;
        tick();O_WRK *object=&sys->obwp[1];
        assert(object->lkwkp==(unsigned char *)plp&&object->lkono==1&&object->flg==0x12340081u);
        assert(object->lox==((signs&1)?-1:1)&&object->loy==((signs&2)?-2:2));
        assert(fabsf(object->loz-((signs&4)?-655.35f:655.35f))<0.001f);
        assert(!bhEtask[0].status&&recvxEventCoreTaskPc(0)==30);
    }
    for(unsigned int mode=1;mode<256;++mode){
        setup(command,sizeof(command));sys->obwp=(O_WRK *)arena;section[20]=mode;section[19]=255;
        sys->obwp[1].flg=0x12340081u;tick();
        assert(sys->obwp[1].flg==0x12340001u&&sys->obwp[1].lkono==255);
    }
    for(unsigned int length=1;length<12;++length){
        setup(command,length);core_fault(RECVX_EVENT_FAULT_TRUNCATED);assert(recvxEventCoreTaskPc(0)==16);
    }
    for(unsigned int variant=0;variant<5;++variant){
        setup(command,sizeof(command));sys->obwp=(O_WRK *)arena;
        plp->mlwP=plp->mdl;plp->mdl[0].obj_num=2;plp->mdl[0].owP=(O_WORK *)(arena+6000);
        if(variant==0)sys->obwp=NULL;
        if(variant==1)section[18]=255;
        if(variant==2)section[19]=2;
        if(variant==3)plp->mlwP=NULL;
        if(variant==4)plp->mdl[0].owP=NULL;
        core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);assert(recvxEventCoreTaskPc(0)==16);
    }
    setup(command,sizeof(command));sys->obwp=(O_WRK *)arena;
    plp->mlwP=plp->mdl;plp->mdl[0].obj_num=2;plp->mdl[0].owP=(O_WORK *)(arena+sizeof(arena)-4);
    REJECT(bhEventScheduler2(),"pose array");assert(sys->obwp[1].lkwkp==NULL&&recvxEventCoreTaskPc(0)==16);
    /* Place the command at an odd address. Byte 1 is ignored padding. */
    unsigned char odd[15]={254};memcpy(odd+1,command,sizeof(command));
    setup(odd,sizeof(odd));sys->obwp=(O_WRK *)arena;section[21]=1;
    tick();tick();assert(sys->obwp[1].lox==1&&recvxEventCoreTaskPc(0)==31);
}

static void setup_task2(const unsigned char *body,size_t count)
{
    setup(body,count);
    memset(section,0,sizeof(section));
    for(unsigned int i=0;i<5;++i)u32(section+i*4,20+i*2);
    for(unsigned int i=20;i<28;i+=2)section[i]=255;
    memcpy(section+28,body,count);
    recvxEventCoreReset();assert(!recvxEventCoreBindSection(section,28+count));
    assert(!Event_exec(2,2));bhEtask[2].work=&ene[7];
}
static void test_sub_control(void)
{
    const unsigned int commands[]={143,146,147},lengths[]={6,8,8};
    for(unsigned int k=0;k<3;++k)for(unsigned int zombie=0;zombie<2;++zombie){
        unsigned char body[]={103,commands[k],2,3,4,5,6,0xff,254,255,0};
        unsigned int length=lengths[k];body[length]=254;body[length+1]=255;body[length+2]=0;
        setup_task2(body,length+3);
        BH_SCEWORK *task=&bhEtask[2];BH_PWORK *a=&ene[7];
        if(zombie){a->id=1;a->exp1=(unsigned char *)&ene[8];state.emtp[1]=&motion;}
        a->flg=a->stflg=UINT32_MAX;a->type=0xabcd;a->ct2=0x12345678;
        a->mnwP=NULL;a->mtn_md=1;a->mdflg=2;a->hokan_rate=3;
        task->mode0=7;task->mode1=9;task->mtn_md=13;task->mdflg=0x12345678;task->hokan_rate=31;
        tick();assert(!recvxEventCoreGetFault()->code&&recvxEventCoreTaskPc(2)==28+length+1);
        assert(a->mode0==2&&a->mode1==3&&a->mode2==4);
        assert(a->mode3==(k==0?0:k==1?6:5));
        assert(a->type==(k==2?0xab06:0xab05));
        if(k<2){
            assert(a->mnwP==&motion&&a->ct2==0x12345600&&!(a->flg&0x200000)&&!(a->stflg&0x10000));
            assert(!task->mode0&&!task->mode1&&a->mtn_md==13&&a->hokan_rate==31);
            assert(a->mdflg==(k==0?0x12345678u:2u));
            if(zombie)assert(ene[8].mode0==1&&ene[8].mnwP==&motion);
        }else{
            assert(a->mnwP==NULL&&a->ct2==0x12345678&&a->flg==UINT32_MAX&&a->stflg==UINT32_MAX);
            assert(task->mode0==7&&task->mode1==9&&a->mtn_md==1&&a->mdflg==2&&a->hokan_rate==3);
            if(zombie)assert(!ene[8].mode0&&!ene[8].mnwP);
        }
        tick();assert(!bhEtask[2].status&&recvxEventCoreTaskPc(2)==28+length+3);
        for(unsigned int truncated=1;truncated<length;++truncated){
            setup_task2(body,truncated);ene[7].mode0=7;
            core_fault(RECVX_EVENT_FAULT_TRUNCATED);
            assert(recvxEventCoreTaskPc(2)==28&&ene[7].mode0==7);
        }
    }
    for(unsigned int kind=0;kind<2;++kind){
        unsigned char body[]={103,kind?146:143,1,2,3,4,5,0,255,0};
        unsigned int length=kind?8:6;body[length]=255;body[length+1]=0;
        setup_task2(body,length+2);state.emtp[91]=NULL;
        core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);assert(!ene[7].mode0&&recvxEventCoreTaskPc(2)==28);
        setup_task2(body,length+2);ene[7].id=128;
        core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
        setup_task2(body,length+2);ene[7].id=1;state.emtp[1]=&motion;ene[7].exp1=NULL;
        core_fault(RECVX_EVENT_FAULT_BAD_OPERAND);
        setup_task2(body,length+2);ene[7].id=1;state.emtp[1]=&motion;ene[7].exp1=(unsigned char *)4;
        REJECT(bhEventScheduler2(),"companion");assert(!ene[7].mode0);
    }
    for(unsigned int flag=0;flag<256;++flag){
        unsigned char body[]={103,148,flag,0xff,255,0};
        setup_task2(body,sizeof(body));
        O_WRK *light=&eff[4];memcpy(ene[7].exp0+0x60,&light,sizeof(light));
        int before=light_calls;tick();assert(light_calls==before+1&&light_flag==(int)flag&&light_actor==&ene[7]);
        assert(recvxEventCoreTaskPc(2)==34);
    }
    const unsigned char light[]={103,148,0,0,255,0};
    for(unsigned int length=1;length<4;++length){setup_task2(light,length);core_fault(RECVX_EVENT_FAULT_TRUNCATED);}
    setup_task2(light,sizeof(light));ene[7].exp0=NULL;REJECT(bhEventScheduler2(),"light context");
    setup_task2(light,sizeof(light));REJECT(bhEventScheduler2(),"light object");
    const unsigned char unknown[]={103,142,255,0};
    setup_task2(unknown,sizeof(unknown));core_fault(RECVX_EVENT_FAULT_UNSUPPORTED);
    puts("PASS: opcode 103 task 2/script 2 commands 143/146/147 actor modes, motion handoff/interpolation and linked zombie synchronization; 148 light dispatch, exact PCs, yield/resume, truncated commands and invalid contexts");
}

int main(void)
{
    test_frame_check();
    test_player_object_link();
    test_sub_control();
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
    puts("PASS: player/object link enable/disable, signed offsets and joint bounds; frame checks for player/enemy/object, half frames, unaligned operands, wait/resume and bounds; original NPC handoff and pause/companion state, mask record selection/sentinel, facial flags, and bounded work/context/enemy/stream operands");
    return 0;
}
