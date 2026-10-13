#define _GNU_SOURCE
#define u_long recvx_host_u_long
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#undef u_long
#include "psp/game_boot.h"
#include "psp/original_clock.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/adv.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/ps2_texture.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/dread.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/message.h"
#include "ps2/veronica/prog/eneset.h"
#include "psp/live_task_dispatch.h"
#include "psp/player_asset.h"
#include "psp/expand.h"
#include "psp/room.h"
#include "ps2/veronica/prog/room.h"
#include "ps2/veronica/prog/gdlib.h"
#include "ps2/veronica/prog/face.h"

extern ADV_WORK AdvWork;
extern RecvxLiveTaskProc bhSysTaskJumpTab[23];
extern unsigned char NowLoadDisp;
static unsigned int presentations, textured_frames;
static int capture;
static const char *capture_path;
static int loading_captured;
uint64_t recvxPlatformTimeUs(void) { return presentations * 33333u; }
void recvxPlatformFlushCaches(void) { }

static unsigned int sample(const RecvxBootTexture *t, float u, float v)
{
    unsigned int x = u < 0 ? 0 : u >= t->width ? t->width - 1 : (unsigned int)u;
    unsigned int y = v < 0 ? 0 : v >= t->height ? t->height - 1 : (unsigned int)v;
    unsigned int local = x % t->tile_width;
    unsigned int offset = (x / t->tile_width) * t->tile_width * t->height +
        ((y / 8) * (t->tile_width / 16) + local / 16) * 128 + (y & 7) * 16 + (local & 15);
    return t->palette[t->pixels[offset]];
}

void recvxGameBootPresent(void)
{
    size_t count, i;
    unsigned int x, y;
    unsigned char pixels[480 * 272 * 3] = {0};
    const RecvxBootDraw *draws = recvxGameBootDraws(&count);
    if (!loading_captured && NowLoadDisp && sys->mn_md0 == 1 && count) {
        capture = 1; capture_path = "/tmp/recvx-game-loading.ppm";
        loading_captured = 1;
    }
    ++presentations;
    if (count) ++textured_frames;
    for (i = 0; i < count; ++i) {
        const RecvxBootDraw *d = &draws[i];
        const QUAD *q = &d->quad;
        if (i) assert(draws[i - 1].depth <= d->depth);
        if (!capture) continue;
        for (y = 0; y < 272; ++y) for (x = 0; x < 480; ++x) {
            float sx = (x + 0.5f) / 0.75f, sy = (y + 0.5f) * 480 / 272;
            unsigned int rgba = 0xffffffffu, color = d->color;
            unsigned int alpha, channel;
            if (sx < q->x1 || sx >= q->x2 || sy < q->y1 || sy >= q->y2) continue;
            if (d->texture) {
                float u = q->u1 + (q->u2 - q->u1) * (sx - q->x1) / (q->x2 - q->x1);
                float v = q->v1 + (q->v2 - q->v1) * (sy - q->y1) / (q->y2 - q->y1);
                rgba = sample(d->texture, u * d->texture->width, v * d->texture->height);
            }
            alpha = (rgba >> 24) * (color >> 24) / 255;
            for (channel = 0; channel < 3; ++channel) {
                unsigned int value = ((rgba >> (channel * 8)) & 255) *
                    ((color >> ((2 - channel) * 8)) & 255) / 255;
                unsigned char *p = &pixels[(y * 480 + x) * 3 + channel];
                *p = (value * alpha + *p * (255 - alpha)) / 255;
            }
        }
    }
    if (capture) {
        FILE *f = fopen(capture_path, "wb"); assert(f);
        fprintf(f, "P6\n480 272\n255\n");
        assert(fwrite(pixels, 1, sizeof(pixels), f) == sizeof(pixels)); fclose(f);
        capture = 0;
    }
}

static void tick(RecvxInputState *input)
{
    int result = recvxGameBootTick(input);
    if (result) fprintf(stderr, "Unexpected boot fault: %s\n", recvxGameBootFault());
    assert(result == 0);
    recvxOriginalEngineClockAdvance(2, 1);
}

static void press(RecvxInputState *input, uint32_t button)
{
    input->buttons = button; tick(input);
    input->buttons = 0; tick(input);
}

static void put32(unsigned char *p, unsigned int value)
{ p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24; }
static void put16(unsigned char *p, unsigned int value)
{ p[0] = value; p[1] = value >> 8; }

static void test_texture_tiles(void)
{
    unsigned int type;
    for (type = 4; type <= 5; ++type) {
        unsigned int image_size = 1024 * 16 / (type == 4 ? 2 : 1);
        unsigned int colors = type == 4 ? 16 : 256;
        unsigned int total = 48 + image_size + colors * 4;
        unsigned char *data = calloc(1, 16 + total), *p, *pixels, *palette;
        RecvxBootTexture texture = {0};
        unsigned int x, y, i;
        assert(data); memcpy(data, "TIM2", 4); data[4] = 4; put16(data + 6, 1);
        p = data + 16; put32(p, total); put32(p + 4, colors * 4); put32(p + 8, image_size);
        put16(p + 12, 48); put16(p + 14, colors);
        p[17] = 1; p[18] = 3; p[19] = type; put16(p + 20, 1024); put16(p + 22, 16);
        pixels = p + 48; palette = pixels + image_size;
        for (i = 0; i < colors; ++i) { palette[i * 4] = i; palette[i * 4 + 3] = 128; }
        for (y = 0; y < 16; ++y) for (x = 0; x < 1024; ++x) {
            unsigned int index = y * 1024 + x;
            unsigned char value = ((x / 512) * 7 + x + y) % colors;
            if (type == 5) pixels[index] = value;
            else pixels[index / 2] |= value << ((index & 1) * 4);
        }
        assert(recvxBootTextureDecode(data, 16 + total, &texture) == 0);
        assert(texture.tile_count == 2 && texture.tile_width == 512);
        for (y = 0; y < 16; ++y) for (x = 0; x < 1024; ++x) {
            unsigned int value = ((x / 512) * 7 + x + y) % colors;
            unsigned int expected = type == 5 ? (value & ~24u) | ((value & 8) << 1) | ((value & 16) >> 1) : value;
            assert(sample(&texture, x, y) == (0xff000000u | expected));
        }
        assert(recvxBootTextureDecode(data, 16 + total - 1, &texture) == -1);
        assert(texture.width == 1024); /* Failed replacement retains valid ownership. */
        put16(p + 22, 1024);
        assert(recvxBootTextureDecode(data, 16 + total, &texture) == -1);
        recvxBootTextureFree(&texture); free(data);
    }
}

