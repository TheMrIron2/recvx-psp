#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_effect_thl.h"
#include "psp/game_boot_effect_sprites.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

SYS_WORK state,*sys=&state;
CAM_WORK cam;
BH_PWORK player,*plp=&player,ene[128];
O_WRK eff[512];
unsigned char *freemem;
static struct { NJS_TEXLIST list; O_WORK joint[2]; NJS_TEXTUREH_VTX vertices[4]; } data;
static unsigned int checks,emitted,offsets[4],filter,selected;
static RecvxGameChunkVertex triangles[4][3];
static NJS_MATRIX captured[4];
static int blend[2],draw_blend[4][2],trans[4];
static NJS_TEXLIST *texture;
static jmp_buf jump;
static int expecting;
static const char *fault;
#define CHECK(x) do{assert(x);++checks;}while(0)
#define NEAR(a,b) CHECK(fabsf((a)-(b))<0.0001f)
#define REJECT(call,text) do{expecting=1;fault=NULL; \
    if(!setjmp(jump)){call;assert(!"expected half-lit effect fault");} \
    expecting=0;CHECK(fault&&strstr(fault,text));}while(0)

void recvxGameBootUnsupported(const char *name)
{
    if(!expecting){fprintf(stderr,"unexpected half-lit effect fault: %s\n",name);abort();}
    fault=name;longjmp(jump,1);
}
void recvxGameBootEffectDrawQueuePrepare(O_WRK **objects,int count)
{
    if(objects!=sys->ef_thl||count<0||count>512)recvxGameBootUnsupported("invalid half-lit queue");
    for(int i=0;i<count;++i)if(objects[i]!=&eff[0])recvxGameBootUnsupported("invalid half-lit actor");
}
void recvxGameBootRenderHalfFogTriangle(const RecvxGameChunkVertex input[3],unsigned int offset,int transparent)
{
    CHECK(emitted<4);njGetMatrix(&captured[emitted]);
    memcpy(triangles[emitted],input,sizeof(triangles[emitted]));offsets[emitted]=offset;
    draw_blend[emitted][0]=blend[0];draw_blend[emitted][1]=blend[1];trans[emitted]=transparent;++emitted;
}
void njTextureFilterMode(Int mode){filter=mode;}
Sint32 njSetTexture(NJS_TEXLIST *list){texture=list;return 0;}
Sint32 njSetTextureNum(Uint32 index){selected=index;return 0;}
void njColorBlendingMode(Int target,Int mode){CHECK(target==0||target==1);blend[target]=mode;}
void njTranslateEx(NJS_VECTOR *p){njTranslate(NULL,p->x,p->y,p->z);}
void njProjectScreen(NJS_MATRIX *matrix,NJS_POINT3 *input,NJS_POINT2 *output)
{NJS_POINT3 v;njCalcPoint(matrix,input,&v);output->x=v.x/(v.z+100);output->y=v.y/(v.z+100);}
void recvxGameBootEffectSpriteNormal(NJS_POINT3 *a,NJS_POINT3 *b,NJS_POINT3 *c,NJS_VECTOR *n)
{n->x=(b->y-a->y)*(c->z-b->z)-(b->z-a->z)*(c->y-b->y);
 n->y=(b->z-a->z)*(c->x-b->x)-(b->x-a->x)*(c->z-b->z);
 n->z=(b->x-a->x)*(c->y-b->y)-(b->y-a->y)*(c->x-b->x);}
static NJS_TEXTUREH_VTX *inline_vertices(void){return (NJS_TEXTUREH_VTX *)eff[0].pv;}
static void reset(void)
{
    memset(&state,0,sizeof(state));memset(eff,0,sizeof(eff));memset(&data,0,sizeof(data));memset(&cam,0,sizeof(cam));
    freemem=(unsigned char *)&data;sys->endp=freemem+sizeof(data);
    recvxEngineOriginalFaultReset();recvxEngineOriginalMatrixReset();njUnitMatrix(NULL);
    eff[0].mtx=(NJS_MATRIX *)eff[0].mtxbuf;eff[0].pn=4;eff[0].tvp=(NJS_TEXTURE_VTX *)eff[0].pv;
    eff[0].px=1;eff[0].py=2;eff[0].pz=100;eff[0].sx=2;eff[0].sy=3;eff[0].sz=4;
    eff[0].bl_src=8;eff[0].bl_dst=10;eff[0].tex_id=2;eff[0].ani_ct=1;
    sys->ef_thl[0]=&eff[0];sys->ef_tn[2]=7;sys->ef_tlist.nbTexture=16;cam.vz=1;
    for(unsigned int i=0;i<4;++i)inline_vertices()[i]=(NJS_TEXTUREH_VTX){
        .x=(float)(i&1),.y=(float)(i>>1),.z=1,.u=(float)(i&1),.v=(float)(i>>1),
        .bcol=0x80402010u+i,.ocol=0x11223344u+i};
    emitted=0;filter=selected=99;texture=NULL;blend[0]=8;blend[1]=6;
}
#define DRAW() bhDrawThlEffect3D((unsigned int *)sys->ef_thl,1)

