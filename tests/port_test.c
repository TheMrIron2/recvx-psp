#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/expand.h"
#include "psp/room.h"
#include "psp/scene.h"
#include "psp/selftest.h"

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/* Exact-sized allocations let ASan catch reads beyond a truncated input.
 * Guards catch writes outside the declared output capacity, including errors.
 */
static void check_malformed_streams(void)
{
    uint32_t state = 0x20184;
    unsigned int test;

    for (test = 0; test < 20000; ++test) {
        size_t source_size = next_random(&state) >> 26;
        size_t capacity = next_random(&state) >> 24;
        unsigned char *source = malloc(source_size ? source_size : 1);
        unsigned char *output = malloc(capacity + 2);
        size_t consumed, written, i;
        RecvxExpandResult result;
        assert(source != NULL && output != NULL);
        for (i = 0; i < source_size; ++i)
            source[i] = next_random(&state) >> 24;
        memset(output, 0xa5, capacity + 2);
        result = recvxExpandBounded(source, source_size, output + 1, capacity,
                                   &consumed, &written);
        assert(result <= RECVX_EXPAND_OK && result >= RECVX_EXPAND_BAD_REFERENCE);
        assert(consumed <= source_size && written <= capacity);
        assert(output[0] == 0xa5 && output[capacity + 1] == 0xa5);
        free(output);
        free(source);
    }
    puts("Malformed input under ASan/UBSan: PASS (20000 streams)");
}

int main(int argc, char **argv)
{
    int flags = recvxCheckFlags();
    int memory = recvxCheckMemory();
    int expand = recvxCheckExpand();
    printf("Game flags: %s (check %d)\n", flags ? "FAIL" : "PASS", flags);
    printf("Ninja memory copies: %s (check %d)\n", memory ? "FAIL" : "PASS", memory);
    printf("Portable decompression: %s (check %d)\n", expand ? "FAIL" : "PASS", expand);
    check_malformed_streams();
    assert(recvxRoomCrc32("123456789", 9) == 0xcbf43926u);
    assert(recvxRoomCrc32(NULL, 0) == 0);
    if (argc > 1) {
        RecvxRoomFileInfo info;
        unsigned char *data = NULL;
        RecvxScene scene = {0};
        int room = recvxLoadRoomFile(argv[1], &info, &data);
        printf("Real room %s: %s (check %d), %u -> %u bytes\n", info.name,
               room ? "FAIL" : "PASS", room, info.packed_size, info.expanded_size);
        if (room)
            return 1;
        assert(recvxBuildRoomScene(data, info.expanded_size, &scene) == 0);
        free(data);
        printf("Room spatial scene: PASS (%u bounds, %u markers, %u vertices)\n",
               scene.box_count, scene.marker_count, scene.vertex_count);
        assert(((uintptr_t)scene.vertices & 15) == 0);
        recvxSceneFree(&scene);
    }
    return flags || memory || expand;
}
