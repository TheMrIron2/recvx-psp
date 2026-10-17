#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_skin.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"

NP_WORK np;
SYS_WORK state, *sys = &state;
BH_PWORK player, *plp = &player, ene[128];
O_WRK eff[512];
unsigned char *freemem;
static struct {
    NJS_CNK_OBJECT object[128]; NJS_CNK_MODEL model[128]; O_WORK joint[128];
    struct { HDR_PS header; float vertex[3][8]; unsigned int end; } stream[128];
    unsigned int table[1024];
} data;
static jmp_buf fault;
static const char *fault_name;
static unsigned int assertions;
static int expecting;
static const unsigned char *morph_source;
static size_t morph_size;
#define CHECK(x) do { assert(x); ++assertions; } while (0)
#define NEAR(a,b) CHECK(fabsf((a)-(b)) < 0.0001f)
#define REJECT(call, text) do { expecting=1; fault_name=NULL; \
    if (!setjmp(fault)) { call; assert(!"expected skin fault"); } \
    expecting=0; CHECK(fault_name && strstr(fault_name,text)); } while (0)

void recvxGameBootUnsupported(const char *message)
{
    if (!expecting) { fprintf(stderr,"unexpected skin fault: %s\n",message); abort(); }
    fault_name=message; longjmp(fault,1);
}
size_t recvxGameBootSourceRemaining(const void *pointer) { (void)pointer; return 0; }
size_t recvxGameBootMorphRemaining(const void *pointer)
{
    uintptr_t p=(uintptr_t)pointer, first=(uintptr_t)morph_source;
    return morph_source && p>=first && p-first<morph_size ? morph_size-(p-first) : 0;
}

static void identity(float *matrix)
{ memset(matrix,0,64); matrix[0]=matrix[5]=matrix[10]=matrix[15]=1; }
static void reset(void)
{
    recvxGameBootSkinReset(); memset(&state,0,sizeof(state)); memset(&player,0,sizeof(player));
    memset(&data,0,sizeof(data)); memset(&np,0,sizeof(np));
    morph_source=NULL; morph_size=0;
    freemem=(unsigned char *)&data; state.endp=freemem+sizeof(data);
    player.mlwP=&player.mdl[0]; player.mlwP->obj_num=128;
    player.mlwP->objP=data.object; player.mlwP->owP=data.joint;
    for (unsigned int i=0;i<128;++i) {
        data.object[i].model=&data.model[i]; data.model[i].vlist=(int *)&data.stream[i];
        identity(data.joint[i].mtx);
        data.stream[i].header.ucType=51; data.stream[i].header.ucAttr=7;
        data.stream[i].header.usIndexOfs=20; data.stream[i].header.usIndexMax=3;
        data.stream[i].header.usSize=(64+3*32-4)/4;
        memset(data.stream[i].header.ucPadding,0xab,56);
        for (unsigned int j=0;j<3;++j) {
            data.stream[i].vertex[j][0]=1; data.stream[i].vertex[j][4]=2;
            unsigned int packed=0x7fc00001u+j;
            memcpy(&data.stream[i].vertex[j][3],&packed,4);
            packed=0xff800001u+j; memcpy(&data.stream[i].vertex[j][7],&packed,4);
        }
        data.stream[i].end=255;
    }
    recvxEngineOriginalMatrixReset(); recvxEngineOriginalFaultReset();
}
static void table(unsigned int output, unsigned int bone)
{
    data.table[0]=2; data.table[1]=output; data.table[2]=3;
    data.table[3]=bone<<24; data.table[4]=(bone<<24)|(9u<<16);
    data.table[5]=(bone<<24)|(4u<<16);
    data.table[6]=1; data.table[7]=0;
}

static void weighted_math(void)
{
    reset(); table(5,1);
    player.px=10;player.py=20;player.pz=30;
    float *m=data.joint[1].mtx;
    m[0]=0;m[1]=1;m[4]=-1;m[5]=0;m[12]=100;m[13]=200;m[14]=300;
    data.object[1].ang[2]=0x4000;
    NJS_MATRIX before; njTranslate(NULL,7,8,9); njGetMatrix(&before);
    npCalcSkin(&player,2,(int *)data.table);
    CHECK(data.model[0].vlist==np.vlp[5]); CHECK(data.model[1].vlist==(int *)&data.stream[1]);
    CHECK(!((uintptr_t)np.vlp[5]&63u));
    float (*out)[8]=(float (*)[8])((HDR_PS *)np.vlp[5]+1);
    NEAR(out[0][0],90.9f); NEAR(out[0][1],180.1f); NEAR(out[0][2],270);
    NEAR(out[0][4],1.8f); NEAR(out[0][5],0.2f); NEAR(out[0][6],0);
    NEAR(out[1][0],90); NEAR(out[1][1],181); NEAR(out[1][4],0); NEAR(out[1][5],2);
    NEAR(out[2][0],90.5f); NEAR(out[2][1],180.5f); NEAR(out[2][4],1); NEAR(out[2][5],1);
    for (unsigned int i=0;i<3;++i) {
        CHECK(!memcmp(&out[i][3],&data.stream[0].vertex[i][3],4));
        CHECK(!memcmp(&out[i][7],&data.stream[0].vertex[i][7],4));
    }
    CHECK(!memcmp(np.vlp[5],&data.stream[0].header,sizeof(HDR_PS)));
    CHECK(*(unsigned int *)(out+3)==255u);
    CHECK(recvxGameBootSkinRemaining(out+3)==4);
    CHECK(recvxGameBootSkinRemaining((unsigned char *)(out+3)+4)==0);
    CHECK(recvxGameBootSkinRemaining(np.bp)==0);
    CHECK(!memcmp(pNaMatMatrixStuckPtr,&before,sizeof(before)));
    CHECK(!recvxEngineOriginalFault());
    CHECK(data.stream[0].vertex[0][0]==1); /* Original stream remains immutable. */
}

