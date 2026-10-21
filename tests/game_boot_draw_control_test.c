#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/game.h"
#include "ps2/veronica/prog/MdlPut.h"
#include "ps2/veronica/prog/objitm.h"
#include "ps2/veronica/prog/eneset.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/njplus.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"

SYS_WORK *sys;
ROM_WORK *rom;
BH_PWORK player, ene[128], *plp;
O_WRK eff[512];
CAM_WORK cam;
NJS_MATRIX *cmat;
unsigned char *freemem, pl_sleep_cnt;
unsigned int Ps2_albinoid_flag, _nj_control_3d_flag_;
static SYS_WORK state;
static ROM_WORK room;
static NJS_MATRIX camera_matrix, camera_backup;
static unsigned char arena[100000] __attribute__((aligned(64)));
static NJS_CNK_MODEL *enemy_model, *player_model, *object_model, *item_model;
static unsigned int events[100], count, last_mode;
static unsigned int multi, easy, simple, simple_multi, fog;
static jmp_buf failure;
static const char *fault;
static int expecting_fault;
void recvxGameBootLog(const char *format,...) { (void)format; }
void recvxGameBootUnsupported(const char *name)
{ if(!expecting_fault){fprintf(stderr,"Unexpected draw fault: %s\n",name);assert(!"unexpected draw fault");}
  fault=name; longjmp(failure,1); }
static void event(unsigned int value) { assert(count<100); events[count++]=value; }
static void model_event(NJS_CNK_MODEL *model)
{ event(model==enemy_model?60:model==player_model?70:model==object_model?40:50); }
void njControl3D(Uint32 flags) { _nj_control_3d_flag_=flags; last_mode=flags; }
void njSetCheapShadowMode(Int mode) { assert(mode==0x80); }
Sint32 njSetTexture(NJS_TEXLIST *texture) { assert(texture);return 0; }
void njCnkEasyDrawObject(NJS_CNK_OBJECT *object) { assert(object==rom->mdl.objP); ++easy; event(10); }
void njCnkEasyMultiDrawObject(NJS_CNK_OBJECT *object) { assert(object==rom->mdl.objP); ++multi; event(10); }
void njCnkEasyDrawModel(NJS_CNK_MODEL *model) { ++easy; model_event(model); }
void njCnkEasyMultiDrawModel(NJS_CNK_MODEL *model) { ++multi; model_event(model); }
void njCnkSimpleDrawModel(NJS_CNK_MODEL *model) { ++simple; model_event(model); }
void njCnkSimpleMultiDrawModel(NJS_CNK_MODEL *model) { ++simple_multi; model_event(model); }
void njFogDisable(void) { ++fog; }
void njFogEnable(void) { assert(fog); --fog; }
void bhDrawEffect(void) { event(20); }
void bhSetLight(void) { }
void bhSetHalfLight(void) { }
void bhSetEasyDirLight(float value) { assert(isfinite(value)); }
void bhCalcModel(BH_PWORK *work) { (void)work; }
void bhGetLightVector(int x,int y,int z,NJS_VECTOR *v) { (void)x;(void)y;(void)z;*v=(NJS_VECTOR){0,1,0}; }
void njCnkSetEasyMultiAmbient(Float x,Float y,Float z) { assert(isfinite(x+y+z)); }
void njCnkSetSimpleMultiAmbient(Float x,Float y,Float z) { assert(isfinite(x+y+z)); }
void njCnkSetEasyMultiLightColor(Int light,Float x,Float y,Float z) { (void)light;assert(isfinite(x+y+z)); }
void njCnkSetSimpleMultiLightColor(Int light,Float x,Float y,Float z) { (void)light;assert(isfinite(x+y+z)); }
void njCnkSetEasyMultiLightVector(Float x,Float y,Float z) { assert(isfinite(x+y+z)); }
void njCnkSetSimpleMultiLightVector(Float x,Float y,Float z) { assert(isfinite(x+y+z)); }
void njCnkSetEasyMultiLightSwitch(Int light,Int flag) { (void)light;(void)flag; }
void njCnkSetSimpleMultiLightSwitch(Int light,Int flag) { (void)light;(void)flag; }
void njCnkSetEasyMultiLightMatrices(void) { }
void njCnkSetSimpleMultiLightMatrices(void) { }
void njCnkSetEasyLight(Float x,Float y,Float z) { assert(isfinite(x+y+z)); }
void njCnkSetEasyLightColor(Float x,Float y,Float z) { assert(isfinite(x+y+z)); }
void njCnkSetEasyLightIntensity(Float x,Float y) { assert(isfinite(x+y)); }
void npPushMdlstr(NJS_CNK_OBJECT *object,int n) { (void)object;(void)n;recvxGameBootUnsupported("npPushMdlstr"); }
void npPopMdlstr(NJS_CNK_OBJECT *object,int n) { (void)object;(void)n;recvxGameBootUnsupported("npPopMdlstr"); }
void npPushMdlstr2(NJS_CNK_OBJECT *object,int n) { (void)object;(void)n;recvxGameBootUnsupported("npPushMdlstr2"); }
void npPopMdlstr2(NJS_CNK_OBJECT *object,int n) { (void)object;(void)n;recvxGameBootUnsupported("npPopMdlstr2"); }
void npCalcSkin(void *work,int n,int *skin) { (void)work;(void)n;(void)skin;recvxGameBootUnsupported("npCalcSkin"); }
void npCalcSkinFM(void *work,int n,int *skin) { (void)work;(void)n;(void)skin;recvxGameBootUnsupported("npCalcSkinFM"); }
void npCalcMorphing(NJS_CNK_OBJECT *a,NJS_CNK_OBJECT *b,float rate,int n)
{ (void)a;(void)b;(void)rate;(void)n;recvxGameBootUnsupported("npCalcMorphing"); }
unsigned int npGetMatColor(NJS_CNK_OBJECT *object,int n) { (void)object;(void)n;recvxGameBootUnsupported("npGetMatColor"); }
void njDrawModel(NJS_MODEL *model) { (void)model;recvxGameBootUnsupported("njDrawModel"); }
#define REJECT(call,message) do { fault=NULL;expecting_fault=1; if (!setjmp(failure)) {call;assert(!"expected draw fault");} \
    expecting_fault=0;assert(fault && strcmp(fault,message)==0); } while(0)

