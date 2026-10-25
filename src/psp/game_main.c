#include <malloc.h>
#include "psp/game_boot_audio.h"
#include "psp/game_boot_movie.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_skin.h"
#include "psp/live_task_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include "psp/game_boot.h"
#include "psp/original_clock.h"
#include "ps2/veronica/prog/main.h"

PSP_MODULE_INFO("RECVX GAME", PSP_MODULE_USER, 0, 7);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024);
static volatile int exit_requested;
static int exit_callback(int a, int b, void *context)
{ (void)a; (void)b; (void)context; exit_requested = 1; return 0; }

static uint64_t main_cpu_time(void)
{
    SceKernelThreadInfo info={.size=sizeof(info)};
    if(sceKernelReferThreadStatus(sceKernelGetThreadId(),&info)<0)return 0;
    return ((uint64_t)info.runClocks.hi<<32)|info.runClocks.low;
}

static unsigned int timer_call_ns(void)
{
    unsigned int best=~0u;
    for(unsigned int pass=0;pass<3;++pass){
        unsigned int start=sceKernelGetSystemTimeLow();
        for(unsigned int i=0;i<512;++i)(void)sceKernelGetSystemTimeLow();
        unsigned int elapsed=sceKernelGetSystemTimeLow()-start;
        if(elapsed<best)best=elapsed;
    }
    return best*1000u/512u;
}

