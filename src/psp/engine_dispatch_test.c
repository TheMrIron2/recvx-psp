#include "ps2/veronica/prog/types.h"
#define __u_char_defined
#define __u_short_defined
#define __u_int_defined
#define __u_long_defined

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/system.h"
#include "psp/engine_event_core.h"
#include "psp/engine_event_runtime.h"
#include "psp/engine_event_test.h"
#include "psp/engine_pad_adapter.h"
#include "psp/engine_pacer.h"
#include "psp/engine_room_owner.h"
#include "psp/event_data.h"
#include "psp/game_tasks.h"
#include "psp/live_task_dispatch.h"
#include "psp/original_clock.h"
#include "psp/platform.h"
#include "psp/renderer.h"
#include "psp/scene.h"

PSP_MODULE_INFO("RECVX TASK TEST", PSP_MODULE_USER, 0, 6);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024);

#define TASK_PAD 6u
#define TASK_EVENT 8u
#define TASK_UNSUPPORTED 1u
#define TEST_TASK_MASK ((1u << TASK_PAD) | (1u << TASK_EVENT))

static RecvxEngineRoomOwner room_owner;
static RecvxEngineRoomView room_view;
static RecvxScene scene;
static RecvxEventData event_data;
static RecvxEngineEventTestReport event_report;
static RecvxInputState tick_input;
static RecvxInputState neutral_input = {.analog_x = 128, .analog_y = 128};
static RecvxEnginePacer pacer;
static RecvxLiveTaskProc jump_table[23];
static SYS_WORK before_sys;
static ROM_WORK before_room;
static BH_SCEWORK before_tasks[16];
static RecvxScene before_scene;
static unsigned int before_event_timer;
static uint32_t before_event_flags, before_event_instructions;
static uint32_t engine_fault_mask;
static uint32_t event_calls, pad_calls, passive_draw_checks;
static uint32_t dispatch_order;
static int runtime_fault;
static int test_complete;
static char overlay[320];

static void task_pad(void)
{
    dispatch_order = dispatch_order * 10 + TASK_PAD;
    ++pad_calls;
    if (recvxEnginePadAdapterUpdate(&tick_input) != 0) {
        runtime_fault = -1;
        return;
    }
    bhSysCallPad();
    if (recvxEnginePadAdapterFaulted())
        runtime_fault = -2;
}

static void task_event(void)
{
    dispatch_order = dispatch_order * 10 + TASK_EVENT;
    ++event_calls;
    if (bhEventScheduler2() != 0)
        runtime_fault = -3;
}

static int load_engine_fixture(const char *directory)
{
    const unsigned char *room_bytes;
    size_t room_size;

    if (recvxEngineRoomOwnerOpen(&room_owner, directory, "RM_0000.RDX") != 0)
        return -1;
    if (recvxEngineRoomOwnerView(&room_owner, &room_view) != 0 ||
        recvxEngineRoomViewPin(&room_view) != 0)
        return -2;

    room_size = room_owner.file.expanded_size;
    room_bytes = recvxEngineRoomViewBytes(&room_view, 0, room_size);
    if (room_bytes == NULL ||
        recvxBuildRoomScene(room_bytes, room_size, &scene) != 0)
        return -3;
    if (recvxEngineEventTestsRun(room_bytes, room_size, &event_report) != 0 ||
        !event_report.passed || !event_report.original_event_ran)
        return -4;
    if (recvxEventDataLoad(room_bytes, room_size, &event_data) != 0)
        return -5;

    recvxEngineEventRuntimeReset();
    rom->evtp = (EVT_WORK *)event_data.bytes;
    if (recvxEventCoreBindSection(event_data.bytes, event_data.size) != 0)
        return -6;
    sys->sp_flg = 0x30;
    sys->keytype = 0;
    sys->tk_flg = TEST_TASK_MASK;
    sys->ts_flg = 0;
    if (Event_exec(7, 38) != 0 || bhEtask[7].status != 1)
        return -7;
    return 0;
}

static int event_dispatch_matches_report(void)
{
    unsigned int i;
    if (bhEtask[7].status != 0 ||
        recvxEventCoreTaskBodyPc(7) != 0x10a ||
        recvxEventCoreFlagWriteCount() != 44 ||
        recvxEventCoreInstructionCount() != 45 || Event_T_timer != 1 ||
        dispatch_order != 68 || pad_calls != 1 || event_calls != 1 ||
        (sys->tk_flg & (1u << 7)) != 0)
        return 0;
    for (i = 0; i < 16; ++i)
        if (sys->ky_flg[i] != event_report.event_flags[i])
            return 0;
    return 1;
}

