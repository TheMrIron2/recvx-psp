#include <stdlib.h>
#include <string.h>
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
#include "psp/engine_original_texture.h"

#define TEXTURE_LIMIT 256
#define DRAW_LIMIT 512
static RecvxBootTexture textures[TEXTURE_LIMIT];
static NJS_TEXMEMLIST descriptors[TEXTURE_LIMIT];
static NJS_TEXLIST *current_list;
static unsigned int current_number, quad_color, palette_mode;
static int texture_filter;
static RecvxBootDraw draws[DRAW_LIMIT];
static size_t draw_count;
NJS_TEXMEMLIST *Ps2_current_texmemlist;

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
    return 0;
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
void recvxGameBootTexturesShutdown(void) { njReleaseTextureAll(); }
void bhGarbageTexture(NJS_TEXMEMLIST *list, unsigned int count) { (void)list; (void)count; }
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
    if (draw_count == DRAW_LIMIT) recvxGameBootUnsupported("ADV draw queue exhausted");
    return &draws[draw_count++];
}
void njDrawQuadTexture(QUAD *quad, float depth)
{
    RecvxBootDraw *draw = next_draw();
    unsigned int slot = descriptor_slot(Ps2_current_texmemlist);
    (void)current_number;
    *draw = (RecvxBootDraw){.quad = *quad, .depth = depth, .color = quad_color,
                           .texture = &textures[slot], .filter = texture_filter};
}

void njColorBlendingMode(Int target, Int mode)
{
    if ((target == 0 && mode == 8) || (target == 1 && mode == 6)) return;
    recvxGameBootUnsupported("unported 2D blend mode");
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
                           .depth = polygon[0].z, .color = polygon[0].col};
}
const RecvxBootDraw *recvxGameBootDraws(size_t *count)
{
    size_t i, j;
    /* Original PS2 sorts submissions by depth; fades are submitted before art. */
    for (i = 1; i < draw_count; ++i) {
        RecvxBootDraw draw = draws[i];
        for (j = i; j > 0 && draws[j - 1].depth > draw.depth; --j)
            draws[j] = draws[j - 1];
        draws[j] = draw;
    }
    *count = draw_count; draw_count = 0;
    return draws;
}
void njFogDisable(void) { }
void njSetScreen(NJS_SCREEN *screen) { (void)screen; }
