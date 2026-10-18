#include <assert.h>
#include "ps2/veronica/prog/ps2_NinjaPtcl.h"
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot_effect_sprites.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

SYS_WORK *sys;
CAM_WORK cam;
BH_PWORK *plp, ene[128];
O_WRK eff[512];
unsigned char *freemem;
static SYS_WORK state;
static BH_PWORK player;
static struct { O_WORK pose[2]; NJS_TEXLIST list; NJS_TEXTURE_VTX vertices[256]; } arena;
static NJS_MATRIX captured[8];
static RecvxGameChunkVertex triangles[8][3];
static unsigned int checks, emitted, filter, selected, global;
static int blend[2], captured_blend[8][2], was_textured[8], was_trans[8];
static NJS_TEXLIST *list;
static jmp_buf jump;
static const char *fault;
static void check(int condition) { ++checks; assert(condition); }
void recvxGameBootUnsupported(const char *name) { fault=name; longjmp(jump,1); }
void recvxGameBootEffectDrawQueuePrepare(O_WRK **objects, int count)
{
    check(objects == sys->ef_trs || objects == sys->ef_ntx || objects == sys->ef_opq);
    if (count < 0 || count > 512) recvxGameBootUnsupported("queue count");
    for (int i=0;i<count;++i) if (objects[i] != &eff[0]) recvxGameBootUnsupported("queue actor");
}
void recvxGameBootRenderSpriteTriangle(const RecvxGameChunkVertex input[3], int textured, int trans)
{
    check(emitted < 8); njGetMatrix(&captured[emitted]);
    memcpy(triangles[emitted],input,sizeof(triangles[emitted]));
    captured_blend[emitted][0]=blend[0]; captured_blend[emitted][1]=blend[1];
    was_textured[emitted]=textured; was_trans[emitted]=trans; ++emitted;
}
void njTextureFilterMode(Int mode) { filter=mode; }
Sint32 njSetTexture(NJS_TEXLIST *value) { list=value; return 0; }
Sint32 njSetTextureNum(Uint32 number) { selected=number; return 0; }
Sint32 njSetTextureNumG(Uint32 index) { global=index; return 0; }
void njColorBlendingMode(Int target, Int mode) { check(target==0 || target==1); blend[target]=mode; }
void njTranslateEx(NJS_VECTOR *p) { njTranslate(NULL,p->x,p->y,p->z); }
void njProjectScreen(NJS_MATRIX *matrix, NJS_POINT3 *input, NJS_POINT2 *output)
{ NJS_POINT3 v; njCalcPoint(matrix,input,&v); output->x=v.x/(v.z+100); output->y=v.y/(v.z+100); }
float recvxEngineOriginalViewProjectionDistance(void){return 320;}
void recvxEngineOriginalViewAspect(float *x,float *y){*x=2;*y=1;}
static void reset(void)
{
    memset(&state,0,sizeof(state)); memset(eff,0,sizeof(eff)); memset(&arena,0,sizeof(arena));
    memset(&player,0,sizeof(player)); sys=&state; plp=&player;
    freemem=(unsigned char *)&arena; sys->endp=freemem+sizeof(arena);
    recvxEngineOriginalFaultReset(); recvxEngineOriginalMatrixReset(); njUnitMatrix(NULL);
    eff[0].mtx=(NJS_MATRIX *)eff[0].mtxbuf;
    eff[0].px=1; eff[0].py=2; eff[0].pz=100;
    eff[0].sx=2; eff[0].sy=3; eff[0].sz=4;
    eff[0].tvp=eff[0].tv; eff[0].pvp=eff[0].pv; eff[0].pn=4;
    eff[0].bl_src=8; eff[0].bl_dst=10; eff[0].tex_id=2; eff[0].ani_ct=1;
    for (unsigned int i=0;i<4;++i) {
        eff[0].tv[i]=(NJS_TEXTURE_VTX){.x=(float)(i&1),.y=(float)(i>>1),.z=1,
            .u=(float)(i&1),.v=(float)(i>>1),.col=0x80402010u+i};
        eff[0].pv[i]=(NJS_POLYGON_VTX){.x=(float)(i&1),.y=(float)(i>>1),.z=1,.col=0xC0102030u+i};
    }
    sys->ef_tn[2]=7; sys->ef_tlist.nbTexture=16;
    sys->ef_trs[0]=sys->ef_ntx[0]=sys->ef_opq[0]=&eff[0];
    emitted=0; filter=selected=global=99; list=NULL; fault=NULL; blend[0]=8;blend[1]=6;
}
#define DRAW(name) name((unsigned int *)sys->ef_trs,1)
#define REJECT(call, message) do { fault=NULL; if (!setjmp(jump)) { call;check(0); } \
    check(fault && strstr(fault,message)); } while(0)

