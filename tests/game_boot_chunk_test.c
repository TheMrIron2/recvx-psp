#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot_chunk.h"
#include "ps2/veronica/prog/main.h"

SYS_WORK *sys;
unsigned char *freemem;
static SYS_WORK work;
static struct {
    NJS_CNK_MODEL model;
    unsigned char vertices[512];
    unsigned char polygons[4096];
} arena;
static RecvxGameChunkVertex triangles[64][3];
static RecvxGameChunkMaterial materials[64];
static unsigned int emitted, checks;
static const void *source_pointer;
static size_t source_size;

size_t recvxGameBootSourceRemaining(const void *pointer)
{ return pointer == source_pointer ? source_size : 0; }
static void check(int condition) { ++checks; assert(condition); }
static void put16(unsigned char *p, unsigned int value)
{ p[0] = value; p[1] = value >> 8; }
static void put32(unsigned char *p, unsigned int value)
{ p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24; }
static void putfloat(unsigned char *p, float value)
{ unsigned int bits; memcpy(&bits, &value, 4); put32(p, bits); }
static int close_to(float a, float b) { return fabsf(a - b) < 0.00001f; }
static int emit(void *context, const RecvxGameChunkMaterial *material, const RecvxGameChunkVertex vertices[3])
{
    check(context == &work); check(emitted < 64);
    memcpy(triangles[emitted], vertices, sizeof(triangles[0])); materials[emitted++] = *material;
    return 0;
}
static int reject(void *context, const RecvxGameChunkMaterial *m, const RecvxGameChunkVertex v[3])
{ (void)context; (void)m; (void)v; return -1; }
static size_t strip(unsigned char *p, unsigned int type, unsigned int flags, int length,
                    unsigned int first, unsigned int attributes)
{
    unsigned char *cursor = p + 4;
    unsigned int count = length < 0 ? (unsigned int)-length : (unsigned int)length;
    p[0] = type; p[1] = flags;
    put16(cursor, 1 | (attributes << 14)); cursor += 2;
    put16(cursor, length); cursor += 2;
    for (unsigned int i = 0; i < count; ++i) {
        put16(cursor, first + i); cursor += 2;
        if (type != 64) {
            put16(cursor, i * 85); put16(cursor + 2, 255 - i * 85); cursor += 4;
        }
        if (i >= 2) for (unsigned int a = 0; a < attributes; ++a) {
            put16(cursor, 0xab00 + a); cursor += 2;
        }
    }
    put16(p + 2, (cursor - p - 4) / 2);
    return cursor - p;
}
static void reset(unsigned int type)
{
    unsigned int header = type == 51 ? 64 : 8, stride = type == 51 ? 32 : 24;
    memset(&arena, 0, sizeof(arena)); memset(&work, 0, sizeof(work));
    sys = &work; freemem = (unsigned char *)&arena;
    sys->endp = freemem + sizeof(arena);
    arena.model.vlist = (Sint32 *)arena.vertices; arena.model.plist = (Sint16 *)arena.polygons;
    arena.vertices[0] = type; put16(arena.vertices + 2, (header + stride * 4 - 4) / 4);
    put16(arena.vertices + 6, 4);
    for (unsigned int i = 0; i < 4; ++i) {
        unsigned char *p = arena.vertices + header + i * stride;
        putfloat(p, (float)i); putfloat(p + 4, (float)(i & 1));
        putfloat(p + (type == 51 ? 20 : 16), 1);
    }
    size_t n = strip(arena.polygons, 64, 0, 4, 0, 0);
    put16(arena.polygons + n, 255);
    emitted = 0; source_pointer = NULL; source_size = 0;
    recvxGameBootChunkReset();
}
static int parse(void) { return recvxGameBootChunkParse(&arena.model, emit, &work); }
static void failed(int code, const char *message)
{ check(parse() == code); check(recvxGameBootChunkFault() && strstr(recvxGameBootChunkFault(), message)); }

