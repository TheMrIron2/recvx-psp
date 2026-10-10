#include <pspdebug.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/room.h"
#include "psp/file.h"
#include "psp/platform.h"
#include "psp/renderer.h"
#include "psp/selftest.h"
#include "psp/player_asset.h"
#include "psp/event_data.h"
#include "psp/game_tasks.h"
#include "psp/input.h"
#include "psp/legacy_runtime.h"
#include "psp/services.h"

PSP_MODULE_INFO("RECVX PSP", PSP_MODULE_USER, 0, 6);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(18 * 1024);

static RecvxScene scene;
static RecvxRoomMesh mesh;
static RecvxTextureSet textures;
static RecvxPlayerRoom player_room;
static RecvxEventData event_data;
static RecvxPlayer player;
static RecvxRoomMesh player_mesh;
static RecvxPlayerAssets player_assets;
static RecvxGameTaskDispatcher game_tasks;
static RecvxInputState game_task_input;
static uint32_t unsupported_task_mask;
static int game_task_playing;
static unsigned char *player_model_data, *player_motion_data;
static int player_asset_status = 1;
static int player_mesh_status = 1;
static size_t player_source_bytes;
static char room_name[RECVX_ROOM_NAME_SIZE];
static char room_catalog[RECVX_MAX_ROOM_CATALOG][RECVX_ROOM_NAME_SIZE];
static size_t room_catalog_count, current_room;
static unsigned int room_spawn;
static int room_catalog_status;
static int player_ready;
static int event_data_status = 1;
static int clock_set_result;
static int legacy_init_status = -1;
static int legacy_scenario_attempted;
static RecvxLegacyScriptResult legacy_scenario_result;
static int door_exit_lock;
static char room_note[64];
static uint64_t player_asset_load_us;
static uint64_t room_file_load_us, room_scene_build_us, room_mesh_build_us;
static uint64_t room_texture_build_us, room_collision_load_us, room_load_total_us;
static uint64_t door_transition_started_us, last_door_transition_us;
static SceSize room_load_min_free_bytes;
static int door_transition_timing;

static void render_game_task(void *context)
{
    (void)context;
    recvxRendererFrame(&scene, &mesh, &textures,
                       game_task_playing ? &player : NULL,
                       game_task_playing ? &player_room : NULL,
                       room_name, &game_task_input);
}

static void init_original_system_task(void *context)
{
    int *status = context;
    *status = recvxLegacySystemInit();
}

static void show_unsupported_tasks(void)
{
    unsigned int slot;
    if (unsupported_task_mask == 0)
        return;
    pspDebugScreenPrintf("Unsupported active task slots:");
    for (slot = 0; slot < RECVX_GAME_TASK_COUNT; ++slot) {
        if (unsupported_task_mask & RECVX_GAME_TASK_MASK(slot))
            pspDebugScreenPrintf("\n%u: %s", slot, recvxGameTaskName(slot));
    }
    pspDebugScreenPrintf("\n\n");
    unsupported_task_mask = 0;
}

static void show_unsupported_services(void)
{
    unsigned int service;
    for (service = 0; service < RECVX_UNSUPPORTED_SERVICE_COUNT; ++service) {
        const RecvxUnsupportedServiceRecord *record =
            recvxUnsupportedServiceGet((enum RecvxUnsupportedServiceId)service);
        if (record->calls != 0)
            pspDebugScreenPrintf("Unsupported %s: %u calls, last %s\n",
                                 recvxUnsupportedServiceName(
                                     (enum RecvxUnsupportedServiceId)service),
                                 (unsigned int)record->calls,
                                 record->last_caller != NULL ?
                                     record->last_caller : "unknown caller");
    }
}

static void sample_room_load_heap(void)
{
    SceSize free_bytes = sceKernelTotalFreeMemSize();
    if (free_bytes < room_load_min_free_bytes)
        room_load_min_free_bytes = free_bytes;
}

static int read_player_asset(const char *data_directory, const char *name,
                             unsigned char **bytes, size_t *size)
{
    char path[512];
    RecvxFile file = {0};
    size_t length, read_count;
    if (snprintf(path, sizeof(path), "%s/player-assets/%s",
                 data_directory, name) >= (int)sizeof(path))
        return -1;
    if (recvxFileOpenRead(&file, path) != 0)
        return 1;
    if (recvxFileSize(&file, &length) != 0 || length == 0) {
        recvxFileClose(&file);
        return -1;
    }
    *bytes = malloc(length);
    if (*bytes == NULL) {
        recvxFileClose(&file);
        return -1;
    }
    if (recvxFileRead(&file, *bytes, length, &read_count) != 0 ||
        read_count != length) {
        free(*bytes);
        *bytes = NULL;
        recvxFileClose(&file);
        return -1;
    }
    recvxFileClose(&file);
    *size = length;
    return 0;
}

