#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_morph.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"

typedef struct VertexStream { HDR_PS header; uint32_t words[4][8]; uint32_t end; } VertexStream;
static struct {
    NJS_CNK_OBJECT source[128], destination[128];
    NJS_CNK_MODEL source_model[128], destination_model[128];
    VertexStream source_vertex[128], destination_vertex[128];
    uint32_t polygons[128][3];
} data;
static SYS_WORK work;
SYS_WORK *sys = &work;
unsigned char *freemem;
NP_WORK np;
static jmp_buf fault;
static int expecting_fault;
static const char *fault_name;
static const void *limited_pointer;
static size_t limited_size;

size_t recvxGameBootSourceRemaining(const void *pointer)
{ return pointer == limited_pointer ? limited_size : 0; }
void recvxGameBootUnsupported(const char *name)
{
    if (!expecting_fault) { fprintf(stderr, "unexpected morph fault: %s\n", name); abort(); }
    fault_name = name; longjmp(fault, 1);
}
#define REJECT(call, message) do { \
    fault_name = NULL; expecting_fault = 1; \
    if (!setjmp(fault)) { call; assert(!"expected morph fault"); } \
    expecting_fault = 0; assert(fault_name && strstr(fault_name, message)); \
} while (0)

static float value(uint32_t bits) { float result; memcpy(&result, &bits, 4); return result; }
static void put(uint32_t *word, float x) { memcpy(word, &x, 4); }
static void stream(VertexStream *s, int count, int destination)
{
    memset(s, destination ? 0x5a : 0xa5, sizeof(*s));
    s->header.ucType = 51; s->header.ucAttr = 0x81;
    s->header.usSize = (64 + 32 * count) / 4 - 1;
    s->header.usIndexOfs = 7; s->header.usIndexMax = count;
    for (int i = 0; i < count; ++i) {
        for (unsigned int j = 0; j < 8; ++j) put(&s->words[i][j], (float)(i * 10 + j + (destination ? 20 : 0)));
        s->words[i][3] = destination ? 0x7fc12345 : 0xdeadbeef;
        s->words[i][7] = destination ? 0xffa76543 : 0x12345678;
    }
}
static void reset(void)
{
    recvxGameBootMorphReset(); memset(&data, 0, sizeof(data)); memset(&np, 0, sizeof(np));
    memset(&work, 0, sizeof(work)); freemem = (unsigned char *)&data;
    sys = &work; sys->endp = freemem + sizeof(data); limited_pointer = NULL;
    for (unsigned int i = 0; i < 128; ++i) {
        data.source[i].model = &data.source_model[i];
        data.destination[i].model = &data.destination_model[i];
        data.source_model[i].vlist = (int *)&data.source_vertex[i];
        data.destination_model[i].vlist = (int *)&data.destination_vertex[i];
        data.source_model[i].plist = (short *)data.polygons[i];
        data.polygons[i][1] = 0x11223344u + i;
        for (unsigned int j = 0; j < 3; ++j) {
            data.source[i].pos[j] = (float)j;
            data.destination[i].pos[j] = (float)j + 100;
        }
        stream(&data.source_vertex[i], 2, 0); stream(&data.destination_vertex[i], 2, 1);
    }
}

static void interpolation(void)
{
    reset();
    /* Equal positions skip the normal morph, even when normals differ. */
    memcpy(data.destination_vertex[0].words[1], data.source_vertex[0].words[1], 12);
    npCalcMorphing(data.source, data.destination, 250, 1);
    assert(data.source[0].pos[0] == 25 && data.source[0].pos[2] == 27);
    assert(np.vlp2[0] == data.source_model[0].vlist && !((uintptr_t)np.vlp2[0] & 63));
    assert(recvxGameBootMorphRemaining(np.vlp2[0]) == 64 + 64 + 4);
    const VertexStream *out = (const VertexStream *)np.vlp2[0];
    assert(!memcmp(&out->header, &data.source_vertex[0].header, sizeof(HDR_PS)));
    for (unsigned int j = 0; j < 3; ++j) {
        assert(value(out->words[0][j]) == j + 5);
        assert(value(out->words[0][j + 4]) == j + 9);
    }
    assert(out->words[0][3] == 0x7fc12345 && out->words[0][7] == 0xffa76543);
    assert(!memcmp(out->words[1], data.source_vertex[0].words[1], 32));
    assert(*(const uint32_t *)((const unsigned char *)out + 128) == 255);
    assert(recvxGameBootMorphRemaining((const unsigned char *)out + 128) == 4);
    assert(!recvxGameBootMorphRemaining((const unsigned char *)out + 132));
    assert(!recvxGameBootMorphRemaining((const unsigned char *)out - 1));
    assert(!recvxGameBootMorphRemaining(NULL));
    /* Original factor is per thousand and extrapolation is permitted. */
    reset(); npTransform(data.source, data.destination, 1500, 3);
    assert(fabsf(value(((VertexStream *)np.vlp2[3])->words[0][0]) - 30) < 0.00001f);
    assert(data.source[0].pos[0] == 0);
    npTransform(data.source, data.destination, 500, 3); /* In-place owned output. */
    assert(fabsf(value(((VertexStream *)np.vlp2[3])->words[0][0]) - 25) < 0.00001f);
    reset(); npTransform(data.source, data.destination, 0, 0);
    out = (const VertexStream *)np.vlp2[0];
    assert(value(out->words[0][0]) == 0 && out->words[0][3] == 0x7fc12345);
}

