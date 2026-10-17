#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_skin.h"
#include "psp/game_boot_vector.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"

/* npPlusInit's original scratch starts at the end of the loaded game arena.
   Keep its matrices and generated streams in separately bounded storage. */
extern NP_WORK np;
extern size_t recvxGameBootMorphRemaining(const void *) __attribute__((weak));
static float matrices[128][2][16] __attribute__((aligned(64)));
static unsigned char *allocation, *storage;
static size_t capacity, used;
static struct { size_t offset, bytes; } ranges[128];
static unsigned int range_count;

static void checked_math(void)
{
    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());
}

typedef struct SkinEntry {
    unsigned int flags, slot, count;
    const unsigned int *weights;
    const unsigned char *source;
    size_t offset;
} SkinEntry;

size_t recvxGameBootSkinRemaining(const void *pointer)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)storage;
    if (!storage || p < first || p - first >= used) return 0;
    for (unsigned int i = 0; i < range_count; ++i)
        if (p - first >= ranges[i].offset && p - first - ranges[i].offset < ranges[i].bytes)
            return ranges[i].bytes - (p - first - ranges[i].offset);
    return 0;
}

void recvxGameBootSkinReset(void)
{
    free(allocation); allocation = storage = NULL; capacity = used = range_count = 0;
    memset(matrices, 0, sizeof(matrices));
    memset(np.vlp, 0, sizeof(np.vlp));
    for (unsigned int i = 0; i < 128; ++i)
        np.mxp[i][0] = np.mxp[i][1] = NULL;
    np.buff = np.bp = NULL; np.sknp = NULL;
}

static size_t source_remaining(const void *pointer)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem;
    uintptr_t end = sys ? (uintptr_t)sys->endp : 0;
    size_t result, recorded;
    if (!pointer || (p & 3u)) return 0;
    if (recvxGameBootMorphRemaining && (result = recvxGameBootMorphRemaining(pointer))) return result;
    if (!freemem || end < first || p < first || p > end) return 0;
    result = end - p; recorded = recvxGameBootSourceRemaining(pointer);
    return recorded && recorded < result ? recorded : result;
}

static int owned(const void *pointer, size_t bytes)
{ return pointer && source_remaining(pointer) >= bytes; }

static unsigned int take(const unsigned int **cursor)
{
    if (!owned(*cursor, 4)) recvxGameBootUnsupported("original skin table exceeds source");
    return *(*cursor)++;
}

static int member(const void *pointer, const void *base, size_t count, size_t stride)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)base;
    return pointer && base && p >= first && p-first < count*stride && !((p-first)%stride);
}

static O_WRK *actor(void *pointer)
{
    O_WRK *work = pointer;
    if (!sys || !work || (work != (O_WRK *)plp && !member(work, ene, 128, sizeof(*ene)) &&
        !member(work, eff, 512, sizeof(*eff)) &&
        !(member(work, sys->obwp, 39936u/sizeof(*work), sizeof(*work)) && owned(work, sizeof(*work))) &&
        !(member(work, sys->itwp, 39936u/sizeof(*work), sizeof(*work)) && owned(work, sizeof(*work)))))
        recvxGameBootUnsupported("invalid original skin actor");
    if ((!member(work->mlwP, work->mdl, 16, sizeof(ML_WORK)) &&
         !(member(work, eff, 512, sizeof(*eff)) && member(work->mlwP, sys->efm, 450, sizeof(ML_WORK)))) ||
        !isfinite(work->px) || !isfinite(work->py) || !isfinite(work->pz))
        recvxGameBootUnsupported("invalid original skin model work");
    return work;
}

