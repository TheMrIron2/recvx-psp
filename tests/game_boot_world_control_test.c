/* Exercise original object callbacks and matrix propagation with owned assets. */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/objitm.h"
#include "ps2/veronica/prog/weapon.h"

static SYS_WORK work;
static ROM_WORK room;
static BH_PWORK player;
static struct {
    O_WRK objects[32], items[32];
    NJS_CNK_OBJECT nodes[64];
    O_WORK matrices[64];
    LGT_WORK lights[4];
    NJS_CNK_MODEL fade_model;unsigned char fade_material[12];
    unsigned char hair[0x5000];
    unsigned char allocations[0x10000];
} data;
SYS_WORK *sys = &work;
ROM_WORK *rom = &room;
BH_PWORK *plp = &player;
BH_PWORK ene[128];
CAM_WORK cam;
static int wall_calls,wall_hit;
static float wall_length;
O_WRK eff[512];
unsigned char *freemem;
unsigned char pl_sleep_cnt;
static jmp_buf fault;
static const char *fault_name;
static int expecting_fault;
static unsigned int sounds, effects;
extern const int WpnAngTab[21][3];
void recvxGameBootWorldQueueCheck(int *count, size_t capacity);

void recvxGameBootUnsupported(const char *name)
{
    if (!expecting_fault) { fprintf(stderr, "unexpected world fault: %s\n", name); abort(); }
    fault_name = name; longjmp(fault, 1);
}
void bhCheckWall(BH_PWORK *p) { (void)p; recvxGameBootUnsupported("bhCheckWall"); }
void CallSystemSe(int se, unsigned int flags)
{ assert(se == 0 && flags == 0x80000240u); ++sounds; }
int bhSetEffectTb(EF_WORK *effect, NJS_POINT3 *offset, unsigned char *link, int joint)
{ assert(effect->id == 2 && !offset && !link && !joint); ++effects; return 0; }

int bhCheckL2Wall(NJS_LINE *line,unsigned int flags,float *length)
{
    assert(flags==1024&&isfinite(line->px)&&isfinite(line->vx));
    float distance=sqrtf(line->vx*line->vx+line->vy*line->vy+line->vz*line->vz);
    assert(fabsf(distance-200)<0.01f);++wall_calls;*length=wall_length;return wall_hit;
}

void npSetAllMatAlphaColor(NJS_CNK_OBJECT *objects,int count,unsigned char alpha)
{assert(count==1&&objects->model==&data.fade_model);((unsigned char *)objects->model->plist)[7]=alpha;}

#define REJECT(call, message) do { \
    fault_name = NULL; expecting_fault = 1; \
    if (!setjmp(fault)) { call; assert(!"expected world fault"); } \
    expecting_fault = 0; assert(fault_name && strstr(fault_name, message)); \
} while (0)

static int close_to(float a, float b) { return fabsf(a - b) < 0.0001f; }
static void model(O_WRK *op, unsigned int node, unsigned int count)
{
    op->mlwP = &op->mdl[0]; op->mlwP->obj_num = count;
    op->mlwP->objP = &data.nodes[node]; op->mlwP->owP = &data.matrices[node];
    op->mtx = (NJS_MATRIX *)op->mtxbuf;
    njUnitMatrix(op->mtx);
    for (unsigned int i = 0; i < count; ++i) {
        NJS_CNK_OBJECT *obj = &data.nodes[node + i];
        if (i + 1 < count) obj->child = &data.nodes[node + i + 1];
        njUnitMatrix((NJS_MATRIX *)data.matrices[node + i].mtx);
    }
}
static void reset(void)
{
    memset(&work, 0, sizeof(work)); memset(&room, 0, sizeof(room));
    memset(&cam,0,sizeof(cam));wall_calls=wall_hit=0;wall_length=0;
    memset(&player, 0, sizeof(player)); memset(&data, 0, sizeof(data));
    freemem = (unsigned char *)&data; sys->endp = freemem + sizeof(data);
    sys->memp = data.allocations; sys->obwp = data.objects; sys->itwp = data.items;
    sys->pletcp = data.hair; sys->sp_flg = 4; sys->pt_flg = 7;
    player.flg = 1; pl_sleep_cnt = 0; sounds = effects = 0;
    recvxEngineOriginalFaultReset(); recvxEngineOriginalMatrixReset();
    for (unsigned int i = 0; i < 4; ++i) model(&data.objects[i], i * 4, 4);
    for (unsigned int i = 0; i < 2; ++i) model(&data.items[i], 16 + i * 4, 4);
    model((O_WRK *)&player, 24, 16);
}