static void load_player_assets(const char *data_directory)
{
    uint64_t started = recvxPlatformTimeUs();
    size_t model_size = 0, motion_size = 0;
    int result = read_player_asset(data_directory,
                                   "player_0_costume_0_models.bin",
                                   &player_model_data, &model_size);
    if (result == 0)
        result = read_player_asset(data_directory,
                                   "player_0_costume_0_motions.bin",
                                   &player_motion_data, &motion_size);
    if (result == 0)
        result = recvxPlayerAssetsParse(player_model_data, model_size,
                                       player_motion_data, motion_size,
                                       &player_assets);
    if (result == 0) {
        if (player_assets.model_count == 0)
            player_mesh_status = -1;
        else {
            player_mesh_status = recvxBuildPlayerMesh(&player_assets.models[0],
                                                       &player_mesh);
            if (player_mesh_status != 0)
                recvxMeshFree(&player_mesh);
        }
    } else {
        player_mesh_status = result;
    }
    if (result != 0) {
        free(player_model_data);
        free(player_motion_data);
        player_model_data = NULL;
        player_motion_data = NULL;
        memset(&player_assets, 0, sizeof(player_assets));
    } else {
        player_source_bytes = player_assets.model_stream_size +
                              player_assets.motion_stream_size;
    }
    player_asset_status = result;
    player_asset_load_us = recvxPlatformTimeUs() - started;
}

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    sceKernelExitGame();
    return 0;
}

