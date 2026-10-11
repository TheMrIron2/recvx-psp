#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <stdio.h>

#include "psp/engine_event_test.h"
#include "psp/room.h"

PSP_MODULE_INFO("RECVX EVENT TEST", PSP_MODULE_USER, 0, 6);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(18 * 1024);

static void print_report(const RecvxEngineEventTestReport *report)
{
    unsigned int i;

    pspDebugScreenPrintf("Checks: %u\n",
                         (unsigned int)report->assertions_passed);
    if (!report->passed) {
        pspDebugScreenPrintf("FAIL at %s\n%s\n", report->stage,
                             report->detail);
        if (report->fault.code != RECVX_EVENT_FAULT_NONE) {
            if (report->fault.task == UINT16_MAX) {
                pspDebugScreenPrintf("Fault: %s pc %08X opcode %02X\n",
                                     recvxEventCoreFaultName(report->fault.code),
                                     (unsigned int)report->fault.section_pc,
                                     report->fault.opcode);
            } else {
                pspDebugScreenPrintf("Fault: %s event %u task %u\n",
                                     recvxEventCoreFaultName(report->fault.code),
                                     (unsigned int)report->fault.event_id,
                                     report->fault.task);
                pspDebugScreenPrintf("PC %08X opcode %02X\n",
                                     (unsigned int)report->fault.section_pc,
                                     report->fault.opcode);
            }
        }
        return;
    }
    if (!report->original_event_ran) {
        pspDebugScreenPrintf("FAIL: original event 38 was not run\n");
        return;
    }
    pspDebugScreenPrintf("PASS: original Event_exec + scheduler\n");
    pspDebugScreenPrintf("RM_0000 event 38: %u writes, %u instructions\n",
                         (unsigned int)report->event_flag_writes,
                         (unsigned int)report->event_instructions);
    pspDebugScreenPrintf("Timer %u  task 7 ended\n",
                         (unsigned int)report->event_timer);
    pspDebugScreenPrintf("PC body %04X  section %08X\n",
                         (unsigned int)report->event_pc_body,
                         (unsigned int)report->event_pc_section);
    pspDebugScreenPrintf("Span SHA-256:\n%s\n", report->event_span_sha256);
    for (i = 0; i < 16; i += 4) {
        pspDebugScreenPrintf("ky_flg[%02u..%02u] %08X %08X %08X %08X\n",
                             i, i + 3, (unsigned int)report->event_flags[i],
                             (unsigned int)report->event_flags[i + 1],
                             (unsigned int)report->event_flags[i + 2],
                             (unsigned int)report->event_flags[i + 3]);
    }
}

int main(void)
{
    RecvxRoomData room = {0};
    RecvxRoomFileInfo info;
    RecvxEngineEventTestReport report;
    SceCtrlData input;
    int load_result;
    int run_result = -1;

    pspDebugScreenInit();
    pspDebugScreenClear();
    pspDebugScreenPrintf("RECVX original event-core test\n");
    pspDebugScreenPrintf("Isolated from the walking viewer\n\n");
    load_result = recvxLoadNamedRoomData("data", "RM_0000.RDX",
                                         &info, &room);
    if (load_result != 0) {
        pspDebugScreenPrintf("FAIL: RM_0000 data load (%d)\n", load_result);
        pspDebugScreenPrintf("Build data from your local ISO with\n");
        pspDebugScreenPrintf("make engine-event-test-assets ISO=<path>\n");
    } else {
        run_result = recvxEngineEventTestsRun(room.bytes, room.size, &report);
        print_report(&report);
        if (run_result != 0 || !report.passed || !report.original_event_ran)
            pspDebugScreenPrintf("\nTEST FAILED\n");
        else
            pspDebugScreenPrintf("\nTEST PASSED\n");
    }
    pspDebugScreenPrintf("\nPress START to exit\n");
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    for (;;) {
        sceCtrlReadBufferPositive(&input, 1);
        if ((input.Buttons & PSP_CTRL_START) != 0)
            break;
        sceDisplayWaitVblankStart();
    }
    recvxRoomDataFree(&room);
    sceKernelExitGame();
    return 0;
}
