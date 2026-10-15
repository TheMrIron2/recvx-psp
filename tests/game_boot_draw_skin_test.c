#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"

static SYS_WORK work;
static struct {
    NJS_CNK_OBJECT objects[128], other[2];
    NJS_CNK_MODEL models[128], replacement;
    int vertices[128][8], modified_vertices[128][8];
    short polygons[128][8], modified_polygons[128][8];
} data;
SYS_WORK *sys = &work;
unsigned char *freemem;
NP_WORK np;
void recvxGameBootDrawSkinReset(void);
static jmp_buf fault;
static const char *fault_name;
static int expecting_fault;

void recvxGameBootUnsupported(const char *name)
{
    if (!expecting_fault) { fprintf(stderr, "unexpected stream save fault: %s\n", name); abort(); }
    fault_name = name; longjmp(fault, 1);
}
#define REJECT(call, message) do { \
    fault_name = NULL; expecting_fault = 1; \
    if (!setjmp(fault)) { call; assert(!"expected stream save fault"); } \
    expecting_fault = 0; assert(fault_name && strstr(fault_name, message)); \
} while (0)

static void reset(void)
{
    memset(&work, 0, sizeof(work)); memset(&data, 0, sizeof(data)); memset(&np, 0, sizeof(np));
    freemem = (unsigned char *)&data; sys->endp = freemem + sizeof(data);
    recvxGameBootDrawSkinReset();
    for (unsigned int i = 0; i < 128; ++i) {
        data.objects[i].model = &data.models[i];
        data.objects[i].pos[0] = i; data.objects[i].pos[1] = i + 1; data.objects[i].pos[2] = i + 2;
        data.models[i].vlist = data.vertices[i]; data.models[i].plist = data.polygons[i];
        data.vertices[i][0] = i; data.modified_vertices[i][0] = 1000 + i;
    }
}

static void normal_restore(void)
{
    reset(); data.objects[1].model = NULL; data.objects[2].evalflags = 8;
    npPushMdlstr(data.objects, 128);
    assert(np.vlist[0] == data.vertices[0] && np.vlist[1] == NULL && np.vlist[2] == data.vertices[2]);
    for (unsigned int i = 0; i < 128; ++i) {
        data.models[i].vlist = data.modified_vertices[i]; data.models[i].plist = data.modified_polygons[i];
        data.objects[i].pos[0] = 1000;
    }
    npPopMdlstr(data.objects, 128);
    assert(data.models[0].vlist == data.vertices[0] && data.models[2].vlist == data.vertices[2]);
    assert(data.models[1].vlist == data.modified_vertices[1]);
    assert(data.models[0].plist == data.modified_polygons[0] && data.objects[0].pos[0] == 1000);
    assert(data.vertices[0][0] == 0 && data.modified_vertices[0][0] == 1000); /* Pointer snapshots do not alter vertices. */
    npPushMdlstr(NULL, 0); npPopMdlstr(NULL, 0);
}

static void morph_and_skin_nesting(void)
{
    reset(); data.objects[1].evalflags = 8;
    npPushMdlstr2(data.objects, 3);
    assert(np.mdlstr2[0].vlist == data.vertices[0] && np.mdlstr2[1].vlist == NULL);
    data.objects[0].pos[0] = 20; data.objects[0].pos[1] = 30; data.objects[0].pos[2] = 40;
    data.models[0].vlist = data.modified_vertices[0]; data.models[0].plist = data.modified_polygons[0];
    data.objects[1].pos[0] = 50; data.models[1].vlist = data.modified_vertices[1];
    npPushMdlstr(data.objects, 3);
    data.models[0].vlist = data.modified_vertices[2];
    npPopMdlstr(data.objects, 3);
    assert(data.models[0].vlist == data.modified_vertices[0]);
    npPopMdlstr2(data.objects, 3);
    assert(data.models[0].vlist == data.vertices[0] && data.models[0].plist == data.polygons[0]);
    assert(data.objects[0].pos[0] == 0 && data.objects[0].pos[1] == 1 && data.objects[0].pos[2] == 2);
    assert(data.objects[1].pos[0] == 50 && data.models[1].vlist == data.modified_vertices[1]);
    for (unsigned int i = 0; i < 200; ++i) {
        npPushMdlstr(data.objects, 3); data.models[0].vlist = data.modified_vertices[0];
        npPopMdlstr(data.objects, 3); assert(data.models[0].vlist == data.vertices[0]);
    }
}

static void invalid_state(void)
{
    reset(); sys = NULL; REJECT(npPushMdlstr(data.objects, 1), "save table"); sys = &work;
    reset(); REJECT(npPushMdlstr(data.objects, 129), "save table");
    REJECT(npPushMdlstr(data.objects, -1), "save table");
    REJECT(npPushMdlstr((NJS_CNK_OBJECT *)1, 1), "save table");
    REJECT(npPopMdlstr(data.objects, 1), "unmatched");
    data.objects[1].model = (NJS_CNK_MODEL *)1;
    REJECT(npPushMdlstr(data.objects, 2), "stream record");
    assert(np.vlist[0] == NULL); /* Reject before writing a partial snapshot. */
    data.objects[1].model = &data.models[1];
    npPushMdlstr(data.objects, 2);
    REJECT(npPushMdlstr(data.objects, 2), "nested");
    REJECT(npPopMdlstr(data.objects, 1), "unmatched");
    REJECT(npPopMdlstr(data.other, 2), "unmatched");
    data.objects[0].model = &data.replacement;
    REJECT(npPopMdlstr(data.objects, 2), "identity changed");
    data.objects[0].model = &data.models[0]; npPopMdlstr(data.objects, 2);
    reset(); npPushMdlstr2(data.objects, 2); npPushMdlstr(data.objects, 2);
    REJECT(npPopMdlstr2(data.objects, 2), "unmatched");
    npPopMdlstr(data.objects, 2); npPopMdlstr2(data.objects, 2);
    reset(); npPushMdlstr2(data.objects, 2); data.objects[0].evalflags = 8;
    REJECT(npPopMdlstr2(data.objects, 2), "identity changed");
    reset(); data.models[0].vlist = (int *)1;
    REJECT(npPushMdlstr(data.objects, 1), "source exceeds arena");
    reset(); data.objects[0].pos[0] = NAN;
    REJECT(npPushMdlstr2(data.objects, 1), "saved model position");
    reset(); npPushMdlstr(data.objects, 1); recvxGameBootDrawSkinReset();
    REJECT(npPopMdlstr(data.objects, 1), "unmatched");
}

int main(void)
{
    normal_restore(); morph_and_skin_nesting(); invalid_state();
    puts("PASS: original model stream pointer/position restore, hidden/null models, 128-node capacity, nested morph/skin ownership, repeated draws and malformed save/restore transactions");
    return 0;
}
