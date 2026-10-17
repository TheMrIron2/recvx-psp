/* Scalar Ninja fog state and table math from ps2_NaFog.c. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_fog.h"
#include "ps2/veronica/prog/ps2_NaFog.h"

float fNaFogNear, fNaFogFar, fNaFogA, fNaFogR, fNaFogG, fNaFogB;
unsigned int ulNaFogA, ulNaFogR, ulNaFogG, ulNaFogB;
float *fpNaFogTblTop;
float fNaFogDensity;
unsigned int ulNaFogState;
static const float default_table[128];

void recvxGameBootFogReset(void)
{
    fNaFogNear=fNaFogFar=fNaFogA=fNaFogR=fNaFogG=fNaFogB=0;
    ulNaFogA=ulNaFogR=ulNaFogG=ulNaFogB=ulNaFogState=0;
    fpNaFogTblTop=NULL; fNaFogDensity=0;
    memcpy(fNaFogTbl,default_table,sizeof(fNaFogTbl));
}

void njFogDisable(void) { ulNaFogState=0; }
void njFogEnable(void) { ulNaFogState=1; }

void njSetFogColor(Uint32 color)
{
    ulNaFogA=(color>>24)&255u; ulNaFogR=(color>>16)&255u;
    ulNaFogG=(color>>8)&255u; ulNaFogB=color&255u;
    fNaFogA=ulNaFogA/255.0f; fNaFogR=ulNaFogR/255.0f;
    fNaFogG=ulNaFogG/255.0f; fNaFogB=ulNaFogB/255.0f;
    /* The PSP retains the channels instead of uploading a PS2 GS register. */
}

void njSetFogTable(NJS_FOG_TABLE table) { fpNaFogTblTop=table; }

void njGenerateFogTable3(NJS_FOG_TABLE table, Float near, Float far)
{
    float first, step;
    int count, included=0;
    /* The retail implementation quantizes -far with a signed integer shift.
       Reject its undefined domains before frexp/casts and shifts. */
    if (!isfinite(near) || !isfinite(far) || far > -1.0f || far < -1073741824.0f)
        recvxGameBootUnsupported("invalid original fog near/far range");
    if (near < -65535.0f) fNaFogDensity=65536.0f;
    else {
        int exponent;
        float fraction=frexpf(-far,&exponent);
        count=exponent-1;
        int packed=((int)(fraction*65536.0f)&0xff00)|count;
        int scale=1<<count;
        float mantissa=(packed&0x7f00)/32768.0f;
        float integer=(float)(packed>>15)*scale;
        fNaFogDensity=mantissa*scale+integer;
    }
    first=far/256.0f>near ? -far : 1.0f;
    fNaFogFar=-far; fNaFogNear=-near;
    /* Retail PS2 uses the global table here, despite accepting a destination
       argument. SetFogTable retains that argument separately. */
    (void)table;
    for(count=0;count<128;++count) {
        step=(float)(1<<(count/16));
        float mantissa=(float)(count&15)+16.0f;
        fNaFogTbl[count]=first/((step*mantissa)/16.0f);
        if(fNaFogTbl[count]>=-near)++included;
    }
    if(first==-far) {
        if(included>1) {
            step=far+fNaFogTbl[included-1];
            if(step==0 || !isfinite(step)) recvxGameBootUnsupported("invalid original fog table divisor");
            for(count=0;count<included;++count)
                fNaFogTbl[count]=1.0f-((far+fNaFogTbl[count])/step);
        } else if(included==1) fNaFogTbl[0]=1;
        for(count=included;count<128;++count)fNaFogTbl[count]=0;
    }
    for(count=0;count<128;++count)fNaFogTbl[count]=255.0f-255.0f*fNaFogTbl[count];
}

static float lookup(float scaled)
{
    static const float inverse[8]={16,8,4,2,1,0.5f,0.25f,0.125f};
    uint32_t bits, base_bits;
    if(isnan(scaled))recvxGameBootUnsupported("invalid original fog lookup value");
    if(scaled<1)scaled=1;
    else if(scaled>255.9999f)scaled=255.9999f;
    memcpy(&bits,&scaled,sizeof(bits));
    int index=(int)((bits>>19)&15u)+((int)(bits>>23)-127)*16;
    if(index<0)index=0;
    else if(index>127)index=127;
    if(index==127)return fNaFogTbl[127];
    base_bits=(uint32_t)((index/16+127)<<23)|((uint32_t)(index&15)<<19);
    float base; memcpy(&base,&base_bits,sizeof(base));
    float blend=inverse[index/16]*(scaled-base);
    return (1.0f-blend)*fNaFogTbl[index]+blend*fNaFogTbl[index+1];
}

float njCalcFogPower(float depth)
{
    if(!ulNaFogState)return 255;
    if(isnan(depth) || !isfinite(fNaFogDensity) || fNaFogDensity<0)
        recvxGameBootUnsupported("invalid original fog depth or density");
    if(depth<0)return lookup(255.9999f);
    if(depth==0)return lookup(fNaFogDensity>0 ? INFINITY : NAN);
    return lookup(fNaFogDensity/depth);
}

