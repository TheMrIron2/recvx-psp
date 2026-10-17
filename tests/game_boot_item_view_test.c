/* Original inspection logic with typed UI leaves; no renderer or model loader
 * is substituted into the shipping program by this focused test. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_item_view.h"
#include "psp/game_boot_inventory_control.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/item.h"
#include "ps2/veronica/prog/itemview.h"
#include "ps2/veronica/prog/sub1.h"

static SYS_WORK state;
SYS_WORK *sys=&state;
static BH_PWORK player;
BH_PWORK *plp=&player;
S_WORK swork;
SITEM sitem;
STCAM_WRK st_cam;
PARTS parts_10b[24];
static unsigned char memory[32768] __attribute__((aligned(64)));
unsigned char *freemem=memory;
static NJS_CNK_OBJECT *joints;
static int messages,last_message,name_changes;
static jmp_buf rejection;
static int rejecting;
static const char *reason;
void recvxGameBootUnsupported(const char *name)
{if(!rejecting){fprintf(stderr,"unexpected item-view boundary: %s\n",name);abort();}reason=name;longjmp(rejection,1);}
#define REJECT(call,part) do{rejecting=1;reason=NULL;if(!setjmp(rejection)){call;assert(!"expected boundary");}rejecting=0;assert(reason&&strstr(reason,part));}while(0)
void bhSetMessage(int bank,int message) {assert(bank==1&&message>=0);++messages;last_message=message;}
void NameChangeSet(S_WORK *work) {assert(work==&swork);++name_changes;}
unsigned int *ItemSearch(unsigned short id)
{for(unsigned int i=0;i<16;++i)if(((sys->itm[i]>>16)&255u)==id)return &sys->itm[i];return NULL;}
size_t recvxGameBootSourceRemaining(const void *pointer) {(void)pointer;abort();}
int recvxGameBootValidateModel(const unsigned char *pointer,size_t bytes) {(void)pointer;(void)bytes;abort();}
int recvxGameBootPvpTextureCount(const unsigned char *pointer,size_t bytes) {(void)pointer;(void)bytes;abort();}
/* Zoom-out completes with an item having hide0, so no extent calculation is
   needed. These matrix leaves also permit the real extent helper to link. */