static unsigned int joined_calls,decline_joined;
static int joined_emit(void *context,const RecvxGameChunkMaterial *m,const RecvxGameChunkVertex *v,unsigned int count,unsigned int reversed)
{
    ++joined_calls;if(decline_joined)return 0;
    for(unsigned int i=2;i<count;++i){
        if(v[i-2].source_index==v[i-1].source_index||v[i].source_index==v[i-2].source_index||v[i].source_index==v[i-1].source_index)continue;
        unsigned int a=i-2,b=i-1;
        if(((i-2)&1u)^reversed){unsigned int swap=a;a=b;b=swap;}
        RecvxGameChunkVertex face[3]={v[a],v[b],v[i]};emit(context,m,face);
    }
    return 1;
}
static void joined_tests(void)
{
    for(unsigned int type=64;type<=66;++type)for(unsigned int signs=0;signs<8;++signs){
        reset(51);arena.polygons[0]=8;put16(arena.polygons+2,0);
        unsigned char *p=arena.polygons+4,*cursor=p+6;
        p[0]=type;p[1]=0x18;put16(p+4,12|(2u<<14));
        for(unsigned int strip_index=0;strip_index<12;++strip_index){
            unsigned int count=3+strip_index%3;
            put16(cursor,signs&(1u<<(strip_index%3))?65536u-count:count);cursor+=2;
            for(unsigned int i=0;i<count;++i){
                put16(cursor,i%4);cursor+=2;
                if(type!=64){put16(cursor,strip_index*43+i*7);put16(cursor+2,strip_index*17+i*11);cursor+=4;}
                if(i>=2){put16(cursor,0xabcd);put16(cursor+2,0x1234);cursor+=4;}
            }
        }
        put16(p+2,(cursor-p-4)/2);put16(cursor,255);
        check(parse()==0);unsigned int expected_count=emitted;
        RecvxGameChunkVertex expected[64][3];RecvxGameChunkMaterial expected_materials[64];
        memcpy(expected,triangles,sizeof(expected));memcpy(expected_materials,materials,sizeof(expected_materials));
        emitted=joined_calls=decline_joined=0;recvxGameBootChunkReset();
        check(recvxGameBootChunkParseJoinedStrips(&arena.model,emit,joined_emit,&work)==0);
        check(joined_calls<12&&emitted==expected_count);
        check(!memcmp(expected,triangles,emitted*sizeof(*triangles)));
        check(!memcmp(expected_materials,materials,emitted*sizeof(*materials)));
        emitted=joined_calls=0;decline_joined=1;recvxGameBootChunkReset();
        check(recvxGameBootChunkParseJoinedStrips(&arena.model,emit,joined_emit,&work)==0);
        check(emitted==expected_count&&!memcmp(expected,triangles,emitted*sizeof(*triangles)));
        check(!memcmp(expected_materials,materials,emitted*sizeof(*materials)));
        put16(p+8,4);emitted=0;
        check(recvxGameBootChunkParseJoinedStrips(&arena.model,emit,joined_emit,&work)==-1);
        check(strstr(recvxGameBootChunkFault(),"vertex index")!=NULL);
    }
}

