#include <stdlib.h>
#include <malloc.h>
#include "psp/game_boot_game_remaining.h"
#include <string.h>
#include <math.h>
#include "psp/game_boot.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"
#include "ps2/veronica/prog/ps2_NaFog.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/ps2_NaDraw2D.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_texture.h"
#include "psp/engine_original_texture.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/ps2_NaDebugFontFunction.h"

#define TEXTURE_LIMIT 256
#define DRAW_LIMIT 512
static RecvxBootTexture textures[TEXTURE_LIMIT];
static NJS_TEXMEMLIST descriptors[TEXTURE_LIMIT];
static NJS_TEXLIST *current_list;
static unsigned int current_number, quad_color, palette_mode;
static int texture_filter;
static RecvxBootDraw draws[DRAW_LIMIT];
static size_t draw_count;
/* PSPSDK supplies the 256-glyph 8x8 font in libpspdebug. Render it through GU,
   never through the debug console's separate framebuffer. */
extern unsigned char msx[2048];
static unsigned char print_pixels[128 * 128] __attribute__((aligned(16)));
static RecvxBootTexture print_texture;
static unsigned int print_size = 13, print_color = 0xffe0e0e0u;

void njPrintSize(Uint16 size)
{
    if (!size || size > 128) recvxGameBootUnsupported("invalid debug print size");
    print_size = size;
}
void njPrintColor(Uint32 color) { print_color = color; }

void njPrintC(Int location, const char *string)
{
    size_t length;
    if (!string) recvxGameBootUnsupported("missing debug print string");
    for (length = 0; length < 1024 && string[length]; ++length) { }
    if (length == 1024) recvxGameBootUnsupported("debug print string exceeds limit");
    if (!print_texture.pixels) {
        for (unsigned int ch = 0; ch < 256; ++ch)
            for (unsigned int row = 0; row < 8; ++row)
                for (unsigned int col = 0; col < 8; ++col) {
                    unsigned int x = (ch & 15u) * 8 + col, y = (ch >> 4) * 8 + row;
                    unsigned int swizzled = ((y / 8) * 8 + x / 16) * 128 + (y & 7) * 16 + (x & 15);
                    print_pixels[swizzled] = !!(msx[ch * 8 + row] & (0x80u >> col));
                }
        print_texture.source = msx; print_texture.source_size = sizeof(msx);
        print_texture.pixels = print_pixels; print_texture.palette[1] = 0xffffffffu;
        print_texture.width = print_texture.height = print_texture.tile_width = 128;
        print_texture.tile_count = 1;
    }
    float origin = (float)(int16_t)((uint32_t)location >> 16) * print_size;
    float x = origin, y = (float)(int16_t)location * print_size;
    for (size_t i = 0; i < length; ++i) {
        unsigned int ch = (unsigned char)string[i];
        if (ch == '\n') { x = origin; y += print_size; continue; }
        if (ch == '\r') { x = origin; continue; }
        if (ch == '\t') { x += 4 * print_size; continue; }
        if (ch != ' ' && x < 640 && y < 480 && x + print_size > 0 && y + print_size > 0) {
            if (draw_count == DRAW_LIMIT) recvxGameBootUnsupported("debug print draw queue exhausted");
            float left = fmaxf(x, 0), top = fmaxf(y, 0);
            float right = fminf(x + print_size, 640), bottom = fminf(y + print_size, 480);
            float u = (ch & 15u) * 8.0f, v = (ch >> 4) * 8.0f;
            draws[draw_count++] = (RecvxBootDraw){.quad = {.x1=left,.y1=top,.x2=right,.y2=bottom,
                .u1=(u + (left-x)*8/print_size)/128, .v1=(v + (top-y)*8/print_size)/128,
                .u2=(u + (right-x)*8/print_size)/128, .v2=(v + (bottom-y)*8/print_size)/128},
                .depth=1.0f, .color=print_color, .texture=&print_texture};
        }
        x += print_size;
    }
}
/* The retail engine has one active room-texture save. Its caller reserves the
   returned arena span until restore. PSP textures already live in RAM, but
   their owned copies must survive the menu's release/reuse of texture slots. */
static struct {
    NJS_TEXLIST *list;
    unsigned char *first;
    size_t bytes;
    unsigned int count;
} texture_save;
typedef struct {
    uint32_t magic, source_bytes, pixel_bytes, global_index, attributes, bank;
    uint32_t palette[256];
} SavedTexture;
#define SAVED_TEXTURE_MAGIC UINT32_C(0x54585053)
NJS_TEXMEMLIST *Ps2_current_texmemlist;
static unsigned int descriptor_slot(NJS_TEXMEMLIST *descriptor);

void Init_PS2_SAVE_TEX(void) { memset(&texture_save, 0, sizeof(texture_save)); }

static int texture_arena(const void *pointer, size_t bytes)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem;
    uintptr_t end = sys ? (uintptr_t)sys->endp : 0;
    return pointer && first && end >= first && p >= first && p <= end && bytes <= end - p;
}