void njUnitMatrix(NJS_MATRIX *matrix) {if(matrix){memset(matrix,0,sizeof(*matrix));(*matrix)[0]=(*matrix)[5]=(*matrix)[10]=(*matrix)[15]=1;}}
int njPushMatrix(NJS_MATRIX *matrix) {(void)matrix;return 1;}
int njPopMatrix(unsigned int count) {(void)count;return 1;}
void njTranslate(NJS_MATRIX *matrix,float x,float y,float z) {(void)matrix;(void)x;(void)y;(void)z;}
void njRotateXYZ(NJS_MATRIX *matrix,int x,int y,int z) {(void)matrix;(void)x;(void)y;(void)z;}
void njScale(NJS_MATRIX *matrix,float x,float y,float z) {(void)matrix;(void)x;(void)y;(void)z;}
void njCalcPoint(NJS_MATRIX *matrix,NJS_POINT3 *input,NJS_POINT3 *output) {(void)matrix;*output=*input;}
void njCalcPoint4(NJS_MATRIX *matrix,NJS_POINT4 *input,NJS_POINT4 *output) {(void)matrix;*output=*input;}
float njScalor(NJS_VECTOR *vector) {return sqrtf(vector->x*vector->x+vector->y*vector->y+vector->z*vector->z);}
static void setup(unsigned int item)
{
    memset(&state,0,sizeof(state));memset(&swork,0,sizeof(swork));memset(&sitem,0,sizeof(sitem));
    memset(&st_cam,0,sizeof(st_cam));memset(parts_10b,0,sizeof(parts_10b));memset(memory,0,sizeof(memory));
    sys=&state;plp=&player;sys->endp=memory+sizeof(memory);sys->memp=memory+4096;
    joints=(NJS_CNK_OBJECT *)memory;
    for(unsigned int i=0;i<6;++i){for(unsigned int a=0;a<3;++a)joints[i].scl[a]=1;if(i<5)joints[i].child=&joints[i+1];}
    sitem.mdl.objP=joints;swork.itemid=item;swork.pip=sys->itm;swork.bxp=&sys->itm[64];
    swork.testmode=2;st_cam.pos_0.z=12;messages=name_changes=0;last_message=-1;
}
static unsigned int action_item(unsigned int action)
{
    for(unsigned int id=0;id<156;++id)if(itemflg[dsptbl[id].hide][1]==action)return id;
    abort();
}
int main(void)
{
    setup(1);sys->pad_on=4|1|0x80;ItemModelCheck();
    assert(st_cam.cay==-546&&st_cam.cax==546&&fabsf(st_cam.pos_1.z-.1f)<1e-6f);
    assert(parts_10b[14].col.r==1&&parts_10b[12].col.g==1&&swork.statusflg&0x100000u);
    sys->pad_on=8|2|0x10;ItemModelCheck();assert(st_cam.cay==546&&st_cam.cax==-546&&fabsf(st_cam.pos_1.z)<1e-6f);
    sys->pad_on=0;ItemModelCheck();assert(!st_cam.cay&&!st_cam.cax);
    st_cam.pos_1.z=3.05f;sys->pad_on=0x80;ItemModelCheck();assert(st_cam.pos_1.z==3.05f);
    st_cam.pos_1.z=-3.05f;sys->pad_on=0x10;ItemModelCheck();assert(st_cam.pos_1.z==-3.05f);
    sys->st_flg=0x200;st_cam.cax=st_cam.cay=st_cam.caz=123;ItemModelCheck();assert(!st_cam.cax&&!st_cam.cay&&!st_cam.caz);
    sys->st_flg=0;sys->pad_ps=0x800;ItemModelCheck();assert(messages==1&&last_message==1);
    sys->pad_ps=0x1000;ItemModelCheck();assert(swork.mode==1&&swork.testmode==1&&name_changes==1&&swork.statusflg&0x20000u);

    setup(30);sys->itm[0]=(30u<<16)|1;sys->pad_ps=0x1000;ItemModelCheck();
    assert(swork.itemid==13&&sys->itm[0]==((13u<<16)|itemdata[13].max));
    setup(82);sys->pad_ps=0x800;sys->itm[0]=(82u<<16);ItemModelCheck();assert(last_message==140);
    sys->itm[0]|=1;ItemModelCheck();assert(last_message==82);

    setup(1);sys->st_flg=0x4000|0x200;sys->cb_flg=0x1000;sys->mes_sel=100;ItemModelMessageWait();
    assert(swork.testmode==5&&swork.actioncount==32&&swork.mesid==1&&!(sys->cb_flg&0x1000u)&&!(sys->st_flg&0x200u));
    unsigned int actionable=action_item(1);setup(actionable);sys->st_flg=0x4000;sys->cb_flg=0x1000;sys->mes_sel=100;ItemModelMessageWait();assert(swork.testmode==4);
    sys->cb_flg=0x1000;sys->mes_sel=101;ItemModelMessageWait();assert(swork.testmode==2);
    sys->st_flg=0;sys->cb_flg=0x1000;sys->mes_sel=100;ItemModelMessageWait();assert(swork.testmode==2&&(sys->cb_flg&0x1000u));

    setup(1);swork.actioncount=2;ItemModelChangeZoomOut();assert(swork.actioncount==1&&fabsf(st_cam.pos_0.z-8.8f)<1e-5f&&sitem.mw.ay1==4095&&sitem.mw.az1==2047);
    swork.mesid=0;ItemModelChangeZoomOut();assert(swork.testmode==1&&swork.actioncount==32&&swork.itemid==0&&swork.ips1==0);
    swork.ips1=44;swork.mesid=1;float before=st_cam.pos_0.z;
    for(unsigned int i=0;i<32;++i)ItemModelChangeZoomIn();
    assert(swork.testmode==2&&swork.actioncount==0&&sys->itm[0]==((44u<<16)|itemdata[44].max)&&last_message==getmestbl[1]);
    assert(fabsf(st_cam.pos_0.z-before-102.4f)<.001f);

    for(unsigned int action=1;action<=3;++action){
        unsigned int id=action_item(action);setup(id);swork.maincsr=0;swork.testmode=4;
        for(unsigned int frame=0;frame<30&&swork.testmode==4;++frame)ItemModelActionSet();
        assert(swork.testmode==5&&swork.actioncount==32&&sitem.mw.pos.y==-.76f);
        unsigned int axis=action==3?2:0;
        unsigned int end=action==2&&id==128?5:action==2&&id==63?4:3;
        assert(joints[end].ang[axis]>=32768&&joints[1].ang[axis]<0);
    }
    const unsigned int special_ids[]={63,128,129};
    for(unsigned int i=0;i<3;++i){
        setup(special_ids[i]);swork.maincsr=0;assert(!MdlAction01(&sitem));
        unsigned int end=special_ids[i]==128?5:special_ids[i]==63?4:3;
        assert(joints[end].ang[0]==1456&&joints[1].ang[0]==-728);
        unsigned int ticks=0;while(!MdlAction01(&sitem)){assert(++ticks<30);}assert(sitem.mw.pos.y==-.76f);
    }
    short direction[2]={1024,0};assert(MdlDirChk(direction,0)&&MdlDirChk(direction,1024)&&!MdlDirChk(direction,1025));
    direction[0]=0;assert(MdlDirChk(direction,-32768));
    setup(160);REJECT(ItemModelCheck(),"model selection");
    setup(1);swork.pip=&sys->itm[1];REJECT(ItemModelCheck(),"inspection action state");
    setup(1);REJECT(ItemModelActionSet(),"model action dispatch");
    setup(action_item(1));joints[2].child=NULL;REJECT(MdlAction00(&sitem),"action joint");
    setup(action_item(1));joints[2].child=joints;REJECT(MdlAction00(&sitem),"hierarchy");
    setup(1);swork.actioncount=1;swork.mesid=159;REJECT(ItemModelChangeZoomIn(),"zoom-in");
    setup(1);swork.actioncount=1;swork.mesid=0;sitem.mdl.objP=NULL;REJECT(ItemModelChangeZoomOut(),"hierarchy");
    puts("Original item inspection: rotation/zoom/cancel, message decisions, action dispatch/joints, transformed-item zoom pair and invalid states PASS");
    return 0;
}