static void show_checks(const char *data_directory)
{
    uint64_t load_started;
    int flags = recvxCheckFlags();
    int memory = recvxCheckMemory();
    int expand = recvxCheckExpand();
    int cpu_clock = recvxPlatformGetCpuClockFrequency();
    int bus_clock = recvxPlatformGetBusClockFrequency();

    room_file_load_us = room_scene_build_us = room_mesh_build_us = 0;
    room_texture_build_us = room_collision_load_us = room_load_total_us = 0;
    room_load_min_free_bytes = sceKernelTotalFreeMemSize();

    recvxSceneFree(&scene);
    recvxMeshFree(&mesh);
    recvxTexturesFree(&textures);
    recvxPlayerRoomFree(&player_room);
    recvxLegacyEventReset();
    recvxEventDataFree(&event_data);
    event_data_status = 1;
    player_ready = 0;
    room_name[0] = '\0';

    pspDebugScreenClear();
    pspDebugScreenPrintf("Resident Evil: Code Veronica X\n");
    pspDebugScreenPrintf("Room walking prototype\n\n");
    if (clock_set_result == 0)
        pspDebugScreenPrintf("Clock: CPU %d MHz, bus/GPU %d MHz\n", cpu_clock, bus_clock);
    else
        pspDebugScreenPrintf("Clock request failed (%d); CPU %d, bus/GPU %d MHz\n",
                             clock_set_result, cpu_clock, bus_clock);
    pspDebugScreenPrintf("Original game flag routines: %s (%d)\n",
                         flags ? "FAIL" : "PASS", flags);
    pspDebugScreenPrintf("Original Ninja memory copies: %s (%d)\n",
                         memory ? "FAIL" : "PASS", memory);
    pspDebugScreenPrintf("Portable asset decompression: %s (%d)\n\n",
                         expand ? "FAIL" : "PASS", expand);
    show_unsupported_tasks();
    show_unsupported_services();
    if (player_asset_status == 0)
        pspDebugScreenPrintf("Original actor data: %u models, %u skins, %u/%u motions\n\n",
                             (unsigned int)player_assets.model_count,
                             (unsigned int)player_assets.skin_count,
                             (unsigned int)player_assets.populated_motions,
                             (unsigned int)player_assets.motion_slots);
    else if (player_asset_status == 1)
        pspDebugScreenPrintf("Original actor data: copy player-assets/ from make player-assets\n\n");
    else
        pspDebugScreenPrintf("Original actor data: invalid records (%d)\n\n",
                             player_asset_status);
    if (player_asset_status == 0 && player_mesh_status == 0)
        pspDebugScreenPrintf("Player mesh: %u objects, %u triangles, static preview\n\n",
                             (unsigned int)player_mesh.object_count,
                             (unsigned int)player_mesh.triangle_count);
    else if (player_asset_status == 0)
        pspDebugScreenPrintf("Player mesh preview unavailable (%d)\n\n",
                             player_mesh_status);
    if (room_note[0] != '\0') {
        pspDebugScreenPrintf("%s\n\n", room_note);
        room_note[0] = '\0';
    }
    pspDebugScreenPrintf("Decompression checks include invalid input.\n");
    pspDebugScreenPrintf("Loading room file...\n");
    load_started = recvxPlatformTimeUs();
    {
        RecvxRoomFileInfo info;
        RecvxRoomData room_data = {0};
        uint64_t stage_started = recvxPlatformTimeUs();
        int result;
        if (room_catalog_status == 0 && room_catalog_count != 0)
            result = recvxLoadNamedRoomData(data_directory, room_catalog[current_room],
                                            &info, &room_data);
        else if (room_catalog_status == 1)
            result = recvxLoadRoomData(data_directory, &info, &room_data);
        else
            result = 2;
        room_file_load_us = recvxPlatformTimeUs() - stage_started;
        sample_room_load_heap();
        if (result == 1) {
            pspDebugScreenPrintf("Room check: copy data/ from make assets.\n\n");
        } else if (result != 0) {
            pspDebugScreenPrintf("Room check: FAIL (%d)\n\n", result);
            if (room_catalog_status > 1)
                pspDebugScreenPrintf("Room catalog is invalid.\n\n");
        } else {
            pspDebugScreenPrintf("Room %s: PASS\n", info.name);
            event_data_status = recvxEventDataLoad(room_data.bytes,
                                                   room_data.size,
                                                   &event_data);
            if (event_data_status == 0 && !legacy_scenario_attempted &&
                strcmp(info.name, "RM_0000.RDX") == 0) {
                size_t script_size;
                const unsigned char *script =
                    recvxEventDataScript(&event_data, 0, &script_size);
                if (script != NULL)
                    legacy_scenario_result = recvxLegacyScenarioRun(
                        script, script_size, &event_data);
                else
                    legacy_scenario_result.status = RECVX_LEGACY_SCRIPT_INVALID;
                legacy_scenario_attempted = 1;
            }
            pspDebugScreenPrintf("Expanded %u -> %u bytes; CRC32 %08x\n",
                                 (unsigned int)info.packed_size,
                                 (unsigned int)info.expanded_size,
                                 (unsigned int)info.crc32);
            pspDebugScreenPrintf("Cameras %u, lights %u, enemies %u\n\n",
                                 (unsigned int)info.room.counts[RECVX_ROOM_COUNT_CUT],
                                 (unsigned int)info.room.counts[RECVX_ROOM_COUNT_LIGHT],
                                 (unsigned int)info.room.counts[RECVX_ROOM_COUNT_ENEMY]);
            stage_started = recvxPlatformTimeUs();
            result = recvxBuildRoomScene(room_data.bytes, room_data.size, &scene);
            room_scene_build_us = recvxPlatformTimeUs() - stage_started;
            sample_room_load_heap();
            if (result == 0) {
                memcpy(room_name, info.name, sizeof(room_name));
                pspDebugScreenPrintf("Room viewer: READY (%u bounds, %u markers)\n\n",
                                     (unsigned int)scene.box_count,
                                     (unsigned int)scene.marker_count);
            } else {
                pspDebugScreenPrintf("Room viewer: FAIL (%d)\n\n", result);
            }
            stage_started = recvxPlatformTimeUs();
            result = recvxBuildRoomMesh(room_data.bytes, room_data.size, &mesh);
            room_mesh_build_us = recvxPlatformTimeUs() - stage_started;
            sample_room_load_heap();
            if (result == 0)
                pspDebugScreenPrintf("Model geometry: READY (%u models, %u triangles)\n\n",
                                     (unsigned int)mesh.model_count, (unsigned int)mesh.triangle_count);
            else
                pspDebugScreenPrintf("Model geometry: FAIL (%d)\n\n", result);
            stage_started = recvxPlatformTimeUs();
            result = recvxBuildRoomTextures(room_data.bytes, room_data.size, &textures);
            if (result == 0)
                result = recvxValidateMeshTextures(&mesh, &textures);
            room_texture_build_us = recvxPlatformTimeUs() - stage_started;
            sample_room_load_heap();
            if (result == 0)
                pspDebugScreenPrintf("Room textures: READY (%u, %u KiB)\n\n",
                                     (unsigned int)textures.count,
                                     (unsigned int)(textures.storage_bytes / 1024));
            else {
                recvxTexturesFree(&textures);
                pspDebugScreenPrintf("Room textures: FAIL (%d)\n\n", result);
            }
            stage_started = recvxPlatformTimeUs();
            result = recvxPlayerRoomLoad(room_data.bytes, room_data.size, &player_room);
            if (result == 0)
                result = recvxPlayerSpawn(&player, &player_room, room_data.bytes,
                                          room_data.size, room_spawn);
            room_collision_load_us = recvxPlatformTimeUs() - stage_started;
            sample_room_load_heap();
            if (result == 0) {
                player_ready = 1;
                pspDebugScreenPrintf("Player spawn and room collision: READY\n\n");
            } else {
                recvxPlayerRoomFree(&player_room);
                pspDebugScreenPrintf("Player data: FAIL (%d)\n\n", result);
            }
        }
        recvxRoomDataFree(&room_data);
    }
    room_load_total_us = recvxPlatformTimeUs() - load_started;
    if (door_transition_timing) {
        last_door_transition_us = recvxPlatformTimeUs() -
                                  door_transition_started_us;
        door_transition_timing = 0;
    }
    if (!player_ready)
        pspDebugScreenPrintf("No playable spawn is available.\n\n");
    if (room_catalog_count > 1)
        pspDebugScreenPrintf("Room %u/%u: %s\n", (unsigned int)(current_room + 1),
                             (unsigned int)room_catalog_count, room_name);
    else if (room_name[0] != '\0')
        pspDebugScreenPrintf("Room: %s\n", room_name);
    pspDebugScreenPrintf("Press CROSS to walk this room.\n");
    if (room_catalog_count > 1)
        pspDebugScreenPrintf("Press LEFT/RIGHT to load another room.\n");
    if (scene.vertices != NULL)
        pspDebugScreenPrintf("Press SQUARE for the 3D room viewer.\n");
    pspDebugScreenPrintf("Press SELECT to repeat the checks.\n");
    pspDebugScreenPrintf("Load file %u.%u scene %u.%u ms\n",
                         (unsigned int)(room_file_load_us / 1000),
                         (unsigned int)((room_file_load_us / 100) % 10),
                         (unsigned int)(room_scene_build_us / 1000),
                         (unsigned int)((room_scene_build_us / 100) % 10));
    pspDebugScreenPrintf("Mesh %u.%u tex %u.%u collision %u.%u ms\n",
                         (unsigned int)(room_mesh_build_us / 1000),
                         (unsigned int)((room_mesh_build_us / 100) % 10),
                         (unsigned int)(room_texture_build_us / 1000),
                         (unsigned int)((room_texture_build_us / 100) % 10),
                         (unsigned int)(room_collision_load_us / 1000),
                         (unsigned int)((room_collision_load_us / 100) % 10));
    pspDebugScreenPrintf("Total %u.%u door %u.%u actor %u.%u minfree %uK\n",
                         (unsigned int)(room_load_total_us / 1000),
                         (unsigned int)((room_load_total_us / 100) % 10),
                         (unsigned int)(last_door_transition_us / 1000),
                         (unsigned int)((last_door_transition_us / 100) % 10),
                         (unsigned int)(player_asset_load_us / 1000),
                         (unsigned int)((player_asset_load_us / 100) % 10),
                         (unsigned int)(room_load_min_free_bytes / 1024));
    if (legacy_init_status == 0) {
        float near_plane, far_plane;
        recvxLegacyGetClip(&near_plane, &far_plane);
        pspDebugScreenPrintf("bhSysCallInit PASS %uB, tasks %06X\n",
                             (unsigned int)recvxLegacyArenaUsed(),
                             (unsigned int)recvxLegacyTaskMask());
        pspDebugScreenPrintf("Ninja clip %0.f / %0.f\n", near_plane, far_plane);
    } else {
        pspDebugScreenPrintf("bhSysCallInit FAIL (%d)\n", legacy_init_status);
    }
    if (event_data_status == 0) {
        size_t scd0_size, scd1_size;
        recvxEventDataScript(&event_data, 0, &scd0_size);
        recvxEventDataScript(&event_data, 1, &scd1_size);
        pspDebugScreenPrintf("Original event data %u scripts, SCD %u/%uB\n",
                             event_data.script_count > 2 ?
                                 (unsigned int)(event_data.script_count - 2) : 0,
                             (unsigned int)scd0_size,
                             (unsigned int)scd1_size);
    } else if (event_data_status == 1) {
        pspDebugScreenPrintf("Original event data unavailable\n");
    } else {
        pspDebugScreenPrintf("Original event data invalid (%d)\n",
                             event_data_status);
    }
    if (legacy_scenario_attempted) {
        if (legacy_scenario_result.status == RECVX_LEGACY_SCRIPT_COMPLETE)
            pspDebugScreenPrintf("Original RM_0000 SCD0 complete, %u ops, %u writes\n",
                                 (unsigned int)legacy_scenario_result.instructions,
                                 (unsigned int)legacy_scenario_result.writes);
        else if (legacy_scenario_result.status == RECVX_LEGACY_SCRIPT_YIELDED)
            pspDebugScreenPrintf("RM_0000 SCD0 yielded at %04X (%u ops, %u writes)\n",
                                 (unsigned int)legacy_scenario_result.stop_offset,
                                 (unsigned int)legacy_scenario_result.instructions,
                                 (unsigned int)legacy_scenario_result.writes);
        else if (legacy_scenario_result.status == RECVX_LEGACY_SCRIPT_UNSUPPORTED)
            pspDebugScreenPrintf("RM_0000 SCD0 unsupported %02X at %04X (%u ops, %u writes)\n",
                                 legacy_scenario_result.stop_opcode,
                                 (unsigned int)legacy_scenario_result.stop_offset,
                                 (unsigned int)legacy_scenario_result.instructions,
                                 (unsigned int)legacy_scenario_result.writes);
        else
            pspDebugScreenPrintf("RM_0000 SCD0 invalid at %04X (%u ops, %u writes)\n",
                                 (unsigned int)legacy_scenario_result.stop_offset,
                                 (unsigned int)legacy_scenario_result.instructions,
                                 (unsigned int)legacy_scenario_result.writes);
        if (legacy_scenario_result.events_queued != 0)
            pspDebugScreenPrintf("SCD0 queued event %u/task %u (%uB body pending)\n",
                                 (unsigned int)legacy_scenario_result.last_event_id,
                                 (unsigned int)legacy_scenario_result.last_event_task,
                                 (unsigned int)legacy_scenario_result.last_event_bytes);
    }
    pspDebugScreenPrintf("Press START or HOME to exit.\n");
}

