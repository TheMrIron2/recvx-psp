#include "ps2/veronica/prog/MdlPut.h"
#include "ps2/veronica/prog/camera.h"
#define PT_WORK RecvxDoorParameterRecord
#include "ps2/veronica/prog/door.h"
#undef PT_WORK
#include "ps2/veronica/prog/en71.h"
#include "ps2/veronica/prog/flag.h"
#include "psp/game_boot_game_remaining.h"
#include "ps2/veronica/prog/weapon.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#define _GNU_SOURCE
#define u_long recvx_host_u_long
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#undef u_long
#include "psp/game_boot.h"
#include "psp/game_boot_enemy.h"
#include "psp/game_boot_save_storage.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_effect_callbacks.h"
#include "ps2/veronica/prog/effsub0.h"
#include "ps2/veronica/prog/effsub5.h"
#include "ps2/veronica/prog/effsub1.h"
#include "ps2/veronica/prog/effsub1b.h"
#include "ps2/veronica/prog/effsub2.h"
#include "ps2/veronica/prog/effsub3.h"
#include "ps2/veronica/prog/effsub6.h"
#include "ps2/veronica/prog/effsub4.h"
#include "ps2/veronica/prog/hitchkl.h"
#include "ps2/veronica/prog/hitchk.h"
#include "psp/game_boot_audio.h"
#include "psp/game_boot_movie.h"
#include "psp/game_boot_movie_audio.h"
#include "psp/engine_original_lighting.h"
#include "psp/engine_original_fault.h"
#include "psp/original_clock.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/adv.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/ps2_texture.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/playpch.h"
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
#include "ps2/veronica/prog/face_bh.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/fileview.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/subpl.h"
#include "ps2/veronica/prog/Motion.h"
#include "ps2/veronica/prog/system.h"
#include "ps2/veronica/prog/screen.h"
#include "ps2/veronica/prog/cut.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/effect.h"
#include "psp/engine_event_core.h"