static void test_palette_variants(void)
{
    /* Two CSM1 4-bit palettes; unused entries contain deliberately different
       colors so reading the wrong halves or reusing variant zero fails. */
    unsigned char data[16 + 48 + 128 + 256] = {0};
    unsigned char *picture = data + 16, *pixels = picture + 48;
    unsigned char *palettes = pixels + 128;
    RecvxBootTexture texture = {0};
    unsigned int variant, i;
    memcpy(data, "TIM2", 4); data[4] = 4; put16(data + 6, 1);
    put32(picture, sizeof(data) - 16); put32(picture + 4, 256); put32(picture + 8, 128);
    put16(picture + 12, 48); put16(picture + 14, 32);
    picture[17] = 1; picture[18] = 3; picture[19] = 4;
    put16(picture + 20, 16); put16(picture + 22, 16);
    memset(palettes, 0x55, 256);
    for (i = 0; i < 256; ++i) pixels[i / 2] |= (i % 16) << ((i & 1) * 4);
    for (variant = 0; variant < 2; ++variant) for (i = 0; i < 16; ++i) {
        unsigned int offset = variant * 128 + (i < 8 ? i : i + 8) * 4;
        put32(palettes + offset, 0x80000000u | (variant * 64 + i));
    }
    for (variant = 0; variant < 2; ++variant) {
        assert(recvxBootTextureDecodePalette(data, sizeof(data), variant, &texture) == 0);
        for (i = 0; i < 16; ++i)
            assert(sample(&texture, i, 0) == (0xff000000u | (variant * 64 + i)));
    }
    assert(recvxBootTextureDecodePalette(data, sizeof(data), 2, &texture) == -1);
    assert(sample(&texture, 0, 0) == 0xff000040u);
    recvxBootTextureFree(&texture);
}

static void test_effect_source_bounds(const unsigned char *source, size_t size)
{
    unsigned char *copy = malloc(size);
    unsigned int first_size, body, object, model;
    assert(copy); memcpy(copy, source, size);
    assert(recvxGameBootValidateEffectSource(copy, size) == 0);
    assert(recvxGameBootValidateEffectSource(copy, 15) == -1);
    first_size = *(const unsigned int *)copy;
    put32(copy, 0xfffffff0u);
    assert(recvxGameBootValidateEffectSource(copy, size) == -1);
    put32(copy, first_size);
    body = copy[8] | (copy[9] << 8); object = *(unsigned int *)(copy + 16);
    put32(copy + 4 + body + object + 44, object); /* Self-referencing hierarchy. */
    assert(recvxGameBootValidateEffectSource(copy, size) == -1);
    memcpy(copy, source, size);
    model = *(unsigned int *)(copy + 4 + body + object + 4);
    put32(copy + 4 + body + model, first_size + 4); /* Escaped vertex stream. */
    assert(recvxGameBootValidateEffectSource(copy, size) == -1);
    free(copy);
}

static unsigned char *system_source(const char *directory, unsigned int entry, size_t *size)
{
    char path[512];
    FILE *file;
    unsigned char *data;
    long length;
    snprintf(path, sizeof(path), "%s/archives/SYSTEM/%u.bin", directory, entry);
    file = fopen(path, "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); length = ftell(file); assert(length > 0);
    rewind(file); *size = (size_t)length;
    assert(posix_memalign((void **)&data, 64, *size) == 0);
    assert(fread(data, 1, *size, file) == *size); fclose(file);
    return data;
}

static void test_player_source_bounds(const char *directory)
{
    size_t size;
    unsigned char *source = system_source(directory, 10, &size), *copy;
    unsigned int model_size = *(unsigned int *)source;
    unsigned int motion_size = *(unsigned int *)(source + model_size + 4);
    unsigned char *motion = source + model_size + 8;
    unsigned char *speed = motion + motion_size + 4;
    RecvxPlayerAssets assets;
    size_t object, vertex, skin, track, texture;
    assert(recvxGameBootValidatePlayerSource(source, size) == 0);
    assert(recvxPlayerAssetsParse(source + 4, model_size, motion, motion_size,
        speed, *(unsigned int *)(motion + motion_size), &assets) == 0);
    assert(assets.model_count == 8 && assets.skin_count == 3 && assets.motion_slots == 100);
    object = (assets.models[0].data - source) + assets.models[0].header_offset + assets.models[0].object_offset;
    vertex = (assets.models[0].data - source) + assets.models[0].header_offset;
    vertex += *(unsigned int *)(source + object + 52 + 4);
    vertex = (assets.models[0].data - source) + assets.models[0].header_offset + *(unsigned int *)(source + vertex);
    skin = assets.models[0].skin - source;
    track = (assets.motions[0].data - source) + 16 + assets.motions[0].data_offset;
    texture = (assets.models[0].data - source) + assets.models[0].header_offset + assets.models[0].texture_offset + 4;
    assert(posix_memalign((void **)&copy, 64, size) == 0);
    assert(recvxGameBootValidatePlayerSource(source, 3) == -1);
    assert(recvxGameBootValidatePlayerSource(source, size - 4096) == -1);
    assert(recvxGameBootValidatePlayerSource(source + 1, size - 1) == -1);
    /* Reject oversize streams before any original memory copies. */
    memcpy(copy, source, size); put32(copy, 131073);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + model_size + 4, 393217);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    /* Cycles and duplicate paths must never reach recursive skin conversion. */
    memcpy(copy, source, size); put32(copy + object + 44, assets.models[0].object_offset);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + object + 48, assets.models[0].object_offset + 52);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    /* Donor weights must match the actual chunk vertex range. */
    memcpy(copy, source, size); put16(copy + vertex + 6, 739);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + skin + 8, 128);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + track, 0xfffffff0u);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + texture, 1);
    assert(recvxGameBootValidatePlayerSource(copy, size) == -1);
    assert(memcmp(source, copy, size) != 0);
    free(copy); free(source);
}

