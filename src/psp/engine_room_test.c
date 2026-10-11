#include <malloc.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/engine_event_test.h"
#include "psp/engine_room_owner.h"

PSP_MODULE_INFO("RECVX ENGINE ROOM TEST", PSP_MODULE_USER, 0, 6);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024);

#define ROOM_TEST_CYCLES 10
#define LARGE_HEAP_PROBE_SIZE (18u * 1024u * 1024u + 4096u)

typedef struct TestAllocator {
    unsigned int allocations;
    unsigned int releases;
    unsigned int fail_at;
} TestAllocator;

typedef struct MemorySnapshot {
    SceSize kernel_total;
    SceSize kernel_largest;
    struct mallinfo libc;
} MemorySnapshot;

static void *test_allocate(void *context, size_t size)
{
    TestAllocator *state = context;
    ++state->allocations;
    if (state->allocations == state->fail_at)
        return NULL;
    return malloc(size);
}

static void test_release(void *context, void *memory)
{
    TestAllocator *state = context;
    ++state->releases;
    free(memory);
}

static MemorySnapshot memory_snapshot(void)
{
    MemorySnapshot snapshot;
    snapshot.kernel_total = sceKernelTotalFreeMemSize();
    snapshot.kernel_largest = sceKernelMaxFreeMemSize();
    snapshot.libc = mallinfo();
    return snapshot;
}

static void print_memory(const char *label, const MemorySnapshot *snapshot)
{
    pspDebugScreenPrintf("%s kernel free total/largest %u/%u KiB\n", label,
        (unsigned int)(snapshot->kernel_total / 1024),
        (unsigned int)(snapshot->kernel_largest / 1024));
    pspDebugScreenPrintf("libc arena/used/free %u/%u/%u KiB\n",
        (unsigned int)(snapshot->libc.arena / 1024),
        (unsigned int)(snapshot->libc.uordblks / 1024),
        (unsigned int)(snapshot->libc.fordblks / 1024));
}

static int check_heap_probe(void)
{
    volatile unsigned char *memory;
    size_t offset;

    memory = malloc(LARGE_HEAP_PROBE_SIZE);
    if (memory == NULL)
        return -1;
    for (offset = 0; offset < LARGE_HEAP_PROBE_SIZE; offset += 4096)
        memory[offset] = (unsigned char)(offset >> 12);
    memory[LARGE_HEAP_PROBE_SIZE - 1] = 0x5a;
    if (memory[LARGE_HEAP_PROBE_SIZE - 1] != 0x5a) {
        free((void *)memory);
        return -1;
    }
    free((void *)memory);
    return 0;
}

static int check_owner_failures(const char *directory, char *detail,
                                size_t detail_capacity)
{
    unsigned int fail_at;
    RecvxEngineRoomOwner owner = {0};
    int result;

    result = recvxEngineRoomOwnerOpen(&owner, "data/no-room", "RM_0010.RDX");
    if (result != 1) {
        snprintf(detail, detail_capacity, "missing-room read returned %d", result);
        return -1;
    }
    for (fail_at = 1; fail_at <= 2; ++fail_at) {
        TestAllocator state = {0, 0, fail_at};
        RecvxRoomAllocator allocator = {&state, test_allocate, test_release};
        result = recvxEngineRoomOwnerOpenWithAllocator(
            &owner, directory, "RM_0010.RDX", &allocator);
        if (result != 4 || owner.resident || owner.data.bytes != NULL ||
            state.allocations != fail_at || state.releases != fail_at - 1) {
            snprintf(detail, detail_capacity,
                     "allocation %u returned %d, alloc/free %u/%u", fail_at,
                     result, state.allocations, state.releases);
            return -1;
        }
    }
    return 0;
}

