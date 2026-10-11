#ifndef RECVX_PSP_LEGACY_RUNTIME_H
#define RECVX_PSP_LEGACY_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "psp/event_data.h"

enum RecvxLegacyScriptStatus {
    RECVX_LEGACY_SCRIPT_COMPLETE,
    RECVX_LEGACY_SCRIPT_YIELDED,
    RECVX_LEGACY_SCRIPT_UNSUPPORTED,
    RECVX_LEGACY_SCRIPT_INVALID,
    RECVX_LEGACY_SCRIPT_LIMIT
};

typedef struct RecvxLegacyScriptResult {
    enum RecvxLegacyScriptStatus status;
    uint32_t instructions;
    uint32_t writes;
    uint32_t events_queued;
    uint32_t stop_offset;
    uint8_t stop_opcode;
    uint8_t last_event_task;
    uint8_t last_event_id;
    uint16_t last_event_bytes;
} RecvxLegacyScriptResult;

typedef struct RecvxLegacyEventTask {
    uint8_t active;
    uint8_t event_id;
    uint16_t script_index;
    uint16_t script_bytes;
} RecvxLegacyEventTask;

int recvxLegacyRuntimePrepare(void);
int recvxLegacySystemInit(void);
void recvxLegacyRuntimeShutdown(void);
int recvxLegacyRuntimeReady(void);
uint32_t recvxLegacyTaskMask(void);
uint32_t recvxLegacyPausedTaskMask(void);
size_t recvxLegacyArenaUsed(void);
void recvxLegacyVsyncTick(void);
int recvxLegacyGetClip(float *near_plane, float *far_plane);
void recvxLegacyEventReset(void);
void recvxLegacyModelVisibilitySet(unsigned int type, unsigned int index,
                                  int visible);
int recvxLegacyModelVisibilityGet(unsigned int type, unsigned int index);
int recvxLegacyEventStart(unsigned int task_level, unsigned int event_id,
                          unsigned int script_index, size_t script_bytes);
const RecvxLegacyEventTask *recvxLegacyEventGet(unsigned int task_level);
RecvxLegacyScriptResult recvxLegacyScenarioRun(const unsigned char *script,
                                               size_t script_size,
                                               const RecvxEventData *events);

#endif
