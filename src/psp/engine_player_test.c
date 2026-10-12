#include <malloc.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/camera.h"
#include "ps2/veronica/prog/cut.h"
#include "ps2/veronica/prog/dread.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/eneset.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/objitm.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/room.h"
#include "ps2/veronica/prog/system.h"
#include "psp/engine_pad_adapter.h"
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
#include "psp/engine_test_input.h"
#endif
#include "psp/engine_event_core.h"
#include "psp/engine_original_event_test.h"
#include "psp/engine_original_audio_test.h"
#include "psp/engine_event_runtime.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_memory.h"
#include "psp/engine_original_texture.h"
#include "psp/engine_original_view.h"
#include "psp/engine_room_owner.h"
#include "psp/engine_pacer.h"
#include "psp/original_clock.h"
#include "psp/live_task_dispatch.h"
#include "psp/mesh.h"
#include "psp/ninja_math.h"
#include "psp/platform.h"
#include "psp/player.h"
#include "psp/renderer.h"
#include "psp/scene.h"
#include "psp/texture.h"

#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
PSP_MODULE_INFO("RECVX PACKAGE 4C TEST", PSP_MODULE_USER, 0, 6);
#else
PSP_MODULE_INFO("RECVX PACKAGE 4A TEST", PSP_MODULE_USER, 0, 6);
#endif
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024);

#define ENGINE_ARENA_SIZE (12u * 1024u * 1024u)

static RecvxEngineRoomOwner room_owner;
static RecvxEngineRoomView room_view;
static RecvxScene scene;
static RecvxRoomMesh room_mesh, player_mesh;
static RecvxTextureSet room_textures, player_textures;
static RecvxPlayerAssets player_assets;
static RecvxPlayer engine_snapshot;
static RecvxInputState callback_input;
static RecvxLiveTaskProc callback_task_table[23];
static unsigned char *engine_arena_raw;
static unsigned char *engine_arena;
static unsigned char *room_file_copy;
static unsigned char *effect_file;
static unsigned char *effect_file_copy;
static unsigned char *player_file;
static unsigned char *player_models;
static unsigned char *player_motions;
static unsigned char *player_extra;
static size_t player_file_size, player_models_size, player_motions_size;
static size_t player_extra_size, player_source_bytes;
static size_t effect_file_size;
static char test_fault[128];
static char data_directory[512] = "data";
static FILE *test_report;
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
#define CALLBACK_DEFERRED_REPORT_CAPACITY (64u * 1024u)
static char *callback_deferred_report;
static size_t callback_deferred_report_size;
static int callback_report_overflow;
static int callback_fault;
static uint32_t callback_order;
static uint32_t callback_pad_ticks;
static uint32_t callback_game_ticks;
static uint32_t callback_event_ticks;
static uint32_t callback_trace_count;
static uint32_t callback_stall_count;
static uint32_t callback_task_us_total, callback_task_us_max;
static uint32_t callback_snapshot_us_total, callback_snapshot_us_max;
static uint32_t callback_draw_us_total, callback_draw_us_max;
static uint32_t callback_draw_count;
static uint32_t event0_wait_pc;
static uint32_t event0_wait_timer;
static uint32_t event0_completion_timer;
static uint32_t event0_previous_pc;
static uint32_t event0_previous_timer;
static uint32_t event0_initial_room_flag;
static uint32_t event0_room_flag_change_tick;
static uint32_t event0_room_flag_change_value;
static int event0_previous_active;
static int event0_wait_seen;
static int event0_resume_seen;
static int event0_completion_seen;
static int event0_room_flag_changed;
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
enum {
    CALLBACK_SCRIPTED_DIRECTION_TICKS = RECVX_ENGINE_TEST_INPUT_PHASE_TICKS,
    CALLBACK_SCRIPTED_DIRECTION_COUNT = RECVX_ENGINE_TEST_INPUT_PHASE_COUNT,
    CALLBACK_SCRIPTED_INPUT_TICKS =
        RECVX_ENGINE_TEST_INPUT_TOTAL_TICKS
};
static uint32_t callback_scripted_input_ticks;
static int callback_scripted_input_active;
static uint32_t callback_scripted_input_phase;
static uint32_t callback_scripted_pad_seen_mask;
static uint32_t callback_scripted_turn_changed_mask;
static int callback_scripted_phase_start_angle[CALLBACK_SCRIPTED_DIRECTION_COUNT];
static int callback_scripted_position_changed;
static float callback_scripted_start_x, callback_scripted_start_y;
static float callback_scripted_start_z;
static float callback_scripted_end_x, callback_scripted_end_y;
static float callback_scripted_end_z;
static uint32_t callback_pose_skip_start_hash;
static uint32_t callback_pose_skip_end_hash;
static uint32_t callback_pose_skip_ticks;
static int callback_pose_skip_start_valid;
static int callback_pose_skip_matrix_changed;
static size_t player_translation_size;
#endif
static RecvxEnginePacer callback_pacer;
#endif

#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
static int callback_report_is_deferred(const char *format)
{
    static const char *const prefixes[] = {
        "trace tick=", "pre-scripted game:", "before game:",
        "after game:", "event handlers:", "muted audio trace tick="
    };
    size_t index;

    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        size_t length = strlen(prefixes[index]);
        if (strncmp(format, prefixes[index], length) == 0)
            return 1;
    }
    return 0;
}
#endif

static void reportf(const char *format, ...)
{
    va_list args;
    char path[640];
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    int defer_callback_report = callback_report_is_deferred(format);
    char deferred_line[768];
#endif

    va_start(args, format);
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    if (defer_callback_report) {
        va_list deferred_args;
        int length;

        va_copy(deferred_args, args);
        length = vsnprintf(deferred_line, sizeof(deferred_line), format,
                           deferred_args);
        va_end(deferred_args);
        if (length < 0 || (size_t)length >= sizeof(deferred_line) ||
            callback_deferred_report == NULL ||
            (size_t)length > CALLBACK_DEFERRED_REPORT_CAPACITY -
                callback_deferred_report_size) {
            callback_report_overflow = 1;
        } else {
            memcpy(callback_deferred_report + callback_deferred_report_size,
                   deferred_line, (size_t)length);
            callback_deferred_report_size += (size_t)length;
        }
    }
    if (!defer_callback_report) {
        va_list psp_link_args;
        va_copy(psp_link_args, args);
        vfprintf(stderr, format, psp_link_args);
        fflush(stderr);
        va_end(psp_link_args);
    }
#endif
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    if (test_report == NULL && !defer_callback_report) {
#else
    if (test_report == NULL) {
#endif
        const char *report_name;
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
        report_name = "package4c-report.txt";
#else
        report_name = "package4a-report.txt";
#endif
        if (snprintf(path, sizeof(path), "%s/%s", data_directory,
                     report_name) >= (int)sizeof(path)) {
            va_end(args);
            return;
        }
        remove(path);
        test_report = fopen(path, "w");
        if (test_report == NULL) {
            va_end(args);
            return;
        }
    }
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    if (!defer_callback_report)
        vfprintf(test_report, format, args);
#else
    vfprintf(test_report, format, args);
#endif
    va_end(args);
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    if (!defer_callback_report)
        fflush(test_report);
#else
    fflush(test_report);
#endif
}

#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
static void report_callback_trace_dump(void)
{
    if (test_report != NULL) {
        if (callback_deferred_report_size != 0) {
            fputs("deferred callback trace follows\n", test_report);
            fwrite(callback_deferred_report, 1,
                   callback_deferred_report_size, test_report);
        }
        fflush(test_report);
        fclose(test_report);
        test_report = NULL;
    }
    if (callback_deferred_report_size != 0)
        fwrite(callback_deferred_report, 1,
               callback_deferred_report_size, stderr);
    if (callback_report_overflow)
        fputs("FAIL: deferred callback trace exceeded its 64 KiB buffer\n",
              stderr);
    fflush(stderr);
    free(callback_deferred_report);
    callback_deferred_report = NULL;
    callback_deferred_report_size = 0;
}
#endif

static int read_file(const char *relative_path, unsigned char **bytes,
                     size_t *size)
{
    char path[640];
    FILE *file;
    long length;
    unsigned char *data;

    if (snprintf(path, sizeof(path), "%s/%s", data_directory,
                 relative_path) >= (int)sizeof(path))
        return -1;
    file = fopen(path, "rb");
    if (file == NULL)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    data = malloc((size_t)length);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return -1;
    }
    fclose(file);
    *bytes = data;
    *size = (size_t)length;
    return 0;
}

