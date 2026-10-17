#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot_event_camera.h"
#include "psp/game_boot_event_screen.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/cut.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/ps2_event.h"

SYS_WORK *sys;
ROM_WORK *rom;
CAM_WORK cam;
BH_PWORK *plp, ene[128];
O_WRK eff[512];
unsigned char *freemem,*bhScePtr;
NJS_MATRIX *cmat;
float GameNear=-2,GameFar=-20000;
unsigned int Ps2_ice_flag;
unsigned char BackColorFlag;
static SYS_WORK system_work;
static ROM_WORK room_work;
static BH_PWORK player;
static struct {
    EVC_WORK camera[2];
    NJS_CNK_OBJECT object[3];
    NJS_CNK_MODEL model;
    LGT_WORK lights[2];
    POS positions[2];
    O_WRK work[2];
    O_WORK poses[3];
} data;
static unsigned int checks,cut_calls,reset_step;
static jmp_buf stopped;
static const char *fault;
static int expecting_fault;
#define CHECK(condition) do{++checks;assert(condition);}while(0)
#define REJECT(call,message) do{fault=NULL;expecting_fault=1;if(!setjmp(stopped)){call;assert(!"expected camera guard");}expecting_fault=0;CHECK(fault&&strstr(fault,message));}while(0)
void recvxGameBootUnsupported(const char *name){if(!expecting_fault){fprintf(stderr,"unexpected camera fault: %s\n",name);abort();}fault=name;longjmp(stopped,1);}
void bhCheckCut(int index){CHECK(index==1);++cut_calls;if(reset_step){CHECK(reset_step++==1);CHECK(!(sys->st_flg&1));CHECK(plp->gpx==plp->px&&plp->gpy==plp->py&&plp->gpz==plp->pz);}}
void bhControlCamera(void){CHECK(reset_step++==2);}
void bhControlLight(void){CHECK(reset_step++==3);}
static int close_to(float a,float b){return fabsf(a-b)<0.001f;}
static void reset(void)
{
    memset(&system_work,0,sizeof(system_work));memset(&room_work,0,sizeof(room_work));
    memset(&data,0,sizeof(data));memset(&cam,0,sizeof(cam));memset(&player,0,sizeof(player));
    memset(ene,0,sizeof(ene));memset(eff,0,sizeof(eff));
    sys=&system_work;rom=&room_work;plp=&player;
    freemem=(unsigned char *)&data;sys->endp=freemem+sizeof(data);
    sys->rdtp=freemem;sys->rdtsz=sizeof(data);
    rom->evcp=data.camera;rom->evc_n=2;
    rom->mdl.objP=data.object;rom->mdl.obj_num=3;
    rom->evlp=data.lights;rom->evl_n=2;
    rom->posp=data.positions;rom->pos_n=2;
    rom->bak_col=0x123456u;
    for(unsigned int i=0;i<2;++i){
        data.camera[i].keyf_n=2;
        for(unsigned int j=0;j<2;++j){
            CAM_KEYF_WORK *key=&data.camera[i].keyf[j];
            key->px=10*j;key->py=5;key->pz=20*i;key->frame=2;key->pers=16384;
            key->fog_col=0x112233u;key->fog_nr=10;key->fog_fr=1000;
        }
    }
    for(unsigned int i=0;i<3;++i)data.object[i].model=&data.model;
    player.mlwP=&player.mdl[0];player.mlwP->obj_num=3;player.mlwP->owP=data.poses;
    player.mtx=(NJS_MATRIX *)player.mtxbuf;
    BackColorFlag=1;cut_calls=0;reset_step=0;fault=NULL;
    recvxEngineOriginalFaultReset();recvxEngineOriginalMatrixReset();recvxEngineOriginalViewReset();
    njUnitMatrix(player.mtx);
    for(unsigned int i=0;i<3;++i)njUnitMatrix(&data.poses[i].mtx);
}
static void screen_commands(void)
{
    reset();unsigned char cine[]={18,0};bhScePtr=cine;
    CHECK(bhCineSet()==1&&bhScePtr==cine+2&&(sys->cb_flg&4)&&(sys->st_flg&4));
    cine[1]=3;bhScePtr=cine;bhCineSet();CHECK((sys->cb_flg&0x44u)==0x44u);
    sys->cine_ap=100;sys->cine_an=50;cine[1]=1;bhScePtr=cine;bhCineSet();
    CHECK(!(sys->cb_flg&4)&&!(sys->st_flg&4)&&!sys->cine_ap&&!sys->cine_an&&sys->sp_flg==UINT32_MAX);
    for(unsigned int selector=2;selector<=5;++selector){
        cine[1]=selector;bhScePtr=cine;sys->cine_ap=sys->cine_an=80;bhCineSet();
        CHECK(bhScePtr==cine+2&&sys->cine_ap==(selector==3?80:0)&&sys->cine_an==(selector==3?80:0));
    }
    unsigned char fade[]={54,255,129,130,131,12};bhScePtr=fade;
    sys->fade_ap=1;sys->fade_rp=2;sys->fade_gp=3;sys->fade_bp=4;
    sys->fade_an=5;sys->fade_rn=6;sys->fade_gn=7;sys->fade_bn=8;
    CHECK(bhFadeSet()==1&&bhScePtr==fade+6);
    CHECK(sys->fade_pbk==0x01020304u&&sys->fade_ap==255&&sys->fade_rp==129&&sys->fade_gp==130&&sys->fade_bp==131);
    CHECK(sys->fade_hkn==13&&sys->fade_ao==5&&sys->fade_ro==6&&sys->fade_go==7&&sys->fade_bo==8);
    fade[5]=0;bhScePtr=fade;bhFadeSet();CHECK(sys->fade_hkn==1);
    unsigned char camera[]={19,0,1,1};bhScePtr=camera;
    CHECK(bhCamSet()==1&&bhScePtr==camera+4&&cam.evc_no==1&&cam.keyf_no==1);
    CHECK(cam.px==10&&cam.pz==20&&(sys->st_flg&1));
    camera[1]=1;bhScePtr=camera;sys->ts_flg=0x200;bhCamSet();
    float near,far;recvxEngineOriginalViewClipPlanes(&near,&far);
    CHECK(cut_calls==1&&!(sys->st_flg&1)&&near==GameNear&&far==GameFar);
    sys->gm_flg|=0x40;bhScePtr=camera;bhCamSet();recvxEngineOriginalViewClipPlanes(&near,&far);
    CHECK(near==-1&&far==-20000);
    sys->ts_flg=0;bhScePtr=camera;bhCamSet();recvxEngineOriginalViewClipPlanes(&near,&far);
    CHECK(near==-1&&far==-99);
    reset();camera[1]=0;camera[2]=2;bhScePtr=camera;cam.flg=0x46;
    REJECT(bhCamSet(),"table or index");CHECK(bhScePtr==camera&&cam.flg==0x46);
}
static void reset_command(void)
{
    reset();unsigned char command[]={91,255};bhScePtr=command;
    player.px=17;player.py=-9;player.pz=42;
    sys->st_flg=0x8001;reset_step=1;
    CHECK(bhInitCamSet()==1&&bhScePtr==command+2);
    CHECK(reset_step==4&&cut_calls==1&&sys->st_flg==0x8000);
    reset();bhScePtr=command;player.py=NAN;sys->st_flg=1;
    REJECT(bhInitCamSet(),"player position");
    CHECK(bhScePtr==command&&sys->st_flg==1&&!cut_calls);
    reset();bhScePtr=command;plp=NULL;
    REJECT(bhInitCamSet(),"player position");
}
static void setter_and_locks(void)
{
    reset();CAM_KEYF_WORK *key=&data.camera[1].keyf[1];
    key->hidobj[0]=0x80000000u;key->hidlgt[0]=0x40000000u;
    sys->evc_no=8;sys->evc_kn=9;sys->st_flg=2;sys->fog_cngct=10;key->flg=8;
    cam.mode0=cam.mode1=cam.mode2=cam.mode3=255;cam.flg=0x38;
    bhSetEventCamera(1,1);
    CHECK(sys->evc_ono==8&&sys->evc_okn==9&&sys->evc_no==1&&sys->evc_kn==1&&cam.keyf_no==1);
    CHECK((sys->gm_flg&0x30u)==0x30u&&(sys->st_flg&1)&&cam.flg==2&&sys->fog_ct==6);
    CHECK(!cam.mode0&&!cam.mode1&&!cam.mode2&&!cam.mode3&&cam.px==10&&cam.pxp==10&&cam.ln==0);
    CHECK((data.object[0].evalflags&8)&&!(data.object[1].evalflags&8)&&
          (data.lights[0].flg&2)&&!(data.lights[1].flg&2));
    CHECK(cam.fog_col==0x112233u&&cam.fog_nr==10&&cam.fog_fr==1000&&
          recvxEngineOriginalViewBackgroundColor()==0x123456u);
    key->lkflg=6;key->lkno=30;key->lkono=3;key->lx=1;key->ly=2;key->lz=3;
    data.positions[0]=(POS){10,20,30,0};NJS_POINT3 result;
    bhGetEvtCamLockPosition(key,&result);
    CHECK(key->lkno==0&&key->lkono==0&&result.x==11&&result.y==22&&result.z==33);
    key->lkflg=1;key->lkno=8;key->lkono=1;data.poses[1].mtx[12]=40;
    bhGetEvtCamLockPosition(key,&result);CHECK(key->lkno==0&&result.x==41&&result.y==2&&result.z==3);
    key->lkono=99;(*player.mtx)[12]=9;bhGetEvtCamLockPosition(key,&result);
    CHECK(key->lkono==0&&result.x==10);
    player.mlwP->obj_num=0;bhGetEvtCamLockPosition(key,&result);CHECK(result.x==10);
    rom->obj_n=1;sys->obwp=data.work;data.work[0].mlwP=&data.work[0].mdl[0];
    data.work[0].mlwP->obj_num=1;data.work[0].mtx=(NJS_MATRIX *)data.work[0].mtxbuf;
    njUnitMatrix(data.work[0].mtx);(*data.work[0].mtx)[13]=8;key->lkflg=3;key->lkno=0;
    bhGetEvtCamLockPosition(key,&result);CHECK(result.y==10);
    rom->ene_n=1;ene[0].mlwP=&ene[0].mdl[0];ene[0].mlwP->obj_num=1;
    ene[0].mtx=(NJS_MATRIX *)ene[0].mtxbuf;njUnitMatrix(ene[0].mtx);(*ene[0].mtx)[12]=3;
    key->lkflg=2;key->lkno=9;bhGetEvtCamLockPosition(key,&result);
    CHECK(key->lkno==0&&result.x==4);
    rom->itm_n=1;sys->itwp=data.work;key->lkflg=4;
    bhGetEvtCamLockPosition(key,&result);CHECK(result.y==10);
    rom->eff_n=1;sys->efid[0]=5;eff[5].mlwP=&eff[5].mdl[0];eff[5].mlwP->obj_num=1;
    eff[5].mtx=(NJS_MATRIX *)eff[5].mtxbuf;njUnitMatrix(eff[5].mtx);(*eff[5].mtx)[12]=6;
    key->lkflg=5;bhGetEvtCamLockPosition(key,&result);CHECK(result.x==7);
    sys->stg_no=3;sys->rom_no=23;cam.evc_no=6;bhChangeBackColorEvt();
    CHECK(recvxEngineOriginalViewBackgroundColor()==0x386880u);
    sys->ts_flg=0x200;bhChangeViewClip(0,2,0,1);float near,far;
    recvxEngineOriginalViewClipPlanes(&near,&far);CHECK(near==-24&&far==-5000);
    bhChangeViewClip(9,5,0,36);CHECK(Ps2_ice_flag==0);
    bhChangeViewClip(9,5,0,35);CHECK(Ps2_ice_flag==1);
    bhChangeClipVolume(0,0,0,0);CHECK(!recvxEngineOriginalFault());
    bhChangeClipVolume(3,15,0,3);CHECK(!recvxEngineOriginalFault());
}
static void spline_control(void)
{
    reset();data.camera[0].nxt_no=1;cam.evc_no=cam.keyf_no=0;
    bhControlEventCamera();CHECK(cam.mode0==1&&cam.ct0==1&&close_to(cam.px,0));
    CHECK(cam.ips[0][0]==10&&cam.ips[1][0]==0&&cam.ips[2][0]==10&&cam.ips[3][0]==0);
    bhControlEventCamera();CHECK(cam.keyf_no==1&&cam.ct0==0&&close_to(cam.px,5));
    bhControlEventCamera();CHECK(cam.keyf_no==1&&cam.ct0==1&&close_to(cam.px,10));
    bhControlEventCamera();CHECK(cam.evc_no==0&&cam.keyf_no==0&&cam.ct0==0&&close_to(cam.px,5));
    for(unsigned int i=0;i<20;++i)bhControlEventCamera();
    CHECK(cam.mode0==1);
    reset();data.camera[0].nxt_no=1;data.positions[0]=(POS){0,5,10,0};
    for(unsigned int i=0;i<2;++i)data.camera[0].keyf[i].lkflg=6;
    bhControlEventCamera();CHECK(cam.ay==32768&&cam.ax==0);
    bhControlEventCamera();CHECK(cam.ay>32768&&cam.ay<49152&&cam.ax==0);
    bhControlEventCamera();CHECK(abs((int)((unsigned int)cam.ay&65535u)-40960)<=1&&cam.ax==0);
    reset();bhControlEventCamera();sys->st_flg|=0x80;bhControlEventCamera();CHECK(cam.ct0==1&&close_to(cam.px,5));
    sys->st_flg&=~0x80u;bhControlEventCamera();CHECK(cam.mode0==2&&cam.keyf_no==1);
    bhControlEventCamera();CHECK(!(sys->st_flg&0x10)&&close_to(cam.px,10));
    reset();data.camera[0].nxt_no=2;data.camera[1].keyf[0].px=20;
    for(unsigned int i=0;i<4;++i)bhControlEventCamera();
    CHECK(cam.evc_no==1&&cam.keyf_no==0&&cam.mode0==1);
}
static void malformed(void)
{
    reset();REJECT(bhSetEventCamera(-1,0),"table or index");
    reset();REJECT(bhSetEventCamera(0,2),"key index");
    reset();data.camera[0].keyf_n=17;REJECT(bhControlEventCamera(),"key count");
    reset();data.camera[0].nxt_no=3;REJECT(bhControlEventCamera(),"link");
    reset();rom->mdl.obj_num=257;REJECT(bhSetEventCamera(0,0),"visibility");
    reset();rom->evl_n=129;REJECT(bhSetEventCamera(0,0),"visibility");
    reset();data.camera[0].keyf[0].px=NAN;REJECT(bhSetEventCamera(0,0),"non-finite");
    reset();data.camera[0].keyf[0].lkflg=7;REJECT(bhSetEventCamera(0,0),"selector");
    reset();data.camera[0].keyf[0].lkflg=6;rom->posp=(POS *)4;
    REJECT(bhSetEventCamera(0,0),"position array");
    reset();data.camera[0].keyf[0].lkflg=3;rom->obj_n=1;sys->obwp=(O_WRK *)4;
    REJECT(bhSetEventCamera(0,0),"work array");
    reset();data.camera[0].keyf[0].lkflg=1;data.camera[0].keyf[0].lkono=-1;
    REJECT(bhSetEventCamera(0,0),"joint");
    reset();data.camera[0].keyf[0].lkflg=5;rom->eff_n=1;sys->efid[0]=512;
    REJECT(bhSetEventCamera(0,0),"mapping");
    reset();data.camera[0].keyf_n=16;cam.keyf_no=15;data.camera[0].keyf[15].lkflg=1;
    REJECT(bhControlEventCamera(),"lookahead");
    reset();REJECT(bhChangeViewClip(10,0,0,0),"stage");
    reset();cam.mode0=1;cam.ips[0][0]=INFINITY;REJECT(bhControlEventCamera(),"spline");
}
int main(void)
{
    screen_commands();reset_command();setter_and_locks();spline_control();malformed();
    printf("PASS: original screen commands, camera setters/visibility/lock targets, clip/background tables and spline transitions/self-loops: %u checks\n",checks);
    return 0;
}
