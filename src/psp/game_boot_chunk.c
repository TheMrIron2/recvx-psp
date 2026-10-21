#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_chunk.h"
#include "ps2/veronica/prog/main.h"

extern size_t recvxGameBootSkinRemaining(const void *) __attribute__((weak));
extern size_t recvxGameBootMorphRemaining(const void *) __attribute__((weak));

#define CHUNK_COMMAND_LIMIT 65536u
#define CHUNK_TRIANGLE_LIMIT 65536u

static const char *chunk_fault;
static RecvxGameChunkMaterial current_material = {
    .diffuse = 0xffffffffu, .ambient = 0xffffffffu, .specular = 0x11ffffffu,
    .texture = UINT_MAX, .sampler = 4, .blend_source = 8, .blend_destination = 6
};

static unsigned int u16(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static unsigned int u32(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }
static float f32(const unsigned char *p)
{
    uint32_t bits = u32(p);
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
    chunk_fault = NULL;
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
            if (!isfinite(f32(v + a * 4)) || !isfinite(f32(v + source->normal + a * 4)))
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
    *out = (RecvxGameChunkVertex){
        .x = f32(p), .y = f32(p + 4), .z = f32(p + 8),
        .nx = f32(p + source->normal), .ny = f32(p + source->normal + 4),
        .nz = f32(p + source->normal + 8), .u = u, .v = v, .color = 0xffffffffu, .source_index=index};
    return 0;
}

int recvxGameBootChunkParse(const NJS_CNK_MODEL *model, RecvxGameChunkEmit emit, void *context)
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
            while (strips--) {
                unsigned int encoded, length, previous_indices[2] = {0, 0};
                RecvxGameChunkVertex previous[2] = {0};
                if (end - cursor < 2) return fail("truncated chunk strip length", -1);
                encoded = u16(cursor); cursor += 2;
                length = encoded & 0x8000u ? 65536u - encoded : encoded;
                if (length < 3) return fail("invalid chunk strip length", -1);
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