#ifdef RECVX_ENGINE_AUTOMATED_INPUT
static uint32_t report_hash_bytes(const unsigned char *bytes, size_t size)
{
    uint32_t hash = 2166136261u;
    size_t index;

    for (index = 0; index < size; ++index)
        hash = (hash ^ bytes[index]) * 16777619u;
    return hash;
}

static int report_player_translation_data(void)
{
    size_t offset = 0;
    size_t translation_offset = 0;
    size_t translation_size = 0;
    size_t record_offset = 0;
    unsigned int chunk;
    unsigned int record;

    if (player_file == NULL || sys == NULL || sys->plzmtp == NULL)
        return -1;
    for (chunk = 0; chunk < 3; ++chunk) {
        uint32_t chunk_size;
        if (offset > player_file_size || player_file_size - offset < 4)
            return -2;
        memcpy(&chunk_size, player_file + offset, sizeof(chunk_size));
        offset += 4;
        if (chunk_size > player_file_size - offset)
            return -3;
        if (chunk == 2) {
            translation_offset = offset;
            translation_size = chunk_size;
        }
        offset += chunk_size;
    }
    player_translation_size = translation_size;
    reportf("player translation: src=%08x dst=%08x bytes=%u src-hash=%08x dst-hash=%08x dmlvl=%u\n",
            (unsigned int)(uintptr_t)(player_file + translation_offset),
            (unsigned int)(uintptr_t)sys->plzmtp,
            (unsigned int)translation_size,
            (unsigned int)report_hash_bytes(player_file + translation_offset,
                                            translation_size),
            (unsigned int)report_hash_bytes(sys->plzmtp, translation_size),
            (unsigned int)((EXP_WORK *)plp->exp0)->dmlvl);
    for (record = 0; record < 20 && record_offset + 4 <= translation_size;
         ++record) {
        uint32_t record_size;
        memcpy(&record_size, sys->plzmtp + record_offset,
               sizeof(record_size));
        reportf("player translation record[%u] offset=%u size=%08x\n",
                record, (unsigned int)record_offset,
                (unsigned int)record_size);
        if (record_size == UINT32_MAX)
            return 0;
        if (record_size > translation_size - record_offset - 4)
            return -4;
        record_offset += (size_t)record_size + 4;
    }
    return -5;
}
#endif

static int load_view_assets(void)
{
    int result;
    result = read_file("player-assets/player_0_costume_0_models.bin",
                       &player_models, &player_models_size);
    if (result == 0)
        result = read_file("player-assets/player_0_costume_0_motions.bin",
                           &player_motions, &player_motions_size);
    if (result == 0)
        result = read_file("player-assets/player_0_costume_0_extra.bin",
                           &player_extra, &player_extra_size);
    if (result != 0)
        return -1;
    if (recvxPlayerAssetsParse(player_models, player_models_size,
                               player_motions, player_motions_size,
                               player_extra, player_extra_size,
                               &player_assets) != 0 ||
        player_assets.model_count == 0)
        return -2;
    if (recvxBuildPlayerMesh(&player_assets.models[0], &player_mesh) != 0)
        return -3;
    result = read_file("player-assets/player_0_costume_0_textures.bin",
                       &player_file, &player_file_size);
    if (result != 0)
        return -4;
    if (recvxBuildPlayerTextures(player_file, player_file_size,
                                 &player_textures) != 0)
        return -5;
    free(player_file);
    player_file = NULL;
    player_source_bytes = player_models_size + player_motions_size +
                          player_extra_size + player_textures.storage_bytes;
    return 0;
}

static int prepare_room_view(void)
{
    if (recvxEngineRoomOwnerOpen(&room_owner, data_directory,
                                 "RM_0010.RDX") != RECVX_ENGINE_ROOM_OK)
        return -1;
    if (recvxBuildRoomScene(room_owner.data.bytes, room_owner.data.size,
                            &scene) != 0 ||
        recvxBuildRoomMesh(room_owner.data.bytes, room_owner.data.size,
                           &room_mesh) != 0 ||
        recvxBuildRoomTextures(room_owner.data.bytes, room_owner.data.size,
                               &room_textures) != 0)
        return -2;
    if (recvxEngineRoomOwnerView(&room_owner, &room_view) != 0 ||
        recvxEngineRoomViewPin(&room_view) != 0)
        return -3;
    if (recvxEngineRoomViewBytes(&room_view, 0, room_owner.data.size) == NULL)
        return -4;
    return 0;
}

