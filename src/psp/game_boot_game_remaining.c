#include <math.h>
#include "psp/game_boot_render.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"
#include <stdint.h>
#include "psp/game_boot_game_remaining.h"
#include "psp/game_boot_player_remaining.h"
#include "psp/game_boot_event_camera.h"
#include "ps2/veronica/prog/main.h"

static int owned(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,first=(uintptr_t)freemem,end=sys?(uintptr_t)sys->endp:0;
    return pointer&&!(p&3u)&&((freemem&&p>=first&&p<=end&&bytes<=end-p)||recvxGameBootSourceRemaining(pointer)>=bytes);
}
void recvxGameBootFixedEventPrepare(int cut,int key)
{ recvxGameBootEventCameraHidePrepare(cut,key); }
void recvxGameBootEyeInitPrepare(void)
{
    if(!sys||!rom||rom->mdl.obj_num>512||
       (rom->mdl.obj_num&&!owned(rom->mdl.objP,rom->mdl.obj_num*sizeof(NJS_CNK_OBJECT)))||
       rom->lgt_n<0||rom->lgt_n>256||(rom->lgt_n&&!owned(rom->lgtp,rom->lgt_n*sizeof(LGT_WORK))))
        recvxGameBootUnsupported("invalid eye camera room models/lights");
    recvxPlayerPose(plp,5);
}
void recvxGameBootAntPrepare(BH_PWORK *actor)
{
    if(!sys||!actor||actor->mode0>=6||actor->mode3>=4||actor->type>5)
        recvxGameBootUnsupported("invalid ant controller selectors");
    for(unsigned int i=0;i<4;++i)
        if(!actor->mdl[i].obj_num||actor->mdl[i].obj_num>64||
            !owned(actor->mdl[i].objP,actor->mdl[i].obj_num*sizeof(NJS_CNK_OBJECT))||
            !owned(actor->mdl[i].owP,actor->mdl[i].obj_num*sizeof(O_WORK)))
            recvxGameBootUnsupported("invalid ant model variants");
    unsigned int motion=actor->mode0?actor->mtn_no:(actor->type&1u);
    if(motion>=512||!owned(actor->mnwP,(motion+1u)*sizeof(MN_WORK))||!actor->mnwP[motion].frm_num)
        recvxGameBootUnsupported("invalid ant motion");
}
void recvxGameBootScopePrepare(void)
{
    if(!sys||!owned(sys->obwp,sizeof(O_WRK))||!sys->obwp->mlwP||
       !owned(sys->obwp->mlwP,sizeof(ML_WORK))||!sys->obwp->mlwP->texP||
       sys->obwp->mlwP->texP->nbTexture<3)
        recvxGameBootUnsupported("missing weapon scope textures");
}
void recvxGameBootSmallCapturePrepare(void)
{
    if(!sys||sys->ren_idx[0]<0||sys->ren_idx[0]>=512)
        recvxGameBootUnsupported("invalid monitor capture effect index");
}

void recvxGameBootDrawCaptureStrip(NJS_TEXTUREH_VTX *vertices,int count)
{
    if(!vertices||count<3||count>32)recvxGameBootUnsupported("invalid captured screen strip");
    njSetTextureNumG(1);
    for(int i=2;i<count;++i){
        int a=i-2,b=i-1;if(i&1){int swap=a;a=b;b=swap;}
        const int order[]={a,b,i};RecvxGameChunkVertex triangle[3];
        for(int j=0;j<3;++j){
            const NJS_TEXTUREH_VTX *v=&vertices[order[j]];
            triangle[j]=(RecvxGameChunkVertex){.x=v->x,.y=v->y,.z=v->z,.u=v->u,.v=v->v,.color=v->bcol};
        }
        recvxGameBootRenderScreenTriangle(triangle,0);
    }
}
