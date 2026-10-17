/* Original controller state machines; storage and draw mocks are test-only. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_save_controllers.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ranking.h"
#include "ps2/veronica/prog/message.h"
#include "ps2/veronica/prog/room.h"
#include "ps2/veronica/prog/bup_00.h"
#include "ps2/veronica/prog/ps2_LoadScreen.h"
#include "ps2/veronica/prog/ps2_SaveScreen.h"
#include "ps2/veronica/prog/ps2_SystemSaveScreen.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/sub1.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
static BH_PWORK player;
BH_PWORK *plp = &player;
static ROM_WORK room;
ROM_WORK *rom = &room;
static unsigned char memory[512*1024] __attribute__((aligned(64)));
unsigned char *freemem = memory, *vwbmemp;
unsigned int palbuf[4096];
extern RANK_WORK rkw;
extern SAVE_SCREEN Save, *pSave;
extern LOAD_SCREEN Load, *pLoad;
static int messages, fonts, bgm, stopped, restored, released, fogged, saves, loads;
static int exit_screen, system_result, returned, exited;
float FontScaleX=1;
static int font_codes[64], font_record_count;
static jmp_buf reject;
static int rejecting;
static const char *reason;
void recvxGameBootUnsupported(const char *name)
{ if(!rejecting){fprintf(stderr,"unexpected controller boundary: %s\n",name);abort();} reason=name;longjmp(reject,1); }
#define REJECT(call,part) do {rejecting=1;reason=NULL;if(!setjmp(reject)){call;assert(!"expected boundary");} rejecting=0;assert(reason&&strstr(reason,part));}while(0)
size_t recvxGameBootSourceRemaining(const void *p)
{ return (uintptr_t)p >= (uintptr_t)memory && (uintptr_t)p < (uintptr_t)(memory+sizeof(memory)) ? sizeof(memory)-((uintptr_t)p-(uintptr_t)memory) : 0; }
int bhCkFlg(unsigned int *flags,unsigned int bit) { return !!(flags[bit/32] & (0x80000000u>>(bit%32))); }
unsigned char *bhGetFreeMemory(int size,int alignment)
{ uintptr_t p=((uintptr_t)sys->memp+alignment-1)&~(uintptr_t)(alignment-1);assert(p+size<=(uintptr_t)sys->endp);sys->memp=(void *)(p+size);memset((void *)p,0,size);return (void *)p; }
int GetIsoFileSize(char *name) {assert(!strcmp(name,"sysmes.ald"));return 16;}
int GetInsideFileSize(unsigned int partition,unsigned int id) {(void)partition;assert(id==1||id==144||(id>=277&&id<=281));return 64;}
int RequestReadIsoFile(char *name,void *p) {(void)name;memset(p,0,16);*(uint32_t *)p=4;return 0;}
int RequestReadInsideFile(unsigned int partition,unsigned int id,void *p) {(void)partition;(void)id;memset(p,0,64);return 0;}
int GetReadFileStatus(void) {return 0;}
void bhSetFontTexture(void *p) {(void)p;}
int bhSetMemPvpTexture(NJS_TEXLIST *list,unsigned char *p,int offset) {(void)p;(void)offset;return list==&rkw.rktx_list?2:3;}
void njSetBackColor(unsigned int a,unsigned int b,unsigned int c) {(void)a;(void)b;(void)c;}
int njSetTexture(NJS_TEXLIST *list) {(void)list;return 0;}
int njSetTextureNum(unsigned int n) {(void)n;return 0;}
void njQuadTextureStart(int n) {(void)n;}
void njQuadTextureEnd(void) {}
void njSetQuadTexture(int id,unsigned int color) {(void)id;(void)color;}
void njDrawQuadTexture(QUAD *q,float z) {(void)q;(void)z;}
void njDrawPolygon2D(NJS_POINT2COL *p,int n,float priority,unsigned int attribute) {(void)p;(void)n;(void)priority;(void)attribute;}
void njDrawPolygon2DM(NJS_POINT2COL *p,int n,float priority,unsigned int attribute) {(void)p;(void)n;(void)priority;(void)attribute;}
void bhDispFontEx(NJS_POINT2 *p,int code,unsigned int color,float priority) {(void)p;(void)color;(void)priority;++fonts;if(font_record_count<64)font_codes[font_record_count++]=code;}
int bhDispMessageEx(float x,float y,float priority,int type,int index,unsigned int color,int count) {(void)x;(void)y;(void)priority;(void)type;(void)index;(void)color;(void)count;++messages;return 0;}
float bhGetFontSize(unsigned short code) {(void)code;return 10;}
void PlayBgm(int number,int fade) {(void)fade;bgm=number;}
void StopBgm(int frames) {assert(frames==200);++stopped;}
void bhReturnTitle(void) {++returned;}
void bhExitGame(void) {++exited;}
void recvxGameBootCheckpointPrepare(void) {assert(sys&&plp&&freemem==memory&&sys->memp>=memory+4096);}
void npCopyMemory(unsigned char *dst,unsigned char *src,unsigned int size) {memmove(dst,src,size);}
char *bhCopyTexmem2MainmemSub(NJS_TEXLIST *textures,char *p) {(void)textures;return p+64;}
void bhCopyMainmem2Texmem(NJS_TEXLIST *textures) {(void)textures;++restored;}
int njReleaseTexture(NJS_TEXLIST *textures) {(void)textures;++released;return 0;}
void njReleaseTextureAll(void) {++released;}
void bhGarbageTexture(NJS_TEXMEMLIST *address,unsigned int count) {(void)address;(void)count;}
void njMemCopy4(void *dst,const void *src,int words) {memcpy(dst,src,words*4u);}
void njSetPaletteMode(unsigned int mode) {assert(mode==2||mode==6);}
void njSetPaletteData(int offset,int count,void *p) {(void)offset;(void)count;(void)p;}
void njFogEnable(void) {++fogged;}
void bhSetScreenFade(unsigned int color,float frames) {(void)color;assert(frames==3);}
void bhDrawScreenFade(void) {}
SAVE_SCREEN *CreateSaveScreen(SAVE_SCREEN *screen,void *work) {(void)work;memset(screen,0,sizeof(*screen));++saves;return screen;}
int ExecuteSaveScreen(SAVE_SCREEN *screen) {screen->usExitFlag=exit_screen;return 0;}
LOAD_SCREEN *CreateLoadScreen(LOAD_SCREEN *screen,void *work) {(void)work;memset(screen,0,sizeof(*screen));++loads;return screen;}
int ExecuteLoadScreen(LOAD_SCREEN *screen) {screen->usExitFlag=exit_screen;return 0;}
SYSSAVE_SCREEN *CreateSysSaveScreen(SYSSAVE_SCREEN *screen,void *work,unsigned short message,unsigned short write) {(void)work;assert(message==1&&write==1);memset(screen,0,sizeof(*screen));return screen;}
int ExecuteSysSaveScreen(SYSSAVE_SCREEN *screen) {(void)screen;return system_result;}

static void setup(void)
{
    memset(&state,0,sizeof(state)); memset(&player,0,sizeof(player));memset(&room,0,sizeof(room));
    memset(&rkw,0,sizeof(rkw));memset(palbuf,0,sizeof(palbuf));
    sys=&state;plp=&player;rom=&room;sys->memp=memory+4096;sys->endp=memory+sizeof(memory);
    sys->typ_exp=memory;memset(memory,0,sizeof(TYP_WORK));sys->time=1000;
    messages=fonts=bgm=stopped=restored=released=fogged=saves=loads=exit_screen=system_result=returned=exited=font_record_count=0;
    DispFadeInit();
}
static void finish_writer(void)
{
    exit_screen=0;ControlTypewriter();assert(sys->typ_md0==1);
    exit_screen=1;ControlTypewriter();assert(sys->typ_md0==2);
    ControlTypewriter();if(sys->typ_md0==2)ControlTypewriter();assert(sys->typ_md0==0);
}
int main(void)
{
    setup();
    assert(GameClearScore(0)==8250&&GameClearScore(972059)==8250&&GameClearScore(972060)==7550);
    assert(AllRanking(10000,0)==1&&AllRanking(9999,0)==2&&AllRanking(6999,0)==4&&AllRanking(1000,0)==16);
    sys->ev_flg[75/32]|=0x80000000u>>(75%32);assert(AllRanking(10000,0)==2&&AllRanking(10000,1)==1);
    sys->ev_flg[75/32]=0;assert(RodorigoEventScore()==-1000&&SteveEventScore()==0);
    sys->spray_ct=0;assert(HealItemUseScore()==1800);sys->spray_ct=1;assert(!HealItemUseScore());
    sys->save_ct=3;sys->retry_ct=2;assert(SaveCountScore()==-1100&&RetryCountScore()==-1050);
    const unsigned int mapbits[]={6,7,0x5d,0x5e,0x8f};assert(!MapScore());
    for(unsigned int i=0;i<5;++i)sys->mp_flg[mapbits[i]/32]|=0x80000000u>>(mapbits[i]%32);
    assert(MapScore()==100);
    sys->gm_mode=3;sys->ply_id=3;assert(GameClearScore(0)==10000);sys->costume=1;assert(GameClearScore(28740)==8000);
    sys->gm_mode=1;REJECT(GameClearScore(0),"score selectors");
    setup();sys->typ_md0=6;REJECT(ControlRanking(),"ranking dispatch");
    setup();sys->typ_md0=3;REJECT(ControlTypewriter(),"typewriter dispatch");
    setup();sys->typ_exp=memory+sizeof(memory)-8;REJECT(ControlTypewriter(),"typewriter dispatch");
    setup();rkw.mesloop=5;sys->typ_md0=4;REJECT(ControlRanking(),"presentation state");
    setup();NJS_POINT2 position={0,0};REJECT(DispNumber(&position,100,0),"two-digit");
    setup();sys->mes_ip=(unsigned int *)memory;*(unsigned int *)memory=3;REJECT(recvxGameBootSaveMessagePrepare(),"message tables");

    setup();sys->ssd_flg=0xf; /* already unlocked; avoid notification overlay */
    for(int i=0;i<9;++i)ControlRanking();
    assert(sys->typ_md0==1&&rkw.rktx_list.nbTexture==2&&rkw.allscore>0);
    ControlRanking();assert(sys->typ_md0==3);
    ControlRanking();assert(sys->typ_md0==3);
    ControlRanking();assert(sys->typ_md0==3);system_result=1;ControlRanking();assert(sys->typ_md0==4&&bgm);
    for(int i=0;i<100;++i)ControlRanking();
    assert(rkw.mode_01==1&&rkw.cnt00==0&&messages>0&&fonts>0);
    sys->pad_ps=0x1800;for(int i=0;i<30;++i)ControlRanking();sys->pad_ps=0;
    for(int i=0;i<40&&sys->typ_md0!=5;++i)ControlRanking();
    assert(stopped==1&&sys->typ_md0==5);
    ControlRanking();assert(returned==1&&sys->typ_md0==0);

    for(int character=0;character<5;++character) {
        setup();sys->gm_mode=3;sys->ply_id=character<4?character:0;sys->costume=character==4;
        sys->typ_md0=0;rkw.mode_00=8;sys->typ_flg=0x80000000u;
        ControlRanking();assert(sys->typ_md0==1&&rkw.col==2&&sys->best_tm[character]==sys->time);
        ControlRanking();ControlRanking();system_result=2;ControlRanking();assert(sys->typ_md0==4);
    }
    setup();NJS_TEXLIST room_textures={0};room.mdl.texP=&room_textures;sys->st_flg=2;
    plp->px=1;plp->py=2;plp->pz=3;plp->hp=88;plp->wpnr_no=4;plp->flr_no=2;plp->ay=123;
    for(int i=0;i<6;++i)ControlTypewriter();
    assert(sys->typ_md0==1&&saves==1&&loads==0&&released==1);
    assert(sys->version==10&&sys->ply_hp[0]==88&&sys->ply_pos.z==3&&sys->ply_wno[0]==4&&sys->ply_ang==123);
    finish_writer();assert(restored==1&&fogged==1&&released==2&&sys->gm_flg&0x8000);
    setup();sys->ss_flg=0x200;for(int i=0;i<9;++i)ControlTypewriter();
    assert(sys->typ_md0==1&&loads==1&&saves==0);finish_writer();assert(released==1&&sys->tk_flg==0x300008);
    setup();sys->gm_mode=0;position=(NJS_POINT2){1,2};
    bhDispTimeEx(&position,14,216000+3600*2+60*3,0xffffffffu,-2);
    const int expected_time[]={16,17,26,16,18,26,16,19};
    assert(font_record_count==8&&!memcmp(font_codes,expected_time,sizeof(expected_time))&&position.x==1);
    sys->gm_mode=3;font_record_count=0;bhDispTimeEx(&position,7,216000,0xffffffffu,-2);
    const int expected_cap[]={21,25,7,21,25,2,25,25};
    assert(font_record_count==8&&!memcmp(font_codes,expected_cap,sizeof(expected_cap)));
    REJECT(bhDispTimeEx(&position,7,-1,0,-2),"time display");
    setup();sys->version=10;sys->retry_ct=98;sys->ply_hp[0]=77;
    memcpy(memory,&sys->version,offsetof(SYS_WORK,save_end)-offsetof(SYS_WORK,version));
    sys->retry_ct=0;sys->ply_hp[0]=1;bhPopGameData();
    assert(exited==1&&sys->retry_ct==99&&sys->ply_hp[0]==77&&sys->tk_flg==0x300020&&sys->gm_flg&2&&sys->ss_flg&0x200&&!(sys->ss_flg&0x20000));
    bhPopGameData();assert(sys->retry_ct==99&&exited==2);
    *(unsigned int *)memory=9;REJECT(bhPopGameData(),"checkpoint payload");
    puts("Original ranking/typewriter: scoring, resources, ranking fade/exit, save/load dispatch, player snapshot, texture restoration, checkpoint restore, timer glyphs and bounds PASS");
    return 0;
}
