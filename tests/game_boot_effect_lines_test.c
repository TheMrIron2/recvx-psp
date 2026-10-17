#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_effect_lines.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaGraphics3D.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"

SYS_WORK state, *sys=&state;
BH_PWORK player, *plp=&player, ene[128];
O_WRK eff[512];
unsigned char *freemem;
static NJS_MATRIX arena_matrix, captured[8];
static RecvxGameChunkVertex lines[8][2];
static unsigned int emitted, checks;
static int blend[2], draw_blend[8][2], trans[8];
static jmp_buf jump;
static int expecting;
static const char *fault;
#define CHECK(x) do { assert(x);++checks; } while(0)
#define NEAR(a,b) CHECK(fabsf((a)-(b))<0.0001f)
#define REJECT(call,text) do { expecting=1;fault=NULL; \
    if(!setjmp(jump)){call;assert(!"expected line fault");} \
    expecting=0;CHECK(fault&&strstr(fault,text)); }while(0)

void recvxGameBootUnsupported(const char *name)
{
    if(!expecting){fprintf(stderr,"unexpected line effect fault: %s\n",name);abort();}
    fault=name;longjmp(jump,1);
}
void recvxGameBootEffectDrawQueuePrepare(O_WRK **objects,int count)
{
    if(objects!=sys->ef_lin||count<0||count>512)recvxGameBootUnsupported("invalid line queue");
    for(int i=0;i<count;++i)if(objects[i]!=&eff[0])recvxGameBootUnsupported("invalid line actor");
}
void recvxGameBootRenderLine(const RecvxGameChunkVertex input[2],int transparent)
{
    CHECK(emitted<8); njGetMatrix(&captured[emitted]);
    memcpy(lines[emitted],input,sizeof(lines[emitted]));
    draw_blend[emitted][0]=blend[0];draw_blend[emitted][1]=blend[1];trans[emitted]=transparent;++emitted;
}
void njColorBlendingMode(Int target,Int mode){CHECK(target==0||target==1);blend[target]=mode;}
void njTranslateEx(NJS_VECTOR *p){njTranslate(NULL,p->x,p->y,p->z);}
static void reset(void)
{
    memset(&state,0,sizeof(state));memset(eff,0,sizeof(eff));
    freemem=(unsigned char *)&arena_matrix;sys->endp=freemem+sizeof(arena_matrix);
    recvxEngineOriginalFaultReset();recvxEngineOriginalMatrixReset();
    eff[0].mtx=(NJS_MATRIX *)eff[0].mtxbuf;eff[0].pn=1;
    eff[0].px=10;eff[0].py=20;eff[0].pz=30;eff[0].az=0x4000;
    eff[0].sx=2;eff[0].sy=3;eff[0].sz=4;
    eff[0].pv[0]=(NJS_POLYGON_VTX){.x=1,.y=0,.z=0,.col=0x80402010};
    eff[0].pv[1]=(NJS_POLYGON_VTX){.x=0,.y=1,.z=0,.col=0xff123456};
    eff[0].bl_src=8;eff[0].bl_dst=10;sys->ef_lin[0]=&eff[0];
    emitted=0;blend[0]=8;blend[1]=6;njUnitMatrix(NULL);njTranslate(NULL,100,0,0);
}
#define DRAW() bhDrawLinEffect((unsigned int *)sys->ef_lin,1)

int main(void)
{
    reset();NJS_MATRIX before;njGetMatrix(&before);DRAW();
    CHECK(emitted==1&&trans[0]);CHECK(lines[0][0].color==0x80402010&&lines[0][1].color==0xff123456);
    CHECK(draw_blend[0][0]==8&&draw_blend[0][1]==10&&blend[0]==8&&blend[1]==6);
    NJS_POINT3 p={1,0,0},q;njCalcPoint(&captured[0],&p,&q);NEAR(q.x,110);NEAR(q.y,22);NEAR(q.z,30);
    p=(NJS_POINT3){0,1,0};njCalcPoint(&captured[0],&p,&q);NEAR(q.x,107);NEAR(q.y,20);NEAR(q.z,30);
    NEAR((*eff[0].mtx)[12],10);NEAR((*eff[0].mtx)[13],20); /* Saved actor matrix excludes scale and view. */
    CHECK(!memcmp(pNaMatMatrixStuckPtr,&before,sizeof(before)));CHECK(!recvxEngineOriginalFault());
    reset();eff[0].flg=0x1000000;eff[0].pn=999;eff[0].mtx=NULL;DRAW();CHECK(!emitted);
    reset();eff[0].stflg=0x1000000;DRAW();CHECK(!emitted);
    reset();sys->gm_flg=0x4000;eff[0].mdflg=0x40;DRAW();CHECK(!emitted);
    reset();eff[0].pn=2;REJECT(DRAW(),"segment count");CHECK(!emitted);
    reset();eff[0].pv[1].z=NAN;REJECT(DRAW(),"coordinate");CHECK(!emitted);
    reset();eff[0].px=INFINITY;REJECT(DRAW(),"coordinate");
    reset();eff[0].mtx=(NJS_MATRIX *)1;REJECT(DRAW(),"matrix");
    reset();eff[0].bl_dst=12;REJECT(DRAW(),"blend");
    reset();eff[0].mtx=&arena_matrix;DRAW();CHECK(emitted==1);
    reset();REJECT(bhDrawLinEffect((unsigned int *)sys->ef_lin,-1),"queue");
    reset();sys->ef_lin[0]=(O_WRK *)1;REJECT(DRAW(),"actor");
    reset();NJS_POINT3 positions[10];NJS_COLOR colors[10];
    for(unsigned int i=0;i<10;++i){positions[i]=(NJS_POINT3){i,2*i,3*i};colors[i].color=0x11223300u+i;}
    NJS_POINT3COL points={.p=positions,.col=colors,.num=1};
    njDrawLine3D(&points,3,2);CHECK(emitted==3&&!trans[0]);
    CHECK(lines[0][0].x==0&&lines[0][1].x==1&&lines[1][0].x==1&&lines[2][1].x==3);
    emitted=0;njDrawLine3D(&points,5,0x40);CHECK(emitted==5&&trans[4]);
    CHECK(lines[4][0].x==8&&lines[4][1].color==0x11223309);
    emitted=0;njDrawLine3D(NULL,0,0);CHECK(!emitted);
    REJECT(njDrawLine3D(&points,6,0),"segment count");
    REJECT(njDrawLine3D(&points,10,2),"segment count");
    REJECT(njDrawLine3D(&points,1,0x80000000u),"textured");
    REJECT(njDrawLine3D(&points,1,4),"attributes");
    positions[3].z=NAN;REJECT(njDrawLine3D(&points,2,0),"coordinate");CHECK(!emitted);
    points.col=NULL;REJECT(njDrawLine3D(&points,1,0),"vertices");
    printf("PASS: original line effects: %u checks for transforms, vertex colors, blending, visibility, pair/strip topology and checked counts\n",checks);
    return 0;
}
