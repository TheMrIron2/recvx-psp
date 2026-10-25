#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "psp/game_boot_render.h"
#include "psp/game_boot_vector.h"
#include "game_boot_cull_check.h"
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
    unsigned char vertices[192], polygons[64];
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
    recvxGameBootRenderReset(); recvxGameBootRenderGroupBoundsEnable(0); recvxEngineOriginalFaultReset();
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
    const float inside[3][3]={{-1,0,4},{0,1,4},{1,-1,4}};
    reset();clip_model_vertices(inside,3);
    const RecvxBootRenderFrame *original=draw();
    RecvxBootRenderVertex expected[3];memcpy(expected,original->vertices,sizeof(expected));
    CHECK(!recvxGameBootRenderStats().clip_inside);
    recvxGameBootRenderConsume();_Make_ClipVolume(160,240);
    const RecvxBootRenderFrame *accepted=draw();
    CHECK(accepted->vertex_count==3&&!memcmp(expected,accepted->vertices,sizeof(expected)));
    CHECK(recvxGameBootRenderStats().clip_inside==1);
    CHECK(!recvxGameBootRenderStats().clip_intersections);
    const float crossing[3][3]={{-4,0,4},{0,1,4},{0,-1,4}};
    reset();clip_model_vertices(crossing,2);_Make_ClipVolume(160,240);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,1,1,1);njCnkSetEasyMultiLightPoint(2,0,0,0);
    njCnkSetEasyMultiLightRange(2,5,50);
    const RecvxBootRenderFrame *f=draw();
    CHECK(recvxGameBootRenderStats().clip_intersections==1);
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
    CHECK(recvxGameBootRenderStats().clip_rejected==1);
    CHECK(!recvxGameBootRenderStats().normal_transforms&&!recvxGameBootRenderStats().shaded_vertices);
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
        CHECK(stats.material_preparations==1);
        CHECK(stats.face_normalizations==((mode==1||mode==3)?22u:0u));
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
    if(getenv("RECVX_RENDER_BENCHMARK_CLIP"))_Make_ClipVolume(1024,960);
    clock_t start=clock();unsigned int total=0;
    for(unsigned int i=0;i<20000;++i){
        const RecvxBootRenderFrame *f=draw();total+=f->vertex_count;
        recvxGameBootRenderConsume();
    }
    RecvxBootRenderStats stats=recvxGameBootRenderStats();
    printf("RENDER BENCH: %u vertices %.3f CPU seconds, pos=%u normal=%u shade=%u\n",total,(double)(clock()-start)/CLOCKS_PER_SEC,stats.position_transforms,stats.normal_transforms,stats.shaded_vertices);
}
/* Evaluate the GE diffuse equation independently, then compare to the
   existing Ninja CPU renderer under rotated, translated and scaled models. */
static void gpu_compare_reference(void)
{
    for(unsigned int range=0;range<8;++range)for(unsigned int mode=0;mode<4;++mode)for(unsigned int reflected=0;reflected<3;++reflected){
        specular_model(0x80c08040u,0,2);
        njTranslate(NULL,3,2,1);njRotateY(NULL,0x2300);njRotateX(NULL,0x1000);
        njScale(NULL,reflected==1?-2:2,reflected==2?3:2,2);
        njCnkSetEasyLight(0.2f,0.3f,1);njCnkSetSimpleLight(0.2f,0.3f,1);
        njCnkSetEasyLightColor(0.6f,0.8f,0.4f);njCnkSetSimpleLightColor(0.6f,0.8f,0.4f);
        njCnkSetEasyLightIntensity(0.75f,0.125f);njCnkSetSimpleLightIntensity(0.75f,0.125f);
        njCnkSetEasyMultiAmbient(0.125f,0.25f,0.375f);njCnkSetSimpleMultiAmbient(0.125f,0.25f,0.375f);
        njCnkSetEasyMultiLight(2);njCnkSetSimpleMultiLight(2);
        njCnkSetEasyMultiLightVector(0.2f,0.3f,1);njCnkSetSimpleMultiLightVector(0.2f,0.3f,1);
        njCnkSetEasyMultiLightPoint(2,1,2,-8);njCnkSetSimpleMultiLightPoint(2,1,2,-8);
        njCnkSetEasyMultiLightRange(2,3,40);njCnkSetSimpleMultiLightRange(2,3,40);
        for(int i=1;i<=2;++i){
            njCnkSetEasyMultiLightSwitch(i,1);njCnkSetSimpleMultiLightSwitch(i,1);
            njCnkSetEasyMultiLightColor(i,0.5f,0.25f,0.75f);njCnkSetSimpleMultiLightColor(i,0.5f,0.25f,0.75f);
        }
        if(range==1){
            njCnkSetEasyLightIntensity(2.25f,0.125f);njCnkSetSimpleLightIntensity(2.25f,0.125f);
            njCnkSetEasyMultiLightColor(1,1.75f,0.75f,1.25f);njCnkSetSimpleMultiLightColor(1,1.75f,0.75f,1.25f);
        }else if(range==2){
            njCnkSetEasyLightIntensity(1.5f,1.25f);njCnkSetSimpleLightIntensity(1.5f,1.25f);
            njCnkSetEasyMultiAmbient(1.25f,0.25f,0.5f);njCnkSetSimpleMultiAmbient(1.25f,0.25f,0.5f);
        }else if(range==6||range==7){
            njCnkSetEasyMultiLight(2);njCnkSetSimpleMultiLight(2);
            njCnkSetEasyMultiLightSwitch(1,0);njCnkSetSimpleMultiLightSwitch(1,0);
            njCnkSetEasyMultiLightColor(2,5,0.5f,1);njCnkSetSimpleMultiLightColor(2,5,0.5f,1);
            njCnkSetEasyMultiLightRange(2,range==6?1:6,40);njCnkSetSimpleMultiLightRange(2,range==6?1:6,40);
        }else if(range==5){
            njCnkSetEasyLightColor(5,1,0);njCnkSetSimpleLightColor(5,1,0);
            njCnkSetEasyLightIntensity(1,0.125f);njCnkSetSimpleLightIntensity(1,0.125f);
            njCnkSetEasyMultiLight(4);njCnkSetSimpleMultiLight(4);
            njCnkSetEasyMultiLightColor(1,5,1,0);njCnkSetSimpleMultiLightColor(1,5,1,0);
            for(int i=2;i<=4;++i){
                njCnkSetEasyMultiLightSwitch(i,1);njCnkSetSimpleMultiLightSwitch(i,1);
                njCnkSetEasyMultiLightColor(i,0.125f,0.25f,0.375f);njCnkSetSimpleMultiLightColor(i,0.125f,0.25f,0.375f);
                njCnkSetEasyMultiLightPoint(i,i,2,-8);njCnkSetSimpleMultiLightPoint(i,i,2,-8);
                njCnkSetEasyMultiLightRange(i,3,40);njCnkSetSimpleMultiLightRange(i,3,40);
            }
        }else if(range==4){
            njCnkSetEasyLightIntensity(5,0.125f);njCnkSetSimpleLightIntensity(5,0.125f);
            njCnkSetEasyMultiLightColor(1,5,1,0);njCnkSetSimpleMultiLightColor(1,5,1,0);
        }else if(range==3){
            njCnkSetEasyLightColor(1.00000012f,0.8f,0.4f);njCnkSetSimpleLightColor(1.00000012f,0.8f,0.4f);
            njCnkSetEasyMultiLightColor(1,1.00000012f,0.75f,0.25f);njCnkSetSimpleMultiLightColor(1,1.00000012f,0.75f,0.25f);
        }
        njCnkSetCurrentDrawMode(mode);njCnkDrawModelLocal(&data.model);
        RecvxBootRenderVertex cpu[3];memcpy(cpu,recvxGameBootRenderFrame()->vertices,sizeof(cpu));
        recvxGameBootRenderConsume();recvxGameBootRenderGpuEnable(1);njCnkDrawModelLocal(&data.model);
        const RecvxBootRenderFrame *f=recvxGameBootRenderFrame();
        if(f->gpu_vertex_count!=3){RecvxBootRenderStats r=recvxGameBootRenderStats();fprintf(stderr,"GPU compare mode=%u reflected=%u reasons=%u,%u,%u,%u,%u,%u,%u,%u\n",mode,reflected,r.gpu_fallback[0],r.gpu_fallback[1],r.gpu_fallback[2],r.gpu_fallback[3],r.gpu_fallback[4],r.gpu_fallback[5],r.gpu_fallback[6],r.gpu_fallback[7]);}
        CHECK(f->gpu_vertex_count==3&&!f->vertex_count);
        const RecvxBootRenderBatch *b=&f->batches[0];const RecvxBootGpuState *g=&b->gpu_state;
        for(unsigned int v=0;v<3;++v){
            const RecvxBootGpuVertex *p=&f->gpu_vertices[v];float position[3],normal[3],n2=0;
            for(unsigned int k=0;k<3;++k){
                position[k]=g->model[k]*p->x+g->model[k+4]*p->y+g->model[k+8]*p->z+g->model[k+12];
                normal[k]=g->model[k]*p->nx+g->model[k+4]*p->ny+g->model[k+8]*p->nz;n2+=normal[k]*normal[k];
            }
            CHECK(close_to(position[0],cpu[v].x)&&close_to(position[1],cpu[v].y)&&close_to(position[2],cpu[v].z));
            for(unsigned int k=0;k<3;++k)normal[k]/=sqrtf(n2);
            float illumination[3];for(unsigned int k=0;k<3;++k)illumination[k]=(((g->vertex_ambient?p->color:g->ambient)>>(8*k))&255)/255.0f;
            for(unsigned int i=0;i<g->light_count;++i)if(b->gpu_mask&(1u<<i)){
                const RecvxBootGpuLight *l=&g->lights[i];float direction[3],d2=0,dot=0;
                for(unsigned int k=0;k<3;++k){direction[k]=l->position[k]-(l->point?position[k]:0);d2+=direction[k]*direction[k];}
                for(unsigned int k=0;k<3;++k)dot+=normal[k]*direction[k]/sqrtf(d2);
                float intensity=fmaxf(0,dot)*(l->point?fminf(1,1/(l->quadratic*d2)):1);
                for(unsigned int k=0;k<3;++k)illumination[k]+=intensity*((l->color>>(8*k))&255)/255.0f;
            }
            for(unsigned int k=0;k<3;++k){
                int predicted=(int)(fminf(1,illumination[k]*((g->diffuse>>(8*k))&255)/255.0f)*255+0.5f);
                int expected=(cpu[v].color>>(8*k))&255;
                CHECK(abs(predicted-expected)<=2);
            }
            CHECK((g->diffuse>>24)==(cpu[v].color>>24));
        }
    }
    reset();
}