static int texture_overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return an && bn && (x <= y ? y - x < an : x - y < bn);
}

char *bhCopyTexmem2MainmemSub(NJS_TEXLIST *list, char *destination)
{
    size_t bytes = 0;
    if (!list || list->nbTexture > TEXTURE_LIMIT || (list->nbTexture && !list->textures))
        recvxGameBootUnsupported("invalid room texture backup list");
    for (unsigned int i = 0; i < list->nbTexture; ++i) {
        const RecvxBootTexture *texture = recvxGameBootRenderTextureSelect(list, i);
        size_t record = sizeof(SavedTexture) + texture->source_size + texture->width * texture->height;
        if (record < texture->source_size || bytes > SIZE_MAX - record)
            recvxGameBootUnsupported("room texture backup size overflow");
        bytes += record;
    }
    if (!texture_arena(destination, bytes) ||
        texture_overlap(destination, bytes, list, sizeof(*list)) ||
        texture_overlap(destination, bytes, list->textures, list->nbTexture * sizeof(*list->textures)))
        recvxGameBootUnsupported("room texture backup exceeds original arena or overlaps list");
    unsigned char *cursor = (unsigned char *)destination;
    for (unsigned int i = 0; i < list->nbTexture; ++i) {
        const RecvxBootTexture *texture = recvxGameBootTextureGet(list, i);
        NJS_TEXMEMLIST *descriptor = (NJS_TEXMEMLIST *)(uintptr_t)list->textures[i].texaddr;
        SavedTexture record = {.magic = SAVED_TEXTURE_MAGIC,
            .source_bytes = (uint32_t)texture->source_size,
            .pixel_bytes = texture->width * texture->height,
            .global_index = descriptor->globalIndex, .attributes = list->textures[i].attr,
            .bank = descriptor->bank};
        memcpy(record.palette, texture->palette, sizeof(record.palette));
        memcpy(cursor, &record, sizeof(record)); cursor += sizeof(record);
        memcpy(cursor, texture->source, record.source_bytes); cursor += record.source_bytes;
        memcpy(cursor, texture->pixels, record.pixel_bytes); cursor += record.pixel_bytes;
    }
    texture_save.list = list; texture_save.first = (unsigned char *)destination;
    texture_save.bytes = bytes; texture_save.count = list->nbTexture;
    if (list->nbTexture) njSetTexture(list);
    return (char *)cursor;
}

void bhCopyMainmem2Texmem(NJS_TEXLIST *list)
{
    if (!list || texture_save.list != list || list->nbTexture != texture_save.count ||
        list->nbTexture > TEXTURE_LIMIT || (list->nbTexture && !list->textures) ||
        !texture_arena(texture_save.first, texture_save.bytes))
        recvxGameBootUnsupported("missing or mismatched room texture backup");
    size_t offset = 0;
    /* Validate the entire saved span before releasing any live texture. */
    for (unsigned int i = 0; i < list->nbTexture; ++i) {
        SavedTexture record;
        if (offset > texture_save.bytes || sizeof(record) > texture_save.bytes - offset)
            recvxGameBootUnsupported("truncated room texture backup");
        memcpy(&record, texture_save.first + offset, sizeof(record)); offset += sizeof(record);
        if (record.magic != SAVED_TEXTURE_MAGIC || record.bank > 255 ||
            record.source_bytes < 64 || record.pixel_bytes < 128 || record.pixel_bytes > 1024u * 512u ||
            record.source_bytes > texture_save.bytes - offset ||
            record.pixel_bytes > texture_save.bytes - offset - record.source_bytes)
            recvxGameBootUnsupported("invalid room texture backup record");
        offset += record.source_bytes + record.pixel_bytes;
    }
    if (offset != texture_save.bytes) recvxGameBootUnsupported("room texture backup extent mismatch");
    if (draw_count || recvxGameBootRenderPending()) recvxGameBootPresent();
    offset = 0;
    for (unsigned int i = 0; i < list->nbTexture; ++i) {
        SavedTexture record;
        memcpy(&record, texture_save.first + offset, sizeof(record));
        offset += sizeof(record) + record.source_bytes + record.pixel_bytes;
        /* A released descriptor slot may now belong to menu artwork. Only
           release the original still-live binding, never a reused slot. */
        uintptr_t address = list->textures[i].texaddr, first = (uintptr_t)descriptors;
        if (address >= first && address - first < sizeof(descriptors) &&
            !((address - first) % sizeof(*descriptors))) {
            unsigned int slot = (unsigned int)((address - first) / sizeof(*descriptors));
            if (textures[slot].source && descriptors[slot].globalIndex == record.global_index &&
                descriptors[slot].bank == record.bank) {
                recvxBootTextureFree(&textures[slot]);
                memset(&descriptors[slot], 0, sizeof(descriptors[slot]));
            }
        }
    }
    offset = 0;
    for (unsigned int i = 0; i < list->nbTexture; ++i) {
        SavedTexture record;
        memcpy(&record, texture_save.first + offset, sizeof(record)); offset += sizeof(record);
        recvxGameBootTextureInstall(list, i, texture_save.first + offset,
            record.source_bytes, 0, record.global_index, record.attributes, record.bank);
        unsigned int slot = descriptor_slot((NJS_TEXMEMLIST *)(uintptr_t)list->textures[i].texaddr);
        if (textures[slot].width * textures[slot].height != record.pixel_bytes)
            recvxGameBootUnsupported("room texture backup pixel extent mismatch");
        offset += record.source_bytes;
        memcpy(textures[slot].pixels, texture_save.first + offset, record.pixel_bytes);
        memcpy(textures[slot].palette, record.palette, sizeof(record.palette));
        offset += record.pixel_bytes;
    }
    Init_PS2_SAVE_TEX();
    if (list->nbTexture) njSetTexture(list);
}