static void ordinary_objects_and_items(void)
{
    O_WRK *op, *item;
    reset(); rom->obj_n = 2; rom->itm_n = 1;
    op = &data.objects[0]; op->flg = 1; op->type = 2; op->aspd = 2; op->px = 12;
    data.objects[1].flg = 1; data.objects[1].type = 11; data.objects[1].id = 1201;
    item = &data.items[0]; item->flg = 1; item->aspd = 1; item->ct0 = 1; item->ct1 = item->ct2 = 4;
    bhControlObjItm();
    assert(op->pxb == 12 && op->ayb == 0 && op->ay == 182);
    assert(sys->onow == 1 && sys->ob_nlgn == 1 && sys->ob_nlg[0] == op);
    assert(sys->ob_hlgn == 1 && sys->ob_hlg[0] == &data.objects[1]);
    assert(item->ct0 == 2 && item->ct1 == 0 && (item->mdflg & 0x80));
    assert(close_to(op->mlwP->owP[0].mtx[12], 12));
    for (unsigned int i = 0; i < 8; ++i) bhControlObjItm();
    assert(item->ct0 == 3 && item->ayp == 65536 && !(item->mdflg & 0x80));
    op->type = 3; op->ct0 = 0; bhControlObjItm(); assert(op->az == 0 && op->ct0 == 1024);
    bhControlObjItm(); assert(op->az > 0 && op->ct0 == 2048);
    op->type = 11; bhControlObjItm(); assert(op->mdflg & 8);
    reset(); sys->sp_flg = 0; rom->obj_n = INT_MAX; bhControlObjItm();
    reset(); sys->cb_flg = 0x40000000; rom->obj_n = INT_MAX; rom->itm_n = INT_MAX;
    data.objects[3].flg = 1; bhControlObjItm(); assert(sys->onow == 3); /* Original four-slot menu mode. */
}

static void linked_equipment(void)
{
    O_WRK *op;
    reset(); rom->obj_n = 1; op = &data.objects[0];
    op->flg = 0x81; op->id = 1201; op->lkono = 2; op->lkwkp = (unsigned char *)&player;
    player.stflg = 0x1000000; op->px = 9; bhControlObjItm();
    assert((op->stflg & 0x1000000) && op->pxb == 0);
    player.stflg = 0; data.matrices[26].mtx[12] = 20; op->lox = 3;
    bhControlObjItm(); assert(!(op->stflg & 0x1000000) && op->pxb == 9 && op->px == 23);
    assert(close_to(op->mlwP->owP[0].mtx[12], 23));
    op->id = 1210; op->type = 2; op->lkono = 9;
    bhControlObjItm();
    assert(op->ax == WpnAngTab[2][0] && op->ay == WpnAngTab[2][1] && op->az == WpnAngTab[2][2]);
    op->flg |= 0x80000; op->mode0 = 0; bhControlObjItm();
    assert(op->mode0 == 1 && close_to(op->mlwP->objP[2].pos[0], -0.3f));
    for (unsigned int i = 0; i < 7; ++i) bhControlObjItm();
    assert(!(op->flg & 0x80000) && op->mode0 == 0 && op->mlwP->objP[2].pos[0] == 0);
    op->id = 1211; op->mode0 = 0; bhControlObjItm();
    assert(op->mode0 == 1 && op->exp0 == data.hair + 4096 && op->exp3 == data.hair);
    player.flg2 = 2; bhControlObjItm(); assert(!(player.flg2 & 2));
    for (unsigned int i = 0; i < 25; ++i) bhControlObjItm();
    assert(op->ct0 == 26);
    for (unsigned int i = 0; i < 4; ++i) {
        assert(isfinite(op->pv[i].x) && isfinite(op->pv[i].y) && isfinite(op->pv[i].z));
        assert(op->mlwP->objP[i].ang[0] >= -32768 && op->mlwP->objP[i].ang[0] <= 32768);
    }
}