int main(void)
{
    reset();NJS_MATRIX before;njGetMatrix(&before);DRAW();
    CHECK(emitted==2&&filter==1&&texture==&sys->ef_tlist&&selected==8);
    CHECK(offsets[0]==0x11223344&&offsets[1]==0x11223344); /* First offset only, even with a gradient. */
    CHECK(triangles[0][0].color==0x80402010&&triangles[0][1].color==0x80402011);
    CHECK(triangles[1][0].color==0x80402012&&triangles[1][1].color==0x80402011&&triangles[1][2].color==0x80402013);
    CHECK(triangles[0][1].u==1&&triangles[1][2].v==1);
    CHECK(draw_blend[0][0]==8&&draw_blend[0][1]==10&&trans[0]&&blend[0]==8&&blend[1]==6);
    CHECK(captured[0][0]==2&&captured[0][5]==3&&captured[0][10]==4);
    CHECK(captured[0][12]==1&&captured[0][13]==2&&captured[0][14]==100);
    CHECK(!memcmp(pNaMatMatrixStuckPtr,&before,sizeof(before)));CHECK(!recvxEngineOriginalFault());
    reset();eff[0].bl_src=eff[0].bl_dst=0;DRAW();CHECK(emitted==2&&!trans[0]&&draw_blend[0][0]==8&&draw_blend[0][1]==6);
    reset();eff[0].flg=0x20000000u;DRAW();CHECK(filter==0);
    reset();data.list.nbTexture=4;eff[0].txp[0]=&data.list;eff[0].stflg=0x20;DRAW();CHECK(texture==&data.list&&selected==3);
    reset();eff[0].flg=0x1000000u;eff[0].tvp=NULL;eff[0].mtx=NULL;DRAW();CHECK(!emitted);
    reset();eff[0].stflg=0x1000000u;DRAW();CHECK(!emitted);
    reset();eff[0].tex_id=-1;eff[0].tvp=NULL;DRAW();CHECK(!emitted);
    reset();sys->gm_flg=0x4000;eff[0].mdflg=0x40;DRAW();CHECK(!emitted);
    reset();eff[0].flg=0x2000000u;DRAW();CHECK(offsets[0]==0xffffffffu);
    CHECK(inline_vertices()[3].ocol==0xffffffffu&&triangles[0][0].color==0x80402010);
    reset();eff[0].flg=0x100000u;eff[0].ay=0x4000;DRAW();NEAR(captured[0][0],2);NEAR(captured[0][10],4);
    reset();eff[0].flg=0x100000u;sys->gm_flg=0x4000;sys->mr_pl.vx=1;DRAW();NEAR(captured[0][0],-2);
    reset();eff[0].flg=0x20000u;DRAW();CHECK(emitted==2&&!(eff[0].stflg&0x10u));
    reset();eff[0].flg=0x20000u;eff[0].sx=-2;DRAW();CHECK(!emitted&&(eff[0].stflg&0x10u));
    reset();eff[0].flg=0x80u;eff[0].lkwkp=(unsigned char *)&eff[1];eff[0].lkono=1;
    eff[1].mlwP=&eff[1].mdl[0];eff[1].mlwP->obj_num=2;eff[1].mlwP->owP=data.joint;
    njUnitMatrix(&data.joint[1].mtx);data.joint[1].mtx[14]=50;
    eff[0].lox=4;eff[0].loy=5;eff[0].loz=6;DRAW();CHECK(captured[0][12]==4&&captured[0][13]==5&&captured[0][14]==56);
    reset();memcpy(data.vertices,inline_vertices(),sizeof(data.vertices));eff[0].tvp=(NJS_TEXTURE_VTX *)data.vertices;
    DRAW();CHECK(emitted==2);
    reset();memcpy(sys->rpb,inline_vertices(),sizeof(data.vertices));eff[0].tvp=(NJS_TEXTURE_VTX *)sys->rpb;
    DRAW();CHECK(emitted==2);
    reset();eff[0].pn=5;REJECT(DRAW(),"vertex count");
    reset();eff[0].tvp=(NJS_TEXTURE_VTX *)eff[0].tv;REJECT(DRAW(),"vertex span");
    reset();eff[0].tvp=(NJS_TEXTURE_VTX *)1;REJECT(DRAW(),"vertex span");
    reset();inline_vertices()[3].u=NAN;REJECT(DRAW(),"coordinate");CHECK(!emitted);
    reset();eff[0].sx=NAN;REJECT(DRAW(),"transform");
    reset();eff[0].mtx=(NJS_MATRIX *)1;REJECT(DRAW(),"matrix");
    reset();eff[0].bl_src=0;REJECT(DRAW(),"blend");
    reset();eff[0].tex_id=450;REJECT(DRAW(),"texture animation");
    reset();eff[0].ani_ct=9;REJECT(DRAW(),"texture animation");
    reset();eff[0].stflg=0x20;eff[0].txp[0]=(NJS_TEXLIST *)1;REJECT(DRAW(),"private texture");
    reset();eff[0].flg=0x80u;REJECT(DRAW(),"parent model");
    reset();eff[0].flg=0x2000000u;cam.vx=NAN;REJECT(DRAW(),"camera direction");
    reset();REJECT(njDrawTexture3DHEx(inline_vertices(),5,1),"submission");
    REJECT(njDrawTexture3DHEx(inline_vertices(),4,2),"submission");
    REJECT(njDrawTexture3DHEx(NULL,4,1),"vertices");
    printf("PASS: half-lit effect rendering: %u checks for authored base/offset colors, alpha/UV gradients, animation, transforms, reflection, texture selection and bounded scratch\n",checks);
    return 0;
}
