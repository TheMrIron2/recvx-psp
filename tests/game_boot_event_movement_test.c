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
char bhCheckFloorNum(float height) { (void)height; assert(0); return 0; }
int bhSetMotion(BH_PWORK *p, int a, int b, void *d)
{ (void)p; (void)a; (void)b; (void)d; assert(0); return 0; }
void bhCalcModel(BH_PWORK *p) { (void)p; assert(0); }
void bhCalcLinkModel(BH_PWORK *p) { (void)p; assert(0); }
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
int main(void)
{
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
    puts("PASS original CommonCtr movement/signs, authored per-frame counted loops, nested stacks, operand bounds and liveness");
    return 0;
}
