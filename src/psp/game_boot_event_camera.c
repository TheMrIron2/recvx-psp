#include <math.h>
#include <stdint.h>
#include "psp/game_boot_event_camera.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"

static int room(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,first=sys?(uintptr_t)sys->rdtp:0;
    return pointer&&sys&&sys->rdtp&&sys->rdtsz>0&&!(p&3u)&&p>=first&&
        p-first<=(size_t)sys->rdtsz&&bytes<=(size_t)sys->rdtsz-(p-first);
}
static int arena(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,first=(uintptr_t)freemem,end=sys?(uintptr_t)sys->endp:0;
    return pointer&&freemem&&end>=first&&!(p&3u)&&p>=first&&p<=end&&bytes<=end-p;
}
static int member(const void *pointer,const void *base,size_t count,size_t stride)
{
    uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)base;
    return pointer&&base&&p>=b&&p-b<count*stride&&!((p-b)%stride);
}
void recvxGameBootEventCameraCheckMath(void)
{
    if(recvxEngineOriginalFault())recvxGameBootUnsupported(recvxEngineOriginalFault());
}
void recvxGameBootEventCameraStagePrepare(int stage)
{
    if(!sys||!rom||stage<0||stage>=10)recvxGameBootUnsupported("invalid original event camera stage");
}
static EVC_WORK *camera(int index)
{
    if(!sys||!rom||rom->evc_n<=0||sys->rdtsz<=0||
       (size_t)rom->evc_n>(size_t)sys->rdtsz/sizeof(EVC_WORK)||
       !room(rom->evcp,(size_t)rom->evc_n*sizeof(EVC_WORK))||index<0||index>=rom->evc_n)
        recvxGameBootUnsupported("invalid original event camera table or index");
    EVC_WORK *result=&rom->evcp[index];
    if(result->keyf_n<1||result->keyf_n>16||result->nxt_no<0||result->nxt_no>rom->evc_n)
        recvxGameBootUnsupported("invalid original event camera key count or link");
    return result;
}
static void finite_key(const CAM_KEYF_WORK *key)
{
    const float values[]={key->px,key->py,key->pz,key->fog_nr,key->fog_fr,key->lx,key->ly,key->lz};
    for(unsigned int i=0;i<sizeof(values)/sizeof(values[0]);++i)
        if(!isfinite(values[i]))recvxGameBootUnsupported("non-finite original event camera key");
    if(key->frame<0||key->lkflg<0||key->lkflg>6)
        recvxGameBootUnsupported("invalid original event camera frame or lock selector");
}
static void visibility(void)
{
    if(rom->mdl.obj_num>256||
       (rom->mdl.obj_num&&!room(rom->mdl.objP,rom->mdl.obj_num*sizeof(NJS_CNK_OBJECT)))||
       rom->evl_n<0||rom->evl_n>128||
       (rom->evl_n&&!room(rom->evlp,(size_t)rom->evl_n*sizeof(LGT_WORK))))
        recvxGameBootUnsupported("invalid original event camera visibility masks");
}
void recvxGameBootEventCameraHidePrepare(int index,int key)
{
    EVC_WORK *work=camera(index);
    if(key<0||key>=work->keyf_n)recvxGameBootUnsupported("invalid original event camera key index");
    finite_key(&work->keyf[key]);visibility();
}
static int resolved(int value,int count,int limit)
{
    if(value<0||count<=0||count>limit)recvxGameBootUnsupported("invalid original event camera lock index");
    return value>=count?0:value;
}
static void lock_target(CAM_KEYF_WORK *key)
{
    O_WRK *target=NULL;
    int index;
    if(!key->lkflg)return;
    if(key->lkono<0)recvxGameBootUnsupported("invalid original event camera lock joint");
    switch(key->lkflg){
    case 1:
        if(!plp)recvxGameBootUnsupported("missing original event camera player lock");
        target=(O_WRK *)plp;break;
    case 2:
        index=resolved(key->lkno,rom->ene_n,128);target=(O_WRK *)&ene[index];break;
    case 3:case 4:{
        int count=key->lkflg==3?rom->obj_n:rom->itm_n;
        O_WRK *base=key->lkflg==3?sys->obwp:sys->itwp;
        index=resolved(key->lkno,count,39936u/sizeof(O_WRK));
        if(!arena(base,(size_t)count*sizeof(O_WRK)))recvxGameBootUnsupported("invalid original event camera lock work array");
        target=&base[index];break;
    }
    case 5:
        index=resolved(key->lkno,rom->eff_n,450);
        if(sys->efid[index]<0||sys->efid[index]>=512)recvxGameBootUnsupported("invalid original event camera effect lock mapping");
        target=&eff[sys->efid[index]];break;
    case 6:
        index=resolved(key->lkno,rom->pos_n,65535);
        if(!room(rom->posp,(size_t)rom->pos_n*sizeof(POS)))recvxGameBootUnsupported("invalid original event camera lock position array");
        if(!isfinite(rom->posp[index].px)||!isfinite(rom->posp[index].py)||!isfinite(rom->posp[index].pz))
            recvxGameBootUnsupported("non-finite original event camera lock position");
        return;
    default:recvxGameBootUnsupported("invalid original event camera lock selector");
    }
    if(!member(target->mlwP,target->mdl,16,sizeof(ML_WORK))&&
       !member(target->mlwP,sys->efm,450,sizeof(ML_WORK)))
        recvxGameBootUnsupported("invalid original event camera lock model work");
    unsigned int count=target->mlwP->obj_num;
    if(count>512)recvxGameBootUnsupported("invalid original event camera lock model count");
    unsigned int joint=(unsigned int)key->lkono>=count?0:(unsigned int)key->lkono;
    const float *matrix;
    if(joint){
        if(!arena(target->mlwP->owP,count*sizeof(O_WORK)))recvxGameBootUnsupported("invalid original event camera lock pose array");
        matrix=target->mlwP->owP[joint].mtx;
    }else{
        if(target->mtx!=(NJS_MATRIX *)target->mtxbuf&&!arena(target->mtx,sizeof(NJS_MATRIX)))
            recvxGameBootUnsupported("invalid original event camera lock matrix");
        matrix=(const float *)target->mtx;
    }
    for(unsigned int i=0;i<16;++i)
        if(!isfinite(matrix[i]))recvxGameBootUnsupported("non-finite original event camera lock matrix");
}
void recvxGameBootEventCameraLockPrepare(CAM_KEYF_WORK *key,NJS_POINT3 *position)
{
    if(!position||((uintptr_t)position&3u))recvxGameBootUnsupported("invalid original event camera lock output");
    camera(0);
    int found=0;
    for(int i=0;i<rom->evc_n&&!found;++i){
        EVC_WORK *work=camera(i);
        found=member(key,work->keyf,work->keyf_n,sizeof(*key));
    }
    if(!found)recvxGameBootUnsupported("invalid original event camera lock key");
    finite_key(key);
    if(!key->lkflg)recvxGameBootUnsupported("inactive original event camera lock");
    lock_target(key);
}
void recvxGameBootEventCameraPrepare(int index,int key)
{
    recvxGameBootEventCameraHidePrepare(index,key);
    recvxGameBootEventCameraStagePrepare(sys->stg_no);
    if(!plp||!isfinite(plp->px)||!isfinite(plp->pz))recvxGameBootUnsupported("invalid original event camera player position");
    lock_target(&rom->evcp[index].keyf[key]);
}
void recvxGameBootEventCameraControlPrepare(void)
{
    EVC_WORK *work=camera(cam.evc_no);
    recvxGameBootEventCameraPrepare(cam.evc_no,cam.keyf_no);
    if(cam.mode0>2||cam.ct0<0)recvxGameBootUnsupported("invalid original event camera control mode or timer");
    EVC_WORK *next=work->nxt_no?camera(work->nxt_no-1):NULL;
    for(unsigned int group=0;group<2;++group){
        EVC_WORK *table=group?next:work;
        if(!table)continue;
        for(int i=0;i<table->keyf_n;++i){finite_key(&table->keyf[i]);lock_target(&table->keyf[i]);}
    }
    if(cam.keyf_no==15&&work->keyf[15].lkflg)
        recvxGameBootUnsupported("original event camera lock lookahead exceeds keys");
    if(cam.mode0){
        const float *tables[]={&cam.ips[0][0],&cam.ian[0][0],&cam.ipf[0][0],&cam.icr[0][0]};
        for(unsigned int i=0;i<4;++i)
            for(unsigned int j=0;j<60;++j)
                if(!isfinite(tables[i][j]))recvxGameBootUnsupported("non-finite original event camera spline");
    }
}