int main(int argc, char **argv)
{
    const char *data_directory = argc > 1 ? argv[1] : "data";
    void *arena = NULL;
    int callback;
    int thread_result;
    int clock_result;
    SceKernelThreadInfo main_thread = {0};
    unsigned int previous_vblank;
    unsigned int perf_frames=0,perf_started=0;
    uint64_t perf_tick_us=0,perf_main_cpu=0;
    RecvxAudioOutputStats perf_audio={0};
    RecvxBootRenderStats perf_render={0};RecvxBootGuStats perf_gu={0};
    RecvxLiveTaskStats perf_tasks={0};
    RecvxBootSkinStats perf_skin={0};RecvxBootChunkStats perf_chunk={0};
    pspDebugScreenInit();
    callback = sceKernelCreateCallback("RECVX Exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(callback);
    clock_result=recvxPlatformSetClockFrequency(333, 333, 166);
    recvxPlatformInputInit();
    main_thread.size = sizeof(main_thread);
    thread_result = sceKernelReferThreadStatus(sceKernelGetThreadId(), &main_thread);
    if (thread_result < 0 || !(main_thread.attr & PSP_THREAD_ATTR_VFPU)) {
        pspDebugScreenPrintf("Game startup failed: main thread has no VFPU access.\nThread attrs=%08x status=%08x\n",
            (unsigned int)main_thread.attr, (unsigned int)thread_result);
    } else if (!(arena = memalign(64, 12u * 1024u * 1024u)) ||
        recvxGameBootInit(data_directory, arena, 12u * 1024u * 1024u) || recvxAudioOutputInit()) {
        pspDebugScreenPrintf("Game startup failed: arena or native audio unavailable.\nSAS=%08x\n%s\n",
            (unsigned int)recvxAudioNativeError(), recvxAudioNativeErrorCall());
        recvxGameBootMovieShutdown();
        recvxAudioOutputShutdown();
    } else {
        recvxGameBootLog("render build=%s %s native GE lighting; direct native strips v1; direct skin v1; strip_join=%u",__DATE__,__TIME__,RECVX_GAME_CHUNK_JOIN_MAX);
        recvxGameBootLog("main thread attributes=%08x; VFPU enabled", (unsigned int)main_thread.attr);
        recvxGameBootLog("clocks cpu_mhz=%d bus_mhz=%d request=%08x",
            scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt(),(unsigned int)clock_result);
        recvxGameBootLog("native audio ready: AVCODEC + SASCORE; %u Hz stereo; grain=%u",RECVX_AUDIO_RATE,RECVX_AUDIO_FRAMES);
        recvxGameBootLog("perf timer_call_ns=%u vertex_sample=1/64 bulk_sample=1/8",timer_call_ns());
        recvxGameBootDisplayInit();
        previous_vblank = sceDisplayGetVcount();
        perf_started=sceKernelGetSystemTimeLow();
        perf_main_cpu=main_cpu_time();perf_audio=recvxAudioOutputStats();
        perf_render=recvxGameBootRenderStats();perf_gu=recvxGameBootGuStats();
        perf_tasks=recvxLiveTaskStats();perf_skin=recvxGameBootSkinStats();perf_chunk=recvxGameBootChunkStats();
        while (!exit_requested) {
            RecvxInputState input;
            if (recvxPlatformReadInput(&input) <= 0) {
                recvxPlatformCheckCallbacks(); recvxPlatformWaitVblank(); continue;
            }
            unsigned int tick_started=sceKernelGetSystemTimeLow();
            if (recvxGameBootTick(&input)) break;
            perf_tick_us+=sceKernelGetSystemTimeLow()-tick_started;
            /* Cap at the original 30 Hz, counting time already spent drawing.
               Over-budget frames wait only for the next tear-free swap. */
            do {recvxPlatformWaitVblank();}
            while((unsigned int)(sceDisplayGetVcount()-previous_vblank)<2u);
            recvxGameBootDisplaySwap();
            {
                unsigned int vblank = sceDisplayGetVcount();
                recvxOriginalEngineClockAdvance(vblank - previous_vblank, 1);
                previous_vblank = vblank;
            }
            recvxPlatformCheckCallbacks();
            if(++perf_frames==120){
                unsigned int now=sceKernelGetSystemTimeLow();
                RecvxBootRenderStats r=recvxGameBootRenderStats();RecvxBootGuStats g=recvxGameBootGuStats();
                uint64_t cpu_now=main_cpu_time();RecvxAudioOutputStats audio=recvxAudioOutputStats();
                recvxGameBootLog("perf_threads main_cpu_us=%u audio_cpu_us=%u audio_render_us=%u audio_wait_us=%u audio_grains=%u",
                    (unsigned int)((cpu_now-perf_main_cpu)/perf_frames),
                    (unsigned int)((audio.cpu_us-perf_audio.cpu_us)/perf_frames),
                    (audio.render_us-perf_audio.render_us)/perf_frames,(audio.wait_us-perf_audio.wait_us)/perf_frames,
                    (audio.grains-perf_audio.grains)/perf_frames);
                perf_main_cpu=cpu_now;perf_audio=audio;
                recvxGameBootLog("perf frames=%u frame_us=%u tick_us=%u gu_build_us=%u gu_wait_us=%u models=%u tris=%u pos_xform=%u normal_xform=%u shade=%u",
                    perf_frames,(now-perf_started)/perf_frames,(unsigned int)(perf_tick_us/perf_frames),
                    (g.build_us-perf_gu.build_us)/perf_frames,(g.wait_us-perf_gu.wait_us)/perf_frames,
                    (r.models-perf_render.models)/perf_frames,(r.triangles-perf_render.triangles)/perf_frames,
                    (r.position_transforms-perf_render.position_transforms)/perf_frames,
                    (r.normal_transforms-perf_render.normal_transforms)/perf_frames,
                    (r.shaded_vertices-perf_render.shaded_vertices)/perf_frames);
                RecvxLiveTaskStats tasks=recvxLiveTaskStats();
                recvxGameBootLog("perf_cpu game_us=%u event_us=%u item_us=%u monitor_us=%u sound_us=%u chunk_us=%u materials=%u face_normals=%u clip_in=%u clip_out=%u clip_cut=%u",
                    (tasks.us[7]-perf_tasks.us[7])/perf_frames,(tasks.us[8]-perf_tasks.us[8])/perf_frames,
                    (tasks.us[9]-perf_tasks.us[9])/perf_frames,(tasks.us[20]-perf_tasks.us[20])/perf_frames,
                    (tasks.us[21]-perf_tasks.us[21])/perf_frames,(r.chunk_us-perf_render.chunk_us)/perf_frames,
                    (r.material_preparations-perf_render.material_preparations)/perf_frames,
                    (r.face_normalizations-perf_render.face_normalizations)/perf_frames,
                    (r.clip_inside-perf_render.clip_inside)/perf_frames,
                    (r.clip_rejected-perf_render.clip_rejected)/perf_frames,
                    (r.clip_intersections-perf_render.clip_intersections)/perf_frames);
                recvxGameBootLog("perf_gpu vertices=%u lit_vertices=%u colored_vertices=%u overflow_vertices=%u fallback_tris=%u",
                    (r.gpu_vertices-perf_render.gpu_vertices)/perf_frames,
                    (r.gpu_lit_vertices-perf_render.gpu_lit_vertices)/perf_frames,
                    (r.gpu_colored_vertices-perf_render.gpu_colored_vertices)/perf_frames,
                    (r.gpu_overflow_vertices-perf_render.gpu_overflow_vertices)/perf_frames,
                    (r.gpu_fallback_triangles-perf_render.gpu_fallback_triangles)/perf_frames);
                recvxGameBootLog("perf_chunk prepare_us=%u emit_sample_us=%u vertices_sample_us=%u classified=%u vfpu_distances=%u direct_vertices=%u distance_batches=%u refinements=%u reused=%u",
                    (r.gpu_prepare_us-perf_render.gpu_prepare_us)/perf_frames,
                    (r.gpu_emit_us-perf_render.gpu_emit_us)/perf_frames,
                    (r.gpu_vertex_us-perf_render.gpu_vertex_us)/perf_frames,
                    (r.gpu_classifications-perf_render.gpu_classifications)/perf_frames,
                    (r.gpu_vfpu_distances-perf_render.gpu_vfpu_distances)/perf_frames,
                    (r.gpu_direct_vertices-perf_render.gpu_direct_vertices)/perf_frames,
                    (r.gpu_distance_batches-perf_render.gpu_distance_batches)/perf_frames,
                    (r.gpu_distance_refinements-perf_render.gpu_distance_refinements)/perf_frames,
                    (r.gpu_distance_reuse-perf_render.gpu_distance_reuse)/perf_frames);
                recvxGameBootLog("perf_bulk triangles=%u attempts=%u sample_us=%u gu_splits_total=%u cache_hits=%u cache_misses=%u light_hits=%u light_misses=%u light_bypass=%u",
                    (r.gpu_bulk_triangles-perf_render.gpu_bulk_triangles)/perf_frames,
                    (r.gpu_bulk_attempts-perf_render.gpu_bulk_attempts)/perf_frames,
                    (r.gpu_bulk_us-perf_render.gpu_bulk_us)/perf_frames,g.list_splits,
                    (r.gpu_strip_cache_hits-perf_render.gpu_strip_cache_hits)/perf_frames,
                    (r.gpu_strip_cache_misses-perf_render.gpu_strip_cache_misses)/perf_frames,
                    (r.gpu_light_cache_hits-perf_render.gpu_light_cache_hits)/perf_frames,
                    (r.gpu_light_cache_misses-perf_render.gpu_light_cache_misses)/perf_frames,
                    (r.gpu_light_cache_bypass-perf_render.gpu_light_cache_bypass)/perf_frames);
                recvxGameBootLog("perf_vertex calls=%u classify_us=%u overflow_us=%u output_us=%u model_cache_bypass=%u",
                    (r.gpu_vertex_calls-perf_render.gpu_vertex_calls)/perf_frames,
                    (r.gpu_classify_us-perf_render.gpu_classify_us)/perf_frames,
                    (r.gpu_overflow_us-perf_render.gpu_overflow_us)/perf_frames,
                    (r.gpu_output_us-perf_render.gpu_output_us)/perf_frames,
                    (r.gpu_model_cache_bypass-perf_render.gpu_model_cache_bypass)/perf_frames);
                recvxGameBootLog("perf_groups classified_vertices=%u constant_vertices=%u metric_skips=%u bounds_us=%u range_queries=%u reused_vertices=%u",
                    (r.gpu_group_vertices-perf_render.gpu_group_vertices)/perf_frames,
                    (r.gpu_group_constant_vertices-perf_render.gpu_group_constant_vertices)/perf_frames,
                    (r.gpu_group_metric_skips-perf_render.gpu_group_metric_skips)/perf_frames,
                    (r.gpu_group_bounds_us-perf_render.gpu_group_bounds_us)/perf_frames,
                    (r.gpu_group_range_queries-perf_render.gpu_group_range_queries)/perf_frames,
                    (r.gpu_group_reused_vertices-perf_render.gpu_group_reused_vertices)/perf_frames);
                recvxGameBootLog("perf_cull groups=%u vertices=%u triangles=%u fast_overflow_vertices=%u vfpu_bound_vertices=%u join_decode_saved=%u matrix_us=%u",
                    (r.gpu_culled_groups-perf_render.gpu_culled_groups)/perf_frames,
                    (r.gpu_culled_vertices-perf_render.gpu_culled_vertices)/perf_frames,
                    (r.gpu_culled_triangles-perf_render.gpu_culled_triangles)/perf_frames,
                    (r.gpu_fast_overflow_vertices-perf_render.gpu_fast_overflow_vertices)/perf_frames,
                    (r.gpu_vfpu_bound_vertices-perf_render.gpu_vfpu_bound_vertices)/perf_frames,
                    (r.chunk_join_skipped_vertices-perf_render.chunk_join_skipped_vertices)/perf_frames,
                    (r.gpu_matrix_us-perf_render.gpu_matrix_us)/perf_frames);
                recvxGameBootLog("perf_native_lighting vertices=%u hdr_lights=%u omitted_lights=%u",
                    (r.gpu_native_light_vertices-perf_render.gpu_native_light_vertices)/perf_frames,
                    (r.gpu_native_hdr_lights-perf_render.gpu_native_hdr_lights)/perf_frames,
                    (r.gpu_native_omitted_lights-perf_render.gpu_native_omitted_lights)/perf_frames);
                recvxGameBootLog("perf_gpu_fallback clip=%u specular=%u matrix=%u color=%u lights=%u normal=%u cutoff=%u vertex_color=%u",
                    (r.gpu_fallback[0]-perf_render.gpu_fallback[0])/perf_frames,
                    (r.gpu_fallback[1]-perf_render.gpu_fallback[1])/perf_frames,
                    (r.gpu_fallback[2]-perf_render.gpu_fallback[2])/perf_frames,
                    (r.gpu_fallback[3]-perf_render.gpu_fallback[3])/perf_frames,
                    (r.gpu_fallback[4]-perf_render.gpu_fallback[4])/perf_frames,
                    (r.gpu_fallback[5]-perf_render.gpu_fallback[5])/perf_frames,
                    (r.gpu_fallback[6]-perf_render.gpu_fallback[6])/perf_frames,
                    (r.gpu_fallback[7]-perf_render.gpu_fallback[7])/perf_frames);
                recvxGameBootLog("perf_raw vertices=%u groups=%u partial_groups=%u source_normals=%u",
                    (r.gpu_raw_vertices-perf_render.gpu_raw_vertices)/perf_frames,
                    (r.gpu_raw_groups-perf_render.gpu_raw_groups)/perf_frames,
                    (r.gpu_raw_partial_groups-perf_render.gpu_raw_partial_groups)/perf_frames,
                    (r.gpu_raw_normal_vertices-perf_render.gpu_raw_normal_vertices)/perf_frames);
                RecvxBootSkinStats skin=recvxGameBootSkinStats();RecvxBootChunkStats chunk=recvxGameBootChunkStats();
                recvxGameBootLog("perf_geometry room=%u/%u/%u parse_us=%u validate_us=%u source_vertices=%u native_preflight_us=%u skin_prepare_us=%u skin_blend_us=%u skin_vertices=%u model_inside_groups=%u model_outside_groups=%u",
                    sys->stg_no,sys->rom_no,sys->rcase,
                    (r.chunk_parse_us-perf_render.chunk_parse_us)/perf_frames,
                    (chunk.validation_us-perf_chunk.validation_us)/perf_frames,
                    (chunk.source_vertices-perf_chunk.source_vertices)/perf_frames,
                    (r.native_preflight_us-perf_render.native_preflight_us)/perf_frames,
                    (skin.prepare_us-perf_skin.prepare_us)/perf_frames,
                    (skin.blend_us-perf_skin.blend_us)/perf_frames,
                    (skin.vertices-perf_skin.vertices)/perf_frames,
                    (r.gpu_model_bounds_inside-perf_render.gpu_model_bounds_inside)/perf_frames,
                    (r.gpu_model_bounds_outside-perf_render.gpu_model_bounds_outside)/perf_frames);
                perf_skin=skin;perf_chunk=chunk;
                recvxGameBootLog("perf_game enemy_us=%u player_us=%u effects_us=%u objects_us=%u camera_us=%u light_us=%u draw_us=%u",
                    (tasks.game_us[0]-perf_tasks.game_us[0])/perf_frames,
                    (tasks.game_us[1]-perf_tasks.game_us[1])/perf_frames,
                    (tasks.game_us[2]-perf_tasks.game_us[2])/perf_frames,
                    (tasks.game_us[3]-perf_tasks.game_us[3])/perf_frames,
                    (tasks.game_us[4]-perf_tasks.game_us[4])/perf_frames,
                    (tasks.game_us[5]-perf_tasks.game_us[5])/perf_frames,
                    (tasks.game_us[6]-perf_tasks.game_us[6])/perf_frames);
                perf_frames=0;perf_tick_us=0;perf_started=sceKernelGetSystemTimeLow();perf_render=r;perf_gu=g;perf_tasks=tasks;
            }
        }
        recvxGameBootMovieShutdown();
        recvxAudioOutputShutdown();
        recvxGameBootRenderReset();
        recvxGameBootTexturesShutdown(); recvxGameBootDisplayShutdown();
        if (recvxGameBootFault()) {
            pspDebugScreenInit(); pspDebugScreenClear();
            pspDebugScreenPrintf("Resident Evil: Code Veronica X\n\nGame boot stopped at:\n%s\n\n", recvxGameBootFault());
            pspDebugScreenPrintf("Task mask: %08x  paused: %08x\n", sys->tk_flg, sys->ts_flg);
            pspDebugScreenPrintf("Next engine service needs a PSP implementation.\nSee boot.log beside EBOOT.PBP.\n\nHOME to exit.\n");
        }
    }
    while (!exit_requested) { sceKernelDelayThreadCB(20000); }
    free(arena); sceKernelExitGame(); return 0;
}
