#include <stdio.h>

#include "psp/file.h"

int recvxFileOpenRead(RecvxFile *file, const char *path)
{
    FILE *stream;
    if (file == NULL || path == NULL)
        return -1;
    file->handle = NULL;
    stream = fopen(path, "rb");
    if (stream == NULL)
        return -1;
    file->handle = stream;
    return 0;
}

int recvxFileRead(RecvxFile *file, void *buffer, size_t capacity,
                  size_t *read_count)
{
    FILE *stream;
    size_t count;
    if (file == NULL || file->handle == NULL ||
        (buffer == NULL && capacity != 0) || read_count == NULL)
        return -1;
    if (capacity == 0) {
        *read_count = 0;
        return 0;
    }
    stream = file->handle;
    count = fread(buffer, 1, capacity, stream);
    if (ferror(stream))
        return -1;
    *read_count = count;
    return 0;
}

int recvxFileReadLine(RecvxFile *file, char *line, size_t capacity,
                      size_t *length)
{
    FILE *stream;
    size_t used = 0;
    int character;
    if (file == NULL || file->handle == NULL || line == NULL ||
        capacity < 2 || length == NULL)
        return -1;
    stream = file->handle;
    while ((character = fgetc(stream)) != EOF) {
        if (character == '\n') {
            if (used != 0 && line[used - 1] == '\r')
                --used;
            line[used] = '\0';
            *length = used;
            return 1;
        }
        if (used + 1 >= capacity)
            return -1;
        line[used++] = (char)character;
    }
    if (ferror(stream))
        return -1;
    if (used == 0)
        return 0;
    if (line[used - 1] == '\r')
        --used;
    line[used] = '\0';
    *length = used;
    return 1;
}

int recvxFileSize(RecvxFile *file, size_t *size)
{
    FILE *stream;
    long end;
    long original;
    if (file == NULL || file->handle == NULL || size == NULL)
        return -1;
    stream = file->handle;
    original = ftell(stream);
    if (original < 0 || fseek(stream, 0, SEEK_END) != 0)
        return -1;
    end = ftell(stream);
    if (end < 0 || fseek(stream, original, SEEK_SET) != 0)
        return -1;
    *size = (size_t)end;
    return 0;
}

void recvxFileClose(RecvxFile *file)
{
    if (file == NULL || file->handle == NULL)
        return;
    fclose((FILE *)file->handle);
    file->handle = NULL;
}
