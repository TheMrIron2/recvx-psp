#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "psp/game_boot_fog.h"
#include "ps2/veronica/prog/ps2_NaFog.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "psp/engine_original_lighting.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_NaView.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"
#include "ps2/veronica/prog/ps2_dummy.h"

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
static const RecvxBootTexture *pending_overlay_view;
void recvxGameBootDrawDiscard(void)
{
    if (pending_overlay_view) {
        assert(pending_overlay_view->palette[2] == 0xff123456u);
        pending_overlay_view = NULL;
    }
}
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
    recvxGameBootFogReset();
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
static void clip_model_vertices(const float positions[3][3],unsigned int attributes)
{
    for(unsigned int i=0;i<3;++i)
        for(unsigned int axis=0;axis<3;++axis)putfloat(data.vertices+64+i*32+axis*4,positions[i][axis]);
    memset(data.polygons,0,sizeof(data.polygons));
    data.polygons[0]=8;put16(data.polygons+2,0);
    unsigned char *strip=data.polygons+4;
    strip[0]=65;strip[1]=attributes;put16(strip+2,11);put16(strip+4,1);put16(strip+6,3);
    for(unsigned int i=0;i<3;++i){
        put16(strip+8+i*6,i);put16(strip+10+i*6,i?255:0);put16(strip+12+i*6,i==2?255:0);
    }
    put16(strip+26,255);recvxGameBootRenderTextureList(&textures);
}
static void specular_model(unsigned int diffuse,unsigned int specular,unsigned int attributes)
{
    reset();
    static const float p[3][3]={{-1,-1,4},{0,1,4},{1,-1,4}};
    clip_model_vertices(p,attributes);
    memmove(data.polygons+12,data.polygons,32);
    data.polygons[0]=21;data.polygons[1]=0;put16(data.polygons+2,4);
    memcpy(data.polygons+4,&diffuse,4);memcpy(data.polygons+8,&specular,4);
    recvxGameBootRenderTextureList(&textures);
}
static void specular_tests(void)
{
    const RecvxBootRenderFrame *f;
    specular_model(0x80ffffffu,0x11ff0000u,0);
    njCnkSetEasyLight(0,0,1);njCnkSetEasyLightColor(1,1,1);njCnkSetEasyLightIntensity(0.5f,1);
    njCnkEasyDrawModel(&data.model);f=recvxGameBootRenderFrame();
    CHECK(f->vertex_count==3&&f->batch_count==1&&(f->batches[0].sampler&RECVX_RENDER_COLOR_DOUBLE));
    CHECK(f->vertices[0].color==0x8080808fu); /* red 143, green/blue 128, unchanged alpha. */
    specular_model(0x80ffffffu,0x110000ffu,0);
    njCnkSetEasyMultiLight(1);njCnkSetEasyMultiLightSwitch(1,1);njCnkSetEasyMultiLightVector(0,0,1);
    njCnkSetEasyMultiLightColor(1,1.5f,1.5f,1.5f);f=draw();
    CHECK(f->vertices[0].color==0x808f8f8fu); /* Retail multi-light specular tint is white. */
    specular_model(0x80000000u,0x11ff0000u,0);
    njCnkSetSimpleLight(0,0,1);njCnkSetSimpleLightColor(1,1,1);njCnkSetSimpleLightIntensity(1,0);
    njCnkSimpleDrawModel(&data.model);f=recvxGameBootRenderFrame();
    CHECK(f->vertices[0].color==0x8000001fu);
    recvxGameBootRenderConsume();njCnkSetSimpleLightIntensity(0.9f,0);
    njCnkSimpleDrawModel(&data.model);f=recvxGameBootRenderFrame();
    unsigned int expected=(unsigned int)(0.5f*(31.0f/128.0f)*powf(0.9f,17)*255+0.5f);
    CHECK((f->vertices[0].color&255u)==expected&&expected==5);
    specular_model(0x80000000u,0x11ffffffu,0);
    njCnkSetSimpleMultiLight(1);njCnkSetSimpleMultiLightSwitch(1,1);njCnkSetSimpleMultiLightVector(0,0,1);
    njCnkSetSimpleMultiLightColor(1,0.5f,0.5f,0.5f);njCnkSetSimpleMultiAmbient(1,1,1);
    njCnkSimpleMultiDrawModel(&data.model);f=recvxGameBootRenderFrame();
    CHECK(f->vertices[0].color==0x80000000u); /* Ambient does not create Simple multi highlights. */
    for(unsigned int ignore=1;ignore<=2;++ignore){
        specular_model(0x80ffffffu,0x11ffffffu,ignore);
        njCnkSetEasyMultiAmbient(0.5f,0.5f,0.5f);f=draw();
        CHECK(!(f->batches[0].sampler&RECVX_RENDER_COLOR_DOUBLE));
        CHECK(f->vertices[0].color==(ignore==1?0x80ffffffu:0x80808080u));
    }
    specular_model(0x80ffffffu,0x11ffffffu,4);
    njCnkSetEasyLight(0,0,1);njCnkSetEasyLightColor(1,1,1);njCnkSetEasyLightIntensity(0.5f,1);
    njCnkEasyDrawModel(&data.model);f=recvxGameBootRenderFrame();
    CHECK(f->vertices[0].color==0x80404040u);
    specular_model(0x80ffffffu,0x11ffffffu,0);
    NJS_ARGB constant={0.5f,1,0,0};njControl3D(0x10);njSetConstantMaterial(&constant);
    f=draw();CHECK(!(f->batches[0].sampler&RECVX_RENDER_COLOR_DOUBLE));
    CHECK(f->vertices[0].color==0x80000000u); /* Existing constant diffuse lighting path. */
}

