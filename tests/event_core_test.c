#include <stdio.h>
#include <string.h>

#include "psp/engine_event_test.h"
#include "psp/room.h"

static void print_report(const RecvxEngineEventTestReport *report)
{
    unsigned int i;

    if (!report->passed) {
        printf("FAIL: %s: %s\n", report->stage, report->detail);
        if (report->fault.code != RECVX_EVENT_FAULT_NONE) {
            printf("fault %s task %u event %u pc %08x opcode %02x\n",
                   recvxEventCoreFaultName(report->fault.code),
                   report->fault.task, report->fault.event_id,
                   report->fault.section_pc, report->fault.opcode);
        }
        return;
    }
    printf("PASS: original event core contracts (%u assertions)\n",
           report->assertions_passed);
    if (!report->original_event_ran) {
        printf("SKIP: RM_0000 event 38; supply a data directory from the local ISO\n");
        return;
    }
    printf("PASS: RM_0000 event 38, %u writes, %u instructions, timer %u\n",
           report->event_flag_writes, report->event_instructions,
           report->event_timer);
    printf("span SHA-256 %s, body PC %04x, section PC %08x\n",
           report->event_span_sha256, report->event_pc_body,
           report->event_pc_section);
    for (i = 0; i < 16; i += 4) {
        printf("ky_flg[%02u..%02u] %08x %08x %08x %08x\n",
               i, i + 3, report->event_flags[i], report->event_flags[i + 1],
               report->event_flags[i + 2], report->event_flags[i + 3]);
    }
}

int main(int argc, char **argv)
{
    RecvxRoomData room = {0};
    RecvxRoomFileInfo info;
    RecvxEngineEventTestReport report;
    int room_result = 0;
    int result;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [data-directory]\n", argv[0]);
        return 2;
    }
    if (argc == 2) {
        room_result = recvxLoadNamedRoomData(argv[1], "RM_0000.RDX",
                                             &info, &room);
        if (room_result != 0) {
            fprintf(stderr, "FAIL: could not load local RM_0000 data (%d)\n",
                    room_result);
            return 1;
        }
    }
    result = recvxEngineEventTestsRun(room.bytes, room.size, &report);
    print_report(&report);
    recvxRoomDataFree(&room);
    if (result != 0 || !report.passed)
        return 1;
    return 0;
}
