#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp/engine_event_core.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

static SYS_WORK system_work;
static ROM_WORK room_work;
static BH_PWORK player;
SYS_WORK *sys=&system_work;
ROM_WORK *rom=&room_work;
BH_PWORK *plp=&player;
BH_PWORK ene[128];
static unsigned char section[256];
static unsigned int movie_queries, frame_queries;
static int movie_active, frame_condition;

int CheckPlayEndMovie(void) { ++movie_queries; return movie_active; }
unsigned int bhFrameCheck(void) { ++frame_queries; bhScePtr+=6; return frame_condition; }
unsigned int bhCk(void)
{
    /* This fixture consumes an already validated six-byte flag condition. */
    unsigned int bit=bhScePtr[2]&31u;
    unsigned int result=bhScePtr[5]^!!(sys->cb_flg&(UINT32_C(0x80000000)>>bit));
    bhScePtr+=6;return result;
}
unsigned int (*bhScenarioJmpT[256])(void) = {
    [0]=bhEnd,[4]=bhCk,[5]=bhSet,[56]=bhFrameCheck,
    [94]=bhMovieStart,[95]=bhMovieStop,[155]=bhMovieCk,
    [243]=bhEwhile2,[248]=bhSleep,[249]=bhSleeping,[250]=bhFor,[251]=bhNext,
    [252]=bhWhile,[253]=bhEwhile,[254]=bhEvtNext,[255]=bhEvtEnd
};

static void u32(unsigned char *p,unsigned int value)
{p[0]=value;p[1]=value>>8;p[2]=value>>16;p[3]=value>>24;}
static void setup(const unsigned char *body,size_t size)
{
    memset(&system_work,0,sizeof(system_work));
    memset(&room_work,0,sizeof(room_work));memset(&player,0,sizeof(player));
    sys=&system_work;rom=&room_work;plp=&player;
    memset(section,0,sizeof(section));
    u32(section,16);u32(section+4,20);u32(section+8,24);u32(section+12,24+(unsigned int)size);
    assert(size<sizeof(section)-25);memcpy(section+24,body,size);
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,25+size));
    assert(!Event_exec(2,0));
    sys->sp_flg=0x10;movie_queries=frame_queries=0;
    movie_active=1;frame_condition=1;
}
static void fault(enum RecvxEventCoreFaultCode code,unsigned int opcode)
{
    assert(bhEventScheduler2()!=0);
    assert(recvxEventCoreGetFault()->code==code);
    assert(recvxEventCoreGetFault()->opcode==opcode);
}

