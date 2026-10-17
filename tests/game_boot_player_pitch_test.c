#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/playpch.h"
#include "ps2/veronica/prog/Motion.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "psp/engine_original_fault.h"

SYS_WORK *sys;
BH_PWORK *plp;
unsigned char *freemem;
NJS_MATRIX lcmat[12];
const WPN_TAB WpnTab[23] = { [1] = {.flg = 0x800} };
const char PlyFlip[23] = {0,1,2,3,4,5,10,11,12,13,6,7,8,9,18,19,20,21,14,15,16,17,-1};
static SYS_WORK system_work;
static BH_PWORK player;
static unsigned char arena[600000] __attribute__((aligned(64)));
static jmp_buf failure;
static const char *fault;

void recvxGameBootUnsupported(const char *service)
{ fault = service; longjmp(failure, 1); }

static void setup(void)
{
    memset(&system_work, 0, sizeof(system_work));
    memset(&player, 0, sizeof(player));
    memset(arena, 0, sizeof(arena));
    sys = &system_work; plp = &player; freemem = arena;
    sys->endp = arena + sizeof(arena);
    sys->plbmtp = arena;
    sys->plwmtp = arena + 393216;
    sys->plzmtp = arena + 458752;
    sys->plmthp = arena + 466944;
    player.exp2 = arena + 480000;
    player.exp1 = arena + 484000;
    player.mlwP = player.mdl;
    player.mlwP->obj_num = 22;
    player.mlwP->objP = (NJS_CNK_OBJECT *)(arena + 486000);
    player.mlwP->owP = (O_WORK *)(arena + 490000);
    player.mnwP = (MN_WORK *)sys->plmthp;
    player.mnwP->obj_num = 22;
    player.mnwP->frm_num = 2;
    player.mnwP->md2P = (NJS_MDATA2_MOD *)arena;
    player.wpnr_no = 1;
    fault = NULL;
    recvxEngineOriginalMatrixReset();
}

static void expect_failure(const char *message)
{
    if (!setjmp(failure)) { PlyPchMain(plp); assert(!"expected pitch validation fault"); }
    assert(fault && strcmp(fault, message) == 0);
}

static void arm_ik(void)
{
    setup();
    NJS_CNK_OBJECT *objects=player.mlwP->objP;
    for(unsigned int i=0;i<22;++i){
        objects[i].ang[0]=100+(int)i;objects[i].ang[1]=200+(int)i;objects[i].ang[2]=300+(int)i;
        njUnitMatrix(&player.mlwP->owP[i].mtx);
    }
    objects[8].pos[0]=objects[9].pos[0]=2;
    NJS_MATRIX before,after;
    njUnitMatrix(NULL);njTranslate(NULL,3,4,5);njGetMatrix(&before);
    NJS_POINT3 target={2,0,0};
    bhArmIkMdk(plp,6,&target,0);
    assert(abs(objects[7].ang[2]-10922)<=3&&abs(objects[8].ang[1]-21845)<=3);
    assert(objects[6].ang[0]==106&&objects[9].ang[1]==209&&objects[8].ang[2]==308);
    njGetMatrix(&after);assert(!memcmp(before,after,sizeof(before))&&!recvxEngineOriginalFault());
    /* The target is in world space: translating both pose and target must
       produce the same arm solution. */
    int angles[3];memcpy(angles,objects[7].ang,sizeof(angles));
    player.mlwP->owP[6].mtx[12]=10;target.x=12;
    bhArmIkMdk(plp,6,&target,0);assert(!memcmp(angles,objects[7].ang,sizeof(angles)));
    player.mlwP->owP[6].mtx[12]=0;
    const NJS_POINT3 limits[]={{100,0,0},{0,0,0},{0,2,0},{0,-2,0},{.0000001f,0,0}};
    for(unsigned int i=0;i<sizeof(limits)/sizeof(limits[0]);++i){
        target=limits[i];bhArmIkMdk(plp,6,&target,35498);
        for(unsigned int a=0;a<3;++a)assert(abs(objects[7].ang[a])<=32768);
        assert(objects[8].ang[1]>=0&&objects[8].ang[1]<=32768);
        njGetMatrix(&after);assert(!memcmp(before,after,sizeof(before))&&!recvxEngineOriginalFault());
    }
    const char *messages[]={"joint chain","bone length","target","matrix","matrix"};
    for(unsigned int variant=0;variant<5;++variant){
        setup();objects=player.mlwP->objP;objects[8].pos[0]=objects[9].pos[0]=2;
        njUnitMatrix(&player.mlwP->owP[6].mtx);target=(NJS_POINT3){2,0,0};
        int base=variant==0?20:6;
        if(variant==1)objects[8].pos[0]=0;
        if(variant==2)target.x=NAN;
        if(variant==3)player.mlwP->owP[6].mtx[0]=NAN;
        if(variant==4)memset(player.mlwP->owP[6].mtx,0,sizeof(NJS_MATRIX));
        NJS_CNK_OBJECT saved[4];memcpy(saved,objects+6,sizeof(saved));
        if(!setjmp(failure)){bhArmIkMdk(plp,base,&target,0);assert(!"invalid arm IK accepted");}
        assert(fault&&strstr(fault,messages[variant])&&!memcmp(saved,objects+6,sizeof(saved)));
    }
}