static void test_weapon_source_bounds(const char *directory)
{
    size_t size;
    unsigned char *source = system_source(directory, 20, &size), *copy;
    unsigned int model_size = *(unsigned int *)source;
    unsigned int texture_size = *(unsigned int *)(source + model_size + 4);
    size_t left = 8u + model_size + texture_size;
    assert(posix_memalign((void **)&copy, 64, size) == 0);
    assert(recvxGameBootValidateWeaponSource(source, size) == 0);
    assert(recvxGameBootValidateWeaponSource(source, 3) == -1);
    assert(recvxGameBootValidateWeaponSource(source, size - 12) == -1);
    assert(recvxGameBootValidateWeaponSource(source + 1, size - 1) == -1);
    memcpy(copy, source, size); put32(copy, 32772);
    assert(recvxGameBootValidateWeaponSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + model_size + 4, 0x7fffffff);
    assert(recvxGameBootValidateWeaponSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + 24, 2); /* Authored texture count. */
    assert(recvxGameBootValidateWeaponSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + left, 0xfffffff0u);
    assert(recvxGameBootValidateWeaponSource(copy, size) == -1);
    memcpy(copy, source, size); put32(copy + size - 8, 65540);
    assert(recvxGameBootValidateWeaponSource(copy, size) == -1);
    free(copy); free(source);
}

#if UINTPTR_MAX == UINT32_MAX
static int weapon_fixture_case;
static const RecvxPlayerMotionAsset *weapon_fixture_motion;
static unsigned int weapon_fixture_size;

static void weapon_fixture_task(void)
{
    unsigned int i;
    bhInitPlayer();
    sys->obwp = (O_WRK *)bhGetFreeMemory(sizeof(O_WRK) * 2, 64);
    sys->ss_flg = 0x100;
    sys->memp = (unsigned char *)(((uintptr_t)sys->memp + 63u) & ~(uintptr_t)63u);
    RequestReadInsideFile(6, 10, sys->memp);
    bhReadPlayerData();
    plp->wpnr_no = 2;
    plp->mnwP[101].md2P = (void *)0x1234;
    if (weapon_fixture_case == 1) {
        RequestReadInsideFile(6, 20, sys->memp);
        *(unsigned int *)sys->memp = 32772;
        sys->obwp[0].id = 999;
        bhReadWeaponData();
        assert(0);
    }
    if (weapon_fixture_case == 2) {
        RequestReadInsideFile(6, 20, sys->memp);
        sys->wlmdlp = sys->wrmdlp;
        bhReadWeaponData();
        assert(0);
    }
    /* More replacements than the texture pool can hold if release leaks. */
    for (i = 0; i < 70; ++i) {
        if (i) sys->ss_flg &= ~0x100u;
        RequestReadInsideFile(6, 20, sys->memp);
        bhReadWeaponData();
        assert(sys->obwp[0].flg == 0x40081 && sys->obwp[1].flg == 0x40081);
        assert((plp->flg & 0x20000) && *(int *)plp->exp0 == 0);
        assert(sys->wmt_size == weapon_fixture_size);
        assert(plp->mnwP[100].frm_num == weapon_fixture_motion->frame_count);
        assert(plp->mnwP[100].datP == sys->plwmtp + 4);
        assert(plp->mnwP[101].md2P == (void *)0x1234); /* Original holes preserve old slots. */
        assert(plp->mnwP[102].frm_num == weapon_fixture_motion->frame_count);
    }
}

static void write_fixture(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(data, 1, size, file) == size); assert(fclose(file) == 0);
}

static void test_weapon_motions_and_replacement(const char *directory, void *arena, size_t arena_size)
{
    char temporary[] = "/tmp/recvx-weapon-XXXXXX", data_dir[256], path[300];
    size_t player_size, weapon_size, size;
    unsigned char *player = system_source(directory, 10, &player_size);
    unsigned char *weapon = system_source(directory, 20, &weapon_size), *fixture;
    unsigned int model_size = *(unsigned int *)player;
    unsigned int motion_size = *(unsigned int *)(player + model_size + 4);
    RecvxPlayerAssets assets;
    RecvxLiveTaskProc original_init = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    unsigned char *cursor;
    assert(recvxPlayerAssetsParse(player + 4, model_size, player + model_size + 8,
        motion_size, NULL, 0, &assets) == 0);
    weapon_fixture_motion = &assets.motions[0]; assert(weapon_fixture_motion->present);
    weapon_fixture_size = weapon_fixture_motion->size * 2u + 16u;
    assert(weapon_fixture_size <= 65536);
    size = weapon_size - 4u + weapon_fixture_size;
    assert(posix_memalign((void **)&fixture, 64, size) == 0);
    memcpy(fixture, weapon, weapon_size - 8u);
    put32(fixture + weapon_size - 8u, weapon_fixture_size);
    cursor = fixture + weapon_size - 4u;
    put32(cursor, weapon_fixture_motion->size); cursor += 4;
    memcpy(cursor, weapon_fixture_motion->data, weapon_fixture_motion->size); cursor += weapon_fixture_motion->size;
    put32(cursor, 0); cursor += 4;
    put32(cursor, weapon_fixture_motion->size); cursor += 4;
    memcpy(cursor, weapon_fixture_motion->data, weapon_fixture_motion->size); cursor += weapon_fixture_motion->size;
    put32(cursor, UINT32_MAX); cursor += 4;
    assert(cursor == fixture + size && recvxGameBootValidateWeaponSource(fixture, size) == 0);
    assert(mkdtemp(temporary));
    snprintf(data_dir, sizeof(data_dir), "%s/data", temporary); assert(mkdir(data_dir, 0700) == 0);
    snprintf(path, sizeof(path), "%s/archives", data_dir); assert(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/archives/SYSTEM", data_dir); assert(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/archives/SYSTEM/10.bin", data_dir); write_fixture(path, player, player_size);
    snprintf(path, sizeof(path), "%s/archives/SYSTEM/20.bin", data_dir); write_fixture(path, fixture, size);
    bhSysTaskJumpTab[0] = weapon_fixture_task;
    for (weapon_fixture_case = 0; weapon_fixture_case < 3; ++weapon_fixture_case) {
        assert(recvxGameBootInit(data_dir, arena, arena_size) == 0);
        if (!weapon_fixture_case) tick(&input);
        else {
            assert(recvxGameBootTick(&input) == -1);
            assert(strcmp(recvxGameBootFault(), weapon_fixture_case == 1 ?
                "invalid SYSTEM weapon asset records" : "original weapon asset buffers overlap") == 0);
            if (weapon_fixture_case == 1) assert(sys->obwp[0].id == 999);
        }
        recvxGameBootTexturesShutdown();
    }
    bhSysTaskJumpTab[0] = original_init;
    snprintf(path, sizeof(path), "%s/archives/SYSTEM/10.bin", data_dir); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/archives/SYSTEM/20.bin", data_dir); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/archives/SYSTEM", data_dir); assert(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/archives", data_dir); assert(rmdir(path) == 0);
    assert(rmdir(data_dir) == 0);
    snprintf(path, sizeof(path), "%s/boot.log", temporary); assert(unlink(path) == 0);
    assert(rmdir(temporary) == 0);
    free(fixture); free(weapon); free(player);
    puts("PASS: original weapon motion relocation, sparse slots, invalid assets and 70 equipment replacements");
}