extern ADV_WORK AdvWork;
extern RecvxLiveTaskProc bhSysTaskJumpTab[23];
extern unsigned char NowLoadDisp;
static unsigned int presentations, textured_frames, game_frames, movie_presentations;
static unsigned int audio_samples_due;
static unsigned int inventory_model_frames, inventory_visible_vertices;
static int capture;
static const char *capture_path;
static int loading_captured;
uint64_t recvxPlatformTimeUs(void) { return presentations * 33333u; }
void recvxPlatformFlushCaches(void) { }
static unsigned int scene_captures;
void recvxGameBootCaptureCopy(unsigned char *pixels)
{
    /* Native fixture models the GE transfer contract; raster output is tested
       by the production GU adapter's command mocks. */
    memset(pixels,0x7f,512u*512u*4u);++scene_captures;
}

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
    const RecvxBootRenderFrame *frame = recvxGameBootRenderFrame();
    if (frame->vertex_count) {
        const RecvxBootRenderFrame *scene = frame;
        assert(scene->vertex_count > 0 && scene->batch_count > 0);
        size_t total = 0, triangles = 0, model_triangles = 0, shadow_triangles = 0, composites = 0;
        for (size_t batch = 0; batch < scene->batch_count; ++batch) {
            assert(scene->batches[batch].first == total && scene->batches[batch].count <= 65535);
            const RecvxBootRenderBatch *draw = &scene->batches[batch];
            if (draw->kind == RECVX_RENDER_SHADOW_START || draw->kind == RECVX_RENDER_SHADOW_END)
                assert(draw->count == 0);
            else if (draw->kind == RECVX_RENDER_LINES || draw->kind == RECVX_RENDER_SHADOW_COMPOSITE) {
                assert(draw->count % 2 == 0);
                if (draw->kind == RECVX_RENDER_SHADOW_COMPOSITE) ++composites;
            } else {
                assert(draw->count % 3 == 0); triangles += draw->count / 3;
                if (draw->kind == RECVX_RENDER_SHADOW_ADD || draw->kind == RECVX_RENDER_SHADOW_SUBTRACT)
                    shadow_triangles += draw->count / 3;
                else if (draw->kind == RECVX_RENDER_TRIANGLES) model_triangles += draw->count / 3;
            }
            if (draw->texture) assert(draw->texture->source && draw->texture->pixels);
            total += draw->count;
        }
        assert(total == scene->vertex_count);
        if((sys->ts_flg&0x80)&&(swork.statusflg&0x10)){
            ++inventory_model_frames;
            if(inventory_model_frames==1){
                float xmin=1e9f,xmax=-1e9f,ymin=1e9f,ymax=-1e9f;
                unsigned int opaque=0;
                for(size_t v=0;v<scene->vertex_count;++v){
                    const RecvxBootRenderVertex *p=&scene->vertices[v];
                    float w=-p->z;
                    if(w>0){
                        float x=(scene->projection[0]*p->x+scene->projection[8]*p->z)/w;
                        float y=(scene->projection[5]*p->y+scene->projection[9]*p->z)/w;
                        x=240*(1+x);y=136*(1-y);
                        xmin=fminf(xmin,x);xmax=fmaxf(xmax,x);ymin=fminf(ymin,y);ymax=fmaxf(ymax,y);
                    }
                    if(p->color>>24)++opaque;
                }
                const RecvxBootRenderBatch *b=&scene->batches[0];
                unsigned int palette_alpha=0;
                if(b->texture)for(unsigned int c=0;c<256;++c)if(b->texture->palette[c]>>24)++palette_alpha;
                fprintf(stderr,"Preview geometry: bbox=%g,%g..%g,%g scissor=%d,%d..%d,%d alpha=%u palette=%u\n",
                    xmin,ymin,xmax,ymax,b->scissor[0],b->scissor[1],b->scissor[2],b->scissor[3],opaque,palette_alpha);
                assert(opaque&&xmax>b->scissor[0]&&xmin<b->scissor[2]&&ymax>b->scissor[1]&&ymin<b->scissor[3]);
            }

            for(size_t v=0;v<scene->vertex_count;++v){
                const RecvxBootRenderVertex *p=&scene->vertices[v];
                float w=-p->z;
                float x=scene->projection[0]*p->x+scene->projection[8]*p->z;
                float y=scene->projection[5]*p->y+scene->projection[9]*p->z;
                float z=scene->projection[10]*p->z+scene->projection[14];
                if(w>0&&fabsf(x)<w&&fabsf(y)<w&&fabsf(z)<w)++inventory_visible_vertices;
            }
        }

        if (!game_frames) fprintf(stderr, "Original opening room submitted: %u triangles (%u modifier), %u composites, %u batches\n",
            (unsigned int)triangles, (unsigned int)shadow_triangles, (unsigned int)composites,
            (unsigned int)scene->batch_count);
        if (!game_frames) fprintf(stderr, "Opening model triangles: %u\n", (unsigned int)model_triangles);
        if (!game_frames) assert(model_triangles > 3265 && shadow_triangles > 0);
        ++game_frames;
    }

    if (!loading_captured && NowLoadDisp && sys->mn_md0 == 1 && count) {
        capture = 1; capture_path = "/tmp/recvx-game-loading.ppm";
        loading_captured = 1;
    }
    ++presentations;
    if (game_frames || recvxMovieAudioActive()) {
        /* Advance the native SAS call model at the same 30 Hz presentation
           cadence as hardware. It tracks compressed voice completion only;
           the host fixture never decodes samples or mixes software audio. */
        int16_t audio[RECVX_AUDIO_FRAMES * 2];
        audio_samples_due += RECVX_AUDIO_RATE / 30;
        while (audio_samples_due >= RECVX_AUDIO_FRAMES) {
            recvxAudioRender(audio, RECVX_AUDIO_FRAMES);
            audio_samples_due -= RECVX_AUDIO_FRAMES;
        }
        assert(!recvxAudioFault());
    }
    if (count) ++textured_frames;
    for (i = 0; i < count; ++i) {
        const RecvxBootDraw *d = &draws[i];
        const QUAD *q = &d->quad;
        if (i) assert(draws[i - 1].depth >= d->depth);
        if (!capture) continue;
        for (y = 0; y < 272; ++y) for (x = 0; x < 480; ++x) {
            float sx = (x + 0.5f) / 0.75f, sy = (y + 0.5f) * 480 / 272;
            unsigned int rgba = 0xffffffffu, color = d->color;
            unsigned int alpha, channel;
            if (d->color_gradient) {
                static const unsigned int triangles[2][3]={{0,1,2},{0,2,3}};
                int inside=0;
                for(unsigned int triangle=0;triangle<2&&!inside;++triangle){
                    unsigned int a=triangles[triangle][0],b=triangles[triangle][1],c=triangles[triangle][2];
                    NJS_POINT2 p=d->points[a],r=d->points[b],s=d->points[c];
                    float denominator=(r.y-s.y)*(p.x-s.x)+(s.x-r.x)*(p.y-s.y);
                    if(fabsf(denominator)<.0001f)continue;
                    float wa=((r.y-s.y)*(sx-s.x)+(s.x-r.x)*(sy-s.y))/denominator;
                    float wb=((s.y-p.y)*(sx-s.x)+(p.x-s.x)*(sy-s.y))/denominator;
                    float wc=1-wa-wb;
                    if(wa<0||wb<0||wc<0)continue;
                    inside=1;color=0;
                    for(unsigned int ch=0;ch<4;++ch){
                        float value=wa*((d->vertex_color[a]>>(8*ch))&255)+
                            wb*((d->vertex_color[b]>>(8*ch))&255)+wc*((d->vertex_color[c]>>(8*ch))&255);
                        color|=(unsigned int)fminf(255,fmaxf(0,value+.5f))<<(8*ch);
                    }
                    if(d->texture)rgba=sample(d->texture,
                        wa*d->uv[a].x+wb*d->uv[b].x+wc*d->uv[c].x,
                        wa*d->uv[a].y+wb*d->uv[b].y+wc*d->uv[c].y);
                }
                if(!inside)continue;
            } else {
                if (sx < q->x1 || sx >= q->x2 || sy < q->y1 || sy >= q->y2) continue;
                if (d->texture) {
                    float u = q->u1 + (q->u2 - q->u1) * (sx - q->x1) / (q->x2 - q->x1);
                    float v = q->v1 + (q->v2 - q->v1) * (sy - q->y1) / (q->y2 - q->y1);
                    rgba = sample(d->texture, u * d->texture->width, v * d->texture->height);
                }
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
    if (recvxGameBootMovieGetFrame()) ++movie_presentations;
    recvxGameBootMovieFrameConsumed();
    recvxGameBootRenderConsume();
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
    /* This fixture has loaded weapon motion 100, unlike the unarmed opening.
       Exercise the actual attack dispatcher using the relocated motion. */
    swork.pip=sys->itm; sys->itm[0]=2; sys->itm[2]=0x2000c;
    plp->mode2=64; plp->mode3=0;
    bhCPM1_act_atk();
    assert(plp->mode3==1 && plp->mtn_no==100 && (plp->stflg&0x10000));
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
        unsigned int mask_size = 0, count = 0, i, lists, connection_offset;
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
        connection_offset = 64 + lists * 4u;
        for (i = 0; i < lists; ++i) connection_offset += mask[64 + i * 4u + 3] * 4u;
        memcpy(copy, mask, mask_size); put32(copy + connection_offset, 32);
        assert(recvxGameBootValidateMaskSource(copy, mask_size, vertices, count) == -1);
        memcpy(copy, mask, mask_size); copy[66] = 33;
        assert(recvxGameBootValidateMaskSource(copy, mask_size, vertices, count) == -1);
        memcpy(copy, mask, mask_size); put32(copy + connection_offset + 4, 0x7fc00000);
        assert(recvxGameBootValidateMaskSource(copy, mask_size, vertices, count) == -1);
        assert(recvxGameBootValidateMaskSource(mask, 63, vertices, count) == -1);
        free(copy);
    }
}

static void test_facial_deformation(void)
{
    int source_words[32] = {0}, destination_words[32] = {0};
    int tongue_source[24] = {0}, tongue_destination[24] = {0};
    const float points[3] = {2, 3, 4};
    NJS_CNK_MODEL source = {.vlist = source_words}, destination = {.vlist = destination_words};
    NJS_CNK_MODEL tsrc = {.vlist = tongue_source}, tdst = {.vlist = tongue_destination};
    NJS_CNK_OBJECT eye = {0};
    VLIST_WORK list = {.id = 0, .mnum = 2};
    LIST_WORK connections[2] = {
        {.id = 0, .scal = 2, .vec = {2, 4, 6}},
        {.id = 1, .scal = 4, .vec = {1, 2, 3}}
    };
    TANG_WORK jaw = {.id = 1, .rate = 1}, tang = {.id = 0, .rate = 1};
    MASK_WORK mask = {.src = &source, .dst = &destination, .vtype = 51,
        .vtop = 16, .vofs = 8, .mode = 1, .vlist = &list, .list = connections,
        .lnum = 1, .jnum = 1, .jaw = &jaw, .tnum = 1, .tang = &tang,
        .tangsrc = &tsrc, .tangdst = &tdst};
    float *face0 = (float *)(destination_words + 16);
    float *face1 = (float *)(destination_words + 24);
    float *tongue = (float *)(tongue_destination + 16);
    unsigned int i;
    memcpy(source_words + 16, points, sizeof(points));
    memcpy(source_words + 24, points, sizeof(points));
    memcpy(tongue_source + 16, points, sizeof(points));
    source_words[19] = source_words[27] = 0x7fc01234; /* Packed bits, not a float. */
    destination_words[20] = 0x76543210; /* Fixed normal must survive. */
    tongue_destination[19] = 0x12345678;
    njUnitMatrix(&mask.jmat1); njUnitMatrix(&mask.jmat2);
    njUnitMatrix(&mask.eyemat[0]); mask.eyesrc[0] = &eye;
    mask.param.muscle[0] = 1; mask.param.muscle[1] = -0.5f;
    mask.param.jawang = 90; mask.param.tangx = 90; mask.param.tangz = 2;
    mask.param.eye.x = 10; mask.param.eye.z = -10;
    recvxGameBootFaceVectorCheck();
    fmCnkCalcFace(&mask);
    for (i = 0; i < 3; ++i) assert(fabsf(face0[i] - (points[i] + 0.75f * (i + 1))) < 0.00001f);
    assert(fabsf(face1[0] - 2) < 0.00001f && fabsf(face1[1] - 4) < 0.00001f && fabsf(face1[2] + 3) < 0.00001f);
    assert(fabsf(tongue[0] - 2) < 0.00001f && fabsf(tongue[1] + 8) < 0.00001f && fabsf(tongue[2] - 3) < 0.00001f);
    assert(destination_words[19] == source_words[19] && destination_words[27] == source_words[27]);
    assert(destination_words[20] == 0x76543210 && tongue_destination[19] == 0x12345678);
    assert(eye.ang[0] == 0 && abs(eye.ang[1] + 8192) <= 1 && eye.ang[2] == 0);
    /* No active muscles must reset to the source without dividing by zero. */
    mask.param.muscle[0] = mask.param.muscle[1] = 0;
    fmCnkCalcFace(&mask);
    assert(memcmp(face0, points, sizeof(points)) == 0);
    puts("PASS: facial muscle weights, jaw/tongue rotation, eye direction and packed vertex preservation");
}

static int effect_fixture_case;
static void effect_control_task(void)
{
    O_WORK matrix_work = {0};
    ATR_WORK ground = {.flg = 1, .type = 7, .px = -20, .pz = -20, .w = 40, .d = 40};
    EF_WORK sprite = {.flg = 1, .id = 9, .type = 1, .py = 5, .sx = 1, .sy = 1, .sz = 1};
    unsigned int i;
    memset(eff, 0, sizeof(eff));
    sys->ef_flg = 1; sys->pt_flg = 1;
    rom->walp = &ground; rom->wal_n = 1;
    plp->flg = 1; plp->stflg = 0x40000000;
    plp->mlwP = plp->mdl; plp->mdl[0].obj_num = 1; plp->mdl[0].owP = &matrix_work;
    njUnitMatrix(&matrix_work.mtx);
    matrix_work.mtx[12] = 2; matrix_work.mtx[13] = 3; matrix_work.mtx[14] = 4;
    if (effect_fixture_case == 1 || effect_fixture_case == 2) {
        eff[0].flg = 0x81; eff[0].id = 9;
        eff[0].lkwkp = effect_fixture_case == 1 ? (unsigned char *)1 : (unsigned char *)plp;
        eff[0].lkono = 2;
    } else if (effect_fixture_case == 3) {
        eff[0].flg = 1; eff[0].id = 225; eff[0].type = 255;
    } else if (effect_fixture_case == 4) {
        eff[0].flg = 1; eff[0].id = 20; eff[0].type = 450;
    } else if (effect_fixture_case == 5) {
        for (i = 0; i < 129; ++i) assert(bhSetShadow(NULL, (unsigned char *)plp, 0, 1, 1, 1) == (int)i);
        sys->ef_lin[0] = &eff[500];
        bhControlEffect();
        bhControlEffect(); /* The 129th shadow must stop before its queue write. */
    } else {
        assert(bhSetShadow(NULL, (unsigned char *)plp, 0, 1, 1, 1) == 0);
        eff[1].flg = 1; eff[1].id = 33;
        eff[2].flg = 1; eff[2].id = 400;
        eff[3].flg = 1; eff[3].id = 149; eff[3].stflg = 0x1000000;
        assert(bhSetEffectTb(&sprite, NULL, NULL, 0) == 4);
        sprite.id = 115; sprite.type = 1;
        assert(bhSetEffectTb(&sprite, NULL, NULL, 0) == 5);
        sys->ef_flg = 0; sys->sp_flg = 0; sys->ef_trsn = 17;
        bhControlEffect(); assert(sys->ef_trsn == 17 && eff[0].mode0 == 0);
        sys->ef_flg = 1;
        bhControlEffect();
        assert(eff[0].mode0 == 1 && eff[4].mode0 == 1 && eff[4].ct0 == 1);
        assert(eff[1].id == 15 && eff[1].mode0 == 0); /* Spawned into an already visited free slot. */
        assert(eff[2].flg == 0 && sys->ef_trsn == 1);
        for (i = 0; i < 24; ++i) {
            bhControlEffect();
            assert(sys->ef_trsn > 0 && sys->ef_trsn < 512 && sys->ef_mdfn == 1);
            assert(sys->ef_mdf[0] == &eff[0] && sys->ef_linn == 0);
            assert(eff[0].px == 2 && eff[0].py == 0 && eff[0].pz == 4);
            assert(eff[4].ct0 >= 1 && eff[4].ct0 <= 8);
        }
        return;
    }
    bhControlEffect();
    assert(0);
}

static int callback_fault_case;
static void callback_fault_task(void)
{
    bhInitSystem();memset(eff,0,sizeof(eff));
    O_WRK *e=&eff[0];e->flg=1;e->id=302;
    eff[1].flg=eff[2].flg=2;
    e->exp0=(unsigned char *)&eff[1];e->exp1=(unsigned char *)&eff[2];
    switch(callback_fault_case){
    case 0:e->type=20;bhEff302(e);break;
    case 1:e->exp0=NULL;bhEff302(e);break;
    case 2:e->exp1=e->exp0;bhEff302(e);break;
    case 3:e->mode0=2;bhEff302(e);break;
    case 4:bhEff302(e);((E02_WORK *)e->exp0)->stg_stt=7;bhEff302(e);break;
    case 5:bhEff302(e);((E02_WORK *)e->exp0)->stg_buf[0]=NULL;bhEff302(e);break;
    case 6:((E17_WORK *)e->exp0)->srt_vtx[2]=16;bhEff300(e);break;
    case 7:e->type=3;bhEff_E02_SandParticle(e);break;
    case 8:e->mdlver=2;bhEff_E02_SandDust(e);break;
    case 9:e->mode0=1;e->type=1;e->exp0=(unsigned char *)1;bhEff002(e);break;
    case 10:bhEff302(e);sys->ef_fncn=512;bhEff302(e);break;
    case 11:e->type=7;bhEff301(e);break;
    case 12:e->flr_no=1;sys->yk_ct=0;e->lkwkp=NULL;bhEff000(e);break;
    case 13:e->type=6;bhEff003(e);break;
    case 14:e->type=3;bhEff005(e);break;
    case 15:e->type=1;e->flr_no=8;bhEff006(e);break;
    case 16:e->mode0=1;e->ct1=128;bhEff013(e);break;
    case 17:e->lkwkp=NULL;bhEff029(e);break;
    case 18:e->mode0=1;e->mode1=1;e->type=255;rom->eff_n=0;bhEff085(e);break;
    case 19:sys->st_flg|=0x40;sys->gm_flg&=~1u;sys->wt_wvp=NULL;bhEff108(e);break;
    case 20:e->mode0=1;e->exp0=(unsigned char *)1;bhEff008(e);break;
    case 21:e->mode0=1;e->exp0=(unsigned char *)1;bhEff100(e);break;
    case 22:e->type=3;bhEff113(e);break;
    case 23:e->mdlver=55;e->pvp=e->pv;bhEff114(e);break;
    case 24:sys->st_flg|=0x2000000u;sys->ef_slow=0;e->pvp=e->pv;bhEff114(e);break;
    case 25:e->mode0=3;bhEff117(e);break;
    case 26:e->mode0=1;e->ct3=9;bhEff118(e);break;
    case 27:e->mode1=3;bhEff122(e);break;
    case 28:e->mdlver=2;bhEff128(e);break;
    case 29:e->mode0=4;e->pvp=e->pv;sys->ef_flg=0;bhEff111(e);break;
    case 30:e->mode0=1;e->ct3=1;((short *)e->pv)[0]=512;bhEff118(e);break;
    case 31:e->lkwkp=NULL;bhEff120(e);break;
    case 32:npSetOffsetUV(NULL,0,0);break;
    case 33:{
        NJS_CNK_MODEL *model=(NJS_CNK_MODEL *)(sys->endp-128);
        short *stream=(short *)(sys->endp-16);memset(model,0,sizeof(*model));
        model->plist=stream;stream[0]=65;stream[1]=-1;
        npSetOffsetUV(model,1,1);break;}
    case 34:e->mode0=1;e->exp0=(unsigned char *)1;bhEff117(e);break;
    case 35:e->mode0=2;bhEff155(e);break;
    case 36:e->mode0=1;e->exp0=(unsigned char *)1;bhEff157(e);break;
    case 37:bhEff155(e);e->ct0=-1;bhEff155(e);break;
    case 38:e->type=1;bhEff159(e);e->ct0=25;e->type=0;bhEff159(e);break;
    case 39:e->sz=NAN;bhEff163(e);break;
    case 40:e->ct1=-32768;bhEff163(e);break;
    case 41:e->ct0=-1;bhEff164(e);break;
    case 42:e->ct0=65536;bhEff165(e);break;
    case 43:e->tex_id=-1;effinit(e);break;
    case 44:getuv(2,e);break;
    case 45:{UV_WORK uv={0};effset(e,&uv,10);break;}
    case 46:setentry(450,0,e);break;
    case 47:e->mode0=1;e->exp0=(unsigned char *)1;bhEff167(e);break;
    case 48:sys->ef_trsn=512;bhEff161(e);break;
    case 49:e->mode0=1;e->ct0=999;e->exp0=(unsigned char *)1;bhEff169(e);break;
    case 50:e->mode0=1;e->exp0=(unsigned char *)1;bhEff171(e);break;
    case 51:e->mode0=1;e->ct1=INT_MIN;bhEff171(e);break;
    case 52:e->type=100;bhEff173(e);break;
    case 53:e->mode0=2;bhEff176(e);break;
    case 54:bhEff176(e);e->ct0=1;bhEff176(e);break;
    case 55:e->ct1=INT_MAX;bhEff178(e);break;
    case 56:e->sz=NAN;bhEff179(e);break;
    case 57:e->sz=-1;bhEff179(e);break;
    case 58:e->sz=INFINITY;bhEff181(e);break;
    case 59:e->type=100;bhEff182(e);break;
    case 60:e->sz=NAN;bhEff182(e);break;
    case 61:e->mode0=1;e->exp0=(unsigned char *)1;bhEff182(e);break;
    case 62:e->ct0=-1;bhEff170(e);break;
    case 63:sys->ef_trsn=512;bhEff180(e);break;
    case 64:e->mode0=1;e->exp0=(unsigned char *)1;bhEff173(e);break;
    case 65:bhEff182(e);e->ct0=999;bhEff182(e);break;
    case 66:e->mode0=2;bhEff145(e);break;
    case 67:e->mode1=3;bhEff148(e);break;
    case 68:e->mtx=NULL;bhEff148(e);break;
    case 69:e->mode0=1;e->frm_no=9;bhEff149(e);break;
    case 70:e->mode0=7;bhEff149(e);break;
    case 71:e->mode0=1;e->exp0=(unsigned char *)1;bhEff151(e);break;
    case 72:e->ct1=12;bhEff152(e);break;
    case 73:e->type=12;bhEff153(e);break;
    case 74:e->mode0=1;e->exp0=(unsigned char *)1;bhEff153(e);break;
    case 75:e->mode0=1;e->exp0=(unsigned char *)1;bhEff_E12_FireSpark(e);break;
    case 76:((P_WRK *)e->exp0)->num=49;bhEff_E00_DrawParticleSpr(e);break;
    case 77:sys->ef_trsn=-1;bhEff147(e);break;
    case 78:e->mtx=(NJS_MATRIX *)e->mtxbuf;bhEff148(e);e->mode1=1;sys->ef_trsn=512;bhEff148(e);break;
    case 79:e->mtx=NULL;bhEff146_Init(e);break;
    case 80:e->ct3=INT_MIN;bhEff_Sub146(e);break;
    case 81:e->mode0=1;e->ct0=20;bhEff191(e);break;
    case 82:e->ct1=15;bhEff193(e);break;
    case 83:e->type=3;bhEff195(e);break;
    case 84:e->mode0=1;e->type=19;e->ct0=8;bhEff196(e);break;
    case 85:sys->ef_opqn=512;bhEff198(e);break;
    case 86:e->mode0=1;e->ct0=8;bhEff200(e);break;
    case 87:e->mode0=1;e->ct0=16;bhEff201(e);break;
    case 88:e->exp0=NULL;bhEff203(e);break;
    case 89:e->type=1;e->sz=0;bhEff203(e);break;
    case 90:e->mode0=2;e->ct0=2;bhEff205(e);break;
    case 91:e->mode0=1;e->ct0=24;bhEff207(e);break;
    case 92:e->lkono=513;bhEff192(e);break;
    case 93:e->mode0=1;e->ct0=4;bhEff185(e);break;
    case 94:e->mode0=1;e->ct0=21;bhEff209(e);break;
    case 95:e->mode0=1;e->type=2;e->ct0=11;bhEff216(e);break;
    case 96:e->type=3;bhEff217(e);break;
    case 97:e->mode0=1;e->exp0=NULL;bhEff143(e);break;
    case 98:e->mode0=1;sys->ond_cx=4;bhEff133(e);break;
    case 99:e->mode0=1;e->hide[0]=4;bhEff138(e);break;
    case 100:e->type=1;e->lkono=0;bhEff220(e);break;
    case 101:e->mdlver=55;bhEff221(e);break;
    case 102:e->mode0=1;e->lkwkp=NULL;bhEff136(e);break;
    case 103:e->type=3;e->sx=0;bhEff219(e);break;
    case 104:e->mode0=1;e->type=1;e->exp0=NULL;bhEff219(e);break;
    case 105:e->mode0=1;e->ct0=13;e->exp0=NULL;bhEff131(e);break;
    case 106:e->type=255;bhEff225(e);break;
    case 107:e->sz=0;bhEff223(e);break;
    case 108:e->lkono=0;bhEff236(e);break;
    case 109:e->lkono=0;bhEff249(e);break;
    case 110:e->type=255;bhEff305(e);break;
    case 111:e->type=255;bhEff306(e);break;
    case 112:((R07_WORK *)((OR_WORK *)e)->free4)->vtx_num=18;bhEff307((OR_WORK *)e);break;
    case 113:((R08_WORK *)((OR_WORK *)e)->free4)->prm_a.vtx_num=17;bhEff308((OR_WORK *)e);break;
    case 114:((R0B_WORK *)((OR_WORK *)e)->free4)->prm_a.vtx_num=1;bhEff30b((OR_WORK *)e);break;
    case 115:bhEff30c((OR_WORK *)e);break;
    case 116:e->mode0=1;e->exp0=NULL;bhEff_E06_Rinpun(e);break;
    case 117:e->mode0=1;e->exp0=NULL;bhEff_E12_FireManager(e);break;
    case 118:e->type=1;e->exp1=NULL;bhEff_E12_Fire(e);break;
    case 119:e->exp0=NULL;bhEff_Sub365(e);break;
    case 120:((D_WORK *)e->exp0)->num=41;bhEff_Sub365(e);break;



    }
    assert(0);
}
static void test_effect_callback_faults(const char *directory,void *arena,size_t arena_size)
{
    static const char *messages[]={"mode/type","auxiliary context","overlapping","mode/type","counts","buffers","vertex index","sand particle state","sand dust animation","UV table/frame","render queue is full","blood pool effect state","links an invalid actor","weapon effect state","explosion animation state","flame UV selector","projectile target","completion link","swap effect placement","water ripple buffer","UV table/frame","auxiliary context","debris emitter type","debris effect state","debris effect state","linked flame mode","linked flame emitter state","selector effect UV frame","gas particle color selector","monitor effect state","linked flame slot","links an invalid actor","UV scroll model","UV scroll command exceeds stream","UV table/frame","sprite animation mode","UV table/frame","UV table/frame","texture bank frame","bubble animation repeat count","bubble animation repeat count","emitter countdown","emitter countdown","initialization texture","quad orientation","UV/color parameters","entry id","UV table/frame","render queue is full","UV table/frame","UV table/frame","sprite repeat counter","sprite blend selector","sprite animation mode","UV table/frame","timed sprite lifetime","emitter interval","emitter interval","emitter interval","smoke color or velocity selector","smoke color or velocity selector","UV table/frame","emitter countdown","render queue is full","UV table/frame","UV table/frame","linked eye/drop effect mode","linked eye/drop effect mode","linked eye/drop matrix","fire sprite state","fire sprite state","UV table/frame","impact emitter phase","impact sprite frame","impact sprite UV pointer","auxiliary context","fire spark particle count","render queue is full","render queue is full","linked eye/drop matrix","eye particle state","late sprite animation frame","late sprite animation frame","late sprite variant","late sprite animation frame","render queue is full","late sprite animation frame","late sprite animation frame","linked particle emitter","linked particle lifetime/variant","late sprite animation frame","late sprite animation frame","burst particle count","environmental animation frame","environmental animation frame","environmental animation frame","dust color variant","auxiliary context","puzzle selector state","oil puzzle LED","debris burst count","debris frame/slowdown","afterimage parent","snow density/area","auxiliary context","UV table/frame","table index out of range","moving sprite lifetime","enemy-attached sprite duration/placement","oriented sprite duration","table index out of range","table index out of range","spline trail state","spark particle state","blood mist state","attached effect model","auxiliary context","auxiliary context","fire manager link","auxiliary context","effect particle count"};
    RecvxLiveTaskProc saved=bhSysTaskJumpTab[0];
    RecvxInputState input={.analog_x=128,.analog_y=128};bhSysTaskJumpTab[0]=callback_fault_task;
    for(callback_fault_case=0;callback_fault_case<(int)(sizeof(messages)/sizeof(*messages));++callback_fault_case){
        assert(!recvxGameBootInit(directory,arena,arena_size));
        assert(recvxGameBootTick(&input)==-1&&recvxGameBootFault());
        if(!strstr(recvxGameBootFault(),messages[callback_fault_case]))
            fprintf(stderr,"callback guard %d: %s\n",callback_fault_case,recvxGameBootFault());
        assert(strstr(recvxGameBootFault(),messages[callback_fault_case]));
    }
    bhSysTaskJumpTab[0]=saved;
    puts("PASS: 121 effect callback fault paths reject invalid modes/types, auxiliary contexts, overlaps, stage/buffer/vertex indices, UV pointers, parents and full queues");
}

static void test_effect_control(const char *directory, void *arena, size_t arena_size)
{
    const char *faults[] = {NULL, "effect links an invalid actor", "effect links an invalid actor joint",
        "effect table index out of range", "invalid original effect texture mapping", "original effect render queue is full"};
    RecvxLiveTaskProc saved = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    bhSysTaskJumpTab[0] = effect_control_task;
    for (effect_fixture_case = 0; effect_fixture_case < 6; ++effect_fixture_case) {
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        if (effect_fixture_case == 0) tick(&input);
        else {
            assert(recvxGameBootTick(&input) == -1);
            assert(strcmp(recvxGameBootFault(), faults[effect_fixture_case]) == 0);
            if (effect_fixture_case == 5) assert(sys->ef_mdfn == 128 && sys->ef_lin[0] == &eff[500]);
        }
    }
    bhSysTaskJumpTab[0] = saved;
    puts("PASS: original effect updates, shadow ground/joints, flame animation/spawning, queue reset and bounded faults");
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

/* Stairs lead from the small corridor to rooms whose compressed and expanded
   copies cannot coexist above the game's permanent allocations in 12 MiB. */
static void test_game_direction_mapping(const char *directory,void *arena,size_t arena_size)
{
    static const uint32_t buttons[]={RECVX_BUTTON_UP,RECVX_BUTTON_DOWN,RECVX_BUTTON_LEFT,RECVX_BUTTON_RIGHT};
    static const unsigned int raw[]={0x1000,0x4000,0x8000,0x2000};
    static const unsigned int logical[]={0x21,0x42,0x4,0x8};
    RecvxInputState input={.analog_x=128,.analog_y=128};
    extern void recvxGameBootReadKeys(const RecvxInputState *);
    extern void bhSetPad(void);
    assert(!recvxGameBootInit(directory,arena,arena_size));
    /* Test the production bridge through the retail pad mapper and all four
       control presets, including edges, release and diagonal directions. */
    for(int preset=0;preset<4;++preset){
        sys->keytype=preset;
        for(unsigned int i=0;i<4;++i){
            input=(RecvxInputState){.analog_x=128,.analog_y=128};
            recvxGameBootReadKeys(&input);bhSetPad();
            input.buttons=buttons[i];recvxGameBootReadKeys(&input);bhSetPad();
            assert(sys->p1per->on==raw[i]);
            assert((sys->pad_on&0x6f)==logical[i]&&(sys->pad_ps&0xf)==(logical[i]&0xf));
            input.buttons=0;recvxGameBootReadKeys(&input);bhSetPad();
            assert(!(sys->pad_on&0x6f)&&sys->p1per->release==raw[i]);
            if(i<2)input.analog_y=i==0?0:255;else input.analog_x=i==2?0:255;
            recvxGameBootReadKeys(&input);bhSetPad();
            assert((sys->pad_on&0x6f)==logical[i]);
        }
    }
    input=(RecvxInputState){.buttons=RECVX_BUTTON_UP|RECVX_BUTTON_RIGHT,.analog_x=128,.analog_y=128};
    recvxGameBootReadKeys(&input);bhSetPad();assert((sys->pad_on&0x6f)==0x29);
    input=(RecvxInputState){.analog_x=128,.analog_y=128};recvxGameBootReadKeys(&input);bhSetPad();
    assert(!sys->pad_on&&!sys->pad_ax&&!sys->pad_ay);
    puts("PASS: PSP D-pad and analog directions through retail pad mapping, all four presets, edges, releases, diagonal and neutral");
}

static unsigned int enemy_invalid_case;
static void invalid_enemy_task(void)
{
    extern void bhEne54(BH_PWORK *);
    BH_PWORK *actor=&ene[0];memset(actor,0,sizeof(*actor));actor->id=54;
    switch(enemy_invalid_case){
    case 0:bhEne54((BH_PWORK *)(uintptr_t)64);break;
    case 1:actor->mode0=255;bhEne54(actor);break;
    case 2:actor->px=NAN;bhEne54(actor);break;
    case 3:actor->mdl_n=1;actor->mdl[0].obj_num=129;bhEne54(actor);break;
    case 4:actor->mnwP=(MN_WORK *)(uintptr_t)64;bhEne54(actor);break;
    case 5:actor->mode0=1;actor->type=255;bhEne54(actor);break;
    }
    assert(!"invalid enemy must reject before using its work");
}
static void test_invalid_enemy_work(const char *directory,void *arena,size_t arena_size)
{
    RecvxLiveTaskProc saved=bhSysTaskJumpTab[0];
    RecvxInputState input={.analog_x=128,.analog_y=128};
    bhSysTaskJumpTab[0]=invalid_enemy_task;
    for(enemy_invalid_case=0;enemy_invalid_case<6;++enemy_invalid_case){
        assert(!recvxGameBootInit(directory,arena,arena_size));
        assert(recvxGameBootTick(&input)==-1);
        assert(strstr(recvxGameBootFault(),enemy_invalid_case==5?"invalid enemy callback":"invalid original enemy"));
    }
    bhSysTaskJumpTab[0]=saved;
    puts("PASS: malformed enemy pointers, phases, positions, model/motion ownership and type dispatch rejected");
}

static unsigned int room_stream_test;
static void room_stream_task(void)
{
    static const char *names[]={"RM_0000.RDX","RM_0010.RDX","RM_0020.RDX","RM_0030.RDX"};
    static const unsigned int lengths[]={5392084,1829316,8105744,8931872};
    static const uint32_t crcs[]={0xaf78e4c2u,0x778dd089u,0x82cab019u,0x0ab1cff7u};
    unsigned int index=room_stream_test;
#if UINTPTR_MAX == UINT32_MAX
    bhInitSystem();
    /* Room relocation uses the object/item pools normally reserved by
       bhFirstGameStart and the texture descriptor pool from bhInitEffect. */
    sys->obwp=(O_WRK *)bhGetFreeMemory(39936,32);
    sys->itwp=(O_WRK *)bhGetFreeMemory(39936,32);
    bhInitPlayer();
    sys->ss_flg|=0x100; /* Use the initial spawn path with these fixture pools. */
    sys->ef_tlist.textures=sys->ef_tex;
    if(index==2){
        RequestReadInsideFile(6,10,sys->memp);
        bhReadPlayerData();bhCalcModel(plp);
    }
#endif
    bhInitReadRDT();
    /* Keep a permanent prefix and a tail guard outside the decoder capacity. */
    unsigned char *arena_end=sys->endp;
    sys->endp-=64;memset(sys->endp,0xa5,64);
    unsigned char *output=freemem+1152*1024;
    memset(output-64,0x5a,64);
    int size=GetFileSize((char *)names[index]);
    unsigned char *source=(unsigned char *)(((uintptr_t)sys->endp-size)&~(uintptr_t)63u);
    assert(!RequestReadIsoFile((char *)names[index],source));
    sys->memp=output;sys->rdtp=source;
    if(index>=2)assert(lengths[index]>(uintptr_t)source-(uintptr_t)output);
    int written=Expand((char *)source,output);
    assert(written==(int)lengths[index]);
    assert(recvxRoomCrc32(output,written)==crcs[index]);
    int valid=recvxGameBootValidateRoomSource(output,written);
    if(valid)fprintf(stderr,"room relocation validation failed: %s\n",names[index]);
    assert(!valid);
    assert(recvxGameBootSourceRemaining(output)==lengths[index]);
    for(unsigned int i=0;i<64;++i)assert(output[-64+(int)i]==0x5a&&sys->endp[i]==0xa5);
#if UINTPTR_MAX == UINT32_MAX
    sys->rdtp=output;sys->rdtsz=written;sys->memp=output+written;
    bhSetRDT();
    assert(rom->mdl.objP&&rom->mdl.obj_num>0&&sys->txr_n>0);
    if(index==2){
        assert(ene[0].mdlver==9&&ene[3].mdlver==32);
        assert(ene[0].mdl[0].texP&&ene[3].mdl[0].texP);
        assert(ene[0].mdl[0].texP==ene[1].mdl[0].texP);
        assert(ene[0].mdl[0].texP!=ene[3].mdl[0].texP);
        assert(*recvxGameBootEnemyTextureSlot(1,32,0)==ene[3].mdl[0].texP);
        assert(recvxGameBootEnemyTextureSlot(1,32,0)!=recvxGameBootEnemyTextureSlot(3,0,0));
        recvxGameBootEnemyTextureReset();
        assert(!*recvxGameBootEnemyTextureSlot(1,32,0));
    }
    while(sys->txr_ct<sys->txr_n){
        unsigned int i=sys->txr_ct;
        bhSetMemPvpTexture(sys->txlp[i],sys->txdp[i],sys->txloff[i]);++sys->txr_ct;
    }
    bhFinishRoom();
    if(index==2){
        for(int i=0;i<rom->ene_n;++i)if(ene[i].id!=1)ene[i].stflg|=0x41000000u;
        RequestRoomSoundBank(0,2,0);recvxGameBootSoundTick();
        sys->sp_flg|=2;
        bhControlEnemy();
        assert(ene[0].exp0&&ene[0].mode0==1&&ene[0].hp>0);
        assert(ene[0].exp1&&((BH_PWORK *)ene[0].exp1)->lkwkp==(unsigned char *)&ene[0]);
        assert(sys->ewk_n>rom->ene_n);
        for(int frame=0;frame<8;++frame){bhControlEnemy();recvxGameBootSoundTick();}
        assert(isfinite(ene[0].px)&&isfinite(ene[0].py)&&isfinite(ene[0].pz));
        BH_PWORK *linked=(BH_PWORK *)ene[0].exp1;
        assert(linked->mlwP==ene[0].mdl);
        /* Opcode 142 checks the linked actor's live bit in the global pool,
           rather than an arena-allocated zombie extension. */
        extern unsigned int bhZombieUpDieCk(void);
        extern unsigned int bhInitMotionPause(void);
        extern unsigned int bhInitMotionPauseEx(void);
        unsigned char damage_command[]={142,0,23,0};
        unsigned char *saved_script=bhScePtr;
        unsigned int saved_room_flags=sys->rm_flg,saved_link_flags=linked->flg;
        sys->rm_flg=0x80000000u;linked->flg&=~2u;bhScePtr=damage_command;
        assert(bhZombieUpDieCk()==1&&sys->rm_flg==0x80000000u&&bhScePtr==damage_command+4);
        linked->flg|=2u;bhScePtr=damage_command;
        assert(bhZombieUpDieCk()==1&&sys->rm_flg==(0x80000000u|(1u<<8)));
        sys->rm_flg=saved_room_flags;linked->flg=saved_link_flags;
        unsigned char pause_command[]={45,0,0,0};
        unsigned char parent_mode=ene[0].mode0,parent_submode=ene[0].mode3;
        unsigned char linked_mode=linked->mode0,linked_submode=linked->mode3;
        int parent_frame=ene[0].frm_no,linked_frame=linked->frm_no;
        unsigned int parent_motion=ene[0].mtn_no,linked_motion=linked->mtn_no;
        MN_WORK *parent_motion_work=ene[0].mnwP;
        bhScePtr=pause_command;assert(bhInitMotionPause()==1);
        assert(ene[0].mode0==5&&ene[0].mode3==4&&linked->mode0==5&&linked->mode3==4);
        pause_command[0]=48;bhScePtr=pause_command;assert(bhInitMotionPauseEx()==1);
        assert(!ene[0].frm_no&&!linked->frm_no&&!ene[0].mtn_no&&!linked->mtn_no);
        ene[0].mode0=parent_mode;ene[0].mode3=parent_submode;ene[0].frm_no=parent_frame;
        ene[0].mtn_no=parent_motion;ene[0].mnwP=parent_motion_work;
        linked->mode0=linked_mode;linked->mode3=linked_submode;
        linked->frm_no=linked_frame;linked->mtn_no=linked_motion;bhScePtr=saved_script;

        /* Replay the exact stair-room instruction reported by real hardware. */
        unsigned char *sub_command=(unsigned char *)rom->evtp+2356;
        assert(recvxGameBootSourceRemaining(sub_command)>=8&&sub_command[0]==103);
        fprintf(stderr,"Stair-room task 2 opcode 103: pc=2356 command=%u operands=%u,%u,%u,%u,%u\n",
            sub_command[1],sub_command[2],sub_command[3],sub_command[4],sub_command[5],sub_command[6]);
        assert(sub_command[1]==143||sub_command[1]==146||sub_command[1]==147);
        BH_PWORK original_parent=ene[0],original_linked=*linked;
        BH_SCEWORK original_task=bhEtask[2];BH_SCEWORK *original_current=bhCetask;
        bhCetask=&bhEtask[2];bhCetask->work=&ene[0];
        bhCetask->mtn_md=13;bhCetask->mdflg=0x12340000;bhCetask->hokan_rate=7;
        bhScePtr=sub_command;assert(bhSubCtr()==1);
        assert(bhScePtr==sub_command+(sub_command[1]==143?6:8));
        assert(ene[0].mode0==sub_command[2]&&ene[0].mode1==sub_command[3]&&ene[0].mode2==sub_command[4]);
        if(sub_command[1]!=147){
            assert(ene[0].mnwP==sys->emtp[1]&&linked->mnwP==sys->emtp[1]&&linked->mode0==1);
            assert(ene[0].mtn_md==13&&ene[0].hokan_rate==7);
        }
        ene[0]=original_parent;*linked=original_linked;bhEtask[2]=original_task;
        /* Also exercise the original light helper, with a registered effect
           object and owned context, rather than the focused test's call spy. */
        BH_PWORK saved_enemy=ene[127];O_WRK saved_light=eff[511];
        unsigned char *saved_memp=sys->memp;
        ene[127]=ene[0];ene[127].id=11;ene[127].exp0=bhGetFreeMemory(128,32);
        O_WRK *light=&eff[511];memcpy(ene[127].exp0+0x60,&light,sizeof(light));
        bhCetask=&bhEtask[2];bhCetask->work=&ene[127];
        unsigned char light_command[]={103,148,0,0};
        static const unsigned char light_modes[]={1,3,0,2};
        for(unsigned int flag=0;flag<4;++flag){
            light_command[2]=flag;bhScePtr=light_command;assert(bhSubCtr()==1);
            assert(bhScePtr==light_command+4&&light->mode0==light_modes[flag]);
        }
        assert(light->ax==0x38e&&light->axp==0x38e);
        ene[127]=saved_enemy;eff[511]=saved_light;sys->memp=saved_memp;
        bhEtask[2]=original_task;bhCetask=original_current;bhScePtr=saved_script;

        /* Drive the full zombie burst/neck-blood controller with the actual
           relocated zombie joints and texture descriptors from the stair room. */
        O_WRK *saved_blood_effects=malloc(sizeof(eff));SYS_WORK *saved_blood_sys=malloc(sizeof(*sys));
        assert(saved_blood_effects&&saved_blood_sys);memcpy(saved_blood_effects,eff,sizeof(eff));*saved_blood_sys=*sys;
        memset(eff,0,sizeof(eff));O_WRK *blood=&eff[0];blood->flg=1;blood->id=356;
        blood->lkwkp=(unsigned char *)&ene[0];blood->px=ene[0].px;blood->py=ene[0].py;blood->pz=ene[0].pz;
        for(int frame=0;frame<80&&blood->flg;++frame)bhEff_Sub356(blood);
        assert(!blood->flg&&blood->mode0==5);
        int blood_children=0;for(int i=1;i<512;++i)blood_children+=!!eff[i].flg;
        assert(blood_children>=20);
        memcpy(eff,saved_blood_effects,sizeof(eff));*sys=*saved_blood_sys;free(saved_blood_effects);free(saved_blood_sys);
        puts("PASS: zombie effect 356 burst and all three neck-blood stages using real stair-room actor joints and textures");

        /* This fixture calls the enemy scheduler directly, so initialize the
           camera pass normally run by the full gameplay frame before drawing. */
        bhInitCamera();cam.px=ene[0].px;cam.py=ene[0].py+2;cam.pz=ene[0].pz+10;
        bhControlCamera();
        recvxGameBootRenderBeginFrame();bhDrawEnemy();bhDrawEneObject();
        assert(!recvxEngineOriginalFault());
        assert(recvxGameBootRenderFrame()->vertex_count>0);
        /* This isolated actor pass has no room/modifier-shadow composition;
           consume it before the full-frame presentation assertions resume. */
        recvxGameBootRenderConsume();
        NJS_POINT3 sound_position={plp->px,plp->py,plp->pz};
        RequestEnemySe(0,&sound_position,0x2300);recvxGameBootSoundTick();
        assert(ChechPlayEnemySe(0,0x2300));
        int native_slots=0;for(int i=0;i<6;++i)native_slots+=recvxAudioSePlaying(i);
        assert(native_slots>0);
        RequestEnemySeEx(0,&sound_position,0x2301,4);recvxGameBootSoundTick();
        for(int i=0;i<4;++i)recvxGameBootSoundTick();
        RequestEnemySe(0,&sound_position,0x2300);
        AllStopEnemySe();recvxGameBootSoundTick();assert(!ChechPlayEnemySe(0,0x2300));
        for(int i=0;i<6;++i)assert(!recvxAudioSePlaying(i));
        /* Exercise the knight controllers with owned relocated fixture poses
           and motion data; boss-specific assets are not packaged here. */
        extern void bhEne54(BH_PWORK *),bhEne55(BH_PWORK *);
        void (*knights[])(BH_PWORK *)={bhEne54,bhEne55};
        for(unsigned int k=0;k<2;++k){
            ene[127]=*plp;BH_PWORK *knight=&ene[127];
            knight->mdl_n=1;knight->mdl[0]=plp->mdl[0];knight->mlwP=knight->mdl;
            knight->id=54+k;knight->type=0;knight->mode0=knight->mode1=knight->mode2=knight->mode3=0;
            knights[k](knight);assert(knight->hp==250&&knight->mode0==1);
            knights[k](knight);assert(knight->mode3==1&&knight->mtn_add==0);
            knight->mode0=3;knight->mode2=knight->mode3=0;
            knights[k](knight);assert(knight->mode3>=1);
            memset(knight,0,sizeof(*knight));
        }
    }
    assert(sys->rmthp&&sys->memp<=sys->endp);
    for(int i=0;i<rom->ene_n;++i)assert(ene[i].mnwP&&ene[i].mdl[0].owP);
    for(unsigned int i=0;i<64;++i)assert(output[-64+(int)i]==0x5a&&sys->endp[i]==0xa5);
#endif
    sys->endp=arena_end;
}
static void test_large_room_streams(const char *directory,void *arena,size_t arena_size)
{
    RecvxLiveTaskProc saved=bhSysTaskJumpTab[0];
    RecvxInputState input={.analog_x=128,.analog_y=128};
    unsigned int empty_motions[514]={0};
    empty_motions[512]=UINT32_MAX;
    assert(!recvxGameBootValidateMotionStream((unsigned char *)empty_motions,513*4));
    empty_motions[512]=0;empty_motions[513]=UINT32_MAX;
    assert(recvxGameBootValidateMotionStream((unsigned char *)empty_motions,sizeof(empty_motions))==-1);
    bhSysTaskJumpTab[0]=room_stream_task;
    for(room_stream_test=0;room_stream_test<4;++room_stream_test){
        assert(!recvxGameBootInit(directory,arena,arena_size));tick(&input);
        /* Each fixture represents a fresh room load, with independent RAM
           texture ownership rather than accumulating all rooms at once. */
        recvxGameBootTexturesShutdown();
    }
    bhSysTaskJumpTab[0]=saved;
#if UINTPTR_MAX == UINT32_MAX
    puts("PASS: all four room/model/motion relocations and finalizations; real zombie initialization, linked actors and movement, native enemy sounds, knight idle/damage controllers; zombie variant 32 cache isolation, same-variant sharing and cache reset");
#endif
    puts("PASS: all four packaged room streams with permanent allocations in 12 MiB, staged stair/large-room expansion, source validation, exact CRCs and arena guards");
}

static int checkpoint_case;
static size_t checkpoint_bytes;
static unsigned char *checkpoint_original_base, *checkpoint_original_limit;
static void checkpoint_task(void)
{
    bhInitSystem();
    checkpoint_bytes = (unsigned char *)&sys->save_end - (unsigned char *)&sys->version;
    checkpoint_original_base = freemem; checkpoint_original_limit = sys->endp;
    assert(checkpoint_bytes == 2100 && keepmem >= freemem + checkpoint_bytes);
    memset(freemem, 0xa5, checkpoint_bytes + 8);
    memset(&sys->version, 0x5a, checkpoint_bytes);
    sys->save_end = 0x12345678; sys->gm_mode = 2;
    sys->ply_id = checkpoint_case < 4 ? checkpoint_case : 0;
    sys->stg_no = 2; sys->rom_no = 17; sys->rcase = 3;
    sys->time = 98765; sys->itm[383] = 0xABCDEF01;
    plp->px = 4.25f; plp->py = -8.5f; plp->pz = 16.75f;
    plp->flr_no = -1; plp->ay = 65530;
    plp->hp = 73; plp->wpnr_no = 4; plp->stflg = 0xFFFFFFFF;
    switch (checkpoint_case) {
    case 4: sys->ply_id = -1; break;
    case 5: sys->ply_id = 4; break;
    case 6: keepmem = freemem + checkpoint_bytes - 1; break;
    case 7: sys->memp = freemem + checkpoint_bytes - 1; break;
    case 8: sys->endp = freemem + checkpoint_bytes - 1; break;
    case 9: freemem += 64; break;
    }
    bhPushGameData();
    assert(memcmp(freemem, &sys->version, checkpoint_bytes) == 0);
    for (unsigned int i = 0; i < 8; ++i) assert(freemem[checkpoint_bytes + i] == 0xa5);
    assert(sys->save_end == 0x12345678 && sys->time == 98765 && sys->itm[383] == 0xABCDEF01);
    assert(sys->version == 10 && sys->flr_no == -1 && sys->ply_stflg[sys->ply_id] == 0x78280000);
    assert(sys->ply_pos.x == 4.25f && sys->ply_pos.y == -8.5f && sys->ply_pos.z == 16.75f);
    assert(sys->ply_ang == 65530 && sys->ply_wno[sys->ply_id] == 4 && sys->ply_hp[sys->ply_id] == 73);
    /* The second checkpoint replaces the first; it retains the complete state. */
    plp->px = -123; plp->hp = 9; sys->time = 99876;
    bhPushGameData();
    assert(memcmp(freemem, &sys->version, checkpoint_bytes) == 0 && sys->ply_pos.x == -123 && sys->ply_hp[sys->ply_id] == 9);
    for (unsigned int i = 0; i < 8; ++i) assert(freemem[checkpoint_bytes + i] == 0xa5);
}

static void test_checkpoint(const char *directory, void *arena, size_t arena_size)
{
    RecvxLiveTaskProc original_init = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    bhSysTaskJumpTab[0] = checkpoint_task;
    for (checkpoint_case = 0; checkpoint_case < 10; ++checkpoint_case) {
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        if (checkpoint_case < 4) tick(&input);
        else {
            assert(recvxGameBootTick(&input) == -1);
            assert(strcmp(recvxGameBootFault(), checkpoint_case < 6 ?
                "invalid original checkpoint player" : "invalid original checkpoint buffer reservation") == 0);
            assert(sys->version == 0x5a5a5a5a && checkpoint_original_base[0] == 0xa5);
            freemem = checkpoint_original_base; sys->endp = checkpoint_original_limit;
        }
    }
    bhSysTaskJumpTab[0] = original_init;
    puts("PASS: original 2100-byte checkpoints, all four player slots, repeated snapshots, excluded marker/guard bytes and invalid reservations/players");
}

extern void (*bhCtrPly_mode0[9])(void);
/* Isolate controller routing/history; real action behavior is tested below. */
#define PLAYER_DISPATCH_PROBE(name) static void probe_##name(void) { recvxGameBootUnsupported(#name); }
PLAYER_DISPATCH_PROBE(bhCPM0_action)
PLAYER_DISPATCH_PROBE(bhCPM0_damage)
PLAYER_DISPATCH_PROBE(bhCPM0_die)
PLAYER_DISPATCH_PROBE(bhCPM0_nage)
PLAYER_DISPATCH_PROBE(bhCPM0_enedam)
PLAYER_DISPATCH_PROBE(bhCPM0_enedie)
PLAYER_DISPATCH_PROBE(bhCPM0_event)
PLAYER_DISPATCH_PROBE(bhCPM0_nothing)
#undef PLAYER_DISPATCH_PROBE
static int player_control_case;
static void player_control_task(void)
{
    EXP_WORK *extra;
    bhInitPlayer();
    plp->mlwP = &plp->mdl[0]; plp->mlwP->obj_num = 22;
    plp->mlwP->objP = (NJS_CNK_OBJECT *)sys->plmdlp;
    plp->mlwP->owP = (O_WORK *)(sys->plmdlp + 8192);
    plp->mnwP = plp->mnwPb = (MN_WORK *)sys->plmthp;
    plp->mode0 = 1; plp->mtn_no = 0;
    plp->px = 1; plp->py = 2; plp->pz = 3; plp->ay = 123;
    plp->mlwP->owP[0].mtx[12] = 7; plp->mlwP->owP[0].mtx[14] = 9;
    extra = (EXP_WORK *)plp->exp0; extra->bpx = 4; extra->bpy = 5; extra->bpz = 6;
    sys->sp_flg = 1;
    switch (player_control_case) {
    case 1: sys->sp_flg = 0; plp->mlwP = NULL; plp->exp0 = NULL; break;
    case 2: plp->stflg |= 0x1000000; plp->mlwP = NULL; plp->exp0 = NULL; break;
    case 3: plp->mode0 = 255; break;
    case 4: sys->ply_id = 4; break;
    case 5: plp->wpnr_no = 23; break;
    case 6: plp->exp0 = NULL; break;
    case 7: plp->exp1 = NULL; break;
    case 8: plp->mlwP = NULL; break;
    case 9: plp->mlwP->obj_num = 21; break;
    case 10: plp->mlwP->objP = NULL; break;
    case 11: plp->mlwP->owP = NULL; break;
    case 12: plp->mtn_no = 512; break;
    case 13: plp->mnwP = NULL; break;
    case 14: plp->flg |= 0x100000; extra->yrct = 0; break;
    case 15: plp->stflg |= 0x281300; extra->dm_ct = 30; break;
    case 16: plp->hp = -1; plp->mode1 = plp->mode2 = plp->mode3 = 255; break;
    case 17: bhCtrPly_mode0[1] = NULL; break;
    default:
        if (player_control_case >= 20) plp->mode0 = player_control_case - 20;
        break;
    }
    bhControlPlayer();
}

static void test_player_control(const char *directory, void *arena, size_t arena_size)
{
    RecvxLiveTaskProc original_init = bhSysTaskJumpTab[0];
    void (*original_callbacks[9])(void);
    memcpy(original_callbacks, bhCtrPly_mode0, sizeof(original_callbacks));
    void (*probes[9])(void) = {NULL, probe_bhCPM0_action, probe_bhCPM0_damage,
        probe_bhCPM0_die, probe_bhCPM0_nage, probe_bhCPM0_enedam, probe_bhCPM0_enedie,
        probe_bhCPM0_event, probe_bhCPM0_nothing};
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    const char *callbacks[] = {NULL, "bhCPM0_action", "bhCPM0_damage", "bhCPM0_die",
        "bhCPM0_nage", "bhCPM0_enedam", "bhCPM0_enedie", "bhCPM0_event", "bhCPM0_nothing"};
    for (unsigned int i=1;i<9;++i) bhCtrPly_mode0[i] = probes[i];
    bhSysTaskJumpTab[0] = player_control_task;
    for (player_control_case = 0; player_control_case < 18; ++player_control_case) {
        const char *expected;
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        if (player_control_case == 1 || player_control_case == 2) {
            tick(&input); assert(plp->px == 1 && plp->pxb == 0 && !recvxGameBootFault()); continue;
        }
        assert(recvxGameBootTick(&input) == -1);
        expected = player_control_case == 3 || player_control_case == 17 ? "invalid player callback" :
            player_control_case >= 4 && player_control_case <= 8 ? "invalid original player control state" :
            player_control_case >= 9 && player_control_case <= 13 ? "invalid original player model or motion work" :
            player_control_case == 14 ? "invalid player turn timer" :
            player_control_case == 16 ? "bhCPM0_die" : "bhCPM0_action";
        assert(strcmp(recvxGameBootFault(), expected) == 0);
        if (player_control_case == 17) for (unsigned int i=1;i<9;++i) bhCtrPly_mode0[i] = probes[i];
        if (player_control_case == 0 || player_control_case == 15) {
            EXP_WORK *extra = (EXP_WORK *)plp->exp0;
            assert(plp->pxb == 1 && plp->pyb == 2 && plp->pzb == 3 && plp->ayb == 123);
            assert(extra->bpxb == 4 && extra->bpyb == 5 && extra->bpzb == 6);
            assert(extra->nlxb == 7 && extra->nlyb == 2 && extra->nlzb == 9);
            if (player_control_case == 15) assert(plp->hp == 159 && extra->dm_ct == 0 && !(plp->stflg & 0x300));
        }
        if (player_control_case == 16)
            assert(plp->mode0 == 3 && !plp->mode1 && !plp->mode2 && !plp->mode3 && plp->mnwP == plp->mnwPb);
    }
    for (player_control_case = 21; player_control_case <= 28; ++player_control_case) {
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        assert(recvxGameBootTick(&input) == -1);
        assert(strcmp(recvxGameBootFault(), callbacks[player_control_case - 20]) == 0);
    }
    memcpy(bhCtrPly_mode0, original_callbacks, sizeof(original_callbacks));
    bhSysTaskJumpTab[0] = original_init;
    puts("PASS: original player update history, poison/death transitions, full mode dispatch, inactive/hidden state and bounded work/callback faults");
}

static BH_PWORK subplayer_fixture;
static NJS_CNK_OBJECT subplayer_object;
static O_WORK subplayer_object_work;
static ML_WORK subplayer_model;
static MN_WORK subplayer_motions[2];
static NJS_MDATA2_MOD subplayer_tracks;
static NJS_MKEY_F_MOD subplayer_positions[4];
static unsigned short subplayer_attributes[4] = {12, 34, 56, 78};
static int subplayer_case;
extern void (*subpl_mv_tbl[1])(BH_PWORK *);

static void reset_subplayer_fixture(void)
{
    memset(&subplayer_fixture, 0, sizeof(subplayer_fixture));
    memset(&subplayer_object, 0, sizeof(subplayer_object));
    memset(&subplayer_object_work, 0, sizeof(subplayer_object_work));
    subplayer_model = (ML_WORK){.obj_num = 1, .objP = &subplayer_object, .owP = &subplayer_object_work};
    subplayer_object_work.flg = 2; /* Keep rotation fixed; exercise real translation tracks. */
    for (unsigned int i = 0; i < 4; ++i)
        subplayer_positions[i] = (NJS_MKEY_F_MOD){{8, 16, 24}};
    subplayer_tracks = (NJS_MDATA2_MOD){{subplayer_positions, NULL}};
    for (unsigned int i = 0; i < 2; ++i)
        subplayer_motions[i] = (MN_WORK){.flg = 0x10, .obj_num = 1, .frm_num = 4,
            .datP = subplayer_positions, .md2P = &subplayer_tracks, .atrP = subplayer_attributes};
    subplayer_fixture.mlwP = &subplayer_model;
    subplayer_fixture.mnwP = subplayer_fixture.mnwPb = subplayer_motions;
    subplayer_fixture.mode0 = 1; subplayer_fixture.mode1 = 1;
    subplayer_fixture.mtn_md = 0x20; subplayer_fixture.mtn_add = 32768;
    subplayer_fixture.ct0 = (int)(0xff9c0000u | 200u); /* Target (-1, 2). */
    subplayer_fixture.ct3 = 100;
}

static void subplayer_task(void)
{
    switch (subplayer_case) {
    case 0: move_subpl(&subplayer_fixture); break;
    case 1: em_sce(&subplayer_fixture); break;
    case 2: bhEne_Event(&subplayer_fixture); break;
    case 3: bhEne_Event2(&subplayer_fixture); break;
    case 4: move_subpl(NULL); break;
    }
}

static void test_subplayer_movement(const char *directory, void *arena, size_t arena_size)
{
    RecvxLiveTaskProc original_init = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    bhSysTaskJumpTab[0] = subplayer_task;
    assert(recvxGameBootInit(directory, arena, arena_size) == 0);
    subplayer_case = 0; reset_subplayer_fixture();
    tick(&input);
    assert(subplayer_fixture.mode3 == 1 && subplayer_fixture.mtn_no == 1);
    assert(subplayer_fixture.hokan_count == 7 && subplayer_fixture.frm_no == 32768);
    assert(subplayer_fixture.mtn_attr == 12 && subplayer_object.pos[0] == 1 && subplayer_object.pos[1] == 2);
    tick(&input);
    assert(subplayer_fixture.hokan_count == 6 && subplayer_fixture.frm_no == 65536);
    assert(subplayer_object.pos[0] == 2 && subplayer_object.pos[2] == 6);
    for (int i = 0; i < 7; ++i) tick(&input);
    assert(subplayer_fixture.hokan_count == 0 && subplayer_fixture.frm_no == 32768);
    assert(subplayer_object.pos[0] == 8 && subplayer_object.pos[1] == 16 && subplayer_object.pos[2] == 24);

    /* Every authored movement/event selector reaches its original handler. */
    for (int route = 0; route < 4; ++route) for (int command = 0; command < 15; ++command) {
        reset_subplayer_fixture(); subplayer_case = route; subplayer_fixture.ct2 = command;
        tick(&input);
        assert(subplayer_fixture.mode3 == 1 && subplayer_fixture.mtn_no == 1);
    }
    reset_subplayer_fixture(); subplayer_case = 0; subplayer_fixture.ct2 = 1;
    tick(&input);
    assert(subplayer_fixture.ay == 182 && fabsf(subplayer_fixture.spd - 1) < 0.00001f);
    assert(subplayer_fixture.px < 0 && subplayer_fixture.pz < 0);
    assert(fabsf(hypotf(subplayer_fixture.px, subplayer_fixture.pz) - 1) < 0.001f);
    reset_subplayer_fixture(); subplayer_fixture.ay = 65530;
    assert(bhSub_DirTarget(&subplayer_fixture, 0, -10, 182) == 6);
    subplayer_fixture.ay = 0;
    assert(bhSub_DirTarget(&subplayer_fixture, 10, 0, 182) == -182);

    /* A parked actor stays parked until its original script changes mode3. */
    reset_subplayer_fixture(); subplayer_fixture.mode3 = 3; subplayer_case = 0;
    tick(&input); assert(subplayer_fixture.mode3 == 3 && subplayer_fixture.frm_no == 0 && subplayer_object.pos[0] == 0);

    for (int i = 0; i < 7; ++i) {
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        reset_subplayer_fixture();
        subplayer_case = i < 2 ? 0 : i < 4 ? 1 : i < 6 ? 3 : 4;
        if (i < 2) subplayer_fixture.mode2 = i ? 255 : 1;
        else subplayer_fixture.ct2 = i & 1 ? -1 : 15;
        assert(recvxGameBootTick(&input) == -1);
        assert(strstr(recvxGameBootFault(), i < 2 || i == 6 ? "invalid subplayer move mode" :
            i < 4 ? "invalid subplayer event command" : "invalid subplayer movement command"));
        assert(subplayer_fixture.mode3 == 0 && subplayer_fixture.frm_no == 0);
    }
    {
        void (*original_move)(BH_PWORK *) = subpl_mv_tbl[0];
        assert(recvxGameBootInit(directory, arena, arena_size) == 0);
        reset_subplayer_fixture(); subplayer_case = 0; subpl_mv_tbl[0] = NULL;
        assert(recvxGameBootTick(&input) == -1 && strstr(recvxGameBootFault(), "invalid subplayer move mode"));
        subpl_mv_tbl[0] = original_move;
    }
    bhSysTaskJumpTab[0] = original_init;
    puts("PASS: original NPC movement/event dispatch, real motion blending/looping, bounded turning/speed, parked state and invalid/null selectors");
}

static void inventory_resource_task(void)
{
    sys->mn_stack[0] = 0x105; /* Original inventory texture-read stage: 5/1. */
    sys->mn_setct = 1;
    bhSysCallMonitor();
}

static void inventory_transition_task(void) { bhSysCallMonitor(); }
static void inventory_close_task(void) { ItemTaskCheck(); }
static void inventory_ui_task(void) { bhSysCallMonitor(); StatusMain(); }

static void test_inventory_resource(const char *directory, void *arena, size_t size)
{
    RecvxLiveTaskProc original = bhSysTaskJumpTab[0];
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    assert(recvxGameBootInit(directory, arena, size) == 0);
    bhSysTaskJumpTab[0] = inventory_resource_task;
    tick(&input);
    assert(sys->mn_md0 == 5 && sys->mn_md1 == 2 && sys->subtxp);
    assert(!((uintptr_t)sys->subtxp & 63u) && GetReadFileStatus() == 0);
    assert(recvxGameBootSourceRemaining(sys->subtxp) == (size_t)GetInsideFileSize(4, 145));
    bhSysTaskJumpTab[0] = original;
    puts("PASS: original inventory monitor loads ITEM1 entry 145 and advances past archive read");
}

static void test_gameplay_screen_saver(const char *directory, void *arena, size_t size)
{
    PDS_PERIPHERAL peripheral = {0};
    assert(recvxGameBootInit(directory, arena, size) == 0);
    recvxGameBootDrawDiscard();
    sys->p1per = &peripheral;
    bhInitScreenSaver();
    for (unsigned int i = 0; i < 10799; ++i) bhSysCallScreenSaver();
    assert(sys->ssv_tim == 10799 && sys->ssv_md0 == 0);
    bhSysCallScreenSaver();
    assert(sys->ssv_md0 == 1 && sys->ssv_hkn == 300 && sys->ssv_an == 0);
    for (unsigned int i = 0; i < 300; ++i) {
        size_t count;
        bhSysCallScreenSaver();
        const RecvxBootDraw *draws = recvxGameBootDraws(&count);
        assert(count == 1 && !draws[0].texture && fabsf(draws[0].depth - .8f) < .00001f);
        assert((draws[0].color & 0xffffffu) == 0);
    }
    assert(sys->ssv_md0 == 2 && sys->ssv_an == 128);
    peripheral.on = 0x800;
    bhSysCallScreenSaver();
    assert(sys->ssv_md0 == 0 && sys->ssv_tim == 0 && sys->ssv_an == 0);
    peripheral.on = 0;
    peripheral.x1 = 48;
    bhSysCallScreenSaver(); assert(sys->ssv_tim == 1);
    peripheral.x1 = 49;
    bhSysCallScreenSaver(); assert(sys->ssv_tim == 0);
    peripheral.x1 = 0;
    sys->cb_flg = 4;
    bhSysCallScreenSaver(); assert(sys->ssv_tim == 0);
    sys->cb_flg = 0;
    plp->stflg = 0x40000;
    bhSysCallScreenSaver(); assert(sys->ssv_tim == 0);
    plp->stflg = 0;
    sys->p1per = NULL;
    bhSysCallScreenSaver(); assert(sys->ssv_tim == 1);
    bhSetScreenSaver(128, 300);
    sys->p1per = &peripheral;
    peripheral.y1 = -49;
    bhSysCallScreenSaver();
    assert(sys->ssv_md0 == 0 && sys->ssv_an == 0 && sys->ssv_tim == 0);
    sys->p1per = NULL;
    puts("PASS: original gameplay screen saver idle threshold, dimming draw queue, input wake, analog deadzone and cinematic/player suppression");
}

#if UINTPTR_MAX == UINT32_MAX
static uint32_t texture_digest(const RecvxBootTexture *texture)
{
    uint32_t hash = 2166136261u;
    const unsigned char *parts[] = {texture->source, texture->pixels,
        (const unsigned char *)texture->palette};
    const size_t sizes[] = {texture->source_size, texture->width * texture->height,
        sizeof(texture->palette)};
    for (unsigned int part = 0; part < 3; ++part)
        for (size_t i = 0; i < sizes[part]; ++i) hash = (hash ^ parts[part][i]) * 16777619u;
    return hash;
}

static void test_room_texture_save(void)
{
    NJS_TEXLIST *list = rom->mdl.texP;
    uint32_t digests[256], indices[256];
    assert(list && list->nbTexture <= 256);
    for (unsigned int i = 0; i < list->nbTexture; ++i) {
        digests[i] = texture_digest(recvxGameBootTextureGet(list, i));
        indices[i] = ((NJS_TEXMEMLIST *)(uintptr_t)list->textures[i].texaddr)->globalIndex;
    }
    unsigned char *previous = sys->memp;
    sys->memp = (unsigned char *)bhCopyTexmem2MainmemSub(list, (char *)previous);
    assert(sys->memp > previous && sys->memp <= sys->endp);
    njReleaseTexture(list);
    bhCopyMainmem2Texmem(list);
    for (unsigned int i = 0; i < list->nbTexture; ++i) {
        assert(texture_digest(recvxGameBootTextureGet(list, i)) == digests[i]);
        assert(((NJS_TEXMEMLIST *)(uintptr_t)list->textures[i].texaddr)->globalIndex == indices[i]);
    }
    fprintf(stderr, "PASS: all %u original room textures survive arena backup/release/restore (%u bytes)\n",
        list->nbTexture, (unsigned int)(sys->memp - previous));
    sys->memp = previous;
}

static void test_loaded_player_restore(void)
{
    static const short health[] = {-1, 29, 30, 119, 120, 160};
    extern const unsigned short PlMtnAct[2][3][7];
    assert(sys->ply_id == 0);
    for (unsigned int i = 0; i < sizeof(health) / sizeof(health[0]); ++i) {
        sys->ply_hp[0] = health[i]; sys->ply_stflg[0] = 0;
        bhResetPlayer();
        int damage = health[i] >= 120 ? 0 : health[i] >= 30 ? 1 : 2;
        EXP_WORK *expression = (EXP_WORK *)plp->exp0;
        assert(plp->hp == health[i] && expression->dmlvl == damage && plp->mlwP == plp->mdl);
        assert(sys->obwp[2].id == 1211 && sys->obwp[2].lkono == 5);
        assert(sys->obwp[2].mdl[0].objP == plp->mdl[4].objP);
        bhStandPlayerMotion();
        assert(plp->mtn_no == PlMtnAct[expression->wpntp][damage][0] && plp->frm_no == 0);
        assert(plp->mnwP == plp->mnwPb && !plp->hokan_count && !plp->hokan_rate);
        assert(plp->mode0 == 1 && !plp->mode1 && !plp->mode2 && !plp->mode3);
        assert(!recvxEngineOriginalFault());
    }
    puts("PASS: actual loaded Claire reset/equipment and standing injury motions through original motion/model calculation");
    unsigned char *previous = sys->memp;
    plp = &ene[0];
    bhInitRoomChangePlayer();
    assert(plp == &ply && plp->mlwP->texP == plp->txp[0]);
    assert(plp->mode0 == 1 && !plp->mode1 && !plp->mode2 && !plp->mode3);
    assert(plp->frm_no == 65536 && !plp->hokan_count && !plp->hokan_rate);
    assert(plp->exp2 >= previous && plp->exp2 + 1712 == sys->memp && !((uintptr_t)plp->exp2 & 31u));
    assert(((EXP_WORK *)plp->exp0)->bpy == plp->py + 12.5f);
    assert(((short *)plp->exp1)[34] == -1 && !plp->kdnp && !recvxEngineOriginalFault());
    puts("PASS: actual loaded room-change player floor/shadow/texture/motion setup and aligned pitch allocation");
}

static void loaded_arm_ik_task(void)
{
    NJS_POINT3 offset={1.8f,-3,-3},target;
    njCalcPoint(&plp->mlwP->owP[4].mtx,&offset,&target);
    bhArmIkMdk(plp,6,&target,35498);
}

static void test_loaded_player_interactions(void)
{
    SYS_WORK *saved=malloc(sizeof(*sys));assert(saved);*saved=*sys;
    BH_PWORK player=*plp;
    ATR_WORK *old_records=rom->etcp;int old_count=rom->etc_n;
    ATR_WORK *record=(ATR_WORK *)(((uintptr_t)sys->memp+3u)&~(uintptr_t)3u);
    assert((unsigned char *)(record+1)<sys->endp);
    memset(record,0,sizeof(*record));
    record->flg=1;record->flr_no=plp->flr_no;
    record->px=plp->px-1000;record->pz=plp->pz-1000;record->w=record->d=2000;
    record->prm0=2;record->prm1=7;record->prm2=3;record->prm3=9;
    rom->etcp=record;rom->etc_n=1;sys->metc_n=0;
    sys->cb_flg=0;sys->gm_flg&=~9u;
    bhCheckExmAtari(plp);
    assert((sys->cb_flg&0x101)==0x101&&(plp->stflg&0x80000000u)&&
        sys->door.stg_no==2&&sys->door.rom_no==7&&sys->door.pos_no==3&&sys->door.dor_tp==9);
    sys->ddmd=0;sys->sp_flg=0x201;sys->ts_flg|=0x800;
    sys->pad_ps=sys->pad_on=0x200;
    bhStartDoorDemo();
    assert(sys->ddmd==1&&sys->mn_mode0==2&&!sys->mn_mode1&&!sys->mn_mode2&&!sys->mn_mode3);
    assert(sys->stg_no==2&&sys->rom_no==7&&sys->pos_no==3&&sys->door.dor_tp==9);
    assert((sys->ts_flg&0x100800u)==0x100000u&&sys->sp_flg==0x248&&!sys->pad_ps&&!sys->pad_on);
    assert(sys->fade_ap==255&&sys->fade_hkn==11&&sys->ef_ct==sys->ef_ctb&&!sys->error);
    assert(!recvxAudioFault()&&!recvxAudioNativeError());
    SYS_WORK *waiting=malloc(sizeof(*sys));assert(waiting);*waiting=*sys;
    bhStartDoorDemo();assert(!memcmp(sys,waiting,sizeof(*sys)));free(waiting);
    *plp=player;sys->cb_flg=0;sys->st_flg&=~0x80u;
    record->type=3;record->attr=1;record->prm0=0;record->prm2=0;
    sys->pad_ps=0x200;
    bhCheckExmAtari(plp);
    assert((sys->cb_flg&8)&&(sys->st_flg&0x84)==0x84&&sys->pad_psb==0x200&&!sys->pad_ps);
    *plp=player;sys->cb_flg=0;
    O_WRK item=sys->itwp[0];sys->itwp[0].flg=1;sys->itwp[0].id=34;
    record->type=4;record->attr=0;record->prm0=0;
    bhCheckExmAtari(plp);
    assert(sys->sb_id==34&&(sys->cb_flg&0x10));
    sys->itwp[0]=item;*plp=player;*sys=*saved;free(saved);
    rom->etcp=old_records;rom->etc_n=old_count;
    puts("PASS: real loaded Claire interactions and door-start handoff use original destination, native sound/fade, timer retention and unchanged waiting state");
}

/* Exercise the retail controllers against the real relocated player resources. */
static void test_loaded_player_remaining(void)
{
    SYS_WORK *saved=malloc(sizeof(*sys)); assert(saved); *saved=*sys;
    BH_PWORK player=*plp;
    EXP_WORK expression=*(EXP_WORK *)plp->exp0;
    unsigned char head[124]; memcpy(head,plp->exp1,sizeof(head));
    CAM_WORK camera=cam;
    O_WRK *effects=malloc(sizeof(eff)); assert(effects); memcpy(effects,eff,sizeof(eff));
    unsigned int *old_inventory=swork.pip;
    ATR_WORK *target=(ATR_WORK *)(((uintptr_t)sys->memp+3u)&~(uintptr_t)3u);
    assert((unsigned char *)(target+1)<sys->endp);
    memset(target,0,sizeof(*target)); target->flg=1; target->prm2=8;
#define RESET_PLAYER() do { *sys=*saved; *plp=player; *(EXP_WORK *)plp->exp0=expression; \
    memcpy(plp->exp1,head,sizeof(head)); cam=camera; plp->mode1=plp->mode2=plp->mode3=0; \
    plp->flg&=~0x400000u; plp->frm_no=0; sys->pad_on=sys->pad_ps=0; } while(0)
    for (int direction=0;direction<2;++direction) for (int level=0;level<4;++level) {
        RESET_PLAYER(); plp->mode1=direction; plp->mode2=level;
        bhCPM0_damage();
        assert(plp->mode3==1 && plp->mtn_no==(unsigned int)(71+direction+level*2));
        assert(plp->stflg&0x10000u);
        plp->flg|=0x400000; bhCPM0_damage();
        assert(plp->mode0==1 && !plp->mode1 && !plp->mode2 && !plp->mode3);
        assert(!(plp->stflg&0x10000u));
    }
    for(int direction=0;direction<2;++direction) {
        RESET_PLAYER(); plp->mode1=direction;
        bhCPM0_die(); assert(plp->mode3==1 && plp->mtn_no==(unsigned int)(25+2*direction));
        bhCPM0_die(); assert(plp->mode3==2 && plp->mtn_no==(unsigned int)(26+2*direction));
    }
    void (*enemy_modes[])(void)={bhCPM0_nage,bhCPM0_enedam,bhCPM0_enedie};
    for(unsigned int i=0;i<3;++i) {
        RESET_PLAYER(); *(int *)plp->exp1=4;
        sys->gm_flg|=0x40; sys->st_flg|=0x800000;
        enemy_modes[i]();
        EXP_WORK *e=(EXP_WORK *)plp->exp0;
        assert(e->spx==plp->px && e->spz==plp->pz && !e->arp);
        assert((*(int *)plp->exp1&0x1e4)==0x1e0 && (sys->st_flg&4));
        assert(!(sys->gm_flg&0x40) && (sys->gm_flg&0x800) && (sys->pt_flg&1));
    }
    for(unsigned int action=0;action<10;++action) {
        RESET_PLAYER(); plp->mode2=action;
        plp->mode1=action==5?0:1;
        if(action==8) plp->mode1=42;
        if(action==6 || action==7) plp->wpnr_no=0;
        bhCPM0_event();
        assert(plp->mode3>0 && plp->mtn_no<512 && !recvxGameBootFault());
    }
    void (*traversal[])(void)={bhCPM2_act_kdu,bhCPM2_act_kdd,bhCPM2_act_dnu,
        bhCPM2_act_dnd,bhCPM2_act_psh,bhCPM2_act_hsu,bhCPM2_act_hsd,bhCPM2_act_rpsh};
    for(unsigned int i=0;i<sizeof(traversal)/sizeof(traversal[0]);++i) {
        RESET_PLAYER(); sys->pl_htp=target;
        traversal[i](); assert(plp->mode3==1 || plp->mode3==2);
        plp->ay=plp->ayp; traversal[i](); assert(plp->mode3==3);
        traversal[i](); assert(plp->mode3>=4);
        if (i==5 || i==6) {
            traversal[i](); assert(plp->mode3==5);
            plp->flg|=0x400000; plp->ct0=1; plp->frm_no=0;
            traversal[i](); assert(plp->mode3==6);
            plp->frm_no=0; traversal[i]();
            assert(plp->mode0==1 && !plp->mode1 && !plp->mode2 && !plp->mode3);
        }
    }
    RESET_PLAYER(); bhCPM2_act_cro(); assert(plp->mtn_no==45 && plp->mode3==1);
    plp->flg|=0x400000; bhCPM2_act_cro();
    assert(!plp->mtn_add && !(sys->st_flg&4) && (sys->cb_flg&0x10));
    RESET_PLAYER(); sys->ewk_n=0;
    GA_WORK knife={0}; knife.wpn_no=0; knife.px=knife.gx=100000;
    knife.py=knife.gy=100000; knife.pz=knife.gz=100000; knife.vz=1; knife.r=0.1f;
    bhCheckKnifeAtari(&knife); assert(!recvxGameBootFault());
    /* A real capsule hit must choose a joint, apply knife damage once, and
       play its hit sound through the actual compressed weapon bank. */
    RequestArmsSoundBank(0); recvxGameBootSoundTick();
    BH_PWORK original_enemy=ene[0]; ene[0]=player;
    ene[0].mlwP=ene[0].mdl; ene[0].id=1; ene[0].flg=0x21; ene[0].stflg=0;
    memset(ene[0].dam,0,sizeof(ene[0].dam)); ene[0].comb_flg=0;
    ene[0].watr=(NJS_CAPSULE){{100000,99999,100001},{100000,100001,100001},1};
    sys->ewk_n=1; sys->st_flg&=~0x40u;
    knife.wpn_no=2; knife.gz=100002; knife.r=1;
    bhCheckKnifeAtari(&knife);
    int joint=ene[0].djnt_no;
    assert((ene[0].flg&4) && joint>0 && joint<22 && ene[0].dam[joint]==10);
    assert((ene[0].comb_flg&0x10) && !recvxAudioFault() && !recvxAudioNativeError());
    bhCheckKnifeAtari(&knife); assert(ene[0].dam[joint]==10);
    ene[0]=original_enemy;
    RESET_PLAYER();
    char route[]={0,1,-1}; NJS_POINT3 before={plp->px,plp->py,plp->pz},offset;
    bhCalcFixOffset(plp,route,NULL,&offset); bhFixPositionXYZ(plp,route);
    assert(fabsf(plp->px-(before.x-offset.x))<0.0001f);
    assert(fabsf(plp->py-(before.y-offset.y))<0.0001f);
    assert(fabsf(plp->pz-(before.z-offset.z))<0.0001f);
    RESET_PLAYER();
    bhSetWaterSplash(plp,17,0,1,1,1);
    NJS_POINT3 splash={1,2,3}; bhSetWaterSplash3(&splash,1234,0,1,2,3);
    assert(sys->ef.id==11 && sys->ef.type==0 && sys->ef.py==2 && sys->ef.sy==2);
    assert(sys->ef.ay==1234-16384);
    RESET_PLAYER(); cam.mode0=0; cam.ct0=rom->cutp[cam.ncut].cam[cam.camver].spd;
    bhControlPlEyeCamera(0x4000); assert(cam.mode0==1 && cam.pers==11832);
    assert(cam.px==plp->mlwP->owP[5].mtx[12]);
    bhControlPlEyeCamera(0x4000); assert(cam.mode0==1);
    bhControlPlEyeCamera(0); assert(cam.mode0==2 && !cam.ct0);
    cam.ct0=rom->cutp[cam.ncut].cam[cam.camver].spd;
    bhControlPlEyeCamera(0); assert(!(cam.flg&4));
    CAM_WORK monitor=cam; bhControlMonitorCamera(); assert(!memcmp(&cam,&monitor,sizeof(cam)));
    extern void *vpDummy;
    NJS_MODEL dummy={0}; njDrawModel(&dummy); assert(vpDummy==&dummy); njDrawModel(NULL); assert(!vpDummy);
    *sys=*saved; *plp=player; *(EXP_WORK *)plp->exp0=expression;
    memcpy(plp->exp1,head,sizeof(head)); cam=camera; swork.pip=old_inventory;
    memcpy(eff,effects,sizeof(eff)); free(effects); free(saved);
#undef RESET_PLAYER
    puts("PASS: real loaded damage/death, enemy-controlled states, ten scripted movement routes, eight traversal routes and both ladder exits, crouch, knife miss/hit/damage/sound, XYZ fix, water splashes and eye-camera handoffs");
}


static void test_remaining_game_services(void)
{
    SYS_WORK *saved=malloc(sizeof(*sys));assert(saved);*saved=*sys;
    O_WRK *effects=malloc(sizeof(eff));assert(effects);memcpy(effects,eff,sizeof(eff));
    BH_PWORK player=*plp,enemy=ene[127];CAM_WORK camera=cam;
    NJS_CNK_OBJECT *objects=malloc(rom->mdl.obj_num*sizeof(*objects));assert(objects);
    memcpy(objects,rom->mdl.objP,rom->mdl.obj_num*sizeof(*objects));
    LGT_WORK *lights=malloc(rom->lgt_n*sizeof(*lights));assert(lights);
    memcpy(lights,rom->lgtp,rom->lgt_n*sizeof(*lights));
    /* Texture push/pop preserves RAM surfaces while restoring selection and
       palette state across the original display-mode transition. */
    njSetTexture(rom->mdl.texP);njSetTextureNum(0);
    const RecvxBootTexture *original=recvxGameBootTextureGet(rom->mdl.texP,0);
    uint32_t digest=texture_digest(original),palette=palbuf[0];
    bhPushAllTexture();palbuf[0]^=0xffffffffu;njSetTextureNum(1);bhPopAllTexture();
    assert(palbuf[0]==palette&&texture_digest(original)==digest);
    memset(eff,0,sizeof(eff));
    sys->evt_tim=0;sys->sp_flg=0x200;sys->gm_flg=0x20000000;
    sys->ts_flg=0x15e00;sys->st_flg=0x200;sys->cb_flg=0;
    sys->ev_flg[2]=0;bhStFlg(sys->ev_flg,69);
    bhCheckEvtTimer();assert(plp->hp==-1&&(sys->cb_flg&0x2000));
    assert(!bhCkFlg(sys->ev_flg,69));
    O_WRK *expiry=NULL;
    for(unsigned int i=0;i<512;++i)if((eff[i].flg&1)&&eff[i].id==26){expiry=&eff[i];break;}
    assert(expiry);
    for(int frame=0;frame<32;++frame){sys->ef_trs2dn=0;bhEff026(expiry);}
    assert(!expiry->flg);
    sys->ts_flg=0x15e00;sys->sp_flg=0x200;sys->cb_flg=0;bhStFlg(sys->ev_flg,67);
    bhCheckEvtTimer();assert(sys->sp_flg==0x20&&(sys->gm_flg&0x400)&&!(sys->ts_flg&0x4000));
    *sys=*saved;*plp=player;cam=camera;memset(eff,0,sizeof(eff));
    plp->flg&=~4u;bhInitPlEyeCamera();
    assert(cam.pers==11832&&(sys->gm_flg&0xe0)==0xe0&&!(sys->st_flg&1));
    sys->st_flg|=0x800000;plp->wax=1024;cam.ppers=10000;
    bhSetPlEyeCamera();assert(cam.ax==-1024&&cam.ay==-plp->ay&&cam.pers==11468);
    bhSetEventFixedCut(0,0);
    assert(cam.evc_no==0&&!cam.keyf_no&&(cam.flg&2)&&cam.pers==rom->evcp[0].keyf[0].pers);
    *sys=*saved;*plp=player;cam=camera;
    NJS_TEXNAME scope_names[3];for(int i=0;i<3;++i)scope_names[i]=rom->mdl.texP->textures[0];
    NJS_TEXLIST scope_list={scope_names,3};ML_WORK *weapon_model=sys->obwp->mlwP;
    NJS_TEXLIST *weapon_textures=weapon_model->texP;weapon_model->texP=&scope_list;
    cam.flg&=~2u;cam.pers=11832;sys->obwp->type=13;
    bhDrawScope();size_t overlays;recvxGameBootDraws(&overlays);assert(overlays>=17);
    weapon_model->texP=weapon_textures;Ps2ClearOT();
    *sys=*saved;*plp=player;cam=camera;sys->ewk_n=0;sys->en_objn=0;
    ene[127]=player;BH_PWORK *ant=&ene[127];ant->id=71;ant->type=0;ant->flg=1;ant->stflg=0;
    ant->mode0=ant->mode1=ant->mode2=ant->mode3=0;ant->exp0=NULL;
    O_WORK *poses=bhGetFreeMemory(4*player.mdl[0].obj_num*sizeof(O_WORK),64);
    for(int i=0;i<4;++i){ant->mdl[i]=player.mdl[0];ant->mdl[i].owP=poses+i*player.mdl[0].obj_num;}
    ant->mlwP=ant->mdl;ant->mnwP=ant->mnwPb=player.mnwPb;
    for(unsigned int frame=0;frame<13;++frame){
        bhEne71(ant);
        assert(ant->mode0==1&&ant->mode2==(frame+1)%3&&ant->mode3==((frame+1)/3)%4);
        assert(ant->mlwP==&ant->mdl[ant->mode3]&&ant->exp0);
    }
    ene[127]=enemy;*sys=*saved;*plp=player;cam=camera;
    sys->pt_flg=0x20;sys->gm_flg=0x80;sys->ewk_n=0;sys->en_objn=0;
    bhEtcMirrorDrawModel();assert(!recvxGameBootFault());Ps2ClearOT();
    /* Use owned real TIM2 sources to initialize both reserved capture IDs. */
    NJS_TEXNAME monitor_name={0};NJS_TEXLIST monitor_list={&monitor_name,1};
    sys->ren_tlist=(NJS_TEXLIST){sys->ren_tex,1};
    recvxGameBootTextureInstall(&sys->ren_tlist,0,original->source,original->source_size,0,1,0,0);
    recvxGameBootTextureInstall(&monitor_list,0,original->source,original->source_size,0,2,0,0);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x)
        sys->rpb[y*16+x]=(NJS_TEXTUREH_VTX){.x=x*(640.0f/15),.y=y*(480.0f/15),.z=0.83f,
            .u=x/15.0f,.v=y/15.0f*(480.0f/512),.bcol=0xffffffffu};
    unsigned int before=scene_captures;
    bhDrawFullScreenRenderTexture();assert(scene_captures==before+1);
    const RecvxBootTexture *captured=recvxGameBootTextureGet(&sys->ren_tlist,0);
    assert(captured->rgba8888&&captured->width==512&&captured->pixels[0]==0x7f);
    assert(recvxGameBootRenderFrame()->vertex_count>=1350);Ps2ClearOT();
    NJS_MATRIX inverse;njSetMatrix(&inverse,cam.mtx);assert(njInvertMatrix(&inverse));
    NJS_POINT3 view_point={0,0,50};njCalcPoint(&inverse,&view_point,(NJS_POINT3 *)&eff[0].px);
    eff[0].type=0;eff[0].stflg=0;sys->ren_idx[0]=0;
    extern unsigned int Ps2_rendertex_initflag;Ps2_rendertex_initflag=1;
    CAM_WORK before_camera=cam;NJS_MATRIX before_matrix;njSetMatrix(&before_matrix,cam.mtx);
    bhDrawSmallScreenRenderTexture();assert(scene_captures==before+2);
    assert(!memcmp(&cam,&before_camera,sizeof(cam))&&!memcmp(cam.mtx,before_matrix,sizeof(before_matrix)));
    assert(recvxGameBootTextureGet(&monitor_list,0)->rgba8888);Ps2ClearOT();
    njReleaseTexture(&sys->ren_tlist);njReleaseTexture(&monitor_list);
    *sys=*saved;*plp=player;cam=camera;sys->pt_flg=0;sys->ewk_n=0;sys->en_objn=0;
    /* Read/setup every packaged door type, then finish one complete demo. */
    extern _door_wrk DoorWrk;
    for(unsigned int type=0;type<86;++type){
        sys->door.dor_tp=type;sys->door.flg=0;bhInitDoor();
        int code=bhReadDoorData();
        /* MRY/125 is the retail zero-filled placeholder for door type 54. */
        if(type==54){assert(code==-2);continue;}
        assert(code==2);
        recvxGameBootSoundTick();assert(bhReadDoorData()==1);
        bhSetDoor();assert(DoorWrk.objP&&DoorWrk.dmo_mode==2);
        int frames=0;
        do{bhControlDoor();Ps2DrawOTag();Ps2ClearOT();++frames;}
        while(type==0&&!(DoorWrk.status&0x40)&&frames<2048);
        if(type==0)assert((DoorWrk.status&0x40)&&frames>1&&frames<2048);
        bhExitDoor();
    }
    memcpy(rom->mdl.objP,objects,rom->mdl.obj_num*sizeof(*objects));
    memcpy(rom->lgtp,lights,rom->lgt_n*sizeof(*lights));
    *sys=*saved;*plp=player;cam=camera;ene[127]=enemy;memcpy(eff,effects,sizeof(eff));
    njSetTexture(rom->mdl.texP);njSetTextureNum(0);
    free(objects);free(lights);free(effects);free(saved);
    puts("PASS: texture state push/pop, timer expiry/effect lifetime, eye/fixed cameras, scope, ant variants, mirror submission, full/monitor captures and restoration; 85 real door packages, retail empty-slot rejection and one complete door demo");
}

