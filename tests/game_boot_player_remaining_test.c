#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_player_remaining.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/sub1.h"

SYS_WORK *sys;
BH_PWORK *plp, ene[128];
ROM_WORK *rom;
CAM_WORK cam;
S_WORK swork;
unsigned char *freemem;
static SYS_WORK system_state;
static BH_PWORK player;
static ROM_WORK room;
static unsigned char memory[65536] __attribute__((aligned(64)));
static jmp_buf failure;
static const char *fault;
static unsigned int rejections;
size_t recvxGameBootSourceRemaining(const void *pointer) { (void)pointer; return 0; }
void recvxGameBootLog(const char *format, ...) { (void)format; }
void recvxGameBootUnsupported(const char *reason) { fault=reason; longjmp(failure,1); }
#define REJECT(call, expected) do { fault=NULL; \
    if (!setjmp(failure)) { call; assert(!"expected player-state rejection"); } \
    assert(fault && !strcmp(fault,expected)); ++rejections; } while(0)
/* Other retained event-table callbacks are covered with real services by
   the full ABI test. This fixture stops if they escape the parked path. */
const unsigned short PlMtnAct[2][3][7]={0};
const char PlFootSnd[4][2][7]={0};
void CallPlayerFootStepSe(int floor,int type,int flag)
{ (void)floor;(void)type;(void)flag;recvxGameBootUnsupported("unexpected footstep"); }
void bhAddSpeed(BH_PWORK *actor,int angle)
{ (void)actor;(void)angle;recvxGameBootUnsupported("unexpected speed"); }
int bhCheckFloorSound(BH_PWORK *actor,int floor,float x,float z)
{ (void)actor;(void)floor;(void)x;(void)z;recvxGameBootUnsupported("unexpected floor sound");return 0; }
ATR_WORK *bhCheckWater(NJS_POINT3 *position)
{ (void)position;recvxGameBootUnsupported("unexpected water query");return NULL; }
int bhGetFrameNum(unsigned int a,unsigned int b,int frame)
{ (void)a;(void)b;(void)frame;recvxGameBootUnsupported("unexpected frame conversion");return 0; }
float bhGetGroundPosition(NJS_POINT3 *position)
{ (void)position;recvxGameBootUnsupported("unexpected ground query");return 0; }
int bhSetEffect(int id,POINT *point,unsigned char *link,int joint)
{ (void)id;(void)point;(void)link;(void)joint;recvxGameBootUnsupported("unexpected effect");return 0; }
int bhSetEffectTb(EF_WORK *effect,NJS_POINT3 *offset,unsigned char *link,int joint)
{ (void)effect;(void)offset;(void)link;(void)joint;recvxGameBootUnsupported("unexpected staged effect");return 0; }
int bhSub_DirTarget(BH_PWORK *actor,float x,float z,int angle)
{ (void)actor;(void)x;(void)z;(void)angle;recvxGameBootUnsupported("unexpected direction");return 0; }
static EXP_WORK *setup(void)
{
    memset(&system_state,0,sizeof(system_state)); memset(&player,0,sizeof(player));
    memset(&room,0,sizeof(room)); memset(&cam,0,sizeof(cam)); memset(ene,0,sizeof(ene));
    memset(memory,0,sizeof(memory)); sys=&system_state; plp=&player; rom=&room;
    freemem=memory; sys->endp=memory+sizeof(memory);
    plp->exp0=memory; plp->exp1=memory+256;
    plp->mlwP=plp->mdl; plp->mlwP->obj_num=22;
    plp->mlwP->objP=(NJS_CNK_OBJECT *)(memory+1024);
    plp->mlwP->owP=(O_WORK *)(memory+4096);
    plp->mnwP=(MN_WORK *)(memory+8192); plp->mnwP[42].frm_num=64;
    sys->pl_htp=(ATR_WORK *)(memory+32000);
    rom->cutp=(CUT_WORK *)(memory+33000); rom->cut_n=1;
    return (EXP_WORK *)plp->exp0;
}
int main(void)
{
    EXP_WORK *e=setup();
    /* The parked controller uses a script-supplied animation bank. Give it a
       bounded five-frame fixture and verify the last-frame hold. */
    plp->mnwP[218].frm_num=5; bhCPM0_nothing();
    assert(plp->mtn_no==218 && plp->mode3==1 && plp->mtn_add==65536);
    plp->frm_no=4*65536; bhCPM0_nothing();
    assert(plp->frm_no==4*65536 && !plp->mtn_add);
    setup(); recvxGameBootPlayerRemainingPrepare(0);
    plp->mode1=2; REJECT(recvxGameBootPlayerRemainingPrepare(0),"invalid player damage selector");
    setup(); plp->mode2=4; REJECT(recvxGameBootPlayerRemainingPrepare(0),"invalid player damage selector");
    setup(); plp->mode3=7; REJECT(recvxGameBootPlayerRemainingPrepare(14),"invalid remaining player controller phase");
    setup(); plp->mode3=6; recvxGameBootPlayerRemainingPrepare(14);
    setup(); plp->mode2=10; REJECT(recvxGameBootPlayerRemainingPrepare(23),"invalid player event dispatch");
    setup(); plp->mode2=73; REJECT(recvxGameBootPlayerRemainingPrepare(7),"invalid player attack dispatch");
    setup(); plp->mode2=64; plp->wpnr_no=23; REJECT(recvxGameBootPlayerRemainingPrepare(7),"invalid player attack weapon");
    setup(); sys->pl_htp=NULL; REJECT(recvxGameBootPlayerRemainingPrepare(8),"invalid player traversal collision");
    setup(); sys->pl_htp->prm1=4; REJECT(recvxGameBootPlayerRemainingPrepare(15),"invalid player traversal collision");
    setup(); plp->exp1=NULL; REJECT(recvxGameBootPlayerRemainingPrepare(2),"invalid remaining player controller work");
    e=setup(); e->wpntp=2; REJECT(recvxGameBootPlayerRemainingPrepare(24),"invalid remaining player controller selectors");
    setup(); plp->mode1=32; REJECT(recvxGameBootPlayerRemainingPrepare(29),"invalid player basic event motion");
    setup(); plp->wpnr_no=2; REJECT(recvxGameBootPlayerRemainingPrepare(30),"invalid player event weapon motion");
    setup(); REJECT(recvxGameBootPlayerRemainingPrepare(33),"invalid player pitch context");
    setup(); plp->px=NAN; REJECT(recvxGameBootPlayerRemainingPrepare(5),"invalid remaining player controller work");
    setup(); assert(recvxPlayerMotionFrames(42)==64);
    REJECT(recvxPlayerMotionFrames(512),"invalid player controller motion table");
    REJECT(recvxPlayerMotionFrames(43),"invalid player controller motion frames");
    plp->mnwP=NULL; REJECT(recvxPlayerMotionFrames(0),"invalid player controller motion table");
    setup(); char route[]={0,1,-1}; recvxPlayerFixPrepare(plp,route);
    char empty[]={-1}; REJECT(recvxPlayerFixPrepare(plp,empty),"invalid player position fix route");
    char invalid[]={22,-1}; REJECT(recvxPlayerFixPrepare(plp,invalid),"invalid player position fix joint");
    char endless[64]={0}; REJECT(recvxPlayerFixPrepare(plp,endless),"invalid player position fix route");
    REJECT(recvxPlayerPose(plp,22),"invalid player action model joints");
    setup(); recvxPlayerEyePrepare(); cam.camver=2;
    REJECT(recvxPlayerEyePrepare(),"invalid player eye camera state");
    setup(); rom->cutp[0].cam[0].spd=-1;
    REJECT(recvxPlayerEyePrepare(),"invalid player eye camera speed");
    setup(); REJECT(recvxPlayerGround(-1),"invalid player controller floor");
    REJECT(recvxPlayerGround(32),"invalid player controller floor");
    setup(); sys->ewk_n=129; REJECT(recvxPlayerEnemiesPrepare(),"invalid player enemy count");
    setup(); sys->ewk_n=1; ene[0].flg=0xa1;
    REJECT(recvxPlayerEnemiesPrepare(),"invalid player linked enemy");
    setup(); sys->ewk_n=1; ene[0].flg=0x21; ene[0].watr.r=NAN;
    REJECT(recvxPlayerEnemiesPrepare(),"invalid player enemy capsule");
    setup(); GA_WORK hit={0}; hit.wpn_no=22;
    REJECT(recvxPlayerHitPrepare(&hit),"invalid player hit weapon");
    setup(); hit.wpn_no=0; recvxPlayerHitPrepare(&hit);
    swork.pip=sys->itm; swork.pip[0]=2; recvxPlayerBulletPrepare();
    swork.pip[0]=384; REJECT(recvxPlayerBulletPrepare(),"invalid player equipped inventory slot");
    swork.pip=sys->itm+1; REJECT(recvxPlayerBulletPrepare(),"invalid player weapon inventory bank");
    setup(); plp->cpcl=(CPCL *)(memory+35000); plp->cpcl[0]=(CPCL){1,2,10};
    recvxPlayerCapsulesPrepare(plp); plp->cpcl[0].jnt_b=22;
    REJECT(recvxPlayerCapsulesPrepare(plp),"invalid player hit capsule joint");
    printf("PASS: %u malformed player controller, model, route, camera, enemy, weapon and capsule cases rejected before access\n",rejections);
    return 0;
}
