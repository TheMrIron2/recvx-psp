#include <stddef.h>
#include <stdint.h>

#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/room.h"
#include "psp/engine_event_core.h"
#include "psp/engine_original_event_test.h"
#include "psp/engine_original_fault.h"

enum {
    RECVX_ORIGINAL_EVENT_UNBOUND,
    RECVX_ORIGINAL_EVENT_BOUND,
    RECVX_ORIGINAL_EVENT_STARTED
};

static int event_test_state;
static uint32_t scenario0_offset;
static uint32_t scenario1_offset;
static uint32_t event0_offset;

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int core_failed(void)
{
    const RecvxEventCoreFault *fault = recvxEventCoreGetFault();
    return fault != NULL && fault->code != RECVX_EVENT_FAULT_NONE;
}

void recvxEngineOriginalEventTestReset(void)
{
    event_test_state = RECVX_ORIGINAL_EVENT_UNBOUND;
    scenario0_offset = 0;
    scenario1_offset = 0;
    event0_offset = 0;
}

int recvxEngineOriginalEventTestBindRoom(void)
{
    const unsigned char *room;
    const unsigned char *section;
    uint32_t event_offset, event_end, table_size;
    size_t room_size;

    recvxEngineOriginalEventTestReset();
    if (sys == NULL || rom == NULL || sys->rdtp == NULL ||
        rom->evtp == NULL || sys->rdtsz < 36) {
        recvxEngineOriginalFaultSet("original event bind missing room state");
        return -1;
    }
    room = sys->rdtp;
    room_size = (size_t)sys->rdtsz;
    event_offset = read_u32(room + 28);
    event_end = read_u32(room + 32);
    if (event_offset < 36 || event_end <= event_offset || event_end > room_size ||
        (const unsigned char *)rom->evtp != room + event_offset) {
        recvxEngineOriginalFaultSet("original event section outside owned room");
        return -2;
    }
    section = room + event_offset;
    table_size = read_u32(section);
    scenario0_offset = read_u32(section);
    scenario1_offset = read_u32(section + 4);
    event0_offset = read_u32(section + 8);
    if (table_size != sizeof(EVT_WORK) || scenario0_offset != table_size ||
        scenario1_offset <= scenario0_offset ||
        event0_offset <= scenario1_offset || event0_offset >= event_end - event_offset) {
        recvxEngineOriginalFaultSet("RM_0010 event offsets are invalid");
        return -3;
    }
    recvxEventCoreReset();
    if (recvxEventCoreBindSection(section, event_end - event_offset) != 0) {
        recvxEngineOriginalFaultSet("original event section bind rejected");
        return -4;
    }
    event_test_state = RECVX_ORIGINAL_EVENT_BOUND;
    return 0;
}

int recvxEngineOriginalEventTestStart(void)
{
    if (event_test_state != RECVX_ORIGINAL_EVENT_BOUND) {
        recvxEngineOriginalFaultSet("original event started before section bind");
        return -1;
    }
    bhInitEvent();
    if (recvxEngineOriginalFault() != NULL || core_failed())
        return -2;
    if ((uint32_t)(uintptr_t)bhScdPtr != scenario0_offset ||
        (uint32_t)(uintptr_t)bhScd2Ptr != scenario1_offset ||
        (uint32_t)(uintptr_t)bhEvdPtr != event0_offset) {
        recvxEngineOriginalFaultSet("original event pointers disagree with room header");
        return -3;
    }
    event_test_state = RECVX_ORIGINAL_EVENT_STARTED;
    return 0;
}

int recvxEngineOriginalEventTestControl(void)
{
    if (event_test_state != RECVX_ORIGINAL_EVENT_STARTED) {
        recvxEngineOriginalFaultSet("original event callback before initialization");
        return -1;
    }
    bhControlEvent();
    return recvxEngineOriginalFault() == NULL && !core_failed() ? 0 : -2;
}