static void bone_origins(NJS_CNK_OBJECT *root, NJS_CNK_OBJECT *object,
                         const float parent[3], float origins[128][3])
{
    float *origin = origins[object - root];
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis) origin[axis] = parent[axis] + object->pos[axis];
    if (object->child) bone_origins(root, object->child, origin, origins);
    if (object->sibling) bone_origins(root, object->sibling, parent, origins);
}

static void test_loaded_player(const char *directory)
{
    static const unsigned int objects[] = {22, 0, 0, 22, 4, 1, 2, 5};
    unsigned int i, j, axis, changed = 0, textures = 0;
    size_t size;
    unsigned char *source = system_source(directory, 10, &size);
    unsigned int model_size = *(unsigned int *)source;
    unsigned int motion_size = *(unsigned int *)(source + model_size + 4);
    unsigned char *motion = source + model_size + 8, *speed = motion + motion_size + 4;
    RecvxPlayerAssets assets;
    const RecvxPlayerModelAsset *body;
    const unsigned char *base, *vertices, *weights;
    float origins[128][3] = {{0}}, zero[3] = {0};
    NJS_CNK_OBJECT *root = plp->mdl[0].objP;
    float *converted = (float *)((unsigned char *)root[1].model->vlist + 64);
    assert(recvxPlayerAssetsParse(source + 4, model_size, motion, motion_size,
        speed, *(unsigned int *)(motion + motion_size), &assets) == 0);
    assert(plp->mdl_n == 8 && plp->mlwP == plp->mdl);
    assert(plp->mnwP == (MN_WORK *)sys->plmthp && plp->mnwPb == plp->mnwP);
    assert(sys->bmt_size == motion_size);
    assert(memcmp(sys->plzmtp, speed, *(unsigned int *)(motion + motion_size)) == 0);
    for (i = 0; i < 8; ++i) {
        ML_WORK *model = &plp->mdl[i];
        assert(model->obj_num == objects[i]);
        assert(model->datP == sys->plmdlp + (assets.models[i].data - assets.model_stream));
        assert((unsigned char *)model->owP >= sys->plmdlp + model_size);
        assert((unsigned char *)model->owP + objects[i] * sizeof(O_WORK) <= sys->plmdlp + 131072);
        if (model->texP) textures += model->texP->nbTexture;
    }
    assert(textures == 19);
    for (i = 0; i < assets.motion_slots; ++i) {
        const RecvxPlayerMotionAsset *record = &assets.motions[i];
        MN_WORK *loaded = &plp->mnwP[i];
        if (!record->present) { assert(!loaded->md2P); continue; }
        base = sys->plbmtp + (record->data - assets.motion_stream) + 16;
        assert(loaded->obj_num == record->object_count && loaded->frm_num == record->frame_count);
        assert((const unsigned char *)loaded->md2P == base + record->data_offset);
        for (j = 0; j < record->object_count * 2u; ++j) {
            unsigned int offset = *(unsigned int *)(record->data + 16 + record->data_offset + j * 4u);
            assert(loaded->md2P[j / 2].p[j & 1] == (offset == UINT32_MAX ? NULL : base + offset));
        }
    }
    /* Compare original converted vertices with authored bone translations. */
    body = &assets.models[0]; base = body->data + body->header_offset;
    vertices = base + *(unsigned int *)(base + *(unsigned int *)(base + body->object_offset + 52 + 4)) + 64;
    weights = body->skin + 16;
    bone_origins(root, root, zero, origins);
    for (i = 0; i < 738; ++i) {
        unsigned int bone = *(unsigned int *)(weights + i * 4u) >> 24;
        for (axis = 0; axis < 3; ++axis) {
            float original, expected;
            memcpy(&original, vertices + i * 32u + axis * 4u, 4);
            expected = original + origins[1][axis] - origins[bone][axis];
            assert(fabsf(converted[i * 8u + axis] - expected) < 0.0001f);
            if (fabsf(original - converted[i * 8u + axis]) > 0.0001f) ++changed;
        }
    }
    assert(changed > 100);
    /* The next read reuses this upload area. Owned textures must survive it. */
    memset(sys->memp, 0xa5, size);
    for (i = 0; i < 8; ++i) {
        ML_WORK *model = &plp->mdl[i];
        if (!model->texP) continue;
        for (j = 0; j < model->texP->nbTexture; ++j) {
            const RecvxBootTexture *texture = recvxGameBootTextureGet(model->texP, j);
            RecvxBootTexture decoded = {0};
            assert(texture->source && texture->pixels && texture->width);
            assert(recvxBootTextureDecode(texture->source, texture->source_size, &decoded) == 0);
            assert(memcmp(decoded.pixels, texture->pixels, texture->width * texture->height) == 0);
            assert(memcmp(decoded.palette, texture->palette, sizeof(decoded.palette)) == 0);
            recvxBootTextureFree(&decoded);
        }
    }
    free(source);
}
#endif