static void gpu_prelit_queue_transition(void)
{
    reset();recvxGameBootRenderGpuEnable(1);data.polygons[1]=2;
    put16(data.vertices+2,47);put16(data.vertices+6,4);
    putfloat(data.vertices+64+3*32+8,2);putfloat(data.vertices+64+3*32+24,-1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,1,3);njCnkSetEasyMultiLightColor(2,0,1,0);
    put16(data.polygons+2,9);put16(data.polygons+4,2);
    put16(data.polygons+6,3);put16(data.polygons+8,0);put16(data.polygons+10,1);put16(data.polygons+12,3);
    put16(data.polygons+14,3);put16(data.polygons+16,0);put16(data.polygons+18,1);put16(data.polygons+20,2);
    put16(data.polygons+22,255);
    const RecvxBootRenderFrame *f=draw();
    CHECK(f->vertex_count==3&&f->gpu_vertex_count==3&&f->batch_count==2);
    CHECK(f->batches[0].gpu==2&&f->batches[0].first==0&&f->batches[0].count==3);
    CHECK(f->batches[1].gpu==1&&f->batches[1].first==0&&f->batches[1].count==3&&f->batches[1].gpu_mask==0);
    CHECK(f->vertices[2].z==2&&f->gpu_vertices[2].z==4);
    reset();
}

static void gpu_shared_light_classifications(void)
{
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,0,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,1,20);
    put16(data.polygons+2,26);put16(data.polygons+6,24);
    for(unsigned int i=0;i<24;++i)put16(data.polygons+8+2*i,i%3);
    put16(data.polygons+56,255);
    const RecvxBootRenderFrame *f=draw();
    CHECK(f->gpu_vertex_count==66&&f->vertex_count==0&&f->batches[0].gpu_mask==1);
    CHECK(recvxGameBootRenderStats().gpu_classifications==3);
    CHECK(recvxGameBootRenderStats().position_transforms==0&&recvxGameBootRenderStats().normal_transforms==0);
    draw();CHECK(recvxGameBootRenderStats().gpu_classifications==6);
    recvxGameBootRenderConsume();putfloat(data.vertices+64,20);draw();
    CHECK(recvxGameBootRenderStats().gpu_classifications==9&&recvxGameBootRenderStats().gpu_fallback[6]>0);
    reset();
}

static void gpu_bulk_reference_tests(void)
{
    for(unsigned int variant=0;variant<4;++variant){
        reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGpuStripsEnable(0);
        njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
        njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,1,200);
        if(variant&1)njScale(NULL,-2,3,2);
        put16(data.polygons+2,26);put16(data.polygons+6,variant&2?65536u-24:24);
        for(unsigned int i=0;i<24;++i)put16(data.polygons+8+2*i,i%3);
        put16(data.polygons+56,255);
        const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==66);
        RecvxBootGpuVertex expected[66];memcpy(expected,f->gpu_vertices,sizeof(expected));
        CHECK(recvxGameBootRenderStats().gpu_bulk_triangles==0);
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripsEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==66&&f->batch_count==1&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
        CHECK(recvxGameBootRenderStats().gpu_bulk_triangles==22);
    }
    reset();recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGpuStripsEnable(0);
    const float positions[3][3]={{-1,-1,4},{0,1,4},{1,-1,4}};clip_model_vertices(positions,3);
    const RecvxBootRenderFrame *f=draw();RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
    recvxGameBootRenderConsume();recvxGameBootRenderGpuStripsEnable(1);f=draw();
    CHECK(f->gpu_vertex_count==3&&!memcmp(expected,f->gpu_vertices,sizeof(expected))&&recvxGameBootRenderStats().gpu_bulk_triangles==1);
    reset();
}

static void compare_native_strip(const RecvxBootRenderFrame *f,const RecvxBootGpuVertex *expected,unsigned int expected_count)
{
    unsigned int matched=0;
    for(unsigned int v=2;v<f->gpu_vertex_count;++v){
        unsigned int a=v-2,b=v-1,d=v;
        if(v&1u){unsigned int swap=a;a=b;b=swap;}
        const RecvxBootGpuVertex *p[3]={&f->gpu_vertices[a],&f->gpu_vertices[b],&f->gpu_vertices[d]};
        if((p[0]->x==p[1]->x&&p[0]->y==p[1]->y&&p[0]->z==p[1]->z)||
           (p[0]->x==p[2]->x&&p[0]->y==p[2]->y&&p[0]->z==p[2]->z)||
           (p[1]->x==p[2]->x&&p[1]->y==p[2]->y&&p[1]->z==p[2]->z))continue;
        CHECK(matched+3<=expected_count);
        unsigned int equal=0;
        for(unsigned int rotation=0;rotation<3;++rotation)
            if(!memcmp(p[0],&expected[matched+rotation],sizeof(**p))&&
               !memcmp(p[1],&expected[matched+(rotation+1)%3],sizeof(**p))&&
               !memcmp(p[2],&expected[matched+(rotation+2)%3],sizeof(**p)))equal=1;
        CHECK(equal);matched+=3;
    }
    CHECK(matched==expected_count);
}