int main(void)
{
    /* Retail script31: while(MovieCk) { EvtNext; } Ewhile2; followed by work. */
    static const unsigned char retail[]={252,8,155,0,0,0,0,0,254,243,
        5,4,31,0,0,0,255,0};
    unsigned char body[128];
    setup(retail,sizeof(retail));
    assert(!bhEventScheduler2()&&bhEtask[2].loop==0&&recvxEventCoreTaskPc(2)==33&&!movie_queries);
    assert(bhEtask[2].lcondition[0]==section+26&&bhEtask[2].lstack[0]==section+32);
    assert(!bhEventScheduler2()&&movie_queries==1&&recvxEventCoreTaskPc(2)==33&&bhEtask[2].status);
    movie_active=0;
    assert(!bhEventScheduler2()&&movie_queries==2&&bhEtask[2].loop==-1&&!bhEtask[2].status);
    assert(sys->rm_flg==1); /* 243 continues to the following command immediately. */

    memcpy(body,retail,sizeof(retail));body[9]=253;
    setup(body,sizeof(retail));assert(!bhEventScheduler2());movie_active=0;
    assert(!bhEventScheduler2()&&!sys->rm_flg&&bhEtask[2].status&&recvxEventCoreTaskPc(2)==34);
    assert(!bhEventScheduler2()&&sys->rm_flg==1&&!bhEtask[2].status); /* 253 yields on false. */

    memcpy(body,retail,sizeof(retail));body[2]=56;
    setup(body,sizeof(retail));assert(!bhEventScheduler2());frame_condition=0;
    assert(!bhEventScheduler2()&&frame_queries==1&&!movie_queries&&sys->rm_flg==1);

    /* Shipped task16 waits on inverted callback flag18, using the same while machinery. */
    memcpy(body,retail,sizeof(retail));body[2]=4;body[3]=10;body[4]=18;body[7]=1;
    setup(body,sizeof(retail));assert(!bhEventScheduler2());
    assert(!bhEventScheduler2()&&bhEtask[2].status&&!sys->rm_flg);
    sys->cb_flg=UINT32_C(0x80000000)>>18;
    assert(!bhEventScheduler2()&&!bhEtask[2].status&&sys->rm_flg==1);
    body[4]=32;setup(body,sizeof(retail));
    fault(RECVX_EVENT_FAULT_BAD_OPERAND,4); /* Callback flag word outside its one-word bank. */
    assert(bhEtask[2].loop==-1);

    /* Nested counted, while, and sleep frames share all four original slots. */
    static const unsigned char nested[]={250,0,2,0,252,8,155,0,0,0,0,0,
        248,249,2,0,243,251,0,255,0};
    setup(nested,sizeof(nested));assert(!bhEventScheduler2()&&bhEtask[2].loop==2);
    movie_active=0;assert(!bhEventScheduler2()&&bhEtask[2].loop==1);
    assert(!bhEventScheduler2()&&bhEtask[2].loop==2); /* Counted loop repeats, pushing fresh while/sleep frames. */
    assert(!bhEventScheduler2());assert(!bhEventScheduler2()&&!bhEtask[2].status);
    assert(movie_queries==2);

    /* Four nested while frames are valid; a fifth is rejected before writing. */
    unsigned int length=0;
    for(unsigned int i=0;i<4;++i){memcpy(body+length,retail,8);length+=8;}
    body[length++]=254;
    for(unsigned int i=0;i<4;++i)body[length++]=243;
    body[length++]=255;body[length++]=0;
    setup(body,length);assert(!bhEventScheduler2()&&bhEtask[2].loop==3);
    movie_active=0;assert(!bhEventScheduler2()&&!bhEtask[2].status&&bhEtask[2].loop==-1);
    memmove(body+32+8,body+32,length-32);memcpy(body+32,retail,8);length+=8;
    setup(body,length);fault(RECVX_EVENT_FAULT_BAD_OPERAND,252);

    static const unsigned char orphan[]={243,255,0};
    setup(orphan,sizeof(orphan));fault(RECVX_EVENT_FAULT_BAD_OPERAND,243);
    setup(orphan,sizeof(orphan));bhEtask[2].loop=0;
    bhEtask[2].lcondition[0]=section+24;bhEtask[2].lstack[0]=section+24;
    fault(RECVX_EVENT_FAULT_BAD_OPERAND,243);
    setup(retail,sizeof(retail));assert(!bhEventScheduler2());
    bhEtask[2].lstack[0]=section+34;fault(RECVX_EVENT_FAULT_BAD_OPERAND,243);
    setup(retail,sizeof(retail));assert(!bhEventScheduler2());
    bhEtask[2].lcondition[0]=section+20;fault(RECVX_EVENT_FAULT_BAD_OPERAND,243);
    setup(retail,sizeof(retail));assert(!bhEventScheduler2());
    assert(!Event_exec(2,1)); /* Event replacement clears the trusted stack. */
    bhEtask[2].loop=0;bhEtask[2].lcondition[0]=section+26;bhEtask[2].lstack[0]=section+32;
    section[24+sizeof(retail)]=243;fault(RECVX_EVENT_FAULT_BAD_OPERAND,243);

    memcpy(body,retail,sizeof(retail));body[1]=7;
    setup(body,sizeof(retail));fault(RECVX_EVENT_FAULT_BAD_OPERAND,252);
    memcpy(body,retail,sizeof(retail));body[1]=sizeof(retail);
    setup(body,sizeof(retail));fault(RECVX_EVENT_FAULT_BAD_OPERAND,252); /* Crossing task body into next script. */
    memcpy(body,retail,sizeof(retail));body[2]=5;
    setup(body,sizeof(retail));fault(RECVX_EVENT_FAULT_UNSUPPORTED,5);
    static const unsigned char short_while[]={252};
    setup(short_while,sizeof(short_while));fault(RECVX_EVENT_FAULT_TRUNCATED,252);
    static const unsigned char short_condition[]={252,8,155,0};
    setup(short_condition,sizeof(short_condition));fault(RECVX_EVENT_FAULT_BAD_OPERAND,252);

    /* A mutation to an existing frame is rejected even if it points to valid script bytes. */
    setup(retail,sizeof(retail));assert(!bhEventScheduler2());
    section[26]=155;section[33]=251;
    fault(RECVX_EVENT_FAULT_BAD_OPERAND,251); /* While frame is not a counted-loop frame. */

    static const unsigned char movie_start[]={94,21,0,0,255,0};
    setup(movie_start,sizeof(movie_start));assert(!bhEventScheduler2());
    assert(sys->mvi_no==21&&(sys->cb_flg&0x4000000));
    memcpy(body,movie_start,sizeof(movie_start));body[1]=22;
    setup(body,sizeof(movie_start));fault(RECVX_EVENT_FAULT_BAD_OPERAND,94);
    assert(!sys->mvi_no&&!(sys->cb_flg&0x4000000));
    const unsigned int opcodes[3]={94,95,155},sizes[3]={4,2,6};
    for(unsigned int i=0;i<3;++i){
        memset(body,0,sizeof(body));body[0]=opcodes[i];
        setup(body,sizes[i]-1);sys->cb_flg=0x4000000;
        fault(RECVX_EVENT_FAULT_TRUNCATED,opcodes[i]);
        assert(sys->cb_flg==0x4000000&&!sys->mvi_no&&!movie_queries);
    }
    puts("PASS: original FMV while/yield/243 continuation, nested loop stack provenance, orphan/cross-script/malformed rejection, and movie event ID/extent preflights");
    return 0;
}