static size_t prepare(O_WRK *work, int count, int *table, int fm, SkinEntry entries[128])
{
    const unsigned int *cursor = (const unsigned int *)table;
    unsigned char slots[128] = {0};
    size_t total = 0;
    ML_WORK *model = work->mlwP;
    if (count < 0 || count > 128 || (unsigned int)count > model->obj_num ||
        (count && (!owned(model->objP, (size_t)count*sizeof(*model->objP)) ||
                   !owned(model->owP, (size_t)count*sizeof(*model->owP)))))
        recvxGameBootUnsupported("invalid original skin joint array");
    memset(entries, 0, 128*sizeof(*entries));
    for (int i = 0; i < count; ++i) {
        SkinEntry *entry = &entries[i];
        if (fm && i >= 11 && i <= 14) continue;
        entry->flags = take(&cursor);
        if (!entry->flags) continue;
        for (unsigned int a = 0; a < 16; ++a)
            if (!isfinite(model->owP[i].mtx[a])) recvxGameBootUnsupported("invalid original skin joint matrix");
        if (!(entry->flags & 2u)) { (void)take(&cursor); continue; }
        entry->slot = take(&cursor); entry->count = take(&cursor);
        if (entry->slot >= 128 || slots[entry->slot]++ || entry->count > 65535u ||
            !owned(cursor, (size_t)entry->count * 4u))
            recvxGameBootUnsupported("invalid original skin output or weight extent");
        entry->weights = cursor; cursor += entry->count;
        NJS_CNK_MODEL *mesh = model->objP[i].model;
        if (!owned(mesh, sizeof(*mesh)) || !owned(mesh->vlist, sizeof(HDR_PS)))
            recvxGameBootUnsupported("invalid original skin source model");
        const HDR_PS *header = (const HDR_PS *)mesh->vlist;
        size_t bytes = sizeof(*header) + (size_t)entry->count * 32u;
        if (header->ucType != 51) recvxGameBootUnsupported("unported original skin vertex command");
        if (header->usIndexMax != entry->count ||
            (unsigned int)header->usIndexOfs + entry->count > 65536u ||
            (size_t)header->usSize * 4u + 4u != bytes || !owned(header, bytes))
            recvxGameBootUnsupported("invalid original skin vertex extent");
        entry->source = (const unsigned char *)header;
        for (unsigned int j = 0; j < entry->count; ++j) {
            unsigned int packed = entry->weights[j];
            unsigned int bone = packed >> 24;
            if (fm && bone > 10) bone += 4;
            if (((packed >> 16) & 255u) >= 10u || bone >= (unsigned int)count)
                recvxGameBootUnsupported("invalid original skin weight or bone");
            const float *vertex = (const float *)(entry->source + sizeof(*header) + j*32u);
            for (unsigned int a = 0; a < 3; ++a)
                if (!isfinite(vertex[a]) || !isfinite(vertex[4+a]))
                    recvxGameBootUnsupported("invalid original skin vertex or normal");
        }
        entry->offset = total;
        total += (bytes + 4u + 63u) & ~(size_t)63u;
        if (total > 8u*1024u*1024u) recvxGameBootUnsupported("original skin scratch capacity exceeded");
    }
    /* Referenced bones must have been prepared by this very table, not left
       over from an earlier actor. FM's omitted face bones are never selected. */
    for (int i = 0; i < count; ++i)
        for (unsigned int j = 0; j < entries[i].count; ++j) {
            unsigned int bone = entries[i].weights[j] >> 24;
            if (fm && bone > 10) bone += 4;
            if (!entries[bone].flags) recvxGameBootUnsupported("original skin references an inactive bone");
        }
    np.sknp = (int *)cursor;
    return total;
}

