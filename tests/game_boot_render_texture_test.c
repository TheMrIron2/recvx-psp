#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "ps2/veronica/prog/ps2_dummy.h"

static SYS_WORK work;
SYS_WORK *sys = &work;
static jmp_buf fault;
static const char *fault_name;
static int expecting_fault;
unsigned int palbuf[4096];
static RecvxBootTexture snapshots[8];
static size_t snapshot_count;
static int render_pending;
void recvxGameBootLog(const char *format, ...) { (void)format; }
void recvxGameBootPresent(void) { assert(!"texture test must not submit draws"); }
int recvxGameBootRenderPending(void) { return render_pending; }
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *texture)
{
    assert(snapshot_count < sizeof(snapshots)/sizeof(snapshots[0]));
    snapshots[snapshot_count] = *texture;
    return &snapshots[snapshot_count++];
}
void recvxGameBootUnsupported(const char *name)
{
    if (!expecting_fault) { fprintf(stderr, "unexpected render-state fault: %s\n", name); abort(); }
    fault_name = name; longjmp(fault, 1);
}
#define REJECT(call, message) do { \
    fault_name = NULL; expecting_fault = 1; \
    if (!setjmp(fault)) { call; assert(!"expected render-state fault"); } \
    expecting_fault = 0; assert(fault_name && strstr(fault_name, message)); \
} while (0)

static void put16(unsigned char *p, unsigned int n) { p[0] = n; p[1] = n >> 8; }
static void put32(unsigned char *p, unsigned int n)
{ p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24; }

static unsigned char *texture_data(unsigned int width, unsigned int type, size_t *size)
{
    unsigned int colors = type == 4 ? 16 : 256, image = width * 8 / (type == 4 ? 2 : 1);
    unsigned int total = 48 + image + colors * 4;
    unsigned char *data = calloc(1, 16 + total), *p;
    assert(data); *size = 16 + total;
    memcpy(data, "TIM2", 4); data[4] = 4; put16(data + 6, 1);
    p = data + 16; put32(p, total); put32(p + 4, colors * 4); put32(p + 8, image);
    put16(p + 12, 48); put16(p + 14, colors); p[17] = 1; p[18] = 3; p[19] = type;
    put16(p + 20, width); put16(p + 22, 8);
    memset(p + 48, 0x22, image);
    for (unsigned int i = 0; i < colors; ++i) put32(p + 48 + image + i * 4, 0x80000000u | i);
    return data;
}

static void texture_resolution(void)
{
    NJS_TEXNAME names[3] = {{0}};
    NJS_TEXLIST list = {.textures = names, .nbTexture = 3};
    const RecvxBootTexture *texture, *other;
    unsigned char *bytes;
    size_t size;
    recvxGameBootRenderTextureReset();
    bytes = texture_data(16, 4, &size);
    recvxGameBootTextureInstall(&list, 0, bytes, size, 0, 100, 0, 0);
    recvxGameBootRenderTextureList(&list);
    texture = recvxGameBootRenderTextureSelect(NULL, 0);
    assert(texture->width == 16 && texture->height == 8 && texture->tile_count == 1);
    assert(texture->palette[2] == 0xff000002u && texture->source != bytes && texture->pixels[0] == 2);
    memset(bytes, 0, size); free(bytes);
    assert(!memcmp(texture->source, "TIM2", 4));
    bytes = texture_data(1024, 5, &size);
    recvxGameBootTextureInstall(&list, 1, bytes, size, 0, 101, 0, 0); free(bytes);
    other = recvxGameBootRenderTextureSelect(&list, 1);
    assert(other->width == 1024 && other->tile_width == 512 && other->tile_count == 2);
    assert(texture != other && recvxGameBootRenderTextureCurrentList() == &list);
    njSetTexture(&list); njSetTextureNum(0);
    assert(recvxGameBootRenderStateGet()->selected_texture == texture);
    njSetTextureNumG(101);
    assert(recvxGameBootRenderStateGet()->selected_texture == other);
    REJECT(njSetTextureNumG(99999), "global texture index");
    njColorBlendingMode(0, 11); njColorBlendingMode(1, 2);
    assert(recvxGameBootRenderStateGet()->polygon_blend_source == 11 &&
        recvxGameBootRenderStateGet()->polygon_blend_destination == 2);
    njColorBlendingMode(0, 8); njColorBlendingMode(1, 6);
    REJECT(recvxGameBootRenderTextureSelect(NULL, 3), "texture exceeds");
    REJECT(recvxGameBootRenderTextureSelect(NULL, 2), "texture descriptor");
    recvxGameBootTexturesShutdown();
    REJECT(recvxGameBootRenderTextureSelect(&list, 0), "valid owned texture");
    recvxGameBootRenderTextureList(NULL);
    REJECT(recvxGameBootRenderTextureSelect(NULL, 0), "texture exceeds");
    list.nbTexture = 257; REJECT(recvxGameBootRenderTextureList(&list), "render texture list");
}