/* Independently expand GE strips, omitting bridge/retail degenerates, and
   compare every shaded vertex and outward winding with the triangle path. */
static void gpu_native_strip_tests(int native_lighting)
{
    for(unsigned int variant=0;variant<32;++variant){
        reset();recvxGameBootRenderGpuEnable(1);recvxGameBootRenderNativeLightingEnable(native_lighting);recvxGameBootRenderGpuStripsEnable(0);
        data.polygons[1]=variant&16?2:3;
        if(variant&1)njScale(NULL,-2,3,2);
        unsigned int cursor=6;put16(data.polygons+4,2);
        for(unsigned int strip=0;strip<2;++strip){
            unsigned int count=10+!!(variant&2);
            unsigned int flip=!!(variant&(strip?8:4));
            put16(data.polygons+cursor,flip?65536u-count:count);cursor+=2;
            for(unsigned int v=0;v<count;++v){
                put16(data.polygons+cursor,v==3?2:v%3);cursor+=2;
            }
        }
        put16(data.polygons+2,cursor/2-2);put16(data.polygons+cursor,255);
        const RecvxBootRenderFrame *f=draw();
        unsigned int expected_count=f->gpu_vertex_count;
        RecvxBootGpuVertex expected[48];CHECK(expected_count<=48);
        memcpy(expected,f->gpu_vertices,expected_count*sizeof(*expected));
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripsEnable(1);
        recvxGameBootRenderGpuNativeStripsEnable(1);f=draw();
        CHECK(f->batch_count==1&&f->batches[0].gpu_strip&&f->gpu_vertex_count<expected_count);
        compare_native_strip(f,expected,expected_count);
    }
    reset();recvxGameBootRenderGpuEnable(1);recvxGameBootRenderNativeLightingEnable(native_lighting);recvxGameBootRenderGpuStripsEnable(0);
    recvxGameBootRenderTextureList(&textures);
    data.polygons[0]=8;put16(data.polygons+2,0);
    unsigned char *p=data.polygons+4;p[0]=65;p[1]=3;put16(p+2,26);put16(p+4,1);put16(p+6,8);
    for(unsigned int v=0;v<8;++v){
        put16(p+8+v*6,v%3);put16(p+10+v*6,v*31);put16(p+12+v*6,v*19);
    }
    put16(p+56,255);
    const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==18);
    RecvxBootGpuVertex textured[18];memcpy(textured,f->gpu_vertices,sizeof(textured));
    recvxGameBootRenderConsume();recvxGameBootRenderGpuStripsEnable(1);recvxGameBootRenderGpuNativeStripsEnable(1);
    f=draw();CHECK(f->gpu_vertex_count==8&&f->batches[0].gpu_strip);
    compare_native_strip(f,textured,18);
    reset();
}

static void cached_strip_reference(void)
{
    recvxGameBootRenderGpuStripCacheEnable(1);
    const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3&&!f->vertex_count&&f->batch_count==1);
    RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
    RecvxBootRenderBatch batch=f->batches[0];
    recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(0);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batch_count==1&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
    CHECK(!memcmp(&batch.gpu_state,&f->batches[0].gpu_state,sizeof(batch.gpu_state))&&batch.gpu_mask==f->batches[0].gpu_mask);
    recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(1);
    draw();recvxGameBootRenderConsume(); /* Allow stable inputs to prime the snapshot. */
    RecvxBootRenderStats before=recvxGameBootRenderStats();
    unsigned int hits=before.gpu_strip_cache_hits;f=draw();
    CHECK(recvxGameBootRenderStats().gpu_strip_cache_hits==hits+1);
    CHECK(recvxGameBootRenderStats().position_transforms==before.position_transforms&&
        recvxGameBootRenderStats().normal_transforms==before.normal_transforms);
    CHECK(!memcmp(expected,f->gpu_vertices,sizeof(expected)));
    recvxGameBootRenderConsume();
}
static void gpu_strip_cache_tests(void)
{
    specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,10,200);
    cached_strip_reference();
    for(unsigned int change=0;change<9;++change){
        switch(change){
        case 0:putfloat(data.vertices+64,0.25f);break;
        case 1:putfloat(data.vertices+64+16,0.25f);break;
        case 2:put16(data.polygons+12+4+10,127);break; /* UV seam. */
        case 3:njTranslate(NULL,1,0,0);break;
        case 4:njScale(NULL,2,3,2);break;
        case 5:njCnkSetEasyMultiLightColor(2,4,2,0);break;
        case 6:njCnkSetEasyMultiLightPoint(2,0,1,0);break;
        case 7:njCnkSetEasyMultiLightRange(2,8,150);break;
        case 8:data.polygons[7]=128;break; /* Material alpha. */
        }
        unsigned int misses=recvxGameBootRenderStats().gpu_strip_cache_misses;
        cached_strip_reference();
        CHECK(recvxGameBootRenderStats().gpu_strip_cache_misses>misses);
    }
    reset();recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGpuStripCacheEnable(1);
    draw();recvxGameBootRenderConsume();
    unsigned int hits=recvxGameBootRenderStats().gpu_strip_cache_hits;
    njTranslate(NULL,1,2,3);const RecvxBootRenderFrame *moved=draw();
    CHECK(recvxGameBootRenderStats().gpu_strip_cache_hits==hits+1);
    CHECK(moved->batches[0].gpu_state.model[12]==1&&moved->batches[0].gpu_state.model[13]==-2);
    CHECK(moved->gpu_vertices[0].x==-1&&moved->gpu_vertices[0].z==4);
    reset();
}

static void gpu_light_response_tests(void)
{
    specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGpuStripCacheEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,10,200);
    draw();recvxGameBootRenderConsume();
    for(unsigned int change=0;change<12;++change){
        njCnkSetEasyMultiLightColor(2,2.5f+change*0.25f,0.5f+change*0.03f,0.125f);
        RecvxBootRenderStats before=recvxGameBootRenderStats();
        const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3&&!f->vertex_count);
        CHECK(recvxGameBootRenderStats().gpu_light_cache_hits==before.gpu_light_cache_hits+3);
        CHECK(recvxGameBootRenderStats().position_transforms==before.position_transforms&&
            recvxGameBootRenderStats().normal_transforms==before.normal_transforms);
        RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
        RecvxBootGpuState lighting=f->batches[0].gpu_state;
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(0);f=draw();
        CHECK(!memcmp(expected,f->gpu_vertices,sizeof(expected))&&
            !memcmp(&lighting,&f->batches[0].gpu_state,sizeof(lighting)));
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(1);
    }
    /* Source deformation and a moving light must rebuild cached distances. */
    for(unsigned int change=0;change<3;++change){
        if(change==0)putfloat(data.vertices+64,0.25f);
        if(change==1)njCnkSetEasyMultiLightPoint(2,1,0,0);
        if(change==2)njCnkSetEasyMultiLightRange(2,1.5f,200);
        RecvxBootRenderStats before=recvxGameBootRenderStats();
        const RecvxBootRenderFrame *f=draw();
        CHECK(recvxGameBootRenderStats().gpu_light_cache_misses+recvxGameBootRenderStats().gpu_light_cache_bypass>before.gpu_light_cache_misses+before.gpu_light_cache_bypass);
        RecvxBootGpuVertex expected[3];CHECK(f->gpu_vertex_count==3);memcpy(expected,f->gpu_vertices,sizeof(expected));
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(0);f=draw();
        CHECK(f->gpu_vertex_count==3&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(1);
    }
    /* An increasing gain can pull previously unlit vertices into its near
       correction sphere; response data must be filled lazily at that point. */
    njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightColor(2,2,0.5f,0);
    draw();recvxGameBootRenderConsume();
    draw();recvxGameBootRenderConsume(); /* Prime a newly stable basis. */
    RecvxBootRenderStats before=recvxGameBootRenderStats();
    njCnkSetEasyMultiLightColor(2,12,0.5f,0);
    const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3);
    CHECK(recvxGameBootRenderStats().gpu_light_cache_hits==before.gpu_light_cache_hits+3);
    CHECK(recvxGameBootRenderStats().normal_transforms>before.normal_transforms);
    RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
    recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(0);f=draw();
    CHECK(f->gpu_vertex_count==3&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
    reset();
}