static void test_effect_palettes(NJS_TEXLIST *list)
{
    unsigned int i, variants = 0;
    const RecvxBootTexture *previous = NULL;
    unsigned int previous_index = 0;
    assert(list->nbTexture >= 40);
    for (i = 0; i < 40; ++i) {
        const RecvxBootTexture *texture = recvxGameBootTextureGet(list, i);
        NJS_TEXMEMLIST *descriptor = (NJS_TEXMEMLIST *)(uintptr_t)list->textures[i].texaddr;
        unsigned int index = descriptor->globalIndex & 0x03ffffffu;
        assert(texture->source && texture->pixels && texture->width && texture->height);
        if (previous && index == previous_index) {
            assert(memcmp(texture->palette, previous->palette, 16 * sizeof(uint32_t)) != 0);
            ++variants;
        }
        previous = texture; previous_index = index;
    }
    assert(variants >= 10);
}

static void assert_player_buffers(void)
{
    unsigned char *buffers[] = {sys->plmdlp, sys->lmmdlp, sys->wrmdlp, sys->wlmdlp,
        sys->plmthp, sys->plbmtp, sys->plwmtp, sys->plzmtp,
        sys->plexwp, sys->plhdwp, sys->pletcp};
    const unsigned int sizes[] = {131072, 32768, 32768, 32768, 12288, 393216,
        65536, 8192, 124, 124, 32768};
    size_t i, j;
    for (i = 0; i < sizeof(buffers) / sizeof(buffers[0]); ++i) {
        assert(buffers[i] && ((uintptr_t)buffers[i] & 63u) == 0);
        assert(buffers[i] >= freemem && buffers[i] + sizes[i] <= sys->endp);
        if (i) assert(buffers[i] >= buffers[i - 1] + sizes[i - 1]);
        for (j = 0; j < sizes[i]; ++j) assert(buffers[i][j] == 0);
    }
    assert(plp->exp0 == sys->plexwp && plp->exp1 == sys->plhdwp && plp->exp3 == sys->pletcp);
    assert(sys->mempb == sys->memp && sys->memp == sys->pletcp + 32768);
}

static void player_task(void)
{
    bhInitPlayer();
#if UINTPTR_MAX == UINT32_MAX
    _Static_assert(sizeof(BH_PWORK) == 1408, "player/enemy work differs from original ABI");
    memset(ene, 0xa5, sizeof(ene));
    bhInitEnemy();
#endif
}

static void test_player_initialization(const char *directory, void *arena, size_t arena_size)
{
    RecvxLiveTaskProc original_init = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    unsigned int mode;
    bhSysTaskJumpTab[0] = player_task;
    for (mode = 0; mode < 4; ++mode) {
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        memset(plp, 0xa5, sizeof(*plp));
        sys->gm_mode = mode;
        tick(&input);
        assert_player_buffers();
        assert(plp->hp == (mode == 2 ? 320 : 160));
        assert(plp->stflg == 0x40000000u && plp->wpnl_no == 0 && plp->wpnr_no == 0);
        assert(plp->mode0 == 0 && plp->px == 0 && plp->frm_no == 0);
#if UINTPTR_MAX == UINT32_MAX
        {
            size_t i;
            for (i = 0; i < sizeof(ene); ++i) assert(((unsigned char *)ene)[i] == 0);
        }
#endif
    }
    assert(recvxGameBootInit(directory, arena, arena_size) == 0);
    sys->ss_flg = 0x200; sys->ply_id = 3;
    sys->ply_hp[3] = 93; sys->ply_wno[3] = 7; sys->ply_stflg[3] = 0x1234;
    tick(&input);
    assert_player_buffers();
    assert(plp->hp == 93 && plp->wpnr_no == 7 && plp->wpnl_no == 0);
    assert(plp->stflg == 0x40001234u && (sys->cb_flg & 0x800000));
    assert(recvxGameBootInit(directory, arena, arena_size) == 0);
    plp->hp = 111; sys->endp = sys->memp + 131071;
    assert(recvxGameBootTick(&input) == -1);
    assert(strcmp(recvxGameBootFault(), "player initialization exceeds original arena") == 0);
    assert(plp->hp == 111 && sys->memp == freemem && sys->plmdlp == NULL);
    assert(recvxGameBootInit(directory, arena, arena_size) == 0);
    sys->ply_id = -1; sys->ss_flg = 0x200; plp->hp = 112;
    assert(recvxGameBootTick(&input) == -1);
    assert(strcmp(recvxGameBootFault(), "invalid player initialization state") == 0 && plp->hp == 112);
    bhSysTaskJumpTab[0] = original_init;
}

static int message_fault_case;
static void message_fault_task(void)
{
    unsigned char *source = sys->memp, *text;
    size_t remaining;
    unsigned int size, offset;
    RequestReadIsoFile("sysmes.ald", source);
    memcpy(&size, source, 4);
    sys->mes_sp = (unsigned int *)(source + size + 8);
    memcpy(&offset, &sys->mes_sp[492], 4);
    text = (unsigned char *)sys->mes_sp + offset;
    if (message_fault_case == 0) sys->mes_sp[492] = 0;
    if (message_fault_case == 1) { text[0] = 0x14; text[1] = 0x05; } /* Glyph 1300. */
    if (message_fault_case == 2) {
        remaining = recvxGameBootSourceRemaining(text);
        memset(text, 0, remaining);
    }
    bhDispMessage(400, 400, -0.9f, 1, message_fault_case == 3 ? 494 : 491, 0, 0);
}

static void test_message_bounds(const char *directory, void *arena, size_t arena_size)
{
    const char *expected[] = {"message offset exceeds loaded table",
        "message glyph exceeds system font pages", "message terminator missing within loaded text",
        "message index exceeds authored count"};
    RecvxLiveTaskProc original_init = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    bhSysTaskJumpTab[0] = message_fault_task;
    for (message_fault_case = 0; message_fault_case < 4; ++message_fault_case) {
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        assert(recvxGameBootTick(&input) == -1);
        assert(strcmp(recvxGameBootFault(), expected[message_fault_case]) == 0);
    }
    bhSysTaskJumpTab[0] = original_init;
}

static int room_fault_case;
static uint32_t original_mask_indices[2][16];

