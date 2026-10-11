#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/engine_room_owner.h"

typedef struct TestAllocator {
    unsigned int allocations;
    unsigned int releases;
    unsigned int fail_at;
} TestAllocator;

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

static uint16_t read_u16(const unsigned char *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int fail(const char *stage, int result)
{
    fprintf(stderr, "FAIL: %s (%d: %s)\n", stage, result,
            recvxEngineRoomResultName(result));
    return 1;
}

static int check_allocation_failures(const char *directory)
{
    unsigned int fail_at;

    for (fail_at = 1; fail_at <= 2; ++fail_at) {
        RecvxEngineRoomOwner owner = {0};
        TestAllocator state = {0, 0, fail_at};
        RecvxRoomAllocator allocator = {&state, test_allocate, test_release};
        int result = recvxEngineRoomOwnerOpenWithAllocator(
            &owner, directory, "RM_0010.RDX", &allocator);
        if (result != 4)
            return fail("allocation failure result", result);
        if (owner.resident || owner.data.bytes != NULL || owner.pins != 0)
            return fail("allocation failure left partial ownership", result);
        if (state.allocations != fail_at || state.releases != fail_at - 1)
            return fail("allocation failure cleanup count", result);
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const uint32_t expected_counts[RECVX_ROOM_COUNT_TOTAL] = {
        3, 9, 1, 5, 2, 1, 31, 5, 2, 2, 0, 0, 3, 1
    };
    RecvxEngineRoomOwner owner = {0};
    RecvxEngineRoomView view, stale_view;
    RecvxEngineRoomPlacement placement;
    uint32_t generation = 0;
    size_t script_size;
    unsigned int cycle, kind, i;

    if (argc != 2)
        return fail("usage: engine-room-owner-test <asset directory>", argc);
    if (check_allocation_failures(argv[1]) != 0)
        return 1;
    if (recvxEngineRoomOwnerOpen(&owner, "build/host/no-room", "RM_0010.RDX") != 1)
        return fail("read failure result", -1);

    for (cycle = 0; cycle < 10; ++cycle) {
        int result = recvxEngineRoomOwnerOpen(&owner, argv[1], "RM_0010.RDX");
        if (result != RECVX_ENGINE_ROOM_OK)
            return fail("RM_0010 owner open", result);
        if (owner.file.expanded_size != 1829316 ||
            memcmp(owner.file.name, "RM_0010.RDX", 11) != 0 ||
            memcmp(owner.file.room.counts, expected_counts,
                   sizeof(expected_counts)) != 0 ||
            owner.active_enemy_count != 1 || owner.model_reference_count != 5)
            return fail("validated room counts or model references", -1);
        if (owner.generation <= generation)
            return fail("room generation did not advance", -1);
        generation = owner.generation;
        if (recvxEngineRoomOwnerView(&owner, &view) != RECVX_ENGINE_ROOM_OK)
            return fail("room view creation", -1);
        if (recvxEngineRoomViewPin(&view) != RECVX_ENGINE_ROOM_OK)
            return fail("room view pin", -1);
        if (recvxEngineRoomOwnerClose(&owner) != RECVX_ENGINE_ROOM_BUSY)
            return fail("close with active room user", -1);

        for (kind = 0; kind < RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT; ++kind) {
            static const unsigned int array_slots[] = {
                RECVX_ROOM_ARRAY_ENEMY,
                RECVX_ROOM_ARRAY_OBJECT,
                RECVX_ROOM_ARRAY_ITEM
            };
            uint32_t array_offset = read_u32(owner.data.bytes +
                owner.file.room.rom_offset +
                RECVX_ROOM_ARRAY_DISK_OFFSET(array_slots[kind]));
            for (i = 0; i < owner.placement_counts[kind]; ++i) {
                const unsigned char *original = owner.data.bytes + array_offset +
                    i * sizeof(RecvxRoomDiskActorPlacement);
                if (recvxEngineRoomViewPlacement(&view,
                        (enum RecvxEngineRoomPlacementKind)kind, i,
                        &placement) != RECVX_ENGINE_ROOM_OK ||
                    placement.placement_index != i ||
                    placement.flags != read_u32(original) ||
                    placement.id != read_u16(original + 4) ||
                    placement.type != read_u16(original + 6) ||
                    placement.work_no != original[10])
                    return fail("placement identity/work index view", -1);
            }
            if (recvxEngineRoomViewPlacement(&view,
                    (enum RecvxEngineRoomPlacementKind)kind,
                    owner.placement_counts[kind], &placement) !=
                    RECVX_ENGINE_ROOM_BAD_ARGUMENT)
                return fail("placement bounds", -1);
        }
        for (i = 0; i < owner.events.script_count; ++i) {
            const unsigned char *script = recvxEngineRoomViewEventScript(
                &view, i, &script_size);
            if (script == NULL || script_size == 0 ||
                script < owner.data.bytes ||
                script + script_size > owner.data.bytes + owner.data.size)
                return fail("event script range", -1);
        }
        if (owner.events.script_count != 3 ||
            recvxEngineRoomViewBytes(&view, (uint32_t)owner.data.size, 1) != NULL)
            return fail("event table count or byte range", -1);

        stale_view = view;
        if (recvxEngineRoomViewUnpin(&view) != RECVX_ENGINE_ROOM_OK ||
            recvxEngineRoomOwnerClose(&owner) != RECVX_ENGINE_ROOM_OK)
            return fail("room release", -1);
        if (recvxEngineRoomViewBytes(&stale_view, 0, 1) != NULL ||
            recvxEngineRoomViewEventScript(&stale_view, 0, &script_size) != NULL)
            return fail("stale room view accepted", -1);
    }

    puts("PASS: room generations, bounded event/placement views, read/allocation failures and 10 load/release cycles");
    return 0;
}