static void update_overlay(uint32_t vblanks)
{
    const char *status = runtime_fault != 0 ? "FAIL" :
                         test_complete ? "PASS" : "RUNNING";
    if (engine_fault_mask != 0) {
        unsigned int slot;
        for (slot = 0; slot < 23; ++slot)
            if (engine_fault_mask & (1u << slot))
                break;
        snprintf(overlay, sizeof(overlay),
                 "%s  original dispatcher / passive GU\n"
                 "VBlank %u  engine tick %u  stall %u  sys.time %d\n"
                 "Pad on %08X pressed %08X released %08X\n"
                 "Event writes %u PC %04X timer %u callback %u\n"
                 "Unsupported slot %u (%s), GU still presenting\n"
                 "START exits",
                 status, (unsigned int)pacer.total_vblanks,
                 (unsigned int)pacer.ticks, (unsigned int)pacer.stalls,
                 sys->time,
                 (unsigned int)sys->pad_on, (unsigned int)sys->pad_ps,
                 (unsigned int)sys->pad_rs,
                 (unsigned int)recvxEventCoreFlagWriteCount(),
                 (unsigned int)recvxEventCoreTaskBodyPc(7),
                 (unsigned int)Event_T_timer, (unsigned int)sys->vsyc_flg,
                 slot,
                 recvxGameTaskName(slot));
    } else if (runtime_fault == -2) {
        snprintf(overlay, sizeof(overlay),
                 "FAIL  original dispatcher / passive GU\n"
                 "Pad dependency fault: %s\n"
                 "VBlank %u  engine tick %u  stall %u\nSTART exits",
                 recvxEnginePadAdapterFaultName(),
                 (unsigned int)pacer.total_vblanks,
                 (unsigned int)pacer.ticks, (unsigned int)pacer.stalls);
    } else {
        snprintf(overlay, sizeof(overlay),
                 "%s  original dispatcher / passive GU\n"
                 "VBlank %u engine %u stall %u sys.time %d\n"
                 "Pad %08X edge %08X release %08X axes %d,%d\n"
                 "Event %u writes PC %04X timer %u callback %u\n"
                 "Draw purity checks %u  elapsed VBlanks %u\n"
                 "D-pad/buttons feed bhSetPad; START exits",
                 status, (unsigned int)pacer.total_vblanks,
                 (unsigned int)pacer.ticks, (unsigned int)pacer.stalls,
                 sys->time,
                 (unsigned int)sys->pad_on, (unsigned int)sys->pad_ps,
                 (unsigned int)sys->pad_rs, sys->pad_ax, sys->pad_ay,
                 (unsigned int)recvxEventCoreFlagWriteCount(),
                 (unsigned int)recvxEventCoreTaskBodyPc(7),
                 (unsigned int)Event_T_timer, (unsigned int)sys->vsyc_flg,
                 (unsigned int)passive_draw_checks, (unsigned int)vblanks);
    }
    recvxRendererSetOverlay(overlay);
}

static int draw_without_game_state_writes(void)
{
    memcpy(&before_sys, sys, sizeof(before_sys));
    memcpy(&before_room, rom, sizeof(before_room));
    memcpy(before_tasks, bhEtask, sizeof(before_tasks));
    before_scene = scene;
    before_event_timer = Event_T_timer;
    before_event_flags = recvxEventCoreFlagWriteCount();
    before_event_instructions = recvxEventCoreInstructionCount();
    recvxRendererDraw(1);
    if (memcmp(&before_sys, sys, sizeof(before_sys)) != 0 ||
        memcmp(&before_room, rom, sizeof(before_room)) != 0 ||
        memcmp(before_tasks, bhEtask, sizeof(before_tasks)) != 0 ||
        memcmp(&before_scene, &scene, sizeof(scene)) != 0 ||
        before_event_timer != Event_T_timer ||
        before_event_flags != recvxEventCoreFlagWriteCount() ||
        before_event_instructions != recvxEventCoreInstructionCount())
        return -1;
    ++passive_draw_checks;
    return 0;
}

static int should_exit(void)
{
    SceCtrlData input;
    sceCtrlReadBufferPositive(&input, 1);
    return (input.Buttons & PSP_CTRL_START) != 0;
}

static void prepare_tasks(void)
{
    memset(jump_table, 0, sizeof(jump_table));
    jump_table[TASK_PAD] = task_pad;
    jump_table[TASK_EVENT] = task_event;
}

