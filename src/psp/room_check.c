#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/expand.h"
#include "psp/room.h"

static uint32_t read_u32(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

int recvxLoadRoomFile(const char *directory, RecvxRoomFileInfo *info,
                      unsigned char **data)
{
    char path[512];
    unsigned char metadata[32];
    unsigned char *packed = NULL, *expanded = NULL;
    FILE *file = NULL;
    size_t consumed, written;
    int result = 2;
    int path_size;

    if (data == NULL)
        return 2;
    *data = NULL;
    if (directory == NULL || info == NULL)
        return 2;
    memset(info, 0, sizeof(*info));
    path_size = snprintf(path, sizeof(path), "%s/room.meta", directory);
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        return 2;
    file = fopen(path, "rb");
    if (file == NULL)
        return 1;
    if (fread(metadata, 1, sizeof(metadata), file) != sizeof(metadata) ||
        fgetc(file) != EOF || ferror(file) || memcmp(metadata, "RCV1", 4) != 0)
        goto done;
    fclose(file);
    file = NULL;
    info->packed_size = read_u32(metadata + 4);
    info->expanded_size = read_u32(metadata + 8);
    info->crc32 = read_u32(metadata + 12);
    memcpy(info->name, metadata + 16, 16);
    info->name[16] = '\0';
    /* Keep the check within the PSP-1000's 16 MiB application heap. */
    if (info->packed_size == 0 || info->expanded_size < 128 ||
        info->packed_size > 8 * 1024 * 1024 ||
        info->expanded_size > 12 * 1024 * 1024 ||
        info->packed_size + info->expanded_size > 14 * 1024 * 1024)
        goto done;
    packed = malloc(info->packed_size);
    expanded = malloc(info->expanded_size);
    if (packed == NULL || expanded == NULL) {
        result = 4;
        goto done;
    }
    path_size = snprintf(path, sizeof(path), "%s/room.rdx", directory);
    result = 3;
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        goto done;
    file = fopen(path, "rb");
    if (file == NULL || fread(packed, 1, info->packed_size, file) != info->packed_size ||
        fgetc(file) != EOF || ferror(file))
        goto done;
    result = 5;
    if (recvxExpandBounded(packed, info->packed_size, expanded, info->expanded_size,
                          &consumed, &written) != RECVX_EXPAND_OK ||
        written != info->expanded_size || consumed != info->packed_size)
        goto done;
    result = 6;
    if (recvxRoomCrc32(expanded, written) != info->crc32)
        goto done;
    result = 7;
    if (recvxInspectRoom(expanded, written, &info->room) != 0)
        goto done;
    result = 0;

done:
    if (file != NULL)
        fclose(file);
    if (result == 0) {
        *data = expanded;
        expanded = NULL;
    }
    free(expanded);
    free(packed);
    return result;
}

int recvxCheckRoomFile(const char *directory, RecvxRoomFileInfo *info)
{
    unsigned char *data = NULL;
    int result = recvxLoadRoomFile(directory, info, &data);
    free(data);
    return result;
}