static void gpu_resident_reference_tests(void)
{
    for(unsigned int variant=0;variant<32;++variant){
        specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);
        njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
        njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,1,0);
        njCnkSetEasyMultiLightRange(2,variant&1?10:1,variant&2?4.2f:200);
        if(variant&4)njRotateY(NULL,0x2300);
        if(variant&8)njScale(NULL,2,3,2);
        if(variant&16)njScale(NULL,-1,1,1);
        recvxGameBootRenderResidentEnable(0);
        const RecvxBootRenderFrame *f=draw();
        RecvxBootGpuVertex gpu[3];RecvxBootRenderVertex cpu[3];
        size_t gpu_count=f->gpu_vertex_count,cpu_count=f->vertex_count,batch_count=f->batch_count;
        CHECK(gpu_count<=3&&cpu_count<=3);
        if(gpu_count)memcpy(gpu,f->gpu_vertices,gpu_count*sizeof(*gpu));
        if(cpu_count)memcpy(cpu,f->vertices,cpu_count*sizeof(*cpu));
        CHECK(recvxGameBootRenderStats().gpu_vfpu_distances==0);
        recvxGameBootRenderConsume();recvxGameBootRenderResidentEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==gpu_count&&f->vertex_count==cpu_count&&f->batch_count==batch_count);
        CHECK(!gpu_count||!memcmp(gpu,f->gpu_vertices,gpu_count*sizeof(*gpu)));
        CHECK(!cpu_count||!memcmp(cpu,f->vertices,cpu_count*sizeof(*cpu)));
        CHECK(recvxGameBootRenderStats().gpu_vfpu_distances+recvxGameBootRenderStats().gpu_distance_batches>0);
    }
    reset();
}

static void gpu_distance_batch_tests(void)
{
    for(unsigned int variant=0;variant<32;++variant){
        specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);
        njCnkSetEasyMultiLight(5);njCnkSetEasyMultiLightSwitch(1,0);
        for(int i=2;i<=5;++i){
            njCnkSetEasyMultiLightSwitch(i,1);njCnkSetEasyMultiLightColor(i,2.5f+i,0.25f*i,0.125f);
            njCnkSetEasyMultiLightPoint(i,(i-2)*0.25f,0,0);
            njCnkSetEasyMultiLightRange(i,variant&1?10:1,variant&2?4.2f:200);
        }
        if(variant&4)njRotateY(NULL,0x2300);
        if(variant&8)njScale(NULL,2,3,2);
        if(variant&16){
            putfloat(data.vertices+64,0);putfloat(data.vertices+96,1000000);
            njCnkSetEasyMultiLightPoint(2,1000000.125f,0,3.5f);
        }
        recvxGameBootRenderDistanceBatchesEnable(0);const RecvxBootRenderFrame *f=draw();
        size_t gc=f->gpu_vertex_count,cc=f->vertex_count,bc=f->batch_count;
        CHECK(gc<=3&&cc<=3&&bc==1);
        RecvxBootGpuVertex gpu[3];RecvxBootRenderVertex cpu[3];RecvxBootRenderBatch batch=f->batches[0];
        if(gc)memcpy(gpu,f->gpu_vertices,gc*sizeof(*gpu));
        if(cc)memcpy(cpu,f->vertices,cc*sizeof(*cpu));
        unsigned int before=recvxGameBootRenderStats().gpu_vfpu_distances;
        CHECK(recvxGameBootRenderStats().gpu_distance_batches==0);
        recvxGameBootRenderConsume();recvxGameBootRenderDistanceBatchesEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==gc&&f->vertex_count==cc&&f->batch_count==bc);
        CHECK(!gc||!memcmp(gpu,f->gpu_vertices,gc*sizeof(*gpu)));
        CHECK(!cc||!memcmp(cpu,f->vertices,cc*sizeof(*cpu)));
        CHECK(!memcmp(&batch.gpu_state,&f->batches[0].gpu_state,sizeof(batch.gpu_state))&&batch.gpu_mask==f->batches[0].gpu_mask);
        CHECK(recvxGameBootRenderStats().gpu_distance_batches>0);
        if(!(variant&18))CHECK(recvxGameBootRenderStats().gpu_vfpu_distances-before<before);
        if((variant&28)==16)CHECK(recvxGameBootRenderStats().gpu_distance_refinements>0);
    }
    reset();
}

static void native_ge_lighting_tests(void)
{
    for(unsigned int variant=0;variant<64;++variant){
        unsigned int attributes=variant&1?0x20u:variant&2?2u:0u;
        specular_model(0x80b08040u,0x11ffffffu,attributes);
        recvxGameBootRenderGpuEnable(1);recvxGameBootRenderNativeLightingEnable(1);
        recvxGameBootRenderGpuStripCacheEnable(1);
        njCnkSetEasyMultiLight(7);njCnkSetEasyMultiLightSwitch(1,1);
        njCnkSetEasyMultiLightVector(0.25f,0.5f,-1);njCnkSetEasyMultiLightColor(1,0.5f,0.4f,0.3f);
        for(int i=2;i<=7;++i){
            njCnkSetEasyMultiLightSwitch(i,1);njCnkSetEasyMultiLightColor(i,i,0.5f,0.25f);
            njCnkSetEasyMultiLightPoint(i,i*0.25f,0,1);njCnkSetEasyMultiLightRange(i,10,variant&4?4.2f:200);
        }
        if(variant&8)njScale(NULL,-2,3,2);
        if(variant&16)njRotateY(NULL,0x2300);
        if(variant&32)putfloat(data.vertices+64+24,0);
        const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3&&!f->vertex_count&&f->batch_count==1);
        const RecvxBootGpuState *g=&f->batches[0].gpu_state;
        CHECK(g->lit&&g->native_lighting&&!g->vertex_ambient&&g->light_count==4);
        CHECK((g->diffuse>>24)==0x80&&f->batches[0].gpu_mask==15);
        CHECK(!g->lights[0].point);
        for(unsigned int i=1;i<4;++i){
            CHECK(g->lights[i].point&&g->lights[i].position[0]>=1.25f&&g->lights[i].position[0]<=1.75f);
            CHECK(isfinite(g->lights[i].quadratic)&&g->lights[i].quadratic>0);
        }
        if(!(attributes&3u))CHECK(g->specular&&g->shininess==17);
        RecvxBootRenderStats stats=recvxGameBootRenderStats();
        CHECK(stats.gpu_native_light_vertices==3&&stats.gpu_native_omitted_lights==3);
        CHECK(!stats.shaded_vertices&&!stats.position_transforms&&!stats.normal_transforms);
        CHECK(!stats.gpu_overflow_vertices&&!stats.gpu_classifications&&!stats.gpu_distance_batches&&!stats.gpu_vfpu_distances);
        CHECK(!stats.gpu_fallback_triangles&&!stats.gpu_strip_cache_hits&&!stats.gpu_strip_cache_misses);
        if(attributes&0x20u){
            float original[3][3]={{-1,-1,4},{0,1,4},{1,-1,4}},view[3][3];
            for(unsigned int i=0;i<3;++i)for(unsigned int k=0;k<3;++k)
                view[i][k]=g->model[k]*original[i][0]+g->model[4+k]*original[i][1]+g->model[8+k]*original[i][2]+g->model[12+k];
            float a[3],b[3];for(unsigned int k=0;k<3;++k){a[k]=view[1][k]-view[0][k];b[k]=view[2][k]-view[0][k];}
            float face[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
            const RecvxBootGpuVertex *p=&f->gpu_vertices[0];float n[3];
            for(unsigned int k=0;k<3;++k)n[k]=g->model[k]*p->nx+g->model[4+k]*p->ny+g->model[8+k]*p->nz;
            CHECK(n[0]*face[0]+n[1]*face[1]+n[2]*face[2]>0);
        }
        recvxGameBootRenderConsume();njCnkSetEasyMultiLightColor(7,1,1,1);draw();
        CHECK(!recvxGameBootRenderStats().gpu_overflow_vertices&&!recvxGameBootRenderStats().shaded_vertices);
    }
    /* All four public draw modes keep model lighting on GE. */
    for(unsigned int mode=0;mode<4;++mode){
        specular_model(0xffffffffu,0x11ffffffu,0);recvxGameBootRenderGpuEnable(1);recvxGameBootRenderNativeLightingEnable(1);
        njCnkSetCurrentDrawMode(mode);njCnkDrawModelLocal(&data.model);
        CHECK(recvxGameBootRenderFrame()->gpu_vertex_count==3&&!recvxGameBootRenderStats().shaded_vertices);
    }
    reset();
}