int main(void)
{
    reset(); DRAW(bhDrawTrsEffect3D);
    check(emitted==2 && filter==1 && selected==8 && list==&sys->ef_tlist);
    check(captured[0][0]==2 && captured[0][5]==3 && captured[0][10]==4);
    check(captured[0][12]==1 && captured[0][13]==2 && captured[0][14]==100);
    check(captured_blend[0][0]==8 && captured_blend[0][1]==10 && blend[0]==8 && blend[1]==6);
    check(was_textured[0] && was_trans[0]);
    check(triangles[0][0].color==0x80402010 && triangles[0][1].u==1 && triangles[0][2].v==1);
    check(triangles[1][0].color==0x80402012 && triangles[1][1].color==0x80402011 && triangles[1][2].color==0x80402013);
    reset(); DRAW(bhDrawNtxEffect3D); check(emitted==2 && !was_textured[0] && was_trans[0]);
    check(triangles[0][0].color==0xC0102030 && captured_blend[0][1]==10);
    reset(); DRAW(bhDrawOpqEffect3D); check(emitted==2 && captured_blend[0][1]==6 && was_trans[0]);
    reset(); eff[0].flg=0x20000000; DRAW(bhDrawTrsEffect3D); check(filter==0);
    reset(); eff[0].flg=0x800000; eff[0].gidx=42; DRAW(bhDrawTrsEffect3D); check(global==42 && !list);
    reset(); arena.list.nbTexture=5; eff[0].stflg=0x20; eff[0].txp[0]=&arena.list;
    DRAW(bhDrawTrsEffect3D); check(selected==3 && list==&arena.list);
    reset(); eff[0].flg=0x1000000; eff[0].tvp=NULL; eff[0].mtx=NULL;
    DRAW(bhDrawTrsEffect3D); check(!emitted);
    reset(); eff[0].stflg=0x1000000; DRAW(bhDrawNtxEffect3D); check(!emitted);
    reset(); sys->gm_flg=0x4000; eff[0].mdflg=0x40; DRAW(bhDrawOpqEffect3D); check(!emitted);
    reset(); eff[0].tex_id=-1; eff[0].tvp=NULL; DRAW(bhDrawTrsEffect3D); check(!emitted);
    reset(); eff[0].id=9; eff[0].type=2; plp->mdflg=1; DRAW(bhDrawTrsEffect3D); check(!emitted);
    reset(); eff[0].id=9; eff[0].type=2; plp->stflg=0x1000000; eff[0].tvp=NULL;
    DRAW(bhDrawTrsEffect3D); check(!emitted);
    reset(); eff[0].id=9; eff[0].type=2; plp=NULL; DRAW(bhDrawOpqEffect3D); check(emitted==2);
    reset(); eff[0].id=9; eff[0].type=2; plp->mdflg=1; sys->sp_flg=8; DRAW(bhDrawTrsEffect3D); check(emitted==2);
    reset(); eff[0].flg=0x80; eff[0].lkwkp=(unsigned char *)&eff[1]; eff[0].lkono=1;
    eff[1].mlwP=&eff[1].mdl[0]; eff[1].mlwP->obj_num=2; eff[1].mlwP->owP=arena.pose;
    njUnitMatrix(&arena.pose[1].mtx); arena.pose[1].mtx[14]=50;
    eff[0].lox=4; eff[0].loy=5; eff[0].loz=6;
    DRAW(bhDrawTrsEffect3D); check(emitted==2 && captured[0][12]==4 && captured[0][13]==5 && captured[0][14]==56);
    reset(); eff[0].flg=0x100000; eff[0].ay=16384; DRAW(bhDrawTrsEffect3D);
    check(fabsf(captured[0][0]-2)<1e-6f && fabsf(captured[0][10]-4)<1e-6f);
    reset(); eff[0].flg=0x20000; DRAW(bhDrawTrsEffect3D); check(emitted==2 && !(eff[0].stflg&0x10));
    reset(); eff[0].flg=0x20000; eff[0].sx=-2; DRAW(bhDrawTrsEffect3D); check(!emitted && (eff[0].stflg&0x10));
    reset(); eff[0].pz=-100; DRAW(bhDrawTrsEffect3D); check(!emitted);
    reset(); eff[0].id=128; eff[0].pz=-100; DRAW(bhDrawTrsEffect3D); check(emitted==2);
    reset(); eff[0].id=204; eff[0].pz=-100; DRAW(bhDrawOpqEffect3D); check(emitted==2);
    reset(); eff[0].tvp=NULL; REJECT(DRAW(bhDrawTrsEffect3D),"vertices");
    reset(); eff[0].pn=5; REJECT(DRAW(bhDrawTrsEffect3D),"vertices");
    reset(); eff[0].pn=22; REJECT(DRAW(bhDrawTrsEffect3D),"count");
    reset(); eff[0].tex_id=450; REJECT(DRAW(bhDrawTrsEffect3D),"animation");
    reset(); eff[0].ani_ct=9; REJECT(DRAW(bhDrawTrsEffect3D),"animation");
    reset(); eff[0].stflg=0x20; eff[0].txp[0]=(NJS_TEXLIST *)1; REJECT(DRAW(bhDrawTrsEffect3D),"animation");
    reset(); eff[0].flg=0x80; REJECT(DRAW(bhDrawNtxEffect3D),"parent model");
    reset(); eff[0].sx=NAN; REJECT(DRAW(bhDrawOpqEffect3D),"transform");
    reset(); eff[0].tv[3].u=NAN; REJECT(DRAW(bhDrawTrsEffect3D),"non-finite"); check(!emitted);
    reset(); eff[0].mtx=(NJS_MATRIX *)1; REJECT(DRAW(bhDrawNtxEffect3D),"matrix");
    reset(); njDrawTexture3DEx(eff[0].tv,4,0); check(emitted==2 && !was_trans[0]);
    reset(); njDrawPolygon3DEx(eff[0].pv,4,1); check(emitted==2 && !was_textured[0]);
    reset(); REJECT(njDrawTexture3DEx1P(arena.vertices,257,1),"submission");
    reset();
    NJS_MATRIX before_particle,after_particle;
    njUnitMatrix(NULL);njTranslate(NULL,3,4,50);njGetMatrix(&before_particle);
    NJS_POINT3 particles[2]={{1,2,3},{-1,-2,-100}};
    njPtclSpriteStart(7,0x80abcdefu,1);njPtclDrawSprite(particles,2,2,3);njPtclSpriteEnd();
    check(emitted==2&&selected==7&&triangles[0][0].color==0x80abcdefu);
    check(triangles[0][0].x==2&&triangles[0][0].y==3&&triangles[0][0].z==53);
    check(triangles[0][1].x==6&&triangles[0][2].y==9);
    njGetMatrix(&after_particle);check(!memcmp(before_particle,after_particle,sizeof(before_particle)));
    REJECT(njPtclDrawSprite(particles,1,1,1),"particle");
    njPtclSpriteStart(1,0xffffffffu,0);
    REJECT(njPtclDrawSprite(particles,257,1,1),"particle");njPtclSpriteEnd();
    reset();njUnitMatrix(NULL);njTranslate(NULL,0,0,320);
    NJS_POINT3 dots[2]={{0,0,0},{0,0,320}};
    njPtclPolygonStart(0xff112233u);njPtclDrawPolygon(dots,2,2);njPtclPolygonEnd();
    check(emitted==4&&!was_textured[0]&&was_trans[0]);
    check(triangles[0][0].x==-1&&triangles[0][0].y==-2&&triangles[0][0].z==320);
    check(triangles[2][0].x==-2&&triangles[2][0].y==-4&&triangles[2][0].z==640);
    check(triangles[0][0].color==0xff112233u);
    REJECT(njPtclDrawPolygon(dots,1,1),"polygon particle");
    printf("original 3D effect sprites: %u checks passed\n",checks);
    return 0;
}
