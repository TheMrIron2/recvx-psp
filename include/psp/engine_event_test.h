#ifndef RECVX_PSP_ENGINE_EVENT_TEST_H
#define RECVX_PSP_ENGINE_EVENT_TEST_H

#include <stddef.h>
#include <stdint.h>

#include "psp/engine_event_core.h"

typedef struct RecvxEngineEventTestReport {
    int passed;
    int original_event_ran;
    uint32_t assertions_passed;
    uint32_t event_instructions;
    uint32_t event_flag_writes;
    uint32_t event_timer;
    uint32_t event_pc_section;
    uint32_t event_pc_body;
    uint32_t event_flags[16];
    char event_span_sha256[65];
    char stage[40];
    char detail[160];
    RecvxEventCoreFault fault;
} RecvxEngineEventTestReport;

int recvxEngineEventTestsRun(const void *expanded_room, size_t room_size,
                             RecvxEngineEventTestReport *report);

#endif
