/* Real Common_controll extraction plus production bounded event scheduler. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_event_core.h"

static SYS_WORK work;
static ROM_WORK room;
static BH_PWORK actor;
SYS_WORK *sys = &work;
ROM_WORK *rom = &room;
BH_PWORK *plp = &actor;
BH_PWORK ene[128];
static unsigned char section[256];
unsigned int bhCommonCtr(void) { ++bhScePtr; Common_controll(); return 1; }
unsigned int (*bhScenarioJmpT[256])(void) = {
    [0] = bhEnd, [105] = bhCommonCtr, [248] = bhSleep, [249] = bhSleeping,
    [250] = bhFor, [251] = bhNext, [254] = bhEvtNext, [255] = bhEvtEnd
};
/* These unrelated common commands must never run in the fixture. */
char bhCheckFloorNum(float height) { (void)height; return 1; }
int bhSetMotion(BH_PWORK *p, int a, int b, void *d)
{ assert(p==&actor); (void)a; (void)b; (void)d; return 0; }
void bhCalcModel(BH_PWORK *p) { assert(p==&actor); }
void bhCalcLinkModel(BH_PWORK *p) { assert(p==&actor); }
static void setup(const unsigned char *bytes, size_t count)
{
    memset(&work, 0, sizeof(work)); memset(&room, 0, sizeof(room));
    memset(&actor, 0, sizeof(actor)); memset(section, 0, sizeof(section));
    section[0] = 12; section[4] = 14; section[8] = 16;
    memcpy(section + 16, bytes, count);
    recvxEventCoreReset(); room.evtp = (EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section, count + 16));
    assert(!Event_exec(0, 0)); bhEtask[0].work = &actor;
    sys->sp_flg = 0x10;
}
static void tick(void)
{ assert(!bhEventScheduler2()); assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE); }
static void bad(const unsigned char *bytes, size_t count, enum RecvxEventCoreFaultCode code)
{
    setup(bytes, count); assert(bhEventScheduler2() == -1);
    assert(recvxEventCoreGetFault()->code == code);
}
static const unsigned char common_sizes[256]={
    [0]=2,[2]=3,[3]=3,[4]=3,[5]=6,[6]=6,[7]=8,[8]=8,[9]=6,[10]=6,[11]=6,[12]=6,[13]=6,
    [14]=4,[15]=6,[16]=6,[17]=3,[18]=3,[19]=3,[21]=4,[22]=6,[23]=4,[24]=8,[25]=8,
    [26]=10,[27]=8,[28]=3,[29]=3,[30]=3,[31]=3,[32]=3,[33]=3,[34]=10,[35]=8,
    [36]=2,[38]=10,[40]=4,[41]=4,[42]=4,[43]=5,[44]=5,[45]=5,[46]=4,[47]=2,
    [48]=4,[49]=4,[50]=4,[51]=6,[52]=4,[53]=4,[132]=6,[140]=8,[141]=6
};
static NJS_CNK_OBJECT nodes[2];
static O_WORK worlds[2];
static ETTY_WORK descriptor;
static POS position;
static MN_WORK motion_work;
static void common_context(void)
{
    memset(nodes,0,sizeof(nodes));memset(worlds,0,sizeof(worlds));
    memset(&position,0,sizeof(position));memset(&descriptor,0,sizeof(descriptor));
    actor.mlwP=&actor.mdl[0];actor.mdl[0].objP=nodes;actor.mdl[0].owP=worlds;actor.mdl[0].obj_num=2;
    bhEtask[0].cnt3=2;bhEtask[0].cnt2=1;
    sys->rmthp=&motion_work;sys->plmthp=(void *)&motion_work;sys->emtp[0]=&motion_work;
    room.enep=&descriptor;room.ene_n=1;descriptor.wrk_no=7;
    ene[7].mlwP=&actor.mdl[0];room.posp=&position;room.pos_n=1;
}
static void test_all_common_extents(void)
{
    unsigned char command[16];unsigned int count=0;
    for(unsigned int sub=0;sub<256;++sub){
        unsigned int size=common_sizes[sub];if(!size)continue;++count;
        memset(command,0,sizeof(command));command[0]=105;command[1]=sub;
        if(sub==24||sub==25||sub==34||sub==35){command[2]=1;command[6]=1;}
        command[size]=255;
        setup(command,size+2);common_context();tick();
        assert(!bhEtask[0].status&&recvxEventCoreTaskPc(0)==18+size);
        for(unsigned int length=1;length<size;++length){
            setup(command,length);common_context();
            assert(bhEventScheduler2()==-1);
            assert(recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_TRUNCATED);
            assert(recvxEventCoreTaskPc(0)==16);
        }
    }
    assert(count==53);
    /* Every animation selector and its optional explicit starting frame.
       Callback selector 1 keeps these checks focused on animation setup. */
    for(unsigned int sub=24;sub<=35;++sub){
        if(sub!=24&&sub!=25&&sub!=34&&sub!=35)continue;
        unsigned int modes=sub==25||sub==35?2:4;
        for(unsigned int mode=0;mode<modes;++mode){
            unsigned int size=sub==34?10:sub==35&&mode==1?9:8;
            memset(command,0,sizeof(command));command[0]=105;command[1]=sub;
            command[2]=1;command[3]=mode;command[4]=3;command[6]=1;
            if(sub==34){command[8]=0xff;command[9]=0xff;}
            command[size]=255;
            setup(command,size+2);common_context();tick();
            assert(!bhEtask[0].status&&recvxEventCoreTaskPc(0)==18+size&&actor.mtn_add==65536);
            if(sub==34)assert(actor.frm_no==-65536);
            for(unsigned int length=4;length<size;++length){
                setup(command,length);common_context();
                assert(bhEventScheduler2()==-1&&recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_TRUNCATED);
                assert(recvxEventCoreTaskPc(0)==16&&!actor.mtn_add);
            }
        }
    }
    /* Odd halfword address, signed movement and high-bit frame packing. */
    const unsigned char frame[]={254,105,40,0xff,0xff,254,255,0};
    setup(frame,sizeof(frame));actor.id=1;actor.exp1=(unsigned char *)&ene[8];tick();tick();
    assert(actor.frm_no==-65536&&ene[8].frm_no==-65536);
    const unsigned char blend[]={105,53,0,255,255,0};
    setup(blend,sizeof(blend));actor.id=1;actor.exp1=(unsigned char *)&ene[8];
    actor.mtn_md=ene[8].mtn_md=0x100;tick();
    assert(actor.mtn_md==0x128&&ene[8].mtn_md==0x128);
    setup(blend,sizeof(blend));actor.id=1;actor.exp1=NULL;
    assert(bhEventScheduler2()==-1&&recvxEventCoreTaskPc(0)==16);
    assert(recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_BAD_OPERAND);
    const unsigned char interpolate[]={105,26,0,0,100,0,200,0,44,1,
        105,26,1,0,244,1,88,2,188,2,105,30,1,255,0};
    setup(interpolate,sizeof(interpolate));common_context();tick();
    assert(actor.px==3&&actor.py==4&&actor.pz==5);
    const unsigned char node_move[]={105,8,100,0,200,0,44,1,255,0};
    setup(node_move,sizeof(node_move));common_context();bhEtask[0].model_cno=1;tick();
    assert(nodes[1].pos[0]==1&&nodes[1].pos[1]==2&&nodes[1].pos[2]==3&&actor.px==0);
    setup(node_move,sizeof(node_move));common_context();bhEtask[0].model_cno=2;
    assert(bhEventScheduler2()==-1&&recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_BAD_OPERAND);
}
int main(void)
{
    test_all_common_extents();
    const unsigned int flags[4]={0xfffffee7u,0xffffffffu,0xffffffe7u,0xffffffffu};
    for(unsigned int mode=0;mode<4;++mode){
        unsigned char command[]={105,23,(unsigned char)mode,0,255,0};
        setup(command,sizeof(command));actor.flg=0xffffffffu;
        tick();assert(actor.flg==flags[mode]);
    }
    const unsigned char reset_root[]={105,36,255,0};
    NJS_CNK_OBJECT roots[2];ML_WORK model;
    setup(reset_root,sizeof(reset_root));memset(roots,0,sizeof(roots));memset(&model,0,sizeof(model));
    roots[0].pos[0]=3;roots[0].pos[1]=-8;roots[0].pos[2]=99;roots[0].ang[1]=1234;
    roots[1].pos[0]=17;model.objP=roots;model.obj_num=2;actor.mlwP=&model;
    tick();assert(!roots[0].pos[0]&&!roots[0].pos[1]&&!roots[0].pos[2]);
    assert(roots[0].ang[1]==1234&&roots[1].pos[0]==17&&actor.mlwP==&model);
    setup(reset_root,sizeof(reset_root));
    assert(bhEventScheduler2()==-1&&recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(reset_root,sizeof(reset_root));actor.mlwP=&model;model.objP=NULL;
    assert(bhEventScheduler2()==-1&&recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(reset_root,sizeof(reset_root));actor.mlwP=&model;model.objP=roots;model.obj_num=0;
    assert(bhEventScheduler2()==-1&&recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(reset_root,sizeof(reset_root));actor.mlwP=&model;model.obj_num=129;
    assert(bhEventScheduler2()==-1&&recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_BAD_OPERAND);
    setup(reset_root,sizeof(reset_root));bhEtask[0].work=NULL;
    assert(bhEventScheduler2()==-1&&recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_BAD_TASK);
    const unsigned char invalid_flags[]={105,23,4,0,255,0};
    const unsigned char short_flags[]={105,23,0};
    bad(invalid_flags,sizeof(invalid_flags),RECVX_EVENT_FAULT_BAD_OPERAND);
    bad(short_flags,sizeof(short_flags),RECVX_EVENT_FAULT_TRUNCATED);
    const unsigned char motion[] = {
        105,5,10,12,255,0, 105,12,0,1,1,0,
        250,0,3,0, 105,2,0,254, 251,0,255,0
    };
    setup(motion, sizeof(motion));
    tick(); assert(fabsf(actor.px - 1) < .0001f && fabsf(actor.py + 1.2f) < .0001f);
    assert(fabsf(actor.pz + 25.5f) < .0001f && bhEtask[0].cnt3 == 3 && bhEtask[0].cnt2 == 3);
    assert(recvxEventCoreTaskPc(0) == 36 && bhEtask[0].cnt[0] == 3);
    tick(); assert(fabsf(actor.px - 2) < .0001f && bhEtask[0].cnt2 == 2);
    tick(); assert(fabsf(actor.px - 3) < .0001f && bhEtask[0].cnt2 == 1);
    tick(); assert(!bhEtask[0].status && bhEtask[0].loop == -1 && actor.px == 3);

    const unsigned char nested[] = {
        105,5,10,0,0,0,250,0,2,0,250,0,3,0,105,2,0,251,0,251,0,255,0
    };
    setup(nested, sizeof(nested)); tick();
    assert(actor.px == 6 && bhEtask[0].loop == -1 && !bhEtask[0].status);
    const unsigned char four[] = {250,0,1,0,250,0,1,0,250,0,1,0,250,0,1,0,251,0,251,0,251,0,251,0,255,0};
    setup(four, sizeof(four)); tick(); assert(bhEtask[0].loop == -1);
    const unsigned char overflow[] = {250,0,1,0,250,0,1,0,250,0,1,0,250,0,1,0,250,0,1,0,255,0};
    bad(overflow, sizeof(overflow), RECVX_EVENT_FAULT_BAD_OPERAND);
    assert(bhEtask[0].loop == 3 && recvxEventCoreGetFault()->section_pc == 32);
    const unsigned char zero[] = {250,0,0,0,255,0};
    const unsigned char negative[] = {250,0,0,128,255,0};
    const unsigned char next[] = {251,0,255,0};
    const unsigned char short_for[] = {250,0,1};
    const unsigned char short_next[] = {251};
    const unsigned char short_add[] = {105,5,10};
    const unsigned char short_move[] = {105,2};
    bad(zero,sizeof(zero),RECVX_EVENT_FAULT_BAD_OPERAND);
    bad(negative,sizeof(negative),RECVX_EVENT_FAULT_BAD_OPERAND);
    bad(next,sizeof(next),RECVX_EVENT_FAULT_BAD_OPERAND);
    bad(short_for,sizeof(short_for),RECVX_EVENT_FAULT_TRUNCATED);
    bad(short_next,sizeof(short_next),RECVX_EVENT_FAULT_TRUNCATED);
    bad(short_add,sizeof(short_add),RECVX_EVENT_FAULT_TRUNCATED);
    bad(short_move,sizeof(short_move),RECVX_EVENT_FAULT_TRUNCATED);
    const unsigned char spin[] = {250,0,255,127,251,0,255,0};
    bad(spin,sizeof(spin),RECVX_EVENT_FAULT_WATCHDOG);
    const unsigned char sleep_next[] = {248,249,1,0,251,0,255,0};
    setup(sleep_next,sizeof(sleep_next)); tick();
    assert(bhEventScheduler2() == -1 && recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND);
    const unsigned char live[] = {250,0,255,127,254,251,0,255,0};
    setup(live,sizeof(live)); tick(); tick();
    assert(bhEtask[0].cnt[0] == 32766 && recvxEventCoreTaskPc(0) == 21);
    /* A saved loop body cannot escape its owning script. */
    bhEtask[0].lstack[0] = section + 12;
    assert(bhEventScheduler2() == -1 && recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_BAD_OPERAND);
    puts("PASS all 53 original CommonCtr subcommands, exact extents/truncations, companion frames/flags, joint positions, interpolation; original CommonCtr movement/signs, authored per-frame counted loops, nested stacks, operand bounds and liveness");
    return 0;
}