static int check_room_cycles(const char *directory, uint32_t *generation_out,
                             uint32_t *resident_bytes_out, char *detail,
                             size_t detail_capacity)
{
    RecvxEngineRoomOwner owner = {0};
    RecvxEngineRoomView view;
    RecvxEngineRoomPlacement placement;
    struct mallinfo released_heap = {0};
    unsigned int cycle, i;
    int have_baseline = 0;

    for (cycle = 0; cycle < ROOM_TEST_CYCLES; ++cycle) {
        int result = recvxEngineRoomOwnerOpen(&owner, directory, "RM_0010.RDX");
        uint32_t current_generation;
        uint32_t script_count;
        uint32_t expanded_size;
        size_t script_size;
        if (result != RECVX_ENGINE_ROOM_OK) {
            snprintf(detail, detail_capacity, "room load: %s",
                     recvxEngineRoomResultName(result));
            return result;
        }
        if (owner.file.expanded_size != 1829316 || owner.active_enemy_count != 1 ||
            owner.placement_counts[RECVX_ENGINE_ROOM_OBJECT] != 5 ||
            owner.placement_counts[RECVX_ENGINE_ROOM_ITEM] != 2 ||
            owner.model_reference_count != 5 || owner.events.script_count != 3) {
            snprintf(detail, detail_capacity, "RM_0010 counts changed");
            return -1;
        }
        if (recvxEngineRoomOwnerView(&owner, &view) != RECVX_ENGINE_ROOM_OK ||
            recvxEngineRoomViewPin(&view) != RECVX_ENGINE_ROOM_OK ||
            recvxEngineRoomOwnerClose(&owner) != RECVX_ENGINE_ROOM_BUSY) {
            snprintf(detail, detail_capacity, "room pin did not block close");
            return -1;
        }
        if (recvxEngineRoomViewPlacement(&view, RECVX_ENGINE_ROOM_OBJECT, 4,
                                         &placement) != RECVX_ENGINE_ROOM_OK ||
            placement.placement_index != 4) {
            snprintf(detail, detail_capacity, "object placement index changed");
            return -1;
        }
        for (i = 0; i < owner.events.script_count; ++i) {
            const unsigned char *script = recvxEngineRoomViewEventScript(
                &view, i, &script_size);
            if (script == NULL || script_size == 0 ||
                script < owner.data.bytes ||
                script + script_size > owner.data.bytes + owner.data.size) {
                snprintf(detail, detail_capacity, "event script %u out of range", i);
                return -1;
            }
        }
        current_generation = owner.generation;
        script_count = owner.events.script_count;
        expanded_size = (uint32_t)owner.data.size;
        if (recvxEngineRoomViewUnpin(&view) != RECVX_ENGINE_ROOM_OK ||
            recvxEngineRoomOwnerClose(&owner) != RECVX_ENGINE_ROOM_OK) {
            snprintf(detail, detail_capacity, "room release failed");
            return -1;
        }
        if (!have_baseline) {
            released_heap = mallinfo();
            have_baseline = 1;
        } else if (mallinfo().arena != released_heap.arena ||
                   mallinfo().uordblks != released_heap.uordblks) {
            snprintf(detail, detail_capacity, "libc heap grew after cycle %u",
                     cycle + 1);
            return -1;
        }
        if (cycle == 0) {
            *generation_out = current_generation;
            *resident_bytes_out = expanded_size;
        } else if (view.generation <= *generation_out) {
            snprintf(detail, detail_capacity, "room generation did not advance");
            return -1;
        } else {
            *generation_out = current_generation;
        }
        if (script_count != 3 || recvxEngineRoomViewBytes(&view, 0, 1) != NULL) {
            snprintf(detail, detail_capacity, "closed generation still resolved");
            return -1;
        }
    }
    return RECVX_ENGINE_ROOM_OK;
}

static void print_event_report(const RecvxEngineEventTestReport *report)
{
    unsigned int i;
    if (!report->passed || !report->original_event_ran) {
        pspDebugScreenPrintf("FAIL event: %s %s\n", report->stage,
                             report->detail);
        if (report->fault.code != RECVX_EVENT_FAULT_NONE)
            pspDebugScreenPrintf("Fault %s PC %08X op %02X\n",
                recvxEventCoreFaultName(report->fault.code),
                (unsigned int)report->fault.section_pc, report->fault.opcode);
        return;
    }
    pspDebugScreenPrintf("Event 38: %u writes, %u ops, timer %u\n",
        (unsigned int)report->event_flag_writes,
        (unsigned int)report->event_instructions,
        (unsigned int)report->event_timer);
    pspDebugScreenPrintf("Task 7 ended at body PC %04X\n",
                         (unsigned int)report->event_pc_body);
    for (i = 0; i < 16; i += 4)
        pspDebugScreenPrintf("flags[%02u] %08X %08X %08X %08X\n", i,
            (unsigned int)report->event_flags[i],
            (unsigned int)report->event_flags[i + 1],
            (unsigned int)report->event_flags[i + 2],
            (unsigned int)report->event_flags[i + 3]);
}

