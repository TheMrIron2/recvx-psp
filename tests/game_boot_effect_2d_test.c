#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_effect_2d.h"
#include "psp/game_boot_render.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

SYS_WORK state,*sys=&state;
BH_PWORK player,*plp=&player,ene[128];
O_WRK eff[512];
unsigned char *freemem;
static NJS_TEXTURE_VTX arena[42];
static RecvxGameChunkVertex triangles[40][3];
static unsigned int checks,emitted,filter,selected;
static int blend[2],draw_blend[40][2],trans[40];
static jmp_buf jump;
static int expecting;
static const char *fault;
#define CHECK(x) do{assert(x);++checks;}while(0)
#define REJECT(call,text) do{expecting=1;fault=NULL; \
    if(!setjmp(jump)){call;assert(!"expected screen effect fault");} \
    expecting=0;CHECK(fault&&strstr(fault,text));}while(0)
void recvxGameBootUnsupported(const char *name)
{if(!expecting){fprintf(stderr,"unexpected screen effect fault: %s\n",name);abort();}fault=name;longjmp(jump,1);}
void recvxGameBootEffectDrawQueuePrepare(O_WRK **objects,int count)
{
    if(objects!=sys->ef_trs2d||count<0||count>512)recvxGameBootUnsupported("invalid screen queue");
    for(int i=0;i<count;++i)if(objects[i]!=&eff[0])recvxGameBootUnsupported("invalid screen actor");
}
void recvxGameBootRenderScreenTriangle(const RecvxGameChunkVertex input[3],int transparent)
{
    CHECK(emitted<40);memcpy(triangles[emitted],input,sizeof(triangles[emitted]));
    draw_blend[emitted][0]=blend[0];draw_blend[emitted][1]=blend[1];trans[emitted]=transparent;++emitted;
}
void njTextureFilterMode(Int mode){filter=mode;}
Sint32 njSetTextureNumG(Uint32 index)
{if(index!=12345)recvxGameBootUnsupported("global texture not loaded");selected=index;return 0;}
void njColorBlendingMode(Int target,Int mode){CHECK(target==0||target==1);blend[target]=mode;}
static void reset(void)
{
    memset(&state,0,sizeof(state));memset(eff,0,sizeof(eff));
    freemem=(unsigned char *)arena;sys->endp=freemem+sizeof(arena);
    eff[0].pn=4;eff[0].tvp=eff[0].tv;eff[0].tex_id=5;eff[0].gidx=12345;
    eff[0].bl_src=8;eff[0].bl_dst=10;sys->ef_trs2d[0]=&eff[0];
    for(unsigned int i=0;i<42;++i)arena[i]=(NJS_TEXTURE_VTX){
        .x=(float)(i&1)*640,.y=(float)(i>>1)*240,.z=(float)i-1,
        .u=(float)(i&1),.v=(float)(i>>1)*0.5f,.col=0x80402010u+i};
    memcpy(eff[0].tv,arena,sizeof(eff[0].tv));
    emitted=0;filter=selected=99;blend[0]=8;blend[1]=6;
}
#define DRAW() bhDrawTrsEffect2D((unsigned int *)sys->ef_trs2d,1)
int main(void)
{
    reset();DRAW();CHECK(emitted==2&&selected==12345&&filter==1);
    CHECK(draw_blend[0][0]==8&&draw_blend[0][1]==10&&trans[0]&&blend[0]==8&&blend[1]==6);
    CHECK(triangles[0][0].x==0&&triangles[0][1].x==640&&triangles[1][2].y==240);
    CHECK(triangles[0][0].color==0x80402010&&triangles[0][1].color==0x80402011);
    CHECK(triangles[1][0].color==0x80402012&&triangles[1][1].color==0x80402011&&triangles[1][2].color==0x80402013);
    CHECK(triangles[0][0].z==1&&triangles[0][1].z==1&&triangles[1][2].z==2);
    CHECK(eff[0].tv[0].z==1&&eff[0].tv[1].z==1&&eff[0].tv[3].z==2);
    CHECK(triangles[0][1].u==1&&triangles[0][2].v==0.5f);
    reset();eff[0].flg=0x20000000u;DRAW();CHECK(filter==0);
    reset();eff[0].flg=0x1000000u;eff[0].tvp=NULL;eff[0].pn=100;DRAW();CHECK(!emitted&&selected==99);
    reset();eff[0].stflg=0x1000000u;DRAW();CHECK(!emitted);
    reset();eff[0].tex_id=-1;eff[0].tvp=NULL;DRAW();CHECK(!emitted);
    reset();sys->gm_flg=0x4000;eff[0].mdflg=0x40;DRAW();CHECK(!emitted);
    reset();eff[0].tvp=arena;eff[0].pn=42;DRAW();CHECK(emitted==40&&triangles[39][2].z==40);
    reset();eff[0].tvp=(NJS_TEXTURE_VTX *)eff[0].pv;memcpy(eff[0].pv,arena,4*sizeof(*arena));
    DRAW();CHECK(emitted==2);
    reset();eff[0].pn=43;REJECT(DRAW(),"vertex count");
    reset();eff[0].pn=2;REJECT(DRAW(),"vertex count");
    reset();eff[0].pn=5;REJECT(DRAW(),"vertex span");
    reset();eff[0].tvp=(NJS_TEXTURE_VTX *)1;REJECT(DRAW(),"vertex span");
    reset();eff[0].tv[3].u=NAN;REJECT(DRAW(),"coordinate");CHECK(!emitted&&eff[0].tv[0].z==-1);
    reset();eff[0].bl_src=0;REJECT(DRAW(),"blend");
    reset();eff[0].gidx=3;REJECT(DRAW(),"global texture");CHECK(!emitted&&eff[0].tv[0].z==-1);
    reset();njDrawTextureSub(arena,3,0);CHECK(emitted==1&&!trans[0]);
    reset();REJECT(njDrawTextureSub(arena,43,0),"submission");
    REJECT(njDrawTextureSub(arena,4,2),"submission");
    REJECT(njDrawTextureSub(NULL,4,0),"vertices");
    arena[3].y=INFINITY;REJECT(njDrawTextureSub(arena,4,0),"coordinate");CHECK(!emitted&&arena[0].z==-1);
    printf("PASS: screen-space effect rendering: %u checks for authored coordinates, depth mutation, alpha/color/UV gradients, global texture, strip winding and 42-vertex capacity\n",checks);
    return 0;
}
