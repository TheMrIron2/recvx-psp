#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/face.h"
#include "ps2/veronica/prog/main.h"

static uint32_t word(const unsigned char *p)
{ return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned int half(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static int span(size_t size, size_t offset, size_t count)
{ return offset <= size && count <= size - offset; }

int recvxGameBootValidateMaskSource(const unsigned char *data, size_t size,
                                  const uint32_t *vertices, unsigned int models)
{
    size_t cursor;
    unsigned int face, lists, connections, jaw, tang, frames, i;
    if (!data || !vertices || size < 64 || memcmp(data, "MASK", 4) || half(data + 4) != 1 ||
        !models || models > 128 || data[52] >= models) return -1;
    face = vertices[data[52]];
    if (!face || face >= 32768 || word(data + 12) != face ||
        half(data + 8) >= face || half(data + 10) >= face) return -1;
    for (i = 53; i < 64; ++i)
        if (data[i] != 255 && data[i] >= models) return -1;
    lists = word(data + 16); connections = word(data + 20);
    jaw = word(data + 24); tang = word(data + 28); frames = word(data + 32);
    if (lists > 4096 || connections > 4096 || jaw > 4096 || tang > 4096 || !frames || frames > 256 ||
        !span(size, 64, lists * 4u)) return -1;
    cursor = 64u + lists * 4u;
    for (i = 0; i < lists; ++i) {
        const unsigned char *list = data + 64 + i * 4u;
        unsigned int j;
        if (half(list) >= face || list[3] > 127 || !span(size, cursor, list[3] * 4u)) return -1;
        for (j = 0; j < list[3]; ++j)
            if (word(data + cursor + j * 4u) >= face) return -1;
        cursor += list[3] * 4u;
    }
    if (!span(size, cursor, connections * 20u + jaw * 8u + tang * 8u + frames * 164u)) return -1;
    return 0;
}

size_t recvxGameBootMaskPlan(BH_PWORK *actor)
{
    ML_WORK *model = actor->mlwP;
    NJS_CNK_OBJECT *parts[128];
    uint32_t vertices[128];
    const unsigned char *mask = actor->mskp;
    unsigned int i, count = 0, inserted = 0;
    size_t required = 1092u + sizeof(MASK_WORK) + sizeof(LIP_WORK) + 3u * 63u, size;
    if (model != actor->mdl || model->obj_num < (mask ? 15u : 11u) || model->obj_num > 128 ||
        recvxGameBootSourceRemaining(model->objP) < model->obj_num * sizeof(NJS_CNK_OBJECT))
        recvxGameBootUnsupported("invalid actor model for mask initialization");
    if (!mask) return required;
    if (recvxGameBootSourceRemaining(mask - 4) < 4)
        recvxGameBootUnsupported("actor mask has no bounded record");
    size = word(mask - 4);
    if (size > recvxGameBootSourceRemaining(mask))
        recvxGameBootUnsupported("actor mask exceeds its loaded record");
    for (i = 0; i < model->obj_num; ++i) {
        NJS_CNK_OBJECT *part = model->objP + i;
        if (!part->model) continue;
        if (recvxGameBootSourceRemaining(part->model) < sizeof(NJS_CNK_MODEL) ||
            recvxGameBootSourceRemaining(part->model->vlist) < 8)
            recvxGameBootUnsupported("actor mask vertex header exceeds model data");
        parts[count] = part;
        vertices[count++] = (unsigned int)part->model->vlist[1] >> 16;
    }
    if (recvxGameBootValidateMaskSource(mask, size, vertices, count))
        recvxGameBootUnsupported("invalid actor MASK tables or vertex references");
    /* The skin records used by these actors use PS2 vertex chunk type 51.
       Preserve its eight-word vertex stride in the original mask routines. */
    for (i = 52; i <= 53; ++i) {
        unsigned int index = mask[i];
        size_t bytes;
        if (index == 255) continue;
        if ((parts[index]->model->vlist[0] & 255) != 51)
            recvxGameBootUnsupported("unsupported actor mask vertex format");
        bytes = (vertices[index] * 8u + 128u) * 4u;
        if (bytes > recvxGameBootSourceRemaining(parts[index]->model->vlist))
            recvxGameBootUnsupported("actor mask vertex copy exceeds model data");
        required += sizeof(NJS_CNK_MODEL) + bytes + 2u * 63u;
    }
    for (i = 53; i < 64; ++i) {
        if (mask[i] == 255) continue;
        if (parts[mask[i]]->child)
            recvxGameBootUnsupported("actor mask attachment is not a leaf object");
        ++inserted;
    }
    required += (model->obj_num + inserted) * (sizeof(NJS_CNK_OBJECT) + sizeof(O_WORK)) + 2u * 63u;
    return required;
}

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(MASK_WORK) == 0x740, "original face context ABI changed");
_Static_assert(sizeof(LIP_WORK) == 16, "original lip context ABI changed");
#endif