static void test_mask_records(unsigned char *room)
{
    unsigned int directory = *(unsigned int *)(room + 20), actor;
    for (actor = 0; actor < 2; ++actor) {
        unsigned char *cursor = room + *(unsigned int *)(room + directory + (actor + 1) * 4u);
        unsigned char *model = NULL, *mask = NULL, *copy;
        unsigned int mask_size = 0, count = 0, i, lists;
        uint32_t vertices[128];
        for (;;) {
            unsigned int length = *(unsigned int *)cursor; cursor += 4;
            if (length == UINT32_MAX) break;
            if (!model && !memcmp(cursor, "MDL", 3)) model = cursor;
            if (length >= 4 && !memcmp(cursor, "MASK", 4)) { mask = cursor; mask_size = length; }
            cursor += length;
        }
        assert(model && mask);
        for (i = 0; i < *(unsigned short *)(model + 6); ++i) {
            unsigned char *base = model + *(unsigned short *)(model + 4);
            unsigned int chunk = *(unsigned int *)(base + *(unsigned int *)(model + 12) + i * 52u + 4);
            if (chunk != UINT32_MAX)
                vertices[count++] = *(unsigned int *)(base + *(unsigned int *)(base + chunk) + 4) >> 16;
        }
        assert(recvxGameBootValidateMaskSource(mask, mask_size, vertices, count) == 0);
        lists = *(unsigned int *)(mask + 16);
        memcpy(original_mask_indices[actor], mask + 64 + lists * 4u, sizeof(original_mask_indices[actor]));
        copy = malloc(mask_size); assert(copy);
        memcpy(copy, mask, mask_size); copy[52] = count;
        assert(recvxGameBootValidateMaskSource(copy, mask_size, vertices, count) == -1);
        memcpy(copy, mask, mask_size); put32(copy + 16, UINT32_MAX);
        assert(recvxGameBootValidateMaskSource(copy, mask_size, vertices, count) == -1);
        memcpy(copy, mask, mask_size); put32(copy + 64 + lists * 4u, vertices[mask[52]]);
        assert(recvxGameBootValidateMaskSource(copy, mask_size, vertices, count) == -1);
        assert(recvxGameBootValidateMaskSource(mask, 63, vertices, count) == -1);
        free(copy);
    }
}

static void room_read_task(void)
{
    unsigned char *source, *output;
    int size;
    if (room_fault_case == 1) { GetFileSize("../rm_0000.rdx"); assert(0); }
    if (room_fault_case == 2) { GetFileSize("rm_9999.rdx"); assert(0); }
    size = GetFileSize("rm_0000.rdx"); assert(size == 2097624);
    assert(GetIsoFileSize("RM_0000.RDX") == size);
    assert(GetIsoFileSize("rM_0000.RdX") == size);
    rom->cut_n = 777; ene[0].flg = 1; eff[0].flg = 123;
    sys->mwal_n = 3; sys->metc_n = 2; sys->mflr_n = 1;
    bhInitReadRDT();
    assert(!rom->cut_n && !ene[0].flg && !eff[0].flg);
    assert(!sys->mwal_n && !sys->metc_n && !sys->mflr_n);
    assert(cam.ln == 256 && cam.pers == 0x2000 && sys->ef_pbnk == 32);
    output = freemem;
    source = (unsigned char *)(((uintptr_t)sys->endp - (unsigned int)size) & ~(uintptr_t)63u);
    assert(RequestReadIsoFile("RM_0000.RDX", source) == 0);
    assert(recvxGameBootSourceRemaining(source) == (size_t)size);
    sys->rdtp = source; sys->memp = output;
    if (room_fault_case == 3) { source[0] = 2; source[1] = 8; source[2] = 0; }
    if (room_fault_case == 4) sys->memp = output = source - 64;
    if (room_fault_case == 5) sys->memp = output = source;
    if (room_fault_case == 6) { source[1] = 0; source[2] = 0; source[3] = 128; source[4] = 63; }
    sys->rdtsz = Expand((char *)source, output);
    assert(room_fault_case == 0 && sys->rdtsz == 5392084);
    assert(recvxGameBootSourceRemaining(output) == 5392084u);
    assert(recvxRoomCrc32(output, sys->rdtsz) == 0xaf78e4c2u);
    assert(recvxGameBootValidateRoomSource(output, sys->rdtsz) == 0);
    test_mask_records(output);
    {
        unsigned char *copy;
        unsigned int room, models, first, body, object, chunk, palette;
        size_t length = sys->rdtsz;
        assert(posix_memalign((void **)&copy, 64, length) == 0);
        room = *(unsigned int *)(output + 16); models = *(unsigned int *)(output + 20);
        first = *(unsigned int *)(output + models); body = output[first + 4];
        object = *(unsigned int *)(output + first + 12);
        chunk = *(unsigned int *)(output + first + body + object + 4);
        /* Locate the first object with a chunk in the original room model. */
        while (chunk == UINT32_MAX) {
            object += 52; chunk = *(unsigned int *)(output + first + body + object + 4);
        }
        palette = *(unsigned int *)(output + *(unsigned int *)(output + 32) + 4);
        memcpy(copy, output, length); put32(copy + room + 0x88, 129);
        assert(recvxGameBootValidateRoomSource(copy, length) == -1);
        memcpy(copy, output, length); put32(copy + first + body + object + 44, object);
        assert(recvxGameBootValidateRoomSource(copy, length) == -1);
        memcpy(copy, output, length); put32(copy + first + body + chunk + 4, UINT32_MAX);
        assert(recvxGameBootValidateRoomSource(copy, length) == -1);
        memcpy(copy, output, length); put16(copy + first + 6, 0); put32(copy + first + 12, UINT32_MAX);
        assert(recvxGameBootValidateRoomSource(copy, length) == -1);
        memcpy(copy, output, length); put32(copy + 28, 0xfffffff0u);
        assert(recvxGameBootValidateRoomSource(copy, length) == -1);
        memcpy(copy, output, length); put32(copy + palette + 4, UINT32_MAX);
        assert(recvxGameBootValidateRoomSource(copy, length) == -1);
        memcpy(copy, output, length);
        palette = *(unsigned int *)(copy + *(unsigned int *)(copy + 32) + 25u * 4u);
        put32(copy + palette, UINT32_MAX); /* Empty effect list has no descriptor to reference. */
        assert(recvxGameBootValidateRoomSource(copy, length) == -1);
        assert(recvxGameBootValidateRoomSource(output, 100) == -1);
        free(copy);
    }
}

