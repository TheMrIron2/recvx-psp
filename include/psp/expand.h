#ifndef RECVX_PSP_EXPAND_H
#define RECVX_PSP_EXPAND_H

#include <stddef.h>
#include "ps2/veronica/prog/expand.h"

typedef enum RecvxExpandResult {
    RECVX_EXPAND_OK = 0,
    RECVX_EXPAND_INVALID_ARGUMENT = -1,
    RECVX_EXPAND_TRUNCATED = -2,
    RECVX_EXPAND_OUTPUT_FULL = -3,
    RECVX_EXPAND_BAD_REFERENCE = -4
} RecvxExpandResult;

/* Decode one stream. Input and output must occupy separate buffers.
 * Counts are optional and report progress on success and failure.
 * Output may be NULL only when its capacity is zero (e.g. an empty stream).
 * Trailing source bytes after the terminator are left unread.
 */
RecvxExpandResult recvxExpandBounded(const void *source, size_t source_size,
                                   void *destination, size_t destination_size,
                                   size_t *consumed, size_t *written);

#endif