int main(void)
{
    PP_WORK *pitch;
    NJS_CNK_OBJECT *objects;
    NJS_MKEY_A_MOD *keys;
    unsigned int i;
    setup(); pitch = (PP_WORK *)plp->exp2;
    pitch->mode = 15; pitch->arm_al = 300; pitch->arm_dr = -200;
    for (i = 0; i < 22; ++i) plp->mlwP->owP[i].flg = 3;
    PlyPchMain(plp);
    assert(!pitch->mode && !pitch->arm_al && !pitch->arm_dr);
    assert(plp->mlwP->owP[7].flg == 1 && plp->mlwP->owP[11].flg == 1);

    /* An active dual-arm motion uses original frame data and mix matrices. */
    plp->mode0 = plp->mode1 = 1; plp->mode2 = 164;
    objects = plp->mlwP->objP;
    keys = (NJS_MKEY_A_MOD *)(arena + 4096);
    keys[0].key[0] = 1000; keys[1].key[0] = 2000;
    plp->mnwP->md2P[7].p[1] = keys;
    plp->mnwP->md2P[11].p[1] = keys;
    plp->frm_no = 65536;
    sys->pad_on = 4;
    PlyPchMain(plp);
    assert(pitch->mode & 1);
    assert(pitch->hed_rate == 65536 - 3276);
    assert(pitch->gun_delay == -1);
    assert(pitch->obj_r.ang[0] == 2000 && pitch->obj_l.ang[0] == 2000);
    assert(objects[7].ang[0] >= 1998 && objects[7].ang[0] <= 2001);
    assert(objects[11].ang[0] >= 1998 && objects[11].ang[0] <= 2001);

    /* Mirroring maps the right arm to joint 11 and negates yaw and roll. */
    plp->mode2 = 68; plp->mtn_md = 2; plp->mtn_tp = (unsigned char *)PlyFlip;
    keys[1].key[1] = 500; keys[1].key[2] = 700;
    PlyPchMain(plp);
    assert((pitch->mode & 2) && pitch->count == 11);
    assert(pitch->obj_r.ang[1] == -500 && pitch->obj_r.ang[2] == -700);

    /* Exercise interpolation, including the formerly unsequenced normalization. */
    plp->hokan_count = 4; plp->hokan_rate = 32768;
    pitch->obj_r.ang[0] = 32000; pitch->obj_r.ang[1] = -31000;
    pitch->obj_l.ang[0] = -32000; pitch->obj_l.ang[2] = 31000;
    PlyPchMain(plp);

    plp->frm_no = 131072;
    expect_failure("invalid original player pitch motion records");
    plp->frm_no = 0;
    plp->mnwP->md2P[11].p[1] = arena + 599998;
    expect_failure("invalid original player pitch rotation keys");
    plp->mnwP->md2P[11].p[1] = keys;
    plp->mtn_tp = arena;
    expect_failure("invalid original player pitch motion records");
    plp->wpnr_no = 23;
    expect_failure("invalid original player pitch work");
    arm_ik();
    puts("original player pitch, arm motion, mirroring, interpolation, arm IK targets/poles/reach, matrix preservation and bounds pass");
    return 0;
}
