#include <limits.h>
#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/expand.h"
#include "psp/room.h"
#include "psp/event_data.h"
#include "ps2/veronica/prog/main.h"

int recvxGameBootExpand(char *source, unsigned char *destination)
{
    uintptr_t p = (uintptr_t)source, output = (uintptr_t)destination;
    uintptr_t first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    size_t source_size = recvxGameBootSourceRemaining(source), consumed, written;
    RecvxRoomInfo info;
    RecvxExpandResult result;
    /* The original monitor loads compressed input at the arena's upper end
       and expands into the current low cursor. Stop before they can collide. */
    if (!source_size || p < first || p > end || source_size > end - p ||
        output < first || output >= p || destination != sys->memp || source != (char *)sys->rdtp)
        recvxGameBootUnsupported("invalid original room expansion buffers");
    result = recvxExpandBounded(source, source_size, destination, p - output, &consumed, &written);
    if (result != RECVX_EXPAND_OK || consumed != source_size || !written || written > INT_MAX)
        recvxGameBootUnsupported("invalid or oversized compressed room stream");
    if (recvxInspectRoom(destination, written, &info))
        recvxGameBootUnsupported("invalid expanded room header or arrays");
    recvxGameBootRegisterSource(destination, written);
    recvxGameBootLog("room expanded: input=%u output=%u version=%.2f enemies=%u objects=%u items=%u",
        (unsigned int)source_size, (unsigned int)written, info.version,
        info.counts[RECVX_ROOM_COUNT_ENEMY], info.counts[RECVX_ROOM_COUNT_OBJECT], info.counts[RECVX_ROOM_COUNT_ITEM]);
    return (int)written;
}

