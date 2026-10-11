#ifndef RECVX_PSP_ENGINE_EVENT_CORE_H
#define RECVX_PSP_ENGINE_EVENT_CORE_H

#include <stddef.h>
#include <stdint.h>

enum RecvxEventCoreFaultCode {
    RECVX_EVENT_FAULT_NONE,
    RECVX_EVENT_FAULT_BAD_SECTION,
    RECVX_EVENT_FAULT_BAD_EVENT,
    RECVX_EVENT_FAULT_BAD_TASK,
    RECVX_EVENT_FAULT_PC_RANGE,
    RECVX_EVENT_FAULT_TRUNCATED,
    RECVX_EVENT_FAULT_BAD_OPERAND,
    RECVX_EVENT_FAULT_UNSUPPORTED,
    RECVX_EVENT_FAULT_WATCHDOG,
    RECVX_EVENT_FAULT_ACTIVE_TASKS
};

typedef struct RecvxEventCoreFault {
    enum RecvxEventCoreFaultCode code;
    uint32_t section_pc;
    uint32_t event_id;
    uint16_t task;
    uint8_t opcode;
} RecvxEventCoreFault;

void recvxEventCoreReset(void);
int recvxEventCoreBindSection(const void *bytes, size_t size);
int recvxEventCoreUnbindSection(void);
int recvxEventCoreRunScenario(uint32_t section_offset);
const RecvxEventCoreFault *recvxEventCoreGetFault(void);
const char *recvxEventCoreFaultName(enum RecvxEventCoreFaultCode code);
uint32_t recvxEventCoreTaskPc(unsigned int task);
uint32_t recvxEventCoreTaskStart(unsigned int task);
uint32_t recvxEventCoreTaskBodyPc(unsigned int task);
uint32_t recvxEventCoreInstructionCount(void);
uint32_t recvxEventCoreFlagWriteCount(void);

#endif
