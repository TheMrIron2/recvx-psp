#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaSprite.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/ps2_texture.h"

static SYS_WORK work;
SYS_WORK *sys = &work;
unsigned char *freemem;
static jmp_buf fault;
static const char *fault_name;
static int expecting_fault;
unsigned int palbuf[4096];
static RecvxBootTexture snapshots[8];
static size_t snapshot_count;
static int render_pending;
static unsigned int presents;
void recvxGameBootLog(const char *format, ...) { (void)format; }
void recvxGameBootPresent(void) { ++presents; recvxGameBootDrawDiscard(); render_pending = 0; }
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
    NJS_TEXANIM animation={.sx=20,.sy=10,.cx=5,.cy=2,.u1=0,.v1=0,.u2=256,.v2=128,.texid=0};
    NJS_SPRITE sprite={.p={100,50,0},.sx=2,.sy=3,.tlist=&list,.tanim=&animation};
    njDrawSprite2D(&sprite,0,-4,NJD_SPRITE_ALPHA);
    size_t queued_count;
    const RecvxBootDraw *queued=recvxGameBootDraws(&queued_count);
    assert(queued_count==1 && queued[0].color_gradient && queued[0].texture==texture);
    assert(queued[0].points[0].x==90 && queued[0].points[0].y==44 && queued[0].points[2].x==130 && queued[0].points[2].y==74);
    assert(queued[0].depth==4 && queued[0].vertex_color[0]==0xffffffffu);
    sprite.ang=16384;
    njDrawSprite2D(&sprite,0,-3,NJD_SPRITE_ALPHA|NJD_SPRITE_ANGLE|NJD_SPRITE_HFLIP);
    queued=recvxGameBootDraws(&queued_count);
    assert(queued_count==1 && fabsf(queued[0].points[0].x-106)<.001f && fabsf(queued[0].points[0].y-40)<.001f);
    assert(queued[0].uv[0].x>queued[0].uv[1].x);
    REJECT(njDrawSprite2D(&sprite,-1,-1,NJD_SPRITE_ALPHA),"inventory sprite");
    REJECT(recvxGameBootRenderTextureSelect(NULL, 3), "texture exceeds");
    REJECT(recvxGameBootRenderTextureSelect(NULL, 2), "texture descriptor");
    recvxGameBootTexturesShutdown();
    REJECT(recvxGameBootRenderTextureSelect(&list, 0), "valid owned texture");
    recvxGameBootRenderTextureList(NULL);
    REJECT(recvxGameBootRenderTextureSelect(NULL, 0), "texture exceeds");
    list.nbTexture = 257; REJECT(recvxGameBootRenderTextureList(&list), "render texture list");
}