static unsigned int descriptor_slot(NJS_TEXMEMLIST *descriptor)
{
    uintptr_t address = (uintptr_t)descriptor, first = (uintptr_t)descriptors;
    if (address < first || address - first >= sizeof(descriptors) ||
        (address - first) % sizeof(*descriptor))
        recvxGameBootUnsupported("invalid original texture descriptor");
    return (unsigned int)((address - first) / sizeof(*descriptor));
}

void njSetTextureInfo(NJS_TEXINFO *info, Uint16 *data, Sint32 type, Sint32 width, Sint32 height)
{
    memset(info, 0, sizeof(*info)); info->texaddr = data;
    info->texsurface.Type = type << 16;
    info->texsurface.nWidth = width; info->texsurface.nHeight = height;
}
void njSetTextureName(NJS_TEXNAME *name, void *info, Uint32 index, Uint32 attr)
{ name->filename = info; name->texaddr = index; name->attr = attr; }
int Ps2CheckTextureAlpha(void *data)
{
    /* Alpha is converted in the PSP-owned palette, never in the source CLUT. */
    if (recvxGameBootSourceRemaining(data) < 256)
        recvxGameBootUnsupported("TIM2 header exceeds its loaded asset");
    return 0;
}
void recvxGameBootTextureInstall(NJS_TEXLIST *list, unsigned int index,
    const void *data, size_t size, unsigned int palette, unsigned int global_index,
    unsigned int attributes, unsigned int bank)
{
    unsigned int slot;
    NJS_TEXNAME *name;
    if (!list || !list->textures || index >= TEXTURE_LIMIT)
        recvxGameBootUnsupported("texture installation exceeds backend capacity");
    name = &list->textures[index];
    for (slot = 0; slot < TEXTURE_LIMIT && textures[slot].source; ++slot) { }
    if (slot == TEXTURE_LIMIT || recvxBootTextureDecodePalette(data, size, palette, &textures[slot]))
        recvxGameBootUnsupported("unsupported or malformed TIM2 palette variant");
    memset(&descriptors[slot], 0, sizeof(descriptors[slot]));
    descriptors[slot].texinfo.texaddr = textures[slot].pixels;
    descriptors[slot].texinfo.texsurface.pSurface = (Uint32 *)textures[slot].source;
    descriptors[slot].texinfo.texsurface.nWidth = textures[slot].width;
    descriptors[slot].texinfo.texsurface.nHeight = textures[slot].height;
    descriptors[slot].texinfo.texsurface.BitDepth =
        textures[slot].source[(textures[slot].source[5] ? 128 : 16) + 19] == 4 ? 4 : 8;
    descriptors[slot].texinfo.texsurface.TextureSize = textures[slot].width * textures[slot].height /
        (descriptors[slot].texinfo.texsurface.BitDepth == 4 ? 2 : 1);
    descriptors[slot].texinfo.texsurface.Type =
        (descriptors[slot].texinfo.texsurface.BitDepth == 4 ? 0x500u : 0x700u) << 16;
    descriptors[slot].globalIndex = global_index;
    descriptors[slot].bank = bank;
    descriptors[slot].count = 1;
    name->filename = NULL; name->attr = attributes;
    name->texaddr = (Uint32)(uintptr_t)&descriptors[slot];
    recvxGameBootLog("TIM2 index=%u size=%ux%u tiles=%u palette=%u bank=%u", global_index,
        textures[slot].width, textures[slot].height, textures[slot].tile_count, palette, bank);
}

const RecvxBootTexture *recvxGameBootTextureGet(NJS_TEXLIST *list, unsigned int index)
{
    if (!list || !list->textures || index >= list->nbTexture)
        recvxGameBootUnsupported("texture query exceeds its list");
    return &textures[descriptor_slot((NJS_TEXMEMLIST *)(uintptr_t)list->textures[index].texaddr)];
}

const char *recvxEngineOriginalTextureFault(void)
{ return NULL; /* Invalid operations abort their protected update immediately. */ }
int recvxEngineOriginalTextureDescriptorValid(NJS_TEXLIST *list, unsigned int index)
{
    if (!recvxGameBootTextureGet(list, index)->source)
        recvxGameBootUnsupported("texture descriptor has no owned surface");
    return 1;
}

