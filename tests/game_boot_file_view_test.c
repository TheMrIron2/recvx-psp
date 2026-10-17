/* Execute original file-view state machines; draw/read leaves are test-only. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_file_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/fileview.h"
#include "ps2/veronica/prog/itemview.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/njplus.h"
static SYS_WORK state;
SYS_WORK *sys=&state;
S_WORK swork;
SITEM sitem;
FV_WORK fvwork;
NP_WORK np;
PARTS parts_00b[24],parts_10b[24],parts_22b[6];
float cen_pos[12][6],cen_pos99[12][6];
unsigned char menuanim[3][4];
NJS_TEXMEMLIST tbuf[256];
char ViewCnt;
static unsigned char memory[512*1024] __attribute__((aligned(64)));
unsigned char *freemem=memory;
static int reads,installs,releases,messages,names,bookmarks,triangles;
static unsigned int last_entry,last_message;
static float matrix_x[64];static unsigned int matrix_depth;
static jmp_buf rejection;static int rejecting;static const char *reason;
void recvxGameBootUnsupported(const char *name) {if(!rejecting){fprintf(stderr,"unexpected file-view boundary: %s\n",name);abort();}reason=name;longjmp(rejection,1);}
#define REJECT(call,part) do{rejecting=1;reason=NULL;if(!setjmp(rejection)){call;assert(!"expected boundary");}rejecting=0;assert(reason&&strstr(reason,part));}while(0)
unsigned char *bhGetFreeMemory(int bytes,int alignment) {uintptr_t p=((uintptr_t)sys->memp+alignment-1)&~(uintptr_t)(alignment-1);assert(p+bytes<=(uintptr_t)sys->endp);sys->memp=(void *)(p+bytes);memset((void *)p,0,bytes);return (void *)p;}
int GetInsideFileSize(unsigned int partition,unsigned int id) {(void)partition;assert(id>=145&&id<=159);return 64;}
int RequestReadInsideFile(unsigned int partition,unsigned int id,void *destination) {(void)partition;memset(destination,0,64);last_entry=id;++reads;return 0;}
int GetReadFileStatus(void) {return 0;}
int bhSetMemPvpTexture(NJS_TEXLIST *list,unsigned char *data,int offset) {(void)list;(void)data;(void)offset;++installs;return 1;}
int njSetTexture(NJS_TEXLIST *list) {(void)list;return 0;}
int njReleaseTexture(NJS_TEXLIST *list) {(void)list;++releases;return 0;}
void bhGarbageTexture(NJS_TEXMEMLIST *list,unsigned int count) {(void)list;(void)count;}
void njClipZ(float near,float far) {assert(near==-2&&far==-20000);}
void CallSystemSe(int mode,int number) {(void)mode;(void)number;}
void NameChangeSet(S_WORK *work) {assert(work==&swork);}
unsigned int *ItemSearch(unsigned short id) {(void)id;return &sys->itm[0];}
void EraseItem(unsigned int *item) {*item=0;}
void bhSetMessage(int type,int id) {assert(type==1&&id==152);++messages;}
int bhDispMessage(float x,float y,float priority,int type,int id,int color,int count) {(void)x;(void)y;(void)priority;(void)type;(void)color;(void)count;last_message=id;++messages;return 0;}
int bhDispItemName(NJS_POINT2 *position,int id,int color,int count,float priority) {(void)position;(void)color;(void)count;(void)priority;assert(id>=159&&id<=182);++names;return 0;}
int bhCkFlg(unsigned int *bits,unsigned int bit) {return !!(bits[bit/32]&(0x80000000u>>(bit%32)));}
void recvxGameBootItemViewTreePrepare(NJS_CNK_OBJECT *object) {assert(object&&((uintptr_t)object>=(uintptr_t)memory)&&((uintptr_t)object<(uintptr_t)(memory+sizeof(memory))));}
void njDrawTriangle3D(NJS_POINT3COL *points,int count,unsigned int attr) {assert(points&&points->num>=(unsigned int)count+2u&&(attr&1));triangles+=count;++bookmarks;}
int njPushMatrix(NJS_MATRIX *unused) {(void)unused;assert(matrix_depth<63);matrix_x[matrix_depth+1]=matrix_x[matrix_depth];++matrix_depth;return 1;}
int njPopMatrix(unsigned int count) {assert(matrix_depth>=count);matrix_depth-=count;return 1;}
void njUnitMatrix(NJS_MATRIX *unused) {(void)unused;matrix_depth=0;matrix_x[0]=0;}
void njTranslate(NJS_MATRIX *unused,float x,float y,float z) {(void)unused;(void)y;(void)z;matrix_x[matrix_depth]+=x;}
void njCalcPoint4(NJS_MATRIX *unused,NJS_POINT4 *in,NJS_POINT4 *out) {(void)unused;*out=*in;out->x+=matrix_x[matrix_depth];}
/* Preparation is already ported separately; fixture defines one donor at
   root0 and a translated child1 sharing its two tagged vertices. */
