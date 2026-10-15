#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "psp/engine_original_lighting.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_NaView.h"

SYS_WORK *sys;
unsigned char *freemem;
CAM_WORK cam;
NJS_MATRIX *cmat;
static SYS_WORK state;
static struct {
    NJS_CNK_OBJECT objects[3];
    NJS_CNK_MODEL model;
    unsigned char vertices[160], polygons[64];
} data;
static NJS_MATRIX camera;
static jmp_buf failure;
static const char *fault;
static unsigned int checks;
static RecvxBootTexture texture;
static unsigned char pixels[128];
static NJS_TEXNAME names[1];
static NJS_TEXLIST textures = {names, 1};
void recvxGameBootUnsupported(const char *name) { fault = name; longjmp(failure, 1); }
size_t recvxGameBootSourceRemaining(const void *p) { (void)p; return 0; }
const RecvxBootTexture *recvxGameBootTextureGet(NJS_TEXLIST *list, unsigned int index)
{ assert(list == &textures && index == 0); return &texture; }
#define CHECK(condition) do { ++checks; assert(condition); } while (0)
#define REJECT(call, message) do { fault = NULL; \
    if (!setjmp(failure)) { call; assert(!"expected renderer boundary"); } \
    CHECK(fault && strstr(fault, message)); \
} while (0)
static void put16(unsigned char *p, unsigned int v) { p[0] = v; p[1] = v >> 8; }
static void putfloat(unsigned char *p, float v) { memcpy(p, &v, 4); }
static int close_to(float a, float b) { return fabsf(a-b) < 0.001f; }
static void reset(void)
{
    memset(&state, 0, sizeof(state)); memset(&data, 0, sizeof(data));
    sys = &state; freemem = (unsigned char *)&data; sys->endp = freemem + sizeof(data);
    recvxGameBootRenderReset(); recvxEngineOriginalFaultReset();
    recvxEngineOriginalMatrixReset(); recvxEngineOriginalViewReset();
    recvxEngineOriginalViewConfigureScreen(640, 480); njSetPerspective(0x4000); njClipZ(-1, -100);
    recvxEngineOriginalLightingReset(); njUnitMatrix(&camera); cam.mtx = &camera;
    njUnitMatrix(NULL);
    data.model.vlist = (Sint32 *)data.vertices; data.model.plist = (Sint16 *)data.polygons;
    data.model.center.z = 4; data.model.r = 2;
    data.vertices[0] = 51; put16(data.vertices+2, 39); put16(data.vertices+6, 3);
    static const float positions[3][3] = {{-1,-1,4},{0,1,4},{1,-1,4}};
    for (unsigned int i = 0; i < 3; ++i) {
        unsigned char *v = data.vertices + 64 + i*32;
        for (unsigned int c = 0; c < 3; ++c) putfloat(v+c*4, positions[i][c]);
        putfloat(v+24, -1);
    }
    data.polygons[0] = 64; data.polygons[1] = 3; /* Unlit, ignore specular. */
    put16(data.polygons+2, 5); put16(data.polygons+4, 1); put16(data.polygons+6, 3);
    put16(data.polygons+8, 0); put16(data.polygons+10, 1); put16(data.polygons+12, 2);
    put16(data.polygons+14, 255);
    for (unsigned int i = 0; i < 3; ++i) {
        data.objects[i].evalflags = 7; data.objects[i].model = &data.model;
    }
    texture = (RecvxBootTexture){.source=pixels,.pixels=pixels,.width=16,.height=8,.tile_width=16,.tile_count=1};
    fault = NULL;
}
static const RecvxBootRenderFrame *draw(void)
{ njCnkEasyMultiDrawModel(&data.model); return recvxGameBootRenderFrame(); }
int main(void)
{
    const RecvxBootRenderFrame *f;
    reset(); f = draw();
    CHECK(f->vertex_count == 3 && f->batch_count == 1 && recvxGameBootRenderPending());
    CHECK(f->vertices[0].x == -1 && f->vertices[0].y == 1 && f->vertices[0].z == -4);
    CHECK(f->vertices[0].color == 0xffffffffu && !f->batches[0].texture);
    CHECK(close_to(f->projection[0], 1.174f) && close_to(f->projection[5], 4.0f/3.0f));
    CHECK(f->projection[11] == -1 && f->projection[15] == 0);
    CHECK(!((uintptr_t)f->projection & 15));
    draw(); f = recvxGameBootRenderFrame(); CHECK(f->vertex_count == 6 && f->batch_count == 1);
    recvxGameBootRenderConsume(); CHECK(!recvxGameBootRenderPending());

    reset(); njTranslate(NULL, 10, 0, 0); f = draw(); CHECK(f->vertices[0].x == 9);
    reset(); njScale(NULL, 2, 3, 1); f = draw(); CHECK(f->vertices[0].x == -2 && f->vertices[0].y == 3);
    reset(); data.polygons[1] = 2; njCnkSetEasyMultiAmbient(0.25f, 0.5f, 1); f = draw();
    CHECK(f->vertices[0].color == 0xffff8040u); /* ABGR: blue1, green0.5, red0.25. */
    reset(); data.polygons[1] = 2; njCnkSetEasyMultiLight(1); njCnkSetEasyMultiLightSwitch(1, 1);
    njCnkSetEasyMultiLightColor(1, 1, 0, 0); njCnkSetEasyMultiLightVector(0,0,1); f = draw();
    CHECK(f->vertices[0].color == 0xff0000ffu);
    reset(); data.polygons[1] = 2; njCnkSetEasyMultiLight(2); njCnkSetEasyMultiLightSwitch(2, 1);
    njCnkSetEasyMultiLightColor(2, 0, 1, 0); njCnkSetEasyMultiLightPoint(2, 0, 0, 0);
    njCnkSetEasyMultiLightRange(2, 5, 20); f = draw(); CHECK((f->vertices[0].color & 0xff00u) > 60000u);
    reset(); NJS_ARGB constant = {0.5f, 1, 0, 0}; njControl3D(0x10); njSetConstantMaterial(&constant); f=draw();
    CHECK(f->vertices[0].color == 0x800000ffu);
    reset(); njControl3D(0x800); njSetConstantAttr(0, 0x1b00); f=draw();
    CHECK(f->batches[0].attributes == 0x1b);
    reset(); data.polygons[1] = 2;
    putfloat(data.vertices+64, 0); putfloat(data.vertices+68, 0); putfloat(data.vertices+72, 10);
    njCnkSetEasyMultiLight(2); njCnkSetEasyMultiLightSwitch(2, 1);
    njCnkSetEasyMultiLightColor(2, 1, 1, 1); njCnkSetEasyMultiLightPoint(2, 0, 0, 0);
    njCnkSetEasyMultiLightRange(2, 5, 50); f = draw(); CHECK(f->vertices[0].color == 0xff404040u);
    recvxGameBootRenderConsume(); njCnkSetEasyMultiLightRange(2, 0, 0); f = draw();
    CHECK(f->vertices[0].color == 0xff000000u);
    reset();
    for (unsigned int i = 0; i < 21846; ++i) njCnkEasyMultiDrawModel(&data.model);
    f = recvxGameBootRenderFrame();
    CHECK(f->vertex_count == 65538 && f->batch_count == 2 && f->batches[0].count == 65535 && f->batches[1].count == 3);
    reset(); njControl3D(0x100); njTranslate(NULL, 0, 0, -20); draw(); CHECK(!recvxGameBootRenderPending());
    reset(); njControl3D(0x100); njTranslate(NULL, 1000, 0, 0); draw(); CHECK(!recvxGameBootRenderPending());

    reset(); data.objects[0].evalflags = 6; data.objects[0].pos[0] = 3;
    data.objects[0].child = &data.objects[1]; data.objects[0].sibling = &data.objects[2];
    njCnkEasyMultiDrawObject(data.objects); f=recvxGameBootRenderFrame();
    CHECK(f->vertex_count == 9 && f->vertices[0].x == 2 && f->vertices[3].x == 2 && f->vertices[6].x == -1);
    NJS_MATRIX current; njGetMatrix(&current); CHECK(current[12] == 0 && !recvxEngineOriginalMatrixUnderflowCount());
    reset(); data.objects[0].evalflags = 7|8; data.objects[0].child = &data.objects[1];
    njCnkEasyDrawObject(data.objects); CHECK(recvxGameBootRenderFrame()->vertex_count == 3);
    reset(); data.objects[0].evalflags = 7|0x10; data.objects[0].child = &data.objects[1];
    njCnkEasyDrawObject(data.objects); CHECK(recvxGameBootRenderFrame()->vertex_count == 3);
    reset(); data.objects[0].evalflags = 7|8|0x100; data.objects[0].child = &data.objects[1];
    njCnkEasyDrawObject(data.objects); CHECK(!recvxGameBootRenderPending());
    reset(); data.objects[0].sibling = data.objects; REJECT(njCnkEasyDrawObject(data.objects), "cyclic");
    reset(); data.objects[0].evalflags = 6; data.objects[0].pos[0] = NAN;
    REJECT(njCnkEasyDrawObject(data.objects), "object transform");
    reset(); REJECT(njCnkEasyDrawObject((NJS_CNK_OBJECT *)1), "object tree");
    reset(); data.model.r = NAN; REJECT(draw(), "model bounds");
    reset(); data.polygons[1] = 0; REJECT(draw(), "specular");
    reset(); data.polygons[1] = 0x43; REJECT(draw(), "environment-mapped");
    reset(); data.vertices[0] = 99; REJECT(draw(), "vertex command");
    recvxGameBootRenderReset(); CHECK(!recvxGameBootRenderPending());
    printf("PASS: original chunk transforms, hierarchy, clip tests, projection, lighting, batching and malformed state: %u checks\n",checks);
    return 0;
}