static int run_engine_test(void)
{
    uint32_t previous_vblanks, elapsed_vblanks = 0;

    if (recvxRendererStart(&scene, NULL, NULL, NULL, NULL,
                           NULL, NULL, NULL, 0) != 0)
        return -1;
    recvxRendererUpdate(&scene, NULL, NULL, NULL, NULL, NULL,
                        "Original task test", &neutral_input);
    recvxPlatformInputInit();
    recvxEnginePadAdapterReset();
    recvxEnginePadAdapterResetFault();
    prepare_tasks();
    previous_vblanks = sceDisplayGetVcount();
    recvxEnginePacerInit(&pacer, previous_vblanks);

    while (!should_exit()) {
        enum RecvxEnginePacerResult result;

        if (draw_without_game_state_writes() != 0) {
            runtime_fault = -5;
            break;
        }
        recvxPlatformCheckCallbacks();
        previous_vblanks = sceDisplayGetVcount();
        result = recvxEnginePacerAdvance(&pacer, previous_vblanks,
                                          &elapsed_vblanks);
        recvxOriginalEngineClockAdvance(
            elapsed_vblanks, result == RECVX_ENGINE_PACER_TICK);

        if (result == RECVX_ENGINE_PACER_TICK && !test_complete &&
            runtime_fault == 0) {
            uint32_t unsupported;
            if (recvxPlatformReadInput(&tick_input) <= 0) {
                runtime_fault = -4;
                break;
            }
            dispatch_order = 0;
            unsupported = recvxRunLiveTaskSlots(&sys->tk_flg, &sys->ts_flg,
                                                jump_table, 23);
            if (unsupported != 0 || runtime_fault != 0) {
                engine_fault_mask = unsupported;
                if (runtime_fault == 0)
                    runtime_fault = -6;
            } else if (event_report.original_event_ran &&
                       event_dispatch_matches_report()) {
                sys->tk_flg |= 1u << TASK_UNSUPPORTED;
                unsupported = recvxRunLiveTaskSlots(
                    &sys->tk_flg, &sys->ts_flg, jump_table, 23);
                if (unsupported != (1u << TASK_UNSUPPORTED) ||
                    dispatch_order != 68 || pad_calls != 1 || event_calls != 1)
                    runtime_fault = -7;
                else {
                    engine_fault_mask = unsupported;
                    test_complete = 1;
                }
            } else {
                runtime_fault = -8;
            }
        }
        update_overlay(elapsed_vblanks);
    }
    return runtime_fault != 0 ? runtime_fault : test_complete ? 0 : -9;
}

static void wait_for_start(void)
{
    SceCtrlData input;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    do {
        sceCtrlReadBufferPositive(&input, 1);
        if (input.Buttons & PSP_CTRL_START)
            break;
        sceDisplayWaitVblankStart();
    } while (1);
}

int main(int argc, char **argv)
{
    char data_directory[512] = "data";
    int result;

    pspDebugScreenInit();
    pspDebugScreenClear();
    pspDebugScreenPrintf("RECVX Package 3 task/pad/clock test\n");
    pspDebugScreenPrintf("Original task loop, room %s, passive GU\n\n",
                         "RM_0000");
    if (argc > 0 && argv != NULL && argv[0] != NULL) {
        const char *last_slash = strrchr(argv[0], '/');
        if (last_slash != NULL && last_slash - argv[0] < 500)
            snprintf(data_directory, sizeof(data_directory), "%.*s/data",
                     (int)(last_slash - argv[0]), argv[0]);
    }

    result = load_engine_fixture(data_directory);
    if (result != 0) {
        pspDebugScreenPrintf("FAIL: engine fixture setup %d\n", result);
        wait_for_start();
        goto cleanup;
    }
    pspDebugScreenPrintf("Original event 38 checks %u PASS\n",
                         (unsigned int)event_report.assertions_passed);
    pspDebugScreenPrintf("Press D-pad/buttons to inspect original pad state\n");
    pspDebugScreenPrintf("The warning-slot fault is injected after the event\n");

    result = run_engine_test();
    if (result != 0)
        pspDebugScreenPrintf("\nENGINE TEST FAILED (%d)\n", result);
    else if (test_complete)
        pspDebugScreenPrintf("\nTEST PASSED; renderer remains active after fault\n");
    else
        pspDebugScreenPrintf("\nTEST EXITED BEFORE COMPLETION\n");
    wait_for_start();

cleanup:
    recvxRendererStop();
    recvxSceneFree(&scene);
    recvxEngineEventRuntimeReset();
    recvxEventDataFree(&event_data);
    if (room_view.owner != NULL && room_view.generation != 0)
        recvxEngineRoomViewUnpin(&room_view);
    if (room_owner.resident)
        recvxEngineRoomOwnerClose(&room_owner);
    sceKernelExitGame();
    return result;
}