static void test_room_read_and_expand(const char *directory, void *arena, size_t arena_size)
{
    static const char *expected[] = {NULL, "unpackaged ISO file request", "/rooms/RM_9999.rdx",
        "invalid or oversized compressed room stream", "invalid or oversized compressed room stream",
        "invalid original room expansion buffers", "invalid expanded room header or arrays"};
    RecvxLiveTaskProc original_init = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    bhSysTaskJumpTab[0] = room_read_task;
    for (room_fault_case = 0; room_fault_case < 7; ++room_fault_case) {
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        if (!room_fault_case) tick(&input);
        else {
            assert(recvxGameBootTick(&input) == -1);
            assert(strstr(recvxGameBootFault(), expected[room_fault_case]));
            if (room_fault_case == 2) assert(GetReadFileStatus() == -1);
        }
    }
    bhSysTaskJumpTab[0] = original_init;
    puts("PASS: original room reset, file names, bounded decompression and malformed room records");
}

int main(int argc, char **argv)
{
    size_t arena_size = 12u * 1024u * 1024u;
    void *arena = mmap((void *)0x20000000u, arena_size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS
#if UINTPTR_MAX > UINT32_MAX
        | MAP_32BIT
#endif
        , -1, 0);
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    unsigned int i, warning_seen = 0, logo_seen = 0, title_seen = 0;
    assert((argc == 2 || argc == 3) && arena != MAP_FAILED);
    test_texture_tiles();
    test_palette_variants();
    /* Original pointer arithmetic and 32-bit texture handles need low addresses.
       This checks startup logic on x86_64, not the complete original PSP ABI. */
    assert((uintptr_t)arena + arena_size < 0x80000000u);
    assert(recvxGameBootInit(argv[1], arena, 128) == -1);
    test_player_initialization(argv[1], arena, arena_size);
    if (argc == 2) {
        test_player_source_bounds(argv[1]);
        test_weapon_source_bounds(argv[1]);
        test_message_bounds(argv[1], arena, arena_size);
#if UINTPTR_MAX == UINT32_MAX
        test_weapon_motions_and_replacement(argv[1], arena, arena_size);
#endif
        test_room_read_and_expand(argv[1], arena, arena_size);
    }
    assert(recvxGameBootInit(argv[1], arena, arena_size) == 0);
    assert(sys->tk_flg == 1);
    if (argc == 3) {
        tick(&input);
        assert(recvxGameBootTick(&input) == -1);
        assert(strstr(recvxGameBootFault(), "archives/SYSTEM/1.bin"));
        assert(GetReadFileStatus() == -1 && sys->tk_flg == 0x300002);
        munmap(arena, arena_size);
        puts("PASS: missing SYSTEM resource stops boot and retains failed read status");
        return 0;
    }
    for (i = 0; i < 1200; ++i) {
        tick(&input);
        if (sys->tk_flg & 2) warning_seen = 1;
        if (sys->tk_flg & 4) logo_seen = 1;
        if ((sys->tk_flg & 8) && AdvWork.Mode == 6) { title_seen = 1; break; }
    }
    assert(warning_seen && logo_seen && title_seen && textured_frames > 300);
    assert(sys->sys_partid == 6 && AdvWork.PatId == 3);
    capture = 1; capture_path = "/tmp/recvx-game-title.ppm"; tick(&input);
    press(&input, RECVX_BUTTON_START);
    for (i = 0; i < 30 && AdvWork.Mode != 9; ++i) tick(&input);
    assert(AdvWork.Mode == 9);
    /* No memory card: original title selects New Game and disables Load. */
    assert(AdvWork.Cursor[0] == 1);
    capture = 1; capture_path = "/tmp/recvx-game-menu.ppm"; tick(&input);
    press(&input, RECVX_BUTTON_CROSS);
    for (i = 0; i < 200 && !(sys->tk_flg & 0x20); ++i) {
        int result = recvxGameBootTick(&input);
        if (result) break;
    }
    /* Original live dispatcher enters Opening and Monitor in the same frame. */
    for (i = 0; i < 20 && !recvxGameBootFault(); ++i) recvxGameBootTick(&input);
#if UINTPTR_MAX > UINT32_MAX
    assert(recvxGameBootFault() && strcmp(recvxGameBootFault(),
        "effect model relocation requires the original 32-bit ABI") == 0);
    {
        static NJS_TEXNAME names[128];
        NJS_TEXLIST list = {names, 0};
        unsigned char *source = sys->memp;
        size_t source_size = recvxGameBootSourceRemaining(source), cursor = 0;
        test_effect_source_bounds(source, source_size);
        for (i = 0; i < 4; ++i) cursor += 4 + *(unsigned int *)(source + cursor);
        cursor = (cursor + 4 + 31u) & ~(size_t)31u;
        list.nbTexture = bhSetMemPvpTexture(&list, source + cursor, 0);
        test_effect_palettes(&list);
        /* All decoded data must survive reuse of the original upload source. */
        memset(source + cursor, 0xa5, source_size - cursor);
        test_effect_palettes(&list);
        njReleaseTexture(&list);
    }
#else
    if (!recvxGameBootFault() || strcmp(recvxGameBootFault(), "bhInitEvent"))
        fprintf(stderr, "Unexpected native loader boundary: %s\n", recvxGameBootFault());
    assert(recvxGameBootFault() && strcmp(recvxGameBootFault(), "bhInitEvent") == 0);
    assert(sys->ef_ctb == 40 && sys->ef_ct == 48 && sys->ef_tlist.nbTexture == (unsigned int)sys->ef_ct);
    assert(sys->efm[0].obj_num == 2 && sys->efm[1].obj_num == 2 &&
        sys->efm[7].obj_num == 3 && sys->efm[13].obj_num == 2);
    assert(sys->efm[0].objP->child == sys->efm[0].objP + 1);
    assert(sys->efm[0].objP->model->r > 0 && sys->efm[0].owP && sys->efm[13].owP);
    assert((sys->ss_flg & 0x40) && (sys->gm_flg & 4));
    assert(cam.ln == 256 && cam.pers == 0x2000 && cam.ocut == -1);
    assert((*cam.mtx)[0] == 1 && (*cam.mtx)[15] == 1);
    test_effect_palettes(&sys->ef_tlist);
    assert(plp->hp == 160 && plp->stflg == 0x40000000u && plp->exp0 == sys->plexwp);
    assert(sys->plmdlp && sys->plbmtp && sys->plmthp && sys->pletcp);
    assert(ene[0].id == 91 && ene[1].id == 98 && ene[127].flg == 0);
    assert(sys->itm[1] == 0x370001 && sys->itm[2] == 0x910001 && sys->itm[383] == 1);
    assert(NowLoadDisp && loading_captured);
    assert(sys->obwp[0].mlwP == sys->obwp[0].mdl && sys->obwp[1].mlwP == sys->obwp[1].mdl);
    for (i = 0; i < 2; ++i) {
        O_WRK *weapon = &sys->obwp[i];
        unsigned char *buffer = i ? sys->wlmdlp : sys->wrmdlp;
        assert(weapon->flg == 0x81 && weapon->id == 1210 && weapon->type == 0);
        assert(weapon->lkono == (i ? 13 : 9) && weapon->lkwkp == (unsigned char *)plp);
        assert(weapon->mdl[0].obj_num == 1 && weapon->mdl[0].datP == buffer);
        assert(weapon->mdl[0].owP == (O_WORK *)(((uintptr_t)buffer +
            (i ? 2612u : 2620u) + 255u) & ~(uintptr_t)255u));
        assert(weapon->mdl[0].texP->nbTexture == 1);
        assert(recvxGameBootTextureGet(weapon->mdl[0].texP, 0)->width == 256);
    }
    assert(sys->wmt_size == 0 && !(plp->flg & 0x20000));
    assert(strcmp(sys->mes, "rm_0000.rdx") == 0);
    assert(sys->rdtsz == 5392084 && rom->mdl.obj_num == 67);
    assert(rom->cut_n == 3 && rom->ene_n == 2 && rom->obj_n == 16 && rom->itm_n == 8);
    assert(sys->txr_n == 21 && sys->txr_ct == sys->txr_n);
    assert(rom->mdl.texP->nbTexture == 32);
    for (i = 0; i < (unsigned int)sys->txr_n; ++i) {
        unsigned int j;
        for (j = 0; j < sys->txlp[i]->nbTexture; ++j) {
            const RecvxBootTexture *texture = recvxGameBootTextureGet(sys->txlp[i], j);
            assert(texture->source && texture->pixels && texture->width);
        }
    }
    assert(sys->obwp[15].mlwP->texP == sys->obwp[6].mlwP->texP); /* ID 1012 shares its list. */
    assert(sys->itwp[5].mlwP->texP == sys->itwp[1].mlwP->texP); /* Item 95 shares its list. */
    for (i = 0; i < 2; ++i) {
        MASK_WORK *mask = (MASK_WORK *)ene[i].exp1;
        unsigned int j;
        size_t face_bytes = (((unsigned int)mask->src->vlist[1] >> 16) * 8u + 128u) * 4u;
        size_t tongue_bytes = (((unsigned int)mask->tangsrc->vlist[1] >> 16) * 8u + 128u) * 4u;
        assert(mask->vtype == 51 && mask->vofs == 8 && mask->vtop == 16 && mask->ntop == 20);
        assert(mask->fnum == (i ? 18 : 24) && mask->lnum == (i ? 738 : 731));
        assert(mask->mode & 1u);
        assert(memcmp(mask->dst->vlist, mask->src->vlist, face_bytes) == 0);
        assert(memcmp(mask->tangdst->vlist, mask->tangsrc->vlist, tongue_bytes) == 0);
        for (j = 0; j < 16; ++j)
            assert(((unsigned int *)mask->nvpt)[j] == (unsigned int)(uintptr_t)
                &mask->dst->vlist[16 + original_mask_indices[i][j] * 8u]);
        assert(ene[i].mdl[0].obj_num == 31 && ene[i].exp0 && ene[i].exp2);
        assert(ene[i].mnwP && ene[i].mnwP == sys->emtp[ene[i].id]);
        assert(!((LIP_WORK *)ene[i].exp2)->flag);
    }
    assert(sys->rmthp && sys->rmthp[0].md2P && sys->rmthp[9].md2P);
    assert(fabsf(plp->px - rom->posp[sys->pos_no].px) < 0.0001f &&
        fabsf(plp->pz - rom->posp[sys->pos_no].pz) < 0.0001f && plp->flr_no == 0 && plp->py == rom->grand[2]);
    assert(sys->fade_hkn == 11.0f && (sys->cb_flg & 2) && !(sys->ss_flg & 0x100));
    assert((sys->gm_flg & 0x800) && sys->sp_flg == UINT32_MAX);
    for (i = 0; i < (unsigned int)rom->lgt_n; ++i)
        assert(rom->lgtp[i].mode == 0 && rom->lgtp[i].light == NULL);
    for (i = 0; i < (unsigned int)rom->eff_n; ++i) {
        assert(sys->efid[i] >= 0 && sys->efid[i] < 512);
        assert(eff[sys->efid[i]].id == rom->effp[i].id);
    }
    test_loaded_player(argv[1]);
#endif
    assert(sys->stg_no == 0 && sys->rom_no == 0 && sys->rcase == 0);
    assert(sys->obwp && sys->itwp && sys->doordp && sys->mes_ip && sys->mes_sp);
    assert(sys->ply_hp[0] == 160 && sys->mn_md0 == (sizeof(void *) == 4 ? 4 : 1));
    assert(sys->mn_md1 == (sizeof(void *) == 4 ? 10 : 4));
    assert(GetReadFileStatus() == 0);
    assert(recvxGameBootTick(&input) == -1); /* A fault cannot advance or fake a room. */
    recvxGameBootTexturesShutdown();
    munmap(arena, arena_size);
#if UINTPTR_MAX > UINT32_MAX
    puts("PASS: sanitized title/new-game boot and bounded effect preflight; original relocation requires 32-bit ABI test");
#else
    printf("PASS: original player/enemy/inventory initialization, loading text and original Claire models/skin/motions/textures; original weapon loading and opening room relocation/textures; original room finalization/spawn/masks; next boundary bhInitEvent (%d effect textures retained)\n", sys->ef_ct);
#endif
    return 0;
}
