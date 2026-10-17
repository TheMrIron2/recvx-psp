#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sdfunc.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
unsigned char *freemem;
static jmp_buf abort_read;
static char fault[512];
void recvxGameBootFilesInit(const char *directory);
void recvxGameBootLog(const char *format, ...) { (void)format; }
void recvxGameBootUnsupported(const char *name) { snprintf(fault, sizeof(fault), "%s", name); longjmp(abort_read, 1); }
#define REJECT(call, message) do { fault[0] = 0; \
    if (!setjmp(abort_read)) { call; assert(!"expected file loader fault"); } \
    assert(fault[0] && strstr(fault, message) && GetReadFileStatus() == -1); } while (0)

int main(int argc, char **argv)
{
    static const unsigned int entries[] = {114, 0, 0, 0, 145, 71, 4};
    assert(argc == 2);
    freemem = malloc(8u * 1024u * 1024u); assert(freemem);
    sys->endp = freemem + 8u * 1024u * 1024u;
    recvxGameBootFilesInit(argv[1]);
    for (unsigned int partition = 0; partition < 7; ++partition) {
        int size = GetInsideFileSize(partition, entries[partition]);
        assert(size > 0 && size <= 8 * 1024 * 1024);
        assert(RequestReadInsideFile(partition, entries[partition], freemem) == 0);
        assert(GetReadFileStatus() == 0 && recvxGameBootSourceRemaining(freemem) == (size_t)size);
    }
    REJECT(GetInsideFileSize(7, 3459), "partition=7 entry=3459");
    REJECT(GetInsideFileSize(4, 9999), "archives/ITEM1/9999.bin");
    REJECT(RequestReadInsideFile(4, 145, sys->endp - 4), "exceeds original arena");
    free(freemem);
    puts("PASS: all seven original archive partitions, actual inventory/door resource reads, registered bounds, unknown partition/entry diagnostics and arena overflow rejection");
    return 0;
}
