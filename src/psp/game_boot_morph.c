#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_morph.h"
#include "psp/game_boot_vector.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"

extern NP_WORK np;
extern size_t recvxGameBootSkinRemaining(const void *) __attribute__((weak));

typedef struct MorphStream {
    void *allocation;
    unsigned char *data;
    size_t capacity, used;
} MorphStream;

static MorphStream streams[128];
static size_t allocated;
#define MORPH_MEMORY_LIMIT (8u * 1024u * 1024u)

size_t recvxGameBootMorphRemaining(const void *pointer)
{
    uintptr_t p = (uintptr_t)pointer;
    if (!pointer) return 0;
    for (unsigned int i = 0; i < 128; ++i) {
        uintptr_t base = (uintptr_t)streams[i].data;
        if (streams[i].data && p >= base && p - base < streams[i].used)
            return streams[i].used - (p - base);
    }
    return 0;
}

void recvxGameBootMorphReset(void)
{
    for (unsigned int i = 0; i < 128; ++i) {
        if (np.vlp2[i] == (int *)streams[i].data) np.vlp2[i] = NULL;
        free(streams[i].allocation);
    }
    memset(streams, 0, sizeof(streams)); allocated = 0;
}

static size_t remaining(const void *pointer, unsigned int alignment)
{
    uintptr_t p = (uintptr_t)pointer;
    uintptr_t first = (uintptr_t)freemem, end = sys ? (uintptr_t)sys->endp : 0;
    size_t size, source;
    if (!pointer || (p & (alignment - 1u))) return 0;
    size = recvxGameBootMorphRemaining(pointer);
    if (!size && recvxGameBootSkinRemaining) size = recvxGameBootSkinRemaining(pointer);
    if (size) return size;
    if (!freemem || end < first || p < first || p > end) return 0;
    size = end - p; source = recvxGameBootSourceRemaining(pointer);
    return source && source < size ? source : size;
}

static void objects(NJS_CNK_OBJECT *object, int count)
{
    if (count < 0 || count > 128 ||
        (count && remaining(object, 4) < (size_t)count * sizeof(*object)))
        recvxGameBootUnsupported("invalid original morph object table");
}

static const HDR_PS *vertices(const NJS_CNK_OBJECT *object)
{
    const HDR_PS *header;
    size_t bytes;
    if (!object->model || remaining(object->model, 4) < sizeof(*object->model))
        recvxGameBootUnsupported("invalid original morph model");
    header = (const HDR_PS *)object->model->vlist;
    if (remaining(header, 4) < sizeof(*header))
        recvxGameBootUnsupported("invalid original morph vertex header");
    if (header->ucType != 51)
        recvxGameBootUnsupported("unported original morph vertex format");
    bytes = sizeof(*header) + (size_t)header->usIndexMax * 32u;
    if (!header->usIndexMax || header->usIndexOfs + (unsigned int)header->usIndexMax > 65536u ||
        bytes != (size_t)header->usSize * 4u + 4u || remaining(header, 4) < bytes)
        recvxGameBootUnsupported("invalid original morph vertex extent");
    const float *v = (const float *)(header + 1);
    for (unsigned int i = 0; i < header->usIndexMax; ++i, v += 8)
        for (unsigned int axis = 0; axis < 3; ++axis)
            if (!isfinite(v[axis]) || !isfinite(v[axis + 4]))
                recvxGameBootUnsupported("invalid original morph vertex coordinate");
    return header;
}