void njSetPaletteBankNum(Uint32 number, Sint32 bank)
{
    unsigned int slot, colors;
    if (!current_list || number >= current_list->nbTexture || bank < 0 || bank >= 256)
        recvxGameBootUnsupported("invalid texture palette bank selection");
    slot = descriptor_slot((NJS_TEXMEMLIST *)(uintptr_t)current_list->textures[number].texaddr);
    colors = descriptors[slot].texinfo.texsurface.BitDepth == 4 ? 16 : 256;
    if ((unsigned int)bank * 16u + colors > 4096)
        recvxGameBootUnsupported("texture palette bank exceeds palette storage");
    if (draw_count || recvxGameBootRenderPending()) {
        const RecvxBootTexture *snapshot = recvxGameBootRenderSnapshotTexture(&textures[slot]);
        for (size_t i = 0; i < draw_count; ++i)
            if (draws[i].texture == &textures[slot]) draws[i].texture = snapshot;
    }
    memcpy(textures[slot].palette, palbuf + bank * 16, colors * sizeof(Uint32));
    descriptors[slot].bank = bank;
}

Sint32 njLoadTexture(NJS_TEXLIST *list)
{
    unsigned int i;
    if (!list || !list->textures || list->nbTexture > TEXTURE_LIMIT)
        recvxGameBootUnsupported("invalid ADV texture list");
    for (i = 0; i < list->nbTexture; ++i) {
        NJS_TEXNAME *name = &list->textures[i];
        NJS_TEXINFO *info = name->filename;
        unsigned int global_index = name->texaddr;
        if (!info) recvxGameBootUnsupported("missing ADV texture info");
        recvxGameBootTextureInstall(list, i, info->texaddr,
            recvxGameBootSourceRemaining(info->texaddr), 0, global_index, name->attr, 0);
    }
    return 0;
}
Sint32 njSetTexture(NJS_TEXLIST *list)
{ recvxGameBootRenderTextureList(list); current_list = list; return 0; }
Sint32 njSetTextureNum(Uint32 number)
{
    if (!current_list || number >= current_list->nbTexture)
        recvxGameBootUnsupported("ADV selected a texture outside its list");
    current_number = number;
    Ps2_current_texmemlist = (NJS_TEXMEMLIST *)(uintptr_t)current_list->textures[number].texaddr;
    descriptor_slot(Ps2_current_texmemlist);
    recvxGameBootRenderTextureSelected(recvxGameBootRenderTextureSelect(current_list, number));
    return 0;
}
Sint32 njSetTextureNumG(Uint32 global_index)
{
    for (unsigned int i = 0; i < TEXTURE_LIMIT; ++i)
        if (textures[i].source && descriptors[i].globalIndex == global_index) {
            Ps2_current_texmemlist = &descriptors[i];
            recvxGameBootRenderTextureSelected(&textures[i]);
            return 0;
        }
    recvxGameBootUnsupported("original global texture index is not loaded");
}
Sint32 njReleaseTexture(NJS_TEXLIST *list)
{
    unsigned int i;
    if (!list || list->nbTexture > TEXTURE_LIMIT)
        recvxGameBootUnsupported("invalid ADV texture release");
    for (i = 0; i < list->nbTexture; ++i) {
        unsigned int slot = descriptor_slot((NJS_TEXMEMLIST *)(uintptr_t)list->textures[i].texaddr);
        /* Draws queued earlier this tick must finish before release. */
        if (draw_count || recvxGameBootRenderPending()) recvxGameBootPresent();
        recvxBootTextureFree(&textures[slot]);
        memset(&descriptors[slot], 0, sizeof(descriptors[slot]));
        list->textures[i].texaddr = 0;
    }
    if (current_list == list) { current_list = NULL; Ps2_current_texmemlist = NULL; recvxGameBootRenderTextureList(NULL); }
    return 0;
}
void njReleaseTextureAll(void)
{
    unsigned int i;
    if (draw_count || recvxGameBootRenderPending()) recvxGameBootPresent();
    for (i = 0; i < TEXTURE_LIMIT; ++i) recvxBootTextureFree(&textures[i]);
    memset(descriptors, 0, sizeof(descriptors)); current_list = NULL;
    Ps2_current_texmemlist = NULL;
    recvxGameBootRenderTextureList(NULL);
}
static void reset_extra_texture_state(void);
void recvxGameBootTexturesShutdown(void) { njReleaseTextureAll(); Init_PS2_SAVE_TEX(); reset_extra_texture_state(); }
Uint32 njCalcTexture(Uint32 flag)
{
    /* Retail reports this fixed compatibility capacity, not measured free
       bytes; the movie state machine compares it with exactly one MiB. */
    (void)flag;
    return 1048576;
}
void njGarbageTexture(NJS_TEXMEMLIST *list, Uint32 count)
{
    /* Retail ignores these arguments and compacts its TIM2 arena, retaining
       all live surfaces. PSP surfaces are independent heap allocations:
       release already reclaims each hole, so moving live surfaces would
       invalidate descriptor and queued palette-view pointers unnecessarily. */
    (void)list; (void)count;
    if (draw_count || recvxGameBootRenderPending()) recvxGameBootPresent();
    for (unsigned int slot = 0; slot < TEXTURE_LIMIT; ++slot) {
        if (!textures[slot].source) {
            recvxBootTextureFree(&textures[slot]);
            if (Ps2_current_texmemlist == &descriptors[slot]) {
                Ps2_current_texmemlist = NULL;
                recvxGameBootRenderTextureSelected(NULL);
            }
            memset(&descriptors[slot], 0, sizeof(descriptors[slot]));
        } else if (!textures[slot].pixels ||
            descriptors[slot].texinfo.texaddr != textures[slot].pixels ||
            descriptors[slot].texinfo.texsurface.pSurface != (Uint32 *)textures[slot].source ||
            !descriptors[slot].count) {
            recvxGameBootUnsupported("invalid original garbage texture ownership");
        }
    }
}
void bhGarbageTexture(NJS_TEXMEMLIST *list, unsigned int count) { njGarbageTexture(list, count); }
void njSetPaletteMode(Uint32 mode) { palette_mode = mode; }
Uint32 njGetPaletteMode(void) { return palette_mode; }
void njSetPaletteData(Sint32 offset, Sint32 count, void *data)
{
    /* Only the title's separate shadow bank is used by this backend. Black
       vertex tint implements it without modifying the normal texture palette. */
    if (offset != 16 || count != 16 || !data)
        recvxGameBootUnsupported("unported palette bank upload");
}
void njTextureFilterMode(Int mode) { recvxGameBootRenderTextureFilter(mode); texture_filter = mode != 0; }
void njQuadTextureStart(int trans) { (void)trans; }
void njQuadTextureEnd(void) { }
void njSetQuadTexture(int number, unsigned int color)
{ njSetTextureNum(number); quad_color = color; }
static RecvxBootDraw *next_draw(void)
{
    const RecvxGameBootRenderState *state = recvxGameBootRenderStateGet();
    if (state->polygon_blend_source != 8 || state->polygon_blend_destination != 6)
        recvxGameBootUnsupported("unported 2D blend mode");
    if (draw_count == DRAW_LIMIT) recvxGameBootUnsupported("ADV draw queue exhausted");
    return &draws[draw_count++];
}
void recvxGameBootDrawGradientQuad(const NJS_POINT2 points[4], const NJS_COLOR colors[4], float depth)
{
    if (!points || !colors || !isfinite(depth) || depth < 1)
        recvxGameBootUnsupported("invalid original gradient polygon");
    for (unsigned int i = 0; i < 4; ++i)
        if (!isfinite(points[i].x) || !isfinite(points[i].y))
            recvxGameBootUnsupported("invalid original gradient coordinate");
    RecvxBootDraw *draw = next_draw();
    *draw = (RecvxBootDraw){.quad = {.x1=points[0].x, .y1=points[0].y,
        .x2=points[2].x, .y2=points[2].y}, .depth=depth, .color=colors[0].color, .color_gradient=1};
    memcpy(draw->points, points, sizeof(draw->points));
    for (unsigned int i = 0; i < 4; ++i) draw->vertex_color[i] = colors[i].color;
}
static float reciprocal_depth(float depth, float limit)
{
    if (!isfinite(depth)) recvxGameBootUnsupported("invalid reciprocal UI depth");
    return depth ? fminf(1.0f / depth, limit) : limit;
}
void njDrawQuadTexture(QUAD *quad, float depth)
{
    RecvxBootDraw *draw = next_draw();
    unsigned int slot = descriptor_slot(Ps2_current_texmemlist);
    (void)current_number;
    *draw = (RecvxBootDraw){.quad = *quad, .depth = reciprocal_depth(depth,65535), .color = quad_color,
                           .texture = &textures[slot], .filter = texture_filter};
}