static void native_raw_reference_tests(void)
{
    for(unsigned int variant=0;variant<64;++variant){
        unsigned int attributes=variant&1?0x20u:0;
        specular_model(0xffb08040u,0x11ffffffu,attributes);
        recvxGameBootRenderGpuEnable(1);recvxGameBootRenderNativeLightingEnable(1);
        recvxGameBootRenderGroupCullEnable(1);
        if(variant&2)njScale(NULL,-2,3,2);
        if(variant&4)njRotateY(NULL,0x1300);
        if(variant&8)putfloat(data.vertices+64+24,0);
        if(variant&16){
            unsigned char source[96];memcpy(source,data.vertices+64,sizeof(source));
            data.vertices[0]=41;put16(data.vertices+2,19);
            for(unsigned int i=0;i<3;++i){
                memcpy(data.vertices+8+i*24,source+i*32,12);
                memcpy(data.vertices+20+i*24,source+i*32+16,12);
            }
        }
        if(variant&32)njTranslate(NULL,1,0,0);
        recvxGameBootRenderRawStripsEnable(0);const RecvxBootRenderFrame *f=draw();
        size_t count=f->gpu_vertex_count,batches=f->batch_count;
        CHECK(count<=3&&batches<=1&&!f->vertex_count);
        RecvxBootGpuVertex expected[3];RecvxBootRenderBatch batch={0};
        if(count)memcpy(expected,f->gpu_vertices,count*sizeof(*expected));
        if(batches)batch=f->batches[0];
        recvxGameBootRenderConsume();recvxGameBootRenderRawStripsEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==count&&f->batch_count==batches&&!f->vertex_count);
        CHECK(!count||!memcmp(expected,f->gpu_vertices,count*sizeof(*expected)));
        CHECK(!batches||(!memcmp(&batch.gpu_state,&f->batches[0].gpu_state,sizeof(batch.gpu_state))&&batch.gpu_mask==f->batches[0].gpu_mask));
        if(attributes&0x20u)CHECK(!recvxGameBootRenderStats().gpu_raw_vertices);
        if(!(attributes&0x20u)&&!(variant&8)&&count)CHECK(recvxGameBootRenderStats().gpu_raw_vertices>0);
    }
    reset();
}

static void native_ge_normal_preflight_tests(void)
{
    static const float values[]={0.0f,1e-30f,9e-11f,1e-10f,0.249999f,0.25f,1,2,2.0001f,1e19f,3e38f};
    for(unsigned int i=0;i<sizeof(values)/sizeof(*values);++i){
        specular_model(0xffb08040u,0x11ffffffu,0);
        recvxGameBootRenderGpuEnable(1);recvxGameBootRenderNativeLightingEnable(1);
        putfloat(data.vertices+64+24,values[i]);
        recvxGameBootRenderGpuStripsEnable(0);const RecvxBootRenderFrame *f=draw();
        CHECK(f->gpu_vertex_count==3&&!f->vertex_count);
        RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripsEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==3&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
        CHECK(!recvxGameBootRenderStats().gpu_fallback_triangles);
    }
    reset();
}

static void gpu_direct_overflow_tests(void)
{
    unsigned int fast=0;
    for(unsigned int variant=0;variant<64;++variant){
        specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);
        njCnkSetEasyMultiLight(5);njCnkSetEasyMultiLightSwitch(1,1);
        njCnkSetEasyMultiLightVector(0.25f,0.5f,-1);njCnkSetEasyMultiLightColor(1,8,2,3);
        for(int i=2;i<=5;++i){
            njCnkSetEasyMultiLightSwitch(i,variant&1);njCnkSetEasyMultiLightColor(i,i,0.5f,0.25f);
            njCnkSetEasyMultiLightPoint(i,(i-2)*0.25f,0,0);njCnkSetEasyMultiLightRange(i,variant&2?10:1,variant&4?4.2f:200);
        }
        if(variant&8)njScale(NULL,-2,3,2);
        if(variant&16)njRotateY(NULL,0x2300);
        if(variant&32)putfloat(data.vertices+64+24,0);
        recvxGameBootRenderOverflowFastEnable(0);const RecvxBootRenderFrame *f=draw();
        size_t gc=f->gpu_vertex_count,cc=f->vertex_count,bc=f->batch_count;
        CHECK(gc<=3&&cc<=3&&bc==1);
        RecvxBootGpuVertex gpu[3];RecvxBootRenderVertex cpu[3];RecvxBootRenderBatch batch=f->batches[0];
        if(gc)memcpy(gpu,f->gpu_vertices,gc*sizeof(*gpu));
        if(cc)memcpy(cpu,f->vertices,cc*sizeof(*cpu));
        CHECK(!recvxGameBootRenderStats().gpu_fast_overflow_vertices);
        recvxGameBootRenderConsume();recvxGameBootRenderOverflowFastEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==gc&&f->vertex_count==cc&&f->batch_count==bc);
        CHECK(!gc||!memcmp(gpu,f->gpu_vertices,gc*sizeof(*gpu)));
        CHECK(!cc||!memcmp(cpu,f->vertices,cc*sizeof(*cpu)));
        CHECK(!memcmp(&batch.gpu_state,&f->batches[0].gpu_state,sizeof(batch.gpu_state))&&batch.gpu_mask==f->batches[0].gpu_mask);
        fast+=recvxGameBootRenderStats().gpu_fast_overflow_vertices;
    }
    CHECK(fast>0);reset();
}
static void gpu_vertex_bounds_tests(void)
{
    RecvxGameChunkVertex source[70];
    for(unsigned int i=0;i<70;++i)source[i]=(RecvxGameChunkVertex){.x=(float)(i%11)-5,.y=(float)(i%17)-8,.z=(float)(i%7)-3,.nx=NAN,.ny=NAN,.nz=NAN,.color=0xffffffffu};
    float lo[3]={11,22,33},hi[3]={44,55,66};recvxRenderResidentBounds(&source[0].x,0,sizeof(source[0]),lo,hi);
    CHECK(lo[0]==11&&hi[2]==66);
    for(unsigned int count=1;count<=70;++count){
        recvxRenderResidentBounds(&source[0].x,count,sizeof(source[0]),lo,hi);
        float lower[3]={source[0].x,source[0].y,source[0].z},upper[3]={source[0].x,source[0].y,source[0].z};
        for(unsigned int i=1;i<count;++i){float xyz[3]={source[i].x,source[i].y,source[i].z};
            for(unsigned int k=0;k<3;++k){if(xyz[k]<lower[k])lower[k]=xyz[k];if(xyz[k]>upper[k])upper[k]=xyz[k];}}
        for(unsigned int k=0;k<3;++k)CHECK(lo[k]==lower[k]&&hi[k]==upper[k]);
    }
}

