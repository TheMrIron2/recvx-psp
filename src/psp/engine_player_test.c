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
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/objitm.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/room.h"
#include "ps2/veronica/prog/system.h"
#include "psp/engine_pad_adapter.h"
#include "psp/engine_event_runtime.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_memory.h"
#include "psp/engine_original_texture.h"
#include "psp/engine_original_view.h"
#include "psp/engine_room_owner.h"
#include "psp/live_task_dispatch.h"
#include "psp/mesh.h"
#include "psp/ninja_math.h"
#include "psp/platform.h"
#include "psp/player.h"
#include "psp/renderer.h"
#include "psp/scene.h"
#include "psp/texture.h"

PSP_MODULE_INFO("RECVX PACKAGE 4A TEST", PSP_MODULE_USER, 0, 6);
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
static int callback_fault;
static uint32_t callback_order;
static uint32_t callback_pad_ticks;
static uint32_t callback_game_ticks;
#endif

static void reportf(const char *format, ...)
{
    va_list args;
    char path[640];

    if (test_report == NULL) {
        const char *report_name;
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
        report_name = "package4b-report.txt";
#else
        report_name = "package4a-report.txt";
#endif
        if (snprintf(path, sizeof(path), "%s/%s", data_directory,
                     report_name) >= (int)sizeof(path))
            return;
        remove(path);
        test_report = fopen(path, "w");
        if (test_report == NULL)
            return;
    }
    va_start(args, format);
    vfprintf(test_report, format, args);
    va_end(args);
    fflush(test_report);
}

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
    engine_cursor = sys->memp;
    bhInitCamera();
    bhInitPlayer();
    bhInitEnemy();
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
    ++callback_pad_ticks;
    if (recvxEnginePadAdapterFaulted())
        callback_fault = -2;
}

static void callback_game_task(void)
{
    callback_order = callback_order * 10 + 7;
    if (callback_fault != 0)
        return;
    bhSysCallGame();
    ++callback_game_ticks;
    if (recvxEngineOriginalFault() != NULL)
        callback_fault = -3;
}

static int prepare_game_callback_test(void)
{
    memset(callback_task_table, 0, sizeof(callback_task_table));
    callback_task_table[6] = callback_pad_task;
    callback_task_table[7] = callback_game_task;
    memset(&callback_input, 0, sizeof(callback_input));
    callback_input.analog_x = 128;
    callback_input.analog_y = 128;
    callback_order = 0;
    callback_fault = 0;
    callback_pad_ticks = 0;
    callback_game_ticks = 0;
    recvxEnginePadAdapterReset();
    recvxEnginePadAdapterResetFault();
    recvxPlatformInputInit();

    // The integration checkpoint runs original pad/game slots before SCD0 admission
    sys->tk_flg = (1u << 6) | (1u << 7);
    sys->ts_flg &= ~((1u << 6) | (1u << 7));
    sys->ts_flg |= 1u << 8;
    sys->sp_flg &= ~0x2;
    if ((sys->sp_flg & 0x1) == 0 || (sys->sp_flg & 0x2) != 0)
        return -1;
    reportf("stage: original dispatcher slots 6/7 active; event slot 8 paused; actor admission held before SCD0\n");
    reportf("stage: room flags sp=%08x tk=%08x ts=%08x\n",
            (unsigned int)sys->sp_flg, (unsigned int)sys->tk_flg,
            (unsigned int)sys->ts_flg);
    return 0;
}

static int run_game_callback_tick(void)
{
    uint32_t unsupported;
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
    callback_order = 0;
    unsupported = recvxRunLiveTaskSlots(&sys->tk_flg, &sys->ts_flg,
                                         callback_task_table, 23);
    if (unsupported != 0 || callback_fault != 0 || callback_order != 67 ||
        callback_pad_ticks != callback_game_ticks)
        return -2;
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
    reportf("Package 4b original game callback test\ndata=%s\n",
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
    if (result == 0)
        result = run_game_callback_tick();
    if (result != 0) {
        snprintf(test_fault, sizeof(test_fault),
                 "first original callback tick failed (%d), pad=%u game=%u dependency=%s",
                 result, (unsigned int)callback_pad_ticks,
                 (unsigned int)callback_game_ticks,
                 recvxEngineOriginalFault() != NULL ?
                     recvxEngineOriginalFault() : "none");
        result = -19;
        goto done;
    }
#endif
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
#ifdef RECVX_ENGINE_GAME_CALLBACK_TEST
    reportf("PASS: original dispatcher pad slot 6 then bhSysCallGame slot 7; event slot 8 and actor 67 held pending SCD0\n");
    reportf("first trace pos=(%.4f,%.4f,%.4f) floor=%d angle=%d motion=%u frame=%d camera=%d/%d\n",
            plp->px, plp->py, plp->pz, (int)plp->flr_no, (int)plp->ay,
            (unsigned int)plp->mtn_no, plp->frm_no, cam.ncut, cam.camver);
    snprintf(overlay, sizeof(overlay),
             "Package 4b original game callback\n"
             "Pad slot 6 -> game slot 7 -> passive GU\n"
             "Position %.2f %.2f floor %d motion %u frame %d\n"
             "Actor 67 and event 8 held before SCD0; START exits",
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
    while (!should_exit()) {
        if (run_game_callback_tick() != 0) {
            snprintf(test_fault, sizeof(test_fault),
                     "original callback tick failed, pad=%u game=%u dependency=%s actor_gate=%u",
                     (unsigned int)callback_pad_ticks,
                     (unsigned int)callback_game_ticks,
                     recvxEngineOriginalFault() != NULL ?
                         recvxEngineOriginalFault() : "none",
                     (unsigned int)((sys->sp_flg & 0x2) != 0));
            result = -20;
            break;
        }
        if (prepare_engine_snapshot() != 0) {
            result = -21;
            break;
        }
        snprintf(overlay, sizeof(overlay),
                 "Package 4b original game callback\n"
                 "Pad slot 6 -> game slot 7 -> passive GU\n"
                 "Position %.2f %.2f floor %d motion %u frame %d\n"
                 "Actor 67 and event 8 held before SCD0; START exits",
                 plp->px, plp->pz, (int)plp->flr_no,
                 (unsigned int)plp->mtn_no, plp->frm_no / 65536);
        recvxRendererSetOverlay(overlay);
        recvxRendererDraw(1);
    }
    if (result == 0)
        reportf("ticks=%u pad=%u game=%u final pos=(%.4f,%.4f,%.4f) floor=%d angle=%d motion=%u frame=%d camera=%d/%d\n",
                (unsigned int)callback_game_ticks,
                (unsigned int)callback_pad_ticks,
                (unsigned int)callback_game_ticks, plp->px, plp->py, plp->pz,
                (int)plp->flr_no, (int)plp->ay, (unsigned int)plp->mtn_no,
                plp->frm_no, cam.ncut, cam.camver);
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
    if (test_report != NULL) {
        fclose(test_report);
        test_report = NULL;
    }
    if (result != 0) {
        if (renderer_ready && renderer_frame_ready) {
            snprintf(overlay, sizeof(overlay),
                     "PACKAGE 4A FAILED\n%s\nNo engine player tick ran\nSTART exits",
                     test_fault[0] != '\0' ? test_fault : "unknown setup error");
            recvxRendererSetOverlay(overlay);
            sceCtrlSetSamplingCycle(0);
            sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
            while (!should_exit())
                recvxRendererDraw(1);
        } else {
            pspDebugScreenClear();
            pspDebugScreenPrintf("RECVX Package 4a initialization\n\nFAIL: %s\n",
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