void njColorBlendingMode(Int target, Int mode)
{
    recvxGameBootRenderPolygonBlend(target, mode);
}

void recvxGameBootDrawSelectedTexture(NJS_TEXLIST **list, Uint32 *number)
{
    if (!current_list || current_number >= current_list->nbTexture)
        recvxGameBootUnsupported("missing public 2D texture selection");
    *list = current_list;
    *number = current_number;
}

void njDrawPolygon2DM(NJS_POINT2COL *polygon, int count, float priority, unsigned int attributes)
{
    RecvxBootDraw *draw;
    const RecvxBootTexture *texture;
    unsigned int i;
    if (!polygon || !polygon->p || !polygon->tex || !polygon->col || count != 4 ||
        polygon->num != 1 || attributes != 0x80000060u ||
        current_list != &sys->ef_tlist || current_number >= 4)
        recvxGameBootUnsupported("unported 2D message polygon");
    for (i = 1; i < 4; ++i)
        if (polygon->col[i].color != polygon->col[0].color)
            recvxGameBootUnsupported("unported message color gradient");
    if (polygon->p[0].x != polygon->p[3].x || polygon->p[1].x != polygon->p[2].x ||
        polygon->p[0].y != polygon->p[1].y || polygon->p[2].y != polygon->p[3].y)
        recvxGameBootUnsupported("unported non-rectangular message polygon");
    texture = recvxGameBootTextureGet(current_list, current_number);
    if (texture->width != 512 || texture->height != 512)
        recvxGameBootUnsupported("unexpected system font atlas dimensions");
    if (priority > -1.0f) priority = -1.0f;
    draw = next_draw();
    /* The PS2 font path addresses a 256x256 logical atlas. The PSP retains
       its original 512x512 pixels, so UVs and PS2 half-texel biases double. */
    *draw = (RecvxBootDraw){.quad = {
        .x1 = polygon->p[0].x, .y1 = polygon->p[0].y,
        .x2 = polygon->p[2].x, .y2 = polygon->p[2].y,
        .u1 = (polygon->tex[0].tex.u + 0.5f) * 2 / texture->width,
        .v1 = (polygon->tex[0].tex.v + 0.5f) * 2 / texture->height,
        .u2 = (polygon->tex[2].tex.u + 0.25f) * 2 / texture->width,
        .v2 = (polygon->tex[2].tex.v + 0.25f) * 2 / texture->height},
        .depth = -priority, .color = polygon->col[0].color,
        .texture = texture, .filter = texture_filter};
}
void njDrawPolygon(NJS_POLYGON_VTX *polygon, Int count, Int trans)
{
    RecvxBootDraw *draw;
    (void)trans;
    if (count != 4 || polygon[0].x != polygon[1].x || polygon[2].x != polygon[3].x ||
        polygon[0].y != polygon[2].y || polygon[1].y != polygon[3].y)
        recvxGameBootUnsupported("unported non-rectangular 2D polygon");
    draw = next_draw();
    *draw = (RecvxBootDraw){.quad = {.x1 = polygon[0].x, .y1 = polygon[0].y,
                                    .x2 = polygon[3].x, .y2 = polygon[3].y},
                           .depth = reciprocal_depth(polygon[0].z,65534), .color = polygon[0].col};
}
void recvxGameBootDrawDiscard(void) { draw_count = 0; }

