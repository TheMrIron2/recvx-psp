#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/expand.h"
#include "psp/file.h"
#include "psp/room.h"

static uint32_t read_u32(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int valid_room_name(const char *name)
{
    unsigned int i;
    if (name == NULL || strlen(name) != 11 || memcmp(name, "RM_", 3) != 0 ||
        memcmp(name + 7, ".RDX", 4) != 0)
        return 0;
    for (i = 3; i < 7; ++i)
        if (name[i] < '0' || name[i] > '9')
            return 0;
    return 1;
}

int recvxLoadRoomCatalog(const char *directory,
                         char (*names)[RECVX_ROOM_NAME_SIZE], size_t capacity,
                         size_t *count)
{
    char path[512], line[64];
    RecvxFile file = {0};
    size_t used = 0;
    size_t line_length;
    int path_size;
    int line_result;
    if (count == NULL)
        return 2;
    *count = 0;
    if (directory == NULL || names == NULL || capacity == 0)
        return 2;
    path_size = snprintf(path, sizeof(path), "%s/rooms.txt", directory);
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        return 2;
    if (recvxFileOpenRead(&file, path) != 0)
        return 1;
    while ((line_result = recvxFileReadLine(&file, line, sizeof(line),
                                             &line_length)) > 0) {
        size_t i;
        if (line_length == 0)
            continue;
        if (!valid_room_name(line) || used == capacity)
            goto invalid;
        for (i = 0; i < used; ++i)
            if (strcmp(names[i], line) == 0)
                goto invalid;
        memcpy(names[used++], line, line_length + 1);
    }
    if (line_result < 0 || used == 0)
        goto invalid;
    recvxFileClose(&file);
    *count = used;
    return 0;

invalid:
    recvxFileClose(&file);
    return 2;
}

static int load_room_file(const char *directory, const char *name,
                          RecvxRoomFileInfo *info, unsigned char **data)
{
    char path[512];
    unsigned char metadata[32];
    unsigned char *packed = NULL, *expanded = NULL;
    RecvxFile file = {0};
    size_t consumed, written, read_count;
    unsigned char extra;
    int result = 2;
    int path_size;

    if (data == NULL)
        return 2;
    *data = NULL;
    if (directory == NULL || info == NULL || (name != NULL && !valid_room_name(name)))
        return 2;
    memset(info, 0, sizeof(*info));
    if (name != NULL)
        path_size = snprintf(path, sizeof(path), "%s/rooms/%.*s.meta", directory, 7, name);
    else
        path_size = snprintf(path, sizeof(path), "%s/room.meta", directory);
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        return 2;
    if (recvxFileOpenRead(&file, path) != 0)
        return 1;
    if (recvxFileRead(&file, metadata, sizeof(metadata), &read_count) != 0 ||
        read_count != sizeof(metadata) ||
        recvxFileRead(&file, &extra, 1, &read_count) != 0 || read_count != 0 ||
        memcmp(metadata, "RCV1", 4) != 0)
        goto done;
    recvxFileClose(&file);
    info->packed_size = read_u32(metadata + 4);
    info->expanded_size = read_u32(metadata + 8);
    info->crc32 = read_u32(metadata + 12);
    memcpy(info->name, metadata + 16, 16);
    info->name[16] = '\0';
    if (name != NULL && strcmp(info->name, name) != 0)
        goto done;
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
    if (name != NULL)
        path_size = snprintf(path, sizeof(path), "%s/rooms/%.*s.rdx", directory, 7, name);
    else
        path_size = snprintf(path, sizeof(path), "%s/room.rdx", directory);
    result = 3;
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        goto done;
    if (recvxFileOpenRead(&file, path) != 0 ||
        recvxFileRead(&file, packed, info->packed_size, &read_count) != 0 ||
        read_count != info->packed_size ||
        recvxFileRead(&file, &extra, 1, &read_count) != 0 || read_count != 0)
        goto done;
    recvxFileClose(&file);
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
    recvxFileClose(&file);
    if (result == 0) {
        *data = expanded;
        expanded = NULL;
    }
    free(expanded);
    free(packed);
    return result;
}

int recvxLoadRoomFile(const char *directory, RecvxRoomFileInfo *info,
                      unsigned char **data)
{
    return load_room_file(directory, NULL, info, data);
}

int recvxLoadNamedRoomFile(const char *directory, const char *name,
                           RecvxRoomFileInfo *info, unsigned char **data)
{
    return load_room_file(directory, name, info, data);
}

static int load_room_data(const char *directory, const char *name,
                          RecvxRoomFileInfo *info, RecvxRoomData *data)
{
    int result;
    if (data == NULL)
        return 2;
    data->bytes = NULL;
    data->size = 0;
    result = load_room_file(directory, name, info, &data->bytes);
    if (result == 0)
        data->size = info->expanded_size;
    return result;
}

int recvxLoadRoomData(const char *directory, RecvxRoomFileInfo *info,
                      RecvxRoomData *data)
{
    return load_room_data(directory, NULL, info, data);
}

int recvxLoadNamedRoomData(const char *directory, const char *name,
                           RecvxRoomFileInfo *info, RecvxRoomData *data)
{
    return load_room_data(directory, name, info, data);
}

void recvxRoomDataFree(RecvxRoomData *data)
{
    if (data == NULL)
        return;
    free(data->bytes);
    data->bytes = NULL;
    data->size = 0;
}

int recvxCheckRoomFile(const char *directory, RecvxRoomFileInfo *info)
{
    unsigned char *data = NULL;
    int result = recvxLoadRoomFile(directory, info, &data);
    free(data);
    return result;
}
