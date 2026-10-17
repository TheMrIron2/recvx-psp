#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_inventory_control.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/fileview.h"
#include "ps2/veronica/prog/message.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/item.h"
#include "ps2/veronica/prog/ps2_NaSprite.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "ps2/veronica/prog/ps2_NaDraw2D.h"

static SYS_WORK state;
SYS_WORK *sys=&state;
static ROM_WORK room;
ROM_WORK *rom=&room;
static BH_PWORK player;
BH_PWORK *plp=&player;
S_WORK swork;
FV_WORK fvwork;
unsigned char BackColorFlag;
float FontScaleX=1,FontScaleY=1,FontScaleCR=1;
static unsigned char arena[262144] __attribute__((aligned(64)));
unsigned char *freemem=arena;
static jmp_buf fault;
static int rejecting,draws,names;
void recvxGameBootUnsupported(const char *reason)
{ if(!rejecting){fprintf(stderr,"inventory fault: %s\n",reason);abort();} longjmp(fault,1); }
#define REJECT(call) do { rejecting=1; if(!setjmp(fault)){ call; assert(!"expected fault");} rejecting=0; } while(0)
unsigned char *bhGetFreeMemory(int size,int alignment)
{ uintptr_t p=((uintptr_t)sys->memp+alignment-1)&~(uintptr_t)(alignment-1); assert(p+size<=(uintptr_t)sys->endp);sys->memp=(unsigned char*)(p+size);return (unsigned char*)p; }
size_t recvxGameBootSourceRemaining(const void *p)
{ return (uintptr_t)p>=(uintptr_t)arena && (uintptr_t)p<(uintptr_t)(arena+sizeof(arena)) ? (uintptr_t)(arena+sizeof(arena))-(uintptr_t)p : 0; }
void bhDispFont(NJS_POINT2 *p,int code,int color,float priority)
{ assert(p && code>=0 && color==0 && isfinite(priority)); ++names; }
float bhGetFontSize(unsigned short code) { (void)code;return 8; }
void bhFontScaleSet(float x,float y,float ratio) { (void)ratio;FontScaleX=x;FontScaleY=y; }
void njDrawSprite2D(NJS_SPRITE *s,Int animation,Float priority,Uint32 attr)
{ assert(s && s->tlist && s->tanim && animation>=0 && isfinite(priority));(void)attr;++draws; }
void njDrawPolygon2D(NJS_POINT2COL *p,Int n,Float pri,Uint32 attr)
{ assert(p && n==4 && isfinite(pri)); (void)attr; ++draws; }
Sint32 njSetTexture(NJS_TEXLIST *list) { assert(list);return 0; }
Sint32 njSetTextureNum(Uint32 n) { assert(n<5);return 0; }
void bhCheckSubPack(void) { }
void Model_Read_Start(S_WORK *s) { assert(s==&swork); }
void Model_Read_Set(S_WORK *s) { assert(s==&swork); }

int main(void)
{
    NJS_POINT2 text={0,0};
    PARTS part={0};
    memset(&state,0,sizeof(state));memset(&swork,0,sizeof(swork));
    sys->memp=arena;sys->endp=arena+sizeof(arena);
    sys->ply_id=0;player.hp=160;
    sys->itm[2]=(1u<<16)|15;
    swork.subscreenmode=1;
    swork.subtx_list.textures=swork.subtx;swork.subtx_list.nbTexture=5;
    sys->mes_ip=(unsigned int*)(arena+200000);
    for(int i=0;i<161;++i)sys->mes_ip[i]=648;
    sys->mes_ip[0]=160;
    ((unsigned short*)sys->mes_ip)[324]=1;
    ((unsigned short*)sys->mes_ip)[325]=0xffff;
    sys->mn_md0=1; /* Controller timing test avoids model I/O; model has its own fixture. */
    StatusMain();
    assert(swork.subscreenmode==2 && swork.mode==1 && swork.pip==sys->itm && swork.listcsr_0==2);
    for(int i=0;i<40;++i) StatusMain();
    assert(draws>100 && !(swork.statusflg&0x40));
    assert(names>0);
    sys->st_flg |= 0x4000; /* Original cancel waits until message display is complete. */
    sys->pad_ps=0x4000;
    StatusMain();sys->pad_ps=0;
    for(int i=0;i<40;++i)StatusMain();
    assert(swork.mode==0x20 && (swork.statusflg&0x80000));
    assert(bhDispItemName(&text,1,0,0,1)==1 && text.x==8);
    REJECT(bhDispItemName(&text,160,0,0,1));
    sys->mes_ip[2]=0xffffffff;REJECT(bhDispItemName(&text,1,0,0,1));
    swork.listcsr_0=16;REJECT(recvxGameBootInventoryControlPrepare());swork.listcsr_0=2;
    sys->itm[2]=156u<<16;REJECT(recvxGameBootInventoryControlPrepare());sys->itm[2]=1u<<16;
    part.parts_num=23;REJECT(recvxGameBootInventorySpritePrepare(&part));
    swork.statusflg=0; swork.testmode=0; swork.itemid=20;
    swork.listcsr_0=2; swork.subcsr=0; sys->itm[2]=(20u<<16)|1;
    player.hp=10; sys->st_flg=0; sys->cb_flg=0;
    assert(ItemUse(&swork)==2 && player.hp==160 && sys->spray_ct==1 && !sys->itm[2]);
    swork.itemid=22; sys->itm[2]=(22u<<16)|1;
    assert(ItemUse(&swork)==2 && sys->itm[2]==((22u<<16)|1)); /* A red herb alone is retained. */
    sys->itm[2]=(16u<<16)|20;sys->itm[3]=(16u<<16)|30;
    assert(Combi_01(27,&sys->itm[2],&sys->itm[3])==1 && !sys->itm[2] && sys->itm[3]==((16u<<16)|50));
    REJECT(Combi_01(27,&sys->itm[2],&sys->itm[2]));
    REJECT(EraseItem((unsigned int*)arena));
    swork.testmode=0; swork.subcsr=0; swork.listcsr_0=2;
    ItemCombination(&swork); assert(swork.subcsr==1);
    sys->itm[383]=0xf0ff8105u; recvxGameBootInventoryControlPrepare();
    swork.pip=(unsigned int*)arena;REJECT(recvxGameBootInventoryWorkPrepare(&swork));
    puts("original inventory initialization, animation, rendering, input cancellation and malformed-state guards: PASS");
}