const RecvxBootDraw *recvxGameBootDraws(size_t *count)
{
    size_t i, j;
    /* Depth is positive distance from the screen. Draw distant backgrounds
       before nearer controls/fades; preserve submission order for equal Z. */
    for (i = 1; i < draw_count; ++i) {
        RecvxBootDraw draw = draws[i];
        for (j = i; j > 0 && draws[j - 1].depth < draw.depth; --j)
            draws[j] = draws[j - 1];
        draws[j] = draw;
    }
    *count = draw_count; draw_count = 0;
    return draws;
}
void njSetScreen(NJS_SCREEN *screen) { recvxEngineOriginalViewSetScreen(screen); }

void njDrawSprite2D(NJS_SPRITE *sprite, Int number, Float priority, Uint32 attributes)
{
    if (!sprite || !sprite->tanim || !sprite->tlist || number < 0 || number > 255 ||
        !(attributes & NJD_SPRITE_ALPHA) || (attributes & ~0x2fu) ||
        !isfinite(priority) || !isfinite(sprite->sx) || !isfinite(sprite->sy) ||
        !isfinite(sprite->p.x) || !isfinite(sprite->p.y))
        recvxGameBootUnsupported("invalid original inventory sprite");
    const NJS_TEXANIM *animation = &sprite->tanim[number];
    njSetTexture(sprite->tlist);
    njSetTextureNum(animation->texid);
    const RecvxBootTexture *texture = recvxGameBootTextureGet(sprite->tlist, animation->texid);
    if (texture->tile_count != 1)
        recvxGameBootUnsupported("tiled inventory sprite is not ported");
    float left = -animation->cx * sprite->sx, right = (animation->sx-animation->cx)*sprite->sx;
    float top = -animation->cy * sprite->sy, bottom = (animation->sy-animation->cy)*sprite->sy;
    float x[4] = {left,right,right,left}, y[4] = {top,top,bottom,bottom};
    float ub = attributes & NJD_SPRITE_HFLIP ? -.5f : .5f;
    float vb = attributes & NJD_SPRITE_VFLIP ? -.5f : .5f;
    float u[2] = {fmaxf(0,animation->u1*(texture->width/256.0f)+ub),
                  fmaxf(0,animation->u2*(texture->width/256.0f)+ub)};
    float v[2] = {fmaxf(0,animation->v1*(texture->width/256.0f)+vb),
                  fmaxf(0,animation->v2*(texture->width/256.0f)+vb)};
    if (!(((sprite->ang+0xffffu)&0xc000u)/16384u)) { v[0]-=1; v[1]-=1; }
    if(attributes & NJD_SPRITE_HFLIP){float t=u[0];u[0]=u[1];u[1]=t;}
    if(attributes & NJD_SPRITE_VFLIP){float t=v[0];v[0]=v[1];v[1]=t;}
    unsigned int color=0xffffffffu;
    if (attributes & NJD_SPRITE_COLOR) {
        const RecvxGameBootRenderState *state = recvxGameBootRenderStateGet();
        const NJS_ARGB *c = &state->constant_material;
        color = ((unsigned int)(fminf(1,fmaxf(0,c->a))*255)<<24) |
                ((unsigned int)(fminf(1,fmaxf(0,c->r))*255)<<16) |
                ((unsigned int)(fminf(1,fmaxf(0,c->g))*255)<<8) |
                (unsigned int)(fminf(1,fmaxf(0,c->b))*255);
    }
    RecvxBootDraw *draw=next_draw();
    *draw=(RecvxBootDraw){.depth=fmaxf(1,-priority),.texture=texture,.filter=texture_filter,.color_gradient=1};
    float co=1,si=0;
    if(attributes & NJD_SPRITE_ANGLE){co=cosf(.0000958738f*sprite->ang);si=sinf(.0000958738f*sprite->ang);}
    static const unsigned int ui[4]={0,1,1,0},vi[4]={0,0,1,1};
    for(unsigned int i=0;i<4;++i){
        draw->points[i]=(NJS_POINT2){sprite->p.x+co*x[i]-si*y[i],sprite->p.y+si*x[i]+co*y[i]};
        draw->uv[i]=(NJS_POINT2){u[ui[i]],v[vi[i]]};
        draw->vertex_color[i]=color;
    }
}