static void transform(NJS_CNK_OBJECT *srcobj, const HDR_PS *source,
                      const HDR_PS *destination, float factor, int slot)
{
    MorphStream *stream = &streams[slot];
    size_t bytes = sizeof(*source) + (size_t)source->usIndexMax * 32u + 4u;
    unsigned char *output = stream->data;
    void *allocation = stream->allocation;
    if (source->usIndexMax != destination->usIndexMax) return;
    if (bytes > stream->capacity) {
        if (bytes > MORPH_MEMORY_LIMIT - (allocated - stream->capacity))
            recvxGameBootUnsupported("original morph scratch capacity exceeded");
        allocation = malloc(bytes + 63u);
        if (!allocation) recvxGameBootUnsupported("original morph scratch allocation failed");
        output = (unsigned char *)(((uintptr_t)allocation + 63u) & ~(uintptr_t)63u);
    }
    /* XYZ instructions leave vf5/vf7 W untouched: changed vertices inherit the
       destination's packed words; equal positions copy both source vectors. */
    memmove(output, source, sizeof(*source));
    const float *src = (const float *)(source + 1);
    const float *dst = (const float *)(destination + 1);
    float *out = (float *)(output + sizeof(*source));
    for (unsigned int i = 0; i < source->usIndexMax; ++i, src += 8, dst += 8, out += 8) {
        float a[8], b[8], delta[3];
        memcpy(a, src, sizeof(a)); memcpy(b, dst, sizeof(b));
        if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
            memcpy(out, b, sizeof(b));
            for (unsigned int part = 0; part <= 4; part += 4) {
                for (unsigned int axis = 0; axis < 3; ++axis)
                    delta[axis] = b[part + axis] - a[part + axis];
                memcpy(out + part, a + part, 3 * sizeof(float));
                recvxFaceScaleAdd(out + part, delta, factor);
            }
        } else memcpy(out, a, sizeof(a));
    }
    /* Chunk termination occupies a full initialized word for checked readers. */
    const uint32_t terminator = 255;
    memcpy(output + bytes - 4, &terminator, sizeof(terminator));
    if (allocation != stream->allocation) {
        allocated = allocated - stream->capacity + bytes;
        free(stream->allocation); stream->capacity = bytes;
    }
    stream->allocation = allocation; stream->data = output; stream->used = bytes;
    np.vlp2[slot] = (int *)output; srcobj->model->vlist = np.vlp2[slot];
}

void npTransform(NJS_CNK_OBJECT *source, NJS_CNK_OBJECT *destination, float no, int slot)
{
    objects(source, 1); objects(destination, 1);
    if ((unsigned int)slot >= 128 || !isfinite(no))
        recvxGameBootUnsupported("invalid original morph transform parameters");
    const HDR_PS *a = vertices(source), *b = vertices(destination);
    recvxFaceVectorPrepare();
    transform(source, a, b, no * 0.001f, slot);
}

void npCalcMorphing(NJS_CNK_OBJECT *source, NJS_CNK_OBJECT *destination, float no, int count)
{
    const HDR_PS *a[128], *b[128];
    objects(source, count); objects(destination, count);
    if (!isfinite(no)) recvxGameBootUnsupported("invalid original morph factor");
    /* Check every input before modifying positions or swapping a model stream. */
    for (int i = 0; i < count; ++i) {
        a[i] = b[i] = NULL;
        if (!source[i].model || (source[i].evalflags & 8u)) continue;
        a[i] = vertices(&source[i]); b[i] = vertices(&destination[i]);
        for (unsigned int axis = 0; axis < 3; ++axis)
            if (!isfinite(source[i].pos[axis]) || !isfinite(destination[i].pos[axis]))
                recvxGameBootUnsupported("invalid original morph object position");
    }
    recvxFaceVectorPrepare();
    for (int i = 0; i < count; ++i) if (a[i]) {
        for (unsigned int axis = 0; axis < 3; ++axis)
            source[i].pos[axis] += no * (0.001f * (destination[i].pos[axis] - source[i].pos[axis]));
        transform(&source[i], a[i], b[i], no * 0.001f, i);
    }
}

unsigned int npGetMatColor(NJS_CNK_OBJECT *object, int count)
{
    unsigned int color;
    objects(object, count);
    for (int i = 0; i < count; ++i) {
        if (!object[i].model || (object[i].evalflags & 8u)) continue;
        if (remaining(object[i].model, 4) < sizeof(*object[i].model) ||
            remaining(object[i].model->plist, 2) < 8)
            recvxGameBootUnsupported("invalid original material color record");
        memcpy(&color, (const unsigned char *)object[i].model->plist + 4, sizeof(color));
        return color;
    }
    return 0;
}