static int prepare_original_room(void)
{
    const unsigned char *source = recvxEngineRoomViewBytes(
        &room_view, 0, room_owner.data.size);
    size_t room_size = room_owner.data.size;
    unsigned char *engine_cursor;
    Float aspect_width, aspect_height;
    if (source == NULL || room_size > INT32_MAX)
        return -4;

    recvxEngineEventRuntimeReset();
    recvxEngineOriginalFaultReset();
    recvxEngineOriginalViewReset();
    recvxEngineOriginalTextureReset();
    reportf("stage: original state reset\n");
    engine_arena_raw = malloc(ENGINE_ARENA_SIZE + 63u);
    if (engine_arena_raw == NULL)
        return -5;
    engine_arena = (unsigned char *)(((uintptr_t)engine_arena_raw + 63u) &
                                     ~(uintptr_t)63u);
    sys->memp = engine_arena;
    sys->endp = engine_arena + ENGINE_ARENA_SIZE;
    recvxEngineOriginalMemoryReset();
    freemem = engine_arena;
    njpmemp = engine_arena + ENGINE_ARENA_SIZE;
    keepmem = NULL;
    sys->gm_mode = 0;
    sys->ply_id = 0;
    sys->pos_no = 0;
    sys->ss_flg = 0;
    sys->ef_pbnk = 32;
    sys->obwp = (O_WRK *)bhGetFreeMemory(39936, 32);
    sys->itwp = (O_WRK *)bhGetFreeMemory(39936, 32);
    if (sys->obwp == NULL || sys->itwp == NULL)
        return -6;
    bhInitObjItm();
    if (read_file("player-assets/effect_1.afsentry", &effect_file,
                  &effect_file_size) != 0) {
        snprintf(test_fault, sizeof(test_fault),
                 "SYSTEM.AFS effect entry 1 is missing");
        return -20;
    }
    if (effect_file_size > INT32_MAX)
        return -21;
    effect_file_copy = (unsigned char *)bhGetFreeMemory(
        (int)effect_file_size, 64);
    if (effect_file_copy == NULL)
        return -22;
    memcpy(effect_file_copy, effect_file, effect_file_size);
    if (recvxEngineOriginalTextureAddRange(effect_file_copy,
                                            effect_file_size) != 0)
        return -23;
    sys->memp = effect_file_copy;
    reportf("stage: original effect source %u bytes copy=%08x\n",
            (unsigned int)effect_file_size,
            (unsigned int)(uintptr_t)effect_file_copy);
    recvxEngineOriginalTextureSetMetadataOnly(1);
    bhInitEffect();
    recvxEngineOriginalTextureSetMetadataOnly(0);
    recvxEngineOriginalMemoryObserve();
    reportf("stage: original effects textures=%u metadata-only=%u arena-high-water=%u fault=%s\n",
            (unsigned int)sys->ef_ct,
            recvxEngineOriginalTextureMetadataCount(),
            (unsigned int)recvxEngineOriginalMemoryHighWater(),
            recvxEngineOriginalFault() != NULL ?
                recvxEngineOriginalFault() : "none");
    if (recvxEngineOriginalTextureFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "effect texture: %s",
                 recvxEngineOriginalTextureFault());
        return -24;
    }
    if (recvxEngineOriginalFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "effect initialization: %s",
                 recvxEngineOriginalFault());
        return -25;
    }
    bhInitCamera();
    bhInitPlayer();
    bhInitEnemy();
    engine_cursor = sys->memp;
    if (sys->plmdlp == NULL || sys->plbmtp == NULL || sys->plzmtp == NULL ||
        plp->exp0 == NULL || plp->exp1 == NULL || plp->exp3 == NULL)
        return -7;
    if (read_file("player-assets/player_0_costume_0.afsentry",
                  &player_file, &player_file_size) != 0)
        return -8;
    reportf("stage: player source %u bytes\n",
            (unsigned int)player_file_size);
    if (recvxEngineOriginalTextureAddRange(player_file, player_file_size) != 0)
        return -9;
    sys->memp = player_file;
    bhReadPlayerData();
    reportf("stage: player data read high-water=%u fault=%s\n",
            (unsigned int)recvxEngineOriginalMemoryHighWater(),
            recvxEngineOriginalFault() != NULL ?
                recvxEngineOriginalFault() : "none");
    if (recvxEngineOriginalTextureFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "player texture: %s",
                 recvxEngineOriginalTextureFault());
        return -10;
    }
    sys->memp = engine_cursor;
    sys->mempb = sys->memp;
    keepmem = sys->memp;

    room_file_copy = (unsigned char *)bhGetFreeMemory((int)room_size, 64);
    if (room_file_copy == NULL)
        return -11;
    memcpy(room_file_copy, source, room_size);
    if (recvxEngineOriginalTextureAddRange(room_file_copy, room_size) != 0)
        return -12;
    sys->rdtp = room_file_copy;
    sys->rdtsz = (int)room_size;
    sys->memp = room_file_copy + room_size;
    reportf("stage: room copy=%08x size=%u arena=%08x..%08x\n",
            (unsigned int)(uintptr_t)room_file_copy,
            (unsigned int)room_size, (unsigned int)(uintptr_t)engine_arena,
            (unsigned int)(uintptr_t)sys->endp);
    recvxEngineOriginalTextureSetMetadataOnly(1);
    bhInitReadRDT();
    recvxEngineOriginalViewAspect(&aspect_width, &aspect_height);
    reportf("stage: original room reset aspect=%.3f,%.3f\n",
            aspect_width, aspect_height);
    bhSetRoom();
    reportf("stage: room realized floors=%d floor-table=%08x positions=%d\n",
            rom->flr_n, (unsigned int)(uintptr_t)rom->flrp, rom->pos_n);
    if (sys->error & 1)
        return -13;
    if (recvxEngineOriginalTextureFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "room texture: %s",
                 recvxEngineOriginalTextureFault());
        return -14;
    }
    if ((uintptr_t)sys->memp < (uintptr_t)engine_arena ||
        (uintptr_t)sys->memp > (uintptr_t)sys->endp)
        return -15;
    if (rom->flr_n < 0 || rom->flr_n > 4096 || sys->mflr_n < 0 ||
        sys->mflr_n > 64 ||
        (rom->flr_n != 0 &&
         ((uintptr_t)rom->flrp < (uintptr_t)room_file_copy ||
          (uintptr_t)rom->flrp > (uintptr_t)room_file_copy + room_size ||
          (size_t)rom->flr_n * sizeof(*rom->flrp) >
              (uintptr_t)room_file_copy + room_size - (uintptr_t)rom->flrp))) {
        reportf("FAIL: floor table bounds floor-count=%d floor-table=%08x extra-count=%d\n",
                rom->flr_n, (unsigned int)(uintptr_t)rom->flrp,
                sys->mflr_n);
        snprintf(test_fault, sizeof(test_fault),
                 "original floor table bounds invalid");
        return -19;
    }
    for (int i = 0; i < rom->flr_n; ++i) {
        ATR_WORK *floor = &rom->flrp[i];
        reportf("floor[%d] at=%08x type=%u flags=%02x attr=%08x x=%.2f y=%.2f z=%.2f w=%.2f h=%.2f d=%.2f prm=%u\n",
                i, (unsigned int)(uintptr_t)floor, floor->type, floor->flg,
                floor->attr, floor->px, floor->py, floor->pz, floor->w,
                floor->h, floor->d, floor->prm0);
    }
    reportf("stage: entering original bhSetPlayer\n");
    bhSetPlayer();
    reportf("stage: original bhSetPlayer returned\n");
    if (recvxEngineOriginalFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "player initialization: %s",
                 recvxEngineOriginalFault());
        return -18;
    }
    if (plp->mlwP == NULL || plp->mlwP->objP == NULL ||
        plp->mlwP->owP == NULL || plp->mnwP == NULL)
        return -16;
    reportf("stage: entering original bhFinishRoom\n");
    bhFinishRoom();
#ifdef RECVX_ENGINE_ORIGINAL_CAMERA_TEST
    sys->sp_flg = -1;
    bhCheckCut(1);
    if (recvxEngineOriginalFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "initial original camera cut: %s",
                 recvxEngineOriginalFault());
        return -17;
    }
#endif
    recvxEngineOriginalTextureSetMetadataOnly(0);
    recvxEngineOriginalMemoryObserve();
    reportf("stage: room finished floors=%d floor-table=%08x extra-floors=%d player=%08x model=%08x room=%08x floor=%d arena-high-water=%u\n",
            rom->flr_n, (unsigned int)(uintptr_t)rom->flrp, sys->mflr_n,
            (unsigned int)(uintptr_t)plp->exp0,
            (unsigned int)(uintptr_t)plp->mlwP,
            (unsigned int)(uintptr_t)room_file_copy, (int)plp->flr_no,
            (unsigned int)recvxEngineOriginalMemoryHighWater());
    if (recvxEngineOriginalFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "room initialization: %s",
                 recvxEngineOriginalFault());
        return -17;
    }
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    {
        int translation_result = report_player_translation_data();
        if (translation_result != 0) {
            snprintf(test_fault, sizeof(test_fault),
                     "player translation table invalid (%d)",
                     translation_result);
            return -19;
        }
    }
#endif
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    {
        unsigned int index;
        unsigned int active = 0;
        for (index = 0; index < 512; ++index) {
            if ((eff[index].flg & 0x1) != 0) {
                if (active < 16)
                    reportf("effect[%u] id=%d type=%u flg=%08x stflg=%08x\n",
                            index, eff[index].id, (unsigned int)eff[index].type,
                            (unsigned int)eff[index].flg,
                            (unsigned int)eff[index].stflg);
                ++active;
            }
        }
        reportf("stage: active original effects=%u (first 16 listed)\n", active);
    }
#endif
    return 0;
}

static int prepare_engine_snapshot(void)
{
    unsigned int frame;
    int result;
    if (plp->mtn_no >= player_assets.motion_count)
        return -1;
    frame = plp->frm_no < 0 ? 0u : (unsigned int)plp->frm_no / 65536u;
    engine_snapshot.x = plp->px;
    engine_snapshot.y = plp->py;
    engine_snapshot.z = plp->pz;
    engine_snapshot.yaw = recvxNinjaAngleRadians((RecvxNinjaAngle)plp->ay);
    engine_snapshot.floor = plp->flr_no;
    engine_snapshot.grounded = 1;
    result = recvxRendererUpdateEngineSnapshot(
        &scene, &room_mesh, &room_textures, NULL, &engine_snapshot,
        "RM_0010 original player", plp->mtn_no, frame);
    if (result != 0)
        return result;
#ifdef RECVX_ENGINE_ORIGINAL_CAMERA_TEST
#ifndef RECVX_ENGINE_GAME_CALLBACK_TEST
    bhControlCamera();
    if (recvxEngineOriginalFault() != NULL) {
        snprintf(test_fault, sizeof(test_fault), "original camera update: %s",
                 recvxEngineOriginalFault());
        return -2;
    }
#endif
    result = recvxRendererUseOriginalCamera(
        cam.mtx[0], recvxEngineOriginalViewProjectionDistance());
    if (result != 0) {
        snprintf(test_fault, sizeof(test_fault),
                 "original camera handoff rejected (%d)", result);
        return -3;
    }
#endif
    return 0;
}