void njDrawLine2D(NJS_POINT2COL *polygon, Int count, Float priority, Uint32 attributes)
{
    int vertices = attributes & 2u ? count+1 : count*2;
    if (!polygon || !polygon->p || !polygon->col || count < 1 || vertices > 32 ||
        (attributes & ~0x42u) || !isfinite(priority))
        recvxGameBootUnsupported("invalid inventory 2D line");
    for(int i=0;i<vertices;++i)
        if(!isfinite(polygon->p[i].x)||!isfinite(polygon->p[i].y))
            recvxGameBootUnsupported("invalid inventory 2D line coordinate");
    for(int i=0;i<count;++i){
        int a=attributes&2u?i:i*2,b=a+1;
        float dx=polygon->p[b].x-polygon->p[a].x,dy=polygon->p[b].y-polygon->p[a].y;
        float length=hypotf(dx,dy);
        if(length==0)continue;
        float ox=-dy*.5f/length,oy=dx*.5f/length;
        NJS_POINT2 p[4]={{polygon->p[a].x+ox,polygon->p[a].y+oy},
            {polygon->p[b].x+ox,polygon->p[b].y+oy},{polygon->p[b].x-ox,polygon->p[b].y-oy},
            {polygon->p[a].x-ox,polygon->p[a].y-oy}};
        NJS_COLOR c[4]={polygon->col[a],polygon->col[b],polygon->col[b],polygon->col[a]};
        recvxGameBootDrawGradientQuad(p,c,fmaxf(1,-priority));
    }
}

void SyncPath(void) { /* GU users synchronize before queue storage is reclaimed. */ }
void Ps2DrawOTag(void) { recvxGameBootPresent(); recvxGameBootCaptureSubmitted(); }
void Ps2DrawOTagI(void) { recvxGameBootPresent(); }
int ViewType;

void recvxGameBootDrawTexturedOverlay(NJS_POINT2COL *polygon, Float priority)
{
    const RecvxBootTexture *texture=recvxGameBootRenderStateGet()->selected_texture;
    if (!texture || texture->tile_count!=1)
        recvxGameBootUnsupported("invalid public textured overlay material");
    for(unsigned int i=0;i<4;++i)
        if(!isfinite(polygon->p[i].x)||!isfinite(polygon->p[i].y))
            recvxGameBootUnsupported("invalid public textured overlay coordinate");
    static const unsigned int perimeter[4]={0,1,2,3}, strip[4]={0,2,3,1};
    const unsigned int *order=polygon->p[0].x==polygon->p[1].x &&
        polygon->p[2].x==polygon->p[3].x && polygon->p[0].y==polygon->p[2].y &&
        polygon->p[1].y==polygon->p[3].y ? strip : perimeter;
    RecvxBootDraw *draw=next_draw();
    *draw=(RecvxBootDraw){.depth=fmaxf(1,-priority),.texture=texture,.filter=texture_filter,.color_gradient=1};
    for(unsigned int i=0;i<4;++i){
        unsigned int corner=order[i];
        draw->points[i]=polygon->p[corner];
        draw->vertex_color[i]=polygon->col[corner].color;
        draw->uv[i]=(NJS_POINT2){(polygon->tex[corner].tex.u+.5f)*texture->width/256,
            (polygon->tex[corner].tex.v+.5f)*texture->height/256};
    }
}