static void native_model_bounds_tests(void)
{
    for(unsigned int variant=0;variant<4;++variant){
        reset();recvxGameBootRenderGpuEnable(1);recvxGameBootRenderNativeLightingEnable(1);
        recvxGameBootRenderGroupCullEnable(1);recvxGameBootRenderResidentEnable(!(variant&1));
        if(variant&2){
            unsigned char source[96];memcpy(source,data.vertices+64,sizeof(source));
            data.vertices[0]=41;put16(data.vertices+2,19);
            for(unsigned int i=0;i<3;++i){
                memcpy(data.vertices+8+i*24,source+i*32,12);
                memcpy(data.vertices+20+i*24,source+i*32+16,12);
            }
        }
        unsigned int cursor=0;
        for(unsigned int i=0;i<4;++i){
            unsigned char *p=data.polygons+cursor;p[0]=64;p[1]=3;
            put16(p+2,5);put16(p+4,1);put16(p+6,3);
            for(unsigned int j=0;j<3;++j)put16(p+8+j*2,j);
            cursor+=14;
        }
        put16(data.polygons+cursor,255);
        const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==12);
        RecvxBootRenderStats stats=recvxGameBootRenderStats();
        CHECK(stats.gpu_model_bounds_inside>1&&!stats.gpu_model_bounds_outside&&!stats.gpu_culled_groups);
        if(!(variant&1))CHECK(stats.gpu_vfpu_bound_vertices==3);
        recvxGameBootRenderConsume();njTranslate(NULL,10000,0,0);f=draw();
        stats=recvxGameBootRenderStats();
        CHECK(!f->gpu_vertex_count&&stats.gpu_model_bounds_outside>1);
        recvxGameBootRenderConsume();njTranslate(NULL,-10000,0,0);f=draw();
        CHECK(f->gpu_vertex_count==12);
    }
    reset();
}

static void gpu_group_cull_tests(int native_lighting)
{
    /* Fully outside each of the six clip planes, with deliberately stale
       authored bounds. The decoded vertices determine rejection. */
    const float offsets[6][3]={{-40,0,0},{40,0,0},{0,-40,0},{0,40,0},{0,0,-8},{0,0,150}};
    for(unsigned int variant=0;variant<24;++variant){
        reset();recvxGameBootRenderNativeLightingEnable(native_lighting);recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGroupCullEnable(1);
        if(variant&1){
            njScale(NULL,-1,2,1);data.polygons[1]=2;
            njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
            njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,10,200);
        }
        if(variant&2)njRotateZ(NULL,0x1000);
        unsigned int side=variant/4;
        /* Translate after the local transform so these offsets are in view space. */
        NJS_MATRIX m;njGetMatrix(&m);m[12]=offsets[side][0];m[13]=offsets[side][1];m[14]=offsets[side][2];njSetMatrix(NULL,&m);
        const RecvxBootRenderFrame *f=draw();
        CHECK(!f->gpu_vertex_count&&!f->vertex_count&&!f->batch_count);
        CHECK(recvxGameBootRenderStats().gpu_culled_groups==1&&recvxGameBootRenderStats().gpu_culled_triangles==1);
        CHECK(recvxGameBootRenderStats().gpu_vertex_calls==0&&recvxGameBootRenderStats().gpu_overflow_vertices==0);
    }
    /* Every partially visible group stays with GE, including the near plane,
       camera plane, far plane, and a triangle enclosing the full viewport. */
    const float retained[][3][3]={
        {{-1,-1,4},{0,1,4},{1,-1,4}},
        {{-40,-1,4},{0,1,4},{1,-1,4}},
        {{-1,-1,0.25f},{0,1,4},{1,-1,4}},
        {{-1,-1,-4},{0,1,4},{1,-1,4}},
        {{-1,-1,150},{0,1,4},{1,-1,4}},
        {{-100,-100,4},{100,-100,4},{0,100,4}}
    };
    for(unsigned int variant=0;variant<sizeof(retained)/sizeof(retained[0]);++variant){
        reset();recvxGameBootRenderNativeLightingEnable(native_lighting);clip_model_vertices(retained[variant],3);recvxGameBootRenderGpuEnable(1);
        const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3);
        RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
        recvxGameBootRenderConsume();recvxGameBootRenderGroupCullEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==3&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
        CHECK(recvxGameBootRenderStats().gpu_culled_groups==0);
    }
    /* Inventory-style off-center projection, and a live-deformed vertex that
       reaches the viewport despite the original off-screen model sphere. */
    reset();recvxGameBootRenderNativeLightingEnable(native_lighting);recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGroupCullEnable(1);
    NJS_SCREEN screen={.dist=500,.w=352,.h=184,.cx=235,.cy=224};recvxEngineOriginalViewSetScreen(&screen);
    const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3);
    recvxGameBootRenderConsume();njTranslate(NULL,40,0,0);draw();
    CHECK(recvxGameBootRenderStats().gpu_culled_groups==1);
    recvxGameBootRenderConsume();putfloat(data.vertices+64,-40);f=draw();
    CHECK(f->gpu_vertex_count==3&&recvxGameBootRenderStats().gpu_culled_groups==1);
    reset();recvxGameBootRenderNativeLightingEnable(native_lighting);
}

static void gpu_box_interval_tests(void)
{
    for(unsigned int variant=0;variant<64;++variant){
        float m[9]={1+(variant&3),0.25f,-0.5f,-0.75f,2,0.125f,0.5f,-0.25f,0.5f+(variant>>2)};
        if(variant&1)for(unsigned int k=0;k<3;++k)m[k]=-m[k];
        float q[6]={0};
        for(unsigned int i=0;i<3;++i)for(unsigned int k=0;k<3;++k)q[i]+=m[i*3+k]*m[i*3+k];
        unsigned int index=3;
        for(unsigned int i=0;i<3;++i)for(unsigned int j=i+1;j<3;++j){
            for(unsigned int k=0;k<3;++k)q[index]+=2*m[i*3+k]*m[j*3+k];
            ++index;
        }
        float center[3]={variant*0.25f-4,2,-3},light[3]={5,-7,variant*0.125f};
        float e[3]={0.25f+variant%3,0.5f,1.25f},range[2];
        recvxRenderResidentBoxDistance(q,center,light,e,range);
        float radius2=q[0]*e[0]*e[0]+q[1]*e[1]*e[1]+q[2]*e[2]*e[2]+
            fabsf(q[3]*e[0]*e[1])+fabsf(q[4]*e[0]*e[2])+fabsf(q[5]*e[1]*e[2]);
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
            float delta[3]={center[0]+x*e[0]-light[0],center[1]+y*e[1]-light[1],center[2]+z*e[2]-light[2]};
            float distance=0;
            for(unsigned int k=0;k<3;++k){
                float world=m[k]*delta[0]+m[3+k]*delta[1]+m[6+k]*delta[2];distance+=world*world;
            }
            CHECK(distance>=range[0]-0.001f&&distance<=range[1]+radius2+0.001f);
        }
    }
}
static void gpu_group_color_reuse_tests(void)
{
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(1,0);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,10,200);
    put16(data.polygons+2,26);put16(data.polygons+6,24);
    for(unsigned int i=0;i<24;++i)put16(data.polygons+8+2*i,i%3);
    put16(data.polygons+56,255);
    const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==66);
    RecvxBootGpuVertex expected[66];memcpy(expected,f->gpu_vertices,sizeof(expected));
    recvxGameBootRenderConsume();recvxGameBootRenderGroupBoundsEnable(1);f=draw();
    CHECK(f->gpu_vertex_count==66&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
    CHECK(recvxGameBootRenderStats().gpu_group_reused_vertices==21);
    CHECK(recvxGameBootRenderStats().gpu_overflow_vertices==6);
    reset();
}

