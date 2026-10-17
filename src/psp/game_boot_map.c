#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_map.h"
#include "ps2/veronica/prog/main.h"

static RecvxMapAsset active_asset;
static unsigned int code_steps;
static unsigned int u16(const unsigned char *p) { return p[0]|(unsigned int)p[1]<<8; }
static uint32_t u32(const unsigned char *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int range(size_t size,size_t offset,size_t bytes) { return offset<=size && bytes<=size-offset; }
static int flag_valid(unsigned int type,unsigned int bit)
{
    switch(type) {
    case 1: case 3: return bit<1024;
    case 2: case 7: case 9: return bit<512;
    case 8: return bit<256;
    case 4: case 5: case 6: case 11: return bit<32;
    default: return 0;
    }
}
static unsigned int *flag_word(unsigned char type,unsigned int bit)
{
    /* Retail masks the encoded index to ten bits, then addresses MSB first.
       Bound the selected storage before using the original addressing rule. */
    bit &= 1023u;
    if(!sys || !flag_valid(type,bit)) recvxGameBootUnsupported("invalid original map flag storage");
    unsigned int *base=NULL;
    switch(type) {
    case 1:base=sys->ev_flg;break; case 2:base=sys->ky_flg;break;
    case 3:base=sys->ed_flg;break; case 4:base=&sys->rm_flg;break;
    case 5:base=&sys->st_flg;break; case 6:base=&sys->sp_flg;break;
    case 7:base=sys->it_flg;break; case 8:base=sys->mp_flg;break;
    case 9:base=sys->ic_flg;break; case 11:base=&sys->gm_flg;break;
    }
    return base+(bit>>5);
}
unsigned int bhFlagCk(unsigned char type,unsigned int bit,unsigned char invert)
{
    if(invert>1) recvxGameBootUnsupported("invalid original map flag check mode");
    return invert ^ ((*flag_word(type,bit) & (0x80000000u>>(bit&31u)))!=0);
}
unsigned int bhFlagSet(unsigned char type,unsigned int bit,unsigned char mode)
{
    if(mode>2) recvxGameBootUnsupported("invalid original map flag write mode");
    unsigned int *word=flag_word(type,bit), mask=0x80000000u>>(bit&31u);
    if(!mode) *word|=mask;
    else if(mode==1) *word&=~mask;
    else *word^=mask;
    return 1;
}
static const char *code_valid(const unsigned char *data,size_t size,unsigned int objects)
{
    (void)objects;
    static const unsigned char arguments[19]={0,0,0,1,1,4,3,3,1,1,1,1,2,6,2,4,4,5,3};
    if (!size || size>262144 || (size&1)) return "invalid map code size";
    size_t words=size/2, cursor=0;
    unsigned char *boundaries=calloc(words,1);
    if (!boundaries) return "map code validation allocation failed";
    const char *error=NULL;
    int terminated=0, selected=0;
    while(cursor<words) {
        boundaries[cursor]=1;
        unsigned int raw=u16(data+cursor*2), op=raw&4095u;
        if(op>18 || arguments[op]>words-cursor-1) {error="invalid map opcode or extent";break;}
        const unsigned char *args=data+(cursor+1)*2;
        if(op==2) {terminated=1;break;}
        /* Some authored files retain unreachable references to deleted model
           objects. Check the index on execution, after condition branches. */
        if(op==3) selected=1;
        if((op==4 || op==6 || op==7 || op==16) && !selected) {error="map code uses unselected object";break;}
        if(op==4 && u16(args)>=32) {error="map code palette exceeds table";break;}
        /* Unknown marker IDs are an explicit retail no-draw switch default. */
        if(op==17 && u16(args)<=5 && u16(args+2)>=32) {error="map marker palette exceeds table";break;}
        if((op==12 || op==14) && !flag_valid(u16(args),u16(args+2))) {error="invalid map flag reference";break;}
        if(op==15 && (int16_t)u16(args+6)<=0) {error="invalid map camera zoom";break;}
        if(op==18 && (u16(args)!=0 || (int16_t)u16(args+2)<=0)) {error="invalid map sound period";break;}
        cursor+=1+arguments[op];
    }
    if(!error && !terminated) error="unterminated map code";
    if(!error) {
        size_t end=cursor; cursor=0;
        while(cursor<end) {
            unsigned int op=u16(data+cursor*2)&4095u;
            if(op>=8 && op<=10) {
                unsigned int target=u16(data+(cursor+1)*2);
                if(target>=words || !boundaries[target]) {error="invalid map code jump target";break;}
            }
            cursor+=1+arguments[op];
        }
    }
    free(boundaries); return error;
}

static const char *floor_valid(const unsigned char *data,size_t size)
{
    if(!data || size<8 || memcmp(data,"FLR",3)) return "invalid map floor header";
    size_t offset=u16(data+4), count=u16(data+6);
    if(count<2 || offset<8 || !range(size,offset,count*8)) return "invalid map floor table extent";
    float previous=-INFINITY;
    for(size_t i=0;i<count;++i) {
        float label; memcpy(&label,data+offset+i*8,4);
        unsigned int map=u16(data+offset+i*8+4), floor=u16(data+offset+i*8+6);
        if(!isfinite(label) || label<previous ||
            ((map>=11 || floor>=6) && !(i==count-1 && map==99 && floor==99)))
            return "invalid map floor ordering or destination";
        previous=label;
    }
    return NULL;
}

const char *recvxGameBootMapValidateAsset(const void *source,size_t size,int index,RecvxMapAsset *asset)
{
    const unsigned char *data=source;
    RecvxMapAsset result={0};
    if(!data || !asset || size<8) return "truncated map asset";
    if(memcmp(data,"MPAC",4)) {
        /* Absent map floors are deliberately packaged as zero-filled files. */
        if(index) return "missing map index MPAC";
        for(size_t i=0;i<size;++i) if(data[i]) return "invalid map asset magic";
        *asset=result; return NULL;
    }
    size_t cursor=4u+(size_t)u32(data+4), chunks=0;
    const unsigned char *payloads[32]; size_t lengths[32];
    for(;;) {
        if(!range(size,cursor,4)) return "truncated map MPAC link";
        uint32_t length=u32(data+cursor); cursor+=4;
        if(length==UINT32_MAX) break;
        if((length&3) || !length || !range(size,cursor,length) || chunks==32)
            return "invalid map MPAC chunk extent";
        payloads[chunks]=data+cursor; lengths[chunks++]=length;
        cursor+=length;
    }
    if(cursor!=size) return "map MPAC trailing bytes";
    if(index) {
        size_t model=0;
        while(model<chunks && lengths[model]>=3 && !memcmp(payloads[model],"MDL",3)) {
            if(model==8 || recvxGameBootValidateModel(payloads[model],lengths[model])) return "invalid map marker model";
            ++model;
        }
        if(model<4 || chunks!=model+12) return "invalid map index chunk schema";
        if(recvxGameBootValidatePvp(payloads[model],lengths[model],1)) return "invalid map icon texture";
        for(size_t i=model+1;i<model+11;++i) {
            if(lengths[i]<4 || u32(payloads[i])>(lengths[i]-4)/4) return "invalid map message table";
            unsigned int count=u32(payloads[i]);
            for(unsigned int j=0;j<count;++j)
                if(!range(lengths[i],u32(payloads[i]+4+j*4),2)) return "invalid map message offset";
        }
        result.markers=(unsigned int)model;
        result.floor=payloads[chunks-1]; result.floor_size=lengths[chunks-1];
        const char *error=floor_valid(result.floor,result.floor_size); if(error)return error;
    } else {
        if(chunks!=3 || recvxGameBootValidateModel(payloads[1],lengths[1])) return "invalid map floor model";
        result.objects=u16(payloads[1]+6);
        if(!result.objects || recvxGameBootValidatePvp(payloads[2],lengths[2],1)) return "invalid map floor texture or objects";
        result.code=payloads[0]; result.code_size=lengths[0];
        const char *error=code_valid(result.code,result.code_size,result.objects); if(error)return error;
    }
    *asset=result; return NULL;
}

int recvxGameBootMapFloor(const void *source,size_t size,int room,float height)
{
    const unsigned char *data=source;
    if(!isfinite(height) || room<0 || floor_valid(data,size)) return -1;
    size_t offset=u16(data+4), count=u16(data+6), low=0, high=count;
    float key=100.0f*room+height;
    while(low<high) {
        size_t middle=low+(high-low)/2;
        float label; memcpy(&label,data+offset+middle*8,4);
        if(label<key) low=middle+1; else high=middle;
    }
    if(low==count) return -1;
    unsigned int map=u16(data+offset+low*8+4), floor=u16(data+offset+low*8+6);
    return map<11 && floor<6 ? (int)(map<<16|floor) : -1;
}

void recvxGameBootMapAssetCheck(const void *data,int index)
{
    const char *error=recvxGameBootMapValidateAsset(data,recvxGameBootSourceRemaining(data),index,&active_asset);
    if(error) recvxGameBootUnsupported(error);
}
void recvxGameBootMapCodeBegin(void) { code_steps=0; }
void recvxGameBootMapCodeStep(const short *instruction)
{
    uintptr_t p=(uintptr_t)instruction, first=(uintptr_t)active_asset.code;
    if(!active_asset.code || p<first || p-first>=active_asset.code_size || ((p-first)&1) || ++code_steps>65536)
        recvxGameBootUnsupported("map code pointer or instruction budget exceeded");
    if (((unsigned short)*instruction & 4095u)==3 && (unsigned short)instruction[1]>=active_asset.objects)
        recvxGameBootUnsupported("executed map object index exceeds model");
}