#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
static void callback_pad_task(void)
{
    callback_order = callback_order * 10 + 6;
    if (recvxEnginePadAdapterUpdate(&callback_input) != 0) {
        callback_fault = -1;
        return;
    }
    bhSysCallPad();
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    if (callback_scripted_input_active &&
        callback_scripted_input_phase < CALLBACK_SCRIPTED_DIRECTION_COUNT) {
        static const uint32_t expected_pad[CALLBACK_SCRIPTED_DIRECTION_COUNT] = {
            0x42u, 0x08u, 0x21u, 0x04u
        };
        if ((sys->pad_on & expected_pad[callback_scripted_input_phase]) ==
            expected_pad[callback_scripted_input_phase])
            callback_scripted_pad_seen_mask |=
                1u << callback_scripted_input_phase;
    }
#endif
    ++callback_pad_ticks;
    if (recvxEnginePadAdapterFaulted())
        callback_fault = -2;
}

static void callback_game_task(void)
{
    callback_order = callback_order * 10 + 7;
    if (callback_fault != 0)
        return;
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    if (callback_game_ticks == 12) {
        reportf("pre-scripted game: input=%08x axes=%u,%u player mode=%u/%u/%u/%u motion=%u frame=%d dmg=%u wpntp=%u trsz=%08x hash=%08x\n",
                (unsigned int)callback_input.buttons,
                (unsigned int)callback_input.analog_x,
                (unsigned int)callback_input.analog_y,
                (unsigned int)plp->mode0, (unsigned int)plp->mode1,
                (unsigned int)plp->mode2, (unsigned int)plp->mode3,
                (unsigned int)plp->mtn_no, plp->frm_no,
                (unsigned int)((EXP_WORK *)plp->exp0)->dmlvl,
                (unsigned int)((EXP_WORK *)plp->exp0)->wpntp,
                (unsigned int)(uintptr_t)sys->plzmtp,
                (unsigned int)report_hash_bytes(sys->plzmtp,
                                                player_translation_size));
    }
#endif
    if (callback_game_ticks == 0)
        reportf("before game: event slot[0x1a]=%08x slot[0x26]=%08x\n",
                (unsigned int)(uintptr_t)bhScenarioJmpT[0x1a],
                (unsigned int)(uintptr_t)bhScenarioJmpT[0x26]);
    bhSysCallGame();
    ++callback_game_ticks;
    if (callback_game_ticks == 1)
        reportf("after game: event slot[0x1a]=%08x slot[0x26]=%08x\n",
                (unsigned int)(uintptr_t)bhScenarioJmpT[0x1a],
                (unsigned int)(uintptr_t)bhScenarioJmpT[0x26]);
    if (recvxEngineOriginalFault() != NULL)
        callback_fault = -3;
    if (callback_fault == -3)
        reportf("player callback fault state: input=%08x pressed=%08x analog=%u,%u mode=%u/%u/%u/%u motion=%u frame=%d pad=%08x pressed=%08x axes=%d,%d sp=%08x ss=%08x\n",
                (unsigned int)callback_input.buttons,
                (unsigned int)callback_input.pressed,
                (unsigned int)callback_input.analog_x,
                (unsigned int)callback_input.analog_y,
                (unsigned int)plp->mode0, (unsigned int)plp->mode1,
                (unsigned int)plp->mode2, (unsigned int)plp->mode3,
                (unsigned int)plp->mtn_no, plp->frm_no,
                (unsigned int)sys->pad_on, (unsigned int)sys->pad_ps,
                sys->pad_ax, sys->pad_ay,
                (unsigned int)sys->sp_flg, (unsigned int)sys->ss_flg);
}

static void callback_event_task(void)
{
    callback_order = callback_order * 10 + 8;
    if (callback_fault != 0)
        return;
    if (callback_event_ticks == 0)
        reportf("event handlers: slot[0x1a]=%08x slot[0x26]=%08x\n",
                (unsigned int)(uintptr_t)bhScenarioJmpT[0x1a],
                (unsigned int)(uintptr_t)bhScenarioJmpT[0x26]);
    if (recvxEngineOriginalEventTestControl() != 0) {
        callback_fault = -4;
        return;
    }
    ++callback_event_ticks;
}

static void report_event_fault(const char *stage)
{
    const RecvxEventCoreFault *fault = recvxEventCoreGetFault();
    RecvxEventCoreDispatch previous;
    reportf("%s: event core=%s code=%u task=%u event=%u pc=%08x opcode=0x%02x handler=0x%08x engine=%s\n",
            stage, recvxEventCoreFaultName(fault->code),
            (unsigned int)fault->code,
            (unsigned int)fault->task, (unsigned int)fault->event_id,
            (unsigned int)fault->section_pc, (unsigned int)fault->opcode,
            (unsigned int)fault->handler_at_fault,
            recvxEngineOriginalFault() != NULL ?
                recvxEngineOriginalFault() : "none");
    if (recvxEventCoreGetLastDispatch(&previous) == 0)
        reportf("event previous dispatch: pc=%08x opcode=0x%02x handler=0x%08x\n",
                (unsigned int)previous.section_pc,
                (unsigned int)previous.opcode,
                (unsigned int)previous.handler);
    if (sys != NULL && rom != NULL && rom->evtp != NULL && sys->rdtp != NULL &&
        sys->rdtsz > 0 && fault->section_pc != UINT32_MAX) {
        uintptr_t room = (uintptr_t)sys->rdtp;
        uintptr_t section = (uintptr_t)rom->evtp;
        uint32_t room_size = (uint32_t)sys->rdtsz;
        if (room_size >= 8u && section >= room &&
            section - room <= room_size - 8u &&
            fault->section_pc <= room_size -
                (uint32_t)(section - room) - 8u) {
            const unsigned char *pc = (const unsigned char *)section +
                                      fault->section_pc;
            reportf("event fault detail: live-pc=%08x bytes=0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x slot=0x%08x\n",
                    (unsigned int)((uintptr_t)bhScePtr - section),
                    pc[0], pc[1], pc[2], pc[3], pc[4], pc[5], pc[6], pc[7],
                    (unsigned int)(uintptr_t)bhScenarioJmpT[fault->opcode]);
        }
    }
}

#ifdef RECVX_ENGINE_AUTOMATED_INPUT
static int callback_original_pose_hash(uint32_t *hash_out)
{
    const ML_WORK *model;
    uint32_t hash = 2166136261u;
    unsigned int object, component;

    if (hash_out == NULL || plp == NULL || plp->mlwP == NULL)
        return -1;
    model = plp->mlwP;
    if (model->owP == NULL || model->obj_num == 0 ||
        model->obj_num > 65536u)
        return -1;
    for (object = 0; object < model->obj_num; ++object) {
        for (component = 0; component < 16; ++component) {
            uint32_t word;
            memcpy(&word, &model->owP[object].mtx[component], sizeof(word));
            hash = (hash ^ word) * 16777619u;
        }
    }
    *hash_out = hash;
    return 0;
}
#endif

