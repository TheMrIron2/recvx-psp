#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_fog.h"
#include "ps2/veronica/prog/ps2_NaFog.h"

/* Run the untouched retail scalar functions beside the port. */
#define fNaFogNear refNear
#define fNaFogFar refFar
#define fNaFogA refA
#define fNaFogR refR
#define fNaFogG refG
#define fNaFogB refB
#define ulNaFogA refUA
#define ulNaFogR refUR
#define ulNaFogG refUG
#define ulNaFogB refUB
#define fpNaFogTblTop refTableTop
#define fNaFogTbl refTable
#define fNaFogDensity refDensity
#define ulNaFogState refState
#define njFogEnable refEnable
#define njFogDisable refDisable
#define njSetFogColor refColor
#define njSetFogTable refSetTable
#define njGenerateFogTable3 refGenerate
#define njCalcFogPower refPower
#define njCalcFogPowerEx refPowerEx
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../src/ps2/veronica/prog/ps2_NaFog.c"
#pragma GCC diagnostic pop
#undef fNaFogNear
#undef fNaFogFar
#undef fNaFogA
#undef fNaFogR
#undef fNaFogG
#undef fNaFogB
#undef ulNaFogA
#undef ulNaFogR
#undef ulNaFogG
#undef ulNaFogB
#undef fpNaFogTblTop
#undef fNaFogTbl
#undef fNaFogDensity
#undef ulNaFogState
#undef njFogEnable
#undef njFogDisable
#undef njSetFogColor
#undef njSetFogTable
#undef njGenerateFogTable3
#undef njCalcFogPower
#undef njCalcFogPowerEx

static unsigned int checks, uploads;
static jmp_buf fault;
static const char *fault_name;
static int expecting;
#define CHECK(x) do { assert(x); ++checks; } while(0)
#define EQUAL(a,b) CHECK(fabsf((a)-(b))<0.0001f)
#define REJECT(call, name) do { expecting=1; fault_name=NULL; \
    if(!setjmp(fault)){call;assert(!"expected fog fault");} \
    expecting=0;CHECK(fault_name&&strstr(fault_name,name)); } while(0)
void Ps2SetFogColor(void) { ++uploads; }
void recvxGameBootUnsupported(const char *name)
{
    if(!expecting){fprintf(stderr,"unexpected fog fault: %s\n",name);abort();}
    fault_name=name;longjmp(fault,1);
}

static void defaults_and_color(void)
{
    recvxGameBootFogReset(); CHECK(!memcmp(refTable,fNaFogTbl,sizeof(refTable)));
    CHECK(!ulNaFogState&&!fpNaFogTblTop&&!fNaFogDensity);
    CHECK(njCalcFogPower(NAN)==255 && njCalcFogPowerEx(NAN)==255);
    njSetFogColor(0x80402010);refColor(0x80402010);
    CHECK(ulNaFogA==refUA&&ulNaFogR==refUR&&ulNaFogG==refUG&&ulNaFogB==refUB);
    EQUAL(fNaFogA,refA);EQUAL(fNaFogR,refR);EQUAL(fNaFogG,refG);EQUAL(fNaFogB,refB);
    CHECK(uploads==1);
    float destination[128];for(unsigned int i=0;i<128;++i)destination[i]=-12345;
    njSetFogTable(destination);refSetTable(destination);CHECK(fpNaFogTblTop==refTableTop);
    njGenerateFogTable3(destination,-10,-1000);refGenerate(destination,-10,-1000);
    for(unsigned int i=0;i<128;++i)CHECK(destination[i]==-12345); /* Retail ignores destination. */
    njFogEnable();refEnable();CHECK(ulNaFogState==refState && ulNaFogState==1);
    njFogEnable();CHECK(ulNaFogState==1);
    njFogDisable();CHECK(!ulNaFogState);
}

static void compare_tables_and_lookups(void)
{
    const float ranges[][2]={{-10,-1000},{-1,-256},{-100,-100},{-1000,-65535},
        {-65536,-100000},{0,-2048},{-1,-1},{-2000,-4096},{-50,-3.75f}};
    for(unsigned int range=0;range<sizeof(ranges)/sizeof(ranges[0]);++range) {
        njGenerateFogTable3(NULL,ranges[range][0],ranges[range][1]);
        refGenerate(NULL,ranges[range][0],ranges[range][1]);
        EQUAL(fNaFogNear,refNear);EQUAL(fNaFogFar,refFar);EQUAL(fNaFogDensity,refDensity);
        for(unsigned int i=0;i<128;++i)EQUAL(fNaFogTbl[i],refTable[i]);
        njFogEnable();refEnable();
        /* Dense samples cover all exponent/mantissa bins and interpolation. */
        for(unsigned int i=1;i<4096;++i) {
            float depth=0.03125f*i*i;
            EQUAL(njCalcFogPower(depth),refPower(depth));
            EQUAL(njCalcFogPowerEx(1.0f/depth),refPowerEx(1.0f/depth));
        }
        EQUAL(njCalcFogPower(-1),refPower(-1));EQUAL(njCalcFogPowerEx(-1),refPowerEx(-1));
        EQUAL(njCalcFogPower(0),refPower(0));EQUAL(njCalcFogPowerEx(0),refPowerEx(0));
    }
    njFogDisable();CHECK(njCalcFogPower(100)==255&&njCalcFogPowerEx(0.01f)==255);
    recvxGameBootFogReset();CHECK(!fNaFogDensity&&!ulNaFogState&&!fpNaFogTblTop);
    EQUAL(fNaFogTbl[0],254.8735046f);EQUAL(fNaFogTbl[127],0);
}

static void invalid_inputs(void)
{
    REJECT(njGenerateFogTable3(NULL,NAN,-1000),"near/far");
    REJECT(njGenerateFogTable3(NULL,-1,NAN),"near/far");
    REJECT(njGenerateFogTable3(NULL,-1,0),"near/far");
    REJECT(njGenerateFogTable3(NULL,-1,-0.5f),"near/far");
    REJECT(njGenerateFogTable3(NULL,-1,-2147483648.0f),"near/far");
    ulNaFogState=1;fNaFogDensity=1000;
    REJECT(njCalcFogPower(NAN),"depth or density");
    REJECT(njCalcFogPowerEx(NAN),"depth or density");
    fNaFogDensity=NAN;REJECT(njCalcFogPower(10),"depth or density");
    fNaFogDensity=-1;REJECT(njCalcFogPowerEx(1),"depth or density");
    fNaFogDensity=0;REJECT(njCalcFogPower(0),"lookup value");
    REJECT(njCalcFogPowerEx(INFINITY),"lookup value");
}

int main(void)
{
    defaults_and_color();compare_tables_and_lookups();invalid_inputs();
    printf("PASS: %u fog checks against untouched retail table generation, exponent interpolation, depth/inverse lookup, channels, resets and malformed inputs\n",checks);
    return 0;
}