static void capacity_and_selection(void)
{
    reset(); data.source[1].evalflags = 8; data.source[2].model = NULL;
    npCalcMorphing(data.source, data.destination, 500, 128);
    assert(!np.vlp2[1] && !np.vlp2[2] && np.vlp2[127]);
    assert(data.source[1].pos[0] == 0 && data.source[127].pos[0] == 50);
    const void *old = np.vlp2[0]; recvxGameBootMorphReset();
    assert(!recvxGameBootMorphRemaining(old) && !np.vlp2[0]);
    reset(); npTransform(data.source, data.destination, 500, 0);
    stream(&data.source_vertex[0], 4, 0); stream(&data.destination_vertex[0], 4, 1);
    data.source_model[0].vlist = (int *)&data.source_vertex[0];
    npTransform(data.source, data.destination, 500, 0);
    assert(recvxGameBootMorphRemaining(np.vlp2[0]) == 64 + 128 + 4);
    assert(value(((VertexStream *)np.vlp2[0])->words[3][2]) == 42);
    reset(); stream(&data.destination_vertex[0], 3, 1);
    npCalcMorphing(data.source, data.destination, 500, 1);
    assert(data.source[0].pos[0] == 50 && !np.vlp2[0]);
    assert(data.source_model[0].vlist == (int *)&data.source_vertex[0]);
    reset(); data.source[0].evalflags = 8; data.source[1].model = NULL;
    assert(npGetMatColor(data.source, 128) == data.polygons[2][1]);
    assert(npGetMatColor(NULL, 0) == 0);
    npCalcMorphing(NULL, NULL, 0, 0);
}

static void malformed(void)
{
    reset(); REJECT(npCalcMorphing(data.source, data.destination, 0, 129), "object table");
    REJECT(npCalcMorphing(data.source, data.destination, 0, -1), "object table");
    REJECT(npTransform((NJS_CNK_OBJECT *)1, data.destination, 0, 0), "object table");
    REJECT(npTransform(data.source, data.destination, 0, 128), "parameters");
    REJECT(npTransform(data.source, data.destination, NAN, 0), "parameters");
    REJECT(npCalcMorphing(data.source, data.destination, INFINITY, 1), "factor");
    data.source_model[1].vlist = (int *)1;
    REJECT(npCalcMorphing(data.source, data.destination, 500, 2), "vertex header");
    assert(!np.vlp2[0] && data.source[0].pos[0] == 0);
    reset(); data.destination[0].model = NULL;
    REJECT(npTransform(data.source, data.destination, 0, 0), "morph model");
    reset(); data.source_vertex[0].header.ucType = 41;
    REJECT(npTransform(data.source, data.destination, 0, 0), "vertex format");
    reset(); data.source_vertex[0].header.usSize--;
    REJECT(npTransform(data.source, data.destination, 0, 0), "vertex extent");
    reset(); limited_pointer = &data.source_vertex[0]; limited_size = 64;
    REJECT(npTransform(data.source, data.destination, 0, 0), "vertex extent");
    reset(); put(&data.source_vertex[0].words[0][0], NAN);
    REJECT(npTransform(data.source, data.destination, 0, 0), "vertex coordinate");
    reset(); data.destination[0].pos[2] = NAN;
    REJECT(npCalcMorphing(data.source, data.destination, 500, 1), "object position");
    reset(); data.source_model[0].plist = (short *)1;
    REJECT(npGetMatColor(data.source, 1), "color record");
    reset(); sys = NULL;
    REJECT(npCalcMorphing(data.source, data.destination, 0, 1), "object table"); sys = &work;
}

int main(void)
{
    interpolation(); capacity_and_selection(); malformed(); recvxGameBootMorphReset();
    puts("PASS: original morph XYZ/normal arithmetic, packed W selection, equal-position branch, per-thousand factors, 128 nodes, owned scratch growth/reset, material lookup and malformed spans");
    return 0;
}