static void wall_and_box(void)
{
    O_WRK *op;
    reset(); rom->obj_n = 1; op = &data.objects[0]; op->flg = 1; op->type = 12;
    op->px = 4; op->pz = 8; rom->grand[2] = 2;
    bhControlObjItm(); assert(sys->mwal_n == 1 && op->ct0 == 0 && op->mode0 == 1);
    assert(sys->mwalp[0].px == 4 && sys->mwalp[0].py == 2 && sys->mwalp[0].pz == 8);
    data.matrices[0].mtx[12] = 9; bhControlObjItm(); assert(sys->mwalp[0].px == 9);
    reset(); rom->obj_n = 1; op = &data.objects[0]; op->flg = 1; op->type = 100;
    bhControlObjItm(); assert(sounds == 1 && op->mode0 == 1 && op->ct0 == 384);
    for (unsigned int i = 0; i < 43; ++i) bhControlObjItm();
    assert(sounds == 1 && (sys->cb_flg & 0x40000) && op->type == 0 && op->ax == 0);
}

static void searchlight_controller(void)
{
    reset();rom->obj_n=1;rom->evlp=data.lights;rom->evl_n=4;
    O_WRK *op=&data.objects[0];op->flg=1;op->type=6;op->aspd=1;op->px=2;op->py=3;op->pz=4;
    cam.wpx=12;cam.wpy=3;cam.wpz=4;
    bhControlObjItm();
    assert(wall_calls==1&&op->mlwP->objP[2].ang[2]==-16384);
    assert(close_to(data.lights[1].nr,20)&&close_to(data.lights[1].fr,30));
    assert(fabsf(data.lights[1].pz+166)<0.001f&&close_to(data.lights[1].px,2));
    wall_hit=1;wall_length=140;bhControlObjItm();
    assert(close_to(data.lights[1].nr,14)&&close_to(data.lights[1].fr,24));
    assert(fabsf(data.lights[1].pz+106)<0.001f);
    wall_length=20;bhControlObjItm();
    assert(close_to(data.lights[1].pz,op->pz)&&close_to(data.lights[1].nr,10)&&close_to(data.lights[1].fr,20));
    wall_hit=0;op->ay=16384;bhControlObjItm();
    assert(fabsf(data.lights[1].px+168)<0.001f&&fabsf(data.lights[1].pz-4)<0.03f);
    int prior=wall_calls;op->aspd=0;rom->evlp=NULL;cam.wpx=-10;bhControlObjItm();assert(wall_calls==prior);
    op->mlwP->obj_num=2;REJECT(bhControlObjItm(),"searchlight model");
    op->mlwP->obj_num=4;op->aspd=4;rom->evlp=data.lights;REJECT(bhControlObjItm(),"searchlight event light");
    op->aspd=1;rom->evlp=(LGT_WORK *)1;REJECT(bhControlObjItm(),"searchlight event light");
    op->aspd=0;cam.wpx=NAN;REJECT(bhControlObjItm(),"searchlight model or camera");
    puts("PASS: original searchlight dispatch, camera-facing joint, wall/no-wall beam placement, near/far light ranges, rotated beam and invalid model/light boundaries");
}

static void alpha_fade_controller(void)
{
    reset();rom->obj_n=1;O_WRK *op=&data.objects[0];op->flg=0x200001;op->type=0;
    op->mlwP->objP[0].model=&data.fade_model;data.fade_model.plist=(short *)data.fade_material;
    int joints[2]={0,1};memcpy(&op->pv[0],joints,sizeof(joints));op->pv[1].x=200;op->pv[1].y=4;
    for(int frame=1;frame<=4;++frame){bhControlObjItm();assert(data.fade_material[7]==frame*50);}
    assert(!(op->flg&0x200000)&&op->pv[1].y==0);
    op->flg|=0x200000;op->pv[1].x=0;op->pv[1].y=2;
    bhControlObjItm();assert(data.fade_material[7]==100);bhControlObjItm();assert(!data.fade_material[7]&&!(op->flg&0x200000));
    op->flg|=0x200000;op->pv[1].x=128;op->pv[1].y=0;bhControlObjItm();assert(data.fade_material[7]==128&&!(op->flg&0x200000));
    op->flg|=0x200000;op->pv[1].y=1;joints[0]=4;memcpy(&op->pv[0],joints,sizeof(joints));
    REJECT(bhControlObjItm(),"object alpha fade");
    puts("PASS: original object alpha interpolation, fade-in/out, completion flags and joint range rejection");
}