static void custom_clip_tests(void)
{
    const float crossing[3][3]={{-4,0,4},{0,1,4},{0,-1,4}};
    reset();clip_model_vertices(crossing,2);_Make_ClipVolume(160,240);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,1,1,1);njCnkSetEasyMultiLightPoint(2,0,0,0);
    njCnkSetEasyMultiLightRange(2,5,50);
    const RecvxBootRenderFrame *f=draw();
    CHECK(f->vertex_count==6&&f->batch_count==1&&f->batches[0].texture==&texture);
    unsigned int intersections=0;
    for(size_t i=0;i<f->vertex_count;++i){
        const RecvxBootRenderVertex *v=&f->vertices[i];
        CHECK(v->x>=-2.00001f&&v->x<=2.00001f&&v->z==-4);
        if(close_to(v->x,-2)){
            ++intersections;
            CHECK(close_to(v->u,0.5f)&&close_to(fabsf(v->y),0.5f));
            CHECK(close_to(v->v,v->y>0?0.5f:0)&&((v->color&255u)==194u)&&(v->color>>24)==255u);
        }
    }
    CHECK(intersections>=2);
    for(size_t i=0;i<f->vertex_count;i+=3){
        const RecvxBootRenderVertex *v=f->vertices+i;
        float area=(v[1].x-v[0].x)*(v[2].y-v[0].y)-(v[1].y-v[0].y)*(v[2].x-v[0].x);
        CHECK(area>0);
    }
    /* Each model clips against the volume present when it is submitted. */
    _Make_ClipVolume(1024,960);draw();f=recvxGameBootRenderFrame();CHECK(f->vertex_count==9);
    const float outside[3][3]={{-4,0,4},{-3,1,4},{-3,-1,4}};
    reset();clip_model_vertices(outside,3);_Make_ClipVolume(160,240);draw();CHECK(!recvxGameBootRenderPending());
    const float touching[3][3]={{-2,0,4},{-3,1,4},{-3,-1,4}};
    reset();clip_model_vertices(touching,3);_Make_ClipVolume(160,240);draw();CHECK(!recvxGameBootRenderPending());
    const float near_crossing[3][3]={{-1,0,0.5f},{0,1,2},{1,0,2}};
    reset();clip_model_vertices(near_crossing,3);_Make_ClipVolume(1024,960);f=draw();CHECK(f->vertex_count==6);
    for(size_t i=0;i<f->vertex_count;++i)CHECK(f->vertices[i].z<=-0.99999f&&f->vertices[i].z>=-2);
    const float far_crossing[3][3]={{-1,0,8},{0,1,2},{1,0,2}};
    reset();clip_model_vertices(far_crossing,3);_Make_ClipVolume(1024,960);njClipZ(-1,-5);f=draw();CHECK(f->vertex_count==6);
    for(size_t i=0;i<f->vertex_count;++i)CHECK(f->vertices[i].z>=-5.00001f&&f->vertices[i].z<=-2);
    const float enclosing[3][3]={{-100,-100,4},{100,-100,4},{0,100,4}};
    reset();clip_model_vertices(enclosing,3);_Make_ClipVolume(160,120);f=draw();CHECK(f->vertex_count>=6&&f->vertex_count<=21);
    for(size_t i=0;i<f->vertex_count;++i)CHECK(fabsf(f->vertices[i].x)<=2.00001f&&fabsf(f->vertices[i].y)<=1.50001f);
    const float guard_crossing[3][3]={{-8,0,4},{0,1,4},{0,-1,4}};
    reset();clip_model_vertices(guard_crossing,3);_Make_ClipVolume(-320,-240);f=draw();CHECK(f->vertex_count==6);
    for(size_t i=0;i<f->vertex_count;++i)CHECK(f->vertices[i].x>=-4.00001f);
    reset();_Make_ClipVolume(0,240);REJECT(draw(),"clip volume");
    reset();_Make_ClipVolume(NAN,240);REJECT(draw(),"clip volume");
}
static void shared_vertex_tests(void)
{
    for(unsigned int mode=0;mode<4;++mode){
        reset();
        unsigned int attributes=mode==1?0x22:mode==2?3:2;
        if(mode==3)for(unsigned int i=0;i<3;++i)putfloat(data.vertices+64+i*32+24,0);
        njCnkSetEasyMultiAmbient(0.2f,0.3f,0.4f);
        njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(1,1);njCnkSetEasyMultiLightSwitch(2,1);
        njCnkSetEasyMultiLightVector(0,0,1);njCnkSetEasyMultiLightColor(1,1,0,0);
        njCnkSetEasyMultiLightColor(2,0,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,5,20);
        data.polygons[1]=attributes;put16(data.polygons+2,26);put16(data.polygons+6,24);
        for(unsigned int i=0;i<24;++i)put16(data.polygons+8+2*i,i%3);
        put16(data.polygons+56,255);
        const RecvxBootRenderFrame *f=draw();CHECK(f->vertex_count==66);
        RecvxBootRenderVertex expected[66];memcpy(expected,f->vertices,sizeof(expected));
        RecvxBootRenderStats stats=recvxGameBootRenderStats();
        CHECK(stats.position_transforms==3);
        if(mode==0)CHECK(stats.normal_transforms==3&&stats.shaded_vertices==3);
        if(mode==1||mode==2)CHECK(stats.normal_transforms==0);
        if(mode==3)CHECK(stats.shaded_vertices==66); /* Face-dependent zero-normal fallback. */
        recvxGameBootRenderConsume();
        put16(data.polygons+2,5);put16(data.polygons+6,3);put16(data.polygons+14,255);
        for(unsigned int i=0;i<22;++i){
            unsigned int a=i%3,b=(i+1)%3,c=(i+2)%3;
            if(i&1u){unsigned int swap=a;a=b;b=swap;}
            put16(data.polygons+8,a);put16(data.polygons+10,b);put16(data.polygons+12,c);draw();
        }
        f=recvxGameBootRenderFrame();CHECK(f->vertex_count==66&&!memcmp(f->vertices,expected,sizeof(expected)));
        recvxGameBootRenderConsume();putfloat(data.vertices+64,7);
        put16(data.polygons+8,0);put16(data.polygons+10,1);put16(data.polygons+12,2);
        f=draw();CHECK(f->vertices[0].x==7); /* Stream deformation invalidates the cache each draw. */
    }
    reset();data.polygons[1]=3;
    unsigned char strip[14];memcpy(strip,data.polygons,sizeof(strip));
    data.polygons[14]=17;data.polygons[15]=0;put16(data.polygons+16,2);
    unsigned int red=0xffff0000u;memcpy(data.polygons+18,&red,4);
    memcpy(data.polygons+22,strip,sizeof(strip));put16(data.polygons+36,255);
    const RecvxBootRenderFrame *f=draw();
    CHECK(f->vertex_count==6&&f->vertices[0].color==0xffffffffu&&f->vertices[3].color==0xff0000ffu);
    CHECK(recvxGameBootRenderStats().position_transforms==3&&recvxGameBootRenderStats().shaded_vertices==6);
    reset();recvxGameBootRenderTextureList(&textures);
    data.polygons[0]=8;put16(data.polygons+2,0);
    unsigned char *p=data.polygons+4;p[0]=65;p[1]=3;put16(p+2,21);put16(p+4,2);
    for(unsigned int strip_index=0;strip_index<2;++strip_index){
        unsigned char *q=p+6+strip_index*20;put16(q,3);
        for(unsigned int i=0;i<3;++i){put16(q+2+i*6,i);put16(q+4+i*6,strip_index*255);put16(q+6+i*6,strip_index*255);}
    }
    put16(data.polygons+50,255);f=draw();
    CHECK(f->vertex_count==6&&f->vertices[0].u==0&&close_to(f->vertices[3].u,1));
    CHECK(recvxGameBootRenderStats().position_transforms==3);

}
static void benchmark_strip(void)
{
    if(!getenv("RECVX_RENDER_BENCHMARK"))return;
    reset();data.polygons[1]=2;put16(data.polygons+2,26);put16(data.polygons+6,24);
    for(unsigned int i=0;i<24;++i)put16(data.polygons+8+2*i,i%3);
    put16(data.polygons+56,255);
    clock_t start=clock();unsigned int total=0;
    for(unsigned int i=0;i<20000;++i){
        const RecvxBootRenderFrame *f=draw();total+=f->vertex_count;
        recvxGameBootRenderConsume();
    }
    RecvxBootRenderStats stats=recvxGameBootRenderStats();
    printf("RENDER BENCH: %u vertices %.3f CPU seconds, pos=%u normal=%u shade=%u\n",total,(double)(clock()-start)/CLOCKS_PER_SEC,stats.position_transforms,stats.normal_transforms,stats.shaded_vertices);
}
int main(void)
{
    benchmark_strip();
    const RecvxBootRenderFrame *f;
    reset(); njGenerateFogTable3(NULL,-1,-100); njSetFogColor(0xff123456); njFogEnable();
    f=draw(); draw();
    CHECK(f->batch_count==1 && f->batches[0].fog_enabled && f->batches[0].fog_color==0x563412);
    CHECK(f->batches[0].fog_near==1 && f->batches[0].fog_far==100 && f->batches[0].count==6);
    njSetFogColor(0xffabcdef); draw();
    njGenerateFogTable3(NULL,-2,-200); draw(); njFogDisable(); draw();
    f=recvxGameBootRenderFrame();
    CHECK(f->batch_count==4 && f->batches[0].fog_color==0x563412 && f->batches[1].fog_color==0xefcdab);
    CHECK(f->batches[1].fog_near==1 && f->batches[2].fog_near==2 && f->batches[2].fog_far==200);
    CHECK(!f->batches[3].fog_enabled && f->batches[3].fog_near==0 && f->batches[3].fog_far==0);
    CHECK(f->vertices[0].color==0xffffffffu); /* Fog happens after texturing in GE. */
    reset(); njFogEnable(); REJECT(draw(),"geometry fog range");
    reset(); f = draw();
    CHECK(f->vertex_count == 3 && f->batch_count == 1 && recvxGameBootRenderPending());
    CHECK(f->vertices[0].x == -1 && f->vertices[0].y == 1 && f->vertices[0].z == -4);
    CHECK(f->vertices[0].color == 0xffffffffu && !f->batches[0].texture);
    CHECK(close_to(f->projection[0], 1.174f) && close_to(f->projection[5], 4.0f/3.0f));
    CHECK(f->projection[11] == -1 && f->projection[15] == 0);
    CHECK(!((uintptr_t)f->projection & 15));
    draw(); f = recvxGameBootRenderFrame(); CHECK(f->vertex_count == 6 && f->batch_count == 1);
    recvxGameBootRenderConsume(); CHECK(!recvxGameBootRenderPending());

    reset(); f=draw();
    float queued_projection[16]; memcpy(queued_projection,f->projection,sizeof(queued_projection));
    njSetPerspective(0x2000); njClipZ(-5,-20);
    f=recvxGameBootRenderFrame(); CHECK(!memcmp(f->projection,queued_projection,sizeof(queued_projection)));
    recvxGameBootRenderConsume(); f=draw();
    CHECK(f->projection[0] != queued_projection[0] && close_to(f->projection[10],-25.0f/15.0f));

    reset(); f=draw();
    RecvxBootRenderVertex original_triangle[3];memcpy(original_triangle,f->vertices,sizeof(original_triangle));
    recvxGameBootRenderConsume();njScale(NULL,-1,1,1);f=draw();
    CHECK(f->vertices[0].x==-original_triangle[0].x && f->vertices[1].x==-original_triangle[2].x);
    CHECK(f->vertices[1].y==original_triangle[2].y && f->vertices[2].y==original_triangle[1].y);
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
    reset(); data.polygons[1] = 0; CHECK(draw()->vertex_count==3); /* Retail untextured strips use diffuse/ambient only. */
    reset(); data.polygons[1] = 0x43; REJECT(draw(), "environment-mapped");
    reset(); data.vertices[0] = 99; REJECT(draw(), "vertex command");
    reset();
    RecvxGameChunkVertex sprite[3] = {
        {.x=-1,.y=-1,.z=4,.u=0,.v=0,.color=0x80112233},
        {.x=0,.y=1,.z=4,.u=0.5f,.v=1,.color=0xff445566},
        {.x=1,.y=-1,.z=4,.u=1,.v=0,.color=0xff778899}
    };
    njTranslate(NULL, 3, 2, 1);
    njControl3D(0x810); njSetConstantAttr(0, 0); njSetConstantMaterial(&constant);
    recvxGameBootRenderPolygonBlend(0, 11); recvxGameBootRenderPolygonBlend(1, 11);
    recvxGameBootRenderSpriteTriangle(sprite, 0, 1); f=recvxGameBootRenderFrame();
    CHECK(f->vertex_count == 3 && f->vertices[0].x == 2 && f->vertices[0].y == -1 && f->vertices[0].z == -5);
    CHECK(f->vertices[0].color == 0x80332211 && f->vertices[1].color == 0xff665544);
    CHECK(f->batches[0].attributes == 0x1b && f->batches[0].blend_source == 11 && f->batches[0].blend_destination == 11);
    REJECT(recvxGameBootRenderSpriteTriangle(sprite, 1, 1), "texture selection");
    recvxGameBootRenderTextureSelected(&texture); recvxGameBootRenderTextureFilter(1);
    recvxGameBootRenderSpriteTriangle(sprite, 1, 0); f=recvxGameBootRenderFrame();
    CHECK(f->batch_count == 2 && f->batches[1].texture == &texture && f->batches[1].sampler == 7);
    CHECK(f->vertices[4].u == 0.5f && f->vertices[4].v == 1 && f->batches[1].attributes == 0x13);
    sprite[0].u = NAN; REJECT(recvxGameBootRenderSpriteTriangle(sprite, 1, 1), "sprite vertex");
    REJECT(recvxGameBootRenderPolygonBlend(2, 8), "blend mode");
    reset();
    recvxGameBootRenderLine(sprite, 1); f=recvxGameBootRenderFrame();
    CHECK(f->vertex_count == 2 && f->batches[0].kind == RECVX_RENDER_LINES && f->batches[0].count == 2);
    CHECK(f->vertices[0].color == 0x80332211 && f->vertices[1].z == -4);
    REJECT(recvxGameBootRenderShadowTriangle(sprite, 0), "modifier pass");
    REJECT(recvxGameBootRenderShadowEnd(), "modifier end");
    recvxGameBootRenderShadowStart();
    REJECT(recvxGameBootRenderShadowStart(), "nested");
    recvxGameBootRenderShadowTriangle(sprite, 0);
    recvxGameBootRenderShadowTriangle(sprite, 1);
    const float shadow_bounds[4] = {100,100,200,200};
    recvxGameBootRenderShadowComposite(shadow_bounds, 255);
    recvxGameBootRenderShadowComposite(shadow_bounds, 127);
    recvxGameBootRenderShadowEnd(); f=recvxGameBootRenderFrame();
    CHECK(f->batch_count == 7 && f->vertex_count == 12);
    CHECK(f->batches[1].kind == RECVX_RENDER_SHADOW_START && f->batches[1].count == 0);
    CHECK(f->batches[2].kind == RECVX_RENDER_SHADOW_ADD && f->batches[3].kind == RECVX_RENDER_SHADOW_SUBTRACT);
    CHECK(f->vertices[2].color == 0xff202020 && f->vertices[8].color == 0xff5e5e5e);
    CHECK(f->vertices[10].color == 0xff2e2e2e && f->batches[5].count == 2);
    CHECK(f->vertices[8].x == 75 && close_to(f->vertices[8].y, 56.666668f) && f->vertices[8].u == 75.375f);
    CHECK(f->batches[6].kind == RECVX_RENDER_SHADOW_END);
    recvxGameBootRenderShadowStart();
    REJECT(recvxGameBootRenderShadowComposite(shadow_bounds, 256), "composite");
    float invalid_bounds[4] = {0,0,641,20};
    REJECT(recvxGameBootRenderShadowComposite(invalid_bounds, 255), "bounds");
    recvxGameBootRenderShadowEnd();
    reset(); sprite[0].u = 0;
    recvxGameBootRenderTextureSelected(&texture);
    recvxGameBootRenderHalfFogTriangle(sprite, 0x80112233, 1);
    recvxGameBootRenderHalfFogTriangle(sprite, 0xff112233, 1);
    recvxGameBootRenderHalfFogTriangle(sprite, 0xff112234, 1);
    recvxGameBootRenderSpriteTriangle(sprite, 1, 1); f=recvxGameBootRenderFrame();
    CHECK(f->batch_count == 3 && f->batches[0].kind == RECVX_RENDER_HALF_FOG && f->batches[0].count == 6);
    CHECK(f->batches[0].fog_color == 0x332211 && f->batches[1].fog_color == 0x342211);
    CHECK(f->batches[2].kind == RECVX_RENDER_TRIANGLES && f->batches[2].fog_color == 0);
    CHECK(f->vertices[0].color == 0x80332211 && f->vertices[1].color == 0xff665544);
    sprite[0].z = 0x1p39f;
    REJECT(recvxGameBootRenderHalfFogTriangle(sprite, 0, 1), "fog depth");
    sprite[0].z = 0x1p38f;
    recvxGameBootRenderHalfFogTriangle(sprite, 0, 1);
    CHECK(recvxGameBootRenderFrame()->batches[3].kind == RECVX_RENDER_HALF_FOG);
    reset(); recvxGameBootRenderTextureSelected(&texture); recvxGameBootRenderTextureFilter(1);
    RecvxGameChunkVertex screen[3]={
        {.x=0,.y=0,.z=1,.u=0,.v=0,.color=0x80112233},
        {.x=320,.y=480,.z=2,.u=0.5f,.v=1,.color=0xff445566},
        {.x=640,.y=0,.z=100,.u=1,.v=0,.color=0xff778899}
    };
    njTranslate(NULL,100,200,300); recvxGameBootRenderScreenTriangle(screen,1); f=recvxGameBootRenderFrame();
    CHECK(f->batch_count==1 && f->vertex_count==3 && f->batches[0].kind==RECVX_RENDER_SCREEN_TRIANGLES);
    CHECK(f->vertices[0].x==0 && f->vertices[0].y==0 && f->vertices[1].x==240 && f->vertices[1].y==272);
    CHECK(f->vertices[1].u==8 && f->vertices[1].v==8 && f->vertices[2].x==480);
    CHECK(f->vertices[0].color==0x80332211 && f->batches[0].sampler==7 && f->vertices[0].z==65534);
    CHECK(f->vertices[1].z==65534 && f->vertices[2].z==65534);
    screen[0].z=0.2f; recvxGameBootRenderScreenTriangle(screen,1);
    CHECK(recvxGameBootRenderFrame()->vertex_count==6);
    recvxGameBootRenderHalfFogScreenTriangle(screen,0xff204060,1);
    f=recvxGameBootRenderFrame();
    CHECK(f->batch_count==3 && f->batches[1].kind==RECVX_RENDER_SCREEN_OFFSET &&
        f->batches[2].kind==RECVX_RENDER_SCREEN_HALF_BASE);
    CHECK(f->batches[1].fog_color==0x302010 && f->vertices[6].color==0x80302010 && f->vertices[9].color==0x80191108);
    CHECK(f->batches[2].blend_source==8 && f->batches[2].blend_destination==10);
    screen[0].z=0; REJECT(recvxGameBootRenderScreenTriangle(screen,1),"screen sprite vertex");
    screen[0].z=1; screen[0].u=INFINITY; REJECT(recvxGameBootRenderScreenTriangle(screen,1),"screen sprite vertex");
    reset(); sprite[0].z=4; sprite[0].u=0;
    NJS_SCREEN item_screen={.dist=500,.w=352,.h=184,.cx=235,.cy=224};
    recvxEngineOriginalViewSetScreen(&item_screen);
    NJS_POINT2 item_clip[2]={{2,3},{10,8}};
    njUserClipping(2,item_clip);
    recvxGameBootRenderSpriteTriangle(sprite,0,1);
    njUserClipping(0,NULL);
    recvxGameBootRenderSpriteTriangle(sprite,0,1);
    f=recvxGameBootRenderFrame();
    CHECK(f->batch_count==2 && f->batches[0].scissor[0]==48 && f->batches[0].scissor[1]==54);
    CHECK(f->batches[0].scissor[2]==264 && f->batches[1].scissor[2]==480 && f->batches[1].scissor[3]==272);
    CHECK(fabsf(f->projection[8]-85.0f/320)<.0001f && fabsf(f->projection[9]+16.0f/240)<.0001f);
    item_clip[1].x=-1; REJECT(njUserClipping(2,item_clip),"user clipping");
    reset(); sprite[0].z=4; sprite[0].u=0;
    recvxGameBootRenderTextureSelected(&texture); texture.palette[2]=0xff112233u;
    recvxGameBootRenderSpriteTriangle(sprite,1,1);
    const RecvxBootTexture *first_palette=recvxGameBootRenderSnapshotTexture(&texture);
    texture.palette[2]=0xff445566u;
    recvxGameBootRenderSpriteTriangle(sprite,1,1);
    const RecvxBootTexture *second_palette=recvxGameBootRenderSnapshotTexture(&texture);
    texture.palette[2]=0xff778899u;
    /* Vertex storage grows while earlier batches retain stable texture views. */
    for(unsigned int i=0;i<1600;++i)recvxGameBootRenderSpriteTriangle(sprite,1,1);
    f=recvxGameBootRenderFrame();
    CHECK(f->batch_count==3&&f->batches[0].texture==first_palette&&
          f->batches[1].texture==second_palette&&f->batches[2].texture==&texture);
    CHECK(first_palette->palette[2]==0xff112233u&&second_palette->palette[2]==0xff445566u&&
          texture.palette[2]==0xff778899u);
    CHECK(first_palette->source==texture.source&&first_palette->pixels==texture.pixels&&
          second_palette->source==texture.source&&second_palette->pixels==texture.pixels);
    CHECK(!((uintptr_t)first_palette->palette&15u)&&!((uintptr_t)second_palette->palette&15u));
    REJECT(recvxGameBootRenderSnapshotTexture(NULL),"snapshot texture");
    recvxGameBootRenderConsume();
    for(unsigned int frame_index=0;frame_index<260;++frame_index){
        texture.palette[2]=frame_index;
        recvxGameBootRenderSpriteTriangle(sprite,1,1);
        const RecvxBootTexture *saved=recvxGameBootRenderSnapshotTexture(&texture);
        texture.palette[2]=frame_index+1;
        CHECK(saved->palette[2]==frame_index);
        recvxGameBootRenderConsume();
    }
    for(unsigned int i=0;i<256;++i)recvxGameBootRenderSnapshotTexture(&texture);
    REJECT(recvxGameBootRenderSnapshotTexture(&texture),"snapshot capacity");
    recvxGameBootRenderConsume();
    CHECK(recvxGameBootRenderSnapshotTexture(&texture)->pixels==pixels);
    texture.palette[2] = 0xff123456u;
    pending_overlay_view = recvxGameBootRenderSnapshotTexture(&texture);
    recvxGameBootRenderSpriteTriangle(sprite,1,1);
    const RecvxGameBootRenderState before_clear = *recvxGameBootRenderStateGet();
    CHECK(recvxGameBootRenderPending());
    Ps2ClearOT();
    f = recvxGameBootRenderFrame();
    CHECK(!recvxGameBootRenderPending() && !pending_overlay_view && !f->vertex_count && !f->batch_count);
    CHECK(!memcmp(&before_clear,recvxGameBootRenderStateGet(),sizeof(before_clear)));
    CHECK(texture.source && texture.pixels == pixels && texture.palette[2] == 0xff123456u);
    /* Clear is repeatable, releases snapshot capacity, and permits fresh
       geometry/shadow submissions without resetting the material state. */
    Ps2ClearOT();
    for(unsigned int i=0;i<256;++i)recvxGameBootRenderSnapshotTexture(&texture);
    Ps2ClearOT();
    recvxGameBootRenderShadowStart(); Ps2ClearOT(); recvxGameBootRenderShadowStart(); Ps2ClearOT();
    recvxGameBootRenderSpriteTriangle(sprite,1,1);
    f=recvxGameBootRenderFrame(); CHECK(f->vertex_count==3 && f->batch_count==1);
    pending_overlay_view = recvxGameBootRenderSnapshotTexture(&texture);
    recvxGameBootRenderReset(); CHECK(!recvxGameBootRenderPending() && !pending_overlay_view);
    custom_clip_tests();shared_vertex_tests();specular_tests();recvxGameBootRenderReset();
    printf("PASS: original chunk transforms, hierarchy, custom plane clipping/UV/color interpolation, projection, lighting, batching, immutable palette view lifetime/capacity and malformed state: %u checks\n",checks);
    return 0;
}