static int prepare_game_callback_test(void)
{
    memset(callback_task_table, 0, sizeof(callback_task_table));
    callback_task_table[6] = callback_pad_task;
    callback_task_table[7] = callback_game_task;
    callback_task_table[8] = callback_event_task;
    memset(&callback_input, 0, sizeof(callback_input));
    callback_input.analog_x = 128;
    callback_input.analog_y = 128;
    callback_order = 0;
    callback_fault = 0;
    callback_pad_ticks = 0;
    callback_game_ticks = 0;
    callback_event_ticks = 0;
    callback_trace_count = 0;
    callback_stall_count = 0;
    callback_task_us_total = 0;
    callback_task_us_max = 0;
    callback_snapshot_us_total = 0;
    callback_snapshot_us_max = 0;
    callback_draw_us_total = 0;
    callback_draw_us_max = 0;
    callback_draw_count = 0;
    event0_wait_pc = 0;
    event0_wait_timer = 0;
    event0_completion_timer = 0;
    event0_previous_pc = 0;
    event0_previous_timer = 0;
    event0_initial_room_flag = 0;
    event0_room_flag_change_tick = 0;
    event0_room_flag_change_value = 0;
    event0_previous_active = 0;
    event0_wait_seen = 0;
    event0_resume_seen = 0;
    event0_completion_seen = 0;
    event0_room_flag_changed = 0;
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    callback_scripted_input_ticks = 0;
    callback_scripted_input_active = 0;
    callback_scripted_input_phase = 0;
    callback_scripted_pad_seen_mask = 0;
    callback_scripted_turn_changed_mask = 0;
    memset(callback_scripted_phase_start_angle, 0,
           sizeof(callback_scripted_phase_start_angle));
    callback_scripted_position_changed = 0;
    callback_scripted_start_x = 0.0f;
    callback_scripted_start_y = 0.0f;
    callback_scripted_start_z = 0.0f;
    callback_scripted_end_x = 0.0f;
    callback_scripted_end_y = 0.0f;
    callback_scripted_end_z = 0.0f;
    callback_pose_skip_start_hash = 0;
    callback_pose_skip_end_hash = 0;
    callback_pose_skip_ticks = 0;
    callback_pose_skip_start_valid = 0;
    callback_pose_skip_matrix_changed = 0;
#endif
    recvxEnginePadAdapterReset();
    recvxEnginePadAdapterResetFault();
    recvxEngineAudioTraceReset();
    recvxPlatformInputInit();

    if (recvxEngineOriginalEventTestBindRoom() != 0 ||
        recvxEngineOriginalEventTestStart() != 0) {
        report_event_fault("FAIL: original event startup");
        return -1;
    }
    pd_port = 0;
    reportf("original dispatch slots: [0x1a]=%08x [0x38]=%08x [0x69]=%08x SCD1=%08x\n",
            (unsigned int)(uintptr_t)bhScenarioJmpT[0x1a],
            (unsigned int)(uintptr_t)bhScenarioJmpT[0x38],
            (unsigned int)(uintptr_t)bhScenarioJmpT[0x69],
            (unsigned int)(uintptr_t)bhScd2Ptr);
    sys->sp_flg = -1;
    bhCheckCut(1);
    if (recvxEngineOriginalFault() != NULL)
        return -2;

    sys->tk_flg = (1u << 6) | (1u << 7) | (1u << 8);
    sys->ts_flg &= ~((1u << 6) | (1u << 7) | (1u << 8));
    event0_initial_room_flag = sys->rm_flg;
    event0_previous_pc = recvxEventCoreTaskPc(0);
    event0_previous_timer = Event_T_timer;
    event0_previous_active = bhEtask[0].status != 0;
    if ((sys->sp_flg & 0x1) == 0 || (sys->sp_flg & 0x2) == 0)
        return -1;
    reportf("stage: event owner active; slots 6/7/8 enabled; pad port %d\n",
            pd_port);
    if (rom->ene_n > 0 && rom->enep != NULL) {
        ETTY_WORK *actor = &rom->enep[0];
        if (actor->wrk_no >= sys->ewk_n) {
            reportf("FAIL: actor placement work index %u is outside %u enemy works\n",
                    (unsigned int)actor->wrk_no, (unsigned int)sys->ewk_n);
            return -1;
        }
        reportf("stage: actor placement id=%u work=%u pos=%.4f,%.4f,%.4f runtime=%u/%u/%u/%u flg=%08x\n",
                (unsigned int)actor->id, (unsigned int)actor->wrk_no,
                actor->px, actor->py, actor->pz,
                (unsigned int)ene[actor->wrk_no].mode0,
                (unsigned int)ene[actor->wrk_no].mode1,
                (unsigned int)ene[actor->wrk_no].mode2,
                (unsigned int)ene[actor->wrk_no].mode3,
                (unsigned int)ene[actor->wrk_no].flg);
    } else {
        reportf("FAIL: RM_0010 scripted actor placement is missing\n");
        return -1;
    }
    reportf("stage: collision walls static=%d dynamic=%d event0 pc=%08x status=%u\n",
            rom->wal_n, sys->mwal_n, event0_previous_pc,
            (unsigned int)bhEtask[0].status);
    reportf("stage: post-SCD0 flags sp=%08x tk=%08x ts=%08x timer=%u\n",
            (unsigned int)sys->sp_flg, (unsigned int)sys->tk_flg,
            (unsigned int)sys->ts_flg, (unsigned int)Event_T_timer);
    reportf("muted audio services: command=%u room-case=%u voice-stop=%u\n",
            recvxEngineAudioTraceGet()->sound_commands,
            recvxEngineAudioTraceGet()->room_sound_cases,
            recvxEngineAudioTraceGet()->voice_stops);
    return 0;
}

