#include <math.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_file_view.h"
#include "psp/game_boot_item_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/fileview.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/itemview.h"
#include "ps2/veronica/prog/njplus.h"

static int arena(const void *pointer,size_t bytes)
{
    uintptr_t first=(uintptr_t)freemem,p=(uintptr_t)pointer,last=sys?(uintptr_t)sys->endp:0;
    return pointer && first && last>=first && p>=first && p<=last && !(p&3u) && bytes<=last-p;
}
void recvxGameBootFileViewPrepare(const char *function)
{
    FV_WORK *file=&fvwork;
    if (!sys || !function || file->mode_00<0 || file->mode_00>4 || file->mode_01<0 || file->mode_01>2 ||
        file->mode_02<0 || file->mode_02>9 || file->mode_03<0 || file->mode_03>1 ||
        file->filenum<0 || file->filenum>=24 || file->filecsr<0 || file->filecsr>=3 ||
        file->tag<0 || file->tag>=8 || file->j<0 || file->j>=16 || file->z<0 || file->z>3 ||
        file->roll<0 || file->roll>1 || file->scrol>4 || file->page>13 ||
        file->pagewait00<0 || file->pagewait01<0 || file->move<0 || file->move>1280)
        recvxGameBootUnsupported("invalid original file-view state");
    if (!strcmp(function,"SelectFile") || (!strcmp(function,"SelectTag") && file->z==2)) {
        recvxGameBootItemViewTreePrepare(sitem.mdl.objP);
        NJS_CNK_OBJECT *child=sitem.mdl.objP->child;
        if (!child || !child->sibling || !child->sibling->sibling)
            recvxGameBootUnsupported("invalid original file-selection model");
    }
    if ((!strcmp(function,"FileExit01") || (!strcmp(function,"ReadFstx") && file->mode_02==8)) &&
        (!arena(swork.sspb,0) || (uintptr_t)swork.sspb>(uintptr_t)sys->memp))
        recvxGameBootUnsupported("invalid original file-view retained arena");
    if (!strcmp(function,"FileScrollSet") || !strcmp(function,"PageScroll") || !strcmp(function,"ReadFstx"))
        for (unsigned int i=0;i<5;++i)
            if (!isfinite(parts_22b[i].pos[0]))
                recvxGameBootUnsupported("invalid original file-page position");
}

/* The original polygon API takes a vertex count. Its convex fill becomes a
   fan of n-2 triangles; the 3D adapter owns transform, clipping and blending. */
void njDrawPolygon3D(NJS_POINT3COL *points,Int count,Uint32 attributes)
{
    if (count<3 || count>256 || (attributes&15u))
        recvxGameBootUnsupported("invalid original 3D polygon count/mode");
    njDrawTriangle3D(points,count-2,attributes|1u);
}

typedef struct SkinRecord {
    unsigned int flags,slot,count;
    const unsigned int *weights;
    NJS_CNK_OBJECT *object;
} SkinRecord;
static void collect(NJS_CNK_OBJECT *object,NJS_CNK_OBJECT **objects,unsigned int *count,unsigned int depth)
{
    if (depth>60) recvxGameBootUnsupported("reverse skin hierarchy exceeds depth");
    for (;object;object=object->sibling) {
        if (!arena(object,sizeof(*object)) || *count>=128)
            recvxGameBootUnsupported("invalid reverse skin object hierarchy");
        for (unsigned int i=0;i<*count;++i)
            if (objects[i]==object) recvxGameBootUnsupported("cyclic reverse skin hierarchy");
        objects[(*count)++]=object;
        for (unsigned int axis=0;axis<3;++axis)
            if (!isfinite(object->pos[axis])) recvxGameBootUnsupported("invalid reverse skin translation");
        collect(object->child,objects,count,depth+1);
    }
}
static unsigned int take(const unsigned int **cursor)
{
    if (!arena(*cursor,4)) recvxGameBootUnsupported("reverse skin table exceeds arena");
    return *(*cursor)++;
}
void recvxGameBootReverseSkinPrepare(NJS_CNK_OBJECT *object,int *skin)
{
    NJS_CNK_OBJECT *objects[128];unsigned int count=0,active=0;
    SkinRecord records[128]={0};SkinRecord *donors[128]={0};
    const unsigned int *cursor=(unsigned int *)skin;
    if (!sys || !object || !arena(skin,4)) recvxGameBootUnsupported("invalid reverse skin source");
    collect(object,objects,&count,0);
    for (unsigned int i=0;i<count;++i) {
        if (objects[i]->evalflags&0x20000000u) continue;
        SkinRecord *record=&records[active++];record->object=objects[i];record->flags=take(&cursor);
        if (record->flags&~3u) recvxGameBootUnsupported("invalid reverse skin flags");
        if (!record->flags) continue;
        record->slot=take(&cursor);
        if (record->slot>=128) recvxGameBootUnsupported("invalid reverse skin donor index");
        if (record->flags&2u) {
            record->count=take(&cursor);record->weights=cursor;
            if (record->count>65535 || !arena(cursor,record->count*4u) || donors[record->slot])
                recvxGameBootUnsupported("invalid reverse skin donor weights");
            cursor+=record->count;donors[record->slot]=record;
            NJS_CNK_MODEL *model=record->object->model;
            if (!arena(model,sizeof(*model)) || !arena(model->vlist,sizeof(HDR_PS)))
                recvxGameBootUnsupported("invalid reverse skin donor model");
            HDR_PS *header=(HDR_PS *)model->vlist;
            if (header->ucType!=51 || header->usIndexMax!=record->count ||
                !arena(header,sizeof(*header)+(size_t)record->count*32u))
                recvxGameBootUnsupported("invalid reverse skin donor vertices");
        }
    }
    for (unsigned int i=0;i<active;++i) {
        const SkinRecord *record=&records[i];
        if (!(record->flags&1u)) continue;
        const SkinRecord *donor=donors[record->slot];
        if (!donor) recvxGameBootUnsupported("undefined reverse skin donor");
        for (unsigned int j=0;j<donor->count;++j) {
            if (donor->weights[j]>>24>=active)
                recvxGameBootUnsupported("reverse skin weight exceeds joint count");
            const NJS_POINT4 *point=(const NJS_POINT4 *)((const unsigned char *)donor->object->model->vlist+64u+j*32u);
            if (!isfinite(point->x)||!isfinite(point->y)||!isfinite(point->z))
                recvxGameBootUnsupported("non-finite reverse skin vertex");
        }
    }
}
