#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/ps2_texture.h"
#include "ps2/veronica/prog/sub1.h"

void recvxGameBootInventoryTexturePrepare(void)
{
    if (!sys || !sys->subtxp)
        recvxGameBootUnsupported("missing inventory texture payload");
    int count = recvxGameBootPvpTextureCount(sys->subtxp, recvxGameBootSourceRemaining(sys->subtxp));
    if (count <= 0 || (unsigned int)count > sizeof(swork.subtx) / sizeof(swork.subtx[0]))
        recvxGameBootUnsupported("invalid inventory texture payload or descriptor count");
}

static int inventory_cursor(const void *pointer)
{
    uintptr_t p = (uintptr_t)pointer;
    return pointer && freemem && p >= (uintptr_t)freemem && p <= (uintptr_t)sys->memp &&
        p <= (uintptr_t)sys->endp;
}

void recvxGameBootInventoryTaskPrepare(void)
{
    if (!sys) recvxGameBootUnsupported("missing inventory task state");
    if ((sys->ts_flg & 0x200u) || (swork.subscreenmode & 0x40u)) {
        if (!inventory_cursor(sys->memp)) recvxGameBootUnsupported("invalid inventory entry cursor");
        return;
    }
    if (!(swork.statusflg & 0x80000u)) return;
    if (swork.flgchk & 2u) {
        if (!inventory_cursor(swork.subp)) recvxGameBootUnsupported("invalid inventory return cursor");
        return;
    }
    if (swork.taskloop > 4) recvxGameBootUnsupported("invalid inventory close phase");
    if (!swork.taskloop && !(swork.flgchk & 8u)) {
        if (swork.subtx_list.textures != swork.subtx ||
            swork.subtx_list.nbTexture > sizeof(swork.subtx) / sizeof(swork.subtx[0]))
            recvxGameBootUnsupported("invalid inventory texture release list");
    }
    if (swork.taskloop == 4 && !sys->mn_md0 && !sys->mn_md1 && !sys->mn_md2 && !sys->mn_md3) {
        if (!rom || !plp || (!(swork.flgchk & 8u) && !inventory_cursor(sys->sbs_sp)))
            recvxGameBootUnsupported("invalid inventory gameplay return state");
    }
}