static void join_capacity_tests(void)
{
    const unsigned int counts[][2]={{31,32},{32,31},{62,3},{30,31}};
    for(unsigned int type=64;type<=66;++type)for(unsigned int signs=0;signs<4;++signs)
    for(unsigned int test=0;test<sizeof(counts)/sizeof(counts[0]);++test){
        reset(51);arena.polygons[0]=8;put16(arena.polygons+2,0);
        unsigned char *p=arena.polygons+4,*cursor=p+6;p[0]=type;p[1]=0x18;put16(p+4,2|(2u<<14));
        for(unsigned int s=0;s<2;++s){
            unsigned int count=counts[test][s];put16(cursor,signs&(1u<<s)?65536u-count:count);cursor+=2;
            for(unsigned int i=0;i<count;++i){
                put16(cursor,i%4);cursor+=2;
                if(type!=64){put16(cursor,i*971);put16(cursor+2,i*719);cursor+=4;}
                if(i>=2){put16(cursor,0xabcd);put16(cursor+2,0x1234);cursor+=4;}
            }
        }
        put16(p+2,(cursor-p-4)/2);put16(cursor,255);
        check(parse()==0);unsigned int expected_count=emitted;
        RecvxGameChunkVertex expected[64][3];memcpy(expected,triangles,sizeof(expected));
        for(unsigned int decline=0;decline<2;++decline){
            emitted=joined_calls=0;decline_joined=decline;recvxGameBootChunkReset();
            check(recvxGameBootChunkParseJoinedStrips(&arena.model,emit,joined_emit,&work)==0);
            check(emitted==expected_count&&!memcmp(expected,triangles,emitted*sizeof(*triangles)));
            if(test<3)check(recvxGameBootChunkJoinSkippedVertices()==counts[test][0]);
        }
    }
}
static void finite_word_tests(void)
{
    const unsigned int bad[]={0x7f800000u,0xff800000u,0x7fc12345u,0x7f800001u};
    const unsigned int good[]={0u,0x80000000u,1u,0x807fffffu,0x7f7fffffu,0xff7fffffu};
    for(unsigned int format=0;format<2;++format)for(unsigned int field=0;field<6;++field){
        unsigned int type=format?41:51,header=format?8:64,normal=format?12:16;
        unsigned int offset=header+(field<3?field*4:normal+(field-3)*4);
        for(unsigned int i=0;i<sizeof(bad)/sizeof(bad[0]);++i){reset(type);put32(arena.vertices+offset,bad[i]);failed(-1,"vertex or normal");}
        for(unsigned int i=0;i<sizeof(good)/sizeof(good[0]);++i){reset(type);put32(arena.vertices+offset,good[i]);check(parse()==0);}
    }
}