static void gpu_group_bounds_tests(void)
{
    unsigned int classified=0,constant=0,metric_skips=0;
    for(unsigned int variant=0;variant<256;++variant){
        specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);
        njCnkSetEasyMultiLight(5);njCnkSetEasyMultiLightSwitch(1,0);
        for(int i=2;i<=5;++i){
            njCnkSetEasyMultiLightSwitch(i,1);njCnkSetEasyMultiLightColor(i,2.5f+i,0.25f*i,0.125f);
            njCnkSetEasyMultiLightPoint(i,(i-2)*0.25f,0,variant&32?100:0);
            njCnkSetEasyMultiLightRange(i,variant&1?10:1,variant&2?4.2f:200);
        }
        if(variant&4)njRotateY(NULL,0x2300);
        if(variant&8)njScale(NULL,2,3,2);
        if(variant&16)njScale(NULL,-1,1,1);
        if(variant&64){
            putfloat(data.vertices+64,0);putfloat(data.vertices+96,1000000);
            njCnkSetEasyMultiLightPoint(2,1000000.125f,0,3.5f);
        }
        if(variant&128)putfloat(data.vertices+64+24,0); /* Invalid normal preserves face fallback. */
        recvxGameBootRenderOverflowFastEnable(0);recvxGameBootRenderGroupBoundsEnable(0);const RecvxBootRenderFrame *f=draw();
        size_t gc=f->gpu_vertex_count,cc=f->vertex_count,bc=f->batch_count;
        CHECK(gc<=3&&cc<=3&&bc==1);
        RecvxBootGpuVertex gpu[3];RecvxBootRenderVertex cpu[3];RecvxBootRenderBatch batch=f->batches[0];
        if(gc)memcpy(gpu,f->gpu_vertices,gc*sizeof(*gpu));
        if(cc)memcpy(cpu,f->vertices,cc*sizeof(*cpu));
        recvxGameBootRenderConsume();recvxGameBootRenderOverflowFastEnable(1);recvxGameBootRenderResidentEnable(!(variant&4));recvxGameBootRenderGroupBoundsEnable(1);f=draw();
        CHECK(f->gpu_vertex_count==gc&&f->vertex_count==cc&&f->batch_count==bc);
        CHECK(!gc||!memcmp(gpu,f->gpu_vertices,gc*sizeof(*gpu)));
        CHECK(!cc||!memcmp(cpu,f->vertices,cc*sizeof(*cpu)));
        CHECK(!memcmp(&batch.gpu_state,&f->batches[0].gpu_state,sizeof(batch.gpu_state))&&batch.gpu_mask==f->batches[0].gpu_mask);
        classified+=recvxGameBootRenderStats().gpu_group_vertices;
        constant+=recvxGameBootRenderStats().gpu_group_constant_vertices;
        metric_skips+=recvxGameBootRenderStats().gpu_group_metric_skips;
    }
    CHECK(classified>0&&constant>0&&metric_skips>0);
    /* A bright point light encloses this group. Preserve the complete spill
       response while eliminating every vertex's metric precheck. */
    specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(1,0);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,10,200);
    recvxGameBootRenderDistanceBatchesEnable(0);recvxGameBootRenderGroupBoundsEnable(0);
    const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3);
    RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
    unsigned int distances=recvxGameBootRenderStats().gpu_vfpu_distances;
    recvxGameBootRenderConsume();recvxGameBootRenderGroupBoundsEnable(1);f=draw();
    CHECK(f->gpu_vertex_count==3&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
    CHECK(recvxGameBootRenderStats().gpu_group_metric_skips==3);
    CHECK(recvxGameBootRenderStats().gpu_vfpu_distances-distances<distances);
    CHECK(recvxGameBootRenderStats().gpu_group_range_queries==1);
    reset();
}

static void gpu_volatile_cache_tests(void)
{
    specular_model(0xffb08040u,0,2);recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGpuStripCacheEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,5,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,10,200);
    for(unsigned int frame=0;frame<8;++frame){
        njTranslate(NULL,0.25f,0,0);
        const RecvxBootRenderFrame *f=draw();CHECK(f->gpu_vertex_count==3&&!f->vertex_count);
        RecvxBootGpuVertex expected[3];memcpy(expected,f->gpu_vertices,sizeof(expected));
        RecvxBootGpuState lighting=f->batches[0].gpu_state;
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(0);f=draw();
        CHECK(f->gpu_vertex_count==3&&!memcmp(expected,f->gpu_vertices,sizeof(expected)));
        CHECK(!memcmp(&lighting,&f->batches[0].gpu_state,sizeof(lighting)));
        recvxGameBootRenderConsume();recvxGameBootRenderGpuStripCacheEnable(1);
    }
    CHECK(recvxGameBootRenderStats().gpu_model_cache_bypass>=5);
    unsigned int bypass=recvxGameBootRenderStats().gpu_model_cache_bypass;
    unsigned int hits=recvxGameBootRenderStats().gpu_strip_cache_hits;
    draw();recvxGameBootRenderConsume();draw();recvxGameBootRenderConsume();draw();
    CHECK(recvxGameBootRenderStats().gpu_model_cache_bypass==bypass);
    CHECK(recvxGameBootRenderStats().gpu_strip_cache_hits>hits);
    reset();recvxGameBootRenderGpuEnable(1);recvxGameBootRenderGpuStripCacheEnable(1);
    for(unsigned int frame=0;frame<8;++frame){njTranslate(NULL,0.25f,0,0);draw();recvxGameBootRenderConsume();}
    CHECK(recvxGameBootRenderStats().gpu_model_cache_bypass==0&&recvxGameBootRenderStats().gpu_strip_cache_hits>=6);
    reset();
}