/* The original snapshot occupies eight bytes and saves polygon blend modes. */
void njGetSystemAttr(NJS_SYS_ATTR *attributes)
{
    if(!attributes)recvxGameBootUnsupported("missing draw state snapshot");
    const RecvxGameBootRenderState *state=recvxGameBootRenderStateGet();
    unsigned char data[8]={(unsigned char)state->polygon_blend_source,
        (unsigned char)state->polygon_blend_destination,(unsigned char)texture_filter,0,0,0,0,0};
    memcpy(attributes,data,sizeof(data));
}
void njSetSystemAttr(NJS_SYS_ATTR *attributes)
{
    if(!attributes)recvxGameBootUnsupported("missing draw state restore");
    unsigned char data[8];memcpy(data,attributes,sizeof(data));
    njColorBlendingMode(0,data[0]);njColorBlendingMode(1,data[1]);njTextureFilterMode(data[2]);
}

/* PSP surfaces are owned RAM allocations, so a display-mode transition never
   migrates or releases them. Preserve palette/selection around that transition. */
static struct { NJS_TEXLIST *list; unsigned int number, mode; int filter; Uint32 palette[4096]; } texture_stack[2];
static unsigned int texture_stack_depth;
void bhPushAllTexture(void)
{
    if(texture_stack_depth==2)recvxGameBootUnsupported("texture state stack overflow");
    if(draw_count||recvxGameBootRenderPending())recvxGameBootPresent();
    unsigned int n=texture_stack_depth++;
    texture_stack[n].list=current_list;texture_stack[n].number=current_number;
    texture_stack[n].mode=palette_mode;texture_stack[n].filter=texture_filter;
    memcpy(texture_stack[n].palette,palbuf,sizeof(palbuf));
}
void bhPopAllTexture(void)
{
    if(!texture_stack_depth)recvxGameBootUnsupported("texture state stack underflow");
    if(draw_count||recvxGameBootRenderPending())recvxGameBootPresent();
    unsigned int n=--texture_stack_depth;
    njGarbageTexture(NULL,0);
    memcpy(palbuf,texture_stack[n].palette,sizeof(palbuf));
    palette_mode=texture_stack[n].mode;texture_filter=texture_stack[n].filter;
    current_list=texture_stack[n].list;current_number=texture_stack[n].number;
    recvxGameBootRenderTextureList(current_list);
    if(current_list)njSetTextureNum(current_number);
}

static unsigned int capture_state,capture_height;
unsigned int PS2_Render_tex_sub_flag,Ps2_render_width;
int recvxGameBootCaptureActive(void){return capture_state==1;}
void recvxGameBootCaptureBegin(unsigned int height)
{
    if(capture_state||!(height==480||height==512))recvxGameBootUnsupported("nested or invalid scene capture");
    if(draw_count||recvxGameBootRenderPending())recvxGameBootPresent();
    capture_state=1;capture_height=height;
}
void recvxGameBootCaptureSubmitted(void){if(capture_state==1)capture_state=2;}
void njSetRenderWidth(Uint32 width)
{if(width!=512)recvxGameBootUnsupported("invalid scene capture width");Ps2_render_width=width;}
static void capture_texture(unsigned int slot)
{
    if(capture_state!=2)recvxGameBootUnsupported("scene capture has not been rendered");
    RecvxBootTexture *t=&textures[slot];
    if(!t->rgba8888){
        unsigned char *pixels=memalign(64,512u*512u*4u);
        if(!pixels)recvxGameBootUnsupported("scene capture allocation failed");
        memset(pixels,0,512u*512u*4u);free(t->pixels);t->pixels=pixels;
        t->rgba8888=1;t->width=t->height=t->tile_width=512;t->tile_count=1;
    }
    t->capture_scale_u=480.0f/512;t->capture_scale_v=272.0f/capture_height;
    recvxGameBootCaptureCopy(t->pixels);
    descriptors[slot].texinfo.texaddr=t->pixels;
    descriptors[slot].texinfo.texsurface.nWidth=descriptors[slot].texinfo.texsurface.nHeight=512;
    capture_state=0;
}
void njRenderTextureNum(Uint32 number)
{
    if(!current_list||number>=current_list->nbTexture)recvxGameBootUnsupported("invalid scene capture texture");
    capture_texture(descriptor_slot((NJS_TEXMEMLIST *)(uintptr_t)current_list->textures[number].texaddr));
}
void njRenderTextureNumG(Uint32 index)
{
    for(unsigned int i=0;i<TEXTURE_LIMIT;++i)if(textures[i].source&&descriptors[i].globalIndex==index){capture_texture(i);return;}
    recvxGameBootUnsupported("scene capture global texture is not loaded");
}
void PS2_Render_Tex_Sub(void){PS2_Render_tex_sub_flag=1;njRenderTextureNumG(2);}

static void reset_extra_texture_state(void){texture_stack_depth=capture_state=capture_height=0;PS2_Render_tex_sub_flag=Ps2_render_width=0;}