static void actor_setup(O_WRK *work,size_t object_offset,size_t matrix_offset,size_t model_offset)
{
    work->flg=1; work->mlwP=work->mdl; work->mlwP->flg=0x80;
    work->mlwP->obj_num=1; work->mlwP->objP=(NJS_CNK_OBJECT *)(arena+object_offset);
    work->mlwP->owP=(O_WORK *)(arena+matrix_offset);
    work->mlwP->objP->model=(NJS_CNK_MODEL *)(arena+model_offset);
    njUnitMatrix((NJS_MATRIX *)&work->mlwP->owP->mtx);
    work->mlwP->owP->mtx[14]=10;
    memset(work->clp_jno,-1,sizeof(work->clp_jno)); work->clp_jno[0]=0;
}
static void setup(void)
{
    memset(&state,0,sizeof(state));memset(&room,0,sizeof(room));memset(&player,0,sizeof(player));
    memset(ene,0,sizeof(ene));memset(arena,0,sizeof(arena));memset(&cam,0,sizeof(cam));
    sys=&state;rom=&room;plp=&player;freemem=arena;sys->endp=arena+sizeof(arena);
    cam.mtx=&camera_matrix;cam.mtxb=&camera_backup;
    sys->obwp=(O_WRK *)(arena+20000);sys->itwp=(O_WRK *)(arena+30000);
    actor_setup((O_WRK *)plp,1000,2000,3000);player_model=plp->mlwP->objP->model;
    actor_setup((O_WRK *)&ene[0],4000,5000,6000);enemy_model=ene[0].mlwP->objP->model;
    actor_setup(sys->obwp,7000,8000,9000);object_model=sys->obwp->mlwP->objP->model;
    actor_setup(sys->itwp,10000,11000,12000);item_model=sys->itwp->mlwP->objP->model;
    rom->mdl.objP=(NJS_CNK_OBJECT *)(arena+13000);
    sys->pt_flg=0x3f;sys->ewk_n=1;sys->en_objn=1;sys->en_obj[0]=&ene[0];
    sys->ob_nlgn=1;sys->ob_nlg[0]=sys->obwp;rom->itm_n=1;
    count=multi=easy=simple=simple_multi=fog=0;Ps2_albinoid_flag=pl_sleep_cnt=0;
    recvxEngineOriginalMatrixReset();recvxEngineOriginalViewReset();
    recvxEngineOriginalViewConfigureScreen(640,480);njSetPerspective(11832);njUnitMatrix(cam.mtx);
}

