#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot_effect_shadow.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_dummy.h"

SYS_WORK *sys;
CAM_WORK cam;
BH_PWORK *plp, ene[128];
O_WRK eff[512];
unsigned char *freemem;
static SYS_WORK state;
static RecvxGameBootRenderState render;
static struct { NJS_CNK_OBJECT objects[2]; NJS_CNK_MODEL model; O_WORK pose[2]; unsigned char v[48],p[24]; O_WRK actor; } arena;
static unsigned int checks, starts, ends, triangles[2], composites, fog;
static float rectangle[4];
static NJS_MATRIX matrix;
static RecvxGameChunkVertex submitted[3];
static jmp_buf jump;
static const char *fault;
static void check(int condition) { ++checks; assert(condition); }
void recvxGameBootUnsupported(const char *name) { fault=name;longjmp(jump,1); }
void recvxGameBootLog(const char *format,...) { (void)format; }
void recvxGameBootEffectDrawQueuePrepare(O_WRK **objects,int count)
{ check(objects==sys->ef_mdf); if(count<0||count>512) recvxGameBootUnsupported("queue"); }
void recvxGameBootEffectSpriteCheckMath(void)
{ if(recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault()); }
size_t recvxGameBootSourceRemaining(const void *p) { (void)p; return 0; }
const RecvxGameBootRenderState *recvxGameBootRenderStateGet(void) { return &render; }
Float recvxEngineOriginalViewProjectionDistance(void) { return 100; }
void recvxEngineOriginalViewClipPlanes(Float *near,Float *far) { *near=-1;*far=-1000; }
void recvxEngineOriginalViewAspect(Float *x,Float *y) { *x=*y=1; }
float njCalcFogPower(float z) { return fmaxf(0,fminf(255,355-z)); }
void njTranslateEx(NJS_VECTOR *p) { njTranslate(NULL,p->x,p->y,p->z); }
void recvxGameBootRenderShadowStart(void) { ++starts; }
void recvxGameBootRenderShadowEnd(void) { ++ends; }
void recvxGameBootRenderShadowTriangle(const RecvxGameChunkVertex input[3],unsigned int pass)
{ check(pass<2);++triangles[pass];memcpy(submitted,input,sizeof(submitted));njGetMatrix(&matrix); }
void recvxGameBootRenderShadowComposite(const float bounds[4],unsigned int value)
{ ++composites;memcpy(rectangle,bounds,sizeof(rectangle));fog=value; }
static void put16(unsigned char *p,unsigned int v) { p[0]=v;p[1]=v>>8; }
static void putf(unsigned char *p,float v) { memcpy(p,&v,4); }
static void reset(void)
{
    memset(&state,0,sizeof(state));memset(eff,0,sizeof(eff));memset(&arena,0,sizeof(arena));
    memset(&render,0,sizeof(render));sys=&state;plp=NULL;
    freemem=(unsigned char *)&arena;sys->endp=freemem+sizeof(arena);
    recvxEngineOriginalFaultReset();recvxEngineOriginalMatrixReset();njUnitMatrix(NULL);recvxGameBootEffectShadowReset();
    eff[0].mtx=(NJS_MATRIX *)eff[0].mtxbuf;eff[0].pz=100;eff[0].sx=eff[0].sy=eff[0].sz=1;
    eff[0].mlwP=&sys->efm[1];eff[0].mlwP->obj_num=1;eff[0].mlwP->objP=arena.objects;
    arena.objects[0].model=&arena.model;arena.model.vlist=(Sint32 *)arena.v;arena.model.plist=(Sint16 *)arena.p;
    arena.v[0]=34;put16(arena.v+2,10);put16(arena.v+4,4);put16(arena.v+6,3);
    putf(arena.v+8,0);putf(arena.v+12,0);putf(arena.v+16,0);
    putf(arena.v+20,1);putf(arena.v+24,0);putf(arena.v+28,0);
    putf(arena.v+32,0);putf(arena.v+36,1);putf(arena.v+40,0);
    arena.p[0]=56;put16(arena.p+2,7);put16(arena.p+4,2);
    const unsigned int indices[]={4,6,5,4,5,6};
    for(unsigned int i=0;i<6;++i) put16(arena.p+6+i*2,indices[i]);
    sys->ef_mdf[0]=&eff[0];starts=ends=triangles[0]=triangles[1]=composites=0;fog=999;fault=NULL;
}
#define DRAW() bhDrawMdfEffect((unsigned int *)sys->ef_mdf,1)
#define REJECT(call,message) do { fault=NULL;if(!setjmp(jump)) { call;check(0); }check(fault&&strstr(fault,message)); } while(0)
int main(void)
{
    reset();DRAW();check(starts==1&&ends==1&&triangles[0]==1&&triangles[1]==1&&composites==1);
    check(matrix[14]==100&&matrix[0]==1);check(submitted[0].x==0&&submitted[1].x==1&&submitted[2].y==1);
    check(rectangle[0]==320&&rectangle[1]==240&&rectangle[2]==321&&rectangle[3]==241&&fog==255);
    reset();eff[0].flg=0x1000000;eff[0].mlwP=NULL;DRAW();check(starts==1&&ends==1&&!composites&&!triangles[0]);
    reset();sys->gm_flg=0x4000;eff[0].mdflg=0x40;DRAW();check(!triangles[1]);
    reset();eff[0].px=1000;DRAW();check(triangles[0]==1&&triangles[1]==1&&!composites);
    reset();eff[0].pz=0.5f;DRAW();check(composites==1&&rectangle[0]==0&&rectangle[1]==0&&rectangle[2]==0&&rectangle[3]==0);
    reset();render.control_flags=0x100;arena.model.r=1;eff[0].pz=2000;DRAW();check(!triangles[0]&&!composites);
    reset();eff[0].sx=2;eff[0].sy=3;DRAW();check(rectangle[2]==322&&rectangle[3]==243&&matrix[0]==2&&matrix[5]==3);
    reset();eff[0].flg=0x80;eff[0].lkwkp=(unsigned char *)&eff[1];eff[1].mlwP=&eff[1].mdl[0];
    eff[1].mlwP->obj_num=1;eff[1].mlwP->objP=arena.objects;eff[1].mlwP->owP=arena.pose;
    njUnitMatrix(&arena.pose[0].mtx);arena.pose[0].mtx[14]=100;eff[0].lox=10;
    DRAW();check(matrix[12]==10&&matrix[14]==100&&rectangle[0]==330);
    reset();eff[0].mlwP=NULL;REJECT(DRAW(),"model work");
    reset();sys->obwp=(O_WRK *)4;sys->ef_mdf[0]=sys->obwp;
    REJECT(DRAW(),"actor");
    reset();eff[0].flg=0x80;sys->itwp=(O_WRK *)(sys->endp-4);eff[0].lkwkp=(unsigned char *)sys->itwp;
    REJECT(DRAW(),"parent");
    reset();sys->obwp=&arena.actor;arena.actor=eff[0];arena.actor.mtx=(NJS_MATRIX *)arena.actor.mtxbuf;
    sys->ef_mdf[0]=&arena.actor;DRAW();check(triangles[0]==1&&triangles[1]==1&&composites==1);
    reset();eff[0].mdlver=1;REJECT(DRAW(),"model object");
    reset();eff[0].mtx=(NJS_MATRIX *)1;REJECT(DRAW(),"matrix");
    reset();eff[0].sx=NAN;REJECT(DRAW(),"transform");
    reset();arena.v[0]=41;REJECT(DRAW(),"command");
    reset();arena.v[0]=32;REJECT(DRAW(),"command"); /* SH4 XYZW cannot use XYZ stride. */
    reset();put16(arena.v+6,65535);REJECT(DRAW(),"extent");
    reset();put16(arena.p+6,3);REJECT(DRAW(),"index");check(!triangles[0]);
    reset();putf(arena.v+32,NAN);REJECT(DRAW(),"non-finite");check(!triangles[0]);
    reset();arena.model.vlist=NULL;REJECT(DRAW(),"shared shadow");
    reset();Ps2ShadowStart();REJECT(Ps2ShadowStart(),"nested");
    reset();REJECT(Ps2ShadowMain0(),"missing");
    reset();DRAW();check(starts==1&&ends==1); /* Fault recovery resets the protocol. */
    printf("original modifier shadows: %u checks passed\n",checks);return 0;
}