static void invalid_state(void)
{
    O_WRK *volatile op;
    int count = 32;
    reset(); rom->obj_n = INT_MAX; REJECT(bhControlObjItm(), "work tables");
    reset(); rom->itm_n = -1; REJECT(bhControlObjItm(), "work tables");
    reset(); rom->obj_n = 1; sys->obwp = (O_WRK *)1; REJECT(bhControlObjItm(), "work tables");
    reset(); rom->obj_n = 1; op = &data.objects[0]; op->flg = 1; op->type = 101;
    REJECT(bhControlObjItm(), "callback index");
    op->type = 1; REJECT(bhControlObjItm(), "bhObj001");
    op->id = 1200; bhControlObjItm(); /* Original hair callback does nothing without a parent. */
    op->id = 1213; REJECT(bhControlObjItm(), "callback index");
    op->id = 0; op->type = 0; op->mlwP = (ML_WORK *)1;
    REJECT(bhControlObjItm(), "model work");
    reset(); rom->obj_n = 1; op = &data.objects[0]; op->flg = 0x81; op->lkwkp = (unsigned char *)1;
    REJECT(bhControlObjItm(), "parent work");
    op->lkwkp = (unsigned char *)&player; op->lkono = 16;
    REJECT(bhControlObjItm(), "parent joint");
    op->lkono = 0; player.mlwP->owP = (O_WORK *)1;
    REJECT(bhControlObjItm(), "model work");
    reset(); rom->obj_n = 1; op = &data.objects[0]; op->flg = 1; op->type = 12; sys->mwal_n = 64;
    REJECT(bhControlObjItm(), "wall allocation");
    op->mode0 = 1; op->ct0 = 64; REJECT(bhControlObjItm(), "wall index");
    op->type = 0; op->flg = 0x200001; op->pv[1].x=NAN; REJECT(bhControlObjItm(), "object alpha fade");
    op->pv[1].x=0;op->flg = 1; op->px = NAN; REJECT(bhControlObjItm(), "transform");
    reset(); rom->obj_n = 1; op = &data.objects[0]; op->flg = 0x81; op->id = 1211;
    op->lkwkp = (unsigned char *)&player; sys->pletcp = NULL;
    REJECT(bhControlObjItm(), "hair allocation");
    sys->pletcp = data.hair; op->mode0 = 1;
    REJECT(bhControlObjItm(), "hair state");
    op->exp0 = data.hair + 4096; op->exp3 = data.hair; op->ct0 = 128;
    REJECT(bhControlObjItm(), "hair state");
    reset(); rom->obj_n = 1; op = &data.objects[0]; op->flg = 0x81; op->id = 1211;
    memset(&ene[0], 0, sizeof(ene[0])); ene[0].flg = 1;
    model((O_WRK *)&ene[0], 24, 16); op->lkwkp = (unsigned char *)&ene[0];
    sys->memp = sys->endp;
    REJECT(bhControlObjItm(), "arena exhausted");
    REJECT(recvxGameBootWorldQueueCheck(&count, 32), "queue is full");
    count = -1; REJECT(recvxGameBootWorldQueueCheck(&count, 32), "queue is full");
}

int main(void)
{
    ordinary_objects_and_items(); linked_equipment(); wall_and_box(); searchlight_controller(); alpha_fade_controller(); invalid_state();
    puts("PASS: original object/item scheduling, rotation, pickup animation, queues, linked weapon action/hair matrices, moving walls, item-box sound, and invalid work boundaries");
    return 0;
}