static int run_game_callback_tick(void)
{
    uint32_t unsupported;
    uint32_t event_pc;
    uint32_t event_timer;
    int event_active;
    int input_result = recvxPlatformReadInput(&callback_input);

    if (input_result < 0)
        return -1;
    if (input_result == 0) {
        callback_input.buttons = 0;
        callback_input.pressed = 0;
        callback_input.released = 0;
        callback_input.repeat = 0;
        callback_input.analog_x = 128;
        callback_input.analog_y = 128;
    }
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    {
        int scripted_result = recvxEngineTestInputSample(
            callback_game_ticks, &callback_input,
            &callback_scripted_input_phase);
        if (scripted_result < 0) {
            snprintf(test_fault, sizeof(test_fault),
                     "scripted input sampler rejected game tick %u (%d)",
                     (unsigned int)callback_game_ticks, scripted_result);
            return -3;
        }
        callback_scripted_input_active = scripted_result != 0;
    }
    if (callback_scripted_input_active) {
        if (callback_scripted_input_ticks == 0) {
            callback_scripted_start_x = plp->px;
            callback_scripted_start_y = plp->py;
            callback_scripted_start_z = plp->pz;
        }
        if ((callback_scripted_input_ticks %
             CALLBACK_SCRIPTED_DIRECTION_TICKS) == 0)
            callback_scripted_phase_start_angle[callback_scripted_input_phase] =
                plp->ay;
        ++callback_scripted_input_ticks;
    }
#endif
    callback_order = 0;
    unsupported = recvxRunLiveTaskSlots(&sys->tk_flg, &sys->ts_flg,
                                         callback_task_table, 23);
    if (unsupported != 0 || callback_fault != 0 || callback_order != 678 ||
        callback_pad_ticks != callback_game_ticks ||
        callback_event_ticks != callback_game_ticks)
        return -2;
    event_pc = recvxEventCoreTaskPc(0);
    event_timer = Event_T_timer;
    event_active = bhEtask[0].status != 0;
    if (event_active && event0_previous_active &&
        event_pc == event0_previous_pc &&
        event_timer == event0_previous_timer + 1u && !event0_wait_seen) {
        event0_wait_seen = 1;
        event0_wait_pc = event_pc;
        event0_wait_timer = event_timer;
    }
    if (event0_wait_seen && event_active &&
        event_pc != event0_previous_pc)
        event0_resume_seen = 1;
    if (sys->rm_flg != event0_initial_room_flag &&
        !event0_room_flag_changed) {
        event0_room_flag_changed = 1;
        event0_room_flag_change_tick = callback_game_ticks;
        event0_room_flag_change_value = sys->rm_flg;
    }
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    if (callback_scripted_input_active) {
        if ((callback_scripted_input_phase == 1u ||
             callback_scripted_input_phase == 3u) &&
            plp->ay != callback_scripted_phase_start_angle[
                callback_scripted_input_phase])
            callback_scripted_turn_changed_mask |=
                1u << callback_scripted_input_phase;
        if (plp->px != callback_scripted_start_x ||
            plp->py != callback_scripted_start_y ||
            plp->pz != callback_scripted_start_z)
            callback_scripted_position_changed = 1;
        if (callback_scripted_input_ticks == CALLBACK_SCRIPTED_INPUT_TICKS) {
            callback_scripted_end_x = plp->px;
            callback_scripted_end_y = plp->py;
            callback_scripted_end_z = plp->pz;
        }
    }
#endif
    if (event0_resume_seen && event0_previous_active && !event_active &&
        event0_room_flag_changed && !event0_completion_seen) {
        event0_completion_seen = 1;
        event0_completion_timer = event_timer;
    }
    if (callback_trace_count < 720) {
        reportf("trace tick=%u buttons=%08x analog=%u,%u scripted=%u pose_skip=%u pad=%08x pressed=%08x pos=%.4f,%.4f,%.4f floor=%d angle=%d motion=%u state=%u/%u/%u/%u frame=%d cut=%d/%d interaction=%u/%u actor67 id=%u flg=%08x pos=%.4f,%.4f,%.4f mode=%u/%u motion=%u frame=%d walls=%d event_timer=%u task0=%u pc0=%08x roomflag=%08x event0=%d/%d/%d\n",
                (unsigned int)callback_game_ticks,
                (unsigned int)callback_input.buttons,
                (unsigned int)callback_input.analog_x,
                (unsigned int)callback_input.analog_y,
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
                (unsigned int)callback_scripted_input_active,
#else
                0u,
#endif
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
                (unsigned int)(callback_game_ticks >= 16u &&
                               callback_game_ticks < 24u),
#else
                0u,
#endif
                (unsigned int)sys->pad_on, (unsigned int)sys->pad_ps,
                plp->px, plp->py, plp->pz, (int)plp->flr_no,
                (int)plp->ay, (unsigned int)plp->mtn_no,
                (unsigned int)plp->mode0, (unsigned int)plp->mode1,
                (unsigned int)plp->mode2, (unsigned int)plp->mode3,
                plp->frm_no,
                cam.ncut, cam.camver, (unsigned int)sys->etc_idx,
                (unsigned int)sys->flr_idx, (unsigned int)ene[0].id,
                (unsigned int)ene[0].flg, ene[0].px, ene[0].py,
                ene[0].pz, (unsigned int)ene[0].mode0,
                (unsigned int)ene[0].mode2, (unsigned int)ene[0].mtn_no,
                ene[0].frm_no, sys->mwal_n, (unsigned int)event_timer,
                (unsigned int)bhEtask[0].status,
                (unsigned int)event_pc, (unsigned int)sys->rm_flg,
                event0_wait_seen, event0_resume_seen,
                event0_completion_seen);
        ++callback_trace_count;
    }
    event0_previous_pc = event_pc;
    event0_previous_timer = event_timer;
    event0_previous_active = event_active;
    if (callback_trace_count == 1 || callback_trace_count == 120) {
        const RecvxEngineAudioTrace *audio = recvxEngineAudioTraceGet();
        reportf("muted audio trace tick=%u command=%u room-case=%u last-case=%d voice-stop=%u last-fade=%d\n",
                (unsigned int)callback_game_ticks, audio->sound_commands,
                audio->room_sound_cases, audio->last_room_sound_case,
                audio->voice_stops, audio->last_voice_fade);
    }
    return 0;
}
#endif

static int should_exit(void)
{
    SceCtrlData input;
    sceCtrlReadBufferPositive(&input, 1);
    return (input.Buttons & PSP_CTRL_START) != 0;
}

static void wait_for_start(void)
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    while (!should_exit())
        sceDisplayWaitVblankStart();
}