static void fm_and_last_bone(void)
{
    reset(); table(127,127);
    unsigned int cursor=6;
    for (unsigned int i=1;i<128;++i) { data.table[cursor++]=i==127?1:0; if(i==127)data.table[cursor++]=0; }
    data.joint[127].mtx[12]=17;
    npCalcSkin(&player,128,(int *)data.table);
    float *out=(float *)((HDR_PS *)np.vlp[127]+1); NEAR(out[0],18);
    reset(); table(0,11); cursor=6;
    for(unsigned int i=1;i<16;++i) {
        if(i>=11&&i<=14)continue;
        data.table[cursor++]=i==15?1:0; if(i==15)data.table[cursor++]=0;
    }
    data.joint[15].mtx[12]=40;
    npCalcSkinFM(&player,16,(int *)data.table);
    out=(float *)((HDR_PS *)np.vlp[0]+1); NEAR(out[0],41);
    for(unsigned int i=11;i<=14;++i)CHECK(data.model[i].vlist==(int *)&data.stream[i]);
    CHECK(np.sknp==(int *)&data.table[cursor]);
    reset(); table(2,1); npInitCalcSkin(&player,2,(int *)data.table);
    CHECK(np.vlp[2]); CHECK(data.model[0].vlist==(int *)&data.stream[0]);
    npInitCalcSkinFM(&player,2,(int *)data.table); CHECK(np.vlp[2]);
    recvxGameBootSkinReset(); CHECK(!recvxGameBootSkinRemaining(out)); CHECK(!np.buff);
    npCalcSkin(NULL,-1,NULL); npCalcSkinFM(NULL,-1,NULL); /* Original null-table no-op. */
}

static void invalid_inputs(void)
{
    reset(); table(0,1); data.table[3]|=10u<<16;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"weight or bone"); CHECK(data.model[0].vlist==(int *)&data.stream[0]);
    reset(); table(0,2); REJECT(npCalcSkin(&player,2,(int *)data.table),"weight or bone");
    reset(); table(128,1); REJECT(npCalcSkin(&player,2,(int *)data.table),"output or weight");
    reset(); table(0,1); data.table[6]=0; REJECT(npCalcSkin(&player,2,(int *)data.table),"inactive bone");
    reset(); table(0,1); data.stream[0].header.usIndexMax=2;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"vertex extent");
    reset(); table(0,1); data.stream[0].header.ucType=41;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"vertex command");
    reset(); table(0,1); data.stream[0].vertex[0][0]=NAN;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"vertex or normal");
    reset(); table(0,1); data.joint[1].mtx[0]=INFINITY;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"joint matrix");
    reset(); table(0,1); REJECT(npCalcSkin(&player,129,(int *)data.table),"joint array");
    REJECT(npCalcSkin(&player,-1,(int *)data.table),"joint array");
    REJECT(npCalcSkin((void *)1,2,(int *)data.table),"skin actor");
    REJECT(npCalcSkin(&player,2,(int *)1),"table exceeds source");
    reset(); table(0,1); state.endp=(unsigned char *)&data.table[5];
    REJECT(npCalcSkin(&player,2,(int *)data.table),"weight extent");
    reset(); table(0,1); data.model[0].vlist=(int *)1;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"source model");
    reset(); table(0,1); player.mlwP=(ML_WORK *)1;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"model work");
    reset(); table(0,1); player.px=NAN;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"model work");
    reset(); table(0,1); player.px=-3.0e38f; data.joint[1].mtx[12]=3.0e38f;
    REJECT(npCalcSkin(&player,2,(int *)data.table),"transformed skin matrix");
    reset(); table(0,1);
    for(unsigned int i=0;i<128;++i)CHECK(njPushMatrixEx());
    REJECT(npCalcSkin(&player,2,(int *)data.table),"matrix stack overflow");
    CHECK(data.model[0].vlist==(int *)&data.stream[0]);
    reset(); table(0,1); pNaMatMatrixStuckPtr=NULL;
    REJECT(npInitCalcSkin(&player,2,(int *)data.table),"matrix stack is not initialized");
}

static void morph_owned_source(void)
{
    reset(); table(0,1);
    unsigned char *source=malloc(sizeof(data.stream[0])); CHECK(source);
    memcpy(source,&data.stream[0],sizeof(data.stream[0]));
    morph_source=source; morph_size=sizeof(data.stream[0]);
    data.model[0].vlist=(int *)source;
    npCalcSkin(&player,2,(int *)data.table);
    float *out=(float *)((HDR_PS *)np.vlp[0]+1); NEAR(out[0],1); NEAR(out[4],2);
    CHECK(!memcmp(source,&data.stream[0],sizeof(data.stream[0])));
    free(source); morph_source=NULL;
}

int main(void)
{
    weighted_math(); fm_and_last_bone(); invalid_inputs(); morph_owned_source(); recvxGameBootSkinReset();
    printf("PASS: %u skin checks, weighted positions and untranslated non-unit normals, packed metadata, 128 bones, FM remap, matrix restore and malformed bounds\n",assertions);
    return 0;
}
