/* Registered zombie companions live outside the room allocation arena. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot_event_remaining.h"
#include "ps2/veronica/prog/event.h"

static SYS_WORK system_work;
static ROM_WORK room_work;
SYS_WORK *sys=&system_work;
ROM_WORK *rom=&room_work;
BH_PWORK ene[128];
CAM_WORK cam;
BH_PWORK *plp=&ene[0];
BH_SCEWORK bhEtask[16],*bhCetask;
unsigned char *bhScePtr,*freemem;
static union { BH_PWORK actor; unsigned char bytes[sizeof(BH_PWORK)+16]; } arena;
static ETTY_WORK placement;
static jmp_buf rejected;
static const char *failure;
size_t recvxGameBootSourceRemaining(const void *p)
{ return p==&placement?sizeof(placement):0; }
void recvxGameBootUnsupported(const char *reason)
{ failure=reason;longjmp(rejected,1); }
static void prepare(unsigned int opcode,const void *link,int valid)
{
    unsigned char command[4]={(unsigned char)opcode,0,0,0};
    bhScePtr=command;ene[0].exp1=(unsigned char *)link;failure=NULL;
    if(!setjmp(rejected)){
        recvxGameBootEventRemainingPrepare(opcode);
        assert(valid);
    }else{
        assert(!valid&&failure&&strstr(failure,opcode==142?"zombie damage":"motion pause"));
    }
}
int main(void)
{
    freemem=arena.bytes;sys->endp=arena.bytes+sizeof(arena);
    rom->enep=&placement;rom->ene_n=1;placement.wrk_no=0;ene[0].id=1;
    const unsigned int opcodes[]={45,48,142};
    for(unsigned int i=0;i<3;++i){
        for(unsigned int actor=0;actor<128;++actor)prepare(opcodes[i],&ene[actor],1);
        prepare(opcodes[i],&arena.actor,1);
        prepare(opcodes[i],NULL,0);
        prepare(opcodes[i],(unsigned char *)&ene[1]+4,0);
        prepare(opcodes[i],ene+128,0);
        prepare(opcodes[i],arena.bytes+sizeof(arena)-4,0);
    }
    /* Non-zombie commands do not read a companion. */
    ene[0].id=2;for(unsigned int i=0;i<3;++i)prepare(opcodes[i],NULL,1);
    puts("PASS: zombie damage and motion-pause guards accept all 128 registered enemy actors and reject null, interior, out-of-pool and truncated companions");
    return 0;
}