static unsigned int u16(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static uint32_t u32(const unsigned char *p)
{ return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int span(size_t size, size_t offset, size_t count)
{ return offset <= size && count <= size - offset; }

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(ML_WORK) == 24, "effect model work must match the original ABI");
_Static_assert(sizeof(NJS_CNK_OBJECT) == 52, "effect object disk stride changed");
_Static_assert(sizeof(NJS_CNK_MODEL) == 24, "effect model disk stride changed");
_Static_assert(sizeof(O_WORK) == 80, "effect matrix work disk stride changed");
#endif

int recvxGameBootValidateModel(const unsigned char *data, size_t size)
{
    unsigned int body, objects, offset, tex, i;
    const unsigned char *p;
    size_t remaining;
    if (size < 16 || memcmp(data, "MDL", 3)) return -1;
    body = u16(data + 4); objects = u16(data + 6);
    offset = u32(data + 12); tex = u32(data + 8);
    if (body < 16 || (body & 3) || body > size || objects > 128) return -1;
    p = data + body; remaining = size - body;
    if (objects == 0) {
        if (offset != UINT32_MAX) return -1;
    } else if (!(data[3] & 0x80) || (offset & 3) ||
               !span(remaining, offset, objects * 52u)) return -1;
    for (i = 0; i < objects; ++i) {
        const unsigned char *object = p + offset + i * 52u;
        unsigned int model = u32(object + 4), link;
        unsigned int axis;
        float radius;
        for (axis = 0; axis < 3; ++axis) {
            float position, scale;
            memcpy(&position, object + 8 + axis * 4, 4);
            memcpy(&scale, object + 32 + axis * 4, 4);
            if (!isfinite(position) || !isfinite(scale)) return -1;
        }
        for (axis = 0; axis < 2; ++axis) {
            link = u32(object + 44 + axis * 4);
            if (link != UINT32_MAX &&
                (link < offset || link - offset >= objects * 52u || (link - offset) % 52u))
                return -1;
            /* Retail effect hierarchies only point forward. Reject cycles
               before exposing them to the original recursive model callers. */
            if (link != UINT32_MAX && link <= offset + i * 52u) return -1;
        }
        if (model == UINT32_MAX) continue;
        if ((model & 3) || !span(remaining, model, 24)) return -1;
        memcpy(&radius, p + model + 20, 4);
        if (!isfinite(radius) || radius >= 0) return -1; /* Not already relocated. */
        link = u32(p + model);
        if (link != UINT32_MAX && ((link & 3) || !span(remaining, link, 4))) return -1;
        link = u32(p + model + 4);
        if (link != UINT32_MAX && ((link & 1) || !span(remaining, link, 2))) return -1;
    }
    if (tex != UINT32_MAX) {
        unsigned int names, count;
        if ((tex & 3) || !span(remaining, tex, 8)) return -1;
        names = u32(p + tex); count = u32(p + tex + 4);
        if (count > 128 || (names & 3) || !span(remaining, names, count * 12u)) return -1;
        for (i = 0; i < count; ++i)
            if (!span(remaining, u32(p + names + i * 12u), 1)) return -1;
    }
    return 0;
}

int recvxGameBootValidateEffectSource(const void *source, size_t size)
{
    const unsigned char *data = source;
    size_t cursor = 0;
    unsigned int i;
    if (!data) return -1;
    for (i = 0; i < 4; ++i) {
        unsigned int record_size;
        if (!span(size, cursor, 4)) return -1;
        record_size = u32(data + cursor); cursor += 4;
        if (!span(size, cursor, record_size) || recvxGameBootValidateModel(data + cursor, record_size) ||
            !u16(data + cursor + 6)) return -1;
        cursor += record_size;
    }
    /* The original initializer consumes one final size word, then aligns the
       PVP stream in memory (SYSTEM requests already start on a 64-byte boundary). */
    if (!span(size, cursor, 4)) return -1;
    cursor = (cursor + 4 + 31u) & ~(size_t)31u;
    if (!span(size, cursor, 32) || u32(data + cursor) != 0x00494c50u) return -1;
    return 0;
}

void recvxGameBootEffectPrepare(void)
{
    if (recvxGameBootValidateEffectSource(sys->memp, recvxGameBootSourceRemaining(sys->memp)))
        recvxGameBootUnsupported("invalid SYSTEM effect model records");
#if UINTPTR_MAX > UINT32_MAX
    recvxGameBootUnsupported("effect model relocation requires the original 32-bit ABI");
#else
    recvxGameBootLog("effect source validated: four models, original 32-bit relocation");
#endif
}

/* PVP archives wrap TIM2 files in a 32-byte transport header. Preserve the
   unmodified retail input and decode its actual inner TIM2, not the wrapper. */
int bhSetMemPvpTexture(NJS_TEXLIST *list, unsigned char *data, int offset)
{
    size_t remaining = recvxGameBootSourceRemaining(data), cursor = 0;
    unsigned int palettes[64], palette_count = 0, count = 0;
    if (!list || !list->textures || offset < 0 || offset >= 128)
        recvxGameBootUnsupported("invalid effect texture list");
    for (;;) {
        unsigned int code, size;
        const unsigned char *payload, *picture;
        unsigned int i, picture_offset, format, colors, global_index;
        if (!span(remaining, cursor, 4)) recvxGameBootUnsupported("PVP end marker missing");
        code = u32(data + cursor);
        if (code == UINT32_MAX) {
            recvxGameBootLog("effect PVP decoded: %u texture descriptors", count);
            return (int)count;
        }
        if (!span(remaining, cursor, 32)) recvxGameBootUnsupported("PVP chunk header truncated");
        size = u32(data + cursor + 4); cursor += 32;
        if (!span(remaining, cursor, size)) recvxGameBootUnsupported("PVP chunk exceeds loaded resource");
        payload = data + cursor;
        if (code == 0x00494c50u) {
            if (size < 4) recvxGameBootUnsupported("PLIO count truncated");
            palette_count = u32(payload);
            if (palette_count > 64 || palette_count > (size - 4u) / 8u)
                recvxGameBootUnsupported("PLIO entries exceed their chunk");
            for (i = 0; i < palette_count; ++i) palettes[i] = u32(payload + 8 + i * 8u);
        } else if (code == 0x324d4954u) {
            size_t tim_size = size;
            if (size >= 32 && !memcmp(payload, "TIM2", 4) && payload[4] == 0x80) {
                payload += 32; tim_size -= 32;
            }
            if (tim_size < 16 || memcmp(payload, "TIM2", 4) || payload[4] != 4 || payload[5] > 1)
                recvxGameBootUnsupported("invalid PVP TIM2 wrapper");
            picture_offset = payload[5] ? 128 : 16;
            if (!span(tim_size, picture_offset, 48)) recvxGameBootUnsupported("PVP TIM2 picture truncated");
            picture = payload + picture_offset; format = picture[16];
            colors = picture[19] == 4 ? 16 : picture[19] == 5 ? 256 : 0;
            global_index = payload[5] ? u32(payload + 8) : 0;
            if (!colors) recvxGameBootUnsupported("unsupported effect TIM2 type");
            if (format < 5 || format >= 9) {
                recvxGameBootTextureInstall(list, offset + count++, payload, tim_size, 0,
                    global_index, 0x40800000u, 0);
            } else {
                if (!palette_count) recvxGameBootUnsupported("indexed PVP texture has no PLIO table");
                for (i = 0; i < palette_count; ++i) {
                    unsigned int entry = palettes[i], bank = (entry >> 8) & 255;
                    unsigned int palette_offset;
                    RecvxBootTexture palette_texture = {0};
                    if (bank == 255) {
                        bank = (unsigned int)sys->ef_pbnk;
                        sys->ef_pbnk += (int)(colors / 16);
                        if (sys->ef_pbnk >= 64) sys->ef_pbnk -= 32;
                    }
                    palette_offset = (bank & 63u) * 16 + (entry >> 16) + (bank / 64u) * 1024;
                    if (palette_offset > 4096u - colors ||
                        recvxBootTextureDecodePalette(payload, tim_size, i, &palette_texture))
                        recvxGameBootUnsupported("invalid PVP palette variant");
                    memcpy(palbuf + palette_offset, palette_texture.palette, colors * sizeof(Uint32));
                    recvxBootTextureFree(&palette_texture);
                    sys->gm_flg |= 4;
                    if (entry & 255) continue; /* Palette-only entries still advance the source CLUT. */
                    recvxGameBootTextureInstall(list, offset + count++, payload, tim_size, i,
                        global_index & 0x03ffffffu,
                        i == 0 ? 0x41000000u : 0x41010000u, bank);
                }
            }
        } else recvxGameBootUnsupported("unsupported PVP chunk type");
        cursor += size;
    }
}
