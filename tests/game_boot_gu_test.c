/* Execute the real GU adapter against typed SDK command mocks, not a rasterizer. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include "psp/game_boot_render.h"

typedef struct { float u, v; unsigned int color; float x, y, z; } SpriteVertex;
typedef struct {
    int primitive, format, count, depth, cull, blend, alpha, textured, shade, fog;
    int wrap_u, wrap_v, filter_min, filter_mag, source, destination, operation;
    int target_format, depth_mask, depth_function, texture_format, texture_swizzle, texture_component;
    uintptr_t target;
    unsigned int at, texsync_at, texflush_at;
    unsigned int source_fixed, destination_fixed;
    float fog_near, fog_far;
    unsigned int fog_color;
    const void *pointer, *pixels, *palette;
    int width, height, stride;
    SpriteVertex sprite[6];
} Submission;

static RecvxBootRenderVertex vertices[64] __attribute__((aligned(16)));
static RecvxBootRenderBatch batches[32];
static RecvxBootRenderFrame frame;
static RecvxBootDraw overlays[2];
static size_t overlay_count;
static RecvxBootTexture texture, wide;
static unsigned char pixels[1024 * 32] __attribute__((aligned(16)));
static unsigned char memory[32768] __attribute__((aligned(16)));
static Submission submissions[64];
static size_t submission_count, memory_used;
static int enabled[32], list_active, finished, synchronized, consumed, cache_flushed;
static int wraps[2], filters[2], blend_source, blend_destination, matrix_mode;
static int shade_model;
static int blend_operation, texture_format, texture_swizzle, texture_component;
static float fog_near, fog_far;
static unsigned int fog_color;
static unsigned int blend_src_fixed, blend_dst_fixed, clear_color, clear_depth;
static int clear_flags, depth_mask, alpha_func, alpha_value, alpha_mask;
static int texture_width, texture_height, texture_stride;
static const void *texture_pixels, *texture_palette;
static int matrix_loaded[3], identities[3];
static float projection[16], scales[2], offsets[2];
static int draw_buffer_mode, draw_buffer_stride, display_width, display_height, display_stride;
static uintptr_t draw_address, display_address, depth_address;
static uintptr_t context_draw_address, context_display_address;
static int depth_stride, depth_near, depth_far, depth_function, front_face;
static unsigned int screen_offset[2];
static int viewport[4], scissor[4], display_enabled, vblanks, swaps, terminated;
static unsigned int calls, start_at, finish_at, sync_at, consume_at, last_model_at, first_sprite_at;
static unsigned int texsync_at, texflush_at;
static struct { int flags, format; uintptr_t target; unsigned int color, at; } clears[16];
static size_t clear_count;
static jmp_buf fault;
static const char *fault_name;
static int expecting_fault;

void recvxGameBootUnsupported(const char *name)
{
    if (!expecting_fault) { fprintf(stderr, "unexpected GU fault: %s\n", name); abort(); }
    fault_name = name; longjmp(fault, 1);
}
const RecvxBootDraw *recvxGameBootDraws(size_t *count) { *count = overlay_count; return overlays; }
const RecvxBootRenderFrame *recvxGameBootRenderFrame(void) { return &frame; }
void recvxGameBootRenderConsume(void)
{
    assert(synchronized && !list_active && finish_at < sync_at && !consumed);
    consumed = 1; consume_at = ++calls;
}
unsigned int recvxEngineOriginalViewBackgroundColor(void) { return 0xff112233u; }

int sceGuInit(void) { ++calls; return 0; }
void sceGuTerm(void) { terminated = 1; ++calls; }
int sceGuStart(int mode, void *list)
{
    assert(mode == GU_DIRECT && !((uintptr_t)list & 15u) && !list_active);
    list_active = 1; finished = synchronized = 0; start_at = ++calls; return 0;
}
int sceGuFinish(void)
{ assert(list_active); finished = 1; finish_at = ++calls; return 0; }
int sceGuSync(int mode, int what)
{
    assert(mode == 0 && what == 0 && finished && list_active);
    list_active = 0; synchronized = 1; sync_at = ++calls; return 0;
}
void *sceGuSwapBuffers(void)
{
    assert(synchronized); ++swaps; ++calls;
    uintptr_t old = context_draw_address; context_draw_address = context_display_address;
    context_display_address = old; draw_address = context_draw_address;
    return (void *)context_draw_address;
}
int sceGuDisplay(int on) { display_enabled = on; ++calls; return 0; }
int sceDisplayWaitVblankStart(void) { ++vblanks; ++calls; return 0; }
void sceKernelDcacheWritebackAll(void) { assert(!list_active); cache_flushed = 1; ++calls; }
void *sceGuGetMemory(int size)
{
    void *result;
    assert(list_active && size > 0 && memory_used + (size_t)size <= sizeof(memory));
    result = memory + memory_used; memory_used += (size + 15u) & ~15u; ++calls; return result;
}
void sceGuDrawBuffer(int mode, void *address, int stride)
{ draw_buffer_mode = mode; draw_address = context_draw_address = (uintptr_t)address; draw_buffer_stride = stride; ++calls; }
void sceGuDrawBufferList(int mode, void *address, int stride)
{
    assert(list_active); draw_buffer_mode = mode; draw_address = (uintptr_t)address;
    draw_buffer_stride = stride; ++calls;
}
void sceGuDispBuffer(int width, int height, void *address, int stride)
{ display_width = width; display_height = height; display_address = context_display_address = (uintptr_t)address; display_stride = stride; ++calls; }
void sceGuDepthBuffer(void *address, int stride) { depth_address = (uintptr_t)address; depth_stride = stride; ++calls; }
void sceGuDepthRange(int near, int far) { depth_near = near; depth_far = far; ++calls; }
void sceGuDepthFunc(int function) { depth_function = function; ++calls; }
void sceGuFog(float near, float far, unsigned int color)
{ fog_near=near; fog_far=far; fog_color=color; ++calls; }
void sceGuFrontFace(int order) { front_face = order; ++calls; }
void sceGuOffset(unsigned int x, unsigned int y) { screen_offset[0] = x; screen_offset[1] = y; ++calls; }
void sceGuViewport(int x, int y, int width, int height)
{ viewport[0] = x; viewport[1] = y; viewport[2] = width; viewport[3] = height; ++calls; }
void sceGuScissor(int x, int y, int width, int height)
{ scissor[0] = x; scissor[1] = y; scissor[2] = width; scissor[3] = height; ++calls; }
void sceGuEnable(int mode) { assert(mode >= 0 && mode < 32); enabled[mode] = 1; ++calls; }
void sceGuDisable(int mode) { assert(mode >= 0 && mode < 32); enabled[mode] = 0; ++calls; }
void sceGuDepthMask(int mask) { depth_mask = mask; ++calls; }
void sceGuShadeModel(int mode) { shade_model = mode; ++calls; }
void sceGuAlphaFunc(int func, int value, int mask)
{ alpha_func = func; alpha_value = value; alpha_mask = mask; ++calls; }
void sceGuBlendFunc(int op, int source, int destination, unsigned int sf, unsigned int df)
{
    assert(op == GU_ADD || op == GU_REVERSE_SUBTRACT); blend_operation = op;
    blend_source = source; blend_destination = destination;
    blend_src_fixed = sf; blend_dst_fixed = df; ++calls;
}
void sceGuTexFunc(int function, int component)
{
    assert(function == GU_TFX_MODULATE && (component == GU_TCC_RGBA || component == GU_TCC_RGB));
    texture_component=component; ++calls;
}
void sceGuTexWrap(int u, int v) { wraps[0] = u; wraps[1] = v; ++calls; }
void sceGuTexFilter(int min, int mag) { filters[0] = min; filters[1] = mag; ++calls; }
void sceGuTexMode(int psm, int mips, int cache, int swizzle)
{
    assert(!mips && !cache && ((psm == GU_PSM_T8 && swizzle == 1) ||
        (psm == GU_PSM_5551 && swizzle == 0)));
    texture_format = psm; texture_swizzle = swizzle; ++calls;
}
void sceGuClutMode(unsigned int psm, unsigned int shift, unsigned int mask, unsigned int offset)
{ assert(psm == GU_PSM_8888 && !shift && mask == 255 && !offset); ++calls; }
void sceGuClutLoad(int blocks, const void *palette)
{ assert(blocks == 32 && palette); texture_palette = palette; ++calls; }
void sceGuTexImage(int mip, int width, int height, int stride, const void *data)
{
    assert(!mip && width <= 512 && height <= 512 && data);
    texture_width = width; texture_height = height; texture_stride = stride; texture_pixels = data; ++calls;
}
void sceGuTexScale(float u, float v) { scales[0] = u; scales[1] = v; ++calls; }
void sceGuTexOffset(float u, float v) { offsets[0] = u; offsets[1] = v; ++calls; }
void sceGuTexFlush(void) { texflush_at = ++calls; }
void sceGuTexSync(void) { assert(list_active); texsync_at = ++calls; }
void sceGuClearColor(unsigned int color) { clear_color = color; ++calls; }
void sceGuClearDepth(unsigned int depth) { clear_depth = depth; ++calls; }
void sceGuClear(int flags)
{
    assert(clear_count < sizeof(clears)/sizeof(clears[0]));
    clear_flags = flags;
    clears[clear_count].flags=flags; clears[clear_count].format=draw_buffer_mode;
    clears[clear_count].target=draw_address; clears[clear_count].color=clear_color;
    clears[clear_count++].at=++calls;
}
void sceGumMatrixMode(int mode) { matrix_mode = mode; ++calls; }
void sceGumLoadIdentity(void)
{ assert(matrix_mode == GU_VIEW || matrix_mode == GU_MODEL); identities[matrix_mode] = 1; ++calls; }
void sceGumLoadMatrix(const ScePspFMatrix4 *matrix)
{
    assert(matrix_mode == GU_PROJECTION && matrix);
    memcpy(projection, matrix, sizeof(projection)); matrix_loaded[matrix_mode] = 1; ++calls;
}
static Submission *submission(int primitive, int format, int count, const void *indices, const void *data)
{
    Submission *s;
    assert(list_active && !finished && cache_flushed && !consumed && !indices);
    assert(submission_count < sizeof(submissions) / sizeof(submissions[0]));
    s = &submissions[submission_count++]; memset(s, 0, sizeof(*s));
    s->primitive = primitive; s->format = format; s->count = count; s->pointer = data;
    s->depth = enabled[GU_DEPTH_TEST]; s->cull = enabled[GU_CULL_FACE]; s->blend = enabled[GU_BLEND];
    s->alpha = enabled[GU_ALPHA_TEST]; s->textured = enabled[GU_TEXTURE_2D];
    s->fog=enabled[GU_FOG]; s->fog_near=fog_near; s->fog_far=fog_far; s->fog_color=fog_color;
    s->shade = shade_model;
    s->wrap_u = wraps[0]; s->wrap_v = wraps[1]; s->filter_min = filters[0]; s->filter_mag = filters[1];
    s->source = blend_source; s->destination = blend_destination;
    s->operation = blend_operation; s->depth_mask = depth_mask; s->depth_function = depth_function;
    s->target = draw_address; s->target_format = draw_buffer_mode;
    s->texture_format = texture_format; s->texture_swizzle = texture_swizzle;
    s->texture_component = texture_component;
    s->at = calls; s->texsync_at = texsync_at; s->texflush_at = texflush_at;
    s->source_fixed = blend_src_fixed; s->destination_fixed = blend_dst_fixed;
    s->pixels = texture_pixels; s->palette = texture_palette;
    s->width = texture_width; s->height = texture_height; s->stride = texture_stride;
    return s;
}
void sceGumDrawArray(int primitive, int format, int count, const void *indices, const void *data)
{
    submission(primitive, format, count, indices, data);
    assert(matrix_loaded[GU_PROJECTION] && identities[GU_VIEW] && identities[GU_MODEL]);
    assert((primitive == GU_TRIANGLES || primitive == GU_LINES) && format ==
        (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D));
    assert((const RecvxBootRenderVertex *)data >= vertices && (const RecvxBootRenderVertex *)data + count <= vertices + frame.vertex_count);
    assert((uintptr_t)data < (uintptr_t)memory || (uintptr_t)data >= (uintptr_t)(memory + sizeof(memory)));
    last_model_at = ++calls;
}
void sceGuDrawArray(int primitive, int format, int count, const void *indices, const void *data)
{
    Submission *s = submission(primitive, format, count, indices, data);
    assert(((primitive == GU_SPRITES && count == 2) || (primitive == GU_TRIANGLES && count > 0 && !(count%3))) && format ==
        (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D));
    uintptr_t pointer=(uintptr_t)data;
    assert((pointer >= (uintptr_t)memory && pointer + count * sizeof(SpriteVertex) <= (uintptr_t)memory + memory_used) ||
        (pointer >= (uintptr_t)vertices && pointer + count * sizeof(SpriteVertex) <= (uintptr_t)vertices + frame.vertex_count*sizeof(*vertices)));
    memcpy(s->sprite, data, (count > 6 ? 6u : (unsigned int)count)*sizeof(SpriteVertex));
    if (primitive==GU_SPRITES) {
        if (!first_sprite_at) first_sprite_at = calls + 1;
    } else last_model_at = calls + 1;
    ++calls;
}

static void reset_frame(void)
{
    memset(vertices, 0, sizeof(vertices)); memset(batches, 0, sizeof(batches)); memset(overlays, 0, sizeof(overlays));
    memset(submissions, 0, sizeof(submissions)); memset(&frame, 0, sizeof(frame));
    submission_count = memory_used = overlay_count = 0; consumed = cache_flushed = 0;
    clear_count = texsync_at = texflush_at = 0;
    first_sprite_at = last_model_at = consume_at = 0;
    memset(matrix_loaded, 0, sizeof(matrix_loaded)); memset(identities, 0, sizeof(identities));
    frame.vertices = vertices; frame.batches = batches;
    texture = (RecvxBootTexture){.pixels = pixels, .width = 64, .height = 32, .tile_width = 64, .tile_count = 1};
    wide = (RecvxBootTexture){.pixels = pixels, .width = 1024, .height = 32, .tile_width = 512, .tile_count = 2};
    for (unsigned int i = 0; i < 16; ++i) frame.projection[i] = i * 0.25f - 2;
}

static void display_init(void)
{
    assert(!recvxGameBootDisplayInit());
    assert(draw_buffer_mode == GU_PSM_8888 && draw_address == 0 && draw_buffer_stride == 512);
    assert(display_width == 480 && display_height == 272 && display_stride == 512 && display_address == 512 * 272 * 4);
    assert(depth_address == 2 * 512 * 272 * 4 && depth_stride == 512 && depth_near == 65535 && depth_far == 0);
    assert(depth_function == GU_GEQUAL && front_face == GU_CCW);
    assert(screen_offset[0] == 1808 && screen_offset[1] == 1912);
    assert(viewport[0] == 2048 && viewport[1] == 2048 && viewport[2] == 480 && viewport[3] == 272);
    assert(scissor[0] == 0 && scissor[1] == 0 && scissor[2] == 480 && scissor[3] == 272);
    assert(enabled[GU_SCISSOR_TEST] && !enabled[GU_DEPTH_TEST] && !enabled[GU_CULL_FACE]);
    assert(display_enabled == GU_TRUE && vblanks == 1 && synchronized);
}

static void models_then_overlays(void)
{
    reset_frame(); frame.vertex_count = 9; frame.batch_count = 3;
    for (unsigned int i = 0; i < 9; ++i) vertices[i] = (RecvxBootRenderVertex){i, 1, 0x7f332211, i * 2, 1, -40};
    batches[0] = (RecvxBootRenderBatch){.first = 0, .count = 3, .attributes = 0x10, .sampler = 1, .texture = &texture};
    batches[1] = (RecvxBootRenderBatch){.first = 3, .count = 3, .attributes = 8, .blend_source = 10, .blend_destination = 11};
    batches[2] = (RecvxBootRenderBatch){.first = 6, .count = 3, .attributes = 0x38, .sampler = 4,
        .blend_source = 8, .blend_destination = 6, .texture = &texture};
    overlays[0] = (RecvxBootDraw){.quad = {.x1 = 10, .y1 = 20, .x2 = 30, .y2 = 40}, .color = 0x7f112233};
    overlays[1] = (RecvxBootDraw){.quad = {.x1 = 0, .y1 = 0, .x2 = 640, .y2 = 480, .u1 = 0, .v1 = 0, .u2 = 1, .v2 = 1},
        .color = 0xff445566, .filter = 1, .texture = &wide}; overlay_count = 2;
    recvxGameBootPresent();
    assert(consumed && sync_at < consume_at && start_at < last_model_at && last_model_at < first_sprite_at && first_sprite_at < finish_at);
    assert(!memcmp(projection, frame.projection, sizeof(projection)));
    assert(clear_color == 0xff332211u && clear_depth == 0 && clear_flags == (GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT));
    assert(submission_count == 6 && memory_used == 3 * 48 && depth_mask == GU_FALSE);
    assert(alpha_func == GU_GREATER && alpha_value == 0 && alpha_mask == 255);
    assert(submissions[0].depth && !submissions[0].cull && !submissions[0].blend && submissions[0].alpha);
    assert(submissions[0].textured && submissions[0].pixels == pixels && submissions[0].palette == texture.palette);
    assert(submissions[0].wrap_u == GU_CLAMP && submissions[0].wrap_v == GU_REPEAT && submissions[0].filter_min == GU_NEAREST);
    assert(submissions[0].pointer == vertices && vertices[0].color == 0x7f332211); /* Already ABGR: no second conversion. */
    assert(submissions[1].cull && submissions[1].blend && !submissions[1].textured && submissions[1].pointer == vertices + 3);
    assert(submissions[1].source == GU_FIX && submissions[1].source_fixed == 0xffffff && submissions[1].destination == GU_FIX && !submissions[1].destination_fixed);
    assert(submissions[2].filter_min == GU_LINEAR && submissions[2].filter_mag == GU_LINEAR && submissions[2].pointer == vertices + 6);
    assert(submissions[0].shade == GU_SMOOTH && submissions[2].shade == GU_FLAT);
    for (unsigned int i = 3; i < 6; ++i) assert(!submissions[i].depth && !submissions[i].cull && !submissions[i].alpha && submissions[i].blend);
    assert(submissions[3].sprite[0].color == 0x7f332211 && submissions[3].sprite[0].x == 7.5f);
    assert(fabsf(submissions[3].sprite[0].y - 20 * 272.0f / 480) < 0.0001f);
    assert(submissions[4].pixels == pixels && submissions[5].pixels == pixels + 512 * 32);
    assert(submissions[4].sprite[0].x == 0 && submissions[4].sprite[1].x == 240);
    assert(submissions[5].sprite[0].x == 240 && submissions[5].sprite[1].x == 480);
    assert(submissions[4].sprite[1].u == 512 && submissions[5].sprite[0].u == 0);
    assert(submissions[4].wrap_u == GU_CLAMP && submissions[4].wrap_v == GU_CLAMP);
    assert(scales[0] == 1 && scales[1] == 1 && offsets[0] == 0 && offsets[1] == 0);
}