static void test_loaded_arm_ik(void)
{
    RecvxInputState input={.analog_x=128,.analog_y=128};
    RecvxLiveTaskProc original=bhSysTaskJumpTab[0];
    NJS_CNK_OBJECT *objects=plp->mlwP->objP;
    int original_angles[4][3];
    for(unsigned int i=0;i<4;++i)memcpy(original_angles[i],objects[6+i].ang,sizeof(original_angles[i]));
    NJS_MATRIX before,after;njGetMatrix(&before);
    sys->tk_flg=1;sys->ts_flg=0;
    bhSysTaskJumpTab[0]=loaded_arm_ik_task;
    tick(&input);
    bhSysTaskJumpTab[0]=original;
    njGetMatrix(&after);assert(!memcmp(before,after,sizeof(before)));
    for(unsigned int i=0;i<3;++i)assert(abs(objects[7].ang[i])<=32768);
    assert(objects[8].ang[1]>=0&&objects[8].ang[1]<=32768);
    assert(!memcmp(original_angles[0],objects[6].ang,sizeof(original_angles[0])));
    assert(!memcmp(original_angles[3],objects[9].ang,sizeof(original_angles[3])));
    for(unsigned int i=0;i<4;++i)memcpy(objects[6+i].ang,original_angles[i],sizeof(original_angles[i]));
    puts("PASS: original handgun arm IK with real loaded Claire joints and world-space hand target, bounded angles and preserved matrix stack");
}

