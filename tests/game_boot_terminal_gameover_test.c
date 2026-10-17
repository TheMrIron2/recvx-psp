/* Test-only codec/draw/sound leaves; execute original death and computer scripts. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_terminal_gameover.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/gameover.h"
#include "ps2/veronica/prog/sp_evt.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/weapon.h"
#include <sg_syrtc.h>

static SYS_WORK state;
SYS_WORK *sys=&state;
static BH_PWORK player;
BH_PWORK *plp=&player;
BH_PWORK ene[128];
static ROM_WORK room;
ROM_WORK *rom=&room;
CAM_WORK cam;
static NJS_MATRIX matrix;
NJS_MATRIX *cmat=&matrix;
static unsigned char memory[512*1024] __attribute__((aligned(64)));
unsigned char *freemem=memory;
unsigned int palbuf[4096];
const unsigned short PlMtnAct[2][3][7]={0};
float fNaViwClipNear=-2,fNaViwClipFar=-20000;
static RecvxGameBootRenderState render;
static RecvxBootTexture texture;
static int triangles,draws,restores,title,retries,stands,explosions,voices;
static RecvxGameChunkVertex first_triangle[3],last_triangle[3];
static jmp_buf rejection;
static const char *reason;
static int rejecting;
void recvxGameBootUnsupported(const char *name) {if(!rejecting){fprintf(stderr,"unexpected terminal/game-over boundary:%s com=%u phase=%u pc=%p\n",name,sys->com_num,sys->com_md1,sys->com_exp?((COM_EVT_WORK *)sys->com_exp)->scp:NULL);abort();}reason=name;longjmp(rejection,1);}
#define REJECT(call,part) do{rejecting=1;reason=NULL;if(!setjmp(rejection)){call;assert(!"expected boundary");}rejecting=0;assert(reason&&strstr(reason,part));}while(0)
unsigned char *bhGetFreeMemory(int bytes,int alignment) {uintptr_t p=((uintptr_t)sys->memp+alignment-1)&~(uintptr_t)(alignment-1);assert(p+bytes<=(uintptr_t)sys->endp);sys->memp=(void *)(p+bytes);memset((void *)p,0,bytes);return (void *)p;}
void npSetMemory(unsigned char *p,unsigned int bytes,char value) {memset(p,value,bytes);}
void npSetMemoryL(unsigned int *p,unsigned int words,int value) {for(unsigned int i=0;i<words;++i)p[i]=value;}
void njMemCopy4(void *dst,const void *src,int words) {memcpy(dst,src,words*4u);}
float njSin(int angle) {return sinf((angle&65535)*(6.283185307179586f/65536));}
float njCos(int angle) {return cosf((angle&65535)*(6.283185307179586f/65536));}
float njUnitVector(NJS_VECTOR *v) {float size=sqrtf(v->x*v->x+v->y*v->y+v->z*v->z);if(size){v->x/=size;v->y/=size;v->z/=size;}return size;}
void bhCrFlg(unsigned int *bits,unsigned int index) {bits[index/32]&=~(0x80000000u>>(index%32));}
void bhStFlg(unsigned int *bits,unsigned int index) {bits[index/32]|=0x80000000u>>(index%32);}
int bhCkFlg(unsigned int *bits,unsigned int index) {return !!(bits[index/32]&(0x80000000u>>(index%32)));}
void bhSetScreenFade(unsigned int color,float count) {(void)color;(void)count;}
void njFogDisable(void) {}
void njClipZ(float near,float far) {(void)near;(void)far;}
void njSetBackColor(unsigned int a,unsigned int b,unsigned int c) {(void)a;(void)b;(void)c;}
void RequestAllStopSoundEx(int a,int b,int frames) {(void)a;(void)b;(void)frames;}
void bhClearEffect(void) {}
int bhSetMotion(BH_PWORK *p,int add,int mode,void *data) {(void)p;(void)add;(void)mode;(void)data;return 0;}
void bhCalcModel(BH_PWORK *p) {(void)p;}
void CallSystemSe(int unused,int sound) {(void)unused;(void)sound;}
void CallPlayerDeadVoice(int player_no) {(void)player_no;++voices;}
void CallPlayerActionSe(int sound,int flag) {(void)sound;(void)flag;}
void bhPopGameData(void) {++retries;}
void bhReturnTitle(void) {++title;}
void bhStandPlayerMotion(void) {++stands;}
int bhDispMessage(float x,float y,float pri,int type,int no,int color,int count) {(void)x;(void)y;(void)pri;(void)type;(void)no;(void)color;(void)count;return 0;}
int bhDispMessageEx(float x,float y,float pri,int type,int no,unsigned int color,int count) {(void)x;(void)y;(void)pri;(void)type;(void)no;(void)color;(void)count;return 0;}
void bhDispFontEx(NJS_POINT2 *point,int no,unsigned int color,float pri) {(void)point;(void)no;(void)color;(void)pri;}
void njSetPerspective(int perspective) {(void)perspective;}
void njSetMatrix(NJS_MATRIX *dst,NJS_MATRIX *src) {(void)dst;(void)src;}
void njUnitMatrix(NJS_MATRIX *matrix_) {(void)matrix_;}
void njTranslate(NJS_MATRIX *m,float x,float y,float z) {(void)m;(void)x;(void)y;(void)z;}
void njRotateXYZ(NJS_MATRIX *m,int x,int y,int z) {(void)m;(void)x;(void)y;(void)z;}
void njScale(NJS_MATRIX *m,float x,float y,float z) {(void)m;(void)x;(void)y;(void)z;}
void njCalcVector(NJS_MATRIX *m,NJS_VECTOR *in,NJS_VECTOR *out) {(void)m;*out=*in;}
void njTextureFilterMode(int mode) {(void)mode;}
void njColorBlendingMode(int mode,int value) {(void)mode;(void)value;}
int njSetTexture(NJS_TEXLIST *list) {(void)list;return 0;}
int njSetTextureNum(unsigned int no) {(void)no;return 0;}
void njQuadTextureStart(int mode) {(void)mode;}
void njQuadTextureEnd(void) {}
void njSetQuadTexture(int no,unsigned int color) {(void)no;(void)color;}
void njDrawQuadTexture(QUAD *quad,float pri) {(void)quad;(void)pri;++draws;}
void njDrawPolygon2D(NJS_POINT2COL *p,int count,float pri,unsigned int attribute) {(void)p;(void)count;(void)pri;(void)attribute;++draws;}
void njUserClipping(int mode,NJS_POINT2 *points) {(void)mode;(void)points;}
void njSetScreen(NJS_SCREEN *screen) {(void)screen;}
void njCnkSetEasyLight(float x,float y,float z) {(void)x;(void)y;(void)z;}
void njCnkSetEasyLightColor(float x,float y,float z) {(void)x;(void)y;(void)z;}
void njCnkSetEasyLightIntensity(float intensity,float ambient) {(void)intensity;(void)ambient;}
void njCnkEasyDrawObject(NJS_CNK_OBJECT *object) {(void)object;}
void npRetSkinConvert(NJS_CNK_OBJECT *object,int *skin) {(void)object;(void)skin;}
void npSkinConvert(NJS_CNK_OBJECT *object,int *skin) {(void)object;(void)skin;}
int bhSetMemPvpTexture(NJS_TEXLIST *list,unsigned char *data,int offset) {(void)list;(void)data;(void)offset;return 3;}
int njReleaseTexture(NJS_TEXLIST *list) {(void)list;return 0;}
void bhGarbageTexture(NJS_TEXMEMLIST *memory_,unsigned int count) {(void)memory_;(void)count;}
void bhCopyMainmem2Texmem(NJS_TEXLIST *list) {(void)list;++restores;}
int bhSetEffectTb(EF_WORK *effect,NJS_POINT3 *offset,unsigned char *link,int no) {(void)effect;(void)offset;(void)link;(void)no;++explosions;return 0;}
const RecvxGameBootRenderState *recvxGameBootRenderStateGet(void) {return &render;}
void recvxGameBootRenderSpriteTriangle(const RecvxGameChunkVertex points[3],int textured,int transparent) {(void)textured;(void)transparent;if(!triangles)memcpy(first_triangle,points,sizeof(first_triangle));memcpy(last_triangle,points,sizeof(last_triangle));++triangles;}

static void setup(void)
{
    memset(&state,0,sizeof(state));memset(&player,0,sizeof(player));memset(&room,0,sizeof(room));
    memset(memory,0,sizeof(memory));memset(ene,0,sizeof(ene));memset(&render,0,sizeof(render));
    sys=&state;plp=&player;rom=&room;sys->memp=memory;sys->endp=memory+sizeof(memory);
    texture.width=texture.height=256;render.selected_texture=&texture;
    triangles=draws=restores=title=retries=stands=explosions=voices=0;
}
static COM_EVT_WORK *computer(unsigned int number)
{
    setup();sys->com_num=number;bhKeepSpEvtComputer();assert(sys->com_flg&1u);
    unsigned char *retained=sys->memp;bhKeepSpEvtComputer();assert(sys->memp==retained);
    sys->sbs_sp=sys->memp;bhControlSpEvtComputer();assert(sys->com_md0==1);
    return sys->com_exp;
}
int main(void)
{
    setup();bhControlGameOver();assert(sys->gov_md0==1&&sys->gov_exp);
    bhControlGameOver();assert(sys->gov_md0==2&&sys->gov_md1==0);
    GV_WORK *game=sys->gov_exp;assert(game->p3c.num==256&&game->pt[0].x==-7.5f&&game->pt[255].y==-7.5f);
    for(int i=0;i<71;++i) { bhControlGameOver(); }
    assert(sys->gov_md1==1);
    for(int i=0;i<51;++i) { bhControlGameOver(); }
    assert(sys->gov_md1==2&&triangles>0);
    for(int i=0;i<17;++i) { bhControlGameOver(); }
    assert(game->count==255);
    sys->pad_ps=0x800;bhControlGameOver();assert(sys->gov_md1==3);sys->pad_ps=0;
    bhControlGameOver();assert(retries==1&&sys->ts_flg&0x4000u);
    sys->gov_md0=3;bhControlGameOver();assert(title==1);sys->gm_mode=3;bhControlGameOver();assert(retries==2);
    sys->gov_md0=4;REJECT(bhControlGameOver(),"dispatch");
    setup();sys->gov_md0=1;sys->gov_exp=memory+sizeof(memory)-4;REJECT(bhControlGameOver(),"work");
    NJS_POINT3 points[4]={{0,0,0},{1,0,0},{0,1,0},{1,1,0}};NJS_COLOR colors[4]={{.color=0xff112233u}},uv[4]={{.tex={128,64}}};
    NJS_POINT3COL strip={.p=points,.col=colors,.tex=uv,.num=4};triangles=0;
    njDrawTriangle3D(&strip,2,0x80000062u);assert(triangles==2&&first_triangle[0].u==.5f&&first_triangle[0].v==.25f&&last_triangle[0].x==0&&last_triangle[0].y==1);
    REJECT(njDrawTriangle3D(&strip,3,0x80000062u),"triangle count");
    REJECT(njDrawTriangle3D(&strip,1,0x40000062u),"attributes");

    for(volatile unsigned int n=0;n<8;++n) {
        COM_EVT_WORK *work=computer(n);assert(work->com_tlist.nbTexture==3&&work->scp);
        assert(recvxGameBootComputerScriptSize(n)>0&&recvxGameBootComputerMessageCount(n)>0);
        REJECT(recvxGameBootComputerInstruction(work->scp+recvxGameBootComputerScriptSize(n),1),"script position");
        for(int i=0;i<18;++i)bhControlSpEvtComputer();
        assert(sys->com_md1==10&&draws>0);
        /* Some authored terminals forbid cancellation. Exercise the controller
           cancellation path with its script held at a wait instruction. */
        sys->com_flg &= ~0x200u; work->txflg |= 4u;
        sys->pad_ps=0x1000;bhControlSpEvtComputer();sys->pad_ps=0;
        for(int i=0;i<100&&sys->com_md0!=2;++i)bhControlSpEvtComputer();
        assert(sys->com_md0==2);bhControlSpEvtComputer();assert(stands==1&&sys->memp==sys->sbs_sp);
    }
    COM_EVT_WORK *work=computer(1);
    /* Run authored security script to its real keyboard, enter a wrong code,
       follow its rejection label, and verify a second input can be requested. */
    for(int i=0;i<2000&&!(sys->com_flg&0x10u);++i){sys->pad_ps=0x800;bhControlSpEvtComputer();}
    assert(sys->com_flg&0x10u);for(int i=0;i<18;++i){sys->pad_ps=0;bhControlSpEvtComputer();}
    assert(sys->com_md1==10&&work->wordmax==8);work->kbn=0; work->word[0][0]=0; work->tx=work->wdx;
    work->kx=1;work->ky=1;sys->pad_ps=0x800;bhControlSpEvtComputer();
    assert(work->kbn==1&&work->word[0][0]=='A');work->kx=9;work->ky=1;bhControlSpEvtComputer();assert(work->kbn==0);
    work->kx=9;work->ky=2;bhControlSpEvtComputer();assert(!(work->txflg&8u));
    for(int i=0;i<3000&&!(sys->com_flg&0x10u);++i){sys->pad_ps=0x800;bhControlSpEvtComputer();}
    work->tx=30;REJECT(bhControlSpEvtComputer(),"text/keyboard state");
    SYS_RTC_DATE date;assert(!syRtcInit()&&!syRtcGetDate(&date)&&date.hour<24&&date.minute<60&&date.second<61);syRtcFinish();
    puts("Original game-over/computer: death/continue, triangle strips, all8 terminal initialization/cancel paths, security keyboard/rejection, bounds and RTC PASS");
    return 0;
}
