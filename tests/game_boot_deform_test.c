#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_chunk.h"
#include "psp/game_boot_morph.h"
#include "psp/game_boot_skin.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"

NP_WORK np;
SYS_WORK state, *sys = &state;
BH_PWORK player, *plp = &player, ene[128];
O_WRK eff[512];
unsigned char *freemem;
static struct {
    NJS_CNK_OBJECT object[2], target[2];
    NJS_CNK_MODEL model, target_model;
    O_WORK joint[2];
    struct { HDR_PS header; float vertex[3][8]; unsigned int end; } source, destination;
    unsigned short polygons[16];
    unsigned int skin[8];
} data;
static unsigned int emitted;

size_t recvxGameBootSourceRemaining(const void *pointer) { (void)pointer; return 0; }
void recvxGameBootUnsupported(const char *name)
{ fprintf(stderr,"unexpected deformation integration fault: %s\n",name); abort(); }
static void identity(float *m) { memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
static int emit(void *context, const RecvxGameChunkMaterial *material, const RecvxGameChunkVertex v[3])
{
    static const float x[3]={2,2,1}, y[3]={2,3,2};
    assert(context==&data); assert(material->flags==2);
    for(unsigned int i=0;i<3;++i) {
        assert(fabsf(v[i].x-x[i])<0.00001f && fabsf(v[i].y-y[i])<0.00001f);
        assert(v[i].z==1 && fabsf(v[i].nx+0.5f)<0.00001f);
        assert(fabsf(v[i].ny)<0.00001f && v[i].nz==0.5f);
    }
    ++emitted; return 0;
}

int main(void)
{
    memset(&data,0,sizeof(data)); freemem=(unsigned char *)&data; sys->endp=freemem+sizeof(data);
    player.mlwP=&player.mdl[0]; player.mlwP->obj_num=2;
    player.mlwP->objP=data.object; player.mlwP->owP=data.joint;
    data.object[0].model=&data.model; data.target[0].model=&data.target_model;
    data.model.vlist=(int *)&data.source; data.model.plist=(short *)data.polygons;
    data.target_model.vlist=(int *)&data.destination;
    data.source.header.ucType=51; data.source.header.usSize=(64+3*32-4)/4;
    data.source.header.usIndexMax=3; data.source.end=255;
    data.source.vertex[1][0]=1; data.source.vertex[2][1]=1;
    for(unsigned int i=0;i<3;++i) {
        data.source.vertex[i][2]=1; data.source.vertex[i][5]=1;
        unsigned int packed=0x7fc12345u+i;
        memcpy(&data.source.vertex[i][3],&packed,4);
        memcpy(&data.source.vertex[i][7],&packed,4);
    }
    memcpy(&data.destination,&data.source,sizeof(data.source));
    for(unsigned int i=0;i<3;++i) {
        data.destination.vertex[i][0]+=4;
        data.destination.vertex[i][5]=0; data.destination.vertex[i][6]=1;
    }
    data.target[0].pos[0]=10;
    identity(data.joint[0].mtx); identity(data.joint[1].mtx);
    player.px=100;
    float *m=data.joint[1].mtx; m[0]=m[5]=0; m[1]=1; m[4]=-1; m[12]=102;
    data.skin[0]=2; data.skin[1]=5; data.skin[2]=3;
    for(unsigned int i=0;i<3;++i)data.skin[3+i]=(1u<<24)|(9u<<16);
    data.skin[6]=1; data.skin[7]=0;
    const unsigned short polygon[]={0x0240,5,1,3,0,1,2,255};
    memcpy(data.polygons,polygon,sizeof(polygon));
    recvxEngineOriginalMatrixReset(); recvxEngineOriginalFaultReset();
    recvxGameBootDrawSkinReset(); recvxGameBootChunkReset();
    /* Exercise real save/restore nesting across repeatedly reused allocations.
       The parser must accept both modules' owned streams outside the arena. */
    for(unsigned int frame=0;frame<200;++frame) {
        npPushMdlstr2(data.object,2);
        npCalcMorphing(data.object,data.target,500,2);
        int *morphed=data.model.vlist;
        assert(recvxGameBootMorphRemaining(morphed)==164 && fabsf(data.object[0].pos[0]-5)<0.00001f);
        npPushMdlstr(data.object,2);
        npCalcSkin(&player,2,(int *)data.skin);
        assert(data.model.vlist!=morphed && recvxGameBootSkinRemaining(data.model.vlist)==164);
        assert(recvxGameBootChunkParse(&data.model,emit,&data)==0);
        /* The reverse direction reads the owned skin stream without float W loads. */
        npTransform(data.object,data.target,0,7);
        assert(recvxGameBootMorphRemaining(data.model.vlist)==164);
        assert(recvxGameBootChunkParse(&data.model,emit,&data)==0);
        npPopMdlstr(data.object,2); assert(data.model.vlist==morphed);
        npPopMdlstr2(data.object,2);
        assert(data.model.vlist==(int *)&data.source && data.object[0].pos[0]==0);
        assert(!recvxEngineOriginalFault());
    }
    assert(emitted==400);
    recvxGameBootSkinReset(); recvxGameBootMorphReset(); recvxGameBootDrawSkinReset();
    puts("PASS: real morph/skin/strip parsing across nested saves, owned stream boundaries, packed NaN words, reverse skin-to-morph reads and 200 allocation reuse frames");
    return 0;
}