int main(int argc, char **argv)
{
    RecvxInputState input;
    RecvxInputRepeat input_repeat;
    int callback;
    int viewing = 0;
    int playing = 0;
    char data_directory[512] = "data";

    clock_set_result = recvxPlatformSetClockFrequency(333, 333, 166);

    if (argc > 0 && argv != NULL && argv[0] != NULL) {
        const char *last_slash = strrchr(argv[0], '/');
        if (last_slash != NULL && last_slash - argv[0] < 500)
            snprintf(data_directory, sizeof(data_directory), "%.*s/data",
                     (int)(last_slash - argv[0]), argv[0]);
    }

    room_catalog_status = recvxLoadRoomCatalog(data_directory, room_catalog,
                                                RECVX_MAX_ROOM_CATALOG,
                                                &room_catalog_count);
    load_player_assets(data_directory);

    pspDebugScreenInit();
    callback = sceKernelCreateCallback("Exit", exit_callback, NULL);
    if (callback < 0 || sceKernelRegisterExitCallback(callback) < 0) {
        pspDebugScreenPrintf("Could not register HOME callback.\n");
    }
    recvxPlatformInputInit();
    recvxInputRepeatReset(&input_repeat);
    recvxGameTaskDispatcherInit(&game_tasks);
    if (recvxLegacyRuntimePrepare() == 0) {
        recvxGameTaskBind(&game_tasks, RECVX_GAME_TASK_INIT,
                          init_original_system_task, &legacy_init_status);
        recvxGameTaskSetMasks(&game_tasks,
                              RECVX_GAME_TASK_MASK(RECVX_GAME_TASK_INIT), 0);
        unsupported_task_mask |= recvxGameTaskDispatch(&game_tasks);
    }
    recvxGameTaskBind(&game_tasks, RECVX_GAME_TASK_GAME, render_game_task, NULL);
    recvxGameTaskSetMasks(&game_tasks, recvxLegacyTaskMask(),
                          recvxLegacyPausedTaskMask());
    unsupported_task_mask |= recvxGameTaskDispatch(&game_tasks);

    show_checks(data_directory);

    for (;;) {
        int walking_started_this_frame = 0;
        if (recvxPlatformReadInput(&input) > 0) {
            recvxInputRepeatAdvance(&input_repeat, &input,
                                    recvxPlatformTimeUs());
            uint32_t pressed = input.pressed;
            if (pressed & RECVX_BUTTON_START)
                break;
            if (!viewing && room_catalog_count > 1 &&
                (input.repeat & (RECVX_BUTTON_LEFT | RECVX_BUTTON_RIGHT))) {
                if (input.repeat & RECVX_BUTTON_LEFT)
                    current_room = (current_room + room_catalog_count - 1) % room_catalog_count;
                else
                    current_room = (current_room + 1) % room_catalog_count;
                room_spawn = 0;
                door_exit_lock = 0;
                show_checks(data_directory);
            }
            if (!viewing && (pressed & RECVX_BUTTON_CROSS) && player_ready) {
                if (recvxRendererStart(&scene, &mesh, &textures, &player,
                                       player_asset_status == 0 ? &player_assets : NULL,
                                       player_mesh_status == 0 ? &player_mesh : NULL,
                                       player_source_bytes) == 0) {
                    viewing = 1;
                    playing = 1;
                    walking_started_this_frame = 1;
                } else {
                    pspDebugScreenPrintf("Could not initialize walking mode.\n");
                }
            }
            if (pressed & RECVX_BUTTON_SQUARE) {
                if (viewing) {
                    recvxRendererStop();
                    viewing = 0;
                    playing = 0;
                    show_checks(data_directory);
                } else if (scene.vertices != NULL) {
                    if (recvxRendererStart(&scene, &mesh, &textures, NULL,
                                           NULL, NULL, 0) == 0)
                        viewing = 1;
                    else
                        pspDebugScreenPrintf("Could not initialize the renderer.\n");
                }
            }
            if (!viewing && (pressed & RECVX_BUTTON_SELECT))
                show_checks(data_directory);
            if (viewing && playing && !walking_started_this_frame) {
                RecvxPlayerDoor door;
                int at_door = recvxPlayerDoorAt(&player, &player_room, &door);
                if (door_exit_lock) {
                    if (!at_door)
                        door_exit_lock = 0;
                } else if ((pressed & RECVX_BUTTON_CROSS) && at_door) {
                    char target_name[RECVX_ROOM_NAME_SIZE];
                    unsigned int case_no = room_name[6] >= '0' && room_name[6] <= '9' ?
                                           (unsigned int)(room_name[6] - '0') : 0;
                    size_t i;
                    snprintf(target_name, sizeof(target_name), "RM_%1u%02u%1u.RDX",
                             door.target_stage, door.target_room, case_no);
                    for (i = 0; i < room_catalog_count; ++i)
                        if (strcmp(room_catalog[i], target_name) == 0)
                            break;
                    door_transition_started_us = recvxPlatformTimeUs();
                    door_transition_timing = 1;
                    recvxRendererStop();
                    viewing = 0;
                    playing = 0;
                    if (i < room_catalog_count) {
                        current_room = i;
                        room_spawn = door.target_pos;
                        door_exit_lock = 1;
                        room_note[0] = '\0';
                    } else {
                        snprintf(room_note, sizeof(room_note),
                                 "Door target %s is not in this room catalog", target_name);
                    }
                    show_checks(data_directory);
                }
            }
            game_task_input = input;
            game_task_playing = playing;
            recvxGameTaskSetMasks(&game_tasks,
                                  recvxLegacyTaskMask() |
                                      (viewing ? RECVX_GAME_TASK_MASK(RECVX_GAME_TASK_GAME) : 0),
                                  recvxLegacyPausedTaskMask());
            unsupported_task_mask |= recvxGameTaskDispatch(&game_tasks);
        }
        if (viewing)
            recvxPlatformCheckCallbacks();
        recvxPlatformWaitVblank();
        recvxLegacyVsyncTick();
    }

    recvxRendererStop();
    recvxSceneFree(&scene);
    recvxMeshFree(&mesh);
    recvxTexturesFree(&textures);
    recvxPlayerRoomFree(&player_room);
    recvxEventDataFree(&event_data);
    recvxMeshFree(&player_mesh);
    free(player_model_data);
    free(player_motion_data);
    recvxLegacyRuntimeShutdown();
    sceKernelExitGame();
    return 0;
}