static void test_loaded_inventory_transition(void)
{
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    RecvxLiveTaskProc original = bhSysTaskJumpTab[0];
    NJS_TEXLIST *list = rom->mdl.texP;
    uint32_t digests[256];
    assert(list && list->nbTexture <= 256);
    for (unsigned int i = 0; i < list->nbTexture; ++i)
        digests[i] = texture_digest(recvxGameBootTextureGet(list, i));
    recvxGameBootMovieShutdown();
    /* Run the real inventory monitor stages in isolation from the opening
       cutscene; no inventory drawing/dispatch replacement is installed. */
    sys->tk_flg = 1; sys->ts_flg = 0x200; sys->cb_flg = 0;
    sys->ef_flg = 0; sys->fade_an = 0;
    swork.subscreenmode = swork.flgchk = swork.statusflg = swork.taskloop = 0;
    fvwork.afsmode = 0;
    sys->mn_mode0 = 0; sys->mn_setct = 1; sys->mn_stack[0] = 5;
    bhSysTaskJumpTab[0] = inventory_transition_task;
    tick(&input); assert(sys->mn_md0 == 5 && sys->mn_md1 == 1);
    tick(&input); assert(sys->mn_md1 == 2 && sys->subtxp);
    tick(&input); assert(sys->mn_md1 == 3 && !recvxGameBootFault());
    for (unsigned int i = 0; i < list->nbTexture; ++i) assert(!list->textures[i].texaddr);
    assert(!recvxGameBootRenderPending());
    size_t draws; recvxGameBootDraws(&draws); assert(!draws);
    unsigned int palette[1024]; memcpy(palette, palbuf, sizeof(palette));
    fvwork.tex_flg = 1;
    tick(&input); assert(sys->mn_md1 == 4 && !recvxGameBootFault());
    assert(swork.subtx_list.textures == swork.subtx && swork.subtx_list.nbTexture == 5);
    assert(!fvwork.tex_flg && njGetPaletteMode() == 6);
    assert(!memcmp(palbuf + 3072, palette, sizeof(palette)));
    for (unsigned int i = 0; i < swork.subtx_list.nbTexture; ++i) {
        const RecvxBootTexture *texture = recvxGameBootTextureGet(&swork.subtx_list, i);
        assert(texture->source && texture->pixels && texture->width && texture->height);
    }
    tick(&input); assert(sys->mn_md0 == 0 && sys->mn_md1 == 0 && !recvxGameBootFault());
    assert((sys->ts_flg & 0x80) && !(sys->ts_flg & 0x200) && (swork.subscreenmode & 1));
    assert(swork.scr.w == 352 && swork.scr.h == 184 && swork.subp == sys->memp);
    /* Exercise the actual inventory layout, font pages and PSP sprite queue,
       with the real loaded player inventory and five authored UI textures. */
    bhSysTaskJumpTab[0] = inventory_ui_task;
    tick(&input);
    assert(!recvxGameBootFault() && swork.subscreenmode == 2 && swork.pip == sys->itm);
    for (unsigned int frame = 0; frame < 40; ++frame) {
        if(frame==39){capture=1;capture_path="/tmp/recvx-game-inventory.ppm";}
        tick(&input);
        assert(!recvxGameBootFault());
    }
    /* Use the retail pickup/context entry to load and draw the lighter. */
    sys->cb_flg|=0x10;sys->sb_id=55;swork.subscreenmode=1;
    inventory_model_frames=inventory_visible_vertices=0;
    for(unsigned int frame=0;frame<80;++frame){
        tick(&input);
        assert(!recvxGameBootFault());
    }
    fprintf(stderr,"Inventory preview: flags=%08x model=%u frames=%u visible=%u monitor=%u/%u\n",
        swork.statusflg,sitem.mw.rdid,inventory_model_frames,inventory_visible_vertices,sys->mn_md0,sys->mn_md1);
    assert((swork.statusflg&0x10)&&sitem.mw.rdid==55&&sitem.mdl.objP&&inventory_model_frames>0&&inventory_visible_vertices>0);
    bhSysTaskJumpTab[0] = inventory_close_task;
    swork.statusflg |= 0x80000;
    for (unsigned int phase = 1; phase <= 4; ++phase) {
        tick(&input); assert(swork.taskloop == phase);
    }
    tick(&input);
    assert(!(sys->ts_flg & 0x80) && (sys->ts_flg & 0x200) && !(swork.statusflg & 0x200));
    assert(sys->memp == sys->sbs_sp && swork.scr.w == 640 && swork.scr.h == 480);
    assert(!memcmp(palbuf, palette, sizeof(palette)) && njGetPaletteMode() == 2);
    assert(sys->sp_flg == UINT32_MAX && !recvxGameBootFault());
    for (unsigned int i = 0; i < list->nbTexture; ++i)
        assert(texture_digest(recvxGameBootTextureGet(list, i)) == digests[i]);
    bhSysTaskJumpTab[0] = original;
    puts("PASS: original inventory entry loads five UI textures; real lighter context loads textured geometry with visible bounds/alpha and preview scissor; original ItemTaskCheck close phases restore gameplay, room textures, palette and memory cursor");
}
#endif

#if UINTPTR_MAX == UINT32_MAX
static void camera_reset_event_task(void)
{
    assert(recvxEventCoreRunScenario(12)==0);
    recvxGameBootEventCheck();
}

static void test_event_skip_task(void)
{
    /* Task 0 executes the real dispatch table, yields, then resumes through
       the disable selector and EVT_END. Both selectors consume two bytes. */
    static unsigned char section[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,
        106,0,254,106,255,255,0};
    static unsigned char truncated[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,106};
    EVT_WORK *previous=rom->evtp;
    unsigned int flags=sys->gm_flg,enabled=sys->sp_flg;
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,sizeof(section))&&!Event_exec(0,0));
    sys->sp_flg|=0x10;sys->gm_flg=0x81234567u;
    assert(!bhEventScheduler2());
    assert(sys->gm_flg==0xc1234567u&&recvxEventCoreTaskPc(0)==19&&bhEtask[0].status);
    assert(!bhEventScheduler2());
    assert(sys->gm_flg==0x81234567u&&recvxEventCoreTaskPc(0)==23&&!bhEtask[0].status);
    assert(recvxEventCoreInstructionCount()==4);
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)truncated;
    assert(!recvxEventCoreBindSection(truncated,sizeof(truncated))&&!Event_exec(0,0));
    assert(bhEventScheduler2()==-1);
    const RecvxEventCoreFault *fault=recvxEventCoreGetFault();
    assert(fault->code==RECVX_EVENT_FAULT_TRUNCATED&&fault->opcode==106&&fault->task==0);
    assert(recvxEventCoreTaskPc(0)==16&&sys->gm_flg==0x81234567u);
    recvxEventCoreReset();rom->evtp=previous;sys->gm_flg=flags;sys->sp_flg=enabled;
    puts("PASS: opcode 106 task 0 skip flag enable/yield/resume/disable/end, exact PCs, unrelated flags and truncated operand rejection");
}

static void test_frame_check_task(void)
{
    static unsigned char section[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,
        252,8,56,0,0,1,7,0,254,243,255,0};
    EVT_WORK *previous=rom->evtp;
    unsigned int enabled=sys->sp_flg;
    int frame=plp->frm_no;
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,sizeof(section))&&!Event_exec(0,0));
    sys->sp_flg|=0x10;plp->frm_no=(7<<16)+32767;
    assert(!bhEventScheduler2()&&recvxEventCoreTaskPc(0)==25);
    assert(!bhEventScheduler2()&&recvxEventCoreTaskPc(0)==25&&bhEtask[0].status);
    ++plp->frm_no;
    assert(!bhEventScheduler2()&&recvxEventCoreTaskPc(0)==28&&!bhEtask[0].status);
    assert(bhEtask[0].loop==-1);
    recvxEventCoreReset();rom->evtp=previous;sys->sp_flg=enabled;plp->frm_no=frame;
    puts("PASS: opcode 56 real task 0 animation half-frame wait/yield/resume/end with exact PCs");
}

static void test_easy_sound_task(void)
{
    static unsigned char section[96]={24,0,0,0,26,0,0,0,28,0,0,0,
        30,0,0,0,32,0,0,0,34,0,0,0};
    static const unsigned char body[]={118,2,0,6,119,2,0,20,40,2,
        127,2,0,0,255,2,
        134,2,0,20,40,128,160,2,0,0,255,0,0,255,0,0,
        254,135,255,117,1,4,255,182,0,131,255,255,0};
    static const unsigned int opcodes[]={70,71,117,118,119,121,122,127,131,134,135,146,147,148,149,167,182,212};
    static const unsigned int sizes[]={8,10,4,4,6,6,2,6,2,16,2,2,2,4,4,4,2,4};
    EVT_WORK *previous=rom->evtp;unsigned int enabled=sys->sp_flg;
    memcpy(section+34,body,sizeof(body));
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,34+sizeof(body))&&!Event_exec(3,3));
    sys->sp_flg|=0x10;
    assert(!bhEventScheduler2()&&recvxEventCoreTaskPc(3)==67&&bhEtask[3].status);
    assert(!bhEventScheduler2()&&recvxEventCoreTaskPc(3)==79&&!bhEtask[3].status);
    assert(!recvxAudioFault()&&!recvxAudioNativeError());
    for(unsigned int i=0;i<sizeof(opcodes)/sizeof(opcodes[0]);++i){
        for(unsigned int length=1;length<sizes[i];++length){
            memset(section+34,0,sizeof(section)-34);section[34]=opcodes[i];
            recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
            assert(!recvxEventCoreBindSection(section,34+length)&&!Event_exec(3,3));
            assert(bhEventScheduler2()==-1);
            const RecvxEventCoreFault *fault=recvxEventCoreGetFault();
            assert(fault->code==RECVX_EVENT_FAULT_TRUNCATED&&fault->opcode==opcodes[i]&&fault->task==3&&fault->event_id==3);
            assert(recvxEventCoreTaskPc(3)==34);
        }
    }
    recvxEventCoreReset();rom->evtp=previous;sys->sp_flg=enabled;
    puts("PASS: opcode 134 native task 3/script 3 sound playback, gain/pan flags, yield/end and all 18 sound command truncation boundaries");
}

