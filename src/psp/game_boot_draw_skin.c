#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"

extern size_t recvxGameBootSkinRemaining(const void *) __attribute__((weak));
extern size_t recvxGameBootMorphRemaining(const void *) __attribute__((weak));
static int vertex_owned(const void *pointer)
{
    if ((uintptr_t)pointer & 3u) return 0;
    return (recvxGameBootSkinRemaining && recvxGameBootSkinRemaining(pointer) >= 4) ||
        (recvxGameBootMorphRemaining && recvxGameBootMorphRemaining(pointer) >= 4);
}

typedef struct StreamSave {
    NJS_CNK_OBJECT *objects;
    int count;
    unsigned int depth;
    NJS_CNK_MODEL *model[128];
    unsigned char eligible[128];
} StreamSave;

static StreamSave saves[2];
static unsigned int depth;

void recvxGameBootDrawSkinReset(void)
{
    memset(saves, 0, sizeof(saves)); depth = 0;
}

static int owned(const void *pointer, size_t bytes, unsigned int alignment)
{
    if (!sys) return 0;
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return pointer && freemem && end >= first && !(p & (alignment - 1u)) &&
        p >= first && p <= end && bytes <= end - p;
}

void recvxGameBootDrawSkinPrepare(NJS_CNK_OBJECT *objects, int count, int variant, int restoring)
{
    StreamSave *save;
    if ((unsigned int)variant >= 2 || count < 0 || count > 128 ||
        (count && !owned(objects, (size_t)count * sizeof(*objects), 4)))
        recvxGameBootUnsupported("invalid original model stream save table");
    save = &saves[variant];
    if (restoring) {
        if (!save->depth || save->depth != depth || save->objects != objects || save->count != count)
            recvxGameBootUnsupported("unmatched original model stream restore");
    } else if (save->depth)
        recvxGameBootUnsupported("nested original model stream save");
    for (int i = 0; i < count; ++i) {
        NJS_CNK_OBJECT *object = &objects[i];
        int eligible = object->model && (!variant || !(object->evalflags & 8u));
        if (object->model && !owned(object->model, sizeof(*object->model), 4))
            recvxGameBootUnsupported("invalid original model stream record");
        if (restoring) {
            if (save->model[i] != object->model || save->eligible[i] != eligible)
                recvxGameBootUnsupported("original model stream save identity changed");
        } else if (eligible) {
            if ((object->model->vlist && !owned(object->model->vlist, 4, 4) && !vertex_owned(object->model->vlist)) ||
                (variant && object->model->plist && !owned(object->model->plist, 2, 2)))
                recvxGameBootUnsupported("original model stream source exceeds arena");
            if (variant && (!isfinite(object->pos[0]) || !isfinite(object->pos[1]) || !isfinite(object->pos[2])))
                recvxGameBootUnsupported("invalid original saved model position");
        }
    }
    /* Validate the complete table before changing any snapshot metadata. */
    if (!restoring) {
        save->objects = objects; save->count = count; save->depth = ++depth;
        for (int i = 0; i < count; ++i) {
            save->model[i] = objects[i].model;
            save->eligible[i] = objects[i].model && (!variant || !(objects[i].evalflags & 8u));
        }
    }
}

void recvxGameBootDrawSkinFinish(int variant)
{
    if ((unsigned int)variant >= 2 || saves[variant].depth != depth || !depth)
        recvxGameBootUnsupported("invalid original model stream restore completion");
    saves[variant].depth = 0; --depth;
}