int main(void)
{
    RecvxEngineEventTestReport event_report;
    RecvxRoomData event_room = {0};
    RecvxRoomFileInfo event_info;
    RecvxEngineRoomOwner owner = {0};
    MemorySnapshot initial_memory, loaded_memory, released_memory;
    SceCtrlData input;
    uint32_t room_generation = 0, room_bytes = 0;
    int event_load_result, event_result = -1;
    int owner_result = RECVX_ENGINE_ROOM_BAD_ARGUMENT;
    int fault_tests_result = -1;
    int heap_probe_result = -1;
    int passed = 0;
    char failure_detail[96] = "";

    pspDebugScreenInit();
    pspDebugScreenClear();
    pspDebugScreenPrintf("RECVX original event + room owner\n");
    pspDebugScreenPrintf("engine-64 MEMSIZE=1 heap=-1 reserve=4096 KiB\n");
    initial_memory = memory_snapshot();
    print_memory("startup", &initial_memory);
    if (initial_memory.kernel_largest < 4u * 1024u * 1024u) {
        pspDebugScreenPrintf("FAIL: 4 MiB external reserve unavailable\n");
        goto done;
    }
    heap_probe_result = check_heap_probe();
    if (heap_probe_result != 0) {
        pspDebugScreenPrintf("FAIL: 18 MiB+ heap probe\n");
        goto done;
    }
    pspDebugScreenPrintf("PASS: touched 18 MiB + 4 KiB heap\n");

    event_load_result = recvxLoadNamedRoomData("data", "RM_0000.RDX",
                                                &event_info, &event_room);
    if (event_load_result != 0) {
        pspDebugScreenPrintf("FAIL: RM_0000 read (%s)\n",
                             recvxEngineRoomResultName(event_load_result));
        pspDebugScreenPrintf("Build ISO data with engine-64-assets\n");
        goto done;
    }
    event_result = recvxEngineEventTestsRun(event_room.bytes, event_room.size,
                                             &event_report);
    print_event_report(&event_report);
    recvxRoomDataFree(&event_room);
    if (event_result != 0 || !event_report.passed ||
        !event_report.original_event_ran) {
        pspDebugScreenPrintf("FAIL: original event core checks\n");
        goto done;
    }

    fault_tests_result = check_owner_failures("data", failure_detail,
                                              sizeof(failure_detail));
    if (fault_tests_result != 0) {
        pspDebugScreenPrintf("FAIL: %s\n", failure_detail);
        goto done;
    }
    owner_result = recvxEngineRoomOwnerOpen(&owner, "data", "RM_0010.RDX");
    if (owner_result != RECVX_ENGINE_ROOM_OK) {
        pspDebugScreenPrintf("FAIL: RM_0010 (%s)\n",
                             recvxEngineRoomResultName(owner_result));
        goto done;
    }
    loaded_memory = memory_snapshot();
    pspDebugScreenPrintf("PASS: RM_0010 %u bytes\n",
                         (unsigned int)owner.data.size);
    pspDebugScreenPrintf("actors en %u obj %u item %u models %u; events %u\n",
        (unsigned int)owner.active_enemy_count,
        (unsigned int)owner.placement_counts[RECVX_ENGINE_ROOM_OBJECT],
        (unsigned int)owner.placement_counts[RECVX_ENGINE_ROOM_ITEM],
        (unsigned int)owner.model_reference_count,
        (unsigned int)owner.events.script_count);
    if (recvxEngineRoomOwnerClose(&owner) != RECVX_ENGINE_ROOM_OK) {
        pspDebugScreenPrintf("FAIL: initial room release\n");
        goto done;
    }
    released_memory = memory_snapshot();
    print_memory("room resident", &loaded_memory);
    print_memory("room released", &released_memory);
    owner_result = check_room_cycles("data", &room_generation, &room_bytes,
                                     failure_detail, sizeof(failure_detail));
    if (owner_result != RECVX_ENGINE_ROOM_OK) {
        if (owner_result > 0)
            pspDebugScreenPrintf("FAIL: room owner (%s)\n",
                                 recvxEngineRoomResultName(owner_result));
        else
            pspDebugScreenPrintf("FAIL: %s\n", failure_detail);
        goto done;
    }
    pspDebugScreenPrintf("PASS: %u cycles, %u bytes, gen %u\n",
                         ROOM_TEST_CYCLES, (unsigned int)room_bytes,
                         (unsigned int)room_generation);
    pspDebugScreenPrintf("PASS: read fault + 2 allocation fault cleanups\n");
    passed = 1;

done:
    recvxRoomDataFree(&event_room);
    if (owner.resident && owner.pins == 0)
        recvxEngineRoomOwnerClose(&owner);
    if (passed)
        pspDebugScreenPrintf("\nTEST PASSED\n");
    else
        pspDebugScreenPrintf("\nTEST FAILED\n");
    pspDebugScreenPrintf("Press START to exit\n");
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    for (;;) {
        sceCtrlReadBufferPositive(&input, 1);
        if ((input.Buttons & PSP_CTRL_START) != 0)
            break;
        sceDisplayWaitVblankStart();
    }
    sceKernelExitGame();
    return passed ? 0 : 1;
}
