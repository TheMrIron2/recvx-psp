#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sdfunc.h"

static char data_directory[384];
static int read_status;
static struct { const unsigned char *address; size_t size; } ranges[16];
static unsigned int range_cursor;

void recvxGameBootFilesInit(const char *directory)
{
    snprintf(data_directory, sizeof(data_directory), "%s", directory);
    memset(ranges, 0, sizeof(ranges)); range_cursor = 0; read_status = 0;
}

size_t recvxGameBootSourceRemaining(const void *pointer)
{
    uintptr_t p = (uintptr_t)pointer;
    unsigned int i;
    /* Newest read wins when the ADV arena reuses an earlier allocation. */
    for (i = 0; i < 16; ++i) {
        unsigned int slot = (range_cursor - 1u - i) & 15u;
        uintptr_t start = (uintptr_t)ranges[slot].address;
        if (ranges[slot].address && p >= start && p - start < ranges[slot].size)
            return ranges[slot].size - (size_t)(p - start);
    }
    return 0;
}

void recvxGameBootMessageCheck(int type, int index)
{
    const unsigned char *text;
    size_t remaining, cursor = 0;
    if (type == 0 || type == 1) {
        const unsigned char *table = (const unsigned char *)(type == 0 ? rom->mesp : sys->mes_sp);
        uint32_t words[2], offset;
        remaining = recvxGameBootSourceRemaining(table);
        if (!table || remaining < 4 || index < 0 || (size_t)index + 1 >= remaining / 4)
            recvxGameBootUnsupported("message index exceeds loaded table");
        memcpy(words, table, 4);
        if (words[0] > remaining / 4 - 1 || (unsigned int)index >= words[0])
            recvxGameBootUnsupported("message index exceeds authored count");
        memcpy(&offset, table + ((size_t)index + 1) * 4, 4);
        if ((offset & 1) || offset < ((size_t)words[0] + 1) * 4 || offset >= remaining)
            recvxGameBootUnsupported("message offset exceeds loaded table");
        text = table + offset; remaining -= offset;
    } else {
        text = (const unsigned char *)sys->mes_tp;
        if (!text) recvxGameBootUnsupported("missing direct message text");
        remaining = recvxGameBootSourceRemaining(text);
        /* Original compiled/temporary direct strings have no file range.
           Their callers own the memory; still bound the control-word scan. */
        if (!remaining) remaining = 1024;
    }
    while (cursor + 2 <= remaining && cursor < 1024) {
        unsigned int code = text[cursor] | ((unsigned int)text[cursor + 1] << 8);
        cursor += 2;
        if (code == 65535) return;
        if (code == 65282 || code == 65283) {
            if (cursor + 2 > remaining) break;
            cursor += 2;
        } else if (code == 65280 || code == 65281 || code == 65284 ||
                   (code >= 65024 && code <= 65039)) {
            continue;
        } else if (code >= 4 * 324u)
            recvxGameBootUnsupported("message glyph exceeds system font pages");
    }
    recvxGameBootUnsupported("message terminator missing within loaded text");
}

static void inside_path(char *path, size_t size, unsigned int partition, unsigned int id)
{
    static const char *const archives[] = {"BGM1", "VOICE1", "MULTSPQ1", "ADV", "ITEM1", "MRY", "SYSTEM"};
    if (partition >= sizeof(archives) / sizeof(archives[0])) {
        read_status = -1;
        snprintf(path, size, "asset archive partition is not packaged: partition=%u entry=%u", partition, id);
        recvxGameBootUnsupported(path);
    }
    const char *archive = archives[partition];
    if (snprintf(path, size, "%s/archives/%s/%u.bin", data_directory, archive, id) >= (int)size)
        recvxGameBootUnsupported("asset path exceeds its buffer");
}

static int file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    if (!f) { read_status = -1; recvxGameBootUnsupported(path); }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || size > 8 * 1024 * 1024) {
        fclose(f); read_status = -1; recvxGameBootUnsupported("invalid boot asset size");
    }
    fclose(f);
    return (int)size;
}

static int read_file(const char *path, void *destination)
{
    int size = file_size(path);
    FILE *f;
    uintptr_t p = (uintptr_t)destination, start = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    if (!destination || p < start || p > end || (size_t)size > end - p) {
        read_status = -1; recvxGameBootUnsupported("asset read exceeds original arena");
    }
    read_status = 1;
    f = fopen(path, "rb");
    if (!f) { read_status = -1; recvxGameBootUnsupported(path); }
    if (fread(destination, 1, (size_t)size, f) != (size_t)size || ferror(f)) {
        fclose(f); read_status = -1; recvxGameBootUnsupported("short boot asset read");
    }
    fclose(f);
    recvxGameBootRegisterSource(destination, (size_t)size);
    read_status = 0;
    recvxGameBootLog("read %s bytes=%d", path, size);
    return 0;
}

int GetInsideFileSize(unsigned int partition, unsigned int id)
{
    char path[512]; inside_path(path, sizeof(path), partition, id);
    return file_size(path);
}
int RequestReadInsideFile(unsigned int partition, unsigned int id, void *destination)
{
    char path[512]; inside_path(path, sizeof(path), partition, id);
    return read_file(path, destination);
}
int GetReadFileStatus(void) { return read_status; }
void recvxGameBootRegisterSource(void *address, size_t size)
{
    uintptr_t p = (uintptr_t)address, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    if (!address || p < first || p > end || !size || size > end - p)
        recvxGameBootUnsupported("registered asset exceeds original arena");
    ranges[range_cursor & 15u].address = address;
    ranges[range_cursor & 15u].size = size;
    ++range_cursor;
}
static void iso_path(char *path, size_t size, const char *name)
{
    char normalized[12];
    unsigned int i;
    int length;
    if (!name) recvxGameBootUnsupported("missing ISO file name");
    for (i = 0; i < sizeof(normalized); ++i) {
        unsigned char c = (unsigned char)name[i];
        normalized[i] = c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
        if (!c) break;
    }
    if (i == sizeof(normalized)) recvxGameBootUnsupported("unpackaged ISO file request");
    if (!strcmp(normalized, "SYSMES.ALD"))
        length = snprintf(path, size, "%s/files/SYSMES.ALD", data_directory);
    else if (i == 11 && !memcmp(normalized, "RM_", 3) && !strcmp(normalized + 7, ".RDX") &&
             normalized[3] >= '0' && normalized[3] <= '9' && normalized[4] >= '0' && normalized[4] <= '9' &&
             normalized[5] >= '0' && normalized[5] <= '9' && normalized[6] >= '0' && normalized[6] <= '9') {
        /* Cooked rooms retain the exact compressed RDX_LNK.AFS payload. */
        normalized[8] = 'r'; normalized[9] = 'd'; normalized[10] = 'x';
        length = snprintf(path, size, "%s/rooms/%s", data_directory, normalized);
    } else recvxGameBootUnsupported("unpackaged ISO file request");
    if (length < 0 || length >= (int)size) recvxGameBootUnsupported("asset path exceeds its buffer");
}
int GetIsoFileSize(char *name)
{ char path[512]; iso_path(path, sizeof(path), name); return file_size(path); }
int GetFileSize(char *name)
{ return GetIsoFileSize(name); }
int RequestReadIsoFile(char *name, void *destination)
{ char path[512]; iso_path(path, sizeof(path), name); return read_file(path, destination); }