static void room_texture_backup(void)
{
    static unsigned char arena[65536];
    NJS_TEXNAME names[2] = {{0}}, menu_name = {0};
    NJS_TEXLIST list = {names, 2}, menu = {&menu_name, 1}, wrong = {names, 1};
    freemem = arena; sys->endp = arena + sizeof(arena); sys->memp = arena + 3;
    Init_PS2_SAVE_TEX();
    for (unsigned int i = 0; i < 2; ++i) {
        size_t size;
        unsigned char *data = texture_data(i ? 1024 : 16, i ? 5 : 4, &size);
        recvxGameBootTextureInstall(&list, i, data, size, 0, 700 + i, 0x10 + i, 3 + i);
        free(data);
        RecvxBootTexture *texture = (RecvxBootTexture *)recvxGameBootTextureGet(&list, i);
        texture->pixels[5] = 77 + i; texture->palette[2] = 0x12345678u + i;
    }
    char *end = bhCopyTexmem2MainmemSub(&list, (char *)sys->memp);
    assert(end > (char *)sys->memp && end <= (char *)sys->endp && sys->memp == arena + 3);
    REJECT(bhCopyTexmem2MainmemSub(&list, (char *)sys->endp - 1), "backup exceeds");
    REJECT(bhCopyMainmem2Texmem(&wrong), "mismatched");
    njReleaseTexture(&list);
    size_t size;
    unsigned char *data = texture_data(16, 4, &size);
    recvxGameBootTextureInstall(&menu, 0, data, size, 0, 999, 0, 0); free(data);
    const RecvxBootTexture *menu_texture = recvxGameBootTextureGet(&menu, 0);
    unsigned int before = presents;
    render_pending = 1;
    bhCopyMainmem2Texmem(&list);
    assert(presents == before + 1 && !render_pending);
    assert(recvxGameBootTextureGet(&menu, 0) == menu_texture && menu_texture->pixels[0] == 2);
    for (unsigned int i = 0; i < 2; ++i) {
        const RecvxBootTexture *texture = recvxGameBootTextureGet(&list, i);
        NJS_TEXMEMLIST *descriptor = (NJS_TEXMEMLIST *)(uintptr_t)names[i].texaddr;
        assert(texture->pixels[5] == 77 + i && texture->palette[2] == 0x12345678u + i);
        assert(descriptor->globalIndex == 700 + i && descriptor->bank == 3 + i);
        assert(names[i].attr == 0x10 + i && !memcmp(texture->source, "TIM2", 4));
    }
    assert(recvxGameBootTextureGet(&list, 1)->tile_count == 2);
    REJECT(bhCopyMainmem2Texmem(&list), "mismatched");
    /* A restore without release must replace the live copies without leaking
       slots, while repeated saves replace the retail single backup. */
    for (unsigned int i = 0; i < 260; ++i) {
        bhCopyTexmem2MainmemSub(&list, (char *)sys->memp);
        bhCopyMainmem2Texmem(&list);
    }
    bhCopyTexmem2MainmemSub(&list, (char *)sys->memp);
    unsigned char saved = sys->memp[0]; sys->memp[0] ^= 1;
    REJECT(bhCopyMainmem2Texmem(&list), "backup record");
    assert(recvxGameBootTextureGet(&list, 0)->pixels[5] == 77);
    sys->memp[0] = saved;
    /* Global eviction leaves old list handles behind; their slots may be
       reused by unrelated artwork before this room is restored. */
    njReleaseTextureAll();
    data = texture_data(16, 4, &size);
    recvxGameBootTextureInstall(&menu, 0, data, size, 0, 1000, 0, 0); free(data);
    bhCopyMainmem2Texmem(&list);
    assert(recvxGameBootTextureGet(&menu, 0)->pixels[0] == 2);
    assert(recvxGameBootTextureGet(&list, 0)->pixels[5] == 77);
    bhCopyTexmem2MainmemSub(&list, (char *)sys->memp);
    recvxGameBootTexturesShutdown();
    REJECT(bhCopyMainmem2Texmem(&list), "mismatched");
    NJS_TEXLIST empty = {0};
    assert(bhCopyTexmem2MainmemSub(&empty, (char *)sys->endp) == (char *)sys->endp);
    bhCopyMainmem2Texmem(&empty);
    Init_PS2_SAVE_TEX(); freemem = NULL; sys->endp = NULL;
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

static void texture_collection(void)
{
    NJS_TEXNAME names[3]={{0}};
    NJS_TEXLIST list={names,3}, hole={names+1,1};
    size_t size, count;
    unsigned char *data=texture_data(16,4,&size);
    for (unsigned int i=0;i<3;++i) recvxGameBootTextureInstall(&list,i,data,size,0,500+i,0,0);
    free(data);
    njSetTexture(&list); njSetTextureNum(0);
    const RecvxBootTexture *first=recvxGameBootTextureGet(&list,0), *last=recvxGameBootTextureGet(&list,2);
    const unsigned char *first_pixels=first->pixels, *last_pixels=last->pixels;
    uintptr_t first_handle=names[0].texaddr, last_handle=names[2].texaddr, released=names[1].texaddr;
    njReleaseTexture(&hole); assert(!names[1].texaddr);
    QUAD quad={.x2=16,.y2=8,.u2=1,.v2=1};
    njSetQuadTexture(0,0xffffffffu); njDrawQuadTexture(&quad,1);
    render_pending=1; unsigned int before=presents;
    njGarbageTexture(NULL,0); assert(presents==before+1 && !render_pending);
    recvxGameBootDraws(&count); assert(!count);
    assert(names[0].texaddr==first_handle && names[2].texaddr==last_handle);
    assert(first->pixels==first_pixels && last->pixels==last_pixels && first->pixels[0]==2 && last->pixels[0]==2);
    assert(recvxGameBootRenderStateGet()->selected_texture==first);
    data=texture_data(16,4,&size); recvxGameBootTextureInstall(&list,1,data,size,0,503,0,0); free(data);
    assert(names[1].texaddr==released); /* Freed descriptor holes are reusable. */
    assert(njCalcTexture(0)==1048576 && njCalcTexture(UINT32_MAX)==1048576);
    bhGarbageTexture(NULL,UINT32_MAX); assert(presents==before+1);
    NJS_TEXMEMLIST *descriptor=(NJS_TEXMEMLIST *)first_handle;
    void *saved=descriptor->texinfo.texaddr; descriptor->texinfo.texaddr=NULL;
    REJECT(njGarbageTexture(NULL,0),"garbage texture ownership");
    descriptor->texinfo.texaddr=saved;
    njReleaseTextureAll(); njGarbageTexture(NULL,256);
    assert(!recvxGameBootRenderStateGet()->selected_texture && !Ps2_current_texmemlist);
}

int main(void)
{
    texture_resolution(); room_texture_backup(); original_render_controls(); sampler_bits(); queued_palette_changes(); texture_collection();
    puts("PASS: owned TIM2 materials, room texture arena save/restore with live palettes/pixels, repeated restore and slot reuse, wide tiles, released/missing descriptors, queued palette snapshots, texture collection preserves live handles and synchronizes queued users, retail capacity, render controls and sampler bits");
    return 0;
}