static void gpu_render_tests(void)
{
    const RecvxBootRenderFrame *f;
    reset();recvxGameBootRenderGpuEnable(1);njTranslate(NULL,3,2,1);f=draw();
    CHECK(f->vertex_count==0&&f->gpu_vertex_count==3&&f->batch_count==1&&recvxGameBootRenderPending());
    CHECK(f->batches[0].gpu&&!f->batches[0].gpu_state.lit);
    CHECK(f->gpu_vertices[0].x==-1&&f->gpu_vertices[0].z==4);
    CHECK(recvxGameBootRenderStats().gpu_direct_vertices==3);
    CHECK(f->batches[0].gpu_state.model[12]==3&&f->batches[0].gpu_state.model[13]==-2&&f->batches[0].gpu_state.model[14]==-1);
    CHECK(f->batches[0].gpu_state.model[5]==-1&&f->batches[0].gpu_state.model[10]==-1);
    CHECK(recvxGameBootRenderStats().position_transforms==0&&recvxGameBootRenderStats().shaded_vertices==0);
    recvxGameBootRenderGpuEnable(0);draw();f=recvxGameBootRenderFrame();
    CHECK(f->batch_count==2&&!f->batches[1].gpu&&f->vertex_count==3&&f->gpu_vertex_count==3);
    recvxGameBootRenderConsume();CHECK(!recvxGameBootRenderPending());
    reset();recvxGameBootRenderGpuEnable(1);data.polygons[1]=2;
    njCnkSetEasyMultiAmbient(0.25f,0.5f,1);njCnkSetEasyMultiLight(1);njCnkSetEasyMultiLightSwitch(1,1);
    njCnkSetEasyMultiLightColor(1,1,0,0);njCnkSetEasyMultiLightVector(0,0,1);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batches[0].gpu_state.lit&&f->batches[0].gpu_mask==1);
    CHECK(f->batches[0].gpu_state.ambient==0xffff8040u&&f->batches[0].gpu_state.lights[0].color==0xff0000ffu);
    CHECK(f->batches[0].gpu_state.lights[0].position[2]==1);
    CHECK(recvxGameBootRenderStats().gpu_lit_vertices==3&&recvxGameBootRenderStats().normal_transforms==0);
    reset();recvxGameBootRenderGpuEnable(1);data.polygons[1]=2;
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightColor(2,0,1,0);njCnkSetEasyMultiLightPoint(2,0,0,0);
    njCnkSetEasyMultiLightRange(2,5,20);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batches[0].gpu_mask==1&&f->batches[0].gpu_state.lights[0].point);
    CHECK(close_to(f->batches[0].gpu_state.lights[0].quadratic,0.04f));
    recvxGameBootRenderConsume();njCnkSetEasyMultiLightRange(2,1,2);f=draw();
    CHECK(f->gpu_vertex_count==3&&!f->batches[0].gpu_mask);
    recvxGameBootRenderConsume();njCnkSetEasyMultiLightRange(2,1,4.2f);f=draw();
    CHECK(f->vertex_count==3&&!f->gpu_vertex_count&&recvxGameBootRenderStats().gpu_fallback_triangles==1);
    reset();recvxGameBootRenderGpuEnable(1);data.polygons[1]=2;njScale(NULL,2,3,1);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batches[0].gpu_state.lit); /* Corrected inverse-transpose normals. */
    reset();recvxGameBootRenderGpuEnable(1);data.polygons[1]=0x22;f=draw();
    CHECK(f->vertex_count==3&&!f->gpu_vertex_count); /* Retail face normals. */
    reset();recvxGameBootRenderGpuEnable(1);data.polygons[1]=2;
    putfloat(data.vertices+64+24,0);f=draw();CHECK(f->vertex_count==3&&!f->gpu_vertex_count);
    specular_model(0x80ffffff,0x11ffffff,0);recvxGameBootRenderGpuEnable(1);f=draw();
    CHECK(f->vertex_count==3&&!f->gpu_vertex_count); /* Retail specular equation. */
    reset();recvxGameBootRenderGpuEnable(1);_Make_ClipVolume(160,240);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batches[0].scissor[0]==99&&f->batches[0].scissor[2]==381);
    CHECK(!recvxGameBootRenderStats().clip_intersections&&!recvxGameBootRenderStats().position_transforms);
    const float crossing[3][3]={{-4,0,4},{0,1,4},{0,-1,4}};
    reset();clip_model_vertices(crossing,3);recvxGameBootRenderGpuEnable(1);_Make_ClipVolume(160,240);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->gpu_vertices[0].x==-4&&f->batches[0].scissor[0]==99);
    CHECK(!recvxGameBootRenderStats().clip_intersections);
    const float near_crossing[3][3]={{-1,-1,0.5f},{0,1,4},{1,-1,4}};
    reset();clip_model_vertices(near_crossing,3);recvxGameBootRenderGpuEnable(1);_Make_ClipVolume(1024,960);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->gpu_vertices[0].z==0.5f);
    specular_model(0x80ffffff,0x11ffffff,0);recvxGameBootRenderGpuEnable(1);f=draw();
    CHECK(f->batches[0].gpu==2&&f->vertices[0].z==4&&f->batches[0].gpu_state.model[10]==-1);
    CHECK(recvxGameBootRenderStats().gpu_colored_vertices==3);
    reset();recvxGameBootRenderGpuEnable(1);data.polygons[1]=0x22;f=draw();
    CHECK(f->batches[0].gpu==2&&f->vertex_count==3);
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,5,20);
    njCnkSetEasyMultiLightColor(2,1.5f,0.25f,0.75f);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batches[0].gpu_state.light_count==1&&f->batches[0].gpu_mask==1);
    CHECK(f->batches[0].gpu_state.lights[0].color==0xff802bffu&&f->batches[0].gpu_state.vertex_ambient);
    CHECK(close_to(f->batches[0].gpu_state.lights[0].quadratic,0.04f/1.5f));
    recvxGameBootRenderConsume();njCnkSetEasyMultiLightRange(2,1,4.2f);f=draw();
    CHECK(f->batches[0].gpu==2&&recvxGameBootRenderStats().gpu_fallback[6]==1); /* Both copies retain far-cutoff fallback. */
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiAmbient(2,1.25f,1);njCnkSetEasyMultiLight(1);njCnkSetEasyMultiLightSwitch(1,1);
    njCnkSetEasyMultiLightColor(1,3,3,3);njCnkSetEasyMultiLightVector(0,0,1);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batches[0].gpu_state.light_count==0&&f->batches[0].gpu_state.ambient==~0u);
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(1);njCnkSetEasyMultiLightSwitch(1,1);njCnkSetEasyMultiLightColor(1,4.5f,1,1);
    njCnkSetEasyMultiLightVector(0,0,1);f=draw();
    CHECK(f->batches[0].gpu==1&&f->batches[0].gpu_state.vertex_ambient&&recvxGameBootRenderStats().gpu_overflow_vertices==3); /* Excess energy uses vertex ambient, with GE lighting retained. */
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(2);njCnkSetEasyMultiLightSwitch(2,1);
    njCnkSetEasyMultiLightPoint(2,0,0,0);njCnkSetEasyMultiLightRange(2,1,20);
    njCnkSetEasyMultiLightColor(2,5,1,0);f=draw();
    CHECK(f->gpu_vertex_count==3&&f->batches[0].gpu_state.light_count==1);
    CHECK(recvxGameBootRenderStats().position_transforms==0&&recvxGameBootRenderStats().normal_transforms==0);
    CHECK(f->gpu_vertices[0].color==0xff000000u); /* Entire light contribution stays on GE beyond the scaled near sphere. */
    CHECK(close_to(f->batches[0].gpu_state.lights[0].quadratic,0.2f));
    /* The hardware case (5,1,0) keeps GE shading and caches only overflow.
       Reusing the mesh in a new draw must recompute its ambient contribution. */
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiLight(1);njCnkSetEasyMultiLightSwitch(1,1);
    njCnkSetEasyMultiLightColor(1,5,1,0);njCnkSetEasyMultiLightVector(0,0,1);f=draw();
    CHECK(f->batches[0].gpu==1&&f->batches[0].gpu_state.light_count==4&&f->batches[0].gpu_state.vertex_ambient);
    CHECK(f->gpu_vertices[0].color==0xff0000ffu&&f->gpu_vertices[0].z==4);
    CHECK(recvxGameBootRenderStats().position_transforms==0&&recvxGameBootRenderStats().shaded_vertices==0);
    CHECK(recvxGameBootRenderStats().gpu_overflow_vertices==3);
    draw();CHECK(recvxGameBootRenderStats().gpu_overflow_vertices==6);
    reset();data.polygons[1]=2;recvxGameBootRenderGpuEnable(1);
    njCnkSetEasyMultiAmbient(-0.1f,0.25f,0.5f);f=draw();
    CHECK(f->batches[0].gpu==2&&recvxGameBootRenderStats().gpu_fallback[3]==1); /* Signed light math is preserved. */
    reset();recvxGameBootRenderGpuEnable(1);njScale(NULL,-1,1,1);f=draw();
    CHECK(f->gpu_vertices[1].x==1&&f->gpu_vertices[2].x==0); /* Preserve outward winding. */
    reset();
}

int main(void)
{
    gpu_compare_reference();gpu_render_tests();gpu_shared_light_classifications();gpu_prelit_queue_transition();gpu_bulk_reference_tests();gpu_native_strip_tests(0);gpu_native_strip_tests(1);gpu_strip_cache_tests();gpu_light_response_tests();gpu_resident_reference_tests();gpu_distance_batch_tests();native_ge_lighting_tests();native_raw_reference_tests();native_ge_normal_preflight_tests();gpu_direct_overflow_tests();gpu_vertex_bounds_tests();native_model_bounds_tests();gpu_group_cull_tests(0);gpu_group_cull_tests(1);gpu_box_interval_tests();gpu_group_color_reuse_tests();gpu_group_bounds_tests();gpu_volatile_cache_tests();benchmark_strip();
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
