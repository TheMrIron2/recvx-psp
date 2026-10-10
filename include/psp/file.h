#ifndef RECVX_PSP_FILE_H
#define RECVX_PSP_FILE_H

#include <stddef.h>

typedef struct RecvxFile {
    void *handle;
} RecvxFile;

// Read-only operations return zero on success
int recvxFileOpenRead(RecvxFile *file, const char *path);
int recvxFileRead(RecvxFile *file, void *buffer, size_t capacity,
                  size_t *read_count);
// Line read returns one line, zero at EOF, or -1 on error or overflow
int recvxFileReadLine(RecvxFile *file, char *line, size_t capacity,
                      size_t *length);
int recvxFileSize(RecvxFile *file, size_t *size);
void recvxFileClose(RecvxFile *file);

#endif