static void test_common_control_task(void)
{
    static unsigned char section[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,
        105,26,0,0,100,0,200,0,44,1,
        105,26,1,0,244,1,88,2,188,2,
        105,30,1,105,6,10,20,30,0,105,13,0,1,0,0,105,3,0,
        105,41,1,0,105,46,1,0,105,50,7,255,105,14,255,255,105,21,7,255,254,255,0};
    EVT_WORK *previous=rom->evtp;unsigned int enabled=sys->sp_flg;
    BH_PWORK saved=*plp;
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,sizeof(section))&&!Event_exec(1,0));
    bhEtask[1].work=plp;bhEtask[1].cnt2=1;bhEtask[1].cnt3=2;
    sys->sp_flg|=0x10;plp->ax=plp->ay=plp->az=0;
    assert(!bhEventScheduler2()&&bhEtask[1].status&&recvxEventCoreTaskPc(1)==sizeof(section)-2);
    assert(plp->px==3&&plp->py==4&&plp->pz==5);
    assert(plp->ax==910&&plp->ay==-1820&&plp->az==2730);
    assert((plp->flg&0x40020)==0x40020&&(plp->type&255)==7&&plp->ct3==255&&plp->ct1==7);
    assert(!bhEventScheduler2()&&!bhEtask[1].status&&recvxEventCoreTaskPc(1)==sizeof(section));
    *plp=saved;recvxEventCoreReset();rom->evtp=previous;sys->sp_flg=enabled;
    puts("PASS: opcode 105 task 1 original position interpolation, signed rotations, flags, type and counters with yield/resume/end");
}

static void test_vibration_event_task(void)
{
    extern int EventVibrationMode;
    static unsigned char section[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,
        158,0,159,255,254,158,255,159,0,255,0};
    static unsigned char truncated[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,158};
    EVT_WORK *previous=rom->evtp;
    unsigned int enabled=sys->sp_flg,flags=sys->gm_flg;
    int mode=EventVibrationMode;
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,sizeof(section))&&!Event_exec(0,0));
    sys->sp_flg|=0x10;SetEventVibrationMode(0);
    assert(!bhEventScheduler2());
    assert(EventVibrationMode==1&&recvxEventCoreTaskPc(0)==21&&bhEtask[0].status);
    assert(!bhEventScheduler2());
    assert(EventVibrationMode==0&&recvxEventCoreTaskPc(0)==27&&!bhEtask[0].status);
    assert(recvxEventCoreInstructionCount()==6&&sys->gm_flg==flags);
    for(unsigned int opcode=158;opcode<=159;++opcode){
        truncated[16]=(unsigned char)opcode;
        recvxEventCoreReset();rom->evtp=(EVT_WORK *)truncated;
        assert(!recvxEventCoreBindSection(truncated,sizeof(truncated))&&!Event_exec(0,0));
        SetEventVibrationMode(7);
        assert(bhEventScheduler2()==-1);
        const RecvxEventCoreFault *fault=recvxEventCoreGetFault();
        assert(fault->code==RECVX_EVENT_FAULT_TRUNCATED&&fault->opcode==opcode&&fault->task==0);
        assert(recvxEventCoreTaskPc(0)==16&&EventVibrationMode==7&&sys->gm_flg==flags);
    }
    recvxEventCoreReset();rom->evtp=previous;sys->sp_flg=enabled;SetEventVibrationMode(mode);
    puts("PASS: opcodes 158/159 task 0 vibration mode/start, yield/resume/end, exact PCs and truncated operand rejection");
}

static void test_camera_pause_task(void)
{
    static unsigned char section[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,
        41,0,254,41,1,254,255,0};
    static unsigned char truncated[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,41};
    EVT_WORK *previous=rom->evtp;unsigned int enabled=sys->sp_flg;CAM_WORK saved=cam;
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,sizeof(section))&&!Event_exec(0,0));
    sys->sp_flg|=0x10;
    assert(!bhEventScheduler2()&&cam.mode0==3&&bhEtask[0].status&&recvxEventCoreTaskPc(0)==19);
    CAM_WORK paused=cam;
    for(unsigned int i=0;i<3;++i){bhControlEventCamera();assert(!memcmp(&cam,&paused,sizeof(cam)));}
    assert(!bhEventScheduler2()&&cam.mode0==1&&bhEtask[0].status&&recvxEventCoreTaskPc(0)==22);
    paused.mode0=1;assert(!memcmp(&cam,&paused,sizeof(cam)));
    assert(!bhEventScheduler2()&&!bhEtask[0].status&&recvxEventCoreTaskPc(0)==24);
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)truncated;
    assert(!recvxEventCoreBindSection(truncated,sizeof(truncated))&&!Event_exec(0,0));
    assert(bhEventScheduler2()==-1);
    const RecvxEventCoreFault *fault=recvxEventCoreGetFault();
    assert(fault->code==RECVX_EVENT_FAULT_TRUNCATED&&fault->opcode==41&&fault->task==0&&fault->event_id==0);
    assert(recvxEventCoreTaskPc(0)==16&&!memcmp(&cam,&paused,sizeof(cam)));
    recvxEventCoreReset();rom->evtp=previous;sys->sp_flg=enabled;cam=saved;
    puts("PASS: opcode 41 task 0/script 0 pause/yield/held camera/resume/end with exact PCs and truncated operand rejection");
}

static void test_remaining_opcode_boundaries(void)
{
    static const struct { unsigned int opcode,extent; } cases[]={
#define RECVX_EVENT_REMAINING(number,handler,bytes) {number,bytes},
#include "psp/game_boot_event_remaining.inc"
#undef RECVX_EVENT_REMAINING
    };
    static const unsigned int player_sub[]={0,7,12,13,52,128,129,130,131,132,137,139,140,141,142,143,145,150};
    static const unsigned int player_size[]={2,8,6,6,2,2,12,2,4,6,4,2,8,6,2,4,6,4};
    unsigned char section[64]={12,0,0,0,14,0,0,0,16,0,0,0};
    EVT_WORK *previous=rom->evtp;
    unsigned int checks=0;
    for(unsigned int i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        assert(bhScenarioJmpT[cases[i].opcode]);
        unsigned int variants=cases[i].opcode==100?18:cases[i].opcode==102?2:1;
        for(unsigned int v=0;v<variants;++v){
            unsigned int extent=cases[i].extent;
            if(cases[i].opcode==100)extent=player_size[v];
            if(cases[i].opcode==102)extent=v?4:2;
            for(unsigned int length=1;length<extent;++length){
                memset(section+16,0,sizeof(section)-16);section[16]=cases[i].opcode;
                if(cases[i].opcode==100)section[17]=player_sub[v];
                if(cases[i].opcode==102)section[17]=v?20:1;
                recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
                assert(!recvxEventCoreBindSection(section,16+length)&&!Event_exec(0,0));
                assert(bhEventScheduler2()==-1);
                const RecvxEventCoreFault *fault=recvxEventCoreGetFault();
                assert(fault->code==RECVX_EVENT_FAULT_TRUNCATED&&fault->opcode==cases[i].opcode);
                assert(recvxEventCoreTaskPc(0)==16);++checks;
            }
        }
    }
    unsigned int assigned=0;
    for(unsigned int opcode=0;opcode<256;++opcode){
        int reserved=(opcode>=213&&opcode<=242)||(opcode>=245&&opcode<=247);
        assert(!!bhScenarioJmpT[opcode]==!reserved);
        if(!reserved)++assigned;
    }
    assert(assigned==223);
    recvxEventCoreReset();rom->evtp=previous;
    printf("PASS: all 223 assigned opcodes dispatched; 33 reserved slots rejected; %u truncation boundaries across 130 new handlers and nested player/object commands\n",checks);
}

static void test_effect_collision_services(void)
{
    ATR_WORK wall={0},water={0};
    ATR_WORK *old_walls=rom->walp,*old_floors=rom->flrp;
    int old_wall_count=rom->wal_n,old_floor_count=rom->flr_n;
    int old_dynamic_walls=sys->mwal_n,old_dynamic_floors=sys->mflr_n;
    wall.flg=1;wall.type=0;wall.px=wall.py=wall.pz=-1;wall.w=wall.h=wall.d=2;
    rom->walp=&wall;rom->wal_n=1;sys->mwal_n=0;
    NJS_POINT3 start={0,0,5},end={0,0,-5},normal,center={0,0,0};
    assert(bhCollisionCheckLine2(&start,&end,0x4400,-1)==&wall&&end.z==1);
    bhGetHitCollisionNormal(&normal);assert(normal.x==0&&normal.y==0&&normal.z==1);
    start=(NJS_POINT3){5,0,5};end=(NJS_POINT3){5,0,-5};
    assert(!bhCollisionCheckLine2(&start,&end,0x4400,-1));
    assert(bhCheckWallType(&center,0,0.2f,0)==&wall);
    water=wall;water.type=3;water.prm0=1;rom->flrp=&water;rom->flr_n=1;sys->mflr_n=0;
    assert(bhCheckWater(&center)==&water);
    center.y=3;assert(!bhCheckWater(&center));
    rom->walp=old_walls;rom->wal_n=old_wall_count;rom->flrp=old_floors;rom->flr_n=old_floor_count;
    sys->mwal_n=old_dynamic_walls;sys->mflr_n=old_dynamic_floors;
    puts("PASS: effect wall sweep clips to the box face and preserves its normal; misses, wall overlap and water volume boundaries");
}

static void test_effect_callback_lifetimes(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *saved_sys=malloc(sizeof(*sys));
    assert(saved&&saved_sys);memcpy(saved,eff,sizeof(eff));*saved_sys=*sys;
    NJS_POINT3 position={0,20,0},direction={1,1,0};
    for(int type=0;type<19;++type){
        memset(eff,0,sizeof(eff));
        assert(bhSetEffParticle(NULL,0,&position,&direction,0xff808080u,type));
        unsigned int emitted=0;
        for(int frame=0;frame<64;++frame){
            sys->ef_fncn=0;
            for(int i=0;i<512;++i)if(eff[i].flg&1u){
                assert(eff[i].id==302);bhEff302(&eff[i]);
            }
            emitted+=sys->ef_fncn;
        }
        assert(emitted);
        for(int i=0;i<512;++i)assert(!eff[i].flg);
    }
    memset(eff,0,sizeof(eff));
    O_WRK *sand=&eff[0];sand->flg=1;sand->id=254;sand->type=1;
    sand->px=3;sand->py=20;sand->pz=4;sand->sx=sand->sy=sand->sz=1;
    sand->sxb=sand->syb=sand->szb=1;
    bhEff_E02_SandParticle(sand);assert(sand->mode0==1&&sand->func&&sand->exp0);
    P_WRK *particles=(P_WRK *)sand->exp0;
    assert(particles->num==16&&particles->flg==2);
    float previous_y=particles->pos[0].y;
    particles->vec[0].y=1;sys->ef_fncn=0;bhEff_E02_SandParticle(sand);
    assert(particles->pos[0].y==previous_y+1&&fabsf(particles->vec[0].y-0.6f)<1e-6f&&sys->ef_fncn==1);
    sand->ct0=0;bhEff_E02_SandParticle(sand);assert(!sand->flg&&!particles->flg);
    for(int variant=0;variant<2;++variant)for(int type=0;type<3;++type){
        memset(eff,0,sizeof(eff));O_WRK *dust=&eff[0];
        dust->flg=1;dust->type=type;dust->mdlver=variant;dust->sx=dust->sy=1;
        int frames=0;while(dust->flg&&frames++<24){sys->ef_trsn=0;bhEff_E02_SandDust(dust);}
        assert(!dust->flg&&frames>1&&frames<24);
    }
    memset(eff,0,sizeof(eff));O_WRK *emitter=&eff[0];emitter->flg=1;
    bhEff_E02_SandParticle2(emitter);assert(emitter->mode0==1&&emitter->ct0>=120&&emitter->ct0<=150);
    emitter->ct0=0;bhEff_E02_SandParticle2(emitter);assert(!emitter->mode0);
    for(int type=0;type<=10;++type){
        memset(eff,0,sizeof(eff));O_WRK *flash=&eff[0];flash->flg=1;flash->type=type;
        flash->sxb=flash->syb=1;flash->mdlver=0;
        bhEff002(flash);assert(flash->mode0==1&&flash->exp0);
        int frames=0;while(flash->flg&&frames++<80){sys->ef_trsn=0;bhEff002(flash);}
        assert(!flash->flg&&frames<80);
    }
    memset(eff,0,sizeof(eff));BP_WORK pool={0};pool.bld_spd=0.1f;
    int made=bhSetBloodPoolLnk(NULL,&position,0,&pool,0);assert(made==1);
    O_WRK *blood=&eff[0];bhEff300(blood);assert(blood->mode0==1&&blood->pn==16);
    for(int frame=0;frame<260;++frame){sys->ef_trsn=0;bhEff300(blood);}
    assert(blood->mode0==2&&sys->ef_trsn==1);
    for(int type=0;type<7;++type){
        memset(eff,0,sizeof(eff));O_WRK *pool_effect=&eff[0];
        pool_effect->flg=1;pool_effect->type=type;pool_effect->px=10000;pool_effect->py=10000;pool_effect->pz=10000;
        sys->gm_flg&=~0x2000000u;sys->ef_trsn=sys->ef_trs2dn=0;bhEff301(pool_effect);
        assert(!pool_effect->flg||pool_effect->mode0);
        sys->gm_flg|=0x2000000u;bhEff301(pool_effect);assert(!pool_effect->flg);
    }
    for(int kind=0;kind<2;++kind){
        memset(eff,0,sizeof(eff));O_WRK *casing=&eff[0];
        casing->flg=0x81;casing->lkwkp=(unsigned char *)plp;casing->lkono=0;casing->type=0;
        casing->loy=100;casing->flr_no=1;sys->yk_ct=0;sys->ef_poln=0;
        if(kind)bhEff007(casing);else bhEff000(casing);
        assert(casing->mode0&&!(casing->flg&0x80u)&&isfinite(casing->py));
    }
    memcpy(eff,saved,sizeof(eff));*sys=*saved_sys;free(saved);free(saved_sys);
    puts("PASS: all 19 staged-particle presets release actor and auxiliary slots; sand gravity, six dust animations and emitter timing; 11 muzzle-flash lifetimes; blood mesh/pool and both casing initializations");
}

static void test_effect_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    static const struct {void (*update)(O_WRK *);int types;} animations[]={
        {bhEff005,3},{bhEff008,3},{bhEff010,1},{bhEff014,4},
        {bhEff019,4},{bhEff023,1},{bhEff119,2},{bhEff132,4}};
    unsigned int variants=0;
    for(size_t a=0;a<sizeof(animations)/sizeof(*animations);++a)
        for(int type=0;type<animations[a].types;++type){
            memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];
            e->flg=1;e->type=type;e->sxb=e->syb=e->szb=1;e->sx=e->sy=e->sz=1;
            e->pvp=e->pv;e->tvp=e->tv;
            int frames=0;
            while(e->flg&&frames++<200){
                sys->ef_trsn=sys->ef_ntxn=sys->ef_fncn=0;animations[a].update(e);
                assert(isfinite(e->sx)&&isfinite(e->sy));
            }
            if(animations[a].update==bhEff008 || (animations[a].update==bhEff010&&type==0))
                assert(e->flg&&frames==201&&e->ct0<12);
            else assert(!e->flg&&frames>1&&frames<200);
            ++variants;
        }
    memset(eff,0,sizeof(eff));O_WRK *rain=&eff[0];rain->flg=1;
    bhEff100(rain);assert(rain->mode0==1&&rain->exp0&&rain->func);
    ER_WORK *drops=(ER_WORK *)(rain->exp0+4);
    assert(((O_WRK *)rain->exp0)->flg==2);
    drops[0].py=30;sys->sp_flg|=8;sys->winds=0;
    recvxGameBootRenderBeginFrame();bhEff106(rain);
    assert(drops[0].py==25);
    sys->ef_fncn=0;bhEff100(rain);assert(sys->ef_fncn==1);
    rain->stflg|=0x1000000;bhEff100(rain);assert(!rain->flg);
    static void (*const custom[])(O_WRK *)={bhEff022,bhEff024,bhEff025,bhEff027,bhEff107};
    for(size_t k=0;k<sizeof(custom)/sizeof(*custom);++k){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;
        e->sx=e->sy=e->sz=1;e->lox=1;e->pvp=e->pv;e->tvp=e->tv;
        custom[k](e);assert(e->exp0&&e->func);
        e->ct0=1;recvxGameBootRenderBeginFrame();e->func(e);
        assert(!recvxEngineOriginalFault());
    }
    for(int type=0;type<6;++type){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=type;
        e->sx=e->sy=e->sz=1;e->pn=4;e->pvp=e->pv;e->tvp=e->tv;
        e->mtx=(NJS_MATRIX *)e->mtxbuf;e->flr_no=2;
        sys->ef_fncn=0;bhEff003(e);assert(e->func&&e->ct0==1);
        recvxGameBootRenderBeginFrame();e->func(e);assert(!recvxEngineOriginalFault());
    }
    memset(eff,0,sizeof(eff));O_WRK *wind=&eff[0];wind->flg=1;wind->sx=2;wind->sy=1;
    wind->type=1;wind->ay=16384;bhEff102(wind);
    assert(sys->windr==16384&&sys->winds==2&&wind->ct0==256);
    /* The capture target is allocated by effect 110, then populated by the
       existing framebuffer transfer service rather than decoded as TIM2. */
    memset(eff,0,sizeof(eff));O_WRK *distortion=&eff[0];distortion->flg=1;
    sys->gm_flg&=~1u;sys->ef_flg&=~1u;bhEff110(distortion);
    assert(distortion->mode0==1&&sys->ren_tlist.nbTexture==1);
    const RecvxBootTexture *target=recvxGameBootTextureGet(&sys->ren_tlist,0);
    assert(target->rgba8888&&target->width==512&&target->height==512);
    njReleaseTexture(&sys->ren_tlist);
    NJS_POINT2 timer_pos={10,20};size_t glyph_count;
    recvxGameBootDrawDiscard();sys->gm_mode=0;
    bhDispTime(&timer_pos,15,216000+2*3600+3*60+30,0,1);
    const RecvxBootDraw *glyphs=recvxGameBootDraws(&glyph_count);
    static const unsigned int timer_codes[]={16,17,26,16,18,26,16,19,21,16};
    assert(glyph_count==10);
    for(size_t i=0;i<glyph_count;++i){
        assert(glyphs[i].quad.u1==((timer_codes[i]%18)*14.0f+0.5f)/256);
        assert(glyphs[i].quad.v1==((timer_codes[i]/18)*14.0f+0.5f)/256);
    }
    recvxGameBootDrawDiscard();sys->gm_mode=3;
    bhDispTime(&timer_pos,7,216000,0,1);
    glyphs=recvxGameBootDraws(&glyph_count);assert(glyph_count==8);
    assert(glyphs[0].quad.u1==((21%18)*14.0f+0.5f)/256); /* 59:59.99 clamp */
    assert(glyphs[7].quad.u1==((25%18)*14.0f+0.5f)/256);
    recvxGameBootDrawDiscard();
    memcpy(eff,saved,sizeof(eff));*sys=*old;free(saved);free(old);
    printf("PASS: %u effect animation variants expire or loop, native rain draw updates, wind control and distortion render target allocation\n",variants);
}

static void test_effect_next_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    for(int kind=0;kind<2;++kind){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;
        for(int frame=0;frame<100;++frame){
            sys->ef_trsn=0;if(kind)bhEff121(e);else bhEff117(e);
            assert(e->flg&&e->ct0>0&&e->ct0<=(kind?8:3)&&sys->ef_trsn==1);
        }
        if(!kind){e->mode0=2;bhEff117(e);assert(!e->flg);}
    }
    for(int selector=0;selector<3;++selector){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->mode1=selector;
        sys->ef_trsn=0;bhEff122(e);assert(e->tv[0].u==selector*0.125f&&sys->ef_trsn==1);
    }
    for(int color=0;color<6;++color){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=color;
        int visible=0,waiting=0;
        for(int frame=0;frame<100;++frame){
            sys->ef_trsn=0;bhEff123(e);visible+=sys->ef_trsn;waiting+=sys->ef_trsn==0;
        }
        assert(visible>=8&&waiting>0&&e->flg);
    }
    for(int type=0;type<3;++type)for(int color=0;color<2;++color){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=type;e->mdlver=color;
        e->sx=e->sz=1;e->flr_no=type;
        int frames=0;while(e->flg&&frames++<70){sys->ef_trsn=0;bhEff128(e);}
        assert(!e->flg&&frames==(type?64:48));
    }
    sys->st_flg&=~0x2000000u;
    for(int tile=0;tile<55;++tile){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->mdlver=tile;
        e->sxb=e->syb=e->szb=1;e->pvp=e->pv;e->pn=4;e->lox=1;
        sys->ef_fncn=0;bhEff114(e);assert(e->func&&sys->ef_fncn==1);
        NJS_TEXTUREH_VTX *v=(NJS_TEXTUREH_VTX *)e->tvp;
        assert(isfinite(v[0].u)&&v[0].u>=0&&v[0].v>=0);
        if(tile==0)assert(v[0].u==0&&v[0].v==0);
        recvxGameBootRenderBeginFrame();e->func(e);assert(!recvxEngineOriginalFault());
        int frames=0;while(e->flg&&frames++<100){sys->ef_fncn=0;bhEff114(e);}
        assert(!e->flg&&frames<100);
    }
    for(int emitter=0;emitter<4;++emitter){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=emitter==3?1:emitter;
        sys->stg_no=emitter==3?1:0;sys->rom_no=9;
        bhEff113(e);assert(!e->flg);
        int count=0;for(int i=1;i<512;++i)if(eff[i].flg&1u){assert(eff[i].id==114);++count;}
        assert(count==(emitter==0?64:emitter==1?225:emitter==2?128:196));
    }
    memset(eff,0,sizeof(eff));O_WRK *emitter=&eff[0];emitter->flg=1;emitter->id=118;
    emitter->sx=1;emitter->sy=2;bhEff118(emitter);assert(emitter->ct3==8);
    emitter->mode1=1;
    for(int frame=0;frame<100&&emitter->mode0;++frame){
        sys->ef_trsn=0;
        for(int i=1;i<512;++i)if(eff[i].flg&1u)bhEff117(&eff[i]);
        bhEff118(emitter);
    }
    assert(!emitter->mode0&&!emitter->ct3&&(emitter->stflg&0x1000000u));
    memset(eff,0,sizeof(eff));emitter=&eff[0];emitter->flg=1;
    for(int i=1;i<512;++i)eff[i].flg=2;
    bhEff118(emitter);assert(emitter->ct3==0);
    emitter->mode1=1;bhEff118(emitter);assert(!emitter->mode0);
    memset(eff,0,sizeof(eff));O_WRK *mirror=&eff[0];mirror->flg=1;mirror->px=3;
    sys->stg_no=2;bhEff112(mirror);assert((sys->st_flg&0x100u)&&sys->mr_pl.px==3);
    mirror->mdflg=1;bhEff112(mirror);assert(!(sys->st_flg&0x100u));
    memset(eff,0,sizeof(eff));O_WRK *smoke=&eff[0];smoke->flg=1;
    for(int frame=0;frame<4;++frame)bhEff116(smoke);
    assert(eff[1].flg&&eff[1].id==15&&eff[1].type==5);
    memset(eff,0,sizeof(eff));O_WRK *gas=&eff[0];gas->flg=1;gas->type=2;gas->py=2;
    bhEff127(gas);assert(gas->mode0==1);
    gas->ct0=15;bhEff127(gas);assert(eff[1].flg&&eff[1].id==128);
    /* Two strips with per-triangle attribute words, signed UV wrap and an
       inverse scroll. Nodes are in the same owned arena as loaded models. */
    unsigned char *scratch=(unsigned char *)(((uintptr_t)sys->memp+15u)&~(uintptr_t)15u);
    assert(scratch+4096<=sys->endp);memset(scratch,0,4096);
    NJS_CNK_MODEL *model=(NJS_CNK_MODEL *)scratch;
    uint16_t *stream=(uint16_t *)(scratch+256);
    static const uint16_t polygon[]={8,0,65,23,0x4002,
        3,0,10,20,1,30,40,2,50,60,0xabcd,
        0xfffd,0,70,80,1,90,100,2,110,120,0xbeef,255};
    memcpy(stream,polygon,sizeof(polygon));model->plist=(short *)stream;
    npSetOffsetUV(model,5,-30);
    assert(stream[7]==15&&stream[8]==65526&&stream[15]==0xabcd&&stream[26]==0xbeef);
    npSetOffsetUV(model,-5,30);assert(!memcmp(stream,polygon,sizeof(polygon)));
    NJS_CNK_OBJECT *nodes=(NJS_CNK_OBJECT *)(scratch+512);
    for(int i=0;i<12;++i)nodes[i].model=model;
    BH_PWORK saved_actor=ene[127];
    O_WRK *actor=(O_WRK *)&ene[127];memset(actor,0,sizeof(*actor));actor->mlwP=actor->mdl;
    actor->mdl[0].objP=nodes;actor->mdl[0].obj_num=12;
    memset(eff,0,sizeof(eff));O_WRK *scroll=&eff[0];scroll->flg=1;
    scroll->lkwkp=(unsigned char *)actor;sys->gm_flg&=~1u;
    bhEff120(scroll);assert(scroll->mode0==1&&scroll->ct1==1024);
    nodes[11].ang[2]=64;bhEff120(scroll);assert(scroll->ct0==-2&&scroll->ct1==1022);
    sys->gm_flg|=1;bhEff120(scroll);assert(!scroll->mode0&&!memcmp(stream,polygon,sizeof(polygon)));
    ene[127]=saved_actor;
    memset(eff,0,sizeof(eff));O_WRK *monitor=&eff[0];monitor->flg=1;monitor->id=111;
    monitor->pvp=monitor->pv;monitor->pn=4;monitor->lkono=1;
    sys->gm_flg&=~1u;sys->ef_flg&=~1u;sys->ren_n=0;
    int texture=sys->ef_ct;bhEff111(monitor);
    assert(monitor->mode0==1&&sys->ren_n==1&&sys->ef_ct==texture+1&&sys->ef_tn[111]==texture);
    const RecvxBootTexture *monitor_texture=recvxGameBootTextureGet(&sys->ef_tlist,texture);
    assert(monitor_texture->rgba8888);
    njSetTexture(&sys->ef_tlist);recvxGameBootCaptureBegin(512);
    recvxGameBootCaptureSubmitted();njRenderTextureNum(texture);
    assert(monitor_texture->pixels[0]==0x7f&&monitor_texture->pixels[512u*512u*4u-1]==0x7f);
    bhGarbageTexture(NULL,0);sys->ef_thln=0;bhEff111(monitor);
    assert(monitor->mode0==2&&monitor->ct0==64&&sys->ef_thln==1);
    monitor->mode1=1;
    for(int i=0;i<64;++i){sys->ef_thln=0;bhEff111(monitor);}
    assert(monitor->mode0==3&&monitor->ct0==0);
    sys->ef_thln=0;bhEff111(monitor);assert(!(sys->gm_flg&0x200u));
    NJS_TEXNAME capture_name=sys->ef_tex[texture];NJS_TEXLIST capture_list={&capture_name,1};
    njReleaseTexture(&capture_list);
    memcpy(eff,saved,sizeof(eff));*sys=*old;free(saved);free(old);
    puts("PASS: monitor capture/fade, 55 debris tiles and drawing/lifetimes, four emitter bursts, linked flame shutdown/pool exhaustion, looping and selectable animations, sparks, gas lifetimes and reversible model UV scroll");
}

