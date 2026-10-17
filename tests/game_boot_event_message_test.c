#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/event.h"

static SYS_WORK work;
static ROM_WORK room;
SYS_WORK *sys=&work;
ROM_WORK *rom=&room;
BH_PWORK *plp;
unsigned char *bhScePtr;
static unsigned int messages[]={2,12,16,0xffff0021u,0xffff0022u};
static jmp_buf stopped;
static const char *fault;
void recvxGameBootUnsupported(const char *reason){fault=reason;longjmp(stopped,1);}
void recvxGameBootEventCameraPrepare(int index,int key){(void)index;(void)key;assert(0);}
static void reject(unsigned char command[4])
{
    SYS_WORK before=work;unsigned char *pc=command;bhScePtr=pc;fault=NULL;
    if(!setjmp(stopped)){bhMessageSet();assert(!"invalid message accepted");}
    assert(fault&&bhScePtr==pc&&!memcmp(&before,&work,sizeof(work)));
}
int main(void)
{
    unsigned char command[]={31,0,1,0},end[]={92,0};
    work.rdtp=(unsigned char *)messages;work.rdtsz=sizeof(messages);room.mesp=messages;
    work.sp_flg=~0u;work.st_flg=~0u;work.cb_flg=~0u;
    work.mes_ct=12;work.mes_tim=13;work.mes_fls=14;work.mes_sel=15;
    bhScePtr=command;assert(bhMessageSet()==1&&bhScePtr==command+4);
    assert(work.mes_dp==messages+4&&work.mes_idx==2&&!(work.sp_flg&7));
    assert(!work.mes_ct&&!work.mes_tim&&!work.mes_fls&&!work.mes_sel);
    assert((work.st_flg&0x200)&&!(work.st_flg&0x8dc00)&&!(work.cb_flg&0x20003000));
    command[2]=0;command[3]=1;work.sp_flg=7;bhScePtr=command;bhMessageSet();
    assert(work.sp_flg==7&&work.mes_dp==messages+3);
    command[1]=1;room.mesp=NULL;work.sp_flg=0;bhScePtr=command;bhMessageSet();
    assert(work.sp_flg==7&&bhScePtr==command+4);room.mesp=messages;command[1]=0;
    command[2]=2;reject(command);command[2]=0;
    messages[1]=11;reject(command);messages[1]=sizeof(messages);reject(command);messages[1]=12;
    messages[0]=100;reject(command);messages[0]=2;
    work.rdtsz=3;reject(command);work.rdtsz=sizeof(messages);
    room.mesp=NULL;reject(command);room.mesp=messages;
    work.mes_ct=12;work.mes_tim=13;work.mes_fls=14;work.mes_sel=15;
    bhScePtr=end;assert(bhMesDispEndSet()==1&&bhScePtr==end+2);
    assert(!work.mes_ct&&!work.mes_tim&&!work.mes_fls&&!work.mes_sel&&!(work.st_flg&0x200));
    puts("PASS original room message event setup/end, control flags, exact PCs and bounded table/index/offset guards");
    return 0;
}
