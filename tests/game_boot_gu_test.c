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
    int primitive, format, count, depth, cull, blend, alpha, textured, shade;
    int wrap_u, wrap_v, filter_min, filter_mag, source, destination;
    unsigned int source_fixed, destination_fixed;
    const void *pointer, *pixels, *palette;
    int width, height, stride;
    SpriteVertex sprite[2];
} Submission;

static RecvxBootRenderVertex vertices[9] __attribute__((aligned(16)));
static RecvxBootRenderBatch batches[3];
static RecvxBootRenderFrame frame;
static RecvxBootDraw overlays[2];
static size_t overlay_count;
static RecvxBootTexture texture, wide;
static unsigned char pixels[1024 * 32] __attribute__((aligned(16)));
static unsigned char memory[32768] __attribute__((aligned(16)));
static Submission submissions[16];
static size_t submission_count, memory_used;
static int enabled[32], list_active, finished, synchronized, consumed, cache_flushed;
static int wraps[2], filters[2], blend_source, blend_destination, matrix_mode;
static int shade_model;
static unsigned int blend_src_fixed, blend_dst_fixed, clear_color, clear_depth;
static int clear_flags, depth_mask, alpha_func, alpha_value, alpha_mask;
static int texture_width, texture_height, texture_stride;
static const void *texture_pixels, *texture_palette;
static int matrix_loaded[3], identities[3];
static float projection[16], scales[2], offsets[2];
static int draw_buffer_mode, draw_buffer_stride, display_width, display_height, display_stride;
static uintptr_t draw_address, display_address, depth_address;
static int depth_stride, depth_near, depth_far, depth_function, front_face;
static unsigned int screen_offset[2];
static int viewport[4], scissor[4], display_enabled, vblanks, swaps, terminated;
static unsigned int calls, start_at, finish_at, sync_at, consume_at, last_model_at, first_sprite_at;
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
void *sceGuSwapBuffers(void) { assert(synchronized); ++swaps; ++calls; return NULL; }
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
{ draw_buffer_mode = mode; draw_address = (uintptr_t)address; draw_buffer_stride = stride; ++calls; }
void sceGuDispBuffer(int width, int height, void *address, int stride)
{ display_width = width; display_height = height; display_address = (uintptr_t)address; display_stride = stride; ++calls; }
void sceGuDepthBuffer(void *address, int stride) { depth_address = (uintptr_t)address; depth_stride = stride; ++calls; }
void sceGuDepthRange(int near, int far) { depth_near = near; depth_far = far; ++calls; }
void sceGuDepthFunc(int function) { depth_function = function; ++calls; }
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
    assert(op == GU_ADD); blend_source = source; blend_destination = destination;
    blend_src_fixed = sf; blend_dst_fixed = df; ++calls;
}
void sceGuTexFunc(int function, int component)
{ assert(function == GU_TFX_MODULATE && component == GU_TCC_RGBA); ++calls; }
void sceGuTexWrap(int u, int v) { wraps[0] = u; wraps[1] = v; ++calls; }
void sceGuTexFilter(int min, int mag) { filters[0] = min; filters[1] = mag; ++calls; }
void sceGuTexMode(int psm, int mips, int cache, int swizzle)
{ assert(psm == GU_PSM_T8 && mips == 0 && cache == 0 && swizzle == 1); ++calls; }
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
void sceGuTexFlush(void) { ++calls; }
void sceGuClearColor(unsigned int color) { clear_color = color; ++calls; }
void sceGuClearDepth(unsigned int depth) { clear_depth = depth; ++calls; }
void sceGuClear(int flags) { clear_flags = flags; ++calls; }
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
    s->shade = shade_model;
    s->wrap_u = wraps[0]; s->wrap_v = wraps[1]; s->filter_min = filters[0]; s->filter_mag = filters[1];
    s->source = blend_source; s->destination = blend_destination;
    s->source_fixed = blend_src_fixed; s->destination_fixed = blend_dst_fixed;
    s->pixels = texture_pixels; s->palette = texture_palette;
    s->width = texture_width; s->height = texture_height; s->stride = texture_stride;
    return s;
}
void sceGumDrawArray(int primitive, int format, int count, const void *indices, const void *data)
{
    submission(primitive, format, count, indices, data);
    assert(matrix_loaded[GU_PROJECTION] && identities[GU_VIEW] && identities[GU_MODEL]);
    assert(primitive == GU_TRIANGLES && format == (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D));
    assert((const RecvxBootRenderVertex *)data >= vertices && (const RecvxBootRenderVertex *)data + count <= vertices + frame.vertex_count);
    assert((uintptr_t)data < (uintptr_t)memory || (uintptr_t)data >= (uintptr_t)(memory + sizeof(memory)));
    last_model_at = ++calls;
}
void sceGuDrawArray(int primitive, int format, int count, const void *indices, const void *data)
{
    Submission *s = submission(primitive, format, count, indices, data);
    assert(primitive == GU_SPRITES && count == 2 && format ==
        (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D));
    assert((const unsigned char *)data >= memory && (const unsigned char *)data + 2 * sizeof(SpriteVertex) <= memory + memory_used);
    memcpy(s->sprite, data, sizeof(s->sprite));
    if (!first_sprite_at) first_sprite_at = calls + 1;
    ++calls;
}

static void reset_frame(void)
{
    memset(vertices, 0, sizeof(vertices)); memset(batches, 0, sizeof(batches)); memset(overlays, 0, sizeof(overlays));
    memset(submissions, 0, sizeof(submissions)); memset(&frame, 0, sizeof(frame));
    submission_count = memory_used = overlay_count = 0; consumed = cache_flushed = 0;
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

int main(void)
{
    display_init(); models_then_overlays(); blend_mappings(); empty_and_capacity();
    recvxGameBootDisplaySwap(); assert(swaps == 1);
    recvxGameBootDisplayShutdown(); assert(terminated);
    puts("PASS: actual GU adapter viewport/VRAM/depth state, perspective matrix, external ABGR model vertices, materials, indexed textures, overlays and consume-after-sync lifetime");
    return 0;
}