static void test_effect_155_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    static const struct {void (*update)(O_WRK *);int types,frames;} animations[]={
        {bhEff155,3,7},{bhEff157,1,12},{bhEff159,3,24},{bhEff161,3,10},
        {bhEff167,1,13},{bhEff169,3,10}};
    unsigned int variants=0;
    for(size_t a=0;a<sizeof(animations)/sizeof(*animations);++a)
        for(int type=0;type<animations[a].types;++type){
            memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=type;
            e->sx=2;e->sy=3;e->sz=0.25f;
            e->tvp=e->tv;e->pvp=e->pv;e->mtx=(NJS_MATRIX *)e->mtxbuf;e->pn=4;
            e->px=cam.wpx+cam.vx*10;e->py=cam.wpy+cam.vy*10;e->pz=cam.wpz+cam.vz*10;
            float start_z=e->pz;int frames=0;
            while(e->flg&&frames<40){
                sys->ef_trsn=0;animations[a].update(e);
                if(!e->flg)break;
                assert(sys->ef_trsn==1&&sys->ef_trs[0]==e);
                assert(isfinite(e->sx)&&isfinite(e->sy)&&e->sx>0&&e->sy>0);
                if(animations[a].update==bhEff159){
                    int expected=type==1?(frames>=14?2:0):frames>=20?3:frames>=18?1:0;
                    assert(e->ani_ct==expected);
                }
                if(frames==0){
                    /* Owned font pages stand in for effect atlases absent from
                       the opening room; exercise the real sprite draw path. */
                    sys->ef_tn[e->tex_id]=0;
                    recvxGameBootRenderBeginFrame();
                    bhDrawTrsEffect3D((unsigned int *)sys->ef_trs,1);
                    assert(!recvxEngineOriginalFault());
                }
                ++frames;
            }
            int expected=animations[a].frames;
            if(animations[a].update==bhEff159&&type==1)expected=26;
            if(animations[a].update==bhEff161&&type==1)expected=15;
            if(animations[a].update==bhEff169&&type)expected=14;
            assert(!e->flg&&frames==expected);
            if(animations[a].update==bhEff157)assert(fabsf(e->pz-start_z-3.25f)<0.0001f);
            ++variants;
        }
    for(int repeats=0;repeats<=3;++repeats){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->sx=e->sy=1;e->sz=repeats;
        int draws=0,frames=0;
        while(e->flg&&frames++<80){sys->ef_trsn=0;bhEff163(e);draws+=sys->ef_trsn;}
        assert(!e->flg&&draws==16+6*(repeats>1?repeats-1:0));
    }
    memset(eff,0,sizeof(eff));O_WRK *bubble=&eff[0];bubble->flg=1;bubble->type=1;
    bubble->sx=bubble->sy=1;int draws=0;
    for(int i=0;i<52;++i){sys->ef_trsn=0;bhEff163(bubble);draws+=sys->ef_trsn;}
    assert(bubble->flg&&draws==32&&bubble->ct0==13);
    static const struct {void (*update)(O_WRK *);int child,type;} emitters[]={
        {bhEff156,157,2},{bhEff158,159,1},{bhEff160,161,1},
        {bhEff162,163,1},{bhEff168,169,1}};
    for(size_t i=0;i<sizeof(emitters)/sizeof(*emitters);++i){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->mode1=2;
        e->px=1;e->py=2;e->pz=3;e->sx=4;e->sy=5;e->sz=6;
        emitters[i].update(e);O_WRK *child=&eff[1];
        assert(child->flg&&child->id==emitters[i].child&&child->type==emitters[i].type);
        assert(child->px==1&&child->py==2&&child->pz==3&&child->sxb==4&&child->syb==5);
        assert(e->mode1==0&&e->type==0);
        emitters[i].update(e);assert(!eff[2].flg);
        memset(eff,0,sizeof(eff));e=&eff[0];e->flg=1;e->type=2;
        for(int j=1;j<512;++j)eff[j].flg=2;
        emitters[i].update(e);assert(!e->type&&!e->mode1);
    }
    memset(eff,0,sizeof(eff));O_WRK *burst=&eff[0];burst->flg=1;burst->mode1=3;
    for(int i=0;i<3;++i)bhEff164(burst);
    assert(!burst->ct0&&!burst->type&&!burst->mode1);
    for(int i=1;i<=3;++i)assert(eff[i].id==159&&eff[i].type==2);
    memset(eff,0,sizeof(eff));O_WRK *trail=&eff[0];trail->flg=1;trail->mode1=2;
    trail->px=3;trail->py=20;trail->pz=4;trail->sz=0.25f;
    for(int i=0;i<4;++i)bhEff165(trail);
    assert(!trail->type&&!trail->ct0&&trail->px==3&&trail->py==20&&trail->pz==4);
    assert(eff[1].id==159&&eff[2].id==159&&!eff[3].flg);
    memset(eff,0,sizeof(eff));O_WRK *periodic=&eff[0];periodic->flg=1;periodic->mode1=1;
    for(int i=0;i<27;++i)bhEff166(periodic);
    assert(periodic->type==1&&periodic->ct1==12);
    for(int i=1;i<=3;++i)assert(eff[i].id==167);
    periodic->type=0;bhEff166(periodic);assert(!eff[4].flg);
    memset(eff,0,sizeof(eff));O_WRK *quad=&eff[0];quad->flg=1;
    njUnitMatrix(NULL);getuv(0,quad);assert(quad->tv[0].x==-1&&quad->tv[1].x==1);
    getuv(1,quad);assert(quad->tv[0].x==1&&quad->tv[1].x==-1);
    memcpy(eff,saved,sizeof(eff));*sys=*old;free(saved);free(old);
    printf("PASS: %u sprite variants expire with original texture-bank timing; bubble repeats/pauses, oriented quads, one-shot/burst/periodic emitters, pool exhaustion and moving-trail restoration\n",variants);
}

static O_WRK *effect_145_fixture(int id);
static unsigned int packed_process_calls,packed_draw_calls;
static void packed_process(void *work){int *value=work;*value+=3;++packed_process_calls;}
static void packed_draw(OR_WORK *work){assert(work==(OR_WORK *)&eff[0]);++packed_draw_calls;}
static void test_packed_effect_batch(void)
{
    RecvxBootTexture *palette_texture=(RecvxBootTexture *)recvxGameBootTextureGet(&sys->ef_tlist,0);
    unsigned int saved_palette[256];memcpy(saved_palette,palette_texture->palette,sizeof(saved_palette));
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));CAM_WORK camera=cam;
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    NJS_MATRIX matrix;njGetMatrix(&matrix);
    NJS_POINT3 pos={cam.wpx+20*cam.vx,cam.wpy+20*cam.vy,cam.wpz+20*cam.vz},dir={.2f,1,.3f};
    fprintf(stderr,"Packed effects: sparks\n");
    for(int type=0;type<4;++type){
        effect_145_fixture(0);eff[0].flg=0;
        bhSetEffGunSpark(&pos,&dir,0xffffffffu,0x00ffffffu,type);
        OR_WORK *e=(OR_WORK *)&eff[0];assert(e->id==308&&e->flg);
        for(int f=0;f<150&&e->flg;++f){
            sys->ef_fncn=0;bhEff308(e);
            if(e->flg&&sys->ef_fncn){recvxGameBootRenderBeginFrame();bhEff308Drw(e);assert(!recvxEngineOriginalFault());recvxGameBootRenderConsume();}
        }
        assert(!e->flg);
    }
    fprintf(stderr,"Packed effects: blood ribbons\n");
    for(int type=0;type<6;++type){
        O_WRK *slot=effect_145_fixture(0);slot->flg=0;
        OR_WORK *e=rySetEffBlood(&plp->mlwP->owP[0].mtx,&pos,&dir,type);assert(e==(OR_WORK *)slot&&e->id==309);
        R09_WORK *r=(R09_WORK *)e->free4;r->texP=&sys->ef_tlist;r->tex_id=0;
        for(int f=0;f<180&&e->flg;++f){
            sys->ef_fncn=0;bhEff309(e);
            if(e->flg&&sys->ef_fncn){recvxGameBootRenderBeginFrame();bhEff309Drw(e);assert(!recvxEngineOriginalFault());recvxGameBootRenderConsume();}
        }
        assert(!e->flg);
    }
    fprintf(stderr,"Packed effects: blood mist\n");
    for(int type=0;type<6;++type){
        O_WRK *slot=effect_145_fixture(0);slot->flg=0;
        OR_WORK *e=rySetEffBlood2(&plp->mlwP->owP[0].mtx,&pos,&dir,type);assert(e==(OR_WORK *)slot&&e->id==311);
        R0B_WORK *r=(R0B_WORK *)e->free4;r->texP=&sys->ef_tlist;r->tex_id=0;
        for(int f=0;f<500&&e->flg;++f){
            sys->ef_fncn=0;bhEff30b(e);
            if(e->flg&&sys->ef_fncn){recvxGameBootRenderBeginFrame();e->func(e);assert(!recvxEngineOriginalFault());recvxGameBootRenderConsume();}
        }
        assert(!e->flg);for(int i=1;i<512;++i)assert(!(eff[i].flg&2));
    }
    fprintf(stderr,"Packed effects: spline trails\n");
    for(int textured=0;textured<2;++textured){
        O_WRK *slot=effect_145_fixture(307);OR_WORK *e=(OR_WORK *)slot;
        R07_WORK *r=(R07_WORK *)e->free4;r->eff_prm.mtxP=&plp->mlwP->owP[0].mtx;
        r->eff_prm.src=pos;r->eff_prm.dst=(NJS_POINT3){pos.x+1,pos.y+1,pos.z};
        r->eff_prm.frm_inc=.25f;r->eff_prm.col=0xffffffffu;r->eff_prm.tim=8;
        r->eff_prm.texP=textured?&sys->ef_tlist:NULL;r->eff_prm.tex_id=0;
        for(int f=0;f<50&&e->flg;++f){
            sys->ef_fncn=0;bhEff307(e);
            if(e->flg&&sys->ef_fncn){recvxGameBootRenderBeginFrame();njSetMatrix(NULL,cam.mtx);bhEff307Drw(e);assert(!recvxEngineOriginalFault());recvxGameBootRenderConsume();}
        }
        assert(!e->flg);
    }
    for(int type=0;type<3;++type){
        OR_WORK *e=(OR_WORK *)effect_145_fixture(310);R0A_WORK *r=(R0A_WORK *)e->free4;
        r->prm_a.type=type;r->prm_a.x_rang=r->prm_a.y_rang=1;r->prm_a.x_rate=r->prm_a.y_rate=.5f;
        r->prm_a.add_ax=100;r->prm_a.add_ay=200;
        sys->sp_flg|=1;sys->cb_flg&=~4u;
        for(int f=0;f<30&&e->flg;++f)bhEff30a(e);
        assert(type==2?!e->flg:!!e->flg);cam=camera;
    }
    O_WRK *shadow=effect_145_fixture(304);shadow->lkwkp=(unsigned char *)plp;
    NJS_POINT3 *p=(NJS_POINT3 *)sys->memp;unsigned char *old_memp=sys->memp;
    p[0]=(NJS_POINT3){plp->px-1,plp->py,plp->pz};p[1]=(NJS_POINT3){plp->px+1,plp->py,plp->pz};p[2]=(NJS_POINT3){plp->px,plp->py,plp->pz+1};
    shadow->ct0=(int)(uintptr_t)&p[0];shadow->ct1=(int)(uintptr_t)&p[1];shadow->ct2=(int)(uintptr_t)&p[2];
    sys->memp+=3*sizeof(*p);sys->ef_mdfn=0;bhEff304(shadow);
    assert(isfinite(shadow->sx)&&isfinite(shadow->sy)&&sys->ef_mdfn<=1);sys->memp=old_memp;
    O_WRK *slot=effect_145_fixture(312);OR_WORK *model=(OR_WORK *)slot;R0_WK *m=(R0_WK *)model->free4;
    /* The OR overlay reuses the matrix bytes later, so its source transform
       lives in the owned player pose, outside the packed payload. */
    m->prm.mtxP=&plp->mlwP->owP[0].mtx;m->prm.texP=plp->mlwP->texP;
    unsigned int joint=0;while(joint<plp->mlwP->obj_num&&!plp->mlwP->objP[joint].model)++joint;assert(joint<plp->mlwP->obj_num);
    m->prm.mdlP=(NJS_MODEL *)plp->mlwP->objP[joint].model;
    bhEff30c(model);assert(m->mode==1&&m->erase==-1);
    recvxGameBootRenderBeginFrame();model->func(model);assert(!recvxEngineOriginalFault());recvxGameBootRenderConsume();
    m->erase=0;bhEff30c(model);assert(!model->flg);
    OR_WORK *custom=(OR_WORK *)effect_145_fixture(349);R49_WORK *c=(R49_WORK *)custom->free4;
    c->fnc_prcP=packed_process;c->fnc_drwP=packed_draw;c->free[0]=7;sys->ef_fncn=0;
    bhEff349(custom);assert(packed_process_calls==1&&c->free[0]==10&&sys->ef_fncn==1);
    custom->func(custom);assert(packed_draw_calls==1);
    memcpy(eff,saved,sizeof(eff));*sys=*old;cam=camera;njSetMatrix(NULL,&matrix);free(saved);free(old);
    memcpy(palette_texture->palette,saved_palette,sizeof(saved_palette));
    puts("PASS: four spark presets, six blood ribbons, six blood mist presets and auxiliary cleanup; textured/untextured spline trails, camera shakes, shadow contacts, attached models and registered custom callbacks");
}
static void test_remaining_effect_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));BH_PWORK player=*plp;
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;CAM_WORK camera=cam;
    NJS_MATRIX matrix;njGetMatrix(&matrix);
    static const struct {int id;void (*update)(O_WRK *);const char *name;} cases[]={
        {222,bhEff222,"bhEff222"},
        {223,bhEff223,"bhEff223"},
        {224,bhEff224,"bhEff224"},
        {225,bhEff225,"bhEff225"},
        {226,bhEff226,"bhEff226"},
        {227,bhEff227,"bhEff227"},
        {228,bhEff228,"bhEff228"},
        {229,bhEff229,"bhEff229"},
        {230,bhEff230,"bhEff230"},
        {231,bhEff231,"bhEff231"},
        {232,bhEff232,"bhEff232"},
        {233,bhEff233,"bhEff233"},
        {234,bhEff234,"bhEff234"},
        {235,bhEff235,"bhEff235"},
        {236,bhEff236,"bhEff236"},
        {237,bhEff237,"bhEff237"},
        {238,bhEff238,"bhEff238"},
        {239,bhEff239,"bhEff239"},
        {240,bhEff240,"bhEff240"},
        {241,bhEff241,"bhEff241"},
        {242,bhEff242,"bhEff242"},
        {243,bhEff243,"bhEff243"},
        {244,bhEff244,"bhEff244"},
        {245,bhEff245,"bhEff245"},
        {246,bhEff246,"bhEff246"},
        {247,bhEff247,"bhEff247"},
        {248,bhEff248,"bhEff248"},
        {249,bhEff249,"bhEff249"},
        {303,bhEff303,"bhEff303"},
        {305,bhEff305,"bhEff305"},
        {306,bhEff306,"bhEff306"},
        {99,bhEffBG,"bhEffBG"},
        {250,bhEff_E00_Mince,"bhEff_E00_Mince"},
        {251,bhEff_E00_DropBlood,"bhEff_E00_DropBlood"},
        {252,bhEff_E00_Fire,"bhEff_E00_Fire"},
        {257,bhEff_E00_Acid,"bhEff_E00_Acid"},
        {298,bhEff_E00_AcidGenerator,"bhEff_E00_AcidGenerator"},
        {297,bhEff_E00_BloodBurst,"bhEff_E00_BloodBurst"},
        {296,bhEff_E00_Blood,"bhEff_E00_Blood"},
        {272,bhEff_E00_Dust,"bhEff_E00_Dust"},
        {256,bhEff_E03_Acid,"bhEff_E03_Acid"},
        {258,bhEff_E03_Shadow,"bhEff_E03_Shadow"},
        {259,bhEff_E05_Blood,"bhEff_E05_Blood"},
        {260,bhEff_E06_Rinpun,"bhEff_E06_Rinpun"},
        {261,bhEff_E11_SearchLight,"bhEff_E11_SearchLight"},
        {270,bhEff_E16_LaserSight,"bhEff_E16_LaserSight"},
        {263,bhEff_E12_Fire,"bhEff_E12_Fire"},
        {265,bhEff_E12_FrameLiquid,"bhEff_E12_FrameLiquid"},
        {266,bhEff_E12_FloorBlood2,"bhEff_E12_FloorBlood2"},
        {262,bhEff_E12_FireManager,"bhEff_E12_FireManager"},
        {274,bhEff_E12_Fire2,"bhEff_E12_Fire2"},
        {275,bhEff_E12_Fire3,"bhEff_E12_Fire3"},
        {276,bhEff_E12_BintaEffControl,"bhEff_E12_BintaEffControl"},
        {267,bhEff_E13_Fluid,"bhEff_E13_Fluid"},
        {268,bhEff_E14_Explosion,"bhEff_E14_Explosion"},
        {269,bhEff_E14_Fire,"bhEff_E14_Fire"},
        {271,bhEff_E14_Fire2,"bhEff_E14_Fire2"},
        {273,bhEff_E14_Mucus,"bhEff_E14_Mucus"},
        {299,bhEff_DamagePoint,"bhEff_DamagePoint"},
        {350,bhEff_Sub350,"bhEff_Sub350"},
        {351,bhEff_Sub351,"bhEff_Sub351"},
        {352,bhEff_Sub352,"bhEff_Sub352"},
        {353,bhEff_Sub353,"bhEff_Sub353"},
        {354,bhEff_Sub354,"bhEff_Sub354"},
        {355,bhEff_Sub355,"bhEff_Sub355"},
        {356,bhEff_Sub356,"bhEff_Sub356"},
        {360,bhEff_Sub360,"bhEff_Sub360"},
        {361,bhEff_Sub361,"bhEff_Sub361"},
        {362,bhEff_Sub362,"bhEff_Sub362"},
        {363,bhEff_Sub363,"bhEff_Sub363"},
        {364,bhEff_Sub364,"bhEff_Sub364"},
        {365,bhEff_Sub365,"bhEff_Sub365"},
        {366,bhEff_Sub366,"bhEff_Sub366"},
        {367,bhEff_Sub367,"bhEff_Sub367"},
        {368,bhEff_Sub368,"bhEff_Sub368"},
        {369,bhEff_Sub369,"bhEff_Sub369"},
        {370,bhEff_Sub370,"bhEff_Sub370"},
        {371,bhEff_Sub371,"bhEff_Sub371"},
        {372,bhEff_Sub372,"bhEff_Sub372"},
        {373,bhEff_Sub373,"bhEff_Sub373"},
        {398,bhEff_Sub398,"bhEff_Sub398"},
    };
    unsigned int updated=0,drawn=0;
    for(size_t k=0;k<sizeof(cases)/sizeof(*cases);++k){
        O_WRK *e=effect_145_fixture(cases[k].id);
        e->sx=e->sy=e->sz=e->sxb=e->syb=e->szb=1;e->spd=1;e->aw=e->ah=1;
        e->lkono=0;e->lkwkp=(unsigned char *)plp;e->mlwP=plp->mlwP;
        e->txp[0]=plp->mlwP->texP;
        for(int v=0;v<4;++v){e->tv[v].x=(v&1)?1:-1;e->tv[v].y=(v&2)?1:-1;}
        e->px=cam.wpx+30*cam.vx;e->py=cam.wpy+30*cam.vy;e->pz=cam.wpz+30*cam.vz;
        if(cases[k].id>=222&&cases[k].id<=249){
            e->lkono=4;e->lkwkp=(unsigned char *)plp;e->mode1=1;
            e->type=(cases[k].id==225||cases[k].id==227||cases[k].id==229||cases[k].id==232||cases[k].id==239||cases[k].id==246||cases[k].id==248)?0:1;
            if(cases[k].id==227||cases[k].id==248){e->exp0=(unsigned char *)&eff[1];eff[1].flg=1;}
            if(cases[k].id==225||cases[k].id==227||cases[k].id==229||cases[k].id==232||cases[k].id==239||cases[k].id==246||cases[k].id==248)e->mode1=0;
        }
        if(cases[k].id==355)e->exp0=(unsigned char *)plp;
        if(cases[k].id>=365&&cases[k].id<=367){
            eff[1].flg=2;e->exp0=(unsigned char *)&eff[1];
            if(cases[k].id==365){EFFSUB365_WORK *p=(EFFSUB365_WORK *)e->exp0;p->dtbl.num=40;p->dtbl.exp0=(unsigned char *)p->vtx_pos;p->dtbl.exp1=(unsigned char *)p->size;}
            if(cases[k].id==366){EFFSUB366_WORK *p=(EFFSUB366_WORK *)e->exp0;p->dtbl.num=16;p->dtbl.exp0=(unsigned char *)p->vtx_pos;p->dtbl.exp1=(unsigned char *)p->vtx_pos2;}
            if(cases[k].id==367){EFFSUB367_WORK *p=(EFFSUB367_WORK *)e->exp0;p->dtbl.num=32;p->dtbl.exp0=(unsigned char *)p->vtx_pos;p->dtbl.exp1=(unsigned char *)p->size;}
        }
        int tested_draw=0;
        for(int f=0;f<(cases[k].id==356?1:64)&&e->flg;++f){
            sys->ef_trsn=sys->ef_fncn=sys->ef_opqn=sys->ef_linn=0;
            cases[k].update(e);++updated;
            if(cases[k].id==261&&f==0&&e->flg){e->mode0=1;e->axp=4096;}
            assert(isfinite(e->px)&&isfinite(e->py)&&isfinite(e->pz));
            if(cases[k].id==260&&f>0&&e->flg){
                P_WRK *p=(P_WRK *)e->exp0;NJS_POINT3 mean={0};
                for(int i=0;i<p->num;++i){mean.x+=p->pos[i].x;mean.y+=p->pos[i].y;mean.z+=p->pos[i].z;}
                assert(fabsf(e->cspr.c.x-mean.x/p->num)<.0001f&&fabsf(e->cspr.c.y-mean.y/p->num)<.0001f&&fabsf(e->cspr.c.z-mean.z/p->num)<.0001f);
            }
            if(!tested_draw&&e->flg&&(sys->ef_trsn||sys->ef_fncn||sys->ef_opqn)){
                int ani=e->ani_ct;e->ani_ct=0;
                if(e->tex_id>=0&&e->tex_id<450)sys->ef_tn[e->tex_id]=0;
                recvxGameBootRenderBeginFrame();njSetMatrix(NULL,cam.mtx);
                if(sys->ef_trsn)bhDrawTrsEffect3D((unsigned int *)sys->ef_trs,sys->ef_trsn);
                if(sys->ef_fncn&&e->func)e->func(e);
                assert(!recvxEngineOriginalFault());recvxGameBootRenderConsume();e->ani_ct=ani;
                recvxGameBootDrawDiscard();tested_draw=1;++drawn;
            }
        }
        assert(sys->ef_trsn>=0&&sys->ef_trsn<=512&&sys->ef_fncn>=0&&sys->ef_fncn<=512);
    }
    O_WRK *fast=effect_145_fixture(223);fast->sz=100;sys->ef_trsn=0;
    bhEff223(fast);assert(fast->ct1==1&&fast->flg);bhEff223(fast);assert(!fast->flg);
    /* The retail callback is deliberately empty. */
    bhEff_Sub357();
    memcpy(eff,saved,sizeof(eff));*sys=*old;*plp=player;cam=camera;njSetMatrix(NULL,&matrix);free(saved);free(old);
    printf("PASS: remaining ordinary effect families: %u updates, %u draw paths, sprite animation/sentinels, emitters, enemy blood/acid/fire/pollen, searchlights, laser sights and particle controllers\n",updated,drawn);
}