int main(void)
{
    size_t n;
    joined_tests();join_capacity_tests();finite_word_tests();
    reset(51); check(parse() == 0); check(emitted == 2);
    check(triangles[0][0].x == 0 && triangles[0][1].x == 1 && triangles[0][2].x == 2);
    check(triangles[1][0].x == 2 && triangles[1][1].x == 1 && triangles[1][2].x == 3);
    check(triangles[0][0].ny == 1 && materials[0].texture == UINT_MAX);
    check(materials[0].diffuse == 0xffffffffu && materials[0].specular == 0x11ffffffu);

    reset(41); check(parse() == 0); check(emitted == 2 && triangles[0][2].ny == 1);
    reset(51); n = strip(arena.polygons, 64, 0x18, -4, 0, 0); put16(arena.polygons + n, 255);
    check(parse() == 0); check(triangles[0][0].x == 1 && triangles[0][1].x == 0);
    check(triangles[1][0].x == 1 && triangles[1][1].x == 2 && materials[0].flags == 0x18);

    reset(51); put16(arena.vertices + 4, 100);
    n = strip(arena.polygons, 64, 0, 4, 100, 3); put16(arena.polygons + n, 255);
    check(parse() == 0 && emitted == 2); check(triangles[0][0].x == 0);

    reset(51);
    arena.polygons[0] = 23; arena.polygons[1] = 0x25; put16(arena.polygons + 2, 6);
    put32(arena.polygons + 4, 0x80aabbccu); put32(arena.polygons + 8, 0xff112233u);
    put32(arena.polygons + 12, 0x08776655u);
    arena.polygons[16] = 8; arena.polygons[17] = 0xb0; put16(arena.polygons + 18, 0x4007);
    n = strip(arena.polygons + 20, 65, 8, 4, 0, 1); put16(arena.polygons + 20 + n, 255);
    check(parse() == 0 && emitted == 2);
    check(materials[0].diffuse == 0x80aabbccu && materials[0].ambient == 0xff112233u &&
          materials[0].specular == 0x08776655u);
    check(materials[0].texture == 7 && materials[0].sampler == 15 &&
          materials[0].texture_bits == 0x4007 && materials[0].texture_flags == 0xb0);
    check(materials[0].blend_source == 8 && materials[0].blend_destination == 6);
    check(close_to(triangles[0][0].v, 1) && close_to(triangles[0][2].u, 170 * 0.003921569f));
    /* Streams inherit Ninja state across models; a dry validation leaves it unchanged. */
    emitted = 0; n = strip(arena.polygons, 65, 0, 3, 0, 0); put16(arena.polygons + n, 255);
    check(parse() == 0 && materials[0].texture == 7 && materials[0].diffuse == 0x80aabbccu);

    reset(51); arena.polygons[0] = 8; put16(arena.polygons + 2, 0xc002);
    n = strip(arena.polygons + 4, 66, 0, 3, 0, 0); put16(arena.polygons + 4 + n, 255);
    put16(arena.polygons + 4 + 8 + 2, 1023); put16(arena.polygons + 4 + 8 + 4, 65535);
    check(parse() == 0 && close_to(triangles[0][0].u, 1));
    check(close_to(triangles[0][0].v, 65535 * 0.0009775171f));

    reset(51); arena.polygons[0] = 1; arena.polygons[1] = (2 << 3) | 3;
    arena.polygons[4] = 3; arena.polygons[5] = 7;
    n = strip(arena.polygons + 8, 64, 0, 3, 0, 0); put16(arena.polygons + 8 + n, 255);
    check(parse() == 0 && materials[0].blend_source == 3 && materials[0].blend_destination == 7);
    check((materials[0].specular >> 24) == 7);

    reset(51); arena.polygons[0] = 8; put16(arena.polygons + 2, 0x4001);
    arena.polygons[4] = 8; put16(arena.polygons + 6, 0x2002);
    n = strip(arena.polygons + 8, 65, 0, 3, 0, 0); put16(arena.polygons + 8 + n, 255);
    check(parse() == 0 && (materials[0].sampler & 4) && materials[0].texture == 2);

    reset(51); check(recvxGameBootChunkParse(&arena.model, NULL, NULL) == 0 && emitted == 0);
    check(recvxGameBootChunkParse(&arena.model, reject, NULL) == -1);
    check(strstr(recvxGameBootChunkFault(), "sink") != NULL);
    reset(51); put16(arena.polygons + 8, 4); failed(-1, "vertex index"); check(emitted == 0);
    reset(51); arena.vertices[0] = 42; failed(-2, "vertex command");
    reset(51); put16(arena.vertices + 2, 1); failed(-1, "vertex extent");
    reset(51); putfloat(arena.vertices + 64, NAN); failed(-1, "vertex or normal");
    reset(51); putfloat(arena.vertices + 80, INFINITY); failed(-1, "vertex or normal");
    reset(51); arena.polygons[0] = 4; failed(-2, "polygon command");
    reset(51); arena.polygons[0] = 17; put16(arena.polygons + 2, 4); failed(-1, "material size");
    reset(51); arena.polygons[0] = 65; failed(-1, "no texture selection");
    reset(51); put16(arena.polygons + 6, 2); failed(-1, "strip length");
    reset(51); source_pointer = arena.polygons; source_size = 6; failed(-1, "payload exceeds");
    reset(51); source_pointer = arena.polygons; source_size = 2; failed(-1, "polygon command");
    reset(51); memset(arena.polygons, 0, sizeof(arena.polygons)); failed(-1, "unterminated");
    reset(51); arena.model.vlist = (Sint32 *)((unsigned char *)&arena + sizeof(arena) - 4);
    failed(-1, "vertex header");
    reset(51); arena.model.vlist = NULL; failed(-2, "shared chunk vertex cache");
    reset(51); arena.model.plist = NULL; check(parse() == 0 && emitted == 0);
    reset(51); check(recvxGameBootChunkParse((NJS_CNK_MODEL *)((unsigned char *)&arena - 4), emit, &work) == -1);
    printf("relocated Ninja chunks: %u checks passed\n", checks);
    return 0;
}
