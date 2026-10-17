#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/ps2_SystemSaveScreen.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
PAD_WRK Pad[4];
static jmp_buf fault;
static int rejecting, card_type, messages;
void recvxGameBootUnsupported(const char *reason)
{ if (!rejecting) { fprintf(stderr,"unexpected: %s\n",reason); abort(); } longjmp(fault,1); }
MEMORYCARDSTATE *CreateMemoryCard(MEMORYCARDSTATE *card)
{ memset(card,0,sizeof(*card)); return card; }
int GetMcSelectPortType(MEMORYCARDSTATE *card,unsigned int port)
{ assert(card && port<2); return card_type; }
void njPrintSize(Uint16 size) { assert(size==12); }
void njPrintColor(Uint32 color) { assert(color==0xffffffff); }
void njPrintC(Int loc,const char *text)
{ assert((loc>>16)==4 && text && *text); ++messages; }
#define REJECT(call) do { rejecting=1; if (!setjmp(fault)) { call; assert(!"expected fault"); } rejecting=0; } while(0)
int main(void)
{
    SYSSAVE_SCREEN screen, stale;
    char scratch[64];
    memset(&screen,0xcd,sizeof(screen));
    assert(CreateSysSaveScreen(&screen,scratch,1,1)==&screen);
    assert(!screen.ulState && !screen.ulSubState && !screen.ulFileSize && !screen.usExitFlag);
    assert(screen.usMesMode==1 && screen.usSaveMode==1 && screen.vpReadBuffer==scratch);
    assert(screen.pConfigFile->vibration==1 && screen.pConfigFile->Check_Sam==1);
    assert(screen.pSelectFileWindow->lRecordMax==15 && screen.pSelectFileWindow->pRecordTop==screen.pSelectFileInfo);
    for (int i=0;i<15;++i) assert(screen.pSelectFileInfo[i].FileNumber==i+1);
    sys->typ_md0=7; sys->typ_md1=3; sys->tk_flg=0x2000;
    Pad[0].press=0x800;
    assert(!ExecuteSysSaveScreen(&screen)); /* START during awareness cannot bypass notice. */
    assert(screen.ulState==2 && screen.cMesFlag==1 && screen.lCardState==103);
    Pad[0].press=0;
    for(int i=0;i<120;++i) assert(!ExecuteSysSaveScreen(&screen));
    Pad[0].press=0x40; assert(!ExecuteSysSaveScreen(&screen));
    Pad[0].press=0x800;
    assert(ExecuteSysSaveScreen(&screen)==1 && screen.ulState==50);
    assert(sys->typ_md0==7 && sys->typ_md1==3);
    assert(ExecuteSysSaveScreen(&screen)==1 && sys->typ_md0==7 && !sys->typ_md1);
    sys->tk_flg=0; assert(ExecuteSysSaveScreen(&screen)==1 && !sys->typ_md0);
    assert(messages>300);
    assert(CreateSysSaveScreen(&screen,NULL,0,0)==&screen && !screen.usExitFlag && !screen.usSaveMode);
    Pad[0].press=0; screen.ulState=3;
    assert(!ExecuteSysSaveScreen(&screen) && screen.ulState==0);
    assert(!ExecuteSysSaveScreen(&screen) && screen.ulState==2);
    screen.ulState=1; Pad[0].press=0x800;
    assert(ExecuteSysSaveScreen(&screen)==1 && screen.ulState==50);
    CreateSysSaveScreen(&screen,NULL,0,1);
    screen.ulState=30; REJECT(ExecuteSysSaveScreen(&screen));
    CreateSysSaveScreen(&screen,NULL,0,1);
    card_type=2; REJECT(ExecuteSysSaveScreen(&screen)); card_type=0;
    stale=screen; stale.pMcState=NULL; REJECT(ExecuteSysSaveScreen(&stale));
    REJECT(CreateSysSaveScreen(NULL,NULL,0,0));
    REJECT(ExecuteSysSaveScreen(NULL));
    puts("system-save no-card lifecycle, explicit cancellation, defaults and unsupported writes: PASS");
}