static void test_effect_130_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    CAM_WORK oldcam=cam;BH_PWORK oldplayer=*plp;NJS_MATRIX matrix;njGetMatrix(&matrix);
    static const struct{int id,frames,variants;void (*update)(O_WRK *);} sprites[]={
        {131,13,1,bhEff131},{186,4,6,bhEff186},{188,12,1,bhEff188},
        {209,21,1,bhEff209},{216,14,6,bhEff216}};
    unsigned int variants=0;
    for(size_t k=0;k<sizeof(sprites)/sizeof(*sprites);++k)for(int type=0;type<sprites[k].variants;++type){
        O_WRK *e=effect_145_fixture(sprites[k].id);e->type=type;e->sz=0;
        for(int v=0;v<4;++v){e->tv[v].x=(v&1)?1:-1;e->tv[v].y=(v&2)?1:-1;}
        e->px=cam.wpx+30*cam.vx;e->py=cam.wpy+30*cam.vy;e->pz=cam.wpz+30*cam.vz;
        int frames=0;
        while(e->flg&&frames<30){
            sys->ef_trsn=0;sprites[k].update(e);if(!e->flg)break;
            assert(sys->ef_trsn==1);
            if(!frames){
                sys->ef_tn[e->tex_id]=0;recvxGameBootRenderBeginFrame();njSetMatrix(NULL,cam.mtx);
                bhDrawTrsEffect3D((unsigned int *)sys->ef_trs,1);
                assert(!recvxEngineOriginalFault()&&recvxGameBootRenderFrame()->vertex_count>0);
                recvxGameBootRenderConsume();
            }
            ++frames;
        }
        assert(!e->flg&&frames==(sprites[k].id==216&&type%3==2?10:sprites[k].frames));++variants;
    }
    O_WRK *e=effect_145_fixture(188);e->type=1;
    for(int f=0;f<6;++f){sys->ef_trsn=0;bhEff188(e);assert((f<5)==!!e->flg);}
    e=effect_145_fixture(184);e->py=50;e->type=0;int frames=0;
    while(e->flg&&frames<1100){sys->ef_trsn=0;bhEff184(e);++frames;}assert(!e->flg&&frames>10&&frames<1100);
    for(int type=1;type<=6;++type){
        e=effect_145_fixture(185);e->type=type;
        for(int f=0;f<12;++f){sys->ef_trsn=0;bhEff185(e);assert(sys->ef_trsn==1&&e->ct0==(f+1)%4);}
    }
    for(int kind=0;kind<2;++kind){
        e=effect_145_fixture(210+kind);e->mode1=1;
        for(int f=0;f<(kind?11:21);++f){sys->ef_trsn=0;if(kind)bhEff211(e);else bhEff210(e);}
        assert(!e->type&&!e->mode1&&!e->mode0&&sys->ef_trsn==0);
    }
    for(int type=1;type<=17;++type){
        e=effect_145_fixture(218);e->mode1=type;
        for(int f=0;f<32;++f){sys->ef_trsn=0;bhEff218(e);assert(sys->ef_trsn==1&&e->ct0>=0&&e->ct0<14);}
    }
    e=effect_145_fixture(212);e->mode1=1;
    for(int f=0;f<48;++f){sys->ef_trsn=0;bhEff212(e);assert(e->ct0==f%16);}
    for(int type=1;type<=3;++type){e=effect_145_fixture(213);e->mode1=type;
        for(int f=0;f<12;++f){sys->ef_trsn=0;bhEff213(e);assert(e->ct0==f%3);}}
    e=effect_145_fixture(144);e->mdlver=2;
    for(int f=0;f<20;++f){sys->ef_trsn=0;bhEff144(e);assert(sys->ef_trsn==1&&e->ani_ct==2&&e->ct1==f%6);}
    e=effect_145_fixture(189);e->mode1=1;e->py=20;e->lkono=0;
    for(int f=0;f<100&&(e->type||f==0);++f){sys->ef_trsn=0;bhEff189(e);if(!e->type)break;assert(e->ct0<10);}
    assert(!e->type&&e->py==20);
    e=effect_145_fixture(183);e->mode1=2;bhEff183(e);assert(eff[1].id==184&&eff[1].type==1);
    for(int type=1;type<=4;++type){e=effect_145_fixture(187);e->mode1=type;bhEff187(e);assert(eff[1].id==188);}
    e=effect_145_fixture(190);e->mode1=1;bhEff190(e);assert(eff[1].id==191);
    e=effect_145_fixture(208);e->mode1=1;for(int f=0;f<7;++f)bhEff208(e);assert(eff[1].id==209);
    e=effect_145_fixture(214);e->mode1=1;bhEff214(e);assert(eff[1].id==11&&!e->mode1);
    for(int type=1;type<=9;++type){e=effect_145_fixture(215);e->mode1=type;bhEff215(e);
        assert(eff[1].id==(type<=6?216:217)&&eff[1].type==(type-1)%3+(type<=6?(type-1)/3*3:0));}
    for(int type=0;type<3;++type){
        e=effect_145_fixture(217);e->type=type;bhEff217(e);P_WRK *p=(P_WRK *)e->exp0;
        assert(p&&p->num==16);int lifetime=e->ct0;
        for(int f=0;f<=lifetime;++f){sys->ef_fncn=0;bhEff217(e);}
        assert(!e->flg&&!p->flg);
    }
    e=effect_145_fixture(143);bhEff143(e);P_WRK *dust=(P_WRK *)e->exp0;
    assert(dust&&dust->num==16&&e->mode0==1);bhEff143(e);e->mode1=1;
    for(int f=0;f<60&&e->mode1;++f){sys->ef_fncn=0;bhEff143(e);}
    assert(!e->mode1&&e->mode0==1);
    for(int i=0;i<16;++i)dust->pos[i]=(NJS_POINT3){cam.wpx+30*cam.vx,cam.wpy+30*cam.vy,cam.wpz+30*cam.vz};
    e->tex_id=0;recvxGameBootRenderBeginFrame();bhEff143_sub(e);
    assert(recvxGameBootRenderFrame()->vertex_count==96);recvxGameBootRenderConsume();
    for(int type=1;type<=3;++type){
        e=effect_145_fixture(219);e->mode1=type;e->ax=32760;e->sx=8;sys->ef_tn[409]=0;
        bhEff219(e);EFF5SNOWRECT *snow=(EFF5SNOWRECT *)e->exp0;
        assert(snow&&snow->lSnowTotalMax==850&&snow->lSnowGrpMax==9&&snow->lSnowExistCrnt==850);
        for(int f=0;f<3;++f){sys->ef_fncn=0;bhEff219(e);assert(sys->ef_fncn==1);}
        recvxGameBootRenderBeginFrame();njSetMatrix(NULL,cam.mtx);DrawEff5SnowRect(e);
        assert(!recvxEngineOriginalFault());recvxGameBootRenderConsume();
        e->ax=-1;for(int f=0;f<150;++f){sys->ef_fncn=0;bhEff219(e);}assert(!snow->lSnowExistCrnt);
        DeleteEff5SnowRect(snow);assert(!snow->flg);
    }
    e=effect_145_fixture(220);e->mode1=1;e->lkono=4;bhEff220(e);
    for(int child=1;child<=4;++child)assert(eff[child].id==221&&eff[child].tex_id==417);
    for(int frame=0;frame<55;++frame){
        e=effect_145_fixture(221);e->mdlver=frame;e->loy=.5f;e->flr_no=7;bhEff221(e);
        NJS_TEXTUREH_VTX *v=(NJS_TEXTUREH_VTX *)e->tvp;
        for(int i=0;i<4;++i)assert(isfinite(v[i].u)&&v[i].u>=0&&v[i].v>=0&&v[i].v<=1);
        sys->ef_tn[e->tex_id]=0;
        e->px=cam.wpx+30*cam.vx;e->py=cam.wpy+30*cam.vy;e->pz=cam.wpz+30*cam.vz;e->spd=e->py-10;
        recvxGameBootRenderBeginFrame();bhDrawEff221(e);
        assert(!recvxEngineOriginalFault()&&recvxGameBootRenderFrame()->vertex_count>0);
        recvxGameBootRenderConsume();
        for(int f=0;f<100&&e->flg;++f){sys->ef_fncn=0;bhEff221(e);}
        assert(!e->flg);
    }
    /* The dial puzzle restores gameplay flags and publishes the 128 answer. */
    e=effect_145_fixture(133);e->mode1=1;sys->ond_cx=3;sys->pad_ps=0;
    sys->ond_n[0]=1;sys->ond_n[1]=2;sys->ond_n[2]=8;unsigned int flags=sys->sp_flg;
    bhEff133(e);assert(e->mode0==1);e->mode0=2;bhEff133(e);
    assert(!e->mode0&&sys->sp_flg==flags&&bhCkFlg(sys->ev_flg,58));
    e=effect_145_fixture(134);e->mode1=1;e->lkono=0;sys->pad_ps=sys->pad_on=0;
    bhEff134(e);assert(e->mode0==1&&e->func==(void *)bhDraw134);
    sys->ef_tn[44]=0;recvxGameBootDrawDiscard();bhDraw134(e);
    size_t overlay_count;recvxGameBootDraws(&overlay_count);assert(overlay_count==10);
    e=effect_145_fixture(137);e->mode1=1;e->type=0;bhEff137(e);
    assert(e->mode0==10&&e->func==(void *)bhDraw137);
    sys->ef_tn[440]=4;recvxGameBootDrawDiscard();bhDraw137(e);
    recvxGameBootDraws(&overlay_count);assert(overlay_count>20);cam=oldcam;
    e=effect_145_fixture(138);e->mode1=1;unsigned int background=rom->bak_col;
    bhEff138(e);assert(e->mode0==1&&e->ct0==2&&e->clp_jno[0]==3&&e->clp_jno[1]==5);
    sys->ef_tn[441]=0;recvxGameBootDrawDiscard();bhDraw138(e);
    recvxGameBootDraws(&overlay_count);assert(overlay_count>10);recvxGameBootDrawDiscard();
    e->mode0=11;bhEff138(e);assert(!e->mode0&&rom->bak_col==background);
    /* Borrow a real loaded joint and check the six-frame snapshot ring. */
    int slot=-1,joint=-1;
    for(int a=0;a<16&&slot<0;++a)if(plp->mdl[a].objP)for(unsigned int j=0;j<plp->mdl[a].obj_num;++j)
        if(plp->mdl[a].objP[j].model&&plp->mdl[a].objP[j].model->vlist){slot=a;joint=j;break;}
    assert(slot>=0);
    e=effect_145_fixture(135);e->flg|=0x80;e->mode1=1;e->lkwkp=(unsigned char *)plp;e->type=slot;e->lkono=joint;
    bhEff135(e);assert(e->mode0==1&&e->exp0);bhEff135(e);
    O_WRK *ghost=&eff[1];assert(ghost->id==136&&ghost->type==0);bhEff136(ghost);
    size_t bytes=recvxGameBootEffectVlistBytes((int *)ghost->exp0);assert(bytes>4&&ghost->ct0==5);
    assert(ghost->exp0!=(unsigned char *)plp->mdl[slot].objP[joint].model->vlist);
    assert(!memcmp(ghost->exp0,plp->mdl[slot].objP[joint].model->vlist,8));
    NJS_CNK_MODEL *mesh=plp->mdl[slot].objP[joint].model;
    size_t polygon_bytes=recvxGameBootSourceRemaining(mesh->plist);assert(polygon_bytes);
    void *polygon_backup=malloc(polygon_bytes);assert(polygon_backup);memcpy(polygon_backup,mesh->plist,polygon_bytes);
    int *vertices=mesh->vlist;
    recvxGameBootRenderBeginFrame();bhDraw136(ghost);assert(!recvxEngineOriginalFault()&&mesh->vlist==vertices);
    recvxGameBootRenderConsume();memcpy(mesh->plist,polygon_backup,polygon_bytes);free(polygon_backup);
    for(int f=0;f<6;++f){sys->ef_fncn=0;bhEff136(ghost);}assert(!ghost->flg);
    memset(ef_Zanzo,0,6*sizeof(*ef_Zanzo));
    /* A flying bolt advances until cancelled; no fake impact is required. */
    e=effect_145_fixture(130);e->type=4;e->px=e->pxb=500;e->py=e->pyb=500;e->pz=e->pzb=500;e->loz=1;
    sys->cb_flg|=4;bhEff130(e);assert(!e->flg&&e->pz==501);
    memcpy(eff,saved,sizeof(eff));*sys=*old;cam=oldcam;*plp=oldplayer;njSetMatrix(NULL,&matrix);free(saved);free(old);
    printf("PASS: 32 new effects: %u finite sprite variants, water impacts, button/fire loops, child spawns, dust/snow wind and density, 55 debris tiles, puzzle state, afterimage snapshots and projectile cancellation\n",variants);
}

static void test_effect_191_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    NJS_MATRIX matrix;njGetMatrix(&matrix);
    static const struct{int id,frames,variants;void (*update)(O_WRK *);} sprites[]={
        {191,20,1,bhEff191},{193,15,1,bhEff193},{195,15,3,bhEff195},{207,24,4,bhEff207}};
    for(size_t k=0;k<sizeof(sprites)/sizeof(*sprites);++k)for(int type=0;type<sprites[k].variants;++type){
        O_WRK *e=effect_145_fixture(sprites[k].id);e->type=type;e->sy=sprites[k].id==193?15:1;e->sz=14.5f;
        /* bhSetEffectTb supplies these local quad corners before callbacks. */
        for(int v=0;v<4;++v){e->tv[v].x=(v&1)?1:-1;e->tv[v].y=(v&2)?1:-1;}

        e->px=cam.wpx+10*cam.vx;e->py=cam.wpy+10*cam.vy;e->pz=cam.wpz+10*cam.vz;
        int drawn=0;
        while(e->flg&&drawn<40){
            sys->ef_trsn=0;sprites[k].update(e);if(!e->flg)break;
            assert(sys->ef_trsn==1&&isfinite(e->px)&&isfinite(e->py)&&isfinite(e->pz));
            for(int v=0;v<4;++v)assert(e->tv[v].u>=0&&e->tv[v].u<1&&e->tv[v].v>=0&&e->tv[v].v<1);
            if(!drawn){
                sys->ef_tn[e->tex_id]=0;recvxGameBootRenderBeginFrame();
                njSetMatrix(NULL,cam.mtx);
                bhDrawTrsEffect3D((unsigned int *)sys->ef_trs,1);
                if(!recvxGameBootRenderFrame()->vertex_count)fprintf(stderr,"empty late effect draw: id=%d type=%d\n",sprites[k].id,type);
                assert(!recvxEngineOriginalFault()&&recvxGameBootRenderFrame()->vertex_count>0);
                recvxGameBootRenderConsume();
            }
            ++drawn;
        }
        assert(!e->flg&&drawn==sprites[k].frames);
    }
    for(int type=1;type<=24;++type){
        O_WRK *e=effect_145_fixture(196);e->mode1=type;
        for(int step=0;step<25;++step){sys->ef_trsn=0;bhEff196(e);
            assert(sys->ef_trsn==1&&e->ani_ct==(type-1)%6&&e->ct0==step%(((type-1)/6)%4==3?3:8));}
        e->type=e->mode1=0;sys->ef_trsn=0;bhEff196(e);assert(!sys->ef_trsn&&(e->flg&0x1000000));
    }
    O_WRK *changing=effect_145_fixture(196);changing->type=1;bhEff196(changing);
    changing->ct0=7;changing->type=19;sys->ef_trsn=0;bhEff196(changing);
    assert(!changing->ct0&&sys->ef_trsn==1); /* Eight-frame to three-frame loop. */
    for(int kind=0;kind<2;++kind){
        O_WRK *e=effect_145_fixture(200+kind);e->mode1=1;e->ax=321;
        for(int frame=0;frame<40;++frame){sys->ef_trsn=0;if(kind)bhEff201(e);else bhEff200(e);
            assert(sys->ef_trsn==1&&e->ct0>=0&&e->ct0<(kind?16:8));}
        if(!kind)assert(e->az==321);
    }
    O_WRK *e=effect_145_fixture(205);e->mode1=1;e->sz=2;
    for(int frame=0;frame<28;++frame){sys->ef_trsn=0;bhEff205(e);assert(sys->ef_trsn==1);}
    assert(e->mode0==2&&!e->ct0);
    sys->ef_trsn=0;bhEff205(e);assert(e->ct0==1&&e->tv[0].u==160.0f/256);
    sys->ef_trsn=0;bhEff205(e);assert(e->ct0==0&&e->tv[0].u==0);
    e->type=e->mode1=0;bhEff205(e);assert(!e->mode0);
    e=effect_145_fixture(192);e->mode1=1;e->lkono=3;bhEff192(e);
    assert(!e->type&&!e->mode1);for(int i=1;i<=3;++i)assert(eff[i].id==193&&eff[i].flg);
    e=effect_145_fixture(194);e->mode1=3;bhEff194(e);assert(eff[1].id==195&&eff[1].type==2);
    for(int type=1;type<=4;++type){
        e=effect_145_fixture(202);e->mode1=type;e->lkono=2;e->sz=10;
        bhEff202(e);assert(!eff[1].flg);bhEff202(e);
        O_WRK *child=&eff[1];assert(child->flg&&child->id==203&&child->type==(type%2?1:2)&&child->mdlver<4&&child->exp0==(unsigned char *)e);
        int frames=0;while(child->flg&&frames<65){sys->ef_trsn=0;bhEff203(child);++frames;}
        assert(!child->flg&&frames==(type%2?61:31));
        child->flg=1;e->stflg|=0x1000000;bhEff203(child);assert(!child->flg);
    }
    for(int type=1;type<=2;++type){
        e=effect_145_fixture(203);e->type=type;e->sz=100;e->exp0=(unsigned char *)&eff[1];
        eff[1].flg=1;sys->ef_trsn=0;bhEff203(e);assert(e->ct1==1);
        bhEff203(e);assert(!e->flg); /* Fast particles still have a valid divisor. */
    }
    for(int type=1;type<=4;++type){
        e=effect_145_fixture(206);e->mode1=type;
        for(int frame=0;frame<13;++frame)bhEff206(e);
        assert(eff[1].id==207&&eff[1].type==type-1&&eff[2].id==207&&!eff[3].flg);
    }
    for(int kind=0;kind<2;++kind){
        e=effect_145_fixture(kind?204:198);e->type=1;
        sys->ef_opqn=0;if(kind)bhEff204(e);else bhEff198(e);
        assert(sys->ef_opqn==1&&sys->ef_opq[0]==e);
        if(kind)assert(sys->ef_tn[449]==7&&e->tv[0].col==0xff000000u);
    }
    e=effect_145_fixture(197);O_WRK untouched=*e;bhEff197();assert(!memcmp(e,&untouched,sizeof(*e)));
    NJS_MATRIX camera;memcpy(&camera,cam.mtx,sizeof(camera));njUnitMatrix(cam.mtx);
    recvxGameBootRenderBeginFrame();bhEff199();assert(recvxGameBootRenderFrame()->vertex_count==360);
    recvxGameBootRenderConsume();memcpy(cam.mtx,&camera,sizeof(camera));njSetMatrix(NULL,&matrix);
    memcpy(eff,saved,sizeof(eff));*sys=*old;free(saved);free(old);
    puts("PASS: effects 191-207 sprite lifetimes/variants/drawing, all 24 palette cycles, loop/hold/restart, burst and periodic emitters, linked particle movement/lifetime/cancellation, opaque queues and 66 polygon particles");
}

static void test_effect_170_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    for(int repeats=0;repeats<=3;++repeats){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=repeats;e->sx=e->sy=1;
        int draws=0;
        for(int i=0;i<37&&e->flg;++i){sys->ef_trsn=0;bhEff171(e);draws+=sys->ef_trsn;}
        if(repeats)assert(!e->flg&&draws==repeats*10);
        else assert(e->flg&&draws==37&&e->ct1==-3);
    }
    for(int blend=0;blend<100;++blend){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=blend;
        for(int i=0;i<3;++i){
            sys->ef_trsn=0;bhEff173(e);
            assert(e->flg&&e->bl_src==2+blend/10&&e->bl_dst==2+blend%10&&sys->ef_trsn==1);
            assert(e->ct0==1&&e->sx==3.984375f&&e->sy==3.984375f);
        }
    }
    memset(eff,0,sizeof(eff));O_WRK *moon=&eff[0];moon->flg=1;moon->sx=2;moon->sy=3;
    for(int i=0;i<3;++i){sys->ef_trsn=0;bhEff176(moon);assert(moon->ct0==0&&moon->sx==8&&moon->sy==12&&sys->ef_trsn==1);}
    for(int start=0;start<=6;++start){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=start;
        e->sx=e->sy=1;e->sz=0.25f;e->lkono=20;
        int visible=0;while(e->flg){sys->ef_trsn=0;bhEff178(e);visible+=sys->ef_trsn;}
        assert(visible==19&&e->ct1==20&&e->pz==4.75f);
    }
    for(int variant=0;variant<2;++variant){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=variant;e->sx=e->sy=1;
        int visible=0;while(e->flg){sys->ef_trsn=0;bhEff180(e);visible+=sys->ef_trsn;}
        assert(visible==10&&e->tex_id==408&&e->ani_ct==variant);
    }
    static const int smoke_frames[10]={20,22,24,24,16,20,17,16,17,20};
    static const unsigned int colors[10]={0xffffffffu,0xd0201010u,0xffe0e0e0u,0xffc01000u,
        0xffa0c090u,0xfff0f040u,0xff806040u,0xffc0e0ffu,0xd0201010u,0xff404040u};
    for(int style=0;style<10;++style)for(int color=0;color<10;++color){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->type=color*10+style;
        e->sx=e->sy=1;e->sz=2.25f;e->pn=4;e->tvp=e->tv;e->pvp=e->pv;e->mtx=(NJS_MATRIX *)e->mtxbuf;
        e->px=cam.wpx+10*cam.vx;e->py=cam.wpy+10*cam.vy;e->pz=cam.wpz+10*cam.vz;
        float start_z=e->pz;int visible=0;
        while(e->flg&&visible<30){
            sys->ef_trsn=0;bhEff182(e);
            if(!e->flg)break;
            assert(e->tv[0].col==colors[color]&&sys->ef_trsn==1);
            if(visible==0){
                /* Opening-room owned textures stand in for unavailable smoke atlases. */
                sys->ef_tn[e->tex_id]=0;recvxGameBootRenderBeginFrame();
                njSetMatrix(NULL,cam.mtx);
                bhDrawTrsEffect3D((unsigned int *)sys->ef_trs,1);assert(!recvxEngineOriginalFault());
            }
            ++visible;
        }
        assert(!e->flg&&visible==smoke_frames[style]);
        assert(fabsf(e->pz-start_z-(visible+1)*0.25f)<0.0001f);
    }
    static const struct{void (*update)(O_WRK *);int child,type;} emitters[]={
        {bhEff172,173,1},{bhEff175,176,0},{bhEff177,178,1}};
    for(size_t i=0;i<sizeof(emitters)/sizeof(*emitters);++i){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->mode1=2;e->lkono=27;
        emitters[i].update(e);
        assert(!e->type&&!e->mode1&&eff[1].id==emitters[i].child&&eff[1].type==emitters[i].type);
        if(emitters[i].child==178)assert(eff[1].lkono==27);
        emitters[i].update(e);assert(!eff[2].flg);
        memset(eff,0,sizeof(eff));e=&eff[0];e->flg=1;e->type=2;
        for(int j=1;j<512;++j)eff[j].flg=2;
        emitters[i].update(e);assert(!e->type&&!e->mode1);
    }
    for(int kind=0;kind<2;++kind)for(int periodic=0;periodic<2;++periodic){
        memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];e->flg=1;e->mode1=2;e->sz=periodic?3.75f:0;
        for(int frame=0;frame<8;++frame){if(kind)bhEff181(e);else bhEff179(e);}
        int count=0;for(int i=1;i<512;++i)if(eff[i].flg){assert(eff[i].id==(kind?182:180));++count;}
        assert(count==(periodic?3:1));
        if(!periodic)assert(!e->type&&!e->mode1);
        else assert(e->type==2&&e->ct1==1);
    }
    memset(eff,0,sizeof(eff));O_WRK *trail=&eff[0];trail->flg=1;trail->mode1=2;
    trail->px=3;trail->py=20;trail->pz=4;trail->sz=0.25f;
    for(int i=0;i<4;++i)bhEff170(trail);
    assert(!trail->type&&!trail->ct0&&trail->px==3&&trail->py==20&&trail->pz==4);
    assert(fabsf(trail->loz-1)<0.0001f&&fabsf(trail->loy+0.6f)<0.0001f);
    assert(eff[1].id==171&&eff[2].id==171&&!eff[3].flg);
    memcpy(eff,saved,sizeof(eff));*sys=*old;free(saved);free(old);
    puts("PASS: repeated sprites, all 100 blends and 100 smoke style/color combinations, fractional velocity, timed sprites, static sprite, one-shot/periodic emitters, child lifetime propagation and moving-trail restoration");
}

static O_WRK *effect_145_fixture(int id)
{
    memset(eff,0,sizeof(eff));O_WRK *e=&eff[0];
    e->flg=1;e->id=id;e->sx=e->sy=e->sz=1;e->sxb=e->syb=e->szb=1;
    e->tvp=e->tv;e->pvp=e->pv;e->pn=4;e->mtx=(NJS_MATRIX *)e->mtxbuf;
    njUnitMatrix(e->mtx);return e;
}
static void test_effect_145_batch(void)
{
    O_WRK *saved=malloc(sizeof(eff));SYS_WORK *old=malloc(sizeof(*sys));
    assert(saved&&old);memcpy(saved,eff,sizeof(eff));*old=*sys;
    O_WRK *eye=effect_145_fixture(145);
    eye->mtx[0][12]=3;eye->mtx[0][13]=20;eye->mtx[0][14]=4;
    bhEff145(eye);assert(eye->mode0==1&&eye->tv[0].col==0xffff0000u);
    bhEff145(eye);
    assert(eff[1].id==146&&eff[2].id==146&&eff[1].exp0==(unsigned char *)eye);
    assert(fabsf(eff[1].px-3.28f)<1e-5f&&fabsf(eff[2].px-2.72f)<1e-5f);
    for(int frame=0;frame<20;++frame){
        sys->ef_trsn=0;
        for(int i=1;i<=2;++i)if(eff[i].flg)bhEff_Sub146(&eff[i]);
    }
    assert(!eff[1].flg&&!eff[2].flg);
    eye->mode1=2;bhEff145(eye);assert((eye->tv[0].col>>24)==239);
    sys->ef_trsn=0;
    for(int i=1;i<=2;++i)bhEff_Sub146(&eff[i]);
    assert(sys->ef_trsn==2&&!eff[1].flg&&!eff[2].flg&&eff[1].tv[3].col==eye->tv[0].col);
    for(int i=1;i<512;++i)eff[i].flg=2;
    bhEff145(eye);assert(eff[511].flg==2&&eye->flg&1u);
    for(int variant=0;variant<2;++variant){
        O_WRK *e=effect_145_fixture(147);e->type=variant;sys->ef_trsn=0;
        bhEff147(e);assert(sys->ef_trsn==1&&e->tex_id==80&&e->bl_dst==3);
        assert(e->tv[0].col==(variant?0xff5f0000u:0xff400000u));
        sys->ef_trsn=512;bhEff147(e);assert(sys->ef_trsn==512);
        e->stflg=0x1000000;sys->ef_trsn=0;bhEff147(e);assert(!sys->ef_trsn);
    }
    O_WRK *drop=effect_145_fixture(148);drop->px=3;drop->py=20;drop->pz=4;
    sys->ef_trsn=0;bhEff148(drop);assert(drop->ofy==20&&!sys->ef_trsn);
    drop->mode1=1;int falling=0;
    while(drop->mode1==1&&falling++<100){sys->ef_trsn=0;bhEff148(drop);assert(sys->ef_trsn==1);}
    assert(falling<100&&drop->mode1==2&&drop->py==drop->gpy);
    sys->ef_trsn=0;bhEff148(drop);assert(drop->py==20&&drop->yn==0&&!drop->mode1&&!sys->ef_trsn);
    O_WRK *fire=effect_145_fixture(149);fire->sx=2;fire->sy=3;
    sys->ef_tn[80]=0;sys->ef_tn[4]=0;
    bhEff149(fire);assert(fire->mode0==6&&fire->func);
    fire->mode1=1;bhEff149(fire);assert(fire->mode0==1);
    while(fire->mode0==1){sys->ef_fncn=0;bhEff149(fire);assert(sys->ef_fncn==1);}
    assert(fire->mode0==2&&fire->ah==1);
    fire->px=cam.wpx+cam.vx*10;fire->py=cam.wpy+cam.vy*10;fire->pz=cam.wpz+cam.vz*10;
    recvxGameBootRenderBeginFrame();fire->func(fire);assert(!recvxEngineOriginalFault());
    fire->ct3=0;sys->ef_fncn=0;bhEff149(fire);assert(eff[1].id==264);
    O_WRK *sparks=&eff[1];bhEff_E12_FireSpark(sparks);assert(sparks->mode0==1&&sparks->func);
    P_WRK *particles=(P_WRK *)sparks->exp0;assert(particles->num==16);
    int spark_frames=0;
    while(sparks->flg&&spark_frames++<70){
        sys->ef_fncn=0;bhEff_E12_FireSpark(sparks);
        if(sys->ef_fncn){recvxGameBootRenderBeginFrame();sparks->func(sparks);}
    }
    assert(!sparks->flg&&!particles->flg&&spark_frames<70);
    fire->mode1=2;int fade_frames=0;
    while(fire->mode0&&fade_frames++<60){sys->ef_fncn=0;bhEff149(fire);}
    assert(!fire->mode0&&fire->sx==2&&fire->sy==3&&fade_frames<60);
    for(int variant=0;variant<2;++variant){
        O_WRK *e=effect_145_fixture(151);e->type=variant;e->sz=0.5f;
        int frames=0;while(e->flg){sys->ef_trsn=0;bhEff151(e);frames+=e->flg!=0;}
        assert(frames==10&&e->ani_ct==variant&&e->py<0);
        e=effect_145_fixture(151);e->type=variant;bhEff151(e);
        e->type=1-variant;sys->ef_trsn=0;bhEff151(e);
        assert(e->ct0==1&&e->ani_ct==1-variant);
    }
    for(int phase=0;phase<12;++phase)for(int strength=0;strength<=20;strength+=10){
        O_WRK *e=effect_145_fixture(153);e->type=phase;e->flr_no=strength;
        for(int frame=0;frame<2;++frame){
            sys->ef_trsn=0;bhEff153(e);assert(e->flg&&sys->ef_trsn==1);
            assert((e->tv[0].col>>24)==(strength>=20?0:255-strength*255/20));
        }
        bhEff153(e);assert(!e->flg);
    }
    O_WRK *line=effect_145_fixture(152);line->mode1=3;line->lkono=10;line->px=3;line->py=20;line->pz=4;
    for(int frame=0;frame<12;++frame){
        memset(eff+1,0,sizeof(eff)-sizeof(*eff));bhEff152(line);
        int phase=(frame+1)%12;
        assert(line->ct1==phase&&line->px==3&&line->py==20&&line->pz==4);
        for(int i=1;i<=3;++i)assert(eff[i].id==153&&eff[i].type==(phase+13-i)%12&&eff[i].flr_no==10);
    }
    O_WRK *burst=effect_145_fixture(150);burst->mode1=2;bhEff150(burst);
    assert(eff[1].id==151&&eff[1].type==1&&!burst->type&&!burst->mode1);
    burst=effect_145_fixture(154);burst->mode1=1;int previous=-1;
    for(int frame=0;frame<15;++frame){
        bhEff154(burst);
        if(!(frame%7)){int slot=frame/7+1;assert(eff[slot].id==155&&eff[slot].type!=previous);previous=eff[slot].type;}
    }
    memcpy(eff,saved,sizeof(eff));*sys=*old;free(saved);free(old);
    puts("PASS: linked eye spawn/fade and pool exhaustion, hidden/full shadow queues, falling drop/reset, fire sprite draw/fade and spark context release, debris variant switching, all 12 impact frames/alpha levels, phased line emission and alternating spark bursts");
}

