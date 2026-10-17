#include <assert.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/hitchk.h"
#include "ps2/veronica/prog/main.h"

SYS_WORK *sys;
ROM_WORK *rom;
BH_PWORK ply, ene[128], *plp;
O_WRK eff[512];
CAM_WORK cam;
unsigned char *freemem;
static SYS_WORK state;
static ROM_WORK room;
static ATR_WORK walls[4];
static jmp_buf failure;
static const char *fault;
static int effect_slot, effects;
void recvxGameBootUnsupported(const char *name) { fault=name; longjmp(failure,1); }
int bhSetEffect(int id, POINT *point, unsigned char *link, int joint)
{ assert(id==29 && point==(POINT *)&ply.px && link==&walls[0].flg && !joint); ++effects; return effect_slot; }
#define REJECT(call, text) do { fault=NULL; if(!setjmp(failure)){call;assert(!"expected actor wall fault");} assert(fault&&strstr(fault,text)); } while(0)
static int near(float a,float b) { return fabsf(a-b)<0.002f; }
static void reset(void)
{
    memset(&state,0,sizeof(state)); memset(&room,0,sizeof(room)); memset(&ply,0,sizeof(ply));
    memset(ene,0,sizeof(ene)); memset(eff,0,sizeof(eff)); memset(walls,0,sizeof(walls));
    sys=&state; rom=&room; plp=&ply; freemem=(unsigned char *)walls; sys->endp=freemem+sizeof(walls);
    room.walp=walls; room.h=3; room.grand[2]=-2; ply.ar=0.25f; ply.ah=1;
    ply.px=0.1f; ply.pz=1; effect_slot=-1; effects=0;
}
static ATR_WORK *wall(int type)
{
    room.wal_n=1; walls[0]=(ATR_WORK){.flg=1,.type=type,.py=-1,.w=2,.d=2,.h=3};
    return walls;
}
int main(void)
{
    reset(); wall(0); bhCheckWall(&ply); assert(near(ply.px,-0.25f) && (ply.stflg&1));
    reset(); wall(0); ply.px=1.9f; bhCheckWall(&ply); assert(near(ply.px,2.25f));
    reset(); wall(0); ply.px=1; ply.pz=0.1f; bhCheckWall(&ply); assert(near(ply.pz,-0.25f));
    reset(); wall(0); ply.px=1; ply.pz=1.9f; bhCheckWall(&ply); assert(near(ply.pz,2.25f));
    reset(); wall(0); ply.px=ply.pz=-0.1f; bhCheckWall(&ply);
    assert(near(hypotf(ply.px,ply.pz),0.25f));
    reset(); wall(1); ply.flg=0x400; bhCheckWall(&ply); assert(near(ply.px,0.1f) && !(ply.stflg&1));
    reset(); wall(0)->attr=1; walls[0].flr_no=1; bhCheckWall(&ply); assert(!(ply.stflg&1));
    reset(); wall(0)->attr=2; ply.flg=0x4000; bhCheckWall(&ply); assert(!(ply.stflg&1));
    reset(); wall(0)->attr=4; ply.stflg=0x40000000; bhCheckWall(&ply); assert(!(ply.stflg&1));
    reset(); wall(0)->h=0; bhCheckWall(&ply); assert(near(ply.px,-0.25f));
    reset(); wall(0); ply.py=4; bhCheckWall(&ply); assert(!(ply.stflg&1));
    reset(); wall(0); state.mwalp[0]=walls[0]; state.mwal_n=1; room.wal_n=0;
    bhCheckWall(&ply); assert(near(ply.px,-0.25f));
    reset(); ply.flg=0x100; bhCheckWall(&ply); assert(ply.py==-2);
    reset(); ply.py=1.2345f; bhCheckWall(&ply); assert(near(ply.py,1.23f));
    reset(); wall(2); ply.px=-0.1f; ply.pz=0; bhCheckWall(&ply);
    assert(near(hypotf(ply.px,ply.pz),2.25f));
    reset(); wall(6); bhCheckWall(&ply);
    assert(!(ply.stflg&1)); /* Slopes only affect floor-following actors. */
    reset(); wall(6); ply.flg=0x100; bhCheckWall(&ply);
    assert(isfinite(ply.py) && (ply.stflg&1));
    reset(); wall(2)->attr=0x40; ply.stflg=0x40000000; ply.px=-0.1f; ply.pz=0;
    effect_slot=7; eff[7].flg=0x80; bhCheckWall(&ply);
    assert(effects==1 && !(eff[7].flg&0x80) && !(walls[0].attr&0x40) && (ply.flg&0x10004)==0x10004 && (state.st_flg&4));
    assert(ply.mode0==2 && !ply.mode2 && !ply.mode3);
    reset(); wall(2)->attr=0x40; ply.stflg=0x40000000; ply.px=-0.1f; ply.pz=0;
    effect_slot=512; REJECT(bhCheckWall(&ply),"effect slot");
    reset(); wall(6)->id=4; REJECT(bhCheckWall(&ply),"wall slope");
    reset(); wall(0)->py=1e30f; REJECT(bhCheckWall(&ply),"quantized height");
    reset(); ply.py=1e30f; REJECT(bhCheckWall(&ply),"quantized height");
    reset(); ply.py=21474836.0f; REJECT(bhCheckWall(&ply),"quantized height");
    reset(); ply.ah=NAN; REJECT(bhCheckWall(&ply),"collision query");
    reset(); ply.flg=0x100; ply.flr_no=127; REJECT(bhCheckWall(&ply),"ground floor");
    reset(); state.mwal_n=65; REJECT(bhCheckWall(&ply),"dynamic wall count");
    reset(); room.grand[12]=NAN; REJECT(bhCheckWall(&ply),"floor height");
    reset(); REJECT(bhCheckWall(NULL),"wall actor");
    reset(); REJECT(bhCheckWall((BH_PWORK *)(uintptr_t)1),"wall actor");
    reset(); BH_PWORK fake={0}; REJECT(bhCheckWall(&fake),"wall actor");
    puts("PASS: retail actor wall response for box sides/corners, type/floor/flags/height gates, dynamic walls, ground snap/quantization, cylinder push/effect state and malformed inputs");
    return 0;
}