static void empty_and_capacity(void)
{
    reset_frame(); recvxGameBootPresent(); assert(consumed && !submission_count && !memory_used && clear_color == 0xff000000);
    reset_frame(); frame.batch_count = 5000; expecting_fault = 1;
    if (!setjmp(fault)) { recvxGameBootPresent(); assert(!"expected GU capacity stop"); }
    expecting_fault = 0; assert(strstr(fault_name, "command list exceeds capacity"));
    assert(!consumed && !submission_count && !cache_flushed && !list_active);
}

static void blend_mappings(void)
{
    const int ninja[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    const int gu[] = {GU_DST_ALPHA, GU_OTHER_COLOR, GU_ONE_MINUS_DST_ALPHA, GU_ONE_MINUS_OTHER_COLOR,
        GU_ONE_MINUS_SRC_ALPHA, GU_ONE_MINUS_OTHER_COLOR, GU_SRC_ALPHA, GU_OTHER_COLOR, GU_FIX, GU_FIX};
    for (unsigned int i = 0; i < sizeof(ninja) / sizeof(ninja[0]); ++i) {
        reset_frame(); frame.vertex_count = 3; frame.batch_count = 1;
        batches[0] = (RecvxBootRenderBatch){.count = 3, .attributes = 8,
            .blend_source = ninja[i], .blend_destination = ninja[i]};
        recvxGameBootPresent();
        assert(consumed && !memory_used && submission_count == 1);
        assert(submissions[0].source == gu[i] && submissions[0].destination == gu[i]);
        assert(submissions[0].source_fixed == (ninja[i] == 10 ? 0xffffffu : 0));
        assert(submissions[0].destination_fixed == (ninja[i] == 10 ? 0xffffffu : 0));
    }
}

static void shadow_protocol(void)
{
    const uintptr_t mask_target=1392640u;
    const uintptr_t scene_target=context_draw_address;
    reset_frame(); frame.vertex_count=24; frame.batch_count=11;
    for(unsigned int i=0;i<24;++i)
        vertices[i]=(RecvxBootRenderVertex){0,0,0xff202020u,i,1,-40};
    vertices[9]=(RecvxBootRenderVertex){30.5f,17.5f,0xff5e5e5eu,30,17,0};
    vertices[10]=(RecvxBootRenderVertex){120.5f,68.5f,0xff5e5e5eu,120,68,0};
    vertices[17]=(RecvxBootRenderVertex){60.5f,34.5f,0xff242424u,60,34,0};
    vertices[18]=(RecvxBootRenderVertex){180.5f,136.5f,0xff242424u,180,136,0};
    batches[0]=(RecvxBootRenderBatch){.count=3,.texture=&texture,.kind=RECVX_RENDER_HALF_FOG,.fog_color=0x332211};
    batches[1]=(RecvxBootRenderBatch){.first=3,.kind=RECVX_RENDER_SHADOW_START};
    batches[2]=(RecvxBootRenderBatch){.first=3,.count=3,.kind=RECVX_RENDER_SHADOW_ADD};
    batches[3]=(RecvxBootRenderBatch){.first=6,.count=3,.kind=RECVX_RENDER_SHADOW_SUBTRACT};
    batches[4]=(RecvxBootRenderBatch){.first=9,.count=2,.kind=RECVX_RENDER_SHADOW_COMPOSITE};
    batches[5]=(RecvxBootRenderBatch){.first=11,.count=3,.kind=RECVX_RENDER_SHADOW_ADD};
    batches[6]=(RecvxBootRenderBatch){.first=14,.count=3,.kind=RECVX_RENDER_SHADOW_SUBTRACT};
    batches[7]=(RecvxBootRenderBatch){.first=17,.count=2,.kind=RECVX_RENDER_SHADOW_COMPOSITE};
    batches[8]=(RecvxBootRenderBatch){.first=19,.kind=RECVX_RENDER_SHADOW_END};
    batches[9]=(RecvxBootRenderBatch){.first=19,.count=2,.attributes=0x18,.blend_source=8,
        .blend_destination=6,.kind=RECVX_RENDER_LINES};
    batches[10]=(RecvxBootRenderBatch){.first=21,.count=3,.texture=&texture};
    overlays[0]=(RecvxBootDraw){.quad={.x1=0,.y1=0,.x2=640,.y2=480},.color=0xff112233};
    overlay_count=1;
    recvxGameBootPresent();
    assert(consumed && submission_count==12 && clear_count==2);
    assert(submissions[0].fog && submissions[0].fog_color==0x332211);
    for(unsigned int i=1;i<12;++i)assert(!submissions[i].fog);
    assert(clears[0].target==scene_target && clears[0].format==GU_PSM_8888 &&
        clears[0].flags==(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT));
    assert(clears[1].target==mask_target && clears[1].format==GU_PSM_5551 &&
        clears[1].flags==GU_COLOR_BUFFER_BIT && clears[1].color==0);
    assert(submissions[0].at<clears[1].at && clears[1].at<submissions[1].at);
    for(unsigned int object=0;object<2;++object) {
        size_t first=1+object*4;
        const Submission *add=&submissions[first], *subtract=&submissions[first+1];
        const Submission *composite=&submissions[first+2], *clear=&submissions[first+3];
        assert(add->primitive==GU_TRIANGLES && subtract->primitive==GU_TRIANGLES);
        assert(add->target==mask_target && subtract->target==mask_target &&
            add->target_format==GU_PSM_5551 && subtract->target_format==GU_PSM_5551);
        assert(add->depth && subtract->depth && add->depth_mask==GU_TRUE && subtract->depth_mask==GU_TRUE);
        assert(add->depth_function==GU_GEQUAL && subtract->depth_function==GU_GEQUAL);
        assert(!add->cull && !subtract->cull && !add->alpha && !subtract->alpha &&
            !add->textured && !subtract->textured && add->blend && subtract->blend);
        assert(add->operation==GU_ADD && subtract->operation==GU_REVERSE_SUBTRACT);
        assert(add->source==GU_FIX && add->destination==GU_FIX && add->source_fixed==0xffffffu && add->destination_fixed==0xffffffu);
        assert(subtract->source==GU_FIX && subtract->destination==GU_FIX &&
            subtract->source_fixed==0xffffffu && subtract->destination_fixed==0xffffffu);
        assert(composite->primitive==GU_SPRITES && composite->count==2 && composite->target==scene_target &&
            composite->target_format==GU_PSM_8888 && composite->textured && composite->blend);
        assert(!composite->depth && composite->depth_mask==GU_TRUE && !composite->alpha && !composite->cull);
        assert(composite->operation==GU_REVERSE_SUBTRACT && composite->source==GU_FIX && composite->destination==GU_FIX &&
            composite->source_fixed==0xffffffu && composite->destination_fixed==0xffffffu);
        assert(composite->texture_format==GU_PSM_5551 && !composite->texture_swizzle && composite->texture_component==GU_TCC_RGB);
        assert((uintptr_t)composite->pixels==0x04000000u+mask_target && composite->width==512 && composite->height==512 && composite->stride==512);
        assert(composite->wrap_u==GU_CLAMP && composite->wrap_v==GU_CLAMP &&
            composite->filter_min==GU_NEAREST && composite->filter_mag==GU_NEAREST);
        assert(subtract->at<composite->texsync_at && composite->texsync_at<composite->texflush_at &&
            composite->texflush_at<composite->at);
        assert(composite->sprite[0].color==(object?0xff242424u:0xff5e5e5eu));
        assert(composite->sprite[0].u==composite->sprite[0].x+0.5f && composite->sprite[1].v==composite->sprite[1].y+0.5f);
        assert(clear->target==mask_target && clear->target_format==GU_PSM_5551 && clear->primitive==GU_SPRITES);
        assert(!clear->blend && !clear->depth && clear->depth_mask==GU_TRUE && !clear->textured && !clear->alpha);
        for(unsigned int i=0;i<2;++i) {
            assert(!clear->sprite[i].color && clear->sprite[i].x==composite->sprite[i].x &&
                clear->sprite[i].y==composite->sprite[i].y);
        }
    }
    assert(submissions[9].primitive==GU_LINES && submissions[9].count==2 &&
        submissions[9].target==scene_target && submissions[9].target_format==GU_PSM_8888);
    assert(submissions[9].depth && submissions[9].depth_mask==GU_FALSE && submissions[9].alpha && submissions[9].operation==GU_ADD);
    assert(submissions[10].primitive==GU_TRIANGLES && submissions[10].target==scene_target &&
        submissions[10].depth && submissions[10].depth_mask==GU_FALSE && submissions[10].alpha && submissions[10].texture_format==GU_PSM_T8);
    assert(submissions[11].primitive==GU_SPRITES && submissions[11].target==scene_target &&
        !submissions[11].depth && !submissions[11].alpha && submissions[11].operation==GU_ADD);
    assert(draw_buffer_mode==GU_PSM_8888 && draw_address==scene_target && context_draw_address==scene_target);
    assert(depth_address==2u*512u*272u*4u && depth_stride==512);
    assert(mask_target+512u*512u*2u<=2u*1024u*1024u);
}

static float ge_command_float(float value)
{
    uint32_t bits; memcpy(&bits,&value,sizeof(bits));
    bits&=0xffffff00u; memcpy(&value,&bits,sizeof(value));
    return value;
}

static void authored_half_fog(void)
{
    reset_frame(); frame.vertex_count=11; frame.batch_count=4;
    batches[0]=(RecvxBootRenderBatch){.count=3,.texture=&texture,.kind=RECVX_RENDER_HALF_FOG,.fog_color=0x776655};
    batches[1]=(RecvxBootRenderBatch){.first=3,.count=3,.texture=&texture};
    batches[2]=(RecvxBootRenderBatch){.first=6,.count=3,.texture=&texture,.kind=RECVX_RENDER_HALF_FOG,.fog_color=0xabcdef};
    batches[3]=(RecvxBootRenderBatch){.first=9,.count=2,.kind=RECVX_RENDER_LINES};
    vertices[0].z=-0x1.fffffep38f; vertices[1].z=-1; vertices[2].z=-10;
    vertices[6].z=0; vertices[7].z=0x1.fffffep38f; vertices[8].z=-100000;
    overlays[0]=(RecvxBootDraw){.quad={.x1=0,.y1=0,.x2=10,.y2=10},.color=0xffffffff};
    overlay_count=1;
    recvxGameBootPresent();assert(consumed && submission_count==5);
    for(unsigned int i=0;i<5;++i) {
        const Submission *s=&submissions[i];
        if(i==0||i==2) {
            assert(s->fog && s->fog_near==-0x1p64f && s->fog_far==0x1p64f);
            assert(s->fog_color==(i==0?0x776655u:0xabcdefu));
            assert(s->textured && s->texture_format==GU_PSM_T8 && s->depth && s->alpha);
            const RecvxBootRenderVertex *v=s->pointer;
            for(unsigned int j=0;j<3;++j) {
                /* The finite powers of two preserve an exactly representable
                   half factor throughout the renderer's validated Z range. */
                float factor=(s->fog_far+v[j].z)/(s->fog_far-s->fog_near);
                assert(factor==0.5f);
                /* sceGuFog emits far and 1/(far-near) as GE float24 command
                   payloads, dropping the low eight IEEE mantissa bits. */
                float far=ge_command_float(s->fog_far);
                float slope=ge_command_float(1.0f/(s->fog_far-s->fog_near));
                assert(ge_command_float((far+v[j].z)*slope)==0.5f);
            }
        } else assert(!s->fog);
    }
    assert(!enabled[GU_FOG]);
}

static void screen_gradient_triangles(void)
{
    reset_frame();frame.vertex_count=12;frame.batch_count=4;
    batches[0]=(RecvxBootRenderBatch){.count=3,.kind=RECVX_RENDER_HALF_FOG,.texture=&texture,.fog_color=0x123456};
    batches[1]=(RecvxBootRenderBatch){.first=3,.count=3,.kind=RECVX_RENDER_SCREEN_TRIANGLES,
        .texture=&texture,.attributes=0x18,.sampler=5,.blend_source=8,.blend_destination=6};
    batches[2]=(RecvxBootRenderBatch){.first=6,.count=3,.texture=&texture,.attributes=0x20};
    batches[3]=(RecvxBootRenderBatch){.first=9,.count=3,.kind=RECVX_RENDER_SCREEN_TRIANGLES,
        .texture=&texture,.attributes=0x18,.sampler=2,.blend_source=10,.blend_destination=11};
    vertices[3]=(RecvxBootRenderVertex){0,0,0x80102040u,0,0,0};
    vertices[4]=(RecvxBootRenderVertex){64,0,0x406080a0u,480,0,65534};
    vertices[5]=(RecvxBootRenderVertex){16,32,0xffefcdabu,240,272,32767};
    memcpy(vertices+9,vertices+3,3*sizeof(*vertices));
    overlays[0]=(RecvxBootDraw){.quad={.x1=0,.y1=0,.x2=640,.y2=480,.u2=1,.v2=1},.texture=&texture,.color=0xffffffff};
    overlay_count=1;
    recvxGameBootPresent();assert(consumed&&submission_count==5);
    assert(submissions[0].fog&&submissions[0].format==
        (GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D));
    for(unsigned int i=1;i<=3;i+=2) {
        const Submission *s=&submissions[i];
        assert(s->primitive==GU_TRIANGLES&&s->count==3&&s->format==
            (GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D));
        assert(s->pointer==vertices+batches[i].first&&!s->fog&&!s->cull&&s->depth&&s->alpha&&s->blend);
        assert(s->depth_mask==GU_FALSE&&s->depth_function==GU_GEQUAL&&s->shade==GU_SMOOTH);
        assert(s->target==context_draw_address&&s->target_format==GU_PSM_8888);
        assert(s->textured&&s->texture_format==GU_PSM_T8&&s->pixels==pixels&&s->palette==texture.palette);
        for(unsigned int j=0;j<3;++j) {
            const RecvxBootRenderVertex *v=&vertices[3+j];
            assert(s->sprite[j].u==v->u&&s->sprite[j].v==v->v&&s->sprite[j].color==v->color);
            assert(s->sprite[j].x==v->x&&s->sprite[j].y==v->y&&s->sprite[j].z==v->z);
        }
        assert(s->operation==GU_ADD);
    }
    assert(submissions[1].filter_min==GU_LINEAR&&submissions[1].wrap_u==GU_CLAMP&&submissions[1].wrap_v==GU_REPEAT);
    assert(submissions[1].source==GU_SRC_ALPHA&&submissions[1].destination==GU_ONE_MINUS_SRC_ALPHA);
    assert(submissions[3].filter_min==GU_NEAREST&&submissions[3].wrap_u==GU_REPEAT&&submissions[3].wrap_v==GU_CLAMP);
    assert(submissions[3].source==GU_FIX&&submissions[3].source_fixed==0xffffffu&&submissions[3].destination==GU_FIX&&!submissions[3].destination_fixed);
    assert(submissions[2].primitive==GU_TRIANGLES&&submissions[2].format==
        (GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D));
    assert(submissions[2].depth&&submissions[2].depth_mask==GU_FALSE&&submissions[2].cull&&submissions[2].shade==GU_FLAT&&!submissions[2].fog);
    assert(submissions[4].primitive==GU_SPRITES&&!submissions[4].depth&&!submissions[4].alpha&&!submissions[4].fog&&
        submissions[4].blend&&submissions[4].operation==GU_ADD&&submissions[4].wrap_u==GU_CLAMP&&submissions[4].wrap_v==GU_CLAMP);
    assert(last_model_at<first_sprite_at&&first_sprite_at<finish_at&&sync_at<consume_at);
}

static void gradient_overlays(void)
{
    reset_frame();
    overlays[0] = (RecvxBootDraw){.color_gradient=1,
        .points={{0,40},{640,40},{640,104},{0,104}},
        .vertex_color={0xff102030,0xff405060,0x00708090,0x00a0b0c0}};
    overlays[1] = (RecvxBootDraw){.quad={.x1=0,.y1=0,.x2=640,.y2=480},.color=0x80000000};
    overlay_count=2;
    recvxGameBootPresent();
    assert(submission_count==2 && consumed);
    const Submission *gradient=&submissions[0], *fade=&submissions[1];
    assert(gradient->primitive==GU_TRIANGLES && gradient->count==6 && !gradient->textured);
    assert(gradient->shade==GU_SMOOTH && !gradient->depth && !gradient->alpha && !gradient->cull);
    assert(gradient->blend && gradient->source==GU_SRC_ALPHA && gradient->destination==GU_ONE_MINUS_SRC_ALPHA);
    static const unsigned int order[6]={0,1,2,0,2,3};
    for(unsigned int i=0;i<6;++i) {
        unsigned int corner=order[i], color=overlays[0].vertex_color[corner];
        assert(gradient->sprite[i].color==((color&0xff00ff00u)|((color>>16)&255u)|((color&255u)<<16)));
        assert(gradient->sprite[i].x==overlays[0].points[corner].x*0.75f);
        assert(gradient->sprite[i].y==overlays[0].points[corner].y*(272.0f/480));
        assert(gradient->sprite[i].z==0);
    }
    assert(fade->primitive==GU_SPRITES && fade->sprite[0].color==0x80000000);
    assert(gradient->at<fade->at && fade->at<finish_at && sync_at<consume_at);
}

int main(void)
{
    display_init(); models_then_overlays(); blend_mappings(); authored_half_fog(); screen_gradient_triangles(); shadow_protocol();
    recvxGameBootDisplaySwap(); assert(swaps==1 && context_draw_address==512u*272u*4u);
    shadow_protocol(); /* Offscreen switches must preserve the swapped scene target. */
    recvxGameBootDisplaySwap(); assert(swaps==2 && context_draw_address==0);
    shadow_protocol(); gradient_overlays(); empty_and_capacity();
    recvxGameBootDisplayShutdown(); assert(terminated);
    puts("PASS: actual GU adapter viewport/VRAM/depth state, perspective matrix, external ABGR geometry/lines, materials, indexed textures, authored constant half-fog, screen triangle gradients/depth, modifier add/subtract mask passes/composite/bounded clears, swap target restoration, overlays and consume-after-sync lifetime");
    return 0;
}