static void initialize(O_WRK *work, int count, SkinEntry entries[128], size_t total)
{
    if (total > capacity) {
        unsigned char *replacement = malloc(total + 63u);
        if (!replacement) recvxGameBootUnsupported("original skin scratch allocation failed");
        free(allocation); allocation = replacement;
        storage = (unsigned char *)(((uintptr_t)replacement + 63u) & ~(uintptr_t)63u);
        capacity = total;
    }
    used = total;
    range_count = 0;
    np.buff = storage; np.bp = storage ? storage + total : NULL;
    memset(np.vlp, 0, sizeof(np.vlp));
    njUnitMatrix(NULL);
    checked_math();
    for (int i = 0; i < count; ++i) {
        np.mxp[i][0] = (NJS_MATRIX *)&matrices[i][0];
        np.mxp[i][1] = (NJS_MATRIX *)&matrices[i][1];
        if (!entries[i].flags) continue;
        memcpy(matrices[i][0], work->mlwP->owP[i].mtx, sizeof(matrices[i][0]));
        matrices[i][0][12] -= work->px;
        matrices[i][0][13] -= work->py;
        matrices[i][0][14] -= work->pz;
        memcpy(matrices[i][1], matrices[i][0], sizeof(matrices[i][1]));
        const int *angle = work->mlwP->objP[i].ang;
        njRotXYZ(np.mxp[i][0], (int)(0u-(unsigned int)angle[0]),
                 (int)(0u-(unsigned int)angle[1]), (int)(0u-(unsigned int)angle[2]));
        checked_math();
        for (unsigned int j = 0; j < 2; ++j)
            for (unsigned int a = 0; a < 16; ++a)
                if (!isfinite(matrices[i][j][a]))
                    recvxGameBootUnsupported("invalid original transformed skin matrix");
        if (entries[i].flags & 2u) {
            np.vlp[entries[i].slot] = (int *)(storage + entries[i].offset);
            ranges[range_count].offset = entries[i].offset;
            ranges[range_count++].bytes = sizeof(HDR_PS) + entries[i].count*32u + 4u;
        }
    }
}

static void calculate(void *pointer, int count, int *table, int fm, int transform)
{
    static const float level[10] = {0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1.0f};
    SkinEntry entries[128];
    if (!table && transform) return;
    O_WRK *work = actor(pointer);
    size_t total = prepare(work, count, table, fm, entries);
    if (transform && !njPushMatrixEx()) {
        checked_math();
        recvxGameBootUnsupported("original skin matrix push failed");
    }
    initialize(work, count, entries, total);
    if (!transform) return;
    recvxFaceVectorPrepare();
    for (int i = 0; i < count; ++i) {
        SkinEntry *entry = &entries[i];
        if (!(entry->flags & 2u)) continue;
        unsigned char *output = storage + entry->offset;
        size_t bytes = sizeof(HDR_PS) + entry->count*32u;
        /* Fourth words are packed metadata; preserve them without float loads. */
        memcpy(output, entry->source, bytes);
        for (unsigned int j = 0; j < entry->count; ++j) {
            unsigned int packed = entry->weights[j], bone = packed >> 24;
            if (fm && bone > 10) bone += 4;
            float weight = level[(packed >> 16) & 255u];
            const float *source = (const float *)(entry->source + sizeof(HDR_PS) + j*32u);
            float *destination = (float *)(output + sizeof(HDR_PS) + j*32u);
            for (unsigned int normal = 0; normal < 2; ++normal) {
                float first[3], second[3], delta[3];
                if (normal) recvxSkinDirection(matrices[bone][1], source + 4, second);
                else recvxFaceTransform(matrices[bone][1], source, second);
                if (weight != 1.0f) {
                    if (normal) recvxSkinDirection(matrices[bone][0], source + 4, first);
                    else recvxFaceTransform(matrices[bone][0], source, first);
                    for (unsigned int a = 0; a < 3; ++a) delta[a] = second[a] - first[a];
                    recvxFaceScaleAdd(first, delta, weight);
                    memcpy(destination + normal*4u, first, sizeof(first));
                } else memcpy(destination + normal*4u, second, sizeof(second));
            }
        }
        *(unsigned int *)(output + bytes) = 255u;
        work->mlwP->objP[i].model->vlist = (int *)output;
    }
    if (!njPopMatrixEx()) recvxGameBootUnsupported("original skin matrix pop failed");
    checked_math();
}

void npInitCalcSkin(void *work, int count, int *table) { calculate(work,count,table,0,0); }
void npCalcSkin(void *work, int count, int *table) { calculate(work,count,table,0,1); }
void npInitCalcSkinFM(void *work, int count, int *table) { calculate(work,count,table,1,0); }
void npCalcSkinFM(void *work, int count, int *table) { calculate(work,count,table,1,1); }