void npSkinConvPreparation(NJS_CNK_OBJECT *object) {np.cobj[0]=object;np.sktp[0]=np.sknp+3;np.op[0]=(NJS_POINT3){0,0,0};}
static void setup(void)
{
    memset(&state,0,sizeof(state));memset(&swork,0,sizeof(swork));memset(&sitem,0,sizeof(sitem));memset(&fvwork,0,sizeof(fvwork));
    memset(memory,0,sizeof(memory));memset(parts_22b,0,sizeof(parts_22b));memset(cen_pos,0,sizeof(cen_pos));memset(cen_pos99,0,sizeof(cen_pos99));
    sys=&state;sys->memp=memory;sys->endp=memory+sizeof(memory);sys->itm_partid=4;
    reads=installs=releases=messages=names=bookmarks=triangles=0;last_entry=last_message=0;
    sitem.mdl.objP=(NJS_CNK_OBJECT *)bhGetFreeMemory(4*sizeof(NJS_CNK_OBJECT),32);
    sitem.mdl.objP[0].child=&sitem.mdl.objP[1];sitem.mdl.objP[1].sibling=&sitem.mdl.objP[2];sitem.mdl.objP[2].sibling=&sitem.mdl.objP[3];
    swork.sspb=sys->memp;
}
static void flag(unsigned int bit) {sys->ev_flg[bit/32]|=0x80000000u>>(bit%32);}
int main(void)
{
    setup();FileFlagInit01();assert(fvwork.mode_00==1&&fvwork.page==0);
    sys->rom_no=7;assert(FileNumberSwitch(48)==8);sys->rom_no=8;sys->stg_no=1;assert(FileNumberSwitch(47)==10);
    sys->stg_no=5;sys->rom_no=51;assert(FileNumberSwitch(1)==11&&sys->ssd_flg&0x1000);assert(FileNumberSwitch(85)==2);
    assert(PlayPageCheck()==7);flag(0x96);assert(PlayPageCheck()==6);flag(0x199);assert(PlayPageCheck()==0);
    swork.itemid=85;GetFile();assert(fvwork.filenum==2&&sys->itm[383]&(1u<<2));
    sys->cb_flg=0x20000;sys->sb_id=48;sys->stg_no=0;sys->rom_no=7;GetFile();assert(fvwork.filenum==8&&sys->itm[383]&(1u<<8));
    setup();sys->itm[383]=(1u<<2)|(1u<<5)|(1u<<17);fvwork.tag=3;assert(SearchTag(1)&&fvwork.tag==5);assert(SearchTag(-1)&&fvwork.tag==5);
    sys->itm[383]=0;assert(!SearchTag(1));REJECT(SearchTag(0),"direction");
    sys->itm[383]=(1u<<2)|(1u<<5);swork.testmode=9;fvwork.tag=2;MakeTag(NULL,0);assert(bookmarks==3&&triangles==6&&sys->itm[383]&1u);
    REJECT(MakeTag(NULL,3),"bookmark group");
    setup();swork.statusflg=0;sys->itm[383]=1;fvwork.filecsr=0;SelectFile();assert(sitem.mdl.objP->pos[1]==.5f);
    sys->pad_on=4;SelectFile();assert(fvwork.filecsr==1&&fvwork.z==1);
    sys->pad_on=0;for(int i=0;i<10&&fvwork.z;++i)SelectFile();assert(!fvwork.z&&!fvwork.roll);
    fvwork.filecsr=0;fvwork.tag=0;sys->pad_ps=0x800;SelectFile();sys->pad_ps=0;
    for(int i=0;i<20&&fvwork.z;++i)SelectFile();
    assert(swork.testmode==9);
    sys->itm[383]=1u|(1u<<3);sys->pad_ps=2;SelectTag();assert(fvwork.tag==3&&names==1);
    sys->pad_ps=0x800;SelectTag();assert(fvwork.mode_00==1&&fvwork.mode_02==1&&swork.subscreenmode==0x80);
    setup();swork.itemid=85;swork.statusflg=0x40000;FileFlagInit();GetFile();StatusToFileView();
    for(int i=0;i<20;++i)ControlFileView();
    assert(fvwork.mode_00==2&&installs==1&&releases==1&&reads==1&&last_entry==151&&ViewCnt==2);
    for(int i=0;i<20;++i)ControlFileView();
    assert(!fvwork.scrol&&last_message==299);
    swork.statusflg&=~0x40u;fvwork.pagewait01=0;sys->pad_on=8;ControlFileView();assert(fvwork.page==1);sys->pad_on=0;
    for(int i=0;i<40;++i)ControlFileView();
    fvwork.pagewait01=0;sys->pad_ps=0x1000;ControlFileView();assert(fvwork.mode_00==4);sys->pad_ps=0;
    ControlFileView();assert(messages&&sys->sb_id==161);
    sys->st_flg=0x4000|0x1000;sys->pad_ps=0x800;ControlFileView();assert(fvwork.mode_00==3);
    sys->pad_ps=0;for(int i=0;i<3;++i)ControlFileView();assert(swork.testmode==0&&swork.mode==1&&sys->memp==swork.sspb);
    fvwork.filenum=24;REJECT(ControlFileView(),"file-view state");

    setup();NJS_CNK_OBJECT *root=sitem.mdl.objP;root->child=&root[1];root[1].sibling=NULL;root[1].pos[0]=1;
    NJS_CNK_MODEL *model=(void *)bhGetFreeMemory(sizeof(*model),32);root->model=model;
    HDR_PS *header=(void *)bhGetFreeMemory(128,32);header->ucType=51;header->usIndexMax=2;model->vlist=(int *)header;
    NJS_POINT4 *vertices=(void *)(header+1);vertices[0]=(NJS_POINT4){2,3,4,1};vertices[2]=(NJS_POINT4){5,6,7,1};
    int *skin=(void *)bhGetFreeMemory(7*sizeof(int),32);int data[7]={3,0,2,0,0x01000000,1,0};memcpy(skin,data,sizeof(data));
    npRetSkinConvert(root,skin);assert(vertices[0].x==2&&vertices[2].x==6&&np.obj_now==2&&!matrix_depth);
    skin[0]=4;REJECT(npRetSkinConvert(root,skin),"skin flags");skin[0]=3;skin[6]=1;REJECT(npRetSkinConvert(root,skin),"undefined reverse skin donor");
    skin[6]=0;root[1].child=root;REJECT(npRetSkinConvert(root,skin),"cyclic");
    puts("Original file viewer: acquisition/map, tag navigation, bookmark quads, resource/open/page/cancel/restore, reverse skin and bounds PASS");
    return 0;
}