int main(int argc, char **argv)
{
    int result;
    char overlay[256];
    int renderer_ready = 0;
    int renderer_frame_ready = 0;

    pspDebugScreenInit();
    if (argc > 0 && argv != NULL && argv[0] != NULL) {
        const char *last_slash = strrchr(argv[0], '/');
        if (last_slash != NULL && last_slash - argv[0] < 500)
            snprintf(data_directory, sizeof(data_directory), "%.*s/data",
                     (int)(last_slash - argv[0]), argv[0]);
    }
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    reportf("Package 4 original player, actor and event test\ndata=%s\n",
            data_directory);
#else
    reportf("Package 4a original player test\ndata=%s\n",
            data_directory);
#endif
    recvxEngineOriginalTextureReset();
    result = load_view_assets();
    if (result != 0) {
        snprintf(test_fault, sizeof(test_fault), "player render assets %d", result);
        goto done;
    }
    result = prepare_room_view();
    if (result != 0) {
        snprintf(test_fault, sizeof(test_fault), "room view setup %d", result);
        goto done;
    }
    if (recvxRendererStart(&scene, &room_mesh, &room_textures,
                           &engine_snapshot, NULL, &player_assets,
                           &player_mesh, &player_textures,
                           player_source_bytes) != 0) {
        snprintf(test_fault, sizeof(test_fault), "GU renderer initialization");
        result = -17;
        goto done;
    }
    renderer_ready = 1;
    result = prepare_original_room();
    if (result != 0) {
        if (test_fault[0] == '\0')
            snprintf(test_fault, sizeof(test_fault), "original init stage %d", result);
        goto done;
    }
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    result = prepare_game_callback_test();
    if (result != 0) {
        report_event_fault("FAIL: original callback setup");
        snprintf(test_fault, sizeof(test_fault),
                 "original callback setup failed (%d), pad=%u game=%u dependency=%s",
                 result, (unsigned int)callback_pad_ticks,
                 (unsigned int)callback_game_ticks,
                 recvxEngineOriginalFault() != NULL ?
                     recvxEngineOriginalFault() : "none");
        result = -19;
        goto done;
    }
    callback_deferred_report =
        (char *)malloc(CALLBACK_DEFERRED_REPORT_CAPACITY);
    if (callback_deferred_report == NULL) {
        snprintf(test_fault, sizeof(test_fault),
                 "callback trace buffer allocation failed");
        result = -34;
        goto done;
    }
#endif
#ifndef RECVX_ENGINE_GAME_CALLBACK_TEST
    result = prepare_engine_snapshot();
    if (result != 0) {
        if (test_fault[0] == '\0')
            snprintf(test_fault, sizeof(test_fault),
                     "unsupported original motion %u frame %d",
                     (unsigned int)plp->mtn_no, plp->frm_no);
        result = -18;
        goto done;
    }
    renderer_frame_ready = 1;
#endif
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    reportf("PASS: original room/player/SCD0 setup; waiting for paced slots 6/7/8\n");
    reportf("initial trace pos=(%.4f,%.4f,%.4f) floor=%d angle=%d motion=%u frame=%d camera=%d/%d\n",
            plp->px, plp->py, plp->pz, (int)plp->flr_no, (int)plp->ay,
            (unsigned int)plp->mtn_no, plp->frm_no, cam.ncut, cam.camver);
    snprintf(overlay, sizeof(overlay),
             "Original pad slot 6 -> game slot 7, paced at 30 Hz\n"
             "Position %.2f %.2f floor %d motion %u frame %d\n"
             "Original actor 67 and event slot 8 active\nSTART exits",
             plp->px, plp->pz, (int)plp->flr_no,
             (unsigned int)plp->mtn_no, plp->frm_no / 65536);
#else
#ifdef RECVX_ENGINE_ORIGINAL_CAMERA_TEST
    reportf("PASS: original room/player setup and camera matrix handoff; no gameplay tick\n");
    reportf("matrix pop underflows: %u first call=%p\n",
            recvxEngineOriginalMatrixUnderflowCount(),
            recvxEngineOriginalMatrixFirstUnderflow());
    reportf("camera cut=%d variant=%d sp_flg=%08x pos=(%.3f,%.3f,%.3f) angle=(%d,%d,%d) lens=%.3f perspective=%u distance=%.3f\n",
            cam.ncut, cam.camver, (unsigned int)sys->sp_flg,
            cam.px, cam.py, cam.pz, cam.ax, cam.ay, cam.az, cam.ln,
            (unsigned int)cam.pers,
            recvxEngineOriginalViewProjectionDistance());
    reportf("view matrix storage: %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f\n",
            cam.mtx[0][0], cam.mtx[0][1], cam.mtx[0][2], cam.mtx[0][3],
            cam.mtx[0][4], cam.mtx[0][5], cam.mtx[0][6], cam.mtx[0][7],
            cam.mtx[0][8], cam.mtx[0][9], cam.mtx[0][10], cam.mtx[0][11],
            cam.mtx[0][12], cam.mtx[0][13], cam.mtx[0][14], cam.mtx[0][15]);
    snprintf(overlay, sizeof(overlay),
             "Original camera matrix handoff; no gameplay tick\n"
             "Room actor work %u; original motion %u frame %d\n"
             "Matrix pop false returns %u (original behavior)\n"
             "Engine arena high-water %u KiB; START exits",
             (unsigned int)room_owner.active_enemy_count,
             (unsigned int)plp->mtn_no,
             plp->frm_no, recvxEngineOriginalMatrixUnderflowCount(),
             (unsigned int)(recvxEngineOriginalMemoryHighWater() / 1024));
#else
    reportf("PASS: original room and player setup; no gameplay tick\n");
    snprintf(overlay, sizeof(overlay),
             "4a stationary original player/model/room\n"
             "Room actor work %u; original motion %u frame %d\n"
             "Engine arena high-water %u KiB; START exits",
             (unsigned int)room_owner.active_enemy_count,
             (unsigned int)plp->mtn_no,
             plp->frm_no, (unsigned int)(recvxEngineOriginalMemoryHighWater() / 1024));
#endif
#endif
    recvxRendererSetOverlay(overlay);
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    recvxEnginePacerInit(&callback_pacer, sceDisplayGetVcount());
    while (!should_exit()) {
        uint32_t elapsed_vblanks = 0;
        uint32_t elapsed_us;
        uint64_t phase_start_us;
        int engine_tick = 0;
        int skip_renderer = 0;
        enum RecvxEnginePacerResult pace = recvxEnginePacerAdvance(
            &callback_pacer, sceDisplayGetVcount(), &elapsed_vblanks);

        recvxOriginalVsyncTimeAdvance(elapsed_vblanks);
        if (pace == RECVX_ENGINE_PACER_STALL) {
            ++callback_stall_count;
        } else if (pace == RECVX_ENGINE_PACER_TICK) {
            int callback_result;

            phase_start_us = sceKernelGetSystemTimeWide();
            callback_result = run_game_callback_tick();
            elapsed_us = (uint32_t)(sceKernelGetSystemTimeWide() -
                                    phase_start_us);
            callback_task_us_total += elapsed_us;
            if (elapsed_us > callback_task_us_max)
                callback_task_us_max = elapsed_us;
            if (callback_result != 0) {
                if (test_fault[0] == '\0')
                    snprintf(test_fault, sizeof(test_fault),
                             "original callback tick failed, pad=%u game=%u event=%u dependency=%s actor_gate=%u",
                             (unsigned int)callback_pad_ticks,
                             (unsigned int)callback_game_ticks,
                             (unsigned int)callback_event_ticks,
                             recvxEngineOriginalFault() != NULL ?
                                 recvxEngineOriginalFault() : "none",
                             (unsigned int)((sys->sp_flg & 0x2) != 0));
                reportf("FAIL: %s\n", test_fault);
                report_event_fault("FAIL: original callback tick");
                result = -20;
                break;
            }
            {
                int snapshot_result;

                phase_start_us = sceKernelGetSystemTimeWide();
                snapshot_result = prepare_engine_snapshot();
                elapsed_us = (uint32_t)(sceKernelGetSystemTimeWide() -
                                        phase_start_us);
                callback_snapshot_us_total += elapsed_us;
                if (elapsed_us > callback_snapshot_us_max)
                    callback_snapshot_us_max = elapsed_us;
                if (snapshot_result != 0) {
                    snprintf(test_fault, sizeof(test_fault),
                             "player snapshot rejected (%d), motion=%u/%u frame=%d floor=%d engine=%s",
                             snapshot_result, (unsigned int)plp->mtn_no,
                             (unsigned int)player_assets.motion_count,
                             plp->frm_no, (int)plp->flr_no,
                             recvxEngineOriginalFault() != NULL ?
                                 recvxEngineOriginalFault() : "none");
                    reportf("FAIL: %s\n", test_fault);
                    result = -21;
                    break;
                }
            }
            renderer_frame_ready = 1;
            engine_tick = 1;
        }
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
        if (engine_tick && callback_game_ticks == 16u) {
            if (callback_original_pose_hash(&callback_pose_skip_start_hash) != 0) {
                snprintf(test_fault, sizeof(test_fault),
                         "original player pose matrices are unavailable before draw skip");
                reportf("FAIL: %s\n", test_fault);
                result = -29;
                break;
            }
            callback_pose_skip_start_valid = 1;
        }
        if (engine_tick && callback_game_ticks >= 16u &&
            callback_game_ticks < 24u)
            ++callback_pose_skip_ticks;
        if (engine_tick && callback_game_ticks == 24u) {
            if (callback_original_pose_hash(&callback_pose_skip_end_hash) != 0) {
                snprintf(test_fault, sizeof(test_fault),
                         "original player pose matrices are unavailable after draw skip");
                reportf("FAIL: %s\n", test_fault);
                result = -30;
                break;
            }
            callback_pose_skip_matrix_changed =
                callback_pose_skip_start_hash != callback_pose_skip_end_hash;
        }
        skip_renderer = callback_game_ticks >= 16u &&
                        callback_game_ticks < 24u;
#endif
        snprintf(overlay, sizeof(overlay),
                 "Original pad slot 6 -> game slot 7, paced at 30 Hz\n"
                 "Position %.2f %.2f floor %d motion %u frame %d\n"
                 "Tick %u stalls %u; event timer %u\nSTART exits",
                 plp->px, plp->pz, (int)plp->flr_no,
                 (unsigned int)plp->mtn_no, plp->frm_no / 65536,
                 (unsigned int)callback_game_ticks,
                 (unsigned int)callback_stall_count,
                 (unsigned int)Event_T_timer);
        if (renderer_frame_ready && !skip_renderer) {
            recvxRendererSetOverlay(overlay);
            phase_start_us = sceKernelGetSystemTimeWide();
            recvxRendererDraw(1);
            elapsed_us = (uint32_t)(sceKernelGetSystemTimeWide() -
                                    phase_start_us);
            callback_draw_us_total += elapsed_us;
            if (elapsed_us > callback_draw_us_max)
                callback_draw_us_max = elapsed_us;
            ++callback_draw_count;
        }
        if (engine_tick)
            recvxOriginalVsyncCallbackCounters();
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
        if (callback_scripted_input_ticks == CALLBACK_SCRIPTED_INPUT_TICKS &&
            event0_completion_seen)
            break;
#endif
    }
    if (result == 0 && callback_report_overflow) {
        snprintf(test_fault, sizeof(test_fault),
                 "callback trace exceeded its 64 KiB buffer");
        result = -35;
    }
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    reportf("scripted input: up/left/down/right ticks=%u pads=%x/0xf turns=%x/0xa position=%s start=(%.4f,%.4f,%.4f) end=(%.4f,%.4f,%.4f)\n",
            (unsigned int)callback_scripted_input_ticks,
            (unsigned int)callback_scripted_pad_seen_mask,
            (unsigned int)callback_scripted_turn_changed_mask,
            callback_scripted_position_changed ? "changed" : "unchanged",
            callback_scripted_start_x, callback_scripted_start_y,
            callback_scripted_start_z, callback_scripted_end_x,
            callback_scripted_end_y, callback_scripted_end_z);
    reportf("GU pose skip: engine ticks=%u matrix=%08x->%08x changed=%u\n",
            (unsigned int)callback_pose_skip_ticks,
            (unsigned int)callback_pose_skip_start_hash,
            (unsigned int)callback_pose_skip_end_hash,
            (unsigned int)callback_pose_skip_matrix_changed);
    if (result == 0 && callback_pose_skip_ticks != 8u) {
        snprintf(test_fault, sizeof(test_fault),
                 "GU rendering was skipped for %u engine ticks instead of 8",
                 (unsigned int)callback_pose_skip_ticks);
        result = -31;
    } else if (result == 0 && !callback_pose_skip_start_valid) {
        snprintf(test_fault, sizeof(test_fault),
                 "original player pose matrix snapshot was not captured");
        result = -32;
    } else if (result == 0 && !callback_pose_skip_matrix_changed) {
        snprintf(test_fault, sizeof(test_fault),
                 "original player pose matrices did not change while GU draw was skipped");
        result = -33;
    } else if (result == 0 &&
               callback_scripted_input_ticks != CALLBACK_SCRIPTED_INPUT_TICKS) {
        snprintf(test_fault, sizeof(test_fault),
                 "scripted four-direction input ran for %u ticks",
                 (unsigned int)callback_scripted_input_ticks);
        result = -26;
    } else if (result == 0 && callback_scripted_pad_seen_mask != 0x0Fu) {
        snprintf(test_fault, sizeof(test_fault),
                 "scripted directions reached original pad phases 0x%x/0xf",
                 (unsigned int)callback_scripted_pad_seen_mask);
        result = -27;
    } else if (result == 0 &&
               (callback_scripted_turn_changed_mask & 0x0Au) != 0x0Au) {
        snprintf(test_fault, sizeof(test_fault),
                 "original player angle did not respond to both turn inputs (0x%x/0xa)",
                 (unsigned int)callback_scripted_turn_changed_mask);
        result = -34;
    } else if (result == 0 && !callback_scripted_position_changed) {
        snprintf(test_fault, sizeof(test_fault),
                 "original player position did not respond to four-direction input");
        result = -28;
    }
#endif
    if (result == 0 && !event0_wait_seen) {
        snprintf(test_fault, sizeof(test_fault),
                 "event 0 never yielded at a persistent task PC");
        result = -22;
    } else if (result == 0 && !event0_resume_seen) {
        snprintf(test_fault, sizeof(test_fault),
                 "event 0 did not resume after its persistent yield");
        result = -23;
    } else if (result == 0 && !event0_completion_seen) {
        snprintf(test_fault, sizeof(test_fault),
                 "event 0 did not end after its yield/resume path");
        result = -24;
    } else if (result == 0 && !event0_room_flag_changed) {
        snprintf(test_fault, sizeof(test_fault),
                 "event 0 did not change the room flag");
        result = -25;
    }
    if (result == 0) {
        reportf("PASS: event 0 yield/resume/end and room flag transition: wait pc=%08x timer=%u completion timer=%u initial=%08x changed at tick %u to %08x final=%08x\n",
                (unsigned int)event0_wait_pc,
                (unsigned int)event0_wait_timer,
                (unsigned int)event0_completion_timer,
                (unsigned int)event0_initial_room_flag,
                (unsigned int)event0_room_flag_change_tick,
                (unsigned int)event0_room_flag_change_value,
                (unsigned int)sys->rm_flg);
        reportf("ticks=%u pad=%u game=%u event=%u traced=%u stalls=%u final pos=(%.4f,%.4f,%.4f) floor=%d angle=%d motion=%u frame=%d camera=%d/%d event_timer=%u roomflag=%08x actor67 id=%u pos=(%.4f,%.4f,%.4f) motion=%u frame=%d walls=%d\n",
                (unsigned int)callback_game_ticks,
                (unsigned int)callback_pad_ticks,
                (unsigned int)callback_game_ticks,
                (unsigned int)callback_event_ticks,
                (unsigned int)callback_trace_count,
                (unsigned int)callback_stall_count,
                plp->px, plp->py, plp->pz,
                (int)plp->flr_no, (int)plp->ay, (unsigned int)plp->mtn_no,
                plp->frm_no, cam.ncut, cam.camver, (unsigned int)Event_T_timer,
                (unsigned int)sys->rm_flg, (unsigned int)ene[0].id,
                ene[0].px, ene[0].py,
                ene[0].pz, (unsigned int)ene[0].mtn_no, ene[0].frm_no,
                sys->mwal_n);
        reportf("profile callback-us=%u/%u snapshot-us=%u/%u draw-us=%u/%u draws=%u\n",
                callback_game_ticks != 0 ?
                    callback_task_us_total / callback_game_ticks : 0,
                (unsigned int)callback_task_us_max,
                callback_game_ticks != 0 ?
                    callback_snapshot_us_total / callback_game_ticks : 0,
                (unsigned int)callback_snapshot_us_max,
                callback_draw_count != 0 ?
                    callback_draw_us_total / callback_draw_count : 0,
                (unsigned int)callback_draw_us_max,
                (unsigned int)callback_draw_count);
    }
    if (result == 0) {
        const RecvxEngineAudioTrace *audio = recvxEngineAudioTraceGet();
        reportf("muted audio final: command=%u room-case=%u last-case=%d voice-stop=%u last-fade=%d\n",
                audio->sound_commands, audio->room_sound_cases,
                audio->last_room_sound_case, audio->voice_stops,
                audio->last_voice_fade);
    }
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    if (result != 0)
        report_callback_trace_dump();
#endif
#ifdef RECVX_ENGINE_AUTOMATED_INPUT
    if (result == 0) {
        snprintf(overlay, sizeof(overlay),
                 "PACKAGE 4C PASS\n"
                 "Original analog-forward input moved Claire\n"
                 "CPU pose updated during GU draw skip\n"
                 "Event 0 yield/resume/end and room flag passed\n"
                 "START exits");
        recvxRendererSetOverlay(overlay);
        while (!should_exit())
            recvxRendererDraw(1);
    }
#endif
#else
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    while (!should_exit())
        recvxRendererDraw(1);
    result = 0;
#endif

done:
    if (result != 0)
        reportf("FAIL: result=%d reason=%s\n", result,
                test_fault[0] != '\0' ? test_fault : "unknown setup error");
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    report_callback_trace_dump();
#endif
    if (test_report != NULL) {
        fclose(test_report);
        test_report = NULL;
    }
    if (result != 0) {
        if (renderer_ready && renderer_frame_ready) {
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
            snprintf(overlay, sizeof(overlay),
                     "PACKAGE 4C FAILED\n%s\nCallback stopped at fault\nSTART exits",
                     test_fault[0] != '\0' ? test_fault : "unknown setup error");
#else
            snprintf(overlay, sizeof(overlay),
                     "PACKAGE 4A FAILED\n%s\nNo engine player tick ran\nSTART exits",
                     test_fault[0] != '\0' ? test_fault : "unknown setup error");
#endif
            recvxRendererSetOverlay(overlay);
            sceCtrlSetSamplingCycle(0);
            sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
            while (!should_exit())
                recvxRendererDraw(1);
        } else {
            pspDebugScreenClear();
            pspDebugScreenPrintf("RECVX Package 4 initialization\n\nFAIL: %s\n",
                                 test_fault[0] != '\0' ? test_fault : "unknown setup error");
            pspDebugScreenPrintf("No engine player tick was run\nPress START to exit");
            wait_for_start();
        }
    }
    recvxRendererStop();
    recvxEngineOriginalTextureReset();
    recvxSceneFree(&scene);
    recvxMeshFree(&room_mesh);
    recvxMeshFree(&player_mesh);
    recvxTexturesFree(&room_textures);
    recvxTexturesFree(&player_textures);
    free(player_file);
    free(player_models);
    free(player_motions);
    free(player_extra);
    free(effect_file);
    free(engine_arena_raw);
    if (room_view.owner != NULL && room_view.generation != 0)
        recvxEngineRoomViewUnpin(&room_view);
    if (room_owner.pins == 0 && room_owner.resident)
        recvxEngineRoomOwnerClose(&room_owner);
    recvxEngineEventRuntimeReset();
    sceKernelExitGame();
    return result;
}