static void original_render_controls(void)
{
    const RecvxGameBootRenderState *state;
    NJS_ARGB material = {.a = 0.5f, .r = 0.2f, .g = 0.3f, .b = 0.4f};
    recvxGameBootRenderTextureReset(); state = recvxGameBootRenderStateGet();
    assert(state->constant_material.a == 1 && fNaCnkConstantA == 1 && state->control_flags == 0);
    njControl3D(0x4910); assert(_nj_control_3d_flag_ == 0x4910 && Ps2_njControl3D_flag == 0x4910);
    assert(Ps2_use_pt_flag == 0x4000 && state->control_flags == 0x4910);
    njSetConstantAttr(0, 0x1a00); assert(state->constant_attr == 0x1a);
    assert(recvxGameBootRenderStripAttributes(0x61) == 0x1a);
    njSetConstantAttr(0xf500, 0x0100); assert(state->constant_attr == 0x11);
    njControl3D(0); assert(recvxGameBootRenderStripAttributes(0x61) == 0x61);
    njSetConstantMaterial(&material);
    assert(state->constant_material.a == 0.5f && fNaCnkConstantR == 0.2f && fNaCnkConstantB == 0.4f);
    njCnkSetEasyMultiAmbient(0.1f, 0.2f, 0.3f); njCnkSetSimpleMultiAmbient(0.4f, 0.5f, 0.6f);
    assert(state->easy_ambient[2] == 0.3f && state->simple_ambient[0] == 0.4f);
    for (unsigned int mode = 0; mode < 4; ++mode) {
        njCnkSetCurrentDrawMode(mode); assert(state->current_draw_mode == mode);
    }
    njSetCheapShadowMode(0x80); assert(state->cheap_shadow_mode == 0x80);
    njSetCnkBlendMode(0x25); assert(state->blend_source == 8 && state->blend_destination == 6);
    njSetCnkBlendMode(0); assert(state->blend_source == 11 && state->blend_destination == 11);
    REJECT(njCnkSetCurrentDrawMode(4), "chunk draw mode");
    REJECT(recvxGameBootRenderStripAttributes(256), "strip attributes");
    material.a = NAN; REJECT(njSetConstantMaterial(&material), "constant material");
    REJECT(njCnkSetEasyMultiAmbient(0, INFINITY, 0), "easy ambient");
    REJECT(njCnkSetSimpleMultiAmbient(0, 0, NAN), "simple ambient");
}

static void queued_palette_changes(void)
{
    NJS_TEXNAME name = {0};
    NJS_TEXLIST list = {.textures=&name,.nbTexture=1};
    QUAD quad = {.x2=16,.y2=8,.u2=1,.v2=1};
    size_t bytes, count;
    unsigned char *data=texture_data(16,4,&bytes);
    recvxGameBootRenderTextureReset();
    recvxGameBootTextureInstall(&list,0,data,bytes,0,102,0,0);free(data);
    njSetTexture(&list);njSetQuadTexture(0,0xffffffffu);
    const RecvxBootTexture *live=recvxGameBootRenderTextureSelect(NULL,0);
    for(unsigned int i=0;i<4096;++i)palbuf[i]=0xff000000u|i;
    snapshot_count=0;
    njDrawQuadTexture(&quad,1);
    njSetPaletteBankNum(0,1);
    assert(snapshot_count==1&&snapshots[0].palette[2]==0xff000002u&&live->palette[2]==0xff000012u);
    njDrawQuadTexture(&quad,2);njSetPaletteBankNum(0,2);njDrawQuadTexture(&quad,3);
    assert(snapshot_count==2&&snapshots[1].palette[2]==0xff000012u&&live->palette[2]==0xff000022u);
    const RecvxBootDraw *queued=recvxGameBootDraws(&count);
    assert(count==3&&queued[0].texture==&snapshots[0]&&queued[1].texture==&snapshots[1]&&queued[2].texture==live);
    for(unsigned int i=0;i<2;++i)
        assert(snapshots[i].pixels==live->pixels&&snapshots[i].source==live->source);
    render_pending=1;njSetPaletteBankNum(0,3);
    assert(snapshot_count==3&&snapshots[2].palette[2]==0xff000022u&&live->palette[2]==0xff000032u);
    REJECT(njSetPaletteBankNum(0,256),"palette bank selection");assert(snapshot_count==3);
    render_pending=0;njSetPaletteBankNum(0,4);assert(snapshot_count==3);
    njDrawQuadTexture(&quad,1); njSetPaletteBankNum(0,5);
    assert(snapshot_count==4);
    recvxGameBootDrawDiscard();
    recvxGameBootTexturesShutdown(); /* No partial-frame Present may dereference a discarded view. */
    recvxGameBootDraws(&count); assert(count==0);
}

static void sampler_bits(void)
{
    RecvxGameBootSampler sampler;
    recvxGameBootRenderTextureReset();
    recvxGameBootRenderTextureSampler(0x4000, 0x50, &sampler);
    assert(sampler.filter == 1 && !sampler.clamp_u && sampler.clamp_v && !sampler.mirror_u && sampler.mirror_v);
    recvxGameBootRenderTextureSampler(0xe000, 0xa0, &sampler);
    assert(sampler.filter == 1 && sampler.clamp_u && !sampler.clamp_v && sampler.mirror_u && !sampler.mirror_v);
    recvxGameBootRenderTextureSampler(0xc000, 0, &sampler); assert(sampler.filter == 3);
    recvxGameBootRenderTextureFilter(2);
    recvxGameBootRenderTextureSampler(0x2000, 0xf0, &sampler);
    assert(sampler.filter == 2 && sampler.clamp_u && sampler.clamp_v && sampler.mirror_u && sampler.mirror_v);
    REJECT(recvxGameBootRenderTextureFilter(4), "texture filter");
    REJECT(recvxGameBootRenderTextureSampler(0, 0, NULL), "sampler output");
}

int main(void)
{
    texture_resolution(); original_render_controls(); sampler_bits(); queued_palette_changes();
    puts("PASS: real owned TIM2 material resolution, wide texture tiles, released/missing descriptors, queued palette bank snapshots, original control/material/ambient state, blend mapping and authored clamp/mirror/filter bits");
    return 0;
}