int main(void)
{
    setup();bhAllDrawModel();
    const unsigned int ordinary[]={10,60,40,50,20,60,70};
    assert(count==7 && !memcmp(events,ordinary,sizeof(ordinary)) && last_mode==0);
    assert(multi==6 && !easy && !fog);
    setup();Ps2_albinoid_flag=1;bhAllDrawModel();
    const unsigned int albinoid[]={10,60,20,40,50,60,70};
    assert(count==7 && !memcmp(events,albinoid,sizeof(albinoid)));
    setup();sys->gm_flg=0x80;bhAllDrawModel();assert(easy==6 && !multi);
    setup();sys->pt_flg=0;bhAllDrawModel();assert(count==0);
    setup();plp->mdflg=1;bhAllDrawModel();assert(events[count-1]==60);
    setup();plp->stflg=0x1000000;pl_sleep_cnt=2;bhAllDrawModel();
    assert(events[count-1]==70 && pl_sleep_cnt==1);
    setup();plp->mlwP->owP->mtx[12]=100000;assert(bhCheckClipModel(plp)==1);
    plp->mdflg=0x20;bhAllDrawModel();assert(events[count-1]==70);
    setup();plp->draw_tp=1;bhPutModel(plp);assert(simple_multi==1);
    plp->draw_tp=3;bhPutModel(plp);assert(simple==1);
    setup();sys->efm[2]=plp->mdl[0];eff[0].mlwP=&sys->efm[2];
    bhPutModel((BH_PWORK *)&eff[0]);assert(count==1 && events[0]==70);
    /* Original linked body/fragment controllers borrow their parent's work. */
    setup();ene[1].flg=1;ene[1].mlwP=ene[0].mdl;ene[1].mdflg=0x21;
    memset(ene[1].clp_jno,-1,sizeof(ene[1].clp_jno));sys->ewk_n=2;
    bhDrawEnemy();assert(count==1&&events[0]==60);
    setup();ene[0].mdl[3]=ene[0].mdl[0];plp->mlwP=&ene[0].mdl[3];
    bhPutModel(plp);assert(count==1&&events[0]==60);
    setup();eff[0].mlwP=ene[0].mdl;
    bhPutModel((BH_PWORK *)&eff[0]);assert(count==1&&events[0]==60);
    setup();ene[0].mlwP=plp->mdl;
    bhPutModel(&ene[0]);assert(count==1&&events[0]==70);
    setup();plp->mlwP=sys->obwp->mdl;
    bhPutModel(plp);assert(count==1&&events[0]==40);
    setup();plp->mlwP=sys->itwp->mdl;
    bhPutModel(plp);assert(count==1&&events[0]==50);
    setup();plp->mlwP->objP->evalflags=8;bhPutModel(plp);assert(count==0);
    setup();sys->st_flg=2;sys->obwp->mdflg=0x10;bhDrawObject(sys->obwp);assert(!fog && count==1);
    setup();plp->skp[0]=(int *)(arena+40000);
    REJECT(bhPutModel(plp),"npPushMdlstr");
    setup();ene[0].mdflg=2;REJECT(bhDrawEnemy(),"npPushMdlstr2");
    setup();sys->en_objn=17;REJECT(bhAllDrawModel(),"invalid original draw state");
    setup();rom->amb_rom=4;REJECT(bhAllDrawModel(),"invalid original draw ambient");
    setup();sys->ob_nlgn=33;REJECT(bhDrawObjItm(),"invalid original object draw queues");
    setup();sys->en_obj[0]=(BH_PWORK *)1;REJECT(bhDrawEneObject(),"invalid original draw actor");
    setup();plp->clp_jno[0]=1;REJECT(bhCheckClipModel(plp),"invalid original model clip joint");
    setup();plp->draw_tp=4;REJECT(bhPutModel(plp),"invalid original model draw selector");
    setup();plp->mlwP=(ML_WORK *)1;REJECT(bhPutModel(plp),"invalid original draw model work");
    setup();plp->mlwP=(ML_WORK *)((unsigned char *)ene[0].mdl+1);
    REJECT(bhPutModel(plp),"invalid original draw model work");
    setup();plp->mlwP=(ML_WORK *)(arena+50000);*plp->mlwP=ene[0].mdl[0];
    REJECT(bhPutModel(plp),"invalid original draw model work");
    setup();plp->mlwP=ene[0].mdl;plp->mlwP->obj_num=513;
    REJECT(bhPutModel(plp),"invalid original draw model work");
    setup();plp->mlwP=ene[0].mdl;plp->mlwP->owP=(O_WORK *)(sys->endp-4);
    REJECT(bhPutModel(plp),"invalid original draw model work");
    puts("original full-game draw order, visibility, draw modes and named boundaries pass");
    return 0;
}