static void test_remaining_effect_services(void)
{
    O_WRK *saved=malloc(sizeof(eff));assert(saved);memcpy(saved,eff,sizeof(eff));
    unsigned char *saved_snapshot=sys->ef_psp;
    memset(eff,0,sizeof(eff));
    POINT point={0};point.px=1;point.py=2;point.pz=3;
    int first=bhSetEffect(20,&point,NULL,0);
    int second=bhSetEffectEvt(21,&point,7,100,200);
    assert(first==0&&second==1&&eff[0].id==20&&eff[1].id==21&&eff[1].type==7);
    assert(eff[0].px==1&&eff[0].py==2&&eff[0].pz==3&&eff[1].ax==100&&eff[1].ay==200);
    assert(eff[0].mtx==(NJS_MATRIX *)eff[0].mtxbuf&&eff[0].tvp==eff[0].tv&&eff[0].pn==4);
    assert(bhPushEffectWork()==1&&sys->ef_psp);
    eff[0].px=91;eff[1].flg=0;
    bhPopEffectWork();assert(eff[0].px==1&&eff[1].flg==1&&eff[1].id==21);
    memcpy(eff,saved,sizeof(eff));free(saved);sys->ef_psp=saved_snapshot;
    puts("PASS: shared original effect allocation, default quad/model state, event effect parameters and arena snapshot/restore");
}

static void test_remaining_opcode_state(void)
{
    SYS_WORK *saved_sys=malloc(sizeof(*sys));assert(saved_sys);*saved_sys=*sys;
    BH_PWORK saved_player=*plp;CAM_WORK saved_camera=cam;
    unsigned int saved_background=rom->bak_col;
    unsigned char *saved_pc=bhScePtr;
    unsigned char commands[]={8,19,3,0,9,5,0xf4,1,40,0,58,1,76,255,17,34,51,0,
        141,7,171,255,51,34,17,5,174,0,206,0,244};
    bhScePtr=commands;
    assert(bhScenarioJmpT[8]()==1&&sys->ef_slow==3&&bhScePtr==commands+4);
    assert(bhScenarioJmpT[9]()==1&&plp->hp==500&&bhScePtr==commands+8);
    assert(bhScenarioJmpT[40]()==1&&(sys->ts_flg&0x200)&&bhScePtr==commands+10);
    assert(bhScenarioJmpT[58]()==1&&(plp->flg&4)&&bhScePtr==commands+12);
    assert(bhScenarioJmpT[76]()==1&&sys->fog_col==0xff112233u&&bhScePtr==commands+18);
    assert(bhScenarioJmpT[141]()==1&&sys->com_num==7&&bhScePtr==commands+20);
    assert(bhScenarioJmpT[171]()==1&&rom->bak_col==0xff332211u&&sys->bcl_ct==5&&bhScePtr==commands+26);
    sys->ply_stflg[0]|=0x200000;
    assert(bhScenarioJmpT[174]()==1&&!(sys->ply_stflg[0]&0x200000)&&bhScePtr==commands+28);
    assert(bhScenarioJmpT[206]()==1&&(sys->gm_flg&0x1000000)&&cam.pe_pers==11832&&bhScePtr==commands+30);
    assert(bhScenarioJmpT[244]()==1&&bhScePtr==commands+31);
    *sys=*saved_sys;free(saved_sys);*plp=saved_player;cam=saved_camera;
    rom->bak_col=saved_background;bhScePtr=saved_pc;
    puts("PASS: original remaining opcode state changes, packed odd-address operands, exact instruction lengths and one-byte COM_NEXT");
}

static void test_player_link_task(void)
{
    static unsigned char section[]={12,0,0,0,14,0,0,0,16,0,0,0,0,0,0,0,
        254,52,255,0,0,0,5,100,0,200,0,44,1,254,
        52,255,0,255,1,0,0,0,0,0,0,0,255,0};
    EVT_WORK *previous=rom->evtp;unsigned int enabled=sys->sp_flg;
    assert(sys->obwp&&rom->obj_n>0&&plp->mlwP&&plp->mlwP->obj_num);
    O_WRK *object=&sys->obwp[0],saved=*object;
    NJS_MATRIX parent;memcpy(parent,plp->mlwP->owP[0].mtx,sizeof(parent));
    memset(object,0,sizeof(*object));object->flg=0x20000001u;
    object->mtx=(NJS_MATRIX *)object->mtxbuf;object->mlwP=object->mdl;
    recvxEventCoreReset();rom->evtp=(EVT_WORK *)section;
    assert(!recvxEventCoreBindSection(section,sizeof(section))&&!Event_exec(0,0));sys->sp_flg|=0x10;
    assert(!bhEventScheduler2()&&recvxEventCoreTaskPc(0)==17);
    assert(!bhEventScheduler2()&&recvxEventCoreTaskPc(0)==30);
    assert(object->lkwkp==(unsigned char *)plp&&object->lkono==0&&object->flg==0x20000081u);
    assert(object->lox==-1&&object->loy==2&&object->loz==-3);
    NJS_POINT3 offset={-1,2,-3},expected;
    njCalcPoint(&parent,&offset,&expected);bhCalcModel((BH_PWORK *)object);
    assert(fabsf((*object->mtx)[12]-expected.x)<0.001f&&fabsf((*object->mtx)[13]-expected.y)<0.001f&&fabsf((*object->mtx)[14]-expected.z)<0.001f);
    plp->mlwP->owP[0].mtx[12]+=10;bhCalcLinkModel(plp);
    assert(fabsf((*object->mtx)[12]-(expected.x+10))<0.001f);
    memcpy(plp->mlwP->owP[0].mtx,parent,sizeof(parent));bhCalcLinkModel(plp);
    assert(!bhEventScheduler2()&&!bhEtask[0].status&&recvxEventCoreTaskPc(0)==44);
    assert(object->flg==0x20000001u&&object->lkono==255&&object->lkwkp==(unsigned char *)plp);
    *object=saved;recvxEventCoreReset();rom->evtp=previous;sys->sp_flg=enabled;
    puts("PASS: opcode 52 real player joint link, signed offsets, following pose movement, odd-address task 0 yield/resume/disable/end");
}

static void test_loaded_camera_reset(void)
{
    /* Real event dispatch and room cut/camera/light controllers with loaded
       room assets. Byte 1 is padding, including when it is nonzero. */
    static unsigned char script[]={12,0,0,0,15,0,0,0,16,0,0,0,91,255,0,0,0};
    static unsigned char truncated[]={12,0,0,0,13,0,0,0,14,0,0,0,91,0,0};
    RecvxInputState input={.analog_x=128,.analog_y=128};
    RecvxLiveTaskProc original=bhSysTaskJumpTab[0];
    recvxEventCoreReset();
    assert(recvxEventCoreBindSection(script,sizeof(script))==0);
    sys->st_flg|=1;sys->sp_flg|=0x40;
    plp->gpx=plp->px+100;plp->gpy=plp->py+100;plp->gpz=plp->pz+100;
    bhSysTaskJumpTab[0]=camera_reset_event_task;
    tick(&input);
    bhSysTaskJumpTab[0]=original;
    assert(!(sys->st_flg&1)&&plp->gpx==plp->px&&plp->gpy==plp->py&&plp->gpz==plp->pz);
    assert(bhScePtr==script+14&&recvxEventCoreInstructionCount()==2);
    assert(cam.ncut>=0&&cam.ncut<rom->cut_n);
    for(unsigned int j=0;j<16;++j)assert(isfinite((*cam.mtx)[j]));
    assert((*cam.mtx)[15]==1);
    recvxEventCoreReset();
    assert(recvxEventCoreBindSection(truncated,sizeof(truncated))==0);
    sys->st_flg|=1;float old_gpx=plp->gpx;
    assert(recvxEventCoreRunScenario(12)==-1);
    assert(recvxEventCoreGetFault()->code==RECVX_EVENT_FAULT_TRUNCATED);
    assert(recvxEventCoreGetFault()->opcode==91&&(sys->st_flg&1)&&plp->gpx==old_gpx);
    recvxEventCoreReset();
    puts("PASS: opcode 91 dispatch restores room camera and lighting with real room assets; truncated reset rejected before mutation");
}
#endif

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    size_t arena_size = 12u * 1024u * 1024u;
    void *arena = mmap((void *)0x20000000u, arena_size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS
#if UINTPTR_MAX > UINT32_MAX
        | MAP_32BIT
#endif
        , -1, 0);
    RecvxInputState input = {.analog_x = 128, .analog_y = 128};
    unsigned int i, warning_seen = 0, logo_seen = 0, title_seen = 0;
    unsigned int extra_frames = 1200;
    int title_idle = argc == 3 && !strcmp(argv[2], "--title-idle");
    assert((argc == 2 || argc == 3 || argc == 4) && arena != MAP_FAILED);
    if (argc == 3) assert(title_idle || !strcmp(argv[2], "--missing-assets"));
    if (argc == 4) {
        assert(!strcmp(argv[2], "--frames") && argv[3][0]);
        extra_frames = 0;
        for (const char *digit = argv[3]; *digit; ++digit) {
            assert(*digit >= '0' && *digit <= '9' && extra_frames < 10000);
            extra_frames = extra_frames * 10 + (unsigned int)(*digit - '0');
        }
        assert(extra_frames > 0 && extra_frames <= 10000);
    }
    test_texture_tiles();
    test_palette_variants();
    /* Original pointer arithmetic and 32-bit texture handles need low addresses.
       This checks startup logic on x86_64, not the complete original PSP ABI. */
    assert((uintptr_t)arena + arena_size < 0x80000000u);
    assert(recvxGameBootInit(argv[1], arena, 128) == -1);
    test_player_initialization(argv[1], arena, arena_size);
    test_facial_deformation();
    test_effect_control(argv[1], arena, arena_size);
    test_effect_callback_faults(argv[1],arena,arena_size);
    test_subplayer_movement(argv[1], arena, arena_size);
    test_player_control(argv[1], arena, arena_size);
    test_checkpoint(argv[1], arena, arena_size);
    if (argc != 3 || title_idle) test_inventory_resource(argv[1], arena, arena_size);
    test_gameplay_screen_saver(argv[1], arena, arena_size);
    if (argc != 3 || title_idle) {
        test_player_source_bounds(argv[1]);
        test_weapon_source_bounds(argv[1]);
        test_message_bounds(argv[1], arena, arena_size);
#if UINTPTR_MAX == UINT32_MAX
        test_weapon_motions_and_replacement(argv[1], arena, arena_size);
#endif
        test_room_read_and_expand(argv[1], arena, arena_size);
        test_game_direction_mapping(argv[1],arena,arena_size);
        test_invalid_enemy_work(argv[1],arena,arena_size);
        test_large_room_streams(argv[1], arena, arena_size);
    }
    assert(recvxGameBootInit(argv[1], arena, arena_size) == 0);
    assert(sys->tk_flg == 1);
    if (argc == 3 && !title_idle) {
        tick(&input);
        assert(recvxGameBootTick(&input) == -1);
        assert(strstr(recvxGameBootFault(), "archives/SYSTEM/1.bin"));
        assert(GetReadFileStatus() == -1 && sys->tk_flg == 0x300002);
        munmap(arena, arena_size);
        puts("PASS: missing SYSTEM resource stops boot and retains failed read status");
        return 0;
    }
    for (i = 0; i < 10000; ++i) {
        tick(&input);
        if (sys->tk_flg & 2) warning_seen = 1;
        if (sys->tk_flg & 4) logo_seen = 1;
        if ((sys->tk_flg & 8) && AdvWork.Mode == 6) { title_seen = 1; break; }
    }
    assert(warning_seen && logo_seen && title_seen && textured_frames > 300);
    assert(sys->sys_partid == 6 && AdvWork.PatId == 3);
    if (title_idle) {
        /* Exercise the real 900-tick timeout, opening dispatch and SYSTEM
           demo read, rather than jumping directly into the file loader. */
        assert(sizeof(void *) == 4);
        for (i = 0; i < 1100 && !sys->pdm_pd; ++i) tick(&input);
        assert(i < 1100 && (sys->ss_flg & 0x400000));
        assert(sys->pdm_dp && sys->mn_md1 == 1);
        assert(sys->pdm_pd == sys->pdm_dp +
            ((uintptr_t)&sys->save_end - (uintptr_t)&sys->version));
        assert(recvxGameBootSourceRemaining(sys->pdm_pd) >= 4);
        assert(GetReadFileStatus() == 0 && !recvxGameBootFault());
        recvxGameBootMovieShutdown(); recvxAudioShutdown();
        munmap(arena, arena_size);
        puts("PASS: original title idle timeout loads SYSTEM demo state and recorded controller input");
        return 0;
    }
    capture = 1; capture_path = "/tmp/recvx-game-title.ppm"; tick(&input);
    press(&input, RECVX_BUTTON_START);
    for (i = 0; i < 30 && AdvWork.Mode != 9; ++i) tick(&input);
    assert(AdvWork.Mode == 9);
    /* Preserve real workspace saves. The title selects Load when a valid
       slot exists; verify both menu directions before choosing New Game. */
    if(recvxGameBootSaveStorageAny()){
        assert(AdvWork.Cursor[0]==0);
        press(&input,RECVX_BUTTON_DOWN);assert(AdvWork.Cursor[0]==1);
        press(&input,RECVX_BUTTON_UP);assert(AdvWork.Cursor[0]==0);
        press(&input,RECVX_BUTTON_DOWN);
    }
    assert(AdvWork.Cursor[0] == 1);
    capture = 1; capture_path = "/tmp/recvx-game-menu.ppm"; tick(&input);
    press(&input, RECVX_BUTTON_CROSS);
    for (i = 0; i < 200 && !(sys->tk_flg & 0x20) &&
         !(sizeof(void *) == 4 && sys->mn_md0 == 4 && sys->mn_md1 == 11); ++i) {
        int result = recvxGameBootTick(&input);
        if (result) break;
    }
    /* Original live dispatcher enters Opening and Monitor in the same frame. */
    for (i = 0; i < 20 && !recvxGameBootFault(); ++i) {
        /* Inspect the initial room immediately after its event/effect update,
           before subsequent frames reset the draw queues and finish loading. */
        if (sizeof(void *) == 4 && sys->mn_md0 == 4 && sys->mn_md1 == 11) break;
        recvxGameBootTick(&input);
    }
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
    assert(!recvxGameBootFault() && !recvxAudioFault());
    assert(recvxEventCoreGetFault()->code == RECVX_EVENT_FAULT_NONE);
    assert(recvxEventCoreInstructionCount() == 144 && recvxEventCoreFlagWriteCount() == 64);
    assert(Event_T_timer == 1 && swork.pip == &sys->itm[sys->ply_id * 16]);
    assert(bhEtask[1].status == 0);
    assert(fabsf(rom->amb_r[2] - 0.2f) < 0.00001f);
    assert(sys->itwp[0].aspd == 0 && sys->itwp[4].aspd == 0);
    assert(sys->ef_ctrn == 14 && sys->ef_trsn == 0 && sys->ef_mdfn == 0 && sys->ef_trs2dn == 0);
    for (i = 9; i <= 11; ++i) {
        O_WRK *overlay = &eff[sys->efid[i]];
        unsigned int index = sys->ef_tn[45] + (i - 9);
        assert(overlay->id == 20 && overlay->mode0 == 1 && overlay->tex_id == 45);
        assert(overlay->ani_ct == (int)i - 9 && overlay->px == 0 && overlay->py == 0);
        assert(overlay->gidx == ((NJS_TEXMEMLIST *)(uintptr_t)sys->ef_tlist.textures[index].texaddr)->globalIndex);
    }
    assert(eff[sys->efid[1]].flg == 0 && eff[sys->efid[4]].flg == 0 && eff[sys->efid[8]].flg == 0);
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
    test_room_texture_save();
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
        assert(memcmp(mask->dst->vlist, mask->src->vlist, 64) == 0);
        if (i) assert(memcmp(mask->dst->vlist, mask->src->vlist, face_bytes) != 0);
        assert(ene[i].mode0 == 1 && ene[i].mode3 == 3);
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
    assert((sys->gm_flg & 0x800) && sys->sp_flg == (UINT32_MAX & ~0x10u));
    for (i = 0; i < (unsigned int)rom->lgt_n; ++i)
        assert(rom->lgtp[i].mode == 0 && rom->lgtp[i].light == NULL);
    for (i = 0; i < (unsigned int)rom->eff_n; ++i) {
        assert(sys->efid[i] >= 0 && sys->efid[i] < 512);
        assert(eff[sys->efid[i]].id == rom->effp[i].id);
    }
    test_loaded_player(argv[1]);
    for (i = 0; i < 20 && !recvxGameBootFault() && !game_frames; ++i) recvxGameBootTick(&input);
    if (recvxGameBootFault()) fprintf(stderr, "Unexpected native game boundary: %s\n", recvxGameBootFault());
    assert(!recvxGameBootFault() && game_frames == 1);
    assert(!recvxAudioBankBusy() && !recvxAudioFault());
    assert(sys->loop_ct == 1 && sys->loop_now == 0 && sys->fade_ct == 0 && sys->fade_hkn == 1);
    assert((unsigned int)sys->enow < 128 && ene[sys->enow].mode0 == 1);
    assert(ene[sys->enow].id == 91 || ene[sys->enow].id == 98);
    assert(plp->mode0 == 1 && !plp->mode1 && !plp->mode2 && !plp->mode3);
    /* Player wall/floor collision has returned and restored its collision flags. */
    if (plp->flg != 0x919 || plp->mtn_no != 42 || plp->frm_no != 131072)
        fprintf(stderr, "Unexpected player update state: flags=%x motion=%u frame=%d\n",
            plp->flg, plp->mtn_no, plp->frm_no);
    assert(plp->flg == 0x919 && plp->mtn_no == 42 && plp->frm_no == 131072);
    assert(plp->ar == 3.5f && plp->ah == 16.5f && plp->mdflg == 0x20);
    assert(sys->obwp[2].id == 1211 && sys->obwp[2].lkwkp == (unsigned char *)plp && sys->obwp[2].lkono == 5);
    assert(sys->obwp[2].mdl[0].objP == plp->mdl[4].objP);
    assert(cam.ncut >= 0 && cam.ncut < rom->cut_n && !(sys->gm_flg & 0x800));
    assert(cam.camver < 2 && isfinite(cam.px) && isfinite(cam.py) && isfinite(cam.pz));
    {
        const RecvxEngineOriginalLightingState *lights = recvxEngineOriginalLightingGet();
        assert(lights->api_calls > 0 && lights->matrix_updates >= 2 && !(lights->matrix_updates & 1u));
        assert(sys->lg_pnt >= 0 && sys->lg_pnt <= 3 && lights->easy_count == 1 + sys->lg_pnt);
        assert(lights->simple_count == lights->easy_count);
        assert(isfinite(lights->easy_direction.x) && isfinite(lights->easy_direction.y) && isfinite(lights->easy_direction.z));
    }
    assert(game_frames == 1);
    assert(plp->exp2 && (unsigned char *)plp->exp2 >= freemem && (unsigned char *)plp->exp2 + 1712 <= sys->memp);
    /* The checkpoint precedes the event, which may now change saved flags. */
    assert(sys->version == 10 && *(unsigned int *)freemem == 10);
    assert((plp->stflg & 0x1000000u) && (sys->st_flg & 1u) && cam.evc_no == 38);
    {
        float near_plane, far_plane;
        recvxEngineOriginalViewClipPlanes(&near_plane, &far_plane);
        assert(isfinite(near_plane) && isfinite(far_plane) && near_plane < 0 && far_plane < near_plane);
    }
    {
        int found = 0;
        for (i = 0; i < 512; ++i) if ((eff[i].flg & 1) && eff[i].id == 1 && eff[i].lkwkp == (unsigned char *)plp) {
            assert(eff[i].lkono == 1 && eff[i].sx == 4.5f && eff[i].sy == 4.0f && eff[i].sz == 3.5f); found = 1;
        }
        assert(found);
    }
#endif
    assert(sys->stg_no == 0 && sys->rom_no == 0 && sys->rcase == 0);
    assert(sys->obwp && sys->itwp && sys->doordp && sys->mes_ip && sys->mes_sp);
    assert(sys->ply_hp[0] == 160 && sys->mn_md0 == (sizeof(void *) == 4 ? 0 : 1));
    assert(sys->mn_md1 == (sizeof(void *) == 4 ? 0 : 4));
    assert(GetReadFileStatus() == 0);
    if (recvxGameBootFault()) assert(recvxGameBootTick(&input) == -1); /* A fault cannot advance or fake a room. */
#if UINTPTR_MAX == UINT32_MAX
    unsigned int initial_presentations = presentations;
    for (i = 0; i < extra_frames && !recvxGameBootFault(); ++i) {
        recvxGameBootTick(&input); recvxOriginalEngineClockAdvance(2, 1);
    }
    if (recvxGameBootFault()) fprintf(stderr, "Sustained native game boundary after %u frames: %s\n", game_frames, recvxGameBootFault());
    assert(!recvxGameBootFault());
    fprintf(stderr, "Original complete game frames: %u; event instructions: %u; task 2 PC: %u\n",
        game_frames, recvxEventCoreInstructionCount(), recvxEventCoreTaskPc(2));
    assert(game_frames > 1 && game_frames <= extra_frames + 1);
    assert(presentations >= initial_presentations + extra_frames);
    fprintf(stderr, "Movie presentations (hardware call model): %u\n", movie_presentations);
    test_loaded_player_restore();
    test_loaded_arm_ik();
    test_loaded_player_interactions();
    test_loaded_player_remaining();
    test_loaded_inventory_transition();
    test_loaded_camera_reset();
    test_camera_pause_task();
    test_player_link_task();
    test_remaining_opcode_boundaries();
    test_remaining_opcode_state();
    test_remaining_effect_services();
    test_effect_batch();
    test_effect_next_batch();
    test_effect_155_batch();
    test_effect_170_batch();
    test_effect_191_batch();
    test_effect_130_batch();
    test_remaining_effect_batch();
    test_packed_effect_batch();
    test_effect_145_batch();
    test_effect_collision_services();
    test_effect_callback_lifetimes();
    test_event_skip_task();
    test_vibration_event_task();
    test_frame_check_task();
    test_easy_sound_task();
    test_common_control_task();
    test_remaining_game_services();
    /* Casing cleanup must cover the entire pool, preserve inactive entries,
       and leave unrelated live effects and actor data intact. */
    for (i = 0; i < 512; ++i) {
        eff[i].id = i % 9;
        eff[i].flg = 0x100u | (i & 1u);
        eff[i].px = (float)i;
    }
    bhDeleteYakkyou();
    for (i = 0; i < 512; ++i) {
        unsigned int expected = ((i & 1u) && (i % 9 == 0 || i % 9 == 7)) ? 0 : 0x100u | (i & 1u);
        assert(eff[i].flg == expected && eff[i].px == (float)i && eff[i].id == i % 9);
    }
    puts("PASS: casing cleanup selectively releases active IDs 0/7 across all 512 effect slots");
#endif
    recvxGameBootTexturesShutdown();
    recvxGameBootMovieShutdown();
    recvxAudioShutdown();
    munmap(arena, arena_size);
#if UINTPTR_MAX > UINT32_MAX
    puts("PASS: sanitized title/new-game boot and bounded effect preflight; original relocation requires 32-bit ABI test");
#else
    printf("PASS: original player/enemy/inventory initialization, loading text and original Claire models/skin/motions/textures; original weapon loading and opening room relocation/textures; original room finalization/spawn/masks; original startup scenario/scheduler and NPC facial update; original effect control and real sound bank transfers; original main gameplay sequence/fade, NPC movement and Claire control/setup/idle pose/shadow; original camera cut/checkpoint, wall/floor collision, head tracking, linked objects, room lighting and actual opening-room and skinned actor geometry/material submission; complete game frames with original modifier shadows, 3D/2D effects and fade (%d effect textures retained)\n", sys->ef_ct);
#endif
    return 0;
}
