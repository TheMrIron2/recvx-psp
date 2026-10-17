#include <string.h>
#include <math.h>
#include "psp/game_boot.h"
#include "psp/player_asset.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"

static uint32_t word(const unsigned char *p)
{ return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned int half(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static int span(size_t size, size_t offset, size_t count)
{ return offset <= size && count <= size - offset; }

static int player_work_span(const void *pointer, size_t size)
{
    uintptr_t address = (uintptr_t)pointer, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return pointer && !(address & 3u) && address >= first && address <= end && size <= end - address;
}

void recvxGameBootPlayerControlPrepare(void)
{
    uintptr_t model;
    if (!plp) recvxGameBootUnsupported("invalid player control actor");
    if (!(sys->sp_flg & 1u) || (plp->stflg & 0x1000000u)) return;
    model = (uintptr_t)plp->mlwP;
    if (sys->ply_id < 0 || sys->ply_id >= 4 || plp->wpnr_no < 0 || plp->wpnr_no >= 23 ||
        !player_work_span(plp->exp0, sizeof(EXP_WORK)) || !player_work_span(plp->exp1, 124) ||
        model < (uintptr_t)plp->mdl || model - (uintptr_t)plp->mdl >= sizeof(plp->mdl) ||
        (model - (uintptr_t)plp->mdl) % sizeof(ML_WORK))
        recvxGameBootUnsupported("invalid original player control state");
    if (plp->mlwP->obj_num < 22 || plp->mlwP->obj_num > 128 ||
        !player_work_span(plp->mlwP->objP, plp->mlwP->obj_num * sizeof(NJS_CNK_OBJECT)) ||
        !player_work_span(plp->mlwP->owP, plp->mlwP->obj_num * sizeof(O_WORK)) ||
        plp->mtn_no >= 512 || !player_work_span(plp->mnwP, (plp->mtn_no + 1u) * sizeof(MN_WORK)))
        recvxGameBootUnsupported("invalid original player model or motion work");
    if ((plp->flg & 0x100000u) && ((EXP_WORK *)plp->exp0)->yrct <= 0)
        recvxGameBootUnsupported("invalid player turn timer");
}

void recvxGameBootPlayerRestorePrepare(int standing)
{
    if (!sys || !plp || sys->ply_id < 0 || sys->ply_id >= 4 ||
        !player_work_span(plp->exp0, sizeof(EXP_WORK)))
        recvxGameBootUnsupported("invalid player restore work");
    ML_WORK *model = standing ? plp->mlwP : plp->mdl;
    uintptr_t address = (uintptr_t)model, first = (uintptr_t)plp->mdl;
    if (!model || address < first || address - first >= sizeof(plp->mdl) ||
        (address - first) % sizeof(*model) || model->obj_num < 22 || model->obj_num > 128 ||
        !player_work_span(model->objP, model->obj_num * sizeof(*model->objP)) ||
        !player_work_span(model->owP, model->obj_num * sizeof(*model->owP)))
        recvxGameBootUnsupported("invalid player restore model");
    if (!standing) {
        if ((sys->ply_id == 0 || sys->ply_id == 3) &&
            !player_work_span(sys->obwp, 3u * sizeof(*sys->obwp)))
            recvxGameBootUnsupported("invalid player restore equipment");
        return;
    }
    EXP_WORK *expression = (EXP_WORK *)plp->exp0;
    extern const unsigned short PlMtnAct[2][3][7];
    int damage = plp->hp >= 120 ? !!(plp->stflg & 0x280000u) : (plp->hp >= 30 ? 1 : 2);
    if (expression->wpntp < 0 || expression->wpntp >= 2 ||
        !isfinite(plp->px) || !isfinite(plp->py) || !isfinite(plp->pz))
        recvxGameBootUnsupported("invalid player standing selectors");
    unsigned int motion = PlMtnAct[expression->wpntp][damage][0];
    if (motion >= 512 || !player_work_span(plp->mnwPb, (motion + 1u) * sizeof(*plp->mnwPb)) ||
        !plp->mnwPb[motion].frm_num || plp->mnwPb[motion].frm_num > 32767)
        recvxGameBootUnsupported("invalid player standing motion");
}

void recvxGameBootRoomChangePlayerPrepare(void)
{
    /* The original entry selects the persistent player, regardless of plp's
       previous value. Validate that actor before the function changes it. */
    BH_PWORK *actor = &ply;
    if (!sys || sys->ply_id < 0 || sys->ply_id >= 4 ||
        !player_work_span(actor->exp0, sizeof(EXP_WORK)) ||
        !player_work_span(actor->exp1, 124))
        recvxGameBootUnsupported("invalid room-change player work");
    ML_WORK *model = actor->mlwP;
    uintptr_t address = (uintptr_t)model, first = (uintptr_t)actor->mdl;
    if (!model || address < first || address - first >= sizeof(actor->mdl) ||
        (address - first) % sizeof(*model) || model->obj_num < 22 || model->obj_num > 128 ||
        !player_work_span(model->objP, model->obj_num * sizeof(*model->objP)) ||
        !player_work_span(model->owP, model->obj_num * sizeof(*model->owP)) || !actor->txp[0])
        recvxGameBootUnsupported("invalid room-change player model");
    EXP_WORK *expression = (EXP_WORK *)actor->exp0;
    if (expression->wpntp < 0 || expression->wpntp >= 2 ||
        expression->dmlvl < 0 || expression->dmlvl >= 3 ||
        !isfinite(actor->px) || !isfinite(actor->py) || !isfinite(actor->pz))
        recvxGameBootUnsupported("invalid room-change player selectors");
    extern const unsigned short PlMtnAct[2][3][7];
    unsigned int motion = PlMtnAct[expression->wpntp][expression->dmlvl][0];
    if (motion >= 512 || !player_work_span(actor->mnwP, (motion + 1u) * sizeof(*actor->mnwP)) ||
        !actor->mnwP[motion].frm_num || actor->mnwP[motion].frm_num > 32767)
        recvxGameBootUnsupported("invalid room-change player motion");
    if (sys->ply_id == 0 && !player_work_span(sys->obwp, 3u * sizeof(*sys->obwp)))
        recvxGameBootUnsupported("invalid room-change player equipment");
}

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(MN_WORK) == 24, "original motion work ABI changed");
_Static_assert(sizeof(O_WRK) == 1248, "original equipment work ABI changed");
#endif

/* The original recursive skin converter consumes records in hierarchy order.
   Require a tree covering the object table, so shared links cannot consume the
   same skin records twice. Forward links have already been checked. */
static int hierarchy(const RecvxPlayerModelAsset *model, unsigned int object,
                     unsigned int *order, unsigned int *count, unsigned char *seen)
{
    const unsigned char *p;
    unsigned int i;
    if (object >= model->object_count || seen[object]) return -1;
    seen[object] = 1; order[(*count)++] = object;
    p = model->data + model->header_offset + model->object_offset + object * 52u;
    if (word(p) & 0x20000000u) return -1;
    for (i = 0; i < 2; ++i) {
        uint32_t link = word(p + 44 + i * 4);
        if (link != UINT32_MAX && hierarchy(model,
                (link - model->object_offset) / 52u, order, count, seen)) return -1;
    }
    return 0;
}

static int skin_source(const RecvxPlayerModelAsset *model)
{
    const unsigned char *base = model->data + model->header_offset;
    size_t size = model->size - model->header_offset, cursor = 0;
    unsigned int order[128], count = 0, i;
    unsigned char seen[128] = {0};
    unsigned int vertex_counts[128] = {0};
    unsigned char defined[128] = {0};
    if (!model->skin) return 0;
    if (!model->object_count || hierarchy(model, 0, order, &count, seen) ||
        count != model->object_count) return -1;
    /* Preparation defines all skin donors before the original second pass. */
    for (i = 0; i < count; ++i) {
        unsigned int flags, donor, vertices;
        const unsigned char *object = base + model->object_offset + order[i] * 52u;
        uint32_t chunk, vlist;
        if (!span(model->skin_size, cursor, 4)) return -1;
        flags = word(model->skin + cursor); cursor += 4;
        if (flags & ~3u) return -1;
        if (!flags) continue;
        if (!span(model->skin_size, cursor, flags & 2 ? 8 : 4)) return -1;
        donor = word(model->skin + cursor); cursor += 4;
        if (donor >= 128) return -1;
        if (!(flags & 2)) continue;
        vertices = word(model->skin + cursor); cursor += 4;
        if (defined[donor] || !span(model->skin_size, cursor, (size_t)vertices * 4)) return -1;
        chunk = word(object + 4);
        if (chunk == UINT32_MAX) return -1;
        vlist = word(base + chunk);
        if ((vlist & 3) || !span(size, vlist, 64) ||
            half(base + vlist + 6) != vertices ||
            !span(size, vlist + 64u, (size_t)vertices * 32u)) return -1;
        defined[donor] = 1; vertex_counts[donor] = vertices;
        cursor += (size_t)vertices * 4;
    }
    cursor = 0;
    for (i = 0; i < count; ++i) {
        unsigned int flags = word(model->skin + cursor), donor;
        cursor += 4;
        if (!flags) continue;
        donor = word(model->skin + cursor); cursor += 4;
        if ((flags & 1) && !defined[donor]) return -1;
        if (flags & 2) {
            unsigned int vertices = word(model->skin + cursor); cursor += 4;
            if ((flags & 1) && vertex_counts[donor] != vertices) return -1;
            cursor += (size_t)vertices * 4;
        }
    }
    return 0;
}

int recvxGameBootValidateSkinnedModel(const unsigned char *data, size_t size,
    const unsigned char *skin, size_t skin_size)
{
    RecvxPlayerModelAsset model = {0};
    const unsigned char *weights;
    uint32_t count;
    if (recvxGameBootValidateModel(data, size) || size > UINT32_MAX || skin_size > UINT32_MAX)
        return -1;
    if (!skin) return skin_size ? -1 : 0;
    if ((uintptr_t)skin & 3) return -1;
    model.data = data; model.size = (uint32_t)size;
    model.header_offset = half(data + 4); model.object_count = half(data + 6);
    model.object_offset = word(data + 12); model.skin = skin; model.skin_size = (uint32_t)skin_size;
    if (recvxPlayerSkinWeights(&model, 0, 0, &weights, &count) < 0) return -1;
    return skin_source(&model);
}

int recvxGameBootValidatePvp(const unsigned char *data, size_t size, unsigned int textures)
{
    size_t cursor = 0;
    unsigned int entries[64], palettes = 0, count = 0;
    for (;;) {
        unsigned int code, length, chunk_length, i;
        const unsigned char *payload;
        if (!span(size, cursor, 4)) return -1;
        code = word(data + cursor);
        if (code == UINT32_MAX)
            return textures == UINT32_MAX ? (count ? 0 : -1) : (count == textures ? 0 : -1);
        if (!span(size, cursor, 32)) return -1;
        chunk_length = length = word(data + cursor + 4); cursor += 32;
        if (!span(size, cursor, length)) return -1;
        payload = data + cursor;
        if (code == 0x00494c50u) {
            if (length < 4) return -1;
            palettes = word(payload);
            if (palettes > 64 || palettes > (length - 4u) / 8u) return -1;
            for (i = 0; i < palettes; ++i) entries[i] = word(payload + 8 + i * 8u);
        } else if (code == 0x324d4954u) {
            unsigned int picture, format;
            if (length >= 32 && !memcmp(payload, "TIM2", 4) && payload[4] == 0x80) {
                payload += 32; length -= 32;
            }
            if (length < 16 || memcmp(payload, "TIM2", 4) || payload[4] != 4 || payload[5] > 1) return -1;
            picture = payload[5] ? 128 : 16;
            if (!span(length, picture, 48)) return -1;
            format = payload[picture + 16];
            if (format < 5 || format >= 9) ++count;
            else {
                if (!palettes) return -1;
                for (i = 0; i < palettes; ++i) if (!(entries[i] & 255)) ++count;
            }
            if (count > textures || count > 256) return -1;
        } else return -1;
        /* Keep the outer length when the TIM2 wrapper has been stripped. */
        cursor += chunk_length;
    }
}

static int aligned_motions(const RecvxPlayerAssets *assets)
{
    unsigned int i;
    for (i = 0; i < assets->motion_count; ++i) {
        const RecvxPlayerMotionAsset *motion = &assets->motions[i];
        const unsigned char *base;
        unsigned int j;
        if (!motion->present) continue;
        if (((uintptr_t)motion->data & 3) || (half(motion->data + 4) & 3) ||
            (motion->motion_offset & 3) || (motion->data_offset & 3) ||
            (motion->attribute_offset != UINT32_MAX && (motion->attribute_offset & 1))) return -1;
        base = motion->data + half(motion->data + 4);
        for (j = 0; j < motion->object_count * 2u; ++j) {
            unsigned int reference = word(base + motion->data_offset + j * 4u);
            if (reference != UINT32_MAX && (reference & 3)) return -1;
        }
    }
    return 0;
}

int recvxGameBootValidateMotionStream(const unsigned char *data, size_t size)
{
    static const unsigned char empty_models[4] = {255, 255, 255, 255};
    RecvxPlayerAssets assets;
    if (recvxPlayerAssetsParse(empty_models, 4, data, size, NULL, 0, &assets)) return -1;
    return aligned_motions(&assets);
}

int recvxGameBootValidatePlayerSource(const void *source, size_t size)
{
    const unsigned char *data = source, *streams[3];
    size_t cursor = 0, work;
    unsigned int sizes[3], i;
    static const unsigned int capacities[3] = {131072, 393216, 8192};
    RecvxPlayerAssets assets;
    if (!data || ((uintptr_t)data & 63u)) return -1;
    for (i = 0; i < 3; ++i) {
        if (!span(size, cursor, 4)) return -1;
        sizes[i] = word(data + cursor); cursor += 4;
        if ((sizes[i] & 3) || sizes[i] > capacities[i] || !span(size, cursor, sizes[i])) return -1;
        streams[i] = data + cursor; cursor += sizes[i];
    }
    if (recvxPlayerAssetsParse(streams[0], sizes[0], streams[1], sizes[1],
                              streams[2], sizes[2], &assets) ||
        !assets.model_count || assets.model_count > 16 || assets.models[0].object_count < 6 ||
        assets.motion_slots > 100) return -1;
    /* Destination buffers are aligned to 64 bytes, rather than necessarily
       256. Reserve the largest padding the original absolute rounding uses. */
    work = sizes[0] + 256u;
    for (i = 0; i < assets.model_count; ++i) {
        const RecvxPlayerModelAsset *model = &assets.models[i];
        unsigned int j;
        if (((uintptr_t)model->data & 3) || recvxGameBootValidateModel(model->data, model->size) ||
            skin_source(model)) return -1;
        work = (work + 63u) & ~(size_t)63u;
        work += model->object_count * 80u;
        if (work > capacities[0]) return -1;
        if (model->texture_offset != UINT32_MAX) {
            unsigned int length, textures;
            if (!span(size, cursor, 4)) return -1;
            length = word(data + cursor); cursor += 4;
            if (length & 0x80000000u) {
                cursor = (cursor + 31u) & ~(size_t)31u;
                length &= 0x7fffffffu;
            }
            textures = word(model->data + model->header_offset + model->texture_offset + 4);
            if (!span(size, cursor, length) || recvxGameBootValidatePvp(data + cursor, length, textures)) return -1;
            cursor += length;
        }
        /* Skin conversion casts its stream directly to int*. */
        if (model->skin && ((uintptr_t)model->skin & 3)) return -1;
        for (j = 0; j < model->object_count; ++j)
            if (word(model->data + model->header_offset + model->object_offset + j * 52u) & 0x20000000u)
                return -1;
    }
    return aligned_motions(&assets);
}

int recvxGameBootValidateWeaponSource(const void *source, size_t size)
{
    static const unsigned char empty_models[4] = {255, 255, 255, 255};
    const unsigned char *data = source;
    size_t cursor = 0;
    unsigned int side, length;
    RecvxPlayerAssets assets;
    if (!data || ((uintptr_t)data & 63u)) return -1;
    for (side = 0; side < 2; ++side) {
        const unsigned char *model;
        unsigned int model_size, objects = 0, texture_offset = UINT32_MAX, header = 0;
        if (!span(size, cursor, 4)) return -1;
        model_size = word(data + cursor); cursor += 4;
        model = data + cursor;
        if ((model_size & 3) || model_size > 32768 || !span(size, cursor, model_size)) return -1;
        if (model_size) {
            if (((uintptr_t)model & 3) || recvxGameBootValidateModel(model, model_size)) return -1;
            objects = half(model + 6); texture_offset = word(model + 8); header = half(model + 4);
            if (model_size + 255u + objects * 80u > 32768) return -1;
        }
        cursor += model_size;
        if (!span(size, cursor, 4)) return -1;
        length = word(data + cursor); cursor += 4;
        if (length & 0x80000000u) {
            length &= 0x7fffffffu; cursor = (cursor + 63u) & ~(size_t)63u;
        }
        if (!span(size, cursor, length) || (length && !model_size)) return -1;
        if (length && texture_offset != UINT32_MAX &&
            recvxGameBootValidatePvp(data + cursor, length, word(model + header + texture_offset + 4))) return -1;
        cursor += length;
    }
    if (!span(size, cursor, 4)) return -1;
    length = word(data + cursor); cursor += 4;
    if ((length & 3) || length > 65536 || !span(size, cursor, length)) return -1;
    if (!length) return 0;
    if (recvxPlayerAssetsParse(empty_models, 4, data + cursor, length, NULL, 0, &assets) ||
        (100u + assets.motion_slots) * 24u > 12288u) return -1;
    return aligned_motions(&assets);
}

void recvxGameBootWeaponPrepare(void)
{
    unsigned char *buffers[] = {sys->wrmdlp, sys->wlmdlp, sys->plwmtp};
    const unsigned int capacities[] = {32768, 32768, 65536};
    uintptr_t first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    uintptr_t source = (uintptr_t)sys->memp, objects = (uintptr_t)sys->obwp;
    size_t source_size = recvxGameBootSourceRemaining(sys->memp);
    uintptr_t player_model = (uintptr_t)sys->plmdlp;
    uintptr_t player_work = (uintptr_t)plp->mdl[0].owP;
    uintptr_t motion_work = (uintptr_t)sys->plmthp, extra = (uintptr_t)sys->plexwp;
    unsigned int i, j;
    if (plp->wpnr_no < 0 || plp->wpnr_no >= 30 || (objects & 3) || objects < first || objects > end ||
        sizeof(O_WRK) * 2u > end - objects || plp->mlwP != plp->mdl ||
        plp->mdl[0].obj_num < 14 || !plp->mdl[0].owP ||
        !sys->plexwp || plp->exp0 != sys->plexwp || plp->mnwP != (MN_WORK *)sys->plmthp)
        recvxGameBootUnsupported("invalid original weapon initialization state");
    if (player_model < first || player_model > end || 131072u > end - player_model ||
        (player_work & 3) || player_work < player_model || player_work > player_model + 131072u ||
        plp->mdl[0].obj_num > (player_model + 131072u - player_work) / 80u ||
        (motion_work & 3) || motion_work < first || motion_work > end || 12288u > end - motion_work ||
        (extra & 3) || extra < first || extra > end || 124u > end - extra)
        recvxGameBootUnsupported("invalid original weapon player work");
    for (i = 0; i < 3; ++i) {
        uintptr_t buffer = (uintptr_t)buffers[i];
        if ((buffer & 63u) || buffer < first || buffer > end || capacities[i] > end - buffer)
            recvxGameBootUnsupported("invalid original weapon asset buffers");
        if (buffer < source + source_size && source < buffer + capacities[i])
            recvxGameBootUnsupported("weapon asset source overlaps reserved buffers");
        for (j = 0; j < i; ++j)
            if (buffer < (uintptr_t)buffers[j] + capacities[j] &&
                (uintptr_t)buffers[j] < buffer + capacities[i])
                recvxGameBootUnsupported("original weapon asset buffers overlap");
    }
    if (!(sys->ss_flg & 0x100))
        for (i = 0; i < 2; ++i)
            if (sys->obwp[i].mlwP != sys->obwp[i].mdl)
                recvxGameBootUnsupported("invalid previous weapon model work");
    if (recvxGameBootValidateWeaponSource(sys->memp, source_size))
        recvxGameBootUnsupported("invalid SYSTEM weapon asset records");
#if UINTPTR_MAX > UINT32_MAX
    recvxGameBootUnsupported("weapon model relocation requires the original 32-bit ABI");
#else
    recvxGameBootLog("weapon source validated: original equipment setup and relocation");
#endif
}

void recvxGameBootPlayerPrepare(void)
{
    unsigned char *buffers[] = {sys->plmdlp, sys->plmthp, sys->plbmtp, sys->plzmtp};
    const unsigned int capacities[] = {131072, 12288, 393216, 8192};
    unsigned int i, j;
    uintptr_t first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    uintptr_t source = (uintptr_t)sys->memp;
    size_t source_size = recvxGameBootSourceRemaining(sys->memp);
    for (i = 0; i < 4; ++i) {
        uintptr_t buffer = (uintptr_t)buffers[i];
        if ((buffer & 63u) || buffer < first || buffer > end || capacities[i] > end - buffer)
            recvxGameBootUnsupported("invalid original player asset buffers");
        if (buffer < source + source_size && source < buffer + capacities[i])
            recvxGameBootUnsupported("player asset source overlaps reserved buffers");
        for (j = 0; j < i; ++j)
            if (buffer < (uintptr_t)buffers[j] + capacities[j] &&
                (uintptr_t)buffers[j] < buffer + capacities[i])
                recvxGameBootUnsupported("original player asset buffers overlap");
    }
    if (recvxGameBootValidatePlayerSource(sys->memp, recvxGameBootSourceRemaining(sys->memp)))
        recvxGameBootUnsupported("invalid SYSTEM player asset records");
#if UINTPTR_MAX > UINT32_MAX
    recvxGameBootUnsupported("player model relocation requires the original 32-bit ABI");
#else
    recvxGameBootLog("player source validated: original models, skin and motion relocation");
#endif
}