static uint32_t word(const unsigned char *p)
{ return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned int half(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static int span(size_t size, size_t offset, size_t count)
{ return offset <= size && count <= size - offset; }

/* Match the original npCnkFlatOff scan, without mutating its chunk flags. */
static int flat_chunks(const unsigned char *model, size_t size)
{
    unsigned int header = half(model + 4), objects = half(model + 6), i;
    const unsigned char *base = model + header;
    size_t remaining = size - header;
    unsigned char incoming[128] = {0};
    for (i = 0; i < objects; ++i) {
        const unsigned char *object = base + word(model + 12) + i * 52u;
        unsigned int chunk = word(base + word(model + 12) + i * 52u + 4);
        unsigned int j;
        size_t cursor;
        for (j = 0; j < 2; ++j) {
            unsigned int link = word(object + 44 + j * 4u);
            if (link != UINT32_MAX && ++incoming[(link - word(model + 12)) / 52u] > 1) return -1;
        }
        if (chunk == UINT32_MAX) continue;
        cursor = word(base + chunk + 4);
        if (cursor == UINT32_MAX) return -1;
        for (;;) {
            unsigned int code;
            if (!span(remaining, cursor, 2)) return -1;
            code = base[cursor]; cursor += 2;
            if (code == 255) break;
            if (code == 8) {
                if (!span(remaining, cursor, 2)) return -1;
                cursor += 2;
            } else if ((code >= 64 && code < 67) || (code >= 17 && code < 24) ||
                       (code >= 56 && code < 59)) {
                unsigned int length;
                if (!span(remaining, cursor, 2)) return -1;
                length = half(base + cursor); cursor += 2;
                if (length > INT16_MAX || !span(remaining, cursor, length * 2u)) return -1;
                cursor += length * 2u;
            }
        }
    }
    return 0;
}

static int queue_model(const unsigned char *model, size_t size, const unsigned char *skin,
                       size_t skin_size, unsigned int *textures, unsigned int *count)
{
    uint32_t offset;
    if (recvxGameBootValidateSkinnedModel(model, size, skin, skin_size)) return -1;
    offset = word(model + 8);
    if (offset != UINT32_MAX) {
        if (*count >= 256) return -1;
        textures[(*count)++] = word(model + half(model + 4) + offset + 4);
    }
    return 0;
}

static int enemy_models(const unsigned char *data, size_t size,
                        unsigned int *textures, unsigned int *count, uint16_t *seen)
{
    size_t cursor = 0, skin_size = 0;
    const unsigned char *skin = NULL;
    unsigned int models = 0, variants = 0;
    for (;;) {
        unsigned int length, marker, old_count = *count;
        if (!span(size, cursor, 4)) return -1;
        length = word(data + cursor); cursor += 4;
        if (length == UINT32_MAX) return models && !skin ? 0 : -1;
        if (!length) continue;
        if ((length & 3) || length < 4 || !span(size, cursor, length)) return -1;
        marker = word(data + cursor);
        if (marker == 0x4e494b53u) {
            if (skin) return -1;
            skin = data + cursor + 4; skin_size = length - 4;
        } else if (marker != 0x4b53414du) { /* MASK is retained for original bhInitMask. */
            if (models++ >= 16 || queue_model(data + cursor, length, skin, skin_size, textures, count)) return -1;
            if (*count != old_count) {
                if (variants >= 16) return -1;
                if (*seen & (1u << variants)) *count = old_count;
                else *seen |= 1u << variants;
                ++variants;
            }
            skin = NULL; skin_size = 0;
        }
        cursor += length;
    }
}

int recvxGameBootValidateRoomSource(const void *source, size_t size)
{
    const unsigned char *data = source, *room;
    RecvxRoomInfo info;
    RecvxEventDataView events;
    unsigned int textures[256], count = 0, refs, model_dir, motion_dir, texture_dir;
    unsigned int i, cursor, models, enemies, objects, object_models, items, effects;
    unsigned char object_seen[1300] = {0}, item_seen[200] = {0}, effect_seen[450] = {0};
    uint16_t enemy_seen[100][16] = {{0}};
    if (!data || ((uintptr_t)data & 3) || recvxInspectRoom(data, size, &info) ||
        recvxEventDataInspect(data, size, &events)) return -1;
    room = data + info.rom_offset;
    enemies = info.counts[RECVX_ROOM_COUNT_ENEMY]; objects = info.counts[RECVX_ROOM_COUNT_OBJECT];
    items = info.counts[RECVX_ROOM_COUNT_ITEM]; effects = info.counts[RECVX_ROOM_COUNT_EFFECT];
    if (enemies > 128 || objects > 32 || items > 32 || effects > 256) return -1;
    object_models = objects > 4 ? objects - 4 : 0;
    /* bhSetRoom replaces evtp with the header's event directory; the retail
       ROM prefix contains an authored address in that unused field. */
    for (i = 0; i < 16; ++i)
        if (i != RECVX_ROOM_ARRAY_EVENT && word(room + i * 4u) != UINT32_MAX && word(room + i * 4u) >= size)
            return -1;
    for (i = 0; i < 32; ++i) {
        float height;
        memcpy(&height, room + 0x120 + i * 4, 4);
        if (!isfinite(height)) return -1;
    }
    for (i = 0; i < info.counts[RECVX_ROOM_COUNT_CUT]; ++i) {
        const unsigned char *cut = data + word(room) + i * sizeof(RecvxRoomDiskCutWorkPrefix);
        if (!span(size, word(cut + 4), cut[3] * sizeof(CUT_WRK))) return -1;
    }
    models = 1 + object_models + items;
    for (i = 0; i < enemies; ++i) if (word(data + word(room + 8) + i * 36u) & 1) ++models;
    model_dir = word(data + 20); motion_dir = word(data + 24); texture_dir = word(data + 32);
    if (model_dir < 36 || model_dir >= motion_dir || motion_dir > size ||
        !span(motion_dir, model_dir, models * 4u)) return -1;
    refs = model_dir;
    for (i = 0; i < models; ++i) {
        unsigned int offset = word(data + refs + i * 4u);
        unsigned int end = i + 1 < models ? word(data + refs + (i + 1) * 4u) : motion_dir;
        if ((offset & 3) || offset < model_dir + models * 4u || offset >= end || end > motion_dir) return -1;
    }
    cursor = 0;
    {
        unsigned int offset = word(data + refs), end = models > 1 ? word(data + refs + 4) : motion_dir;
        if (end - offset < 16 || !half(data + offset + 6) ||
            queue_model(data + offset, end - offset, NULL, 0, textures, &count) ||
            flat_chunks(data + offset, end - offset)) return -1;
        ++cursor;
    }
    for (i = 0; i < enemies; ++i) {
        const unsigned char *placement = data + word(room + 8) + i * 36u;
        unsigned int offset, end;
        if (!(word(placement) & 1)) continue;
        if (half(placement + 4) >= 100 || placement[9] >= 16) return -1;
        offset = word(data + refs + cursor * 4u); ++cursor;
        end = cursor < models ? word(data + refs + cursor * 4u) : motion_dir;
        /* Each authored enemy stream is validated separately before the
           original texture-sharing table can expose its relocated pointers. */
        if (enemy_models(data + offset, end - offset, textures, &count,
                         &enemy_seen[half(placement + 4)][placement[9]])) return -1;
    }
    for (i = 0; i < object_models + items; ++i) {
        unsigned int item = i >= object_models, index = item ? i - object_models : i + 4;
        const unsigned char *placement = data + word(room + (item ? 16 : 12)) + index * 36u;
        unsigned int id = half(placement + 4), offset, end, old_count = count;
        const unsigned char *model, *skin = NULL;
        size_t skin_size = 0, model_size;
        unsigned char *seen = item ? item_seen : object_seen;
        if (id >= (item ? 200u : 1300u)) return -1;
        offset = word(data + refs + cursor * 4u); ++cursor;
        end = cursor < models ? word(data + refs + cursor * 4u) : motion_dir;
        model = data + offset; model_size = end - offset;
        if (memcmp(model, "MDL", 3)) {
            unsigned int length;
            if (item || model_size < 12 || word(model + 4) != 0x4e494b53u) return -1;
            length = word(model);
            if (length < 4 || (length & 3) || !span(model_size, 4, length) || model_size - length - 4 < 4) return -1;
            skin = model + 8; skin_size = length - 4;
            model += length + 8; model_size -= length + 8;
        }
        if (queue_model(model, model_size, skin, skin_size, textures, &count)) return -1;
        if (seen[id]) count = old_count;
        else seen[id] = 1;
    }
    if (cursor != models || texture_dir < motion_dir || !span(size, texture_dir, 4)) return -1;
    for (i = 0; i < effects; ++i) {
        unsigned int id = half(data + word(room + 20) + i * 68u + 4);
        if (id >= 450) return -1;
        if (((id >= 30 && id < 100) || id >= 400) && !effect_seen[id]) {
            if (count >= 256) return -1;
            effect_seen[id] = 1; textures[count++] = UINT32_MAX;
        }
    }
    if (word(data + texture_dir) != count || !span(size, texture_dir + 4u, count * 4u)) return -1;
    for (i = 0; i < count; ++i) {
        unsigned int offset = word(data + texture_dir + 4u + i * 4u);
        unsigned int end = i + 1 < count ? word(data + texture_dir + 8u + i * 4u) : (unsigned int)size;
        if (offset < texture_dir + 4u + count * 4u || offset >= end || end > size ||
            recvxGameBootValidatePvp(data + offset, end - offset, textures[i])) return -1;
    }
    return 0;
}

void recvxGameBootRoomPrepare(void)
{
    size_t size = recvxGameBootSourceRemaining(sys->rdtp);
    if (sys->rdtsz <= 0 || size != (size_t)sys->rdtsz ||
        sys->memp != sys->rdtp + size || recvxGameBootValidateRoomSource(sys->rdtp, size))
        recvxGameBootUnsupported("invalid original room relocation records");
#if UINTPTR_MAX > UINT32_MAX
    recvxGameBootUnsupported("room relocation requires the original 32-bit ABI");
#else
    recvxGameBootLog("room source validated: original room and placement relocation");
#endif
}

static size_t bounded_stream(const unsigned char *data, size_t size)
{
    size_t cursor = 0;
    for (;;) {
        unsigned int length;
        if (!span(size, cursor, 4)) return 0;
        length = word(data + cursor); cursor += 4;
        if (length == UINT32_MAX) return cursor;
        if ((length & 3) || !span(size, cursor, length)) return 0;
        cursor += length;
    }
}

void recvxGameBootRoomFinishPrepare(void)
{
    const unsigned char *data = sys->rdtp;
    size_t size = recvxGameBootSourceRemaining(data), required = 0;
    uintptr_t cursor = (uintptr_t)sys->memp, end = (uintptr_t)sys->endp;
    unsigned int directory, texture, i, j, free_effects = 0;
    if (sys->rdtsz <= 0 || size != (size_t)sys->rdtsz || size < 36 ||
        sys->txr_ct != sys->txr_n || rom->ene_n < 0 || rom->ene_n > 128 ||
        rom->obj_n < 0 || rom->obj_n > 32 || rom->itm_n < 0 || rom->itm_n > 32 ||
        rom->eff_n < 0 || rom->eff_n > 256)
        recvxGameBootUnsupported("invalid room finalization state");
    directory = word(data + 24); texture = word(data + 32);
    if (directory >= texture || !span(texture, directory, (rom->ene_n + 3u) * 4u) ||
        cursor != (uintptr_t)data + texture || cursor > end)
        recvxGameBootUnsupported("invalid room motion directory or arena cursor");
#if UINTPTR_MAX > UINT32_MAX
    recvxGameBootUnsupported("room finalization requires the original 32-bit ABI");
#endif
    for (i = 0; i < (unsigned int)rom->ene_n + 3u; ++i) {
        unsigned int offset = word(data + directory + i * 4u);
        if (i <= (unsigned int)rom->ene_n) required += 12288u + 7u;
        if (offset) {
            size_t length;
            if ((offset & 3) || offset < directory + (rom->ene_n + 3u) * 4u || offset >= texture ||
                !(length = bounded_stream(data + offset, texture - offset)))
                recvxGameBootUnsupported("invalid room motion or lip stream");
            if (i <= (unsigned int)rom->ene_n && recvxGameBootValidateMotionStream(data + offset, length))
                recvxGameBootUnsupported("invalid room motion relocation records");
        }
    }
    for (i = 0; i < (unsigned int)rom->ene_n; ++i) {
        BH_PWORK *actor = &ene[i];
        if (actor->id >= 128 || actor->mdl_n > 16 || actor->mlwP != actor->mdl || actor->lkwkp)
            recvxGameBootUnsupported("invalid actor work during room finalization");
        for (j = 0; j < actor->mdl_n; ++j) {
            if (actor->mdl[j].obj_num > 128)
                recvxGameBootUnsupported("actor matrix work exceeds model capacity");
            required += actor->mdl[j].obj_num * 80u + 15u;
        }
        if (actor->id > 90) required += recvxGameBootMaskPlan(actor);
    }
    for (i = 4; i < (unsigned int)rom->obj_n; ++i) required += sys->obwp[i].mlwP->obj_num * 80u + 15u;
    for (i = 0; i < (unsigned int)rom->itm_n; ++i) required += sys->itwp[i].mlwP->obj_num * 80u + 15u;
    if (required > end - cursor) recvxGameBootUnsupported("room finalization exceeds original arena");
    if (sys->mwal_n < 0 || sys->mwal_n > 64 || sys->metc_n < 0 || sys->metc_n > 64 ||
        sys->mflr_n < 0 || sys->mflr_n > 64 ||
        (!(sys->cb_flg & 0x800000) && rom->pos_n && (unsigned int)sys->pos_no >= (unsigned int)rom->pos_n))
        recvxGameBootUnsupported("invalid room collision or spawn state");
    for (i = 0; i < (unsigned int)rom->eff_n; ++i) {
        EF_WRK *effect = &rom->effp[i];
        if (effect->id >= 450 || effect->lkflg < 0 || effect->lkflg > 5 || effect->lkno < 0 || effect->lkono < 0)
            recvxGameBootUnsupported("invalid room effect attachment");
    }
    for (i = 0; i < 512; ++i) if (!(eff[i].flg & 3u)) ++free_effects;
    if (free_effects < (unsigned int)rom->eff_n)
        recvxGameBootUnsupported("room effects exceed available effect work");
    recvxGameBootLog("room finish validated: motions, masks, spawn and %u bytes of work space", (unsigned int)required);
}