float njCalcFogPowerEx(float inverse_depth)
{
    if(!ulNaFogState)return 255;
    if(isnan(inverse_depth) || !isfinite(fNaFogDensity) || fNaFogDensity<0)
        recvxGameBootUnsupported("invalid original fog inverse depth or density");
    if(inverse_depth<0)return fNaFogTbl[127];
    return lookup(fNaFogDensity*inverse_depth);
}
static const float default_table[128] = { 254.8735046f, 254.7428131f, 254.6076202f, 254.4675751f, 254.3222351f, 254.1712036f, 254.013916f, 253.849823f, 253.6782074f, 253.4983368f, 253.3092957f, 253.1100464f, 252.8993683f, 252.6757812f, 252.4375763f, 252.1826172f, 251.9586487f, 251.7265015f, 251.4855652f, 251.2351227f, 250.9743347f, 250.702301f, 250.4179688f, 250.1201477f, 249.8074951f, 249.4784393f, 249.131134f, 248.7634735f, 248.3729248f, 247.956543f, 247.5107727f, 247.03125f, 246.6507416f, 246.2555237f, 245.8444519f, 245.4161835f, 244.9692078f, 244.5018616f, 244.0122223f, 243.4981079f, 242.9569855f, 242.3859863f, 241.7817383f, 241.1403351f, 240.4571075f, 239.7265625f, 238.9421082f, 238.0957184f, 237.4674377f, 236.8139038f, 236.1330719f, 235.4226227f, 234.6799774f, 233.9021912f, 233.0859375f, 232.2274017f, 231.3222046f, 230.3653259f, 229.3508759f, 228.2720032f, 227.1206055f, 225.887085f, 224.5598755f, 223.125f, 222.1089478f, 221.0508881f, 219.9473419f, 218.7944489f, 217.5878754f, 216.3226624f, 214.9932251f, 213.5931702f, 212.1151276f, 210.5506287f, 208.889801f, 207.1211395f, 205.2309723f, 203.203125f, 201.018158f, 198.6524353f, 197.0351868f, 195.3495789f, 193.5899811f, 191.75f, 189.8225708f, 187.7995758f, 185.671875f, 183.4289703f, 181.0588226f, 178.5475616f, 175.8789673f, 173.0341492f, 169.9907227f, 166.7221375f, 163.1964722f, 159.375f, 156.8328705f, 154.1814117f, 151.4115448f, 148.5131073f, 145.4746246f, 142.2831879f, 138.9240112f, 135.380249f, 131.6325531f, 127.6585541f, 123.4322739f, 118.9232407f, 114.0954895f, 108.9062424f, 103.3041458f, 97.22679901f, 93.27104187f, 89.14274597f, 84.82763672f, 80.30953979f, 75.57035828f, 70.58953857f, 65.34374237f, 59.80628204f, 53.9464798f, 47.72891998f, 41.11236572f, 34.04853058f, 26.4804287f, 18.34020233f, 9.546380043f, 0 };
float fNaFogTbl[128] = { 254.8735046f, 254.7428131f, 254.6076202f, 254.4675751f, 254.3222351f, 254.1712036f, 254.013916f, 253.849823f, 253.6782074f, 253.4983368f, 253.3092957f, 253.1100464f, 252.8993683f, 252.6757812f, 252.4375763f, 252.1826172f, 251.9586487f, 251.7265015f, 251.4855652f, 251.2351227f, 250.9743347f, 250.702301f, 250.4179688f, 250.1201477f, 249.8074951f, 249.4784393f, 249.131134f, 248.7634735f, 248.3729248f, 247.956543f, 247.5107727f, 247.03125f, 246.6507416f, 246.2555237f, 245.8444519f, 245.4161835f, 244.9692078f, 244.5018616f, 244.0122223f, 243.4981079f, 242.9569855f, 242.3859863f, 241.7817383f, 241.1403351f, 240.4571075f, 239.7265625f, 238.9421082f, 238.0957184f, 237.4674377f, 236.8139038f, 236.1330719f, 235.4226227f, 234.6799774f, 233.9021912f, 233.0859375f, 232.2274017f, 231.3222046f, 230.3653259f, 229.3508759f, 228.2720032f, 227.1206055f, 225.887085f, 224.5598755f, 223.125f, 222.1089478f, 221.0508881f, 219.9473419f, 218.7944489f, 217.5878754f, 216.3226624f, 214.9932251f, 213.5931702f, 212.1151276f, 210.5506287f, 208.889801f, 207.1211395f, 205.2309723f, 203.203125f, 201.018158f, 198.6524353f, 197.0351868f, 195.3495789f, 193.5899811f, 191.75f, 189.8225708f, 187.7995758f, 185.671875f, 183.4289703f, 181.0588226f, 178.5475616f, 175.8789673f, 173.0341492f, 169.9907227f, 166.7221375f, 163.1964722f, 159.375f, 156.8328705f, 154.1814117f, 151.4115448f, 148.5131073f, 145.4746246f, 142.2831879f, 138.9240112f, 135.380249f, 131.6325531f, 127.6585541f, 123.4322739f, 118.9232407f, 114.0954895f, 108.9062424f, 103.3041458f, 97.22679901f, 93.27104187f, 89.14274597f, 84.82763672f, 80.30953979f, 75.57035828f, 70.58953857f, 65.34374237f, 59.80628204f, 53.9464798f, 47.72891998f, 41.11236572f, 34.04853058f, 26.4804287f, 18.34020233f, 9.546380043f, 0 };
