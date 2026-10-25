#include <limits.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_chunk.h"
#include "ps2/veronica/prog/main.h"

extern size_t recvxGameBootSkinRemaining(const void *) __attribute__((weak));
extern size_t recvxGameBootMorphRemaining(const void *) __attribute__((weak));

_Static_assert(sizeof(float)==4&&FLT_RADIX==2&&FLT_MANT_DIG==24&&FLT_MAX_EXP==128,"chunk floats must use IEEE binary32");
_Static_assert(sizeof(RecvxGameChunkVertex)==40&&_Alignof(RecvxGameChunkVertex)>=4,"decoded vertices require aligned words");

#define CHUNK_COMMAND_LIMIT 65536u
#define CHUNK_TRIANGLE_LIMIT 65536u

static const char *chunk_fault;
static unsigned int join_skipped_vertices;
unsigned int recvxGameBootChunkJoinSkippedVertices(void){return join_skipped_vertices;}
static RecvxGameChunkMaterial current_material = {
    .diffuse = 0xffffffffu, .ambient = 0xffffffffu, .specular = 0x11ffffffu,
    .texture = UINT_MAX, .sampler = 4, .blend_source = 8, .blend_destination = 6
};

static unsigned int u16(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static unsigned int u32(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }
static int finite_word(const unsigned char *p)
{
    uint32_t bits;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(&bits,__builtin_assume_aligned(p,4),sizeof(bits));
#else
    bits=u32(p);
#endif
    return (bits&0x7f800000u)!=0x7f800000u;
}
static inline float f32(const unsigned char *p)
{
    /* Vertex streams were checked for word alignment and have word-sized
       headers/strides. Preserve bits with one aligned load on Allegrex. */
    uint32_t bits;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(&bits,__builtin_assume_aligned(p,4),sizeof(bits));
#else
    bits=u32(p);
#endif
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
static int fail(const char *name, int result)
{ chunk_fault = name; return result; }
static size_t remaining(const void *pointer, unsigned int alignment)
{
    uintptr_t first = (uintptr_t)freemem, end = sys ? (uintptr_t)sys->endp : 0;
    uintptr_t p = (uintptr_t)pointer;
    size_t size, source;
    if (!pointer || (p & (alignment - 1u))) return 0;
    if (recvxGameBootSkinRemaining && (size = recvxGameBootSkinRemaining(pointer))) return size;
    if (recvxGameBootMorphRemaining && (size = recvxGameBootMorphRemaining(pointer))) return size;
    if (!freemem || end < first || p < first || p > end || (p & (alignment - 1u)))
        return 0;
    size = end - p;
    source = recvxGameBootSourceRemaining(pointer);
    return source && source < size ? source : size;
}
static void blending(RecvxGameChunkMaterial *material, unsigned int flags)
{
    static const int source[8] = {11, 10, 3, 5, 8, 6, 2, 4};
    static const int destination[8] = {11, 10, 9, 7, 8, 6, 2, 4};
    material->blend_source = source[(flags >> 3) & 7];
    material->blend_destination = destination[flags & 7];
}

void recvxGameBootChunkReset(void)
{
    chunk_fault = NULL;join_skipped_vertices=0;
    current_material = (RecvxGameChunkMaterial){
        .diffuse = 0xffffffffu, .ambient = 0xffffffffu, .specular = 0x11ffffffu,
        .texture = UINT_MAX, .sampler = 4, .blend_source = 8, .blend_destination = 6};
}
const char *recvxGameBootChunkFault(void) { return chunk_fault; }

/* Scalar replacement for njplus.c's PS2 strip-UV assembly. Validate the entire
   stream first so a malformed later command cannot leave an earlier one edited. */
void npSetOffsetUV(NJS_CNK_MODEL *model, short offu, short offv)
{
    if(remaining(model,4)<sizeof(*model))
        recvxGameBootUnsupported("invalid UV scroll model");
    if(!model->plist)return;
    for(int apply=0;apply<2;++apply){
        unsigned char *p=(unsigned char *)model->plist;
        size_t left=remaining(p,2);
        unsigned int commands=0;
        for(;;){
            if(++commands>CHUNK_COMMAND_LIMIT||left<2)
                recvxGameBootUnsupported("unterminated UV scroll stream");
            unsigned int type=p[0];
            if(type==255)break;
            size_t size=2;
            if(type==0){p+=2;left-=2;continue;}
            if(left<4)recvxGameBootUnsupported("truncated UV scroll command");
            if((type>=1&&type<=3)||type==8)size=4;
            else if((type>=17&&type<=23)||(type>=56&&type<=58)||(type>=64&&type<=66))
                size=4u+u16(p+2)*2u;
            else recvxGameBootUnsupported("unsupported UV scroll command");
            if(size>left)recvxGameBootUnsupported("UV scroll command exceeds stream");
            if(type==65||type==66){
                unsigned char *v=p+4,*end=p+size;
                if(end-v<2)recvxGameBootUnsupported("truncated UV scroll strips");
                unsigned int strips=u16(v)&0x3fffu,attributes=u16(v)>>14;v+=2;
                while(strips--){
                    if(end-v<2)recvxGameBootUnsupported("truncated UV scroll strip length");
                    unsigned int encoded=u16(v),count=encoded&0x8000u?65536u-encoded:encoded;
                    v+=2;
                    if(count<3)recvxGameBootUnsupported("invalid UV scroll strip length");
                    for(unsigned int i=0;i<count;++i){
                        size_t bytes=6u+(i>=2?attributes*2u:0u);
                        if(bytes>(size_t)(end-v))recvxGameBootUnsupported("truncated UV scroll vertex");
                        if(apply){
                            uint16_t u=(uint16_t)(u16(v+2)+offu),w=(uint16_t)(u16(v+4)+offv);
                            v[2]=u;v[3]=u>>8;v[4]=w;v[5]=w>>8;
                        }
                        v+=bytes;
                    }
                }
                if(end-v>2 || (end-v==2&&u16(v)))
                    recvxGameBootUnsupported("invalid UV scroll strip padding");
            }
            p+=size;left-=size;
        }
    }
}

typedef struct VertexSource {
    const unsigned char *data;
    unsigned int first, count, stride, normal;
} VertexSource;

static int vertices(const NJS_CNK_MODEL *model, VertexSource *source)
{
    const unsigned char *p = (const unsigned char *)model->vlist;
    size_t size = remaining(p, 4), header, bytes;
    unsigned int type;
    if (size < 8) return fail("invalid chunk vertex header", -1);
    type = p[0];
    if (type == 51) { header = 64; source->stride = 32; source->normal = 16; }
    else if (type == 41) { header = 8; source->stride = 24; source->normal = 12; }
    else return fail("unported chunk vertex command", -2);
    source->first = u16(p + 4); source->count = u16(p + 6);
    bytes = header + source->count * source->stride;
    if (!source->count || source->first + source->count > 65536u || bytes > size ||
        (size_t)u16(p + 2) * 4u + 4u != bytes)
        return fail("invalid chunk vertex extent", -1);
    source->data = p + header;
    for (unsigned int i = 0; i < source->count; ++i) {
        const unsigned char *v = source->data + i * source->stride;
        for (unsigned int a = 0; a < 3; ++a)
            if (!finite_word(v + a * 4) || !finite_word(v + source->normal + a * 4))
                return fail("invalid chunk vertex or normal", -1);
    }
    return 0;
}

static int vertex(const VertexSource *source, unsigned int index, float u, float v,
                  RecvxGameChunkVertex *out)
{
    const unsigned char *p;
    if (index < source->first || index - source->first >= source->count)
        return fail("chunk strip vertex index exceeds its vertex chunk", -1);
    p = source->data + (index - source->first) * source->stride;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    /* Coordinates are validated float bits, not arithmetic. Integer copies
       avoid six FPU load/store interlocks for every strip occurrence. */
    typedef uint32_t ChunkWord __attribute__((__may_alias__));
    const ChunkWord *position=(const ChunkWord *)p,*normal=(const ChunkWord *)(p+source->normal);
    ChunkWord *words=(ChunkWord *)out;
    words[0]=position[0];words[1]=position[1];words[2]=position[2];
    words[3]=normal[0];words[4]=normal[1];words[5]=normal[2];
    out->u=u;out->v=v;out->color=0xffffffffu;out->source_index=index;
#else
    *out = (RecvxGameChunkVertex){
        .x = f32(p), .y = f32(p + 4), .z = f32(p + 8),
        .nx = f32(p + source->normal), .ny = f32(p + source->normal + 4),
        .nz = f32(p + source->normal + 8), .u = u, .v = v, .color = 0xffffffffu, .source_index=index};
#endif
    return 0;
}

/* Join short authored strips before invoking the renderer. Repeated source
   vertices preserve every face/order while preventing bridge fragments. Keep
   the working set small enough for Allegrex's data cache. */
#define JOINED_STRIP_MAX 64u
static int joined_strips(const VertexSource *source,const unsigned char **cursor,const unsigned char *end,
    unsigned int *remaining_strips,unsigned int type,unsigned int attributes,unsigned int *triangle_count,
    RecvxGameChunkEmitStrip strip,void *context,const RecvxGameChunkMaterial *material)
{
    /* Decide whether a second strip fits before decoding the first. The
       original fallback otherwise immediately decoded the same strip again. */
    const unsigned char *probe=*cursor;
    if(end-probe<2)return fail("truncated chunk strip length",-1);
    unsigned int encoded=u16(probe),first=encoded&0x8000u?65536u-encoded:encoded;
    if(first<3)return fail("invalid chunk strip length",-1);
    unsigned int occupied=first+!!(encoded&0x8000u);
    if(occupied>JOINED_STRIP_MAX)return 2;
    if(*remaining_strips<2){join_skipped_vertices+=first;return 2;}
    size_t bytes=2u+first*(type==64?2u:6u)+(first-2)*attributes*2u;
    if(bytes>(size_t)(end-probe))return fail("truncated chunk strip vertex",-1);
    probe+=bytes;
    if(end-probe<2)return fail("truncated chunk strip length",-1);
    encoded=u16(probe);unsigned int second=encoded&0x8000u?65536u-encoded:encoded;
    if(second<3)return fail("invalid chunk strip length",-1);
    if(second+!!(encoded&0x8000u)+2+(occupied&1u)>JOINED_STRIP_MAX-occupied){
        join_skipped_vertices+=first;return 2;
    }
    RecvxGameChunkVertex decoded[JOINED_STRIP_MAX];
    const unsigned char *scan=*cursor;
    unsigned int count=0,groups=0,triangles=0;
    while(groups<*remaining_strips){
        if(end-scan<2)return fail("truncated chunk strip length",-1);
        unsigned int encoded=u16(scan),flip=!!(encoded&0x8000u);
        unsigned int length=flip?65536u-encoded:encoded;
        if(length<3)return fail("invalid chunk strip length",-1);
        unsigned int bridge=count?2+(count&1u):0;
        if(length+flip+bridge>JOINED_STRIP_MAX-count)break;
        scan+=2;
        unsigned int base=count+bridge+flip;
        for(unsigned int i=0;i<length;++i){
            size_t bytes=(type==64?2u:6u)+(i>=2?attributes*2u:0u);
            if(bytes>(size_t)(end-scan))return fail("truncated chunk strip vertex",-1);
            float u=0,v=0;
            if(type!=64){float scale=type==65?0.003921569f:0.0009775171f;u=u16(scan+2)*scale;v=u16(scan+4)*scale;}
            int result=vertex(source,u16(scan),u,v,&decoded[base+i]);if(result)return result;
            if(i>=2&&decoded[base+i-2].source_index!=decoded[base+i-1].source_index&&
               decoded[base+i].source_index!=decoded[base+i-2].source_index&&decoded[base+i].source_index!=decoded[base+i-1].source_index)++triangles;
            scan+=bytes;
        }
        if(count){
            RecvxGameChunkVertex last=decoded[count-1];
            unsigned int odd=count&1u;
            decoded[count++]=last;if(odd)decoded[count++]=last;
            decoded[count++]=decoded[base];
        }
        if(flip)decoded[count++]=decoded[base];
        count+=length;++groups;
    }
    if(groups<2)return 2; /* Retain the original single-strip path. */
    if(triangles>CHUNK_TRIANGLE_LIMIT-*triangle_count)return fail("chunk triangle budget exceeded",-1);
    int handled=strip(context,material,decoded,count,0);
    if(handled<0)return fail("chunk strip sink rejected geometry",-1);
    if(!handled)return 0;
    *cursor=scan;*remaining_strips-=groups;*triangle_count+=triangles;return 1;
}

static int parse_strips(const NJS_CNK_MODEL *model, RecvxGameChunkEmit emit, RecvxGameChunkEmitStrip strip, void *context,int join)

{
    VertexSource source;
    RecvxGameChunkMaterial material = current_material;
    const unsigned char *p;
    size_t left;
    unsigned int commands = 0, triangles = 0;
    int result;
    chunk_fault = NULL;
    if (remaining(model, 4) < sizeof(*model)) return fail("invalid relocated chunk model", -1);
    if (!model->plist) return 0;
    if (!model->vlist) return fail("unported shared chunk vertex cache", -2);
    if ((result = vertices(model, &source))) return result;
    p = (const unsigned char *)model->plist; left = remaining(p, 2);
    while (++commands <= CHUNK_COMMAND_LIMIT) {
        unsigned int type, flags, words;
        size_t size;
        if (left < 2) return fail("unterminated chunk polygon stream", -1);
        type = p[0]; flags = p[1];
        if (type == 255) {
            if (emit) current_material = material;
            return 0;
        }
        if (type == 0) { p += 2; left -= 2; continue; }
        if (left < 4) return fail("truncated chunk polygon command", -1);
        if (type >= 1 && type <= 3) {
            /* This PS2 implementation advances CHUNK_HEAD (four bytes),
               including for the generic Ninja bit commands. */
            if (type == 1) blending(&material, flags);
            else if (type == 3) material.specular = (material.specular & 0xffffffu) | (flags << 24);
            p += 4; left -= 4; continue;
        }
        if (type == 8) {
            unsigned int texture = u16(p + 2);
            material.texture = texture & 0xfffu;
            material.texture_bits = texture; material.texture_flags = flags;
            unsigned int filter = material.sampler & 4u;
            material.sampler = ((flags & 0x20u) ? 1u : 0u) | ((flags & 0x10u) ? 2u : 0u) |
                               ((flags & 0x80u) ? 8u : 0u) | ((flags & 0x40u) ? 16u : 0u);
            if (!(texture & 0x2000u)) {
                if (texture & 0xc000u) material.sampler |= 4u;
            } else material.sampler |= filter;
            p += 4; left -= 4; continue;
        }
        if (!((type >= 17 && type <= 23) || (type >= 64 && type <= 66)))
            return fail("unported chunk polygon command", -2);
        words = u16(p + 2); size = 4u + words * 2u;
        if (size > left) return fail("chunk polygon payload exceeds its stream", -1);
        if (type < 64) {
            unsigned int mask = type - 16;
            unsigned int count = (mask & 1u) + ((mask >> 1) & 1u) + ((mask >> 2) & 1u);
            const unsigned char *data = p + 4;
            if (words != count * 2u) return fail("invalid chunk material size", -1);
            blending(&material, flags);
            if (mask & 1u) { material.diffuse = u32(data); data += 4; }
            if (mask & 2u) { material.ambient = u32(data); data += 4; }
            if (mask & 4u) material.specular = u32(data);
        } else {
            const unsigned char *cursor = p + 4, *end = p + size;
            unsigned int strips, attributes;
            RecvxGameChunkMaterial batch = material;
            if (end - cursor < 2) return fail("truncated chunk strip header", -1);
            strips = u16(cursor); cursor += 2;
            attributes = strips >> 14; strips &= 0x3fffu;
            batch.flags = flags;
            if (type == 64) batch.texture = UINT_MAX;
            else if (batch.texture == UINT_MAX) return fail("textured chunk strip has no texture selection", -1);
            int joining=join&&strip;
            while (strips) {
                if(joining&&strips>1){
                    int grouped=joined_strips(&source,&cursor,end,&strips,type,attributes,&triangles,strip,context,&batch);
                    if(grouped<0)return grouped;
                    if(grouped==1)continue;
                    if(grouped==0)joining=0; /* Bound work when light masks differ. */
                }
                --strips;
                unsigned int encoded, length, previous_indices[2] = {0, 0};
                RecvxGameChunkVertex previous[2] = {0};
                if (end - cursor < 2) return fail("truncated chunk strip length", -1);
                encoded = u16(cursor); cursor += 2;
                length = encoded & 0x8000u ? 65536u - encoded : encoded;
                if (length < 3) return fail("invalid chunk strip length", -1);
                if(strip&&length<=RECVX_GAME_CHUNK_STRIP_MAX){
                    RecvxGameChunkVertex decoded[RECVX_GAME_CHUNK_STRIP_MAX];
                    const unsigned char *scan=cursor;unsigned int valid_triangles=0;
                    for(unsigned int i=0;i<length;++i){
                        size_t bytes=(type==64?2u:6u)+(i>=2?attributes*2u:0u);
                        if(bytes>(size_t)(end-scan))return fail("truncated chunk strip vertex",-1);
                        float u=0,v=0;
                        if(type!=64){float scale=type==65?0.003921569f:0.0009775171f;u=u16(scan+2)*scale;v=u16(scan+4)*scale;}
                        if((result=vertex(&source,u16(scan),u,v,&decoded[i])))return result;
                        if(i>=2&&decoded[i-2].source_index!=decoded[i-1].source_index&&
                           decoded[i].source_index!=decoded[i-2].source_index&&decoded[i].source_index!=decoded[i-1].source_index)++valid_triangles;
                        scan+=bytes;
                    }
                    if(valid_triangles>CHUNK_TRIANGLE_LIMIT-triangles)return fail("chunk triangle budget exceeded",-1);
                    int handled=strip(context,&batch,decoded,length,!!(encoded&0x8000u));
                    if(handled<0)return fail("chunk strip sink rejected geometry",-1);
                    if(!handled){
                        /* The sink declined, but decoded vertices and the
                           complete strip have already been validated. */
                        for(unsigned int i=2;i<length;++i){
                            unsigned int a=i-2,b=i-1,d=i;
                            if(decoded[a].source_index==decoded[b].source_index||decoded[a].source_index==decoded[d].source_index||decoded[b].source_index==decoded[d].source_index)continue;
                            if(((i-2u)&1u)^!!(encoded&0x8000u)){unsigned int swap=a;a=b;b=swap;}
                            RecvxGameChunkVertex triangle[3]={decoded[a],decoded[b],decoded[d]};
                            if(emit&&emit(context,&batch,triangle))return fail("chunk triangle sink rejected geometry",-1);
                        }
                    }
                    triangles+=valid_triangles;cursor=scan;continue;
                }
                for (unsigned int i = 0; i < length; ++i) {
                    RecvxGameChunkVertex value;
                    unsigned int index;
                    float u = 0, v = 0;
                    size_t bytes = (type == 64 ? 2u : 6u) + (i >= 2 ? attributes * 2u : 0u);
                    if (bytes > (size_t)(end - cursor)) return fail("truncated chunk strip vertex", -1);
                    index = u16(cursor);
                    if (type != 64) {
                        /* Constants and unsigned interpretation from retail Uvn/Uvh. */
                        float scale = type == 65 ? 0.003921569f : 0.0009775171f;
                        u = u16(cursor + 2) * scale;
                        v = u16(cursor + 4) * scale;
                    }
                    if ((result = vertex(&source, index, u, v, &value))) return result;
                    if (i >= 2 && previous_indices[0] != previous_indices[1] &&
                        index != previous_indices[0] && index != previous_indices[1]) {
                        RecvxGameChunkVertex triangle[3] = {previous[0], previous[1], value};
                        if (((i - 2u) & 1u) ^ !!(encoded & 0x8000u)) {
                            triangle[0] = previous[1]; triangle[1] = previous[0];
                        }
                        if (++triangles > CHUNK_TRIANGLE_LIMIT) return fail("chunk triangle budget exceeded", -1);
                        if (emit && emit(context, &batch, triangle)) return fail("chunk triangle sink rejected geometry", -1);
                    }
                    previous[0] = previous[1]; previous[1] = value;
                    previous_indices[0] = previous_indices[1]; previous_indices[1] = index;
                    cursor += bytes;
                }
            }
            if (end - cursor > 2 || (end - cursor == 2 && u16(cursor)))
                return fail("invalid chunk strip payload padding", -1);
        }
        p += size; left -= size;
    }
    return fail("chunk polygon command budget exceeded", -1);
}

int recvxGameBootChunkParseStrips(const NJS_CNK_MODEL *model,RecvxGameChunkEmit emit,RecvxGameChunkEmitStrip strip,void *context)
{return parse_strips(model,emit,strip,context,0);}
int recvxGameBootChunkParseJoinedStrips(const NJS_CNK_MODEL *model,RecvxGameChunkEmit emit,RecvxGameChunkEmitStrip strip,void *context)
{return parse_strips(model,emit,strip,context,1);}

int recvxGameBootChunkParse(const NJS_CNK_MODEL *model,RecvxGameChunkEmit emit,void *context)
{return recvxGameBootChunkParseStrips(model,emit,NULL,context);}
